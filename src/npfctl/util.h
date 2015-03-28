/*-
 * Copyright (c) 2006 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef	_UTIL_H_
#define	_UTIL_H_

#include <stdio.h>
#include <stdarg.h>
#include <err.h>

#include "../kern/stand/cext.h"

#ifndef __CTASSERT
#define	__CTASSERT		CTASSERT
#endif

#ifndef ecalloc
static inline void *
ecalloc(size_t n, size_t s)
{
	void *p = calloc(n, s);
	if (p == NULL && n != 0 && s != 0)
		err(1, "Cannot allocate %zu blocks of size %zu", n, s);
	return p;
}
#endif

#ifndef erealloc
static inline void *
erealloc(void *p, size_t n)
{
	void *q = realloc(p, n);
	if (q == NULL && n != 0)
		err(1, "Cannot re-allocate %zu bytes", n);
	return q;
}
#endif

#ifndef estrdup
static inline char *
estrdup(const char *s)
{
	char *d = strdup(s);
	if (d == NULL)
		err(1, "Cannot copy string");
	return d;
}
#endif

#ifndef estrndup
static inline char *
estrndup(const char *s, size_t len)
{
	char *d = strndup(s, len);
	if (d == NULL)
		err(1, "Cannot copy string");
	return d;
}
#endif

#ifdef __linux__ // XXX glibc
int asprintf(char **strp, const char *fmt, ...);
int vasprintf(char **strp, const char *fmt, va_list ap);
#endif

#ifndef easprintf
static inline int
easprintf(char ** __restrict ret, const char * __restrict format, ...)
{
	int rv;
	va_list ap;
	va_start(ap, format);
	if ((rv = vasprintf(ret, format, ap)) == -1)
		err(1, "Cannot format string");
	va_end(ap);
	return rv;
}
#endif

#endif
