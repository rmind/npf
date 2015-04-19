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

/*
 * This is a demo to illustrate NPF and DPDK integration.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <err.h>

#include <rte_common.h>
#include <rte_eal.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>

#define	__FAVOR_BSD
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <netinet/udp.h>

#include <pcap/pcap.h>
#include "npf_dpdk.h"

#define MAX_MBUF_SIZE		(4096)
#define	MAX_MBUFS		(8192 - 1)
#define	MAX_LOCAL_MBUFS		(512)
#define	PKT_BATCH		(64)

static struct rte_mempool *	pktmbuf_pool = NULL;
static bool			debug = false;

static void
dpdk_init(int argc, char **argv)
{
	if (rte_eal_init(argc, argv) < 0) {
		err(EXIT_FAILURE, "rte_eal_init() failed");
	}
	pktmbuf_pool = rte_mempool_create("mbufpl", MAX_MBUFS, MAX_MBUF_SIZE,
	    MAX_LOCAL_MBUFS, sizeof(struct rte_pktmbuf_pool_private),
	    rte_pktmbuf_pool_init, NULL, rte_pktmbuf_init, NULL,
	    rte_socket_id(), 0);
	assert(pktmbuf_pool != NULL);
}

/*
 * build_pcap_filter: given the filter criteria, build BPF byte-code and
 * associate it with the given NPF rule.
 */
static void
build_pcap_filter(nl_rule_t *rl, const char *filter)
{
	const size_t maxsnaplen = 64 * 1024;
	struct bpf_program bf;
	size_t len;

	/* Compile the expression (use DLT_RAW for NPF rules). */
	if (pcap_compile_nopcap(maxsnaplen, DLT_RAW, &bf,
	    filter, 1, PCAP_NETMASK_UNKNOWN) == -1) {
		errx(EXIT_FAILURE, "pcap_compile() failed");
	}

	/* Assign the byte-code to this rule. */
	len = bf.bf_len * sizeof(struct bpf_insn);
	if (npf_rule_setcode(rl, NPF_CODE_BPF, bf.bf_insns, len) == -1) {
		errx(EXIT_FAILURE, "npf_rule_setcode() failed");
	}

	if (debug) {
		printf("BPF byte-code for '%s' expression:\n", filter);
		bpf_dump(&bf, 0);
	}
	pcap_freecode(&bf);
}

/*
 * create_npf_config: construct an NPF config with a rule and return it.
 */
static nl_config_t *
create_npf_config(void)
{
	nl_config_t *ncf;
	nl_rule_t *rl;

	ncf = npf_config_create();
	assert(ncf != NULL);

	/*
	 * Create a "pass" rule, accepting both incoming and outgoing
	 * packets where either source or destination is 10.1.1.1 .
	 */
	rl = npf_rule_create(NULL, NPF_RULE_PASS |
	    NPF_RULE_IN | NPF_RULE_OUT, NULL);
	assert(rl != NULL);
	build_pcap_filter(rl, "host 10.1.1.1");

	/* Insert a rule into it. */
	npf_rule_setprio(rl, NPF_PRI_LAST);
	npf_rule_insert(ncf, NULL, rl);

	return ncf;
}

static void
load_npf_config(npf_t *npf, nl_config_t *ncf)
{
	npf_error_t errinfo;
	void *ref;

	/*
	 * - Build the config (returns a reference for loading).
	 * - Load it for the NPF instance.
	 * - Finally, destroy the config.
	 */
	ref = npf_config_build(ncf);
	if (npf_load(npf, ref, &errinfo) != 0) {
		errx(EXIT_FAILURE, "npf_load() failed");
	}
	npf_config_destroy(ncf);
}

static struct rte_mbuf *
get_packet(void)
{
	struct rte_mbuf *m;
	struct ip *ip;
	struct udphdr *uh;
	unsigned len;

	m = rte_pktmbuf_alloc(pktmbuf_pool);
	if (!m) {
		return NULL;
	}
	ip = rte_pktmbuf_mtod(m, struct ip *);
	len = sizeof(struct ip) + sizeof(struct udphdr) + 1;
	memset(ip, 0, len);

	/* IPv4 header. */
	ip->ip_v = IPVERSION;
	ip->ip_hl = sizeof(struct ip) >> 2;
	ip->ip_off = 0;
	ip->ip_ttl = 64;
	ip->ip_p = IPPROTO_UDP;
	ip->ip_len = htons(len);

	/* Randomly chose either 10.1.1.1 or 10.1.1.2 address. */
	ip->ip_src.s_addr = (random() & 0x1) ?
	    inet_addr("10.1.1.1") : inet_addr("10.1.1.2");
	ip->ip_dst.s_addr = inet_addr("10.1.1.252");

	/* UDP header. */
	uh = (struct udphdr *)(ip + 1);
	uh->uh_sport = htons(25000);
	uh->uh_sport = htons(80);
	uh->uh_ulen = htons(1);

	m->pkt_len = m->data_len = len;
	m->nb_segs = 1;
	m->next = NULL;
	return m;
}

static void
process_packets(npf_t *npf, struct ifnet *ifp, int di)
{
	struct rte_mbuf *in_pkts[PKT_BATCH];
	struct rte_mbuf *out_pkts[PKT_BATCH];
	unsigned out = 0;

	/* Process a batch of packets. */
	for (unsigned i = 0; i < PKT_BATCH; i++) {
		int ret;

		if ((in_pkts[i] = get_packet()) == NULL) {
			continue;
		}

		ret = npf_packet_handler(npf,
		    (struct mbuf **)&in_pkts[i], ifp, di);
		puts(ret ? "block" : "allow");
		if (ret) {
			/* The packet was blocked or destroyed. */
			continue;
		}
		out_pkts[out++] = in_pkts[i];
	}

	/* Send a burst of packets. */
	for (unsigned i = 0; i < out; i++) {
		/* TODO */
		rte_pktmbuf_free(out_pkts[i]);
	}
}

int
main(int argc, char **argv)
{
	npf_t *npf;
	struct ifnet *ifp;
	nl_config_t *ncf;

	/* Initialise DPDK and NPF. */
	dpdk_init(argc, argv);
	npf_dpdk_init(pktmbuf_pool);

	/* Create a new NPF instance. */
	npf = npf_dpdk_create();
	assert(npf != NULL);

	/* Attach a virtual interface to NPF. */
	ifp = npf_dpdk_ifattach(npf, "dpdk0", 1);
	assert(ifp != NULL);

	/* Create NPF configuration (ruleset) and load it. */
	ncf = create_npf_config();
	load_npf_config(npf, ncf);

	/*
	 * Process the packets.  Note: before processing packets,
	 * each thread doing that must register with NPF instance.
	 */
	npf_thread_register(npf);
	process_packets(npf, ifp, PFIL_IN);

	npf_destroy(npf);
	return 0;
}
