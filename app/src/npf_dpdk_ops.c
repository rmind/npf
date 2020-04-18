/*
 * Copyright (c) 2015-2020 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#include <sys/queue.h>
#include <net/if.h>
#include <string.h>

#include <rte_common.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>

#include <net/npf.h>
#include <net/npfkern.h>
#include <npf.h>

#include "npf_router.h"
#include "utils.h"

struct mbuf;

static npf_router_t *		npf_router; // XXX

void
npf_dpdk_init(npf_router_t *router)
{
	npf_router = router; // XXX
}

/*
 * Virtual DPDK interfaces.
 */

static const char *
dpdk_ifop_getname(ifnet_t *ifp)
{
	return ifp->name;
}

static ifnet_t *
dpdk_ifop_lookup(const char *ifname)
{
	ifnet_t *ifp;

	LIST_FOREACH(ifp, &npf_router->ifnet_list, entry) {
		if (!strcmp(ifp->name, ifname))
			break;
	}
	return ifp;
}

static void
dpdk_ifop_flush(void *arg)
{
	ifnet_t *ifp;

	LIST_FOREACH(ifp, &npf_router->ifnet_list, entry) {
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
dpdk_mbuf_alloc(int type __unused, int flags __unused)
{
	return (void *)rte_pktmbuf_alloc(npf_router->mbuf_pool);
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
	return rte_pktmbuf_pkt_len(m);
}

static bool
dpdk_mbuf_ensure_config(struct mbuf **mp, size_t len)
{
	struct rte_mbuf *m = (void *)*mp;

	if (len > rte_pktmbuf_data_len(m) && rte_pktmbuf_linearize(m) < 0) {
		return false;
	}
	return len <= rte_pktmbuf_data_len(m);
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
	.ensure_contig		= dpdk_mbuf_ensure_config,
	.ensure_writable	= NULL,
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
