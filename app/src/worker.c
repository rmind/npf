/*
 * Copyright (c) 2020 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

/*
 * Worker of the NPF-router.
 */

#include <stdio.h>
#include <arpa/inet.h>

#include <rte_eal.h>
#include <rte_launch.h>
#include <rte_mempool.h>
#include <rte_malloc.h>
#include <rte_mbuf.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_ip.h>

#include "npf_router.h"
#include "utils.h"

static int
firewall_process(npf_t *npf, struct rte_mbuf **mp, ifnet_t *ifp, const int di)
{
	int error;

	error = npfk_packet_handler(npf, (struct mbuf **)mp, ifp, di);

	/* Note: NPF may consume the packet. */
	if (error || *mp == NULL) {
		if (*mp) {
			rte_pktmbuf_free(*mp);
		}
		return -1;
	}
	return 0;
}

static int
if_output(worker_t *worker, unsigned if_idx, struct rte_mbuf *m)
{
	const npf_mbuf_priv_t *minfo = rte_mbuf_to_priv(m);
	struct ether_hdr *eh;
	ifnet_t *ifp;

	/*
	 * If the packet already has L2 header, then nothing more to do.
	 */
	if ((minfo->flags & MBUF_NPF_NEED_L2) == 0) {
		return 0;
	}

	if ((ifp = ifnet_get(worker->router, if_idx)) == NULL) {
		return -1; // drop
	}

	/*
	 * Add the Ethernet header.
	 */
	ASSERT(m->l2_len == ETHER_HDR_LEN);
	eh = (void *)rte_pktmbuf_prepend(m, ETHER_HDR_LEN);
	if (eh == NULL) {
		ifnet_put(ifp);
		return -1; // drop
	}
	ether_addr_copy(&ifp->hwaddr, &eh->s_addr);
	ifnet_put(ifp);

	/*
	 * Perform ARP resolution.
	 */
	if (arp_resolve(worker, &minfo->route, &eh->d_addr) == -1) {
		return -1; // drop
	}
	eh->ether_type = minfo->ether_type;
	return 0;
}

/*
 * pkt_enqueue: enqueue the packet for TX.
 */
int
pktq_enqueue(worker_t *worker, unsigned if_idx, struct rte_mbuf *m)
{
	const unsigned pktq_size = worker->router->pktqueue_size;
	pktqueue_t *pq;

	ASSERT(if_idx < worker->router->ifnet_count);
	pq = worker->queue[if_idx];

	if (__predict_false(pq->count >= pktq_size)) {
		return -1; // drop
	}
	if (if_output(worker, if_idx, m) == -1) {
		return -1; // drop
	}
	worker->bitmap |= (1U << if_idx);
	pq->pkt[pq->count++] = m;
	return 0;
}

static unsigned
pktq_tx(worker_t *worker, const unsigned if_idx)
{
	pktqueue_t *pq = worker->queue[if_idx];
	const unsigned pktcount = pq->count;
	unsigned sent;

	/* Send a burst of packets. */
	sent = rte_eth_tx_burst(if_idx, worker->i, pq->pkt, pktcount);

	/* Destroy any remaining packets. */
	for (unsigned i = sent; i < pktcount; i++) {
		rte_pktmbuf_free(pq->pkt[i]);
		pq->pkt[i] = NULL;
	}
	pq->count = 0;
	return sent;
}

static void
pktq_tx_all(worker_t *worker)
{
	unsigned if_idx;

	while ((if_idx = ffs(worker->bitmap)) != 0) {
		if_idx--;

		/*
		 * Transmit the queue (send a burst of packets).
		 */
		pktq_tx(worker, if_idx);

		/* Next .. */
		worker->bitmap &= ~(1U << if_idx);
	}
	ASSERT(worker->bitmap == 0);
}

static int
l2_input(worker_t *worker, struct rte_mbuf *m, const unsigned if_idx)
{
	npf_mbuf_priv_t *minfo = rte_mbuf_to_priv(m);
	const struct ether_hdr *eh;

	/*
	 * Do we have an L2 header?  If not, then nothing to do.
	 */
	if ((m->packet_type & RTE_PTYPE_L2_MASK) == 0) {
		return 0;
	}

	/*
	 * We have an L2 header, which must be Ethernet.  Save it in the
	 * mbuf private area for later pre-pending.
	 */

	eh = rte_pktmbuf_mtod(m, const struct ether_hdr *);
	minfo->ether_type = eh->ether_type;

	ASSERT(sizeof(struct ether_hdr) == ETHER_HDR_LEN);
	m->l2_len = ETHER_HDR_LEN;

	switch (ntohs(eh->ether_type)) {
	case ETHER_TYPE_ARP:
		return arp_input(worker, m, if_idx);
	default:
		break;
	}

	/*
	 * Remove the L2 header as we are preparing for L3 processing.
	 */
	rte_pktmbuf_adj(m, sizeof(struct ether_hdr));
	minfo->flags |= MBUF_NPF_NEED_L2;
	return 0;
}

/*
 * ip_route: find a route for the IPv4/IPv6 packet.
 */
static int
ip_route(npf_router_t *router, struct rte_mbuf *m)
{
	npf_mbuf_priv_t *minfo = rte_mbuf_to_priv(m);
	route_info_t *rt = &minfo->route;
	const void *addr;
	unsigned alen;

	/*
	 * Determine whether it is IPv4 or IPv6 packet.
	 */
	if (RTE_ETH_IS_IPV4_HDR(m->packet_type)) {
		const struct ipv4_hdr *ip4;

		ip4 = rte_pktmbuf_mtod(m, const struct ipv4_hdr *);
		addr = &ip4->dst_addr;
		alen = sizeof(ip4->dst_addr);
		m->ol_flags |= (PKT_TX_IPV4 | PKT_TX_IP_CKSUM);

		if (ip4->time_to_live <= 1) {
			/* ICMP_TIMXCEED */
			return -1;
		}
		/* TODO: ip4->time_to_live--; */

	} else if (RTE_ETH_IS_IPV6_HDR(m->packet_type)) {
		const struct ipv6_hdr *ip6;

		ip6 = rte_pktmbuf_mtod(m, const struct ipv6_hdr *);
		addr = &ip6->dst_addr;
		alen = sizeof(ip6->dst_addr);
		m->ol_flags |= (PKT_TX_IPV6 | PKT_TX_IP_CKSUM);

		if (ip6->hop_limits <= 1) {
			/* ICMP_TIMXCEED */
			return -1;
		}
		/* TODO: ip6->hop_limits--; */

	} else {
		return -1;
	}

	/*
	 * Lookup the route and get the interface and next hop.
	 */
	if (route_lookup(router->rtable, addr, alen, rt) == -1) {
		return -1;
	}
	if (!rt->addr_len) {
		/*
		 * There is next-hop specified with route, so it will be
		 * the destination address.
		 */
		memcpy(&rt->next_hop, addr, alen);
		rt->addr_len = alen;
	}
	return rt->if_idx;
}

static int
ip_output(worker_t *worker, struct rte_mbuf *m, const unsigned if_idx)
{
	ifnet_t *ifp;
	int ret;

	/*
	 * Firewall -- outbound.
	 */
	if ((ifp = ifnet_get(worker->router, if_idx)) == NULL) {
		rte_pktmbuf_free(m);
		return -1;
	}
	ret = firewall_process(worker->npf, &m, ifp, PFIL_OUT);
	ifnet_put(ifp);
	if (ret) {
		/* Consumed or dropped. */
		return 0;
	}

	/*
	 * Enqueue for the destination interface.
	 */
	if (pktq_enqueue(worker, if_idx, m) == -1) {
		rte_pktmbuf_free(m);
		return -1;
	}
	return 0;
}

void
if_input(worker_t *worker, const unsigned rx_if_idx)
{
	const unsigned burst_size = worker->router->pktqueue_size;
	struct rte_mbuf *mbufs[burst_size];
	ifnet_t *rx_ifp;
	unsigned npkts;

	/* Get a burst of packets on this interface. */
	if ((rx_ifp = ifnet_get(worker->router, rx_if_idx)) == NULL) {
		return; // raced with interface detach
	}
	npkts = rte_eth_rx_burst(rx_if_idx, worker->i, mbufs, burst_size);
	if (__predict_false(npkts == 0)) {
		ifnet_put(rx_ifp);
		return; // nothing to do here
	}

	/*
	 * Route each packet.
	 */
	worker->bitmap = 0;
	for (unsigned i = 0; i < npkts; i++) {
		struct rte_mbuf *m = mbufs[i];
		int if_idx;

		/*
		 * L2 processing.
		 */
		if (l2_input(worker, m, rx_if_idx)) {
			/* Consumed (dropped or re-enqueued). */
			continue;
		}

		/*
		 * Firewall -- inbound.
		 */
		if (firewall_process(worker->npf, &m, rx_ifp, PFIL_IN)) {
			/* Consumed or dropped. */
			continue;
		}

		/*
		 * L3 routing.
		 */
		if ((if_idx = ip_route(worker->router, m)) == -1) {
			/* Packet could not be routed -- drop it. */
			rte_pktmbuf_free(m);
			continue;
		}
		(void)ip_output(worker, m, if_idx);
	}
	ifnet_put(rx_ifp);

	/*
	 * Send packets on the destination interfaces.
	 */
	pktq_tx_all(worker);
}
