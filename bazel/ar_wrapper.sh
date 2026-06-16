#!/bin/bash
#
# `AR` shim for autotools/foreign_cc rules on darwin. Strips the Apple-libtool
# flags that rules_foreign_cc prepends from the hermetic-llvm toolchain's macOS
# archiver and forwards a clean GNU-`ar` invocation to the hermetic `llvm-ar`
# (its path comes in via $SIPI_AR, set by `darwin_autotools_env()`).
#
# Why this exists: the hermetic `llvm` toolchain's default macOS archiver is
# `llvm-libtool-darwin` (Apple-style — `libtool -static -o foo.a *.o`, with
# `archiver_flags = ["-D", "-no_warning_for_no_symbols", "-static"]`).
# rules_foreign_cc reads the toolchain's archiver_flags and **prepends** them to
# whatever autotools passes via `$AR_FLAGS`, so autotools' generated `libtool`
# archive template runs `$AR -D -no_warning_for_no_symbols -static <lib>.a
# <objs>`. `llvm-ar` (GNU interface) rejects the libtool-only flags. Stripping
# them and forwarding `llvm-ar cr <lib>.a <objs>` restores standard ar-create
# syntax. Forwarding to the bundle's GNU `llvm-ar` (not the host `/usr/bin/ar`)
# keeps the archive step hermetic — the point of the toolchains_llvm → llvm
# swap. On Linux the toolchain's default archiver is already `llvm-ar`, so this
# wrapper is darwin-only (see `darwin_autotools_env()`).

set -euo pipefail

AR="${SIPI_AR:?ar_wrapper.sh: SIPI_AR (hermetic llvm-ar path) is not set}"

# Pass query/help probes straight through — autotools/automake runs
# `$AR --version` (and similar) at configure time to decide which ar
# interface to use. Mangling these would break the configure step.
case "${1:-}" in
    --version|--help|-V|-h|"")
        exec "$AR" "$@"
        ;;
esac

# Drop the Apple-libtool-only flags that `llvm-ar` does not understand.
filtered=()
for arg in "$@"; do
    case "$arg" in
        -static|-D|-no_warning_for_no_symbols) ;;  # libtool-only, ignore.
        *) filtered+=("$arg") ;;
    esac
done

# `llvm-ar` GNU syntax is `ar <operation> <archive> <objs…>`. If the first
# surviving arg is already an ar operation string, forward as-is; otherwise
# the libtool template left only `<archive> <objs>`, so prepend `cr`
# (create + replace) to make a valid create invocation.
if [[ ${#filtered[@]} -gt 0 && "${filtered[0]}" =~ ^[crqsdmptux]+$ ]]; then
    exec "$AR" "${filtered[@]}"
else
    exec "$AR" cr "${filtered[@]}"
fi
