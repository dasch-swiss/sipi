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
        tag = "kakadu-v8.7",
        asset = "v8_7-01727L.zip",
        repo = "dasch-swiss/dsp-ci-assets",
        sha256 = "d5fc94e4a8fa08e49b387c72aea16af267b41ab8f411ec4c1a616c3394cfafbc",
        strip_prefix = "v8_7-01727L",
        patches = [
            "//patches:kakadu-Makefile-Linux-x86-64-clang.patch",
            "//patches:kakadu-Makefile-Linux-arm-64-clang.patch",
        ],
        build_file = "//bazel:kakadu.BUILD.bazel",
    )

kakadu_extension = module_extension(
    implementation = _kakadu_extension_impl,
)
