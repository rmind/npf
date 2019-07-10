/*	$NetBSD: npf_alg_pptp.c,v 1.02 2019/06/17 19:23:41 alexk99 Exp $	*/

/*-
 * Copyright (c) 2010 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This material is based upon work partially supported by The
 * NetBSD Foundation under a contract with Mindaugas Rasiukevicius.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * NPF ALG for PPTP translations.
 */

#ifdef _KERNEL
#include <sys/cdefs.h>
__KERNEL_RCSID(0,
		  "$NetBSD: npf_alg_pptp.c,v 1.00 2019/06/17 19:23:41 alexk99 Exp $");

#include <sys/param.h>
#include <sys/module.h>

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <net/pfil.h>
#endif

#define __NPF_CONN_PRIVATE
#include "stand/cext.h"
#include "npf_impl.h"
#include "npf_conn.h"
#include "npf_pptp_gre.h"
#include "npf_print_debug.h"
#include "npf_conn.h"
#include "npf_impl.h"

MODULE(MODULE_CLASS_MISC, npf_alg_pptp, "npf");

#define	PPTP_SERVER_PORT	1723

struct npf_pptp_alg {
	npf_alg_t *alg_pptp_tcp;
	npf_alg_t *alg_pptp_gre;
	npf_portmap_t *pm;
	npf_portmap_params_t pm_params;
};

static struct npf_pptp_alg pptp_alg;

/* PPTP messages types */
#define PPTP_CTRL_MSG 1

/* Control message types */
#define PPTP_OUTGOING_CALL_REQUEST    7
#define PPTP_OUTGOING_CALL_REPLY      8
#define PPTP_CALL_CLEAR_REQUEST       12
#define PPTP_CALL_DISCONNECT_NOTIFY   13
#define PPTP_WAN_ERROR_NOTIFY         14

#define PPTP_OUTGOING_CALL_MIN_LEN   32

#define PPTP_MAGIC_COOKIE    0x1A2B3C4D

/* Maximum number of GRE connections
 * a host can establish to the same server
 */
#define PPTP_MAX_GRE_PER_CLIENT        4

/* PPTP ALG argument flags */
#define PPTP_ALG_FL_GRE_STATE_ESTABLISHED 0x1
#define PPTP_ALG_FL_FREE_ENTRY            0x2

struct pptp_msg_hdr {
	uint16_t len;
	uint16_t pptp_msg_type;
	uint32_t magic_cookie;
	uint16_t ctrl_msg_type;
	uint16_t rsvd0;
	uint16_t call_id;
}
__packed;

struct pptp_outgoing_call_req {
	struct pptp_msg_hdr hdr;
	uint16_t call_serial_nb;
	uint32_t min_bps;
	uint32_t max_bps;
	uint32_t bearer_type;
	uint16_t framing_type;
	/* etc */
}
__packed;

struct pptp_outgoing_call_reply {
	struct pptp_msg_hdr hdr;
	uint16_t peer_call_id;
	uint8_t  result_code;
	uint8_t  err_code;
	uint16_t cause_code;
	/* etc */
}
__packed;

/*
 * pptp gre connection
 */
struct pptp_gre_con {
	union {
		struct {
			/* all call id values use network byte order */
			struct pptp_gre_context ctx; /* client and server call ids*/
			uint16_t orig_client_call_id; /* original client call id */
			uint16_t flags;
		};

		uint64_t u64;
	};
};

/*
 * TCP PPTP NAT ALG datum.
 * Associated with a tcp connection via
 * npf_nat::nt_alg_arg
 */
struct pptp_alg_arg
{
	struct pptp_gre_con gre_cons[PPTP_MAX_GRE_PER_CLIENT];
};

/*
 * npfa_icmp_match: matching inspector determines ALG case and associates
 * our ALG with the NAT entry.
 */
static bool
npfa_pptp_tcp_match(npf_cache_t *npc, npf_nat_t *nt, int di)
{
	const uint16_t proto = npc->npc_proto;

	KASSERT(npf_iscached(npc, NPC_IP46));

	/* note: only the outbound NAT is supported */
	if (di != PFIL_OUT || proto != IPPROTO_TCP || npc->npc_l4.tcp == NULL ||
			  npc->npc_l4.tcp->th_dport != htons(PPTP_SERVER_PORT))
		return false;

	/* Associate ALG with translation entry. */
	npf_nat_setalg(nt, pptp_alg.alg_pptp_tcp, 0);
	return true;
}

/*
 *
 */
static int
npfa_pptp_gre_establish_gre_conn(npf_cache_t *npc, int di,
		  struct pptp_gre_con *gre_con, npf_nat_t *pptp_tcp_nt)
{
	npf_conn_t *con = NULL;
	int ret;

	NPF_DPRINTFCL(NPF_DC_PPTP_ALG, 50,
			  "establishing a new pptp gre connection: client call_id %hu, "
			  "server call_id %hu, orig client call_id %hu\n",
			  gre_con->ctx.client_call_id, gre_con->ctx.server_call_id,
			  gre_con->orig_client_call_id);

	/* establish new gre connection state */
	con = npf_conn_establish(npc, di, true);
	if (con == NULL) {
		NPF_DPRINTFCL(NPF_DC_PPTP_ALG, 50,
				  "failed to establish pptp gre connection\n");
		return ENOMEM;
	}
	else {
		NPF_DPRINTFCL(NPF_DC_PPTP_ALG, 50,
				  "new pptp gre connection is established, flags %u\n",
				  con->c_flags);
	}

	NPF_HEX_DUMPCL(NPF_DC_PPTP_ALG, 50,
			  "gre forw key",
			  npf_conn_getforwkey(con),
			  NPF_CONNKEY_V4WORDS * sizeof(uint32_t));
	NPF_HEX_DUMPCL(NPF_DC_PPTP_ALG, 50,
			  "gre back key",
			  npf_conn_getbackkey(con, NPF_CONNKEY_ALEN(npf_conn_getforwkey(con))),
			  NPF_CONNKEY_V4WORDS * sizeof(uint32_t));

	/*
	 * Create a new nat entry for created GRE connection.
	 * Use the same nat policy as the parent PPTP TCP control connection uses.
	 * Associate created nat entry with the gre connection.
	 */
	ret = npf_nat_share_policy(npc, con, pptp_tcp_nt);
	if (ret)
		return ret;

	/* associate GRE ALG with the gre connection */
	npf_nat_setalg(con->c_nat, pptp_alg.alg_pptp_gre, (uintptr_t)gre_con->u64);

	NPF_DPRINTFCL(NPF_DC_PPTP_ALG, 50,
			  "new pptp gre connection's nat %p\n", con->c_nat);

	/* make gre connection active and pass */
	npf_conn_set_active_pass(con);

	gre_con->flags |= PPTP_ALG_FL_GRE_STATE_ESTABLISHED;
	return 0;
}

static uint16_t
npfa_translated_call_id_get(uint32_t ip)
{
	in_port_t port;
	npf_addr_t addr;

	addr.word32[0] = ip;
	port = npf_portmap_get_pm(pptp_alg.pm, &pptp_alg.pm_params, sizeof(ip),
			  &addr);

	NPF_DPRINTFCL(NPF_DC_PPTP_ALG, 50,
			  "pptp alg: get call_id %hu from "
			  "the pormap ip %u\n", port, ip);

	return (uint16_t)port;
}

static void
npfa_translated_call_id_put(uint32_t ip, uint16_t call_id)
{
	npf_addr_t addr;
	addr.word32[0] = ip;

	npf_portmap_put_pm(pptp_alg.pm, sizeof(ip), &addr, (in_port_t)call_id);

	NPF_DPRINTFCL(NPF_DC_PPTP_ALG, 50,
			  "pptp alg: put call_id %hu to "
			  "the pormap ip %u\n", call_id, ip);
}

/* lookup for and explicitly expire
 * the associatiated gre connection state
 */
static void
npfa_pptp_expire_gre_con(npf_t *npf, struct pptp_gre_con *gre_con,
		  uint32_t client_ip, uint32_t server_ip)
{
	npf_conn_t *con;
	npf_connkey_t key;
	bool forw;

	NPF_DPRINTFCL(NPF_DC_PPTP_ALG, 50,
			  "expire gre con: orig client call_id %hu, client call_id %hu, "
			  "server call_id %hu, flags %hu, cl %u, srv %u\n",
			  gre_con->orig_client_call_id, gre_con->ctx.client_call_id,
			  gre_con->ctx.server_call_id, gre_con->flags, client_ip, server_ip);

	/* exit if gre connection was not established */
	if ((gre_con->flags & PPTP_ALG_FL_GRE_STATE_ESTABLISHED) == 0)
		return;

	/* return translated call-id value back to the portmap */
	if (gre_con->ctx.client_call_id != 0) {
		npfa_translated_call_id_put(server_ip, gre_con->ctx.client_call_id);
		NPF_DPRINTFCL(NPF_DC_PPTP_ALG, 50,
				  "pptp control connection: put call_id %hu back to "
				  "the pormap ip %u\n", gre_con->ctx.client_call_id, server_ip);
	}

	/* init a forward gre key */
	npf_conn_init_ipv4_key((void *)&key, IPPROTO_GRE,
			  gre_con->ctx.server_call_id, 0, client_ip, server_ip);

	NPF_HEX_DUMPCL(NPF_DC_PPTP_ALG, 50, "gre key", &key,
			  NPF_CONNKEY_V4WORDS * sizeof(uint32_t));

	/* lookup for the associated pptp gre connection */
	con = npf_conndb_lookup(npf->conn_db, &key, &forw);
	if (con == NULL) {
		NPF_DPRINTFCL(NPF_DC_PPTP_ALG, 50,
			  "pptp control connection: associated gre conn not found, "
			  "server call_id %hu\n", gre_con->ctx.server_call_id);
		return;
	}

	NPF_DPRINTFCL(NPF_DC_PPTP_ALG, 50,
			  "pptp control connection: expire associated gre conn "
			  "server call_id %hu\n", gre_con->ctx.server_call_id);

	/* mark the gre connection as expired */
	npf_conn_set_expire(con);

	gre_con->flags &= ~PPTP_ALG_FL_GRE_STATE_ESTABLISHED;
	gre_con->flags |= PPTP_ALG_FL_FREE_ENTRY;

	return;
}

/*
 * Allocate and init pptp alg arg
 */
static struct pptp_alg_arg *
npfa_pptp_alg_arg_init(void)
{
	int i;
	struct pptp_alg_arg *alg_arg;

	/* allocate */
	alg_arg = kmem_zalloc(sizeof(struct pptp_alg_arg), KM_SLEEP);
	if (alg_arg == NULL)
		return NULL;

	/* mark all entries as empty */
	for (i = 0; i < PPTP_MAX_GRE_PER_CLIENT; i++)
		alg_arg->gre_cons[i].flags = PPTP_ALG_FL_FREE_ENTRY;

	return alg_arg;
}

/*
 *
 */
static struct pptp_gre_con *
npfa_pptp_alg_arg_lookup(struct pptp_alg_arg *arg)
{
	int i;

	for (i = 0; i < PPTP_MAX_GRE_PER_CLIENT; i++)
		if (arg->gre_cons[i].flags & PPTP_ALG_FL_FREE_ENTRY)
			return &arg->gre_cons[i];

	return NULL;
}

/*
 *
 */
static struct pptp_gre_con *
npfa_pptp_alg_arg_lookup_with_server_call_id(struct pptp_alg_arg *arg,
		  uint16_t server_call_id)
{
	int i;
	struct pptp_gre_con *gre_con;

	for (i = 0; i < PPTP_MAX_GRE_PER_CLIENT; i++) {
		gre_con = &arg->gre_cons[i];

		if ((gre_con->flags & PPTP_ALG_FL_FREE_ENTRY) == 0 &&
				  gre_con->ctx.server_call_id == server_call_id)
			return gre_con;
	}

	return NULL;
}

/*
 *
 */
static struct pptp_gre_con *
npfa_pptp_alg_arg_lookup_with_orig_client_call_id(struct pptp_alg_arg *arg,
		  uint16_t client_call_id)
{
	int i;
	struct pptp_gre_con *gre_con;

	for (i = 0; i < PPTP_MAX_GRE_PER_CLIENT; i++) {
		gre_con = &arg->gre_cons[i];

		if ((gre_con->flags & PPTP_ALG_FL_FREE_ENTRY) == 0 &&
				  gre_con->orig_client_call_id == client_call_id)
			return gre_con;
	}

	return NULL;
}

/*
 *
 */
static struct pptp_gre_con *
npfa_pptp_alg_arg_lookup_with_client_call_id(struct pptp_alg_arg *arg,
		  uint16_t client_call_id)
{
	int i;
	struct pptp_gre_con *gre_con;

	for (i = 0; i < PPTP_MAX_GRE_PER_CLIENT; i++) {
		gre_con = &arg->gre_cons[i];

		if ((gre_con->flags & PPTP_ALG_FL_FREE_ENTRY) == 0 &&
				  gre_con->ctx.client_call_id == client_call_id)
			return gre_con;
	}

	return NULL;
}

/*
 * PPTP TCP control connection ALG translator.
 * It rewrites Call ID in the Outgoing-Call-Request
 * message and Peer Call ID in the Outgoing-Call-Reply message.
 */
static bool
npfa_pptp_tcp_translate(npf_cache_t *npc, npf_nat_t *nt, bool forw)
{
	uint16_t cksum;
	uint16_t old_call_id;
	uint32_t tcp_hdr_size;
	uint32_t ip;
	nbuf_t *nbuf;
	struct tcphdr *tcp;
	struct pptp_msg_hdr *pptp;
	struct pptp_outgoing_call_reply *pptp_call_reply;
	struct pptp_alg_arg *alg_arg;
	struct pptp_gre_con *gre_con;
	npf_cache_t gre_npc;
	npf_addr_t *o_addr;
	in_port_t o_port;

	/* only ipv4 is supported so far */
	if (!(npf_iscached(npc, NPC_IP4) && npf_iscached(npc, NPC_TCP) &&
			  (npc->npc_l4.tcp->th_dport == htons(PPTP_SERVER_PORT) ||
			  npc->npc_l4.tcp->th_sport == htons(PPTP_SERVER_PORT))))
		return false;

	nbuf = npc->npc_nbuf;
	tcp = npc->npc_l4.tcp;
	tcp_hdr_size = tcp->th_off << 2;
	nbuf_reset(nbuf);

	pptp = nbuf_advance(nbuf, npc->npc_hlen + tcp_hdr_size,
			  sizeof(struct pptp_msg_hdr));
	if (pptp == NULL)
		return false;

	if (pptp->pptp_msg_type != htons(PPTP_CTRL_MSG) ||
			  pptp->len < htons(PPTP_OUTGOING_CALL_MIN_LEN) ||
			  pptp->magic_cookie != htonl(PPTP_MAGIC_COOKIE))
		return false;

	/* get alg arg */
	alg_arg = (struct pptp_alg_arg *)npf_nat_get_alg_arg(nt);
	if (alg_arg == NULL) {
		/* init alg arg */
		alg_arg = npfa_pptp_alg_arg_init();
		if (alg_arg == NULL)
			return false;
		npf_nat_set_alg_arg(nt, (uintptr_t)alg_arg);
	}

	NPF_DPRINTFCL(NPF_DC_PPTP_ALG, 50,
			  "pptp alg: received pptp msg, type %hu\n",
			  ntohs(pptp->ctrl_msg_type));

	switch (ntohs(pptp->ctrl_msg_type)) {
	case PPTP_OUTGOING_CALL_REQUEST:
		if (pptp->len < sizeof(struct pptp_outgoing_call_req))
			return false;
		/* lookup by client call id */
		gre_con = npfa_pptp_alg_arg_lookup_with_orig_client_call_id(alg_arg,
				  pptp->call_id);
		if (gre_con != NULL) {
			/* expire old connection before using this entry */
			if (gre_con->flags & PPTP_ALG_FL_GRE_STATE_ESTABLISHED)
				npfa_pptp_expire_gre_con(npc->npc_ctx, gre_con,
						  npc->npc_ips[NPF_SRC]->word32[0],
						  npc->npc_ips[NPF_DST]->word32[0]);
		} else {
			/* lookup for an empty gre connection entry */
			gre_con = npfa_pptp_alg_arg_lookup(alg_arg);
			if (gre_con == NULL)
				/* all entries are in use */
				return false;
			NPF_DPRINTFCL(NPF_DC_PPTP_ALG, 50, "start new gre connection\n");
		}

		/* save client call id  */
		gre_con->orig_client_call_id = pptp->call_id;

		/* get translated call id value.
		 * it should be a unique value within the scope
		 * of all pptp connection distinated to the same server.
		 * Note: it's better to use the source address scope, but
		 * the translated source ip address is not known at this point,
		 * since alg->translate() executed before the normal NAT translation.
		 */
		ip = npc->npc_ips[NPF_DST]->word32[0]; /* pptp server ip */
		gre_con->ctx.client_call_id = npfa_translated_call_id_get(ip);
		if (gre_con->ctx.client_call_id == 0)
			/* no free call id values */
			break;
		NPF_DPRINTFCL(NPF_DC_PPTP_ALG, 50,
				  "pptp control connection: get call_id %hu from "
				  "the pormap ip %u\n",
				  gre_con->ctx.client_call_id, ip);

		/* rewrite client call id */
		pptp->call_id = gre_con->ctx.client_call_id;
		/* fix checksum */
		cksum = tcp->check;
		tcp->check = npf_fixup16_cksum(cksum, gre_con->orig_client_call_id,
				  gre_con->ctx.client_call_id);
		/* use the entry */
		gre_con->flags &= ~PPTP_ALG_FL_FREE_ENTRY;
		break;

	case PPTP_OUTGOING_CALL_REPLY:
		if (pptp->len < sizeof(struct pptp_outgoing_call_reply))
			return false;
		pptp_call_reply = (struct pptp_outgoing_call_reply *)pptp;

		/* lookup a gre connection */
		gre_con = npfa_pptp_alg_arg_lookup_with_client_call_id(alg_arg,
				  pptp_call_reply->peer_call_id);
		if (gre_con == NULL) {
			NPF_DPRINTFCL(NPF_DC_PPTP_ALG, 50,
					  "gre connection not found: client call_id %hu\n",
					  pptp_call_reply->peer_call_id);
			return false;
		}

		/* rewrite Peer Call ID */
		old_call_id = pptp_call_reply->peer_call_id;
		pptp_call_reply->peer_call_id = gre_con->orig_client_call_id;
		/* save server call id */
		gre_con->ctx.server_call_id = pptp_call_reply->hdr.call_id;

		/* fix checksum */
		cksum = tcp->check;
		tcp->check = npf_fixup16_cksum(cksum, old_call_id,
				  pptp_call_reply->peer_call_id);

		/* if client and server call ids have been seen,
		 * create new gre connection state entry
		 */
		if (gre_con->ctx.client_call_id != 0 &&
				  gre_con->ctx.server_call_id != 0 &&
				  gre_con->orig_client_call_id != 0) {
			/* create pptp gre context cache */
			memcpy(&gre_npc, npc, sizeof(npf_cache_t));
			gre_npc.npc_proto = IPPROTO_GRE;
			gre_npc.npc_info = NPC_IP46 | NPC_LAYER4 | NPC_ALG_PPTP_GRE_CTX;
			gre_npc.npc_l4.pptp_gre_ctx = &gre_con->ctx;
			/* setup ip addresses */
			npf_nat_getorig(nt, &o_addr, &o_port);
			gre_npc.npc_ips[NPF_SRC] = o_addr;
			gre_npc.npc_ips[NPF_DST] = npc->npc_ips[NPF_SRC];
			/* establish gre connection state and associate nat */
			npfa_pptp_gre_establish_gre_conn(&gre_npc, PFIL_OUT, gre_con, nt);
		}
		break;

	case PPTP_CALL_DISCONNECT_NOTIFY:
		if (pptp->len < sizeof(struct pptp_msg_hdr))
			return false;
		npf_nat_getorig(nt, &o_addr, &o_port);

		/* lookup for a gre connection entry */
		gre_con = npfa_pptp_alg_arg_lookup_with_server_call_id(alg_arg,
				  pptp->call_id);
		if (gre_con == NULL)
			return false;

		npfa_pptp_expire_gre_con(npc->npc_ctx, gre_con, o_addr->word32[0],
				  npc->npc_ips[NPF_SRC]->word32[0]);
		break;

	case PPTP_WAN_ERROR_NOTIFY:
		if (pptp->len < sizeof(struct pptp_msg_hdr))
			return false;

		NPF_DPRINTFCL(NPF_DC_PPTP_ALG, 50,
				  "pptp wan error notify received, peer's call_id %hu\n",
				  pptp->call_id);

		gre_con = npfa_pptp_alg_arg_lookup_with_client_call_id(alg_arg,
				  pptp->call_id);
		if (gre_con == NULL)
			return false;

		old_call_id = pptp->call_id;
		pptp->call_id = gre_con->orig_client_call_id;
		cksum = tcp->check;
		tcp->check = npf_fixup16_cksum(cksum, old_call_id,
				  gre_con->orig_client_call_id);
		break;

	default:
		return false;
	}

	return true;
}

/*
 *
 */
static void
npfa_pptp_tcp_destroy(npf_t *npf, npf_conn_t *con)
{
	struct pptp_gre_con *gre_con;
	struct pptp_alg_arg *alg_arg;
	npf_connkey_t *fw;
	uint32_t client_ip, server_ip;
	int i;

	NPF_DPRINTFCL(NPF_DC_PPTP_ALG, 50,
			  "pptp tcp alg destroy a tcp connection %p\n", con);

	alg_arg = (struct pptp_alg_arg *)npf_nat_get_alg_arg(con->c_nat);
	fw = npf_conn_getforwkey(con);

	/* only ipv4 is supported */
	if (NPF_CONNKEY_ALEN(fw) == sizeof(uint32_t) || alg_arg == NULL)
		return;

	client_ip = fw->ck_key[2];
	server_ip = fw->ck_key[3];

	NPF_DPRINTFCL(NPF_DC_PPTP_ALG, 50,
			  "pptp tcp alg destroy a tcp connection %p, "
			  "client ip %u, server ip %u\n",
			  con, client_ip, server_ip);

	for (i = 0; i < PPTP_MAX_GRE_PER_CLIENT; i++) {
		gre_con = &alg_arg->gre_cons[i];
		if ((gre_con->flags & PPTP_ALG_FL_FREE_ENTRY) == 0)
			npfa_pptp_expire_gre_con(npf, gre_con, client_ip, server_ip);
	}

	kmem_free(alg_arg, sizeof(struct pptp_alg_arg));
}

/*
 * PPTP GRE ALG translator.
 */
static bool
npfa_pptp_gre_translate(npf_cache_t *npc, npf_nat_t *nt, bool forw)
{
	nbuf_t *nbuf = npc->npc_nbuf;
	struct pptp_gre_hdr *gre;
	struct pptp_gre_con gre_con;

	if (forw || !npf_iscached(npc, NPC_IP4 | NPC_ALG_PPTP_GRE) ||
			  npc->npc_proto != IPPROTO_GRE)
		return false;

	nbuf_reset(nbuf);
	gre = nbuf_advance(nbuf, npc->npc_hlen, sizeof(struct pptp_gre_hdr));
	if (gre == NULL)
		return false;

	gre_con.u64 = (uint64_t)npf_nat_get_alg_arg(nt);

	KASSERT(gre->call_id == gre_con.ctx.client_call_id);
	NPF_DPRINTFCL(NPF_DC_PPTP_ALG, 60,
			  "gre call id translated %hu -> %hu, forw %d\n",
			  gre->call_id, gre_con.orig_client_call_id, forw);

	gre->call_id = gre_con.orig_client_call_id;
	return true;
}

/*
 * npf_alg_icmp_{init,fini,modcmd}: ICMP ALG initialization, destruction
 * and module interface.
 */

static int
npf_alg_pptp_init(npf_t *npf)
{
	static const npfa_funcs_t pptp_tcp = {
		.match     = npfa_pptp_tcp_match,
		.translate = npfa_pptp_tcp_translate,
		.inspect   = NULL,
		.destroy   = npfa_pptp_tcp_destroy,
	};

	static const npfa_funcs_t pptp_gre = {
		.match     = NULL,
		.translate = npfa_pptp_gre_translate,
		.inspect   = NULL,
		.destroy   = NULL,
	};

	/* call_id range */
	pptp_alg.pm_params.min_port = 1;
	pptp_alg.pm_params.max_port = UINT16_MAX;

	pptp_alg.pm = npf_portmap_init_pm();
	if (pptp_alg.pm == NULL)
		return ENOMEM;

	pptp_alg.alg_pptp_tcp = npf_alg_register(npf, "pptp_tcp", &pptp_tcp);
	if (pptp_alg.alg_pptp_tcp == NULL)
		return ENOMEM;

	pptp_alg.alg_pptp_gre = npf_alg_register(npf, "pptp_gre", &pptp_gre);
	if (pptp_alg.alg_pptp_tcp == NULL) {
		npf_alg_unregister(npf, pptp_alg.alg_pptp_tcp);
		return ENOMEM;
	} else {
		return 0;
	}
}

static int
npf_alg_pptp_fini(npf_t *npf)
{
	KASSERT(alg_pptp_tcp != NULL);
	KASSERT(alg_pptp_gre != NULL);
	npf_portmap_fini_pm(pptp_alg.pm);
	npf_alg_unregister(npf, pptp_alg.alg_pptp_tcp);
	return npf_alg_unregister(npf, pptp_alg.alg_pptp_gre);
}

#ifdef _KERNEL
static int
npf_alg_pptp_modcmd(modcmd_t cmd, void *arg)
{
	npf_t *npf = npf_getkernctx();
#else
int
npf_alg_pptp_modcmd(modcmd_t cmd, void *arg)
{
	npf_t *npf = arg;
#endif

	switch (cmd) {
	case MODULE_CMD_INIT:
		return npf_alg_pptp_init(npf);
	case MODULE_CMD_FINI:
		return npf_alg_pptp_fini(npf);
	case MODULE_CMD_AUTOUNLOAD:
		return EBUSY;
	default:
		return ENOTTY;
	}
	return 0;
}
