/* glibc 2.42 → 2.31 ABI compatibility shims (Linux only).
 *
 * kakadu's foreign_cc make() rule compiles via Nix's clang++, which sees
 * the host runner's glibc 2.42 headers. Those have C23 transparent
 * renames like `#define strtol __isoc23_strtol` (gated by
 * __GLIBC_USE(ISOC2X), enabled under _GNU_SOURCE) and LFS folding
 * (`fcntl` → `fcntl64` from glibc 2.34 onward). The kakadu .o files
 * therefore reference symbols that the toolchains_llvm Chromium
 * debian-bullseye sysroot's glibc 2.31 doesn't export under those names.
 *
 * This translation unit provides the missing aliases as wrapper
 * functions calling the canonical glibc 2.31 entry points. It's linked
 * into the final sipi binary on Linux only — see
 * src/BUILD.bazel:sipi_lib's `deps += ":glibc23_compat"` selector. The
 * earlier ld.lld --defsym approach failed because lld requires both
 * sides of an alias to be resolved at link time, but `strtol` lives in
 * libc.so (dynamic) and isn't visible during the defsym pass.
 *
 * ABI equivalence on this code path:
 *   * The C23 strto*l functions only differ from C99's in accepting a
 *     0x prefix on octal input. kakadu's kdu_window CID parsing and
 *     file_io fd-flag mapping don't rely on that distinction, so the
 *     shim is exact for our usage.
 *   * fcntl64 was folded into fcntl from glibc 2.34 onward; on glibc
 *     2.31 fcntl is the LFS-aware canonical entry, so the alias is
 *     identical at the syscall layer.
 *
 * Y+1 cleanup: refactor kakadu.BUILD.bazel to `targets=[…]` +
 * `prefix_script` so foreign_cc declares the cc_toolchain as an action
 * input. With Bazel's toolchain clang doing the kakadu compile, --sysroot
 * is honoured natively and this whole shim file becomes deletable.
 */

#define _GNU_SOURCE
#include <stdarg.h>
#include <stdlib.h>
#include <fcntl.h>
#include <locale.h>  /* `locale_t` (xlocale.h was folded into locale.h in glibc 2.26+). */

/* The C23 strtol family — forward to C99 canonical entries. */

long __isoc23_strtol(const char *nptr, char **endptr, int base) {
    return strtol(nptr, endptr, base);
}

long long __isoc23_strtoll(const char *nptr, char **endptr, int base) {
    return strtoll(nptr, endptr, base);
}

unsigned long __isoc23_strtoul(const char *nptr, char **endptr, int base) {
    return strtoul(nptr, endptr, base);
}

unsigned long long __isoc23_strtoull(const char *nptr, char **endptr, int base) {
    return strtoull(nptr, endptr, base);
}

long __isoc23_strtol_l(const char *nptr, char **endptr, int base, locale_t loc) {
    return strtol_l(nptr, endptr, base, loc);
}

long long __isoc23_strtoll_l(const char *nptr, char **endptr, int base, locale_t loc) {
    return strtoll_l(nptr, endptr, base, loc);
}

unsigned long __isoc23_strtoul_l(const char *nptr, char **endptr, int base, locale_t loc) {
    return strtoul_l(nptr, endptr, base, loc);
}

unsigned long long __isoc23_strtoull_l(const char *nptr, char **endptr, int base, locale_t loc) {
    return strtoull_l(nptr, endptr, base, loc);
}

/* Variadic forwarder for fcntl64 → fcntl. fcntl's third argument is
 * cmd-dependent (int / pointer / void), but at the va_list level we just
 * forward whatever a 64-bit register-width arg looked like. glibc's own
 * implementation does the same reinterpret. */
int fcntl64(int fd, int cmd, ...) {
    va_list ap;
    va_start(ap, cmd);
    void *arg = va_arg(ap, void *);
    va_end(ap);
    return fcntl(fd, cmd, arg);
}
