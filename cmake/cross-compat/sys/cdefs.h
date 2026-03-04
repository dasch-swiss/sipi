/*
 * Minimal sys/cdefs.h for cross-compilation with zig.
 *
 * When cross-compiling C++ for musl targets, zig's libc++ headers
 * include <features.h> which resolves to the host's glibc features.h.
 * Glibc's features.h includes sys/cdefs.h — a glibc-internal header
 * not present in musl's sysroot. This shim provides the minimum
 * definitions so the include chain doesn't fail.
 *
 * This file is added via -isystem and only affects cross-compilation.
 * Native builds don't use this path.
 */
#ifndef _SYS_CDEFS_H
#define _SYS_CDEFS_H

/* Provide __GLIBC_PREREQ as a no-op so glibc feature checks don't fail */
#ifndef __GLIBC_PREREQ
#define __GLIBC_PREREQ(maj, min) 0
#endif

/* __BEGIN_DECLS / __END_DECLS for C++ compatibility */
#ifndef __BEGIN_DECLS
#ifdef __cplusplus
#define __BEGIN_DECLS extern "C" {
#define __END_DECLS }
#else
#define __BEGIN_DECLS
#define __END_DECLS
#endif
#endif

/* Common attribute macros used by glibc headers */
#ifndef __THROW
#ifdef __cplusplus
#define __THROW noexcept
#else
#define __THROW
#endif
#endif

#ifndef __nonnull
#define __nonnull(params)
#endif

#ifndef __attribute_pure__
#define __attribute_pure__
#endif

#ifndef __attribute_const__
#define __attribute_const__
#endif

#ifndef __attribute_malloc__
#define __attribute_malloc__
#endif

#ifndef __attribute_deprecated__
#define __attribute_deprecated__
#endif

#ifndef __attribute_warn_unused_result__
#define __attribute_warn_unused_result__
#endif

#ifndef __wur
#define __wur
#endif

#ifndef __glibc_clang_prereq
#define __glibc_clang_prereq(maj, min) 0
#endif

#endif /* _SYS_CDEFS_H */
