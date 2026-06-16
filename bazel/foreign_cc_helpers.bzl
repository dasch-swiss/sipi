"""Shared helpers for the `ext/<lib>/BUILD.bazel` foreign_cc rules.

Centralises the per-platform tweaks that several rules apply identically.
Add helpers here when more than one BUILD.bazel needs the same pattern.
"""

# Archiver overrides for darwin foreign_cc builds — see `bazel/ar_wrapper.sh`
# for the full write-up. Short form: the hermetic `llvm` toolchain's default
# macOS archiver is `llvm-libtool-darwin` (Apple-style, with
# `archiver_flags = ["-D", "-no_warning_for_no_symbols", "-static"]`), but
# foreign_cc's cmake/autotools deps invoke the archiver with GNU-`ar`
# conventions, which `llvm-libtool-darwin` rejects ("-static option: must be
# specified" / "-o option: must be specified"). We point those deps at the
# bundle's GNU-style `llvm-ar` (`//bazel:llvm-ar`) instead:
#
#   * autotools (configure_make) deps go through `ar_wrapper.sh`, which strips
#     the prepended Apple-libtool flags and forwards to `llvm-ar` ($SIPI_AR);
#   * cmake() deps set `CMAKE_AR` / `CMAKE_RANLIB` directly (CMake drives its
#     own archive command, so no wrapper is needed).
#
# On Linux the toolchain's default archiver is already `llvm-ar`, so none of
# these overrides apply there — every helper is a no-op on the default branch.

def darwin_autotools_env(extra = {}):
    """Build a select()-typed env dict with the darwin AR override merged in.

    Args:
        extra: extra env entries (passed in BOTH branches of the select).

    Returns:
        A select() over the `@platforms//os:macos` config setting. The macOS
        branch points `AR` at `ar_wrapper.sh` and `SIPI_AR` at the hermetic
        `llvm-ar` the wrapper delegates to; the default branch carries only
        `extra`.

    Note: callers that pass this to `configure_make(env = ..., ...)` must also
    list `darwin_autotools_build_data()` in `build_data` so Bazel materialises
    the wrapper script AND `llvm-ar` inside the action sandbox; both are
    referenced by `$(execpath …)` and Bazel only stages files declared as
    build inputs.
    """
    return select({
        "@platforms//os:macos": dict(
            extra,
            AR = "$(execpath //bazel:ar_wrapper.sh)",
            SIPI_AR = "$(execpath //bazel:llvm-ar)",
        ),
        "//conditions:default": extra,
    })

# Build_data convenience: pair with `darwin_autotools_env()` so the wrapper
# script AND the hermetic `llvm-ar` it delegates to are staged into the action
# sandbox (otherwise `$(execpath …)` would expand to paths that don't exist at
# action time).
def darwin_autotools_build_data():
    """Build_data list for a configure_make rule that uses darwin_autotools_env.

    Returns:
        A list of labels — empty on non-darwin, [ar_wrapper.sh, llvm-ar] on
        darwin. Wrapped in `select()` so cross-compile-from-Linux
        configurations don't pick up unused deps.
    """
    return select({
        "@platforms//os:macos": [
            "//bazel:ar_wrapper.sh",
            "//bazel:llvm-ar",
        ],
        "//conditions:default": [],
    })

def darwin_cmake_cache_entries(extra = {}):
    """cache_entries with the darwin CMAKE_AR/CMAKE_RANLIB override merged in.

    cmake() deps fail on darwin because rules_foreign_cc sets `CMAKE_AR` to the
    toolchain's `llvm-libtool-darwin`, which CMake then drives with GNU-`ar`
    archive rules (`<AR> qc …`, no `-static`) that libtool rejects. Pointing
    CMAKE_AR/CMAKE_RANLIB at the bundle's GNU `llvm-ar`/`llvm-ranlib` makes
    CMake's GNU archive rules work as written.

    Args:
        extra: extra cache_entries (passed in BOTH branches of the select).

    Returns:
        A select() over `@platforms//os:macos`. The macOS branch adds CMAKE_AR
        and CMAKE_RANLIB; the default branch carries only `extra` (Linux already
        archives with `llvm-ar`).

    Note: callers must add `darwin_cmake_build_data()` to the cmake rule's
    `build_data` so `$(execpath …)` resolves at action time.
    """
    return select({
        "@platforms//os:macos": dict(
            extra,
            CMAKE_AR = "$(execpath //bazel:llvm-ar)",
            CMAKE_RANLIB = "$(execpath //bazel:llvm-ranlib)",
        ),
        "//conditions:default": extra,
    })

def darwin_cmake_build_data():
    """Build_data list for a cmake() rule that uses darwin_cmake_cache_entries.

    Returns:
        A list of labels — empty on non-darwin, [llvm-ar, llvm-ranlib] on
        darwin.
    """
    return select({
        "@platforms//os:macos": [
            "//bazel:llvm-ar",
            "//bazel:llvm-ranlib",
        ],
        "//conditions:default": [],
    })

# Link-wrapper clang++ resolution (darwin foreign_cc C++ deps).
#
# hermetic-llvm 0.8.8's macOS "complete" toolchain routes every link action
# through a `link-wrapper` binary (it adds ThinLTO + dsym handling). The wrapper
# `execv()`s the compiler named by the `LLVM_CLANGXX` env var, which the
# toolchain sets to an *execroot-relative* path
# (`external/<bundle>/bin/clang++`). That resolves for normal Bazel link actions
# (cwd == execroot), but foreign_cc relocates each build into
# `<dep>.build_tmpdir/...` and runs cmake/make from there, so the relative
# `LLVM_CLANGXX` no longer resolves and the wrapper dies with "failed to execute
# external/.../clang++: No such file or directory". CMake's own
# compiler-detection step links a C++ test executable, so every C++ foreign_cc
# dep (exiv2, sentry) fails at configure time; C-only deps never link a test
# program and are unaffected (kakadu drives its own `clang++`-from-PATH wrapper,
# so it bypasses the link-wrapper too).
#
# foreign_cc merges a rule's `env` *after* the toolchain action env, so we
# re-export LLVM_CLANGXX as an absolute path. `$$EXT_BUILD_ROOT$$` survives
# expand_make_variables and is substituted with the live execroot shell var in
# the generated build script. Darwin-only — on Linux the toolchain's default
# tool map has no link-wrapper.

def darwin_link_wrapper_env(extra = {}):
    """env with an absolute LLVM_CLANGXX for the darwin link-wrapper.

    Args:
        extra: extra env entries (passed in BOTH branches of the select).

    Returns:
        A select() over `@platforms//os:macos`. The macOS branch overrides
        LLVM_CLANGXX with `$EXT_BUILD_ROOT/<clang++ execpath>`; the default
        branch carries only `extra`.

    Note: callers must add `darwin_link_wrapper_build_data()` to the rule's
    `build_data` so `$(execpath //bazel:clang++)` resolves at action time.
    """
    return select({
        "@platforms//os:macos": dict(
            extra,
            LLVM_CLANGXX = "$$EXT_BUILD_ROOT$$/$(execpath //bazel:clang++)",
        ),
        "//conditions:default": extra,
    })

def darwin_link_wrapper_build_data():
    """Build_data list for a foreign_cc rule that uses darwin_link_wrapper_env.

    Returns:
        A list of labels — empty on non-darwin, [clang++] on darwin.
    """
    return select({
        "@platforms//os:macos": ["//bazel:clang++"],
        "//conditions:default": [],
    })
