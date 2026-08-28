/* Reads mimalloc's allocator accounting for the `sipi.malloc.*` gauges.
 *
 * Deliberately a C translation unit compiled against the vendored
 * `mimalloc-stats.h` (the same @mimalloc tree the allocator links from), so
 * the `mi_stats_get` contract — signature, `mi_stats_t` layout, and the
 * caller-filled `size`/`version` handshake — is checked by the C compiler
 * instead of being hand-mirrored in Rust, where a declaration drifting from
 * the pinned mimalloc version is a runtime SIGSEGV on the metrics thread
 * (SIPI-1R), not a build error. A mimalloc bump that changes the stats API
 * must break this file's compile, never production.
 */

#include <stdbool.h>
#include <stdint.h>

#include <mimalloc-stats.h>
#include <mimalloc.h>

/* The three figures `sipi::malloc_stats::MallocStats` is derived from (the
 * field mapping lives with the Rust caller): live normal/huge malloc bytes
 * from `mi_stats_get`, and the process resident set size from
 * `mi_process_info`. RSS is the true OS-resident figure the `sipi.malloc.*`
 * gauges report, unlike mimalloc's `committed` (which ratchets and reads far
 * above RSS, so it misleads as an OOM/RSS proxy — SIPI RSS-ratchet learning).
 * Returns false when `mi_stats_get` rejects the size/version handshake —
 * impossible while shim and allocator compile from the same tree, but it is
 * the documented contract, so surface it instead of returning zeros. */
bool sipi_mi_stats_read(int64_t* malloc_normal_current,
                        int64_t* malloc_huge_current,
                        int64_t* rss_current) {
  mi_stats_t stats;
  stats.size = sizeof(mi_stats_t);
  stats.version = MI_STAT_VERSION;
  if (!mi_stats_get(&stats)) {
    return false;
  }
  *malloc_normal_current = stats.malloc_normal.current;
  *malloc_huge_current = stats.malloc_huge.current;

  /* mi_process_info fills every out-param; we keep only current_rss. */
  size_t elapsed, user, sys, current_rss, peak_rss, current_commit, peak_commit, faults;
  mi_process_info(&elapsed, &user, &sys, &current_rss, &peak_rss, &current_commit, &peak_commit,
                  &faults);
  *rss_current = (int64_t)current_rss;
  return true;
}
