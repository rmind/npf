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
__KERNEL_RCSID(0, "$NetBSD$");

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

typedef struct {
	npf_alg_t *		tcp;
	npf_alg_t *		gre;
	npf_portmap_t *		pm;
} npf_pptp_alg_t;

static npf_pptp_alg_t		pptp_alg	__cacheline_aligned;

#define	PPTP_SERVER_PORT		1723

#define	PPTP_OUTGOING_CALL_MIN_LEN	32

#define	PPTP_MAGIC_COOKIE		0X1a2b3c4d

/*
 * GRE headers: standard and PPTP ("enhanced").
 */

#define	GRE_VER_FLD_MASK		0x7
#define	GRE_STANDARD_HDR_VER		0
#define	GRE_ENHANCED_HDR_VER		1

typedef struct {
	uint16_t	flags_ver;
	uint16_t	proto;
	/* optional fields */
} __packed gre_hdr_t;

typedef struct {
	uint16_t	flags_ver;
	uint16_t	proto;
	uint16_t	payload_len;
	uint16_t	call_id;
	/* optional fields */
} __packed pptp_gre_hdr_t;

/*
 * PPTP TCP messages.
 */

/* PPTP messages types. */
#define	PPTP_CTRL_MSG			1

/* PPTP control message types. */
#define	PPTP_OUTGOING_CALL_REQUEST	7
#define	PPTP_OUTGOING_CALL_REPLY	8
#define	PPTP_CALL_CLEAR_REQUEST		12
#define	PPTP_CALL_DISCONNECT_NOTIFY	13
#define	PPTP_WAN_ERROR_NOTIFY		14

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
	/* ... */
} __packed pptp_outgoing_call_req_t;

typedef struct {
	pptp_msg_hdr_t	hdr;
	uint16_t	peer_call_id;
	uint8_t		result_code;
	uint8_t		err_code;
	uint16_t	cause_code;
	/* ... */
} __packed pptp_outgoing_call_reply_t;

/*
 * PPTP ALG captured GRE state.
 */

#define	CLIENT_CALL_ID			0
#define	SERVER_CALL_ID			1
#define	CALL_ID_COUNT			2

typedef struct {
	uint16_t	call_id[CALL_ID_COUNT];
} __packed pptp_gre_ctx_t;

/*
 * PPTP GRE connection state.
 */

#define	GRE_STATE_ESTABLISHED		0x1
#define	GRE_STATE_FREESLOT		0x2

typedef union {
	struct {
		/*
		 * - GRE context: client and server call IDs.
		 * - Original client call ID.
		 *
		 * Note: call ID values are in the network byte order.
		 */
		pptp_gre_ctx_t	ctx;
		uint16_t	orig_client_call_id;
		uint16_t	flags;
	};
	uint64_t		u64;
} pptp_gre_state_t;

/*
 * Maximum number of the GRE connections a host can establish
 * to the same server.
 */
#define	PPTP_MAX_GRE_PER_CLIENT		4

/*
 * ALG context associated with the PPTP TCP control connection.
 */
typedef struct {
	kmutex_t		lock;
	pptp_gre_state_t	gre_conns[PPTP_MAX_GRE_PER_CLIENT];
} pptp_tcp_ctx_t;

static pptp_tcp_ctx_t *	pptp_tcp_ctx_alloc(npf_nat_t *);
static void		pptp_tcp_ctx_free(pptp_tcp_ctx_t *);

///////////////////////////////////////////////////////////////////////////

int
npf_pptp_gre_cache(npf_cache_t *npc, nbuf_t *nbuf, unsigned hlen)
{
	gre_hdr_t *gre_hdr = nbuf_advance(nbuf, hlen, sizeof(gre_hdr_t));
	unsigned ver = ntohs(gre_hdr->flags_ver) & GRE_VER_FLD_MASK;

	if (ver == GRE_ENHANCED_HDR_VER) {
		npc->npc_l4.hdr = nbuf_ensure_contig(nbuf, sizeof(pptp_gre_hdr_t));
		return NPC_LAYER4 | NPC_ENHANCED_GRE;
	}
	return 0;
}

void
npf_pptp_conn_conkey(const npf_cache_t *npc, uint16_t *id, bool forw)
{
	KASSERT(npf_iscached(npc, NPC_ENHANCED_GRE));

	if (npf_iscached(npc, NPC_ALG_PPTP_GRE_CTX)) {
		const pptp_gre_ctx_t *pptp_gre_ctx;

		pptp_gre_ctx = npc->npc_l4.hdr;
		if (forw) {
			/* PPTP client -> PPTP server. */
			id[NPF_SRC] = pptp_gre_ctx->call_id[SERVER_CALL_ID];
			id[NPF_DST] = 0; /* not used */
		} else {
			/* PPTP client <- PPTP server */
			id[NPF_SRC] = 0; /* not used */
			id[NPF_DST] = pptp_gre_ctx->call_id[CLIENT_CALL_ID];
		}
	} else {
		const pptp_gre_hdr_t *pptp_gre_hdr;

		/* NPC_ALG_PPTP_GRE */
		pptp_gre_hdr = npc->npc_l4.hdr;
		id[NPF_SRC] = pptp_gre_hdr->call_id;
		id[NPF_DST] = 0; /* not used */
	}
}

static inline uint16_t
pptp_call_id_get(const npf_addr_t *ip)
{
	return npf_portmap_get(pptp_alg.pm, sizeof(uint32_t), ip);
}

static inline void
pptp_call_id_put(const npf_addr_t *ip, uint16_t call_id)
{
	npf_portmap_put(pptp_alg.pm, sizeof(uint32_t), ip, call_id);
}

static int
pptp_gre_establish_state(npf_cache_t *npc, const int di,
    pptp_gre_state_t *gre_state, npf_nat_t *pptp_tcp_nt)
{
	npf_conn_t *con = NULL;
	int ret;

	/* Establish a state for the GRE connection. */
	con = npf_conn_establish(npc, di, true);
	if (con == NULL)
		return ENOMEM;

	/*
	 * Create a new nat entry for created GRE connection.  Use the
	 * same NAT policy as the parent PPTP TCP control connection uses.
	 * Associate created NAT entry with the GRE connection.
	 */
	ret = npf_nat_share_policy(npc, con, pptp_tcp_nt);
	if (ret) {
		npf_conn_expire(con);
		npf_conn_release(con);
		return ret;
	}

	/* Associate GRE ALG with the GRE connection. */
	npf_nat_setalg(con->c_nat, pptp_alg.gre, (uintptr_t)gre_state->u64);

	/* Make GRE connection state active and passing. */
	npf_conn_setpass(con, NULL, NULL);
	npf_conn_release(con);

	gre_state->flags |= GRE_STATE_ESTABLISHED;
	return 0;
}

/*
 * pptp_gre_destroy_state: destroy the ALG GRE state and expire the
 * associated GRE tunnel connection state.
 */
static void
pptp_gre_destroy_state(npf_t *npf, pptp_gre_state_t *gre_state, npf_addr_t **ips)
{
	npf_conn_t *con;
	uint16_t ids[2];
	bool forw;

	/* Expire the GRE connection state. */
	if (gre_state->flags & GRE_STATE_ESTABLISHED) {
		npf_connkey_t key;

		/* Initialise the forward GRE connection key. */
		ids[NPF_SRC] = gre_state->ctx.call_id[SERVER_CALL_ID];
		ids[NPF_DST] = 0;
		npf_connkey_setkey((void *)&key, IPPROTO_GRE, ips, ids,
		    sizeof(uint32_t), true);

		/* Lookup the associated PPTP GRE connection state. */
		con = npf_conndb_lookup(npf->conn_db, &key, &forw);
		if (con != NULL) {
			/*
			 * Mark the GRE connection as expired.
			 *
			 * Note: translated call ID will be put back to the
			 * portmap by the GRE connection state destructor.
			 */
			npf_conn_expire(con);
			npf_conn_release(con);
		}
		gre_state->flags &= ~GRE_STATE_ESTABLISHED;

	} else if (gre_state->ctx.call_id[CLIENT_CALL_ID] != 0) {
		/*
		 * Return translated call ID value back to the portmap.
		 */
		pptp_call_id_put(ips[NPF_DST], gre_state->ctx.call_id[CLIENT_CALL_ID]);
	}

	/* Mark the slot as free. */
	gre_state->flags |= GRE_STATE_FREESLOT;
}

/*
 * pptp_gre_get_state: find a free GRE state slot or reuse one with
 * the same orig_client_call_id.  Note: there can be only one slot
 * with the same orig_client_call_id.
 *
 * => Returns NULL if there are no empty slots (or a slot to re-use);
 * => Otherwise, return a reference to a slot marked as used and where
 *    the client call ID and translated client call ID valued are stored.
 */
static pptp_gre_state_t *
pptp_gre_get_state(npf_cache_t *npc, pptp_tcp_ctx_t *tcp_ctx,
    uint16_t client_call_id, uint16_t trans_client_call_id)
{
	pptp_gre_state_t *gre_state, *empty_slot = NULL, old_reused_slot;
	bool reuse_slot = false;

	/*
	 * Scan all slots to ensure that there is no one using the call ID.
	 */
	mutex_enter(&tcp_ctx->lock);
	for (unsigned i = 0; i < PPTP_MAX_GRE_PER_CLIENT; i++) {
		gre_state = &tcp_ctx->gre_conns[i];

		/*
		 * If call ID is already in use, then expire the associated
		 * GRE connection and re-use this GRE state slot.
		 */
		if (gre_state->flags & GRE_STATE_FREESLOT) {
			/* Empty slot. */
			empty_slot = gre_state;
		}
		else if (gre_state->orig_client_call_id == client_call_id) {
			reuse_slot = true;
			old_reused_slot.u64 = gre_state->u64;
			break;
		}
	}

	/* Use empty slot or reuse a slot with the same client call ID. */
	if (reuse_slot || empty_slot != NULL) {
		pptp_gre_state_t new_gre_state;

		if (!reuse_slot) {
			gre_state = empty_slot;
		}
		new_gre_state.orig_client_call_id = client_call_id;
		new_gre_state.ctx.call_id[CLIENT_CALL_ID] = trans_client_call_id;
		new_gre_state.ctx.call_id[SERVER_CALL_ID] = 0;
		new_gre_state.flags = 0;
		gre_state->u64 = new_gre_state.u64;
	}
	mutex_exit(&tcp_ctx->lock);

	if (reuse_slot) {
		pptp_gre_destroy_state(npc->npc_ctx, &old_reused_slot, npc->npc_ips);
		return gre_state;
	}
	return empty_slot;
}

/*
 * pptp_gre_lookup_state: lookup for a GRE state with the given call ID.
 */
static pptp_gre_state_t *
pptp_gre_lookup_state(pptp_tcp_ctx_t *tcp_ctx, unsigned which, uint16_t call_id)
{
	KASSERT(which == CLIENT_CALL_ID || which == SERVER_CALL_ID);
	KASSERT(mutex_owned(&tcp_ctx->lock));

	for (unsigned i = 0; i < PPTP_MAX_GRE_PER_CLIENT; i++) {
		pptp_gre_state_t *gre_state = &tcp_ctx->gre_conns[i];

		if ((gre_state->flags & GRE_STATE_FREESLOT) == 0 &&
		    gre_state->ctx.call_id[which] == call_id)
			return gre_state;
	}
	return NULL;
}

static pptp_tcp_ctx_t *
pptp_tcp_ctx_alloc(npf_nat_t *nt)
{
	pptp_tcp_ctx_t *ctx, *oldctx;

	ctx = kmem_intr_zalloc(sizeof(pptp_tcp_ctx_t), KM_SLEEP);
	if (ctx == NULL) {
		return NULL;
	}
	mutex_init(&ctx->lock, MUTEX_DEFAULT, IPL_SOFTNET);
	for (unsigned i = 0; i < PPTP_MAX_GRE_PER_CLIENT; i++) {
		ctx->gre_conns[i].flags = GRE_STATE_FREESLOT;
	}
	oldctx = npf_nat_cas_alg_arg(nt, (uintptr_t)NULL, (uintptr_t)ctx);
	if (oldctx != NULL) {
		/* Race: already allocated. */
		pptp_tcp_ctx_free(oldctx);
		return oldctx;
	}
	return ctx;
}

static void
pptp_tcp_ctx_free(pptp_tcp_ctx_t *ctx)
{
	mutex_destroy(&ctx->lock);
	kmem_intr_free(ctx, sizeof(pptp_tcp_ctx_t));
}

///////////////////////////////////////////////////////////////////////////

/*
 * pptp_tcp_match: detects the PPTP TCP connection which controls the
 * PPTP GRE tunnel connection and associates it with the relevant ALG.
 */
static bool
pptp_tcp_match(npf_cache_t *npc, npf_nat_t *nt, const int di)
{
	KASSERT(npf_iscached(npc, NPC_IP46));

	/* Note: only the outbound NAT is supported */
	if (di != PFIL_OUT || !npf_iscached(npc, NPC_TCP) ||
	    npc->npc_l4.tcp->th_dport != htons(PPTP_SERVER_PORT))
		return false;

	/* Associate with the PPTP TCP ALG. */
	npf_nat_setalg(nt, pptp_alg.tcp, 0);
	return true;
}

/*
 * pptp_tcp_translate: PPTP TCP control connection ALG translator.
 *
 * This rewrites Call ID in the Outgoing-Call-Request message and
 * Peer Call ID in the Outgoing-Call-Reply message.
 */
static bool
pptp_tcp_translate(npf_cache_t *npc, npf_nat_t *nt, bool forw)
{
	nbuf_t *nbuf = npc->npc_nbuf;
	struct tcphdr *th = npc->npc_l4.tcp;
	uint32_t tcp_hdr_size;
	pptp_tcp_ctx_t *tcp_ctx;
	pptp_msg_hdr_t *pptp;
	pptp_gre_state_t *gre_state;
	uint16_t old_call_id, trans_client_call_id, orig_client_call_id;
	pptp_outgoing_call_reply_t *pptp_call_reply;
	npf_addr_t *ip, *ips[2];
	in_port_t o_port;

	/* Note: only IPv4 is supported. */
	if (!(npf_iscached(npc, NPC_IP4) && npf_iscached(npc, NPC_TCP) &&
	    (th->th_dport == htons(PPTP_SERVER_PORT) ||
	    th->th_sport == htons(PPTP_SERVER_PORT))))
		return false;

	tcp_hdr_size = th->th_off << 2;
	nbuf_reset(nbuf);
	pptp = nbuf_advance(nbuf, npc->npc_hlen + tcp_hdr_size,
	    sizeof(pptp_msg_hdr_t));
	if (pptp == NULL)
		return false;

	if (pptp->pptp_msg_type != htons(PPTP_CTRL_MSG) ||
	    pptp->len < htons(PPTP_OUTGOING_CALL_MIN_LEN) ||
	    pptp->magic_cookie != htonl(PPTP_MAGIC_COOKIE))
		return false;

	/* Get or allocate GRE connection context for the ALG. */
	tcp_ctx = (pptp_tcp_ctx_t *)npf_nat_get_alg_arg(nt);
	if (!tcp_ctx && (tcp_ctx = pptp_tcp_ctx_alloc(nt)) == NULL) {
		return false;
	}

	switch (ntohs(pptp->ctrl_msg_type)) {
	case PPTP_OUTGOING_CALL_REQUEST:
		if (pptp->len < sizeof(pptp_outgoing_call_req_t))
			return false;

		/*
		 * Get the translated call ID value.
		 *
		 * It should be a unique value within the scope of all PPTP
		 * connections destined to the same server.
		 *
		 * Note: it is better to use the source address scope, but
		 * the translated source IP address is not known at this
		 * point, since alg->translate() executed before the normal
		 * NAT translation.
		 */
		ip = npc->npc_ips[NPF_DST]; /* PPTP server IP */
		trans_client_call_id = pptp_call_id_get(ip);
		if (trans_client_call_id == 0)
			return false;

		/*
		 * Lookup for an empty GRE state slot or reuse one with
		 * the same original call ID.
		 */
		gre_state = pptp_gre_get_state(npc, tcp_ctx,
		    pptp->call_id, trans_client_call_id);
		if (gre_state == NULL) {
			/* all entries are in use */
			pptp_call_id_put(ip, trans_client_call_id);
			return false;
		}

		/* Rewrite client call ID. */
		old_call_id = pptp->call_id;
		pptp->call_id = trans_client_call_id;
		th->th_sum = npf_fixup16_cksum(th->th_sum, old_call_id,
		    trans_client_call_id);
		break;

	case PPTP_OUTGOING_CALL_REPLY:
		if (pptp->len < sizeof(pptp_outgoing_call_reply_t)) {
			return false;
		}
		pptp_call_reply = nbuf_ensure_contig(nbuf,
		    sizeof(pptp_outgoing_call_reply_t));

		/* Lookup the GRE connection context. */
		mutex_enter(&tcp_ctx->lock);
		gre_state = pptp_gre_lookup_state(tcp_ctx,
		    CLIENT_CALL_ID, pptp_call_reply->peer_call_id);
		if (gre_state == NULL || gre_state->ctx.call_id[SERVER_CALL_ID] != 0) {
			/*
			 * Slot is not found or call reply message has been
			 * already received.
			 */
			mutex_exit(&tcp_ctx->lock);
			return false;
		}

		/* Save the server call ID. */
		gre_state->ctx.call_id[SERVER_CALL_ID]= pptp_call_reply->hdr.call_id;

		/*
		 * If client and server call IDs have been seen, create
		 * new GRE connection state entry.
		 */
		if (gre_state->ctx.call_id[CLIENT_CALL_ID] != 0 &&
		    gre_state->ctx.call_id[SERVER_CALL_ID] != 0 &&
		    gre_state->orig_client_call_id != 0) {
			npf_cache_t gre_npc;
			npf_addr_t *o_addr;

			/* Create PPTP GRE context cache. */
			memcpy(&gre_npc, npc, sizeof(npf_cache_t));
			gre_npc.npc_proto = IPPROTO_GRE;
			gre_npc.npc_info = NPC_IP46 | NPC_LAYER4 | NPC_ALG_PPTP_GRE_CTX;
			gre_npc.npc_l4.hdr = (void *)&gre_state->ctx;

			/* Setup the IP addresses. */
			npf_nat_getorig(nt, &o_addr, &o_port);
			gre_npc.npc_ips[NPF_SRC] = o_addr;
			gre_npc.npc_ips[NPF_DST] = npc->npc_ips[NPF_SRC];

			/*
			 * Establish the GRE connection state and associate
			 * the NAT entry.
			 */
			pptp_gre_establish_state(&gre_npc, PFIL_OUT, gre_state, nt);
		}
		orig_client_call_id = gre_state->orig_client_call_id;
		mutex_exit(&tcp_ctx->lock);

		/* Rewrite the peer сall ID. */
		old_call_id = pptp_call_reply->peer_call_id;
		pptp_call_reply->peer_call_id = orig_client_call_id;
		th->th_sum = npf_fixup16_cksum(th->th_sum, old_call_id,
		    orig_client_call_id);
		break;

	case PPTP_CALL_DISCONNECT_NOTIFY:
		if (pptp->len < sizeof(pptp_msg_hdr_t))
			return false;

		/* Lookup the GRE connection state. */
		mutex_enter(&tcp_ctx->lock);
		gre_state = pptp_gre_lookup_state(tcp_ctx,
		    SERVER_CALL_ID, pptp->call_id);
		if (gre_state == NULL) {
			mutex_exit(&tcp_ctx->lock);
			return false;
		}
		npf_nat_getorig(nt, &ips[NPF_SRC], &o_port);
		ips[NPF_DST] = npc->npc_ips[NPF_SRC];
		pptp_gre_destroy_state(npc->npc_ctx, gre_state, ips);
		mutex_exit(&tcp_ctx->lock);
		break;

	case PPTP_WAN_ERROR_NOTIFY:
		if (pptp->len < sizeof(pptp_msg_hdr_t))
			return false;

		mutex_enter(&tcp_ctx->lock);
		gre_state = pptp_gre_lookup_state(tcp_ctx,
		    CLIENT_CALL_ID, pptp->call_id);
		if (gre_state == NULL) {
			mutex_exit(&tcp_ctx->lock);
			return false;
		}
		orig_client_call_id = gre_state->orig_client_call_id;
		mutex_exit(&tcp_ctx->lock);

		/* Translate the call ID. */
		old_call_id = pptp->call_id;
		pptp->call_id = orig_client_call_id;
		th->th_sum = npf_fixup16_cksum(th->th_sum, old_call_id,
		    orig_client_call_id);
		break;

	default:
		return false;
	}

	return true;
}

/*
 * pptp_tcp_destroy: free the structures associated with PPTP TCP connection.
 * It will expire all associated GRE connection states.
 */
static void
pptp_tcp_destroy(npf_t *npf, npf_nat_t *nt, npf_conn_t *con)
{
	npf_connkey_t *fw;
	npf_addr_t ips[2], *ipv[2];
	uint16_t ids[2], alen, proto;
	pptp_tcp_ctx_t *tcp_ctx;

	tcp_ctx = (pptp_tcp_ctx_t *)npf_nat_get_alg_arg(nt);
	if (tcp_ctx == NULL) {
		return;
	}

	ipv[NPF_SRC] = &ips[NPF_SRC];
	ipv[NPF_DST] = &ips[NPF_DST];

	/* Note: only IPv4 is supported. */
	fw = npf_conn_getforwkey(con);
	KASSERT(NPF_CONNKEY_ALEN(fw) == sizeof(uint32_t));
	npf_connkey_getkey(fw, &proto, ips, ids, &alen);

	for (unsigned i = 0; i < PPTP_MAX_GRE_PER_CLIENT; i++) {
		pptp_gre_state_t *gre_state;

		gre_state = &tcp_ctx->gre_conns[i];
		if ((gre_state->flags & GRE_STATE_FREESLOT) == 0)
			pptp_gre_destroy_state(npf, gre_state, ipv);
	}
	pptp_tcp_ctx_free(tcp_ctx);
}

///////////////////////////////////////////////////////////////////////////

/*
 * pptp_gre_translate: translate the PPTP GRE connection.
 */
static bool
pptp_gre_translate(npf_cache_t *npc, npf_nat_t *nt, bool forw)
{
	nbuf_t *nbuf = npc->npc_nbuf;
	pptp_gre_hdr_t *gre;
	pptp_gre_state_t gre_state;

	if (forw || !npf_iscached(npc, NPC_IP4 | NPC_ENHANCED_GRE))
		return false;

	nbuf_reset(nbuf);
	gre = nbuf_advance(nbuf, npc->npc_hlen, sizeof(pptp_gre_hdr_t));
	if (gre == NULL)
		return false;

	gre_state.u64 = (uint64_t)npf_nat_get_alg_arg(nt);
	KASSERT(gre->call_id == gre_state.ctx.call_id[CLIENT_CALL_ID]);
	gre->call_id = gre_state.orig_client_call_id;

	return true;
}

/*
 * Destroy the GRE connection context.
 *
 * => Puts the translated call ID back to the portmap.
 */
static void
pptp_gre_destroy(npf_t *npf, npf_nat_t *nt, npf_conn_t *con)
{
	npf_connkey_t *fw;
	npf_addr_t ips[2];
	uint16_t ids[2], alen, proto;
	pptp_gre_state_t gre_state;

	/* Note: only IPv4 is supported. */
	fw = npf_conn_getforwkey(con);
	KASSERT(NPF_CONNKEY_ALEN(fw) == sizeof(uint32_t));
	npf_connkey_getkey(fw, &proto, ips, ids, &alen);

	gre_state.u64 = (uint64_t)npf_nat_get_alg_arg(nt);
	if (gre_state.ctx.call_id[CLIENT_CALL_ID] != 0) {
		pptp_call_id_put(&ips[NPF_DST], gre_state.ctx.call_id[CLIENT_CALL_ID]);
	}
}

///////////////////////////////////////////////////////////////////////////

/*
 * npf_alg_pptp_{init,fini,modcmd}: PPTP ALG initialization, destruction
 * and module interface.
 */
__dso_public int
npf_alg_pptp_init(npf_t *npf)
{
	static const npfa_funcs_t pptp_tcp = {
		.match		= pptp_tcp_match,
		.translate	= pptp_tcp_translate,
		.inspect	= NULL,
		.destroy	= pptp_tcp_destroy,
	};
	static const npfa_funcs_t pptp_gre = {
		.match		= NULL,
		.translate	= pptp_gre_translate,
		.inspect	= NULL,
		.destroy	= pptp_gre_destroy,
	};

	/* Portmap for the PPTP call ID range. */
	pptp_alg.pm = npf_portmap_create(1, UINT16_MAX);
	if (pptp_alg.pm == NULL) {
		return ENOMEM;
	}
	pptp_alg.tcp = npf_alg_register(npf, "pptp_tcp", &pptp_tcp);
	if (pptp_alg.tcp == NULL) {
		return ENOMEM;
	}
	pptp_alg.gre = npf_alg_register(npf, "pptp_gre", &pptp_gre);
	if (pptp_alg.tcp == NULL) {
		npf_alg_unregister(npf, pptp_alg.tcp);
		return ENOMEM;
	}
	return 0;
}

__dso_public int
npf_alg_pptp_fini(npf_t *npf)
{
	KASSERT(pptp_alg.tcp != NULL);
	KASSERT(pptp_alg.gre != NULL);
	npf_portmap_destroy(pptp_alg.pm);
	return npf_alg_unregister(npf, pptp_alg.gre);
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
