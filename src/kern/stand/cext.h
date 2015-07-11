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
#define	__UNCONST(a)		((void*)(const void*)a)
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

#ifndef roundup
#define	roundup(x, y)	((((x) + ((y) - 1)) / (y)) * (y))
#endif
#ifndef rounddown
#define	rounddown(x,y)	(((x) / (y)) * (y))
#endif
#ifndef roundup2
#define	roundup2(x, m)	(((x) + (m) - 1) & ~((m) - 1))
#endif
#ifndef powerof2
#define	powerof2(x)	((((x) - 1) & (x)) == 0)
#endif

/*
 * Maths helpers: log2 on integer, fast division and remainder.
 */

#ifndef flsl
#define	flsl(x)		(__predict_true(x) ? \
    (sizeof(unsigned long) * CHAR_BIT) - __builtin_clzl(x) : 0)
#endif
#ifndef flsll
#define	flsll(x)	(__predict_true(x) ? \
    (sizeof(unsigned long long) * CHAR_BIT) - __builtin_clzll(x) : 0)
#endif

#ifndef ilog2
#define	ilog2(x)	(flsl(x) - 1)
#endif

#ifndef fast_div32_prep

static inline uint64_t
fast_div32_prep(uint32_t div)
{
	const int l = flsl(div - 1);
	uint64_t mt, m;
	uint8_t s1, s2;

	mt = (uint64_t)(0x100000000ULL * ((1ULL << l) - div));
	m = (uint32_t)(mt / div + 1);
	s1 = (l > 1) ? 1 : l;
	s2 = (l == 0) ? 0 : l - 1;

	return m | (uint64_t)s1 << 32 | (uint64_t)s2 << 40;
}

static inline uint32_t
fast_div32(uint32_t v, uint32_t div, uint64_t inv)
{
	const uint32_t m = inv & 0xffffffff;
	const uint32_t t = (uint32_t)(((uint64_t)v * m) >> 32);
	const uint8_t s1 = (inv >> 32) & 0xff, s2 = (inv >> 40) & 0xff;
	(void)div;

	return (t + ((v - t) >> s1)) >> s2;
}

static inline uint32_t
fast_rem32(uint32_t v, uint32_t div, uint64_t inv)
{
	return v - div * fast_div32(v, div, inv);
}

#endif

#define	zalloc(len)	calloc(1, (len))
#define	ASSERT		assert

#define atomic_compare_exchange_weak(ptr, expected, desired) \
    __sync_bool_compare_and_swap(ptr, expected, desired)

#endif
