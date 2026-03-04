/*
 * Minimal bits/timesize.h for cross-compilation with zig.
 *
 * When cross-compiling C++ for musl targets, zig's libc++ headers
 * include <features.h> which resolves to the host's glibc features.h.
 * Glibc's features-time64.h needs bits/timesize.h — a multilib header
 * not present on most Ubuntu runners. This shim provides the minimum
 * definitions so the include chain doesn't fail.
 *
 * This file is added via -isystem and only affects cross-compilation.
 * Native builds don't use this path.
 */
#ifndef _BITS_TIMESIZE_H
#define _BITS_TIMESIZE_H

#include <bits/wordsize.h>

#if __WORDSIZE == 64
#define __TIMESIZE 64
#else
#define __TIMESIZE 32
#endif

#endif /* _BITS_TIMESIZE_H */
