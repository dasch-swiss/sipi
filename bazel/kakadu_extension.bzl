"""Module extension that wires `kakadu_archive` into MODULE.bazel.

Bzlmod requires repository rules to be invoked via a module extension; this is
the thinnest viable wiring. The single `kakadu` target invokes
`kakadu_archive` with the values pinned in MODULE.bazel; bumping Kakadu means
editing MODULE.bazel only.
"""

load("//bazel:kakadu.bzl", "kakadu_archive")

def _kakadu_extension_impl(_ctx):
    kakadu_archive(
        name = "kakadu",
        # Release tag matches the GitHub release at dasch-swiss/dsp-ci-assets.
        tag = "kakadu-v8.5",
        asset = "v8_5-01382N.zip",
        repo = "dasch-swiss/dsp-ci-assets",
        sha256 = "c19c7579d1dee023316e7de090d9de3eb24764e349b4069e5af3a540fb644e75",
        strip_prefix = "v8_5-01382N",
        patches = [
            "//patches:kakadu-Makefile-Linux-x86-64-clang.patch",
            "//patches:kakadu-Makefile-Linux-arm-64-clang.patch",
        ],
        build_file = "//bazel:kakadu.BUILD.bazel",
    )

kakadu_extension = module_extension(
    implementation = _kakadu_extension_impl,
)
