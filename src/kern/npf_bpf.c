/*
 * Copyright (c) 2009-2013 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

/*
 * NPF byte-code processing.
 */

#ifdef _KERNEL
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: npf_bpf.c,v 1.14 2018/09/29 14:41:36 rmind Exp $");

#include <sys/types.h>
#include <sys/param.h>

#include <sys/bitops.h>
#include <sys/mbuf.h>
#include <net/bpf.h>
#endif

#define NPF_BPFCOP
#include "npf_impl.h"

#if defined(_NPF_STANDALONE)
#define	m_length(m)		(nbuf)->nb_mops->getchainlen(m)
#endif

/*
 * BPF context and the coprocessor.
 */

static bpf_ctx_t *npf_bpfctx __read_mostly;

static uint32_t	npf_cop_l3(const bpf_ctx_t *, bpf_args_t *, uint32_t);
static uint32_t	npf_cop_table(const bpf_ctx_t *, bpf_args_t *, uint32_t);

static const bpf_copfunc_t npf_bpfcop[] = {
	[NPF_COP_L3]	= npf_cop_l3,
	[NPF_COP_TABLE]	= npf_cop_table,
};

#define	BPF_MW_ALLMASK \
    ((1U << BPF_MW_IPVER) | (1U << BPF_MW_L4OFF) | (1U << BPF_MW_L4PROTO))

void
npf_bpf_sysinit(void)
{
	npf_bpfctx = bpf_create();
	bpf_set_cop(npf_bpfctx, npf_bpfcop, __arraycount(npf_bpfcop));
	bpf_set_extmem(npf_bpfctx, NPF_BPF_NWORDS, BPF_MW_ALLMASK);
}

void
npf_bpf_sysfini(void)
{
	bpf_destroy(npf_bpfctx);
}

void
npf_bpf_prepare(npf_cache_t *npc, bpf_args_t *args, uint32_t *M)
{
	nbuf_t *nbuf = npc->npc_nbuf;
	const struct mbuf *mbuf = nbuf_head_mbuf(nbuf);
	const size_t pktlen = m_length(mbuf);

	/* Prepare the arguments for the BPF programs. */
#ifdef _NPF_STANDALONE
	args->pkt = (const uint8_t *)nbuf_dataptr(nbuf);
	args->wirelen = args->buflen = pktlen;
#else
	args->pkt = (const uint8_t *)mbuf;
	args->wirelen = pktlen;
	args->buflen = 0;
#endif
	args->mem = M;
	args->arg = npc;

	/*
	 * Convert address length to IP version.  Just mask out
	 * number 4 or set 6 if higher bits set, such that:
	 *
	 *	0	=>	0
	 *	4	=>	4 (IPVERSION)
	 *	16	=>	6 (IPV6_VERSION >> 4)
	 */
	const u_int alen = npc->npc_alen;
	const uint32_t ver = (alen & 4) | ((alen >> 4) * 6);

	/*
	 * Output words in the memory store:
	 *	BPF_MW_IPVER	IP version (4 or 6).
	 *	BPF_MW_L4OFF	L4 header offset.
	 *	BPF_MW_L4PROTO	L4 protocol.
	 */
	M[BPF_MW_IPVER] = ver;
	M[BPF_MW_L4OFF] = npc->npc_hlen;
	M[BPF_MW_L4PROTO] = npc->npc_proto;
}

int
npf_bpf_filter(bpf_args_t *args, const void *code, bpfjit_func_t jcode)
{
	/* Execute JIT-compiled code. */
	if (__predict_true(jcode)) {
		return jcode(npf_bpfctx, args);
	}

	/* Execute BPF byte-code. */
	return bpf_filter_ext(npf_bpfctx, code, args);
}

void *
npf_bpf_compile(void *code, size_t size)
{
	return bpf_jit_generate(npf_bpfctx, code, size);
}

bool
npf_bpf_validate(const void *code, size_t len)
{
	const size_t icount = len / sizeof(struct bpf_insn);
	return bpf_validate_ext(npf_bpfctx, code, icount) != 0;
}

/*
 * NPF_COP_L3: fetches layer 3 information.
 */
static uint32_t
npf_cop_l3(const bpf_ctx_t *bc, bpf_args_t *args, uint32_t A)
{
	const npf_cache_t * const npc = (const npf_cache_t *)args->arg;
	const uint32_t ver = (npc->npc_alen & 4) | ((npc->npc_alen >> 4) * 6);
	uint32_t * const M = args->mem;

	M[BPF_MW_IPVER] = ver;
	M[BPF_MW_L4OFF] = npc->npc_hlen;
	M[BPF_MW_L4PROTO] = npc->npc_proto;
	return ver; /* A <- IP version */
}

#define	SRC_FLAG_BIT	(1U << 31)

/*
 * NPF_COP_TABLE: perform NPF table lookup.
 *
 *	A <- non-zero (true) if found and zero (false) otherwise
 */
static uint32_t
npf_cop_table(const bpf_ctx_t *bc, bpf_args_t *args, uint32_t A)
{
	const npf_cache_t * const npc = (const npf_cache_t *)args->arg;
	npf_tableset_t *tblset = npf_config_tableset(npc->npc_ctx);
	const uint32_t tid = A & (SRC_FLAG_BIT - 1);
	const npf_addr_t *addr;
	npf_table_t *t;

	if (!npf_iscached(npc, NPC_IP46)) {
		return 0;
	}
	t = npf_tableset_getbyid(tblset, tid);
	if (__predict_false(!t)) {
		return 0;
	}
	addr = npc->npc_ips[(A & SRC_FLAG_BIT) ? NPF_SRC : NPF_DST];
	return npf_table_lookup(t, npc->npc_alen, addr) == 0;
}
