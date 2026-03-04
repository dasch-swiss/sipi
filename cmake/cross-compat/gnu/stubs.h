/*
 * Minimal gnu/stubs.h for cross-compilation with zig.
 *
 * Glibc's features.h includes gnu/stubs.h at the end of its include
 * chain. This header normally lists stub functions but is architecture-
 * specific and not always installed on CI runners. This empty shim
 * satisfies the #include without providing any stubs (which are
 * irrelevant for musl targets anyway).
 */
#ifndef __GNU_STUBS_H
#define __GNU_STUBS_H
#endif
