/*
 * Minimal bits/wordsize.h for cross-compilation with zig.
 *
 * When cross-compiling C++ for musl targets, zig's libc++ headers
 * include <features.h> which resolves to the host's glibc features.h.
 * Glibc's features.h includes features-time64.h which needs
 * bits/wordsize.h — a multilib header not present on most Ubuntu
 * runners. This shim provides the minimum definitions so the
 * include chain doesn't fail.
 *
 * This file is added via -isystem and only affects cross-compilation.
 * Native builds don't use this path.
 */
#ifndef _BITS_WORDSIZE_H
#define _BITS_WORDSIZE_H

#if defined(__x86_64__) || defined(__aarch64__) || defined(__powerpc64__) || defined(__s390x__)
#define __WORDSIZE 64
#else
#define __WORDSIZE 32
#endif

#define __WORDSIZE_TIME64_COMPAT32 0

#endif /* _BITS_WORDSIZE_H */
