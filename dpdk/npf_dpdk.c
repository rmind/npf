/*-
 * Copyright (c) 2015 Mindaugas Rasiukevicius <rmind at netbsd org>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/queue.h>
#include <string.h>
#include <net/if.h>

#include <rte_common.h>
#include <rte_eal.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>

#include "npf_dpdk.h"

typedef struct ifnet {
	struct if_nameindex	ini;
	void *			arg;
	LIST_ENTRY(ifnet)	entry;
} ifnet_t;

/*
 * XXX/TODO: The API should allow this to be per some instance.
 */

static LIST_HEAD(, ifnet)	dpdk_ifnet_list;
static struct rte_mempool *	dpdk_mbuf_mempool;

void
npf_dpdk_init(struct rte_mempool *mp)
{
	/* XXX: see above */
	LIST_INIT(&dpdk_ifnet_list);
	dpdk_mbuf_mempool = mp;
}

/*
 * Virtual DPDK interfaces.
 */

ifnet_t *
npf_dpdk_ifattach(npf_t *npf, const char *name, unsigned idx)
{
	ifnet_t *ifp;

	if ((ifp = calloc(1, sizeof(ifnet_t))) == NULL) {
		return NULL;
	}
	ifp->ini.if_name = (char *)(uintptr_t)name;
	ifp->ini.if_index = idx;
	LIST_INSERT_HEAD(&dpdk_ifnet_list, ifp, entry);
	npfk_ifmap_attach(npf, ifp);
	return ifp;
}

void
npf_dpdk_ifdetach(npf_t *npf, ifnet_t *ifp)
{
	LIST_REMOVE(ifp, entry);
	npfk_ifmap_detach(npf, ifp);
	free(ifp);
}

static const char *
dpdk_ifop_getname(ifnet_t *ifp)
{
	return ifp->ini.if_name;
}

static ifnet_t *
dpdk_ifop_lookup(const char *ifname)
{
	ifnet_t *ifp;

	LIST_FOREACH(ifp, &dpdk_ifnet_list, entry) {
		if (!strcmp(ifp->ini.if_name, ifname))
			break;
	}
	return ifp;
}

static void
dpdk_ifop_flush(void *arg)
{
	ifnet_t *ifp;

	LIST_FOREACH(ifp, &dpdk_ifnet_list, entry) {
		ifp->arg = arg;
	}
}

static void *
dpdk_ifop_getmeta(const ifnet_t *ifp)
{
	return ifp->arg;
}

static void
dpdk_ifop_setname(ifnet_t *ifp, void *arg)
{
	ifp->arg = arg;
}

/*
 * DPDK mbuf wrappers.
 */

static struct mbuf *
dpdk_mbuf_alloc(int type, int flags)
{
	(void)type; (void)flags;
	return (void *)rte_pktmbuf_alloc(dpdk_mbuf_mempool);
}

static void
dpdk_mbuf_free(struct mbuf *m0)
{
	struct rte_mbuf *m = (void *)m0;
	rte_pktmbuf_free(m);
}

static void *
dpdk_mbuf_getdata(const struct mbuf *m0)
{
	const struct rte_mbuf *m = (const void *)m0;
	return rte_pktmbuf_mtod(m, void *);
}

static struct mbuf *
dpdk_mbuf_getnext(struct mbuf *m0)
{
	struct rte_mbuf *m = (void *)m0;
	return (void *)m->next;
}

static size_t
dpdk_mbuf_getlen(const struct mbuf *m0)
{
	const struct rte_mbuf *m = (const void *)m0;
	return rte_pktmbuf_data_len(m);
}

static size_t
dpdk_mbuf_getchainlen(const struct mbuf *m0)
{
	const struct rte_mbuf *m = (const void *)m0;
	size_t tlen = 0;

	while (m) {
		tlen += rte_pktmbuf_data_len(m);
		m = m->next;
	}
	return tlen;
}

static bool
dpdk_mbuf_ensure_something(struct mbuf **m, size_t len)
{
	(void)m; (void)len;
	return false;
}

/*
 * NPF ops vectors.
 */

static const npf_mbufops_t npf_mbufops = {
	.alloc			= dpdk_mbuf_alloc,
	.free			= dpdk_mbuf_free,
	.getdata		= dpdk_mbuf_getdata,
	.getnext		= dpdk_mbuf_getnext,
	.getlen			= dpdk_mbuf_getlen,
	.getchainlen		= dpdk_mbuf_getchainlen,
	.ensure_contig		= dpdk_mbuf_ensure_something,
	.ensure_writable	= dpdk_mbuf_ensure_something,
};

static const npf_ifops_t npf_ifops = {
	.getname		= dpdk_ifop_getname,
	.lookup			= dpdk_ifop_lookup,
	.flush			= dpdk_ifop_flush,
	.getmeta		= dpdk_ifop_getmeta,
	.setmeta		= dpdk_ifop_setname,
};

npf_t *
npf_dpdk_create(int flags)
{
	return npfk_create(flags, &npf_mbufops, &npf_ifops);
}
