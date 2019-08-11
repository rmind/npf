/*	$NetBSD: npf_alg_pptp.c,v 1.02 2019/06/17 19:23:41 alexk99 Exp $	*/

/*-
 * Copyright (c) 2019 Alex Kiselev <alex at therouter net>
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
#include "npf_impl.h"
#include "npf_conn.h"

MODULE(MODULE_CLASS_MISC, npf_alg_pptp, "npf");

#define	PPTP_SERVER_PORT	1723

#define	IPV4_ADDR(ip)		(ip)->word32[0]

typedef struct {
	npf_alg_t *	alg_pptp_tcp;
	npf_alg_t *	alg_pptp_gre;
	npf_portmap_t *	pm;
	npf_portmap_params_t	pm_params;
} npf_pptp_alg_t;

static npf_pptp_alg_t pptp_alg;

/* PPTP messages types */
#define PPTP_CTRL_MSG 1

/* Control message types */
#define	PPTP_OUTGOING_CALL_REQUEST		7
#define	PPTP_OUTGOING_CALL_REPLY		8
#define	PPTP_CALL_CLEAR_REQUEST			12
#define	PPTP_CALL_DISCONNECT_NOTIFY		13
#define	PPTP_WAN_ERROR_NOTIFY			14

#define	PPTP_OUTGOING_CALL_MIN_LEN		32

#define	PPTP_MAGIC_COOKIE				0x1A2B3C4D

/*
 * Maximum number of GRE connections
 * a host can establish to the same server
 */
#define	PPTP_MAX_GRE_PER_CLIENT			4

/* PPTP ALG argument flags */
#define	PPTP_ALG_FL_GRE_STATE_ESTABLISHED 0x1
#define	PPTP_ALG_FL_FREE_SLOT             0x2

typedef struct {
	uint16_t client_call_id;
	uint16_t server_call_id;
} __packed pptp_gre_context_t;

/*
 * PPTP GRE header
 */
typedef struct {
	uint16_t flags_ver;
	uint16_t proto;
	uint16_t payload_len;
	uint16_t call_id;
	uint16_t seq_num;
} __packed pptp_gre_hdr_t;

/*
 * PPTP TCP messages
 */
typedef struct {
	uint16_t	len;
	uint16_t	pptp_msg_type;
	uint32_t	magic_cookie;
	uint16_t	ctrl_msg_type;
	uint16_t	rsvd0;
	uint16_t	call_id;
} __packed pptp_msg_hdr_t;

typedef struct {
	pptp_msg_hdr_t	hdr;
	uint16_t	call_serial_nb;
	uint32_t	min_bps;
	uint32_t	max_bps;
	uint32_t	bearer_type;
	uint16_t	framing_type;
	/* etc */
} __packed pptp_outgoing_call_req_t;

typedef struct {
	pptp_msg_hdr_t	hdr;
	uint16_t	peer_call_id;
	uint8_t	result_code;
	uint8_t	err_code;
	uint16_t	cause_code;
	/* etc */
} __packed pptp_outgoing_call_reply_t;

/*
 * pptp gre connection slot
 */
typedef union {
	struct {
		/* all call id values use network byte order */
		pptp_gre_context_t	ctx; /* client and server call ids */
		uint16_t		orig_client_call_id; /* original client call id */
		uint16_t		flags;
	};

	uint64_t	u64;
} pptp_gre_con_slot_t;

/*
 * TCP PPTP NAT ALG datum.
 * Associated with a tcp connection via
 * npf_nat::nt_alg_arg
 */
typedef struct {
	pptp_gre_con_slot_t	gre_con_slots[PPTP_MAX_GRE_PER_CLIENT];
	/* lock to protect gre connection slots */
	kmutex_t	lock;
} pptp_gre_conns_t;

/*
 * npfa_icmp_match: matching inspector determines ALG case and associates
 * our ALG with the NAT entry.
 */
static bool
npfa_pptp_tcp_match(npf_cache_t *npc, npf_nat_t *nt, int di)
{
	KASSERT(npf_iscached(npc, NPC_IP46));

	/* note: only the outbound NAT is supported */
	if (di != PFIL_OUT || !npf_iscached(npc, NPC_TCP) ||
			  npc->npc_l4.tcp->th_dport != htons(PPTP_SERVER_PORT))
		return false;

	/* Associate ALG with translation entry. */
	npf_nat_setalg(nt, pptp_alg.alg_pptp_tcp, 0);
	return true;
}

static int
npfa_pptp_gre_establish_gre_conn(npf_cache_t *npc, int di,
    pptp_gre_con_slot_t *gre_slot, npf_nat_t *pptp_tcp_nt)
{
	npf_conn_t *con = NULL;
	int ret;

	/* establish new gre connection state */
	con = npf_conn_establish(npc, di, true);
	if (con == NULL)
		return ENOMEM;

	/*
	 * Create a new nat entry for created GRE connection.
	 * Use the same nat policy as the parent PPTP TCP control connection uses.
	 * Associate created nat entry with the gre connection.
	 */
	ret = npf_nat_share_policy(npc, con, pptp_tcp_nt);
	if (ret) {
		npf_conn_expire(con);
		npf_conn_release(con);
		return ret;
	}

	/* associate GRE ALG with the gre connection */
	npf_nat_setalg(con->c_nat, pptp_alg.alg_pptp_gre, (uintptr_t)gre_slot->u64);

	/* make gre connection active and pass */
	npf_conn_setpass(con, NULL, NULL);
	npf_conn_release(con);

	gre_slot->flags |= PPTP_ALG_FL_GRE_STATE_ESTABLISHED;
	return 0;
}

static inline uint16_t
npfa_translated_call_id_get(const npf_addr_t *ip)
{
	return (uint16_t)npf_portmap_get_pm(pptp_alg.pm, &pptp_alg.pm_params,
	    sizeof(uint32_t), ip);
}

static inline void
npfa_translated_call_id_put(const npf_addr_t *ip, uint16_t call_id)
{
	npf_portmap_put_pm(pptp_alg.pm, sizeof(uint32_t), ip, (in_port_t)call_id);
}

/*
 * Free the gre slot and expire the gre connection associated with it.
 */
static void
npfa_pptp_gre_slot_free(npf_t *npf, pptp_gre_con_slot_t *gre_slot,
    npf_addr_t **ips)
{
	npf_conn_t *gre_con;
	uint16_t ids[2];
	bool forw;

	/* expire the gre connection associated with the slot */
	if (gre_slot->flags & PPTP_ALG_FL_GRE_STATE_ESTABLISHED) {
		npf_connkey_t key;

		/* init a forward gre key */
		ids[NPF_SRC] = gre_slot->ctx.server_call_id;
		ids[NPF_DST] = 0;
		npf_connkey_setkey((void *)&key, IPPROTO_GRE, ips, ids, sizeof(uint32_t),
		    true);

		/* lookup for the associated pptp gre connection */
		gre_con = npf_conndb_lookup(npf->conn_db, &key, &forw);
		if (gre_con != NULL) {
			/*
			 * mark the gre connection as expired.
			 * note: translated call-id will be put back to the portmap
			 * by gre connection destructor
			 */
			npf_conn_expire(gre_con);
			npf_conn_release(gre_con);
		}
		gre_slot->flags &= ~PPTP_ALG_FL_GRE_STATE_ESTABLISHED;
	} else if (gre_slot->ctx.client_call_id != 0) {
		/* return translated call-id value back to the portmap */
		npfa_translated_call_id_put(ips[NPF_DST], gre_slot->ctx.client_call_id);
	}

	/* free gre slot in the parent tcp connection nat arg */
	gre_slot->flags |= PPTP_ALG_FL_FREE_SLOT;

	return;
}

/*
 * Allocate and init pptp alg connections
 */
static pptp_gre_conns_t *
npfa_pptp_gre_conns_init(void)
{
	pptp_gre_conns_t *gre_conns;

	/* allocate */
	gre_conns = kmem_intr_zalloc(sizeof(pptp_gre_conns_t), KM_SLEEP);
	if (gre_conns == NULL)
		return NULL;

	/* mark all slots as empty */
	for (int i = 0; i < PPTP_MAX_GRE_PER_CLIENT; i++)
		gre_conns->gre_con_slots[i].flags = PPTP_ALG_FL_FREE_SLOT;

	mutex_init(&gre_conns->lock, MUTEX_DEFAULT, IPL_SOFTNET);

	return gre_conns;
}

/*
 * Destroy pptp alg arg
 */
static void
npfa_pptp_gre_conns_fini(pptp_gre_conns_t *gre_conns)
{
	mutex_destroy(&gre_conns->lock);
	kmem_intr_free(gre_conns, sizeof(pptp_gre_conns_t));
}

/*
 * Find a free slot or reuse one with the same orig_client_call_id.
 * There must be only one slot with the same orig_client_call_id.
 *
 * Result:
 *   NULL - no empty slots or slot to reuse
 *   otherwise - a reference to a slot marked as used
 *      and into which client_call_id and trans_client_call_id are written
 */
static pptp_gre_con_slot_t *
npfa_pptp_gre_slot_lookup_and_use(npf_cache_t *npc, pptp_gre_conns_t *gre_conns,
    uint16_t client_call_id, uint16_t trans_client_call_id)
{
	pptp_gre_con_slot_t *slot, *empty_slot, old_reused_slot, new_slot;
	bool reuse_slot;

	reuse_slot = false;
	empty_slot = NULL;

	mutex_enter(&gre_conns->lock);

	/* scan all slots to ensure that there is no one using the call_id */
	for (int i = 0; i < PPTP_MAX_GRE_PER_CLIENT; i++) {
		slot = &gre_conns->gre_con_slots[i];
		/*
		 * if call_id is already in use by a slot, then
		 * expire associated GRE connection and reuse the slot
		 */
		if (slot->flags & PPTP_ALG_FL_FREE_SLOT) {
			/* empty slot */
			empty_slot = slot;
		}
		else if (slot->orig_client_call_id == client_call_id) {
			reuse_slot = true;
			old_reused_slot.u64 = slot->u64;
			break;
		}
	}

	/* use empty slot or reuse a slot with the same client_call_id */
	if (reuse_slot || empty_slot != NULL) {
		if (!reuse_slot)
			slot = empty_slot;

		new_slot.orig_client_call_id = client_call_id;
		new_slot.ctx.client_call_id = trans_client_call_id;
		new_slot.ctx.server_call_id = 0;
		new_slot.flags = 0;

		slot->u64 = new_slot.u64;
	}

	mutex_exit(&gre_conns->lock);

	if (reuse_slot) {
		npfa_pptp_gre_slot_free(npc->npc_ctx, &old_reused_slot, npc->npc_ips);
		return slot;
	}

	return empty_slot;
}

/*
 * Lookup for an empty gre slot with original server call_id
 */
static pptp_gre_con_slot_t *
npfa_pptp_gre_slot_lookup_with_server_call_id(pptp_gre_conns_t *gre_conns,
    uint16_t server_call_id)
{
	KASSERT(mutex_owned(&gre_conns->lock));

	for (int i = 0; i < PPTP_MAX_GRE_PER_CLIENT; i++) {
		pptp_gre_con_slot_t *gre_slot = &gre_conns->gre_con_slots[i];

		if ((gre_slot->flags & PPTP_ALG_FL_FREE_SLOT) == 0 &&
				  gre_slot->ctx.server_call_id == server_call_id)
			return gre_slot;
	}

	return NULL;
}

/*
 * Lookup for an empty gre slot with client call id
 */
static pptp_gre_con_slot_t *
npfa_pptp_gre_slot_lookup_with_client_call_id(pptp_gre_conns_t *gre_conns,
    uint16_t call_id)
{
	KASSERT(mutex_owned(&gre_conns->lock));

	for (int i = 0; i < PPTP_MAX_GRE_PER_CLIENT; i++) {
		pptp_gre_con_slot_t *gre_slot = &gre_conns->gre_con_slots[i];

		if ((gre_slot->flags & PPTP_ALG_FL_FREE_SLOT) == 0 &&
				  gre_slot->ctx.client_call_id == call_id)
			return gre_slot;
	}

	return NULL;
}

/*
 * Init and setup new alg arg
 */
static pptp_gre_conns_t *
npfa_pptp_gre_conns_alloc(npf_nat_t *nt)
{
	pptp_gre_conns_t *res_gre_conns;
	pptp_gre_conns_t *new_gre_conns;

	new_gre_conns = npfa_pptp_gre_conns_init();
	if (new_gre_conns == NULL)
		return NULL;

	res_gre_conns = npf_nat_cas_alg_arg(nt, (uintptr_t)NULL,
			  (uintptr_t)new_gre_conns);
	if (res_gre_conns != NULL) {
		/* someone else has already allocated arg before us */
		npfa_pptp_gre_conns_fini(new_gre_conns);
		return res_gre_conns;
	}

	return new_gre_conns;
}

/*
 * PPTP TCP control connection ALG translator.
 * It rewrites Call ID in the Outgoing-Call-Request
 * message and Peer Call ID in the Outgoing-Call-Reply message.
 */
static bool
npfa_pptp_tcp_translate(npf_cache_t *npc, npf_nat_t *nt, bool forw)
{
	uint16_t old_call_id;
	uint16_t trans_client_call_id;
	uint16_t orig_client_call_id;
	in_port_t o_port;
	uint32_t tcp_hdr_size;
	nbuf_t *nbuf;
	struct tcphdr *tcp;
	pptp_msg_hdr_t *pptp;
	pptp_outgoing_call_reply_t *pptp_call_reply;
	pptp_gre_conns_t *gre_conns;
	pptp_gre_con_slot_t *gre_slot;
	npf_addr_t *o_addr, *ip;
	npf_addr_t *ips[2];
	npf_cache_t gre_npc;

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
	    sizeof(pptp_msg_hdr_t));
	if (pptp == NULL)
		return false;

	if (pptp->pptp_msg_type != htons(PPTP_CTRL_MSG) ||
	    pptp->len < htons(PPTP_OUTGOING_CALL_MIN_LEN) ||
	    pptp->magic_cookie != htonl(PPTP_MAGIC_COOKIE))
		return false;

	/* get or allocate alg arg (gre connections) */
	gre_conns = (pptp_gre_conns_t *)npf_nat_get_alg_arg(nt);
	if (gre_conns == NULL) {
		gre_conns = npfa_pptp_gre_conns_alloc(nt);
		if (gre_conns == NULL)
			return false;
	}

	switch (ntohs(pptp->ctrl_msg_type)) {
	case PPTP_OUTGOING_CALL_REQUEST:
		if (pptp->len < sizeof(pptp_outgoing_call_req_t))
			return false;

		/*
		 * get translated call id value.
		 * it should be a unique value within the scope
		 * of all pptp connection distinated to the same server.
		 * Note: it's better to use the source address scope, but
		 * the translated source ip address is not known at this point,
		 * since alg->translate() executed before the normal NAT translation.
		 */
		ip = npc->npc_ips[NPF_DST]; /* pptp server ip */
		trans_client_call_id = npfa_translated_call_id_get(ip);
		if (trans_client_call_id == 0)
			return false;

		/*
		 * lookup for an empty gre slot or
		 * reuse one with the same original call_id
		 */
		gre_slot = npfa_pptp_gre_slot_lookup_and_use(npc, gre_conns,
		    pptp->call_id, trans_client_call_id);
		if (gre_slot == NULL) {
			/* all entries are in use */
			npfa_translated_call_id_put(ip, trans_client_call_id);
			return false;
		}

		/* rewrite client call id */
		old_call_id = pptp->call_id;
		pptp->call_id = trans_client_call_id;
		tcp->check = npf_fixup16_cksum(tcp->check, old_call_id,
				  trans_client_call_id);
		break;

	case PPTP_OUTGOING_CALL_REPLY:
		if (pptp->len < sizeof(pptp_outgoing_call_reply_t))
			return false;
		pptp_call_reply = (pptp_outgoing_call_reply_t *)nbuf_ensure_contig(nbuf,
		    sizeof(pptp_outgoing_call_reply_t));

		/* lookup a gre connection */
		mutex_enter(&gre_conns->lock);
		gre_slot = npfa_pptp_gre_slot_lookup_with_client_call_id(gre_conns,
		    pptp_call_reply->peer_call_id);
		/* slot is not found or call reply message has been already received */
		if (gre_slot == NULL || gre_slot->ctx.server_call_id != 0) {
			mutex_exit(&gre_conns->lock);
			return false;
		}

		/* save server call id */
		gre_slot->ctx.server_call_id = pptp_call_reply->hdr.call_id;

		/*
		 * if client and server call ids have been seen,
		 * create new gre connection state entry
		 */
		if (gre_slot->ctx.client_call_id != 0 &&
		    gre_slot->ctx.server_call_id != 0 &&
		    gre_slot->orig_client_call_id != 0) {
			/* create pptp gre context cache */
			memcpy(&gre_npc, npc, sizeof(npf_cache_t));
			gre_npc.npc_proto = IPPROTO_GRE;
			gre_npc.npc_info = NPC_IP46 | NPC_LAYER4 | NPC_ALG_PPTP_GRE_CTX;
			gre_npc.npc_l4.hdr = (void *)&gre_slot->ctx;
			/* setup ip addresses */
			npf_nat_getorig(nt, &o_addr, &o_port);
			gre_npc.npc_ips[NPF_SRC] = o_addr;
			gre_npc.npc_ips[NPF_DST] = npc->npc_ips[NPF_SRC];
			/* establish gre connection state and associate nat */
			npfa_pptp_gre_establish_gre_conn(&gre_npc, PFIL_OUT, gre_slot, nt);
		}

		orig_client_call_id = gre_slot->orig_client_call_id;
		mutex_exit(&gre_conns->lock);

		/* rewrite peer сall id */
		old_call_id = pptp_call_reply->peer_call_id;
		pptp_call_reply->peer_call_id = orig_client_call_id;
		tcp->check = npf_fixup16_cksum(tcp->check, old_call_id,
				  orig_client_call_id);
		break;

	case PPTP_CALL_DISCONNECT_NOTIFY:
		if (pptp->len < sizeof(pptp_msg_hdr_t))
			return false;
		npf_nat_getorig(nt, &ips[NPF_SRC], &o_port);

		/* lookup for a gre connection entry */
		mutex_enter(&gre_conns->lock);
		gre_slot = npfa_pptp_gre_slot_lookup_with_server_call_id(gre_conns,
		    pptp->call_id);
		if (gre_slot == NULL) {
			mutex_exit(&gre_conns->lock);
			return false;
		}

		ips[NPF_DST] = npc->npc_ips[NPF_SRC];
		npfa_pptp_gre_slot_free(npc->npc_ctx, gre_slot, ips);
		mutex_exit(&gre_conns->lock);
		break;

	case PPTP_WAN_ERROR_NOTIFY:
		if (pptp->len < sizeof(pptp_msg_hdr_t))
			return false;

		mutex_enter(&gre_conns->lock);
		gre_slot = npfa_pptp_gre_slot_lookup_with_client_call_id(gre_conns,
		    pptp->call_id);
		if (gre_slot == NULL) {
			mutex_exit(&gre_conns->lock);
			return false;
		}

		orig_client_call_id = gre_slot->orig_client_call_id;
		mutex_exit(&gre_conns->lock);

		/* rewrite */
		old_call_id = pptp->call_id;
		pptp->call_id = orig_client_call_id;
		tcp->check = npf_fixup16_cksum(tcp->check, old_call_id,
		    orig_client_call_id);
		break;

	default:
		return false;
	}

	return true;
}

/*
 * Destroy PPTP TCP nat argument.
 * It will expire all associated gre connections.
 */
static void
npfa_pptp_tcp_destroy(npf_t *npf, npf_conn_t *con)
{
	pptp_gre_con_slot_t *gre_slot;
	pptp_gre_conns_t *gre_conns;
	npf_connkey_t *fw;
	npf_addr_t ips[2];
	npf_addr_t *ipv[2];
	uint16_t ids[2];
	uint16_t alen;
	uint16_t proto;

	gre_conns = (pptp_gre_conns_t *)npf_nat_get_alg_arg(con->c_nat);
	fw = npf_conn_getforwkey(con);

	if (gre_conns == NULL)
		return;

	/* only ipv4 is supported */
	KASSERT(NPF_CONNKEY_ALEN(fw) == sizeof(uint32_t));

	ipv[NPF_SRC] = &ips[NPF_SRC];
	ipv[NPF_DST] = &ips[NPF_DST];
	npf_connkey_getkey(fw, &proto, ips, ids, &alen);

	for (int i = 0; i < PPTP_MAX_GRE_PER_CLIENT; i++) {
		gre_slot = &gre_conns->gre_con_slots[i];
		if ((gre_slot->flags & PPTP_ALG_FL_FREE_SLOT) == 0)
			npfa_pptp_gre_slot_free(npf, gre_slot, ipv);
	}

	npfa_pptp_gre_conns_fini(gre_conns);
}

/*
 * PPTP GRE ALG translator.
 */
static bool
npfa_pptp_gre_translate(npf_cache_t *npc, npf_nat_t *nt, bool forw)
{
	nbuf_t *nbuf = npc->npc_nbuf;
	pptp_gre_hdr_t *gre;
	pptp_gre_con_slot_t gre_slot;

	if (forw || !npf_iscached(npc, NPC_IP4 | NPC_ALG_PPTP_GRE) ||
	    npc->npc_proto != IPPROTO_GRE)
		return false;

	nbuf_reset(nbuf);
	gre = nbuf_advance(nbuf, npc->npc_hlen, sizeof(pptp_gre_hdr_t));
	if (gre == NULL)
		return false;

	gre_slot.u64 = (uint64_t)npf_nat_get_alg_arg(nt);
	KASSERT(gre->call_id == gre_slot.ctx.client_call_id);
	gre->call_id = gre_slot.orig_client_call_id;

	return true;
}

/*
 * Destroy gre connection.
 * Put translated call-id back to the portmap.
 */
static void
npfa_pptp_gre_destroy(npf_t *npf, npf_conn_t *con)
{
	pptp_gre_con_slot_t gre_slot;
	npf_nat_t *nt;
	npf_connkey_t *fw;
	npf_addr_t ips[2];
	uint16_t ids[2];
	uint16_t alen;
	uint16_t proto;

	fw = npf_conn_getforwkey(con);
	nt = con->c_nat;

	if (nt == NULL)
		return;

	/* only ipv4 is supported */
	KASSERT(NPF_CONNKEY_ALEN(fw) == sizeof(uint32_t));

	npf_connkey_getkey(fw, &proto, ips, ids, &alen);

	gre_slot.u64 = (uint64_t)npf_nat_get_alg_arg(nt);
	if (gre_slot.ctx.client_call_id != 0)
		npfa_translated_call_id_put(&ips[NPF_DST], gre_slot.ctx.client_call_id);
}

/*
 * npf_alg_pptp_{init,fini,modcmd}: PPTP ALG initialization, destruction
 * and module interface.
 */
__dso_public int
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
		.destroy   = npfa_pptp_gre_destroy,
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
	}

	return 0;
}

__dso_public int
npf_alg_pptp_fini(npf_t *npf)
{
	KASSERT(pptp_alg.alg_pptp_tcp != NULL);
	KASSERT(pptp_alg.alg_pptp_gre != NULL);
	npf_portmap_fini_pm(pptp_alg.pm);
	npf_alg_unregister(npf, pptp_alg.alg_pptp_tcp);
	return npf_alg_unregister(npf, pptp_alg.alg_pptp_gre);
}

void
npf_pptp_conn_conkey(const npf_cache_t *npc, uint16_t *id, bool forw)
{
	const pptp_gre_context_t *pptp_gre_ctx;
	const pptp_gre_hdr_t *pptp_gre_hdr;

	KASSERT(npf_iscached(npc, NPC_ALG_PPTP_GRE | NPC_ALG_PPTP_GRE_CTX));
	if (npf_iscached(npc, NPC_ALG_PPTP_GRE_CTX)) {
		pptp_gre_ctx = npc->npc_l4.hdr;
		if (forw) {
			/* pptp client -> pptp server */
			id[NPF_SRC] = pptp_gre_ctx->server_call_id;
			id[NPF_DST] = 0; /* not used */
		} else {
			/* pptp client <- pptp server */
			id[NPF_SRC] = 0; /* not used */
			id[NPF_DST] = pptp_gre_ctx->client_call_id;
		}
	} else {
		/* NPC_ALG_PPTP_GRE */
		pptp_gre_hdr = npc->npc_l4.hdr;
		id[NPF_SRC] = pptp_gre_hdr->call_id;
		id[NPF_DST] = 0; /* not used */
	}
}

inline size_t
npf_pptp_gre_hdr_len(void)
{
	return sizeof(pptp_gre_hdr_t);
}

#ifdef _KERNEL
static int
npf_alg_pptp_modcmd(modcmd_t cmd, void *arg)
{
	npf_t *npf = npf_getkernctx();

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
#endif
