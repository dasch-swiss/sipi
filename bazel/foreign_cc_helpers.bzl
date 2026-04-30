"""Shared helpers for the `ext/<lib>/BUILD.bazel` foreign_cc rules.

Centralises the per-platform tweaks that several rules apply identically.
Add helpers here when more than one BUILD.bazel needs the same pattern.
"""

# AR override for darwin autotools builds — see `bazel/ar_wrapper.sh` for
# the full incident write-up. Short form: toolchains_llvm 1.7.0 hard-codes
# `archiver_flags=["-static"]` on macOS so its Apple `libtool` archiver
# behaves correctly. rules_foreign_cc *prepends* those toolchain flags to
# any user `env = {"ARFLAGS": ...}` value, and autotools' generated
# `libtool` then bakes the merged string into its archive_cmds template.
# The end state is `/usr/bin/ar -static cr foo.a obj.o`, which BSD ar
# rejects because it parses `-static` as `-s -t -a -t -i -c` and barfs on
# the conflicting positional/flag arguments.
#
# The ar_wrapper.sh script silently drops `-static` and forwards everything
# else to `/usr/bin/ar`, restoring ar-create syntax. Routing AR through it
# is the smallest change that survives the foreign_cc flag-merge.
#
# On Linux the toolchain already provides `bin/llvm-ar` directly with no
# `-static` flag, so the wrapper is a no-op there — we omit the AR
# override entirely on the default branch.
def darwin_autotools_env(extra = {}):
    """Build a select()-typed env dict with the darwin AR override merged in.

    Args:
        extra: extra env entries (passed in BOTH branches of the select).

    Returns:
        A select() over the `@platforms//os:macos` config setting. The macOS
        branch points `AR` at the wrapper script; the default branch carries
        only `extra`.

    Note: callers that pass this to `configure_make(env = ..., ...)` must
    also list `//bazel:ar_wrapper.sh` in `build_data` so Bazel materialises
    the script inside the action sandbox; the wrapper is referenced by
    `$(execpath …)` and Bazel only stages files declared as build inputs.
    """
    return select({
        "@platforms//os:macos": dict(
            extra,
            AR = "$(execpath //bazel:ar_wrapper.sh)",
        ),
        "//conditions:default": extra,
    })

# Build_data convenience: pair with `darwin_autotools_env()` so the wrapper
# script is staged into the action sandbox (otherwise `$(execpath …)` would
# expand to a path that doesn't exist at action time).
def darwin_autotools_build_data():
    """Build_data list for a configure_make rule that uses darwin_autotools_env.

    Returns:
        A list of labels — empty on non-darwin, [//bazel:ar_wrapper.sh] on
        darwin. Wrapped in `select()` so cross-compile-from-Linux
        configurations don't pick up an unused dep.
    """
    return select({
        "@platforms//os:macos": ["//bazel:ar_wrapper.sh"],
        "//conditions:default": [],
    })
