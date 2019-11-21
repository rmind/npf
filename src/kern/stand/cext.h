/*
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Berkeley Software Design, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)cdefs.h	8.8 (Berkeley) 1/9/95
 */

/*
 * Various C and compiler wrappers, helpers, constants, etc.
 * Portions are taken from NetBSD's sys/cdefs.h and sys/param.h headers.
 */

#ifndef	_SYS_CEXT_H_
#define	_SYS_CEXT_H_

#include <sys/types.h>
#include <inttypes.h>
#include <limits.h>
#include <assert.h>

#ifndef __predict_true
#define	__predict_true(x)	__builtin_expect((x) != 0, 1)
#endif
#ifndef __predict_false
#define	__predict_false(x)	__builtin_expect((x) != 0, 0)
#endif

#ifndef __constructor
#define	__constructor		__attribute__((constructor))
#endif

#ifndef __packed
#define	__packed		__attribute__((__packed__))
#endif

#ifndef __aligned
#define	__aligned(x)		__attribute__((__aligned__(x)))
#endif

#ifndef __unused
#define	__unused		__attribute__((__unused__))
#endif

#ifndef __arraycount
#define	__arraycount(__x)	(sizeof(__x) / sizeof(__x[0]))
#endif

#ifndef __UNCONST
#define	__UNCONST(a)		((void*)(uintptr_t)(const void*)a)
#endif

#ifndef __noinline
#ifdef __GNUC__
#define	__noinline		__attribute__((__noinline__))
#else
#define	__noinline
#endif
#endif

/*
 * C++ compatibility and DSO visibility.
 */

#ifndef __BEGIN_DECLS
#if defined(__cplusplus)
#define	__BEGIN_DECLS	extern "C" {
#define	__END_DECLS	}
#else
#define	__BEGIN_DECLS
#define	__END_DECLS
#endif
#endif

#ifndef __GNUC_PREREQ__
#ifdef __GNUC__
#define	__GNUC_PREREQ__(x, y)						\
    ((__GNUC__ == (x) && __GNUC_MINOR__ >= (y)) || (__GNUC__ > (x)))
#else
#define	__GNUC_PREREQ__(x, y)	0
#endif
#endif

#ifndef __BEGIN_PUBLIC_DECLS
#if __GNUC_PREREQ__(4, 0)
#  define __BEGIN_PUBLIC_DECLS	\
	_Pragma("GCC visibility push(default)") __BEGIN_DECLS
#  define __END_PUBLIC_DECLS	__END_DECLS _Pragma("GCC visibility pop")
#else
#  define __BEGIN_PUBLIC_DECLS	__BEGIN_DECLS
#  define __END_PUBLIC_DECLS	__END_DECLS
#endif
#endif

#if !defined(__dso_public)
#if __GNUC_PREREQ__(4, 0)
#define	__dso_public	__attribute__((__visibility__("default")))
#else
#define	__dso_public
#endif
#endif

/*
 * Compile-time assertion: if C11 static_assert() is not available,
 * then emulate it.
 */
#ifndef CTASSERT
#define	__CEXT_CTASSERT0(x, y)	typedef char __assert ## y[(x) ? 1 : -1]
#define	__CEXT_CTASSERT(x, y)	__CEXT_CTASSERT0(x, y)
#define	CTASSERT(x)		__CEXT_CTASSERT(x, __LINE__)
#endif
#ifndef static_assert
#define	static_assert(exp, msg)	CTASSERT(exp)
#endif

/*
 * Cache line size - a reasonable upper bound.
 */
#ifndef CACHE_LINE_SIZE
#define	CACHE_LINE_SIZE		64
#endif

/*
 * Minimum, maximum and rounding macros.
 */

#ifndef MIN
#define	MIN(x, y)	((x) < (y) ? (x) : (y))
#endif

#ifndef MAX
#define	MAX(x, y)	((x) > (y) ? (x) : (y))
#endif

#ifndef roundup2
#define	roundup2(x, m)	(((x) + (m) - 1) & ~((m) - 1))
#endif

#define	zalloc(len)	calloc(1, (len))
#define	ASSERT		assert

/*
 * C11-style memory fences and atomic loads/stores.
 */
#ifndef atomic_thread_fence
#define	memory_order_relaxed	__ATOMIC_RELAXED
#define	memory_order_acquire	__ATOMIC_ACQUIRE
#define	memory_order_release	__ATOMIC_RELEASE
#define	memory_order_seq_cst	__ATOMIC_SEQ_CST
#define	atomic_thread_fence(m)	__atomic_thread_fence(m)
#endif
#ifndef atomic_store_explicit
#define	atomic_store_explicit	__atomic_store_n
#endif
#ifndef atomic_load_explicit
#define	atomic_load_explicit	__atomic_load_n
#endif

/*
 * Exponential back-off for the spinning paths.
 */
#define	SPINLOCK_BACKOFF_MIN	4
#define	SPINLOCK_BACKOFF_MAX	128
#if defined(__x86_64__) || defined(__i386__)
#define SPINLOCK_BACKOFF_HOOK	__asm volatile("pause" ::: "memory")
#else
#define SPINLOCK_BACKOFF_HOOK
#endif
#define	SPINLOCK_BACKOFF(count)					\
do {								\
	for (int __i = (count); __i != 0; __i--) {		\
		SPINLOCK_BACKOFF_HOOK;				\
	}							\
	if ((count) < SPINLOCK_BACKOFF_MAX)			\
		(count) += (count);				\
} while (/* CONSTCOND */ 0);

#endif
