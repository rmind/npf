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

#ifndef _NPF_BPF_H_
#define _NPF_BPF_H_

#include <inttypes.h>
#include <pcap/bpf.h>

typedef struct bpf_args {
	const uint8_t *	pkt;
	size_t		wirelen;
	size_t		buflen;
	uint32_t *	mem;
	void *		arg;
} bpf_args_t;

#define	BPF_MAX_MEMWORDS	30
#define	BPF_MEMWORD_INIT(k)	(UINT32_C(1) << (k))
#define	BPF_MAXINSNS		512

#define	BPF_COP			0x20
#define	BPF_COPX		0x40

typedef uint32_t	bpf_memword_init_t;
typedef struct bpf_ctx	bpf_ctx_t;

typedef uint32_t (*bpf_copfunc_t)(const bpf_ctx_t *, bpf_args_t *, uint32_t);

struct bpf_ctx {
	/*
	 * BPF coprocessor functions and the number of them.
	 */
	const bpf_copfunc_t *	copfuncs;
	size_t			nfuncs;

	/*
	 * The number of memory words in the external memory store.
	 * There may be up to BPF_MAX_MEMWORDS words; if zero is set,
	 * then the internal memory store is used which has a fixed
	 * number of words (BPF_MEMWORDS).
	 */
	size_t			extwords;

	/*
	 * The bitmask indicating which words in the external memstore
	 * will be initialised by the caller.
	 */
	bpf_memword_init_t	preinited;
};

typedef unsigned int (*bpfjit_func_t)(const bpf_ctx_t *, bpf_args_t *);

bpf_ctx_t *bpf_create(void);
void	bpf_destroy(bpf_ctx_t *);

int	bpf_set_cop(bpf_ctx_t *, const bpf_copfunc_t *, size_t);
int	bpf_set_extmem(bpf_ctx_t *, size_t, bpf_memword_init_t);
u_int	bpf_filter_ext(const bpf_ctx_t *, const struct bpf_insn *, bpf_args_t *);
int	bpf_validate_ext(const bpf_ctx_t *, const struct bpf_insn *, int);

bpfjit_func_t bpf_jit_generate(bpf_ctx_t *, void *, size_t);
void	bpf_jit_freecode(bpfjit_func_t);

#endif
