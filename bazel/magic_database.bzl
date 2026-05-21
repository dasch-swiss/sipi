"""`magic_mgc_header` — embed libmagic's `magic.mgc` database as a C header.

Reads a single `magic.mgc` file and emits a C header that declares the
two symbols `shttps/Parsing.cpp` consumes via
`magic_load_buffers(magic_mgc, magic_mgc_len, …)` — so SIPI binaries
ship with the magic database in-binary and don't need an external
`magic.mgc` on disk.

The C-array conversion is `xxd -i`-style — the same wire format the
existing `generate_icc_header.sh` produces. Inlining it here makes the
rule self-contained and removes the host-tool dependency on `xxd`'s
flag-set quirks (Toybox xxd on macOS doesn't accept the
output-as-positional-arg form that GNU xxd does); we drive `xxd` via
stdin/stdout, which is identical across all supported flavours.
"""

def _magic_mgc_header_impl(ctx):
    mgc = ctx.file.mgc

    output = ctx.actions.declare_file(ctx.attr.out)
    ctx.actions.run_shell(
        inputs = [mgc],
        outputs = [output],
        command = """
            set -euo pipefail
            tmpdir=$(mktemp -d)
            trap 'rm -rf "$tmpdir"' EXIT
            # `xxd -i` infers the C-array name from the input filename, so
            # stage the file as plain `magic.mgc` rather than passing the
            # sandbox-relative path. Reading via stdin avoids xxd flavour
            # differences (Toybox vs GNU) in how positional args map to
            # output streams.
            cp "{mgc}" "$tmpdir/magic.mgc"
            (cd "$tmpdir" && xxd -i magic.mgc) > {out}
        """.format(
            mgc = mgc.path,
            out = output.path,
        ),
        use_default_shell_env = True,
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
    },
)
