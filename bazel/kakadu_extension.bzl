"""Module extension that wires the Kakadu source archive into MODULE.bazel.

Bzlmod requires repository rules to be invoked via a module extension; this is
the thinnest viable wiring. The single `kakadu` repo invokes
`gh_release_archive` with the values pinned here; bumping Kakadu means
editing this file only.
"""

load("//bazel:gh_release.bzl", "gh_release_archive")

def _kakadu_extension_impl(_ctx):
    gh_release_archive(
        name = "kakadu",
        # Release tag matches the GitHub release at dasch-swiss/dsp-ci-assets.
        tag = "kakadu-v8.7",
        asset = "v8_7-01727L.zip",
        repo = "dasch-swiss/dsp-ci-assets",
        sha256 = "d5fc94e4a8fa08e49b387c72aea16af267b41ab8f411ec4c1a616c3394cfafbc",
        strip_prefix = "v8_7-01727L",
        # No patches: the native cc_library (//bazel:kakadu.BUILD.bazel) compiles
        # the sources directly and never invokes Kakadu's makefiles, so the old
        # `Makefile-*-clang.patch` clang/flag adjustments are obsolete.
        build_file = "//bazel:kakadu.BUILD.bazel",
    )

kakadu_extension = module_extension(
    implementation = _kakadu_extension_impl,
)
