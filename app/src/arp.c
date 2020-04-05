/*
 * Copyright (c) 2020 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

/*
 * Minimalistic ARP implementation (demo only).
 *
 * Ethernet Address Resolution Protocol, RFC 826, November 1982.
 */

#include <stdio.h>
#include <arpa/inet.h>

#include <rte_ether.h>
#include <rte_arp.h>
#include <rte_mbuf.h>

#include "npf_router.h"
#include "utils.h"

typedef struct {
	uint32_t		ipaddr;
	struct ether_addr	hwaddr;
} arp_entry_t;

static void
arp_cache(ifnet_t *ifp, const uint32_t *ipaddr,
    const struct ether_addr *hwaddr, const bool allow_new)
{
	arp_entry_t *ac;
	bool ok;

	ac = thmap_get(ifp->arp_cache, ipaddr, sizeof(uint32_t));
	if (__predict_true(ac)) {
		/* TODO: Update. */
		return;
	}
	if (!allow_new || (ac = malloc(sizeof(arp_entry_t))) == NULL) {
		return;
	}
	memcpy(&ac->ipaddr, ipaddr, sizeof(ac->ipaddr));
	memcpy(&ac->hwaddr, hwaddr, sizeof(ac->hwaddr));

	ok = thmap_put(ifp->arp_cache, &ac->ipaddr, sizeof(ac->ipaddr), ac) == ac;
	if (__predict_false(!ok)) {
		/* Race: already cached. */
		free(ac);
	}
}

static int
arp_cache_lookup(ifnet_t *ifp, const uint32_t *ipaddr, struct ether_addr *hwaddr)
{
	arp_entry_t *ac;

	ac = thmap_get(ifp->arp_cache, ipaddr, sizeof(uint32_t));
	if (ac == NULL) {
		return -1;
	}
	ether_addr_copy(&ac->hwaddr, hwaddr);
	return 0;
}

/*
 * arp_request: construct an ARP REQUEST packet.
 *
 * => On success, returns an mbuf with Ethernet header; NULL otherwise.
 */
static struct rte_mbuf *
arp_request(worker_t *worker, const struct ether_addr *src_hwaddr,
    const uint32_t *src_addr, const uint32_t *target)
{
	struct rte_mbuf *m;
	struct ether_hdr *eh;
	struct arp_hdr *ah;
	struct arp_ipv4 *arp;

	m = rte_pktmbuf_alloc(worker->router->mbuf_pool);
	if (m == NULL) {
		return NULL;
	}
	eh = rte_pktmbuf_mtod(m, struct ether_hdr *);
	m->l2_len = sizeof(struct ether_hdr);
	m->l3_len = sizeof(struct arp_hdr);
	m->data_len = m->l2_len + m->l3_len;
	m->pkt_len = m->data_len;

	memset(&eh->d_addr, 0xff, sizeof(struct ether_addr));
	ether_addr_copy(src_hwaddr, &eh->s_addr);
	eh->ether_type = htons(ETHER_TYPE_ARP);

	/*
	 * ARP Ethernet REQUEST.
	 */
	ah = rte_pktmbuf_mtod_offset(m, struct arp_hdr *, m->l2_len);
	ah->arp_hrd = htons(ARP_HRD_ETHER);
	ah->arp_pro = htons(ETHER_TYPE_IPv4);
	ah->arp_hln = ETHER_ADDR_LEN;
	ah->arp_pln = sizeof(struct in_addr);
	ah->arp_op = htons(ARP_OP_REQUEST);

	arp = &ah->arp_data;
	ether_addr_copy(src_hwaddr, &arp->arp_sha);
	memcpy(&arp->arp_sip, src_addr, sizeof(arp->arp_sip));

	/* Broadcast message to look for the target. */
	memset(&arp->arp_tha, 0xff, sizeof(arp->arp_tha));
	memcpy(&arp->arp_tip, target, sizeof(arp->arp_tip));
	return m;
}

/*
 * arp_resolve: given the IPv4 address and an interface, both specified
 * by the route, resolve its MAC address in the Ethernet network.
 *
 * => Performs a lookup in the ARP cache or sends an ARP request.
 * => On success, return 0 and writes the MAC address into the buffer.
 * => On failure, return -1.
 */
int
arp_resolve(worker_t *worker, const route_info_t *rt,
    struct ether_addr *hwaddr)
{
	const uint32_t *addr = (const void *)&rt->next_hop;
	struct rte_mbuf *m;
	ifnet_t *ifp;
	int ret;

	if ((ifp = ifnet_get(worker->router, rt->if_idx)) == NULL) {
		return -1;
	}

	/* Lookup in the ARP cache. */
	ret = arp_cache_lookup(ifp, addr, hwaddr);
	if (ret == 0) {
		ifnet_put(ifp);
		return 0;
	}

	/* Construct an ARP request. */
	m = arp_request(worker, &ifp->hwaddr, (const void *)&ifp->ipaddr, addr);
	ifnet_put(ifp);

	/* Send an ARP request. */
	if (m && pktq_enqueue(worker, rt->if_idx, m) == -1) {
		return -1;
	}

	/* XXX: Just drop the packet for now; the caller will retry. */
	return -1;
}

static inline bool
arp_is_interesting(const struct arp_hdr *ah, const ifnet_t *ifp, bool *targeted)
{
	const struct arp_ipv4 *arp = &ah->arp_data;
	const struct ether_addr *tha = &arp->arp_tha;
	bool ucast, bcast;

	/* Unicast to us, broadcast or ARP probe? */
	ucast = is_same_ether_addr(&ifp->hwaddr, &arp->arp_tha);
	bcast = is_broadcast_ether_addr(tha) || is_zero_ether_addr(tha);
	if (ucast || bcast) {
		/* Is the target IP matching the interface? */
		*targeted = memcmp(&ifp->ipaddr,
		    &arp->arp_tip, sizeof(arp->arp_tip)) == 0;
		return true;
	}
	return false;
}

/*
 * arp_input: process an ARP packet.
 */
int
arp_input(worker_t *worker, struct rte_mbuf *m, const unsigned if_idx)
{
	ifnet_t *ifp = NULL;
	struct ether_hdr *eh;
	struct arp_hdr *ah;
	struct arp_ipv4 *arp;
	bool targeted;

	/*
	 * Get the ARP header and verify 1) hardware address type
	 * 2) hardware address length 3) protocol address length.
	 */
	ah = rte_pktmbuf_mtod_offset(m, struct arp_hdr *, m->l2_len);
	if (ah->arp_hrd != htons(ARP_HRD_ETHER) ||
	    ah->arp_hln != ETHER_ADDR_LEN ||
	    ah->arp_pln != sizeof(in_addr_t)) {
		goto drop;
	}
	arp = &ah->arp_data;

	if ((ifp = ifnet_get(worker->router, if_idx)) == NULL) {
		goto drop;
	}

	if (!arp_is_interesting(ah, ifp, &targeted)) {
		goto drop;
	}

	/*
	 * ARP cache entry:
	 *
	 * => If target IP is us, then CREATE or UPDATE.
	 * => Otherwise, UPDATE (only if the entry already exists).
	 */
	arp_cache(ifp, &arp->arp_sip, &arp->arp_sha, targeted);

	/*
	 * If ARP REQUEST for us, then process it producing APR REPLY.
	 */
	if (targeted && ntohs(ah->arp_op) == ARP_OP_REQUEST) {
		const uint32_t ipaddr = arp->arp_tip; // copy

		/*
		 * Prepare an ARP REPLY.  Swap the source and target fields,
		 * both for the hardware and protocol addresses.
		 */
		ah->arp_op = htons(ARP_OP_REPLY);

		memcpy(&arp->arp_tha, &arp->arp_sha, sizeof(arp->arp_tha));
		memcpy(&arp->arp_tip, &arp->arp_sip, sizeof(arp->arp_tip));

		memcpy(&arp->arp_sha, &ifp->hwaddr, sizeof(arp->arp_sha));
		memcpy(&arp->arp_sip, &ipaddr, sizeof(arp->arp_sip));

		/* Update the Ethernet frame too. */
		eh = rte_pktmbuf_mtod(m, struct ether_hdr *);
		ether_addr_copy(&eh->s_addr, &eh->d_addr);
		ether_addr_copy(&ifp->hwaddr, &eh->s_addr);

		ifnet_put(ifp);

		if (pktq_enqueue(worker, if_idx, m) == -1) {
			goto drop;
		}
		return 1; // consume
	}
drop:
	/*
	 * Drop the packet:
	 * - Release the interface, if holding.
	 * - Free the packet.
	 */
	if (ifp) {
		ifnet_put(ifp);
	}
	rte_pktmbuf_free(m);
	return -1;
}
