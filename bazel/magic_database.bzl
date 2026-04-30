"""`magic_mgc_header` — embed libmagic's `magic.mgc` database as a C header.

Mirrors the CMake build's `magic_database` chain (`CMakeLists.txt`):
extract `share/misc/magic.mgc` from libmagic's install tree and emit a
C header that declares two symbols `shttps/Parsing.cpp` consumes via
`magic_load_buffers(magic_mgc, magic_mgc_len, …)` — so SIPI binaries
ship with the magic database in-binary and don't need an external
`magic.mgc` on disk.

Why a custom rule rather than a `genrule`:
  * `//ext/libmagic:libmagic` is a `configure_make` foreign_cc rule. Its
    DefaultInfo lists the install tree as three top-level entries:
    `include/`, `share/`, and `lib/libmagic.a`. `magic.mgc` lives inside
    the `share/` *tree* artefact, not as a standalone label, so neither
    `$(execpath)` nor `$(rootpath)` can pick it out at analysis time.
  * Walking DefaultInfo.files for the `share/` directory and `find`-ing
    `magic.mgc` at action time keeps the dependency precise: the action
    only re-runs when libmagic's outputs (or the rule's logic) change.

The C-array conversion is `xxd -i`-style — the same wire format the
existing `generate_icc_header.sh` produces. Inlining it here makes the
rule self-contained and removes the host-tool dependency on `xxd`'s
flag-set quirks (Toybox xxd on macOS doesn't accept the
output-as-positional-arg form that GNU xxd does); we drive `xxd` via
stdin/stdout, which is identical across all supported flavours.
"""

def _magic_mgc_header_impl(ctx):
    # rules_foreign_cc's `configure_make` rule declares each `out_data_dirs`
    # entry via `ctx.actions.declare_directory()` (foreign_cc 0.15.1
    # `framework.bzl:874`), so `share/` lives in DefaultInfo.files as a
    # real tree artefact whose contents Bazel stages into the action
    # sandbox. Pick that specific entry (rather than passing the entire
    # DefaultInfo, which would also materialise the .a archive — wasted
    # I/O for what is otherwise a few-MB action).
    share_dir = None
    for f in ctx.attr.libmagic[DefaultInfo].files.to_list():
        if f.is_directory and f.basename == "share":
            share_dir = f
            break
    if share_dir == None:
        fail(
            "magic_mgc_header: libmagic must expose a `share/` tree " +
            "artefact (declared via `out_data_dirs = [\"share\"]` in " +
            "ext/libmagic/BUILD.bazel). Files seen: " +
            ", ".join([
                "%s (%s)" % (f.path, "dir" if f.is_directory else "file")
                for f in ctx.attr.libmagic[DefaultInfo].files.to_list()
            ]),
        )

    output = ctx.actions.declare_file(ctx.attr.out)
    ctx.actions.run_shell(
        inputs = [share_dir],
        outputs = [output],
        command = """
            set -euo pipefail
            mgc=$(find {share} -type f -name magic.mgc 2>/dev/null | head -1)
            if [ -z "$mgc" ]; then
                echo "magic_mgc_header: magic.mgc not found under {share}" >&2
                find {share} -type f 2>/dev/null | sed 's/^/  seen: /' >&2
                exit 1
            fi
            tmpdir=$(mktemp -d)
            trap 'rm -rf "$tmpdir"' EXIT
            # `xxd -i` infers the C-array name from the input filename, so
            # stage the file as plain `magic.mgc` rather than passing the
            # sandbox-relative path. Reading via stdin avoids xxd flavour
            # differences (Toybox vs GNU) in how positional args map to
            # output streams.
            cp "$mgc" "$tmpdir/magic.mgc"
            (cd "$tmpdir" && xxd -i magic.mgc) > {out}
        """.format(
            share = share_dir.path,
            out = output.path,
        ),
        use_default_shell_env = True,
        # Tree artefacts produced via `ctx.actions.declare_directory()`
        # don't propagate their CONTENTS into Bazel's macOS sandbox —
        # only the directory placeholder is mounted, so `find` walks an
        # empty tree and the action fails. Linux's sandbox handles tree
        # artefacts correctly, and `--spawn_strategy=local` works on
        # macOS too. Tagging the action `no-sandbox` lets it run in the
        # exec-root directly on darwin while keeping sandboxed execution
        # on Linux. Inputs are still tracked, so cache invalidation
        # works as expected.
        execution_requirements = {"no-sandbox": "1"},
        mnemonic = "MagicMgcHeader",
        progress_message = "Generating embedded magic database header %s" % output.short_path,
    )
    return [DefaultInfo(files = depset([output]))]

magic_mgc_header = rule(
    implementation = _magic_mgc_header_impl,
    doc = "Generates `magic_mgc.h` by embedding libmagic's `magic.mgc` file as a C array.",
    attrs = {
        "libmagic": attr.label(
            mandatory = True,
            doc = "The libmagic foreign_cc target whose output tree contains `share/misc/magic.mgc`.",
        ),
        "out": attr.string(
            mandatory = True,
            doc = "Output filename (e.g. `generated/magic_mgc.h`). Resolved relative to the package.",
        ),
    },
)
