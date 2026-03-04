/*
 * Minimal bits/libc-header-start.h for cross-compilation with zig.
 *
 * On x86_64 Ubuntu, glibc's stdint.h and other standard headers include
 * this file. It normally lives in the multiarch path
 * (/usr/include/x86_64-linux-gnu/bits/) which isn't in the default
 * include search path for cross-compilation. This shim provides the
 * minimum definitions so the include chain doesn't fail.
 */
#ifndef _BITS_LIBC_HEADER_START_H
#define _BITS_LIBC_HEADER_START_H

/* __GLIBC_USE(F) evaluates to 1 if feature F is enabled, 0 otherwise.
   For musl cross-compilation we disable all glibc-specific features. */
#ifndef __GLIBC_USE
#define __GLIBC_USE(F) 0
#endif

#endif /* _BITS_LIBC_HEADER_START_H */
