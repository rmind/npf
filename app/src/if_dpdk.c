/*
 * Copyright (c) 2020 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

/*
 * DPDK abstracts NIC ports.  We concern only the Ethernet ports.
 */

#include <stdio.h>
#include <inttypes.h>
#include <net/if.h>
#include <arpa/inet.h>

#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_ethdev.h>

#include "npf_router.h"
#include "utils.h"

#define	RX_RING_SIZE	1024
#define	TX_RING_SIZE	1024

static const struct rte_eth_conf eth_conf_default = {
	.rxmode = {
		.max_rx_pkt_len = ETHER_MAX_LEN,
	},
};

int
ifnet_setup(npf_router_t *router, const unsigned port_id, const unsigned nqueues)
{
	struct rte_eth_conf pconf = eth_conf_default;
	uint16_t nb_rxd = RX_RING_SIZE, nb_txd = TX_RING_SIZE;
	struct rte_eth_dev_info dev_info;
	struct rte_eth_txconf txconf;

	/*
	 * Obtain and setup some information about the Ethernet port.
	 */
	if (!rte_eth_dev_is_valid_port(port_id)) {
		return -1;
	}
	rte_eth_dev_info_get(port_id, &dev_info);
	if (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_MBUF_FAST_FREE) {
		pconf.txmode.offloads |= DEV_TX_OFFLOAD_MBUF_FAST_FREE;
	}
	txconf = dev_info.default_txconf;
	txconf.offloads = pconf.txmode.offloads;

	/*
	 * Configure the Ethernet device.
	 * Allocate and setup RX and TX rings and queues.
	 */
	if (rte_eth_dev_configure(port_id, nqueues, nqueues, &pconf) < 0) {
		return -1;
	}
	if (rte_eth_dev_adjust_nb_rx_tx_desc(port_id, &nb_rxd, &nb_txd) < 0) {
		return -1;
	}
	for (unsigned q = 0; q < nqueues; q++) {
		if (rte_eth_rx_queue_setup(port_id, (uint16_t)q, nb_rxd,
		    rte_eth_dev_socket_id(port_id), NULL,
		    router->mbuf_pool) < 0) {
			return -1;
		}
	}
	for (unsigned q = 0; q < nqueues; q++) {
		if (rte_eth_tx_queue_setup(port_id, (uint16_t)q, nb_txd,
		    rte_eth_dev_socket_id(port_id), &txconf) < 0) {
			return -1;
		}
	}

	/*
	 * Start the Ethernet port and enable the promiscuous mode.
	 */
	if (rte_eth_dev_start(port_id) < 0) {
		return -1;
	}
	rte_eth_promiscuous_enable(port_id);
	return 0;
}

int
ifnet_register(npf_router_t *router, const char *name)
{
	unsigned if_idx, port_id;

	if ((if_idx = if_nametoindex(name)) == 0) {
		return -1;
	}
	RTE_ETH_FOREACH_DEV(port_id) {
		struct rte_eth_dev_info dev_info;

		rte_eth_dev_info_get(port_id, &dev_info);
		if (dev_info.if_index == if_idx) {
			/* Register this port ID as of "interest". */
			ASSERT(port_id < MAX_IFNET_IDS);
			router->ifnet_bitset |= (1U << port_id);
			return port_id;
		}
	}
	return -1;
}

bool
ifnet_interesting(npf_router_t *router, const unsigned port_id)
{
	ASSERT(port_id < MAX_IFNET_IDS);
	return (router->ifnet_bitset & (1U << port_id)) != 0;
}

int
ifnet_ifattach(npf_router_t *router, const unsigned port_id)
{
	struct rte_eth_dev_info dev_info;
	char name[IF_NAMESIZE];
	in_addr_t addr;
	ifnet_t *ifp;

	rte_eth_dev_info_get(port_id, &dev_info);
	if (if_indextoname(dev_info.if_index, name) == NULL) {
		return -1;
	}
	if ((ifp = calloc(1, sizeof(ifnet_t))) == NULL) {
		return -1;
	}
	ifp->arp_cache = thmap_create(0, NULL, THMAP_NOCOPY);
	if (ifp->arp_cache == NULL) {
		free(ifp);
		return -1;
	}

	ifp->port_id = port_id;
	strncpy(ifp->name, name, sizeof(ifp->name));
	rte_eth_macaddr_get(port_id, &ifp->hwaddr);
	if (!router->ifnet_addrs[port_id]) {
		free(ifp);
		return -1;
	}
	addr = inet_addr(router->ifnet_addrs[port_id]); // XXX: IPv4 only for now
	memcpy(&ifp->ipaddr, &addr, sizeof(in_addr_t));

	LIST_INSERT_HEAD(&router->ifnet_list, ifp, entry);
	router->ifnet_map[port_id] = ifp;

	npfk_ifmap_attach(router->npf, ifp);
	return 0;
}

void
ifnet_ifdetach(npf_router_t *router, ifnet_t *ifp)
{
	router->ifnet_map[ifp->port_id] = NULL;
	LIST_REMOVE(ifp, entry);

	npfk_ifmap_detach(router->npf, ifp);
	thmap_destroy(ifp->arp_cache);
	free(ifp);
}

ifnet_t *
ifnet_get(npf_router_t *router, const unsigned port_id)
{
	ASSERT(port_id < MAX_IFNET_IDS);
	return router->ifnet_map[port_id];
}

void
ifnet_put(ifnet_t *ifp)
{
	(void)ifp;
	// release
}
