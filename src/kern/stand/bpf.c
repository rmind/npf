/*-
 * Copyright (c) 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from the Stanford/CMU enet packet filter,
 * (net/enet.c) distributed as part of 4.3BSD, and code contributed
 * to Berkeley by Steven McCanne and Van Jacobson both of Lawrence
 * Berkeley Laboratory.
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
 *	@(#)bpf_filter.c	8.1 (Berkeley) 6/10/93
 */

#include <stdlib.h>
#include <errno.h>

#include <net/bpf.h>

#include "bpf.h"

bpf_ctx_t *
bpf_create(void)
{
	return calloc(1, sizeof(bpf_ctx_t));
}

void
bpf_destroy(bpf_ctx_t *bc)
{
	free(bc);
}

int
bpf_set_cop(bpf_ctx_t *bc, const bpf_copfunc_t *funcs, size_t n)
{
	bc->copfuncs = funcs;
	bc->nfuncs = n;
	return 0;
}

int
bpf_set_extmem(bpf_ctx_t *bc, size_t nwords, bpf_memword_init_t preinited)
{
	if (nwords > BPF_MAX_MEMWORDS || (preinited >> nwords) != 0) {
		return EINVAL;
	}
	bc->extwords = nwords;
	bc->preinited = preinited;
	return 0;
}
