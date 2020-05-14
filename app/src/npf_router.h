/*
 * Copyright (c) 2020 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#ifndef _NPF_ROUTER_H_
#define _NPF_ROUTER_H_

#include <sys/queue.h>
#include <inttypes.h>

#include <rte_ether.h>
#include <thmap.h>

#include <net/if.h>
#include <net/npf.h>
#include <net/npfkern.h>

struct ifnet;
struct worker;

typedef struct route_table route_table_t;

#define	NPF_CONFSOCK_PATH	"/dev/npf"

#define	MAX_IFNET_IDS		(32) // XXX: hardcoded, e.g. worker_t::bitmap

/*
 * NPF router structures.
 */

typedef struct {
	unsigned		if_idx;
	unsigned		addr_len;
	npf_addr_t		next_hop;
} route_info_t;

#define	MBUF_NPF_NEED_L2	0x01

typedef struct {
	unsigned		flags;
	unsigned		ether_type;
	route_info_t		route;
} npf_mbuf_priv_t;

typedef struct ifnet {
	unsigned		port_id;

	npf_addr_t		ipaddr;
	struct ether_addr	hwaddr;

	void *			arg;
	LIST_ENTRY(ifnet)	entry;

	thmap_t *		arp_cache;
	char			name[IF_NAMESIZE];
} ifnet_t;

typedef struct npf_router {
	npf_t *			npf;
	int			config_sock;
	struct rte_mempool *	mbuf_pool;
	unsigned		pktqueue_size;
	route_table_t *		rtable;

	/*
	 * Interface list, map, count as well as bitmap.
	 */
	LIST_HEAD(, ifnet)	ifnet_list;
	unsigned		ifnet_count;
	uint32_t		ifnet_bitset;
	ifnet_t *		ifnet_map[MAX_IFNET_IDS];
	char *			ifnet_addrs[MAX_IFNET_IDS];

	unsigned		worker_count;
	struct worker *		worker[];
} npf_router_t;

typedef struct {
	unsigned		count;
	struct rte_mbuf *	pkt[];
} pktqueue_t;

typedef struct worker {
	unsigned		i;
	npf_t *			npf;
	npf_router_t *		router;

	uint32_t		bitmap;
	pktqueue_t *		queue[];
} worker_t;

/*
 * NPF DPDK operations, network interface and configuration loading.
 */

npf_t *		npf_dpdk_create(int, npf_router_t *);

int		ifnet_setup(npf_router_t *, const unsigned, const unsigned);
int		ifnet_register(npf_router_t *, const char *);
bool		ifnet_interesting(npf_router_t *, const unsigned);

int		ifnet_ifattach(npf_router_t *, const unsigned);
void		ifnet_ifdetach(npf_router_t *, ifnet_t *);
ifnet_t *	ifnet_get(npf_router_t *, const unsigned);
void		ifnet_put(ifnet_t *);

int		load_config(npf_router_t *);

/*
 * Routing and ARP.
 */

route_table_t *	route_table_create(void);
void		route_table_destroy(route_table_t *);

int		route_add(route_table_t *, const void *, unsigned, unsigned,
		    const route_info_t *);
int		route_lookup(route_table_t *, const void *, unsigned,
		    route_info_t *);

int		arp_resolve(worker_t *, const route_info_t *, struct ether_addr *);
int		arp_input(worker_t *, struct rte_mbuf *, const unsigned);

/*
 * Worker / processing.
 */

int		pktq_enqueue(worker_t *, unsigned, struct rte_mbuf *);
void		if_input(worker_t *, const unsigned);

#endif
