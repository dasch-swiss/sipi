//! Burst-coalescing cache for the Lua `pre_flight` access decision.
//!
//! `dispatch_engine` runs the `pre_flight` hook on every IIIF request, before the
//! engine image cache is even consulted (`routes::iiif_access`). Each call is a
//! blocking FFI + Lua VM round-trip that, on DSP, does an HTTP call to dsp-api and
//! a SPARQL query (~20-40ms). A viewer reopening the tiles of one deep-zoom image
//! fires that hook once per tile, and every tile of one image shares the same
//! `identifier`. This cache serves the recorded decision for a repeated
//! `(prefix, identifier, credential)` instead of re-running the hook.
//!
//! **Opt-in.** The cache is disabled by default (TTL 0); a deployment enables it
//! with `--preflight-cache-ttl <secs>` / `SIPI_PREFLIGHT_CACHE_TTL` only once its
//! `pre_flight` hook is known to satisfy the key contract below.
//!
//! **Key contract — the caller must honour this.** The key is only
//! `(prefix, identifier, Cookie, Authorization)`. But the hook does not merely
//! receive those as arguments: `build_ctx` hands it the whole request, and the Lua
//! `server.*` bindings expose the full path (`server.uri`), every header
//! (`server.header[...]`, e.g. an `X-Api-Key`), the host, and the client IP. A
//! hook whose decision depends on any of those unkeyed fields will be served a
//! stale or wrong cached decision for a different request that shares only the
//! keyed fields. The cache is therefore correct **only** for a hook whose decision
//! is a pure function of `(prefix, identifier, Cookie, Authorization)`; that is a
//! constraint on the deployed hook, not a property the seam guarantees. See
//! `docs/src/lua/index.md`.
//!
//! **This is a burst-coalescing cache, not a durable auth store.** The TTL is the
//! staleness bound on a permission change (revocation, token expiry): a cached
//! decision can be served for at most `ttl` after the hook last ran. It coalesces
//! requests spread across the TTL window; it does **not** single-flight a
//! genuinely concurrent burst — concurrent requests that all miss before the first
//! insert lands each run the hook independently.
//!
//! Correctness guardrails:
//! - `credential` is the raw `Cookie` + `Authorization` header bytes. A present but
//!   empty header keys distinctly from an absent one (a presence marker precedes
//!   each), so two different credentials never share an entry.
//! - The `u64` fingerprint is only a bucket index / fast-reject; on a fingerprint
//!   match the full key bytes are compared before the entry is trusted, so a hash
//!   collision can never leak one user's `Allow` to another.
//! - Both positive and negative decisions are cached (same TTL). A hook that wrote
//!   its own response (`direct_response`) is request-specific and is never cached
//!   (the caller only inserts a plain `outcome`).
//!
//! The table is a fixed set of slots allocated once at startup and reused in place
//! (per-request key bytes are still allocated by [`make_key`]): `NUM_SHARDS` shards
//! (each a `Mutex<Box<[Option<Slot>]>>`), open addressing with a short linear
//! probe, evicting the earliest-expiring slot when a probe window is full. The live
//! working set (distinct `(image, user)` pairs in a burst) is tiny, so the table is
//! sized for the hot set, not to fill L3.

use std::collections::hash_map::DefaultHasher;
use std::hash::{Hash, Hasher};
use std::sync::atomic::{AtomicU64, AtomicUsize, Ordering};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

use crate::ffi::SipiPermType;

/// Fixed shard count (power of two so the fingerprint can pick a shard by masking).
const NUM_SHARDS: usize = 16;
/// Linear-probe window within a shard before falling back to earliest-expiry eviction.
const PROBE: usize = 4;
/// Default slot count when `--preflight-cache-slots` is unset.
pub const DEFAULT_SLOTS: usize = 4096;
/// Default TTL in seconds when `--preflight-cache-ttl` is unset: 0 = disabled.
/// The cache is opt-in; a deployment enables it only once its `pre_flight` hook
/// satisfies the key contract in the module docs.
pub const DEFAULT_TTL_SECS: u64 = 0;

// ── Metrics (process-global; read by the OTel bridge in `crate::metrics`) ────
// Plain atomics, matching the pool counters in `routes.rs`: the hot path holds no
// meter handle and the counts are correct regardless of whether export is on.

static HITS: AtomicU64 = AtomicU64::new(0);
static MISSES: AtomicU64 = AtomicU64::new(0);
/// Count of filled slots (`Some(_)`) across all shards. This includes entries
/// whose TTL has expired but that have not yet been looked up (and so evicted) or
/// overwritten — expiry is lazy, there is no sweep. So the count trends toward the
/// configured slot ceiling on a busy server and is an occupancy-vs-capacity signal,
/// not a live/non-expired working-set gauge.
static ENTRIES: AtomicUsize = AtomicUsize::new(0);

/// Cumulative preflight-cache hits, for the `sipi.preflight_cache.hits` counter.
pub(crate) fn hits() -> u64 {
    HITS.load(Ordering::Relaxed)
}

/// Cumulative preflight-cache misses, for the `sipi.preflight_cache.misses` counter.
pub(crate) fn misses() -> u64 {
    MISSES.load(Ordering::Relaxed)
}

/// Filled-slot count (incl. expired-but-not-yet-reclaimed), for the
/// `sipi.preflight_cache.entries` gauge. See the `ENTRIES` doc for what it means.
pub(crate) fn entries() -> i64 {
    // Bounded by the fixed slot count (a small config value), so the cast is safe.
    ENTRIES.load(Ordering::Relaxed) as i64
}

/// The cacheable part of a `PreflightOutcome`: the permission plus the kv channel
/// (`infile`, restrict `size`/`watermark`, auth-service urls). Cloned in and out.
#[derive(Clone)]
pub struct CachedDecision {
    pub permission: SipiPermType,
    pub kv: Vec<(String, String)>,
}

/// A lookup key: the exact `(prefix, identifier, credential)` bytes plus a
/// precomputed fingerprint. Built once per request via [`make_key`] and passed to
/// both [`PreflightCache::get`] and [`PreflightCache::insert`].
pub struct KeyBuf {
    bytes: Box<[u8]>,
    fingerprint: u64,
}

/// Append an optional credential field with a 1-byte presence marker (`1` = the
/// header was present, `0` = absent) so `Some("")` and `None` produce distinct
/// bytes and never collide in the key.
fn push_optional(bytes: &mut Vec<u8>, value: Option<&str>) {
    match value {
        Some(v) => {
            bytes.push(1);
            bytes.extend_from_slice(v.as_bytes());
        }
        None => bytes.push(0),
    }
}

/// Build a cache key from the preflight inputs. `cookie` / `authorization` are the
/// raw header values (the credential). A present-but-empty header (`Some("")`) is
/// kept distinct from an absent one (`None`) by a leading presence marker, so a
/// client sending an empty `Authorization:` never shares an entry with an
/// anonymous request. The identifier and prefix never contain a NUL (rejected
/// upstream in `routes::iiif`, see `routes.rs`), so the NUL separators make the
/// concatenation unambiguous; the credentials are last, so their arbitrary bytes
/// cannot be confused with an earlier field.
#[must_use]
pub fn make_key(
    prefix: &str,
    identifier: &str,
    cookie: Option<&str>,
    authorization: Option<&str>,
) -> KeyBuf {
    let mut bytes = Vec::with_capacity(
        prefix.len()
            + identifier.len()
            + cookie.map_or(0, str::len)
            + authorization.map_or(0, str::len)
            + 5,
    );
    bytes.extend_from_slice(prefix.as_bytes());
    bytes.push(0);
    bytes.extend_from_slice(identifier.as_bytes());
    bytes.push(0);
    push_optional(&mut bytes, cookie);
    bytes.push(0);
    push_optional(&mut bytes, authorization);

    let mut hasher = DefaultHasher::new();
    bytes.hash(&mut hasher);
    let fingerprint = hasher.finish();

    KeyBuf {
        bytes: bytes.into_boxed_slice(),
        fingerprint,
    }
}

/// One occupied slot: the fingerprint (fast-reject), the exact key bytes (trusted
/// compare), the expiry instant, and the cached decision.
struct Slot {
    fingerprint: u64,
    key: Box<[u8]>,
    expiry: Instant,
    decision: CachedDecision,
}

/// A fixed-slot, sharded, TTL cache for preflight decisions. Allocated once; slots
/// are reused in place. Cheap to share behind an `Arc` (stored in `AppState`).
/// One shard of the table: a fixed slab of open-addressing slots behind a lock.
type Shard = Mutex<Box<[Option<Slot>]>>;

pub struct PreflightCache {
    shards: Box<[Shard]>,
    slots_per_shard: usize,
    ttl: Duration,
}

impl PreflightCache {
    /// Build a cache with roughly `slots` total slots (rounded to a multiple of
    /// `NUM_SHARDS`, at least one per shard) and the given TTL. Returns `None` when
    /// `ttl` is zero — the disabled state — so callers store `Option<Arc<_>>` and
    /// skip all cache work when absent.
    #[must_use]
    pub fn new(slots: usize, ttl: Duration) -> Option<Arc<Self>> {
        if ttl.is_zero() {
            return None;
        }
        let slots_per_shard = slots.div_ceil(NUM_SHARDS).max(1);
        let shards = (0..NUM_SHARDS)
            .map(|_| {
                let slots: Vec<Option<Slot>> = (0..slots_per_shard).map(|_| None).collect();
                Mutex::new(slots.into_boxed_slice())
            })
            .collect::<Vec<_>>()
            .into_boxed_slice();
        Some(Arc::new(Self {
            shards,
            slots_per_shard,
            ttl,
        }))
    }

    fn shard(&self, key: &KeyBuf) -> &Shard {
        // Low bits pick the shard; higher bits pick the base slot within it.
        &self.shards[(key.fingerprint as usize) & (NUM_SHARDS - 1)]
    }

    fn base_index(&self, key: &KeyBuf) -> usize {
        ((key.fingerprint >> 4) as usize) % self.slots_per_shard
    }

    /// Look up a live decision for `key`. Bumps the hit/miss counters. An expired
    /// entry is evicted on encounter (and counted as a miss).
    #[must_use]
    pub fn get(&self, key: &KeyBuf, now: Instant) -> Option<CachedDecision> {
        let mut shard = self.shard(key).lock().unwrap_or_else(|e| e.into_inner());
        let base = self.base_index(key);
        for i in 0..PROBE {
            let idx = (base + i) % self.slots_per_shard;
            match &shard[idx] {
                Some(slot) if slot.fingerprint == key.fingerprint && slot.key == key.bytes => {
                    if slot.expiry > now {
                        let decision = slot.decision.clone();
                        HITS.fetch_add(1, Ordering::Relaxed);
                        return Some(decision);
                    }
                    // Expired: evict so the slot is reusable and the gauge is honest.
                    shard[idx] = None;
                    ENTRIES.fetch_sub(1, Ordering::Relaxed);
                    MISSES.fetch_add(1, Ordering::Relaxed);
                    return None;
                }
                _ => {}
            }
        }
        MISSES.fetch_add(1, Ordering::Relaxed);
        None
    }

    /// Store `decision` for `key`, expiring `ttl` from `now`. Overwrites an
    /// existing entry for the same key in place; otherwise fills the first free
    /// (empty or expired) slot in the probe window; otherwise evicts the
    /// earliest-expiring slot in the window.
    pub fn insert(&self, key: &KeyBuf, decision: CachedDecision, now: Instant) {
        let expiry = now + self.ttl;
        let mut shard = self.shard(key).lock().unwrap_or_else(|e| e.into_inner());
        let base = self.base_index(key);

        let mut free: Option<usize> = None;
        let mut oldest: Option<(usize, Instant)> = None;
        for i in 0..PROBE {
            let idx = (base + i) % self.slots_per_shard;
            match &shard[idx] {
                Some(slot) if slot.fingerprint == key.fingerprint && slot.key == key.bytes => {
                    // Same key → refresh in place (occupancy unchanged).
                    shard[idx] = Some(Slot {
                        fingerprint: key.fingerprint,
                        key: key.bytes.clone(),
                        expiry,
                        decision,
                    });
                    return;
                }
                Some(slot) => {
                    if slot.expiry <= now {
                        // An expired slot is as good as free (occupancy unchanged
                        // when we reuse it).
                        free.get_or_insert(idx);
                    }
                    let older = match oldest {
                        Some((_, e)) => slot.expiry < e,
                        None => true,
                    };
                    if older {
                        oldest = Some((idx, slot.expiry));
                    }
                }
                None => {
                    // Truly empty → filling it increases occupancy.
                    shard[idx] = Some(Slot {
                        fingerprint: key.fingerprint,
                        key: key.bytes.clone(),
                        expiry,
                        decision,
                    });
                    ENTRIES.fetch_add(1, Ordering::Relaxed);
                    return;
                }
            }
        }

        // No empty slot in the window: reuse an expired one if we saw it (occupancy
        // unchanged), else evict the earliest-expiring occupied slot (also
        // unchanged — we replace a `Some` with a `Some`).
        let victim = free
            .or(oldest.map(|(i, _)| i))
            .unwrap_or(base % self.slots_per_shard);
        shard[victim] = Some(Slot {
            fingerprint: key.fingerprint,
            key: key.bytes.clone(),
            expiry,
            decision,
        });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn decision(perm: SipiPermType, infile: &str) -> CachedDecision {
        CachedDecision {
            permission: perm,
            kv: vec![("infile".to_owned(), infile.to_owned())],
        }
    }

    /// Serializes every test that touches the process-global HITS/MISSES/ENTRIES
    /// counters: Rust runs a binary's tests in parallel, so without this a sibling
    /// test's `get`/`insert` would perturb the absolute counts the counter/entries
    /// assertions check. Held for the duration of each such test via the guard
    /// returned by [`reset_metrics`].
    static SERIAL: Mutex<()> = Mutex::new(());

    /// Acquire the shared serialization lock (recovering from a poisoned mutex so
    /// one panicking test does not cascade), then zero the global counters. The
    /// caller must bind the returned guard (`let _serial = reset_metrics();`) so it
    /// is held for the whole test body (the returned `MutexGuard` is itself
    /// `#[must_use]`, so a discarded call is caught).
    fn reset_metrics() -> std::sync::MutexGuard<'static, ()> {
        let guard = SERIAL.lock().unwrap_or_else(|e| e.into_inner());
        HITS.store(0, Ordering::Relaxed);
        MISSES.store(0, Ordering::Relaxed);
        ENTRIES.store(0, Ordering::Relaxed);
        guard
    }

    #[test]
    fn hit_returns_the_inserted_decision() {
        let _serial = reset_metrics();
        let cache = PreflightCache::new(64, Duration::from_secs(2)).expect("enabled");
        let now = Instant::now();
        let key = make_key("0803", "abc.jp2", Some("session=x"), None);
        cache.insert(&key, decision(SipiPermType::Allow, "/img/abc.jp2"), now);

        let got = cache.get(&key, now).expect("hit");
        assert_eq!(got.permission, SipiPermType::Allow);
        assert_eq!(
            got.kv,
            vec![("infile".to_owned(), "/img/abc.jp2".to_owned())]
        );
    }

    #[test]
    fn distinct_credentials_do_not_share_an_entry() {
        let _serial = reset_metrics();
        let cache = PreflightCache::new(64, Duration::from_secs(2)).expect("enabled");
        let now = Instant::now();
        let alice = make_key("0803", "abc.jp2", Some("session=alice"), None);
        let bob = make_key("0803", "abc.jp2", Some("session=bob"), None);

        cache.insert(&alice, decision(SipiPermType::Allow, "/img/abc.jp2"), now);
        // Bob has a different credential → must miss, not inherit Alice's Allow.
        assert!(cache.get(&bob, now).is_none());
        // Anonymous (no credential) is a third distinct key.
        let anon = make_key("0803", "abc.jp2", None, None);
        assert!(cache.get(&anon, now).is_none());
    }

    #[test]
    fn different_identifiers_key_separately() {
        let _serial = reset_metrics();
        let cache = PreflightCache::new(64, Duration::from_secs(2)).expect("enabled");
        let now = Instant::now();
        let a = make_key("0803", "a.jp2", Some("s=1"), None);
        let b = make_key("0803", "b.jp2", Some("s=1"), None);
        cache.insert(&a, decision(SipiPermType::Deny, ""), now);
        assert!(cache.get(&b, now).is_none());
    }

    #[test]
    fn entry_expires_after_ttl() {
        let _serial = reset_metrics();
        let ttl = Duration::from_secs(2);
        let cache = PreflightCache::new(64, ttl).expect("enabled");
        let now = Instant::now();
        let key = make_key("0803", "abc.jp2", None, None);
        cache.insert(&key, decision(SipiPermType::Allow, "/img/abc.jp2"), now);

        assert!(cache.get(&key, now + Duration::from_millis(500)).is_some());
        assert!(cache
            .get(&key, now + ttl + Duration::from_millis(1))
            .is_none());
    }

    #[test]
    fn negative_decisions_are_cached() {
        let _serial = reset_metrics();
        let cache = PreflightCache::new(64, Duration::from_secs(2)).expect("enabled");
        let now = Instant::now();
        let key = make_key("0803", "secret.jp2", Some("s=1"), None);
        cache.insert(&key, decision(SipiPermType::Deny, ""), now);
        assert_eq!(
            cache.get(&key, now).expect("hit").permission,
            SipiPermType::Deny
        );
    }

    #[test]
    fn zero_ttl_disables() {
        assert!(PreflightCache::new(64, Duration::ZERO).is_none());
    }

    #[test]
    fn refresh_in_place_extends_expiry_without_growing_occupancy() {
        let _serial = reset_metrics();
        let ttl = Duration::from_secs(2);
        let cache = PreflightCache::new(64, ttl).expect("enabled");
        let now = Instant::now();
        let key = make_key("0803", "abc.jp2", None, None);
        cache.insert(&key, decision(SipiPermType::Allow, "/img/abc.jp2"), now);
        let entries_after_first = entries();
        // Re-insert the same key later: refreshes in place, occupancy unchanged.
        cache.insert(
            &key,
            decision(SipiPermType::Allow, "/img/abc.jp2"),
            now + Duration::from_secs(1),
        );
        assert_eq!(entries(), entries_after_first);
        // The refreshed entry lives ttl past the second insert.
        assert!(cache.get(&key, now + Duration::from_millis(2500)).is_some());
    }

    #[test]
    fn hit_and_miss_counters_move() {
        let _serial = reset_metrics();
        let cache = PreflightCache::new(64, Duration::from_secs(2)).expect("enabled");
        let now = Instant::now();
        let key = make_key("0803", "abc.jp2", None, None);
        assert!(cache.get(&key, now).is_none());
        assert_eq!(misses(), 1);
        cache.insert(&key, decision(SipiPermType::Allow, "/x"), now);
        assert!(cache.get(&key, now).is_some());
        assert_eq!(hits(), 1);
    }
}
