"""Custom repository_rule for ApprovalTests.cpp (single-header library).

ApprovalTests.cpp ships its release as a single .hpp file (not a tarball), so
`http_archive` does not apply and BCR has no `approvaltests_cpp` module. This
rule mirrors today's CMake flow in `test/CMakeLists.txt:53-72`:

    file(DOWNLOAD ${HEADER_URL} ${HEADER_FILE} EXPECTED_HASH SHA256=...)

It downloads `ApprovalTests.v.<version>.hpp` to `ApprovalTests.hpp` inside the
generated repo and writes a `BUILD.bazel` exposing the header as a
`cc_library(name = "approval_tests")`. The plan's `@approvaltests_cpp//:approval_tests`
target name (DEV-6343, plan §"PR Y+1") resolves through this rule.

Version + checksum match the values pinned in `test/CMakeLists.txt:60` so the
Bazel-built and Nix-built test paths consume the exact same header bytes.
"""

def _approvaltests_cpp_impl(ctx):
    ctx.download(
        url = ctx.attr.url,
        output = "ApprovalTests.hpp",
        sha256 = ctx.attr.sha256,
    )
    ctx.file("BUILD.bazel", """\
load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "approval_tests",
    hdrs = ["ApprovalTests.hpp"],
    includes = ["."],
    visibility = ["//visibility:public"],
)
""")

approvaltests_cpp = repository_rule(
    implementation = _approvaltests_cpp_impl,
    attrs = {
        "url": attr.string(mandatory = True, doc = "URL of the single-header release."),
        "sha256": attr.string(mandatory = True, doc = "SHA-256 of the header bytes."),
    },
)
