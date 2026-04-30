#!/bin/bash
#
# Filters Apple-libtool flags out of `ar` invocations and forwards to
# `/usr/bin/ar`. Used as the `AR` env override for autotools/foreign_cc
# rules on darwin.
#
# Why this exists: toolchains_llvm 1.7.0 hard-codes `archiver_flags=["-static"]`
# in its darwin cc_toolchain_config, because the toolchain's own archiver is
# Apple `libtool` (`libtool -static -o foo.a *.o`). rules_foreign_cc reads
# the toolchain's archiver_flags and **prepends** them to whatever the user
# passes via `env = {"ARFLAGS": ...}`, so an override of `ARFLAGS=cr`
# becomes `ARFLAGS="-static cr"` at the configure command line. autotools'
# generated `libtool` script then bakes that string into its archive_cmds
# template, and at link time runs `$AR -static cr <lib>.a <objs>` against
# `/usr/bin/ar` (BSD ar in macOS clean-env, or LLVM-ar in BSD-compat when
# invoked as `ar`) — both of which reject `-static` outright.
#
# Pointing `AR` at this wrapper short-circuits the flag-mismatch by
# stripping `-static` before delegating to `/usr/bin/ar`. The remaining
# `cr <lib>.a <objs>` is the standard ar-create syntax, accepted by every
# ar implementation we ship against.

set -euo pipefail

# Pass-through query/help flags untouched — autotools/automake probes the
# archiver with `$AR --version` (and similar) at configure time to decide
# which ar interface to use. Mangling these would yield "unknown" and break
# the configure step.
case "${1:-}" in
    --version|--help|-V|-h|"")
        exec /usr/bin/ar "$@"
        ;;
esac

# autotools libtool's static-link template is:
#     $AR $AR_FLAGS $oldlib $oldobjs
# With foreign_cc + toolchains_llvm 1.7.0 on darwin, $AR_FLAGS resolves to
# "-static" (Apple-libtool flavour). After stripping `-static` the remaining
# args are `<archive>.a <obj>.o…`, but BSD ar (`/usr/bin/ar`) requires an
# explicit operation letter as the first argument and rejects bare paths
# with `illegal option -- .` (it parses the leading char of `.libs/foo.a`
# as a flag). For real archive operations, prepend the standard `cr`
# (create + replace) so the forwarded invocation is `ar cr <archive> <objs>`.

filtered=()
for arg in "$@"; do
    case "$arg" in
        -static) ;;            # Apple-libtool flavour, ignore.
        *) filtered+=("$arg") ;;
    esac
done

# If the first remaining arg already looks like an ar operation (single
# alpha-only string), forward as-is — otherwise prepend `cr`.
if [[ ${#filtered[@]} -gt 0 && "${filtered[0]}" =~ ^[crqsdmptux]+$ ]]; then
    exec /usr/bin/ar "${filtered[@]}"
else
    exec /usr/bin/ar cr "${filtered[@]}"
fi
