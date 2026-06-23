"""`magic_mgc_header` — embed libmagic's `magic.mgc` database as a C header.

Reads a single `magic.mgc` file and emits a C header that declares the
two symbols `shttps/util/Parsing.cpp` consumes via
`magic_load_buffers(magic_mgc, magic_mgc_len, …)` — so SIPI binaries
ship with the magic database in-binary and don't need an external
`magic.mgc` on disk.

The C-array conversion runs the hermetic `//tools:bin2c` tool rather than
the host `xxd`, so the rule builds identically on macOS, Linux CI, and lean
RBE images that ship no xxd.
"""

def _magic_mgc_header_impl(ctx):
    mgc = ctx.file.mgc
    output = ctx.actions.declare_file(ctx.attr.out)
    ctx.actions.run(
        executable = ctx.executable.bin2c,
        inputs = [mgc],
        outputs = [output],
        arguments = [mgc.path, output.path, "magic_mgc"],
        mnemonic = "MagicMgcHeader",
        progress_message = "Generating embedded magic database header %s" % output.short_path,
    )
    return [DefaultInfo(files = depset([output]))]

magic_mgc_header = rule(
    implementation = _magic_mgc_header_impl,
    doc = "Generates `magic_mgc.h` by embedding a `magic.mgc` file as a C array.",
    attrs = {
        "mgc": attr.label(
            mandatory = True,
            allow_single_file = True,
            doc = "The `magic.mgc` file to embed (e.g. `@libmagic//:magic.mgc`).",
        ),
        "out": attr.string(
            mandatory = True,
            doc = "Output filename (e.g. `generated/magic_mgc.h`). Resolved relative to the package.",
        ),
        "bin2c": attr.label(
            default = "//tools:bin2c",
            executable = True,
            cfg = "exec",
            doc = "The `xxd -i` replacement that emits the C array.",
        ),
    },
)
