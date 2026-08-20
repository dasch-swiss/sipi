//! The `helper` table and the sole-compatible UUID/base62 implementation.
//!
//! The base62 codec is a byte-faithful port of sole 1.0.1's `uuid::base62` /
//! `rebuild` — it is load-bearing: base62 UUIDs are the DSP/Knora IRI scheme,
//! pinned by golden vectors against the C++ outputs. One deliberate
//! divergence: `rebuild` accepted any bytes and produced garbage for
//! malformed input; the Rust decoder rejects it with the `(false, msg)`
//! error shape instead.

use mlua::Table;

use crate::runtime::RequestVm;

/// A sole-layout UUID: two big-endian u64 halves of the 128-bit value.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SoleUuid {
    pub ab: u64,
    pub cd: u64,
}

const BASE62: &[u8; 62] = b"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

impl SoleUuid {
    /// A random v4 UUID (version/variant bits per sole's `uuid4`).
    pub fn new_v4() -> Self {
        let ab: u64 = rand::random();
        let cd: u64 = rand::random();
        Self {
            ab: (ab & 0xFFFF_FFFF_FFFF_0FFF) | 0x0000_0000_0000_4000,
            cd: (cd & 0x3FFF_FFFF_FFFF_FFFF) | 0x8000_0000_0000_0000,
        }
    }

    /// Standard hyphenated lowercase hex (sole's `uuid::str()`).
    pub fn hyphenated(&self) -> String {
        format!(
            "{:08x}-{:04x}-{:04x}-{:04x}-{:012x}",
            self.ab >> 32,
            (self.ab >> 16) & 0xFFFF,
            self.ab & 0xFFFF,
            self.cd >> 48,
            self.cd & 0xFFFF_FFFF_FFFF
        )
    }

    /// sole's base62 form: `base62(ab) + "-" + base62(cd)`, each half
    /// most-significant-digit first, no padding.
    pub fn base62(&self) -> String {
        fn encode(mut v: u64, out: &mut Vec<u8>) {
            let mut digits = [0u8; 11];
            let mut n = 0;
            loop {
                digits[n] = BASE62[(v % 62) as usize];
                v /= 62;
                n += 1;
                if v == 0 {
                    break;
                }
            }
            out.extend(digits[..n].iter().rev());
        }
        let mut out = Vec::with_capacity(23);
        encode(self.ab, &mut out);
        out.push(b'-');
        encode(self.cd, &mut out);
        String::from_utf8(out).expect("base62 alphabet is ASCII")
    }

    /// Parses the hyphenated hex form. `None` for anything malformed.
    pub fn from_hyphenated(s: &str) -> Option<Self> {
        let b = s.as_bytes();
        if b.len() != 36 || b[8] != b'-' || b[13] != b'-' || b[18] != b'-' || b[23] != b'-' {
            return None;
        }
        let hex: String = s.chars().filter(|c| *c != '-').collect();
        if hex.len() != 32 {
            return None;
        }
        let ab = u64::from_str_radix(&hex[..16], 16).ok()?;
        let cd = u64::from_str_radix(&hex[16..], 16).ok()?;
        Some(Self { ab, cd })
    }

    /// Parses the base62 form (`AB-CD`, both halves non-empty, alphabet
    /// strict). `None` for anything malformed — a divergence from sole's
    /// garbage-in-garbage-out `rebuild`.
    pub fn from_base62(s: &str) -> Option<Self> {
        let (ab_part, cd_part) = s.split_once('-')?;
        if cd_part.contains('-') {
            return None;
        }
        fn decode(part: &str) -> Option<u64> {
            if part.is_empty() || part.len() > 11 {
                return None;
            }
            let mut v: u64 = 0;
            for &b in part.as_bytes() {
                let digit = match b {
                    b'0'..=b'9' => b - b'0',
                    b'A'..=b'Z' => b - b'A' + 10,
                    b'a'..=b'z' => b - b'a' + 10 + 26,
                    _ => return None,
                } as u64;
                v = v.checked_mul(62)?.checked_add(digit)?;
            }
            Some(v)
        }
        Some(Self {
            ab: decode(ab_part)?,
            cd: decode(cd_part)?,
        })
    }
}

/// Installs the `helper` table's functions.
pub fn install(vm: &RequestVm, helper: &Table) -> mlua::Result<()> {
    vm.register_binding("helper", helper, "filename_hash", filename_hash)
}

/// `helper.filename_hash` — the storage-path derivation over the engine's
/// `SipiFilenameHash` (byte-identical: it derives on-disk layout).
fn filename_hash(
    lua: &mlua::Lua,
    args: mlua::Variadic<mlua::Value>,
) -> mlua::Result<mlua::MultiValue> {
    use mlua::Value;
    let fail = |msg: String| -> mlua::Result<mlua::MultiValue> {
        Ok(mlua::MultiValue::from_iter([
            Value::Boolean(false),
            Value::String(lua.create_string(msg)?),
        ]))
    };
    if args.is_empty() {
        return fail("'helper.hash(filename)': parameter missing".into());
    }
    let name = match args.first() {
        Some(Value::String(s)) => s.to_string_lossy(),
        Some(Value::Integer(i)) => i.to_string(),
        Some(Value::Number(n)) => n.to_string(),
        _ => return fail("'helper.hash(filename)': filename is not a string".into()),
    };
    match crate::engine_ffi::filename_hash(&name) {
        Err(msg) => fail(format!("'helper.hash(filename)': {msg}")),
        Ok(path) => Ok(mlua::MultiValue::from_iter([
            Value::Boolean(true),
            Value::String(lua.create_string(path)?),
        ])),
    }
}

#[cfg(test)]
mod tests {
    use super::SoleUuid;

    // Golden vectors computed with sole 1.0.1 (the exact C++ implementation
    // this ports — `rebuild(hex).base62()` and the reverse round-trip).
    const VECTORS: &[(&str, &str)] = &[
        // (hyphenated, base62)
        (
            "f81d4fae-7dec-11d0-a765-00a0c91e6bf6",
            "LIhsBrTE21A-EN2J2swqbwM",
        ),
        ("00000000-0000-4000-8000-000000000000", "4GG-AzL8n0Y58m8"),
        (
            "ffffffff-ffff-ffff-ffff-ffffffffffff",
            "LygHa16AHYF-LygHa16AHYF",
        ),
        ("00000000-0000-0000-0000-000000000001", "0-1"),
        (
            "3e0b9e33-3a3c-4f5d-9b1a-000c29e6b0f1",
            "5KGUww0bpUr-DJbK14CVyQj",
        ),
    ];

    #[test]
    fn base62_matches_sole_golden_vectors() {
        for (hex, b62) in VECTORS {
            let u = SoleUuid::from_hyphenated(hex).expect(hex);
            assert_eq!(u.base62(), *b62, "{hex}");
            let back = SoleUuid::from_base62(b62).expect(b62);
            assert_eq!(back, u, "{b62}");
            assert_eq!(back.hyphenated(), *hex);
        }
    }

    #[test]
    fn v4_has_version_and_variant_bits() {
        for _ in 0..64 {
            let u = SoleUuid::new_v4();
            assert_eq!((u.ab >> 12) & 0xF, 4, "version nibble");
            assert_eq!(u.cd >> 62, 0b10, "variant bits");
            let s = u.hyphenated();
            assert_eq!(s.len(), 36);
            assert_eq!(SoleUuid::from_hyphenated(&s), Some(u));
            assert_eq!(SoleUuid::from_base62(&u.base62()), Some(u));
        }
    }

    #[test]
    fn malformed_inputs_are_rejected() {
        for bad in [
            "",
            "no-hyphens-here-at-all!",
            "f81d4fae-7dec-11d0-a765-00a0c91e6bf", // one nibble short
            "zzz",
            "abc-def-ghi",
            "abc_def",
            "-abc",
            "abc-",
            "LygHa16AHYZ-0", // overflows u64
        ] {
            assert!(
                SoleUuid::from_base62(bad).is_none() || bad.len() == 36,
                "{bad} should be rejected by from_base62"
            );
        }
        assert!(SoleUuid::from_hyphenated("f81d4fae7dec11d0a76500a0c91e6bf6").is_none());
        assert!(SoleUuid::from_hyphenated("f81d4fae-7dec-11d0-a765-00a0c91e6bfg").is_none());
    }
}
