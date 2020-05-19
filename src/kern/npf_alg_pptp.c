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

#include "npf_impl.h"
#include "npf_conn.h"

MODULE(MODULE_CLASS_MISC, npf_alg_pptp, "npf");

typedef struct {
	npf_alg_t *		tcp;
	npf_alg_t *		gre;
	npf_portmap_t *		pm;
} npf_pptp_alg_t;

static npf_pptp_alg_t		pptp_alg	__cacheline_aligned;

#define	PPTP_SERVER_PORT		htons(1723)

#define	PPTP_OUTGOING_CALL_MIN_LEN	32

#define	PPTP_MAGIC_COOKIE		0x1a2b3c4d

/*
 * GRE headers: standard and PPTP ("enhanced").
 */

#define	GRE_VER_FLD_MASK		0x7
#define	GRE_STANDARD_HDR_VER		0
#define	GRE_ENHANCED_HDR_VER		1

typedef struct {
	uint16_t	flags_ver;
	uint16_t	proto;
	/* enhanced header: */
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

#define PPTP_MIN_MSG_SIZE (MIN(\
	sizeof(pptp_outgoing_call_req_t) - sizeof(pptp_msg_hdr_t), \
	sizeof(pptp_outgoing_call_reply_t) - sizeof(pptp_msg_hdr_t)))

/*
 * PPTP GRE connection state.
 */

#define	CLIENT_CALL_ID			0
#define	SERVER_CALL_ID			1
#define	CALL_ID_COUNT			2

#define	GRE_STATE_USED			0x1
#define	GRE_STATE_ESTABLISHED		0x2
#define	GRE_STATE_SERVER_CALL_ID	0x4

typedef struct {
	/*
	 * - Client and server call IDs.
	 * - Original client call ID.
	 * - State flags.
	 *
	 * Note: call ID values are in the network byte order.
	 */
	uint16_t	call_id[CALL_ID_COUNT];
	uint16_t	orig_client_call_id;
	uint16_t	flags;
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

///////////////////////////////////////////////////////////////////////////

static void
pptp_gre_prepare_state(const npf_cache_t *npc, npf_nat_t *nt,
    const pptp_gre_state_t *gre_state, npf_cache_t *gre_npc,
    npf_connkey_t *ckey)
{
	uint16_t gre_id[2];
	npf_addr_t *o_addr;
	in_port_t o_port;

	/*
	 * Create PPTP GRE context cache.  Needed for:
	 *
	 * => npf_conn_establish() to pick up a different protocol.
	 * => npf_nat_share_policy() to obtain the IP addresses.
	 */
	memcpy(gre_npc, npc, sizeof(npf_cache_t));
	gre_npc->npc_proto = IPPROTO_GRE;
	gre_npc->npc_info = NPC_IP46 | NPC_LAYER4;

	/*
	 * Setup the IP addresses and call IDs.
	 *
	 * PPTP client -> PPTP server (and vice versa, if NPF_FLOW_FORW).
	 */
	npf_nat_getorig(nt, &o_addr, &o_port);
	gre_npc->npc_ips[NPF_SRC] = o_addr;
	gre_npc->npc_ips[NPF_DST] = npc->npc_ips[NPF_SRC];
	gre_id[NPF_SRC] = gre_state->call_id[SERVER_CALL_ID];
	gre_id[NPF_DST] = 0; /* not used */

	/*
	 * Additionally, set the custom key for npf_conn_establish().
	 * We need bypass key construction as this is a custom protocol,
	 * i.e. "enhanced PPTP" is unknown to npf_conn_conkey().
	 */
	npf_connkey_setkey(ckey, npc->npc_alen, IPPROTO_GRE,
	    gre_npc->npc_ips, gre_id, NPF_FLOW_FORW);
	gre_npc->npc_ckey = &ckey;
}

static int
pptp_gre_establish_state(npf_cache_t *npc, const int di,
    pptp_gre_state_t *gre_state, npf_nat_t *pptp_tcp_nt)
{
	npf_cache_t gre_npc;
	npf_connkey_t ckey;
	npf_conn_t *con = NULL;
	npf_nat_t *nt;

	pptp_gre_prepare_state(npc, pptp_tcp_nt, gre_state, &gre_npc, &ckey);

	/* Establish a state for the GRE connection. */
	con = npf_conn_establish(&gre_npc, di, true);
	if (con == NULL)
		return ENOMEM;

	/*
	 * Create a new nat entry for created GRE connection.  Use the
	 * same NAT policy as the parent PPTP TCP control connection uses.
	 * Associate the created NAT entry with the GRE connection.
	 */
	nt = npf_nat_share_policy(&gre_npc, con, pptp_tcp_nt);
	if (nt == NULL) {
		npf_conn_expire(con);
		npf_conn_release(con);
		return ENOMEM;
	}
	gre_state->flags |= GRE_STATE_ESTABLISHED;

	/* Associate GRE ALG with the GRE connection. */
	npf_nat_setalg(nt, pptp_alg.gre, (uintptr_t)(const void *)gre_state);

	/* Make GRE connection state active and passing. */
	npf_conn_setpass(con, NULL, NULL);
	npf_conn_release(con);
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

	/* Expire the GRE connection state. */
	if (gre_state->flags & GRE_STATE_ESTABLISHED) {
		npf_connkey_t key;
		npf_flow_t flow;

		/* Initialize the forward GRE connection key. */
		ids[NPF_SRC] = gre_state->call_id[SERVER_CALL_ID];
		ids[NPF_DST] = 0;
		npf_connkey_setkey((void *)&key, sizeof(uint32_t),
		    IPPROTO_GRE, ips, ids, NPF_FLOW_FORW);

		/* Lookup the associated PPTP GRE connection state. */
		con = npf_conndb_lookup(npf, &key, &flow);
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

	} else if (gre_state->call_id[CLIENT_CALL_ID] != 0) {
		/*
		 * Return translated call ID value back to the portmap.
		 */
		pptp_call_id_put(ips[NPF_DST], gre_state->call_id[CLIENT_CALL_ID]);
	}

	/* Mark the entry as unused. */
	gre_state->flags &= ~GRE_STATE_USED;
}

/*
 * pptp_gre_get_state: find a free GRE state entry or reuse one with
 * the same orig_client_call_id.  Note: there can be only one entry
 * with the same orig_client_call_id.
 *
 * => Returns NULL if there are no empty entries (or an entry to re-use);
 * => Otherwise, return a reference to a entry marked as used and where
 *    the client call ID and translated client call ID valued are stored.
 */
static pptp_gre_state_t *
pptp_gre_get_state(npf_cache_t *npc, pptp_tcp_ctx_t *tcp_ctx,
    uint16_t client_call_id, uint16_t trans_client_call_id)
{
	pptp_gre_state_t *gre_state = NULL;

	/*
	 * Scan all state entries to check if the given call ID is used.
	 */
	mutex_enter(&tcp_ctx->lock);
	for (unsigned i = 0; i < PPTP_MAX_GRE_PER_CLIENT; i++) {
		pptp_gre_state_t *gre_state_iter = &tcp_ctx->gre_conns[i];

		if ((gre_state_iter->flags & GRE_STATE_USED) == 0) {
			/* Unused state entry; remember it. */
			gre_state = gre_state_iter;
			continue;
		}

		/*
		 * If call ID is already in use, then expire the associated
		 * GRE connection and re-use this GRE state entry.
		 */
		if (gre_state_iter->orig_client_call_id == client_call_id) {
			pptp_gre_destroy_state(npc->npc_ctx,
			    gre_state_iter, npc->npc_ips);
			KASSERT((gre_state_iter->flags & GRE_STATE_USED) == 0);
			gre_state = gre_state_iter;
			break;
		}
	}
	if (gre_state) {
		gre_state->orig_client_call_id = client_call_id;
		gre_state->call_id[CLIENT_CALL_ID] = trans_client_call_id;
		gre_state->flags = GRE_STATE_USED;
	}
	mutex_exit(&tcp_ctx->lock);
	return gre_state;
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

		if ((gre_state->flags & GRE_STATE_USED) == 0)
			continue;
		if (gre_state->call_id[which] != call_id)
			continue;
		if ((gre_state->flags & GRE_STATE_SERVER_CALL_ID) != 0 ||
		    (which == CLIENT_CALL_ID)) {
			return gre_state;
		}
	}
	return NULL;
}

static pptp_tcp_ctx_t *
pptp_tcp_ctx_alloc(npf_nat_t *nt)
{
	pptp_tcp_ctx_t *ctx;

	ctx = kmem_intr_zalloc(sizeof(pptp_tcp_ctx_t), KM_NOSLEEP);
	if (ctx == NULL) {
		return NULL;
	}
	mutex_init(&ctx->lock, MUTEX_DEFAULT, IPL_SOFTNET);
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
	pptp_tcp_ctx_t *tcp_ctx;

	KASSERT(npf_iscached(npc, NPC_IP46));

	/* Note: only the outbound NAT is supported */
	if (di != PFIL_OUT || !npf_iscached(npc, NPC_TCP) ||
	    npc->npc_l4.tcp->th_dport != PPTP_SERVER_PORT)
		return false;

	/* Associate with the PPTP TCP ALG. */
	if ((tcp_ctx = pptp_tcp_ctx_alloc(nt)) == NULL) {
		return false;
	}
	npf_nat_setalg(nt, pptp_alg.tcp, (uintptr_t)(void *)tcp_ctx);
	return true;
}

/*
 * pptp_tcp_translate: PPTP TCP control connection ALG translator.
 *
 * This rewrites Call ID in the Outgoing-Call-Request message and
 * Peer Call ID in the Outgoing-Call-Reply message.
 */
static bool
pptp_tcp_translate(npf_cache_t *npc, npf_nat_t *nt, npf_flow_t flow)
{
	nbuf_t *nbuf = npc->npc_nbuf;
	struct tcphdr *th = npc->npc_l4.tcp;
	pptp_tcp_ctx_t *tcp_ctx;
	pptp_msg_hdr_t *pptp;
	pptp_gre_state_t *gre_state;
	uint16_t old_call_id, trans_client_call_id, orig_client_call_id;
	pptp_outgoing_call_reply_t *pptp_call_reply;
	npf_addr_t *ip, *ips[2];
	in_port_t o_port;

	/*
	 * Note: if ALG is associated, then the checks have been already
	 * performed in pptp_tcp_match().
	 */
	if (!npf_nat_getalg(nt)) {
		return false;
	}
	if (th->th_dport != PPTP_SERVER_PORT &&
	    th->th_sport != PPTP_SERVER_PORT) {
		return false;
	}

	nbuf_reset(nbuf);
	pptp = nbuf_advance(nbuf, npc->npc_hlen + ((unsigned)th->th_off << 2),
	    sizeof(pptp_msg_hdr_t) + PPTP_MIN_MSG_SIZE);
	if (pptp == NULL)
		return false;

	/* Note: re-fetch the L4 pointer. */
	npf_recache(npc);
	th = npc->npc_l4.tcp;

	if (pptp->pptp_msg_type != htons(PPTP_CTRL_MSG) ||
	    pptp->len < htons(PPTP_OUTGOING_CALL_MIN_LEN) ||
	    pptp->magic_cookie != htonl(PPTP_MAGIC_COOKIE))
		return false;

	/* Get or allocate GRE connection context for the ALG. */
	tcp_ctx = (pptp_tcp_ctx_t *)npf_nat_getalgarg(nt);
	KASSERT(tcp_ctx != NULL);

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
		 * Lookup for an empty GRE state entry or reuse one with
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
		CTASSERT(sizeof(pptp_outgoing_call_reply_t) <=
		    sizeof(pptp_msg_hdr_t) + PPTP_MIN_MSG_SIZE);
		pptp_call_reply = (pptp_outgoing_call_reply_t *)pptp;

		/* Lookup the GRE connection context. */
		mutex_enter(&tcp_ctx->lock);
		gre_state = pptp_gre_lookup_state(tcp_ctx,
		    CLIENT_CALL_ID, pptp_call_reply->peer_call_id);

		if (gre_state == NULL ||
		    (gre_state->flags & GRE_STATE_SERVER_CALL_ID) != 0) {
			/*
			 * State entry not found or call reply message
			 * has been already received.
			 */
			mutex_exit(&tcp_ctx->lock);
			return false;
		}

		/* Save the server call ID. */
		gre_state->call_id[SERVER_CALL_ID]= pptp_call_reply->hdr.call_id;
		gre_state->flags |= GRE_STATE_SERVER_CALL_ID;

		/*
		 * Client and server call IDs have been seen.  Create a new
		 * GRE connection state entry and share the NAT entry with
		 * the TCP state.
		 */
		if (pptp_gre_establish_state(npc, PFIL_OUT, gre_state, nt)) {
			gre_state->flags &= ~GRE_STATE_SERVER_CALL_ID;
			mutex_exit(&tcp_ctx->lock);
			return false;
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
	pptp_tcp_ctx_t *tcp_ctx;
	npf_addr_t ips[2], *ipv[2];
	unsigned alen, proto;
	uint16_t ids[2];

	tcp_ctx = (pptp_tcp_ctx_t *)npf_nat_getalgarg(nt);
	if (tcp_ctx == NULL) {
		return;
	}

	ipv[NPF_SRC] = &ips[NPF_SRC];
	ipv[NPF_DST] = &ips[NPF_DST];

	/* Note: only IPv4 is supported. */
	fw = npf_conn_getforwkey(con);
	KASSERT(NPF_CONNKEY_ALEN(fw) == sizeof(uint32_t));
	npf_connkey_getkey(fw, &alen, &proto, ips, ids);

	for (unsigned i = 0; i < PPTP_MAX_GRE_PER_CLIENT; i++) {
		pptp_gre_state_t *gre_state = &tcp_ctx->gre_conns[i];

		if (gre_state->flags & GRE_STATE_USED) {
			pptp_gre_destroy_state(npf, gre_state, ipv);
		}
	}
	pptp_tcp_ctx_free(tcp_ctx);
}

///////////////////////////////////////////////////////////////////////////


/*
 * pptp_gre_inspect: lookup a custom PPTP GRE connection state.
 */
static npf_conn_t *
pptp_gre_inspect(npf_cache_t *npc, int di)
{
	nbuf_t *nbuf = npc->npc_nbuf;
	pptp_gre_hdr_t *gre_hdr;
	npf_connkey_t ckey;
	uint16_t gre_id[2];
	npf_conn_t *con;
	npf_flow_t flow;
	unsigned ver;

	if (npc->npc_proto != IPPROTO_GRE) {
		return NULL;
	}
	gre_hdr = nbuf_advance(nbuf, npc->npc_hlen, sizeof(pptp_gre_hdr_t));
	if (gre_hdr == NULL) {
		return NULL;
	}
	ver = ntohs(gre_hdr->flags_ver) & GRE_VER_FLD_MASK;
	if (ver != GRE_ENHANCED_HDR_VER) {
		return NULL;
	}

	/*
	 * Prepare the GRE connection key.
	 */
	gre_id[NPF_SRC] = gre_hdr->call_id;
	gre_id[NPF_DST] = 0; /* not used */
	npf_connkey_setkey(&ckey, npc->npc_alen, IPPROTO_GRE,
	    npc->npc_ips, gre_id, NPF_FLOW_FORW);

	/* Lookup using the custom key. */
	npc->npc_ckey = &ckey;
	con = npf_conn_lookup(npc, di, &flow);
	npc->npc_ckey = NULL;

	return con;
}

/*
 * pptp_gre_translate: translate the PPTP GRE connection.
 */
static bool
pptp_gre_translate(npf_cache_t *npc, npf_nat_t *nt, npf_flow_t flow)
{
	nbuf_t *nbuf = npc->npc_nbuf;
	const pptp_gre_state_t *gre_state;
	pptp_gre_hdr_t *gre_hdr;
	unsigned ver;

	if (flow == NPF_FLOW_FORW || !npf_iscached(npc, NPC_IP4)) {
		return false;
	}
	if (!npf_nat_getalg(nt)) {
		return false;
	}

	/*
	 * Note: since pptp_gre_inspect() cannot pass an arbitrary ALG
	 * information right now, we need to re-check the header.
	 */
	nbuf_reset(nbuf);
	gre_hdr = nbuf_advance(nbuf, npc->npc_hlen, sizeof(pptp_gre_hdr_t));
	if (gre_hdr == NULL) {
		return false;
	}
	ver = ntohs(gre_hdr->flags_ver) & GRE_VER_FLD_MASK;
	if (ver != GRE_ENHANCED_HDR_VER) {
		return false;
	}

	gre_state = (const void *)npf_nat_getalgarg(nt);
	KASSERT(gre_hdr->call_id == gre_state->call_id[CLIENT_CALL_ID]);
	gre_hdr->call_id = gre_state->orig_client_call_id;

	// FIXME: IP checksum?

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
	const pptp_gre_state_t *gre_state;
	uint16_t call_id;

	gre_state = (const void *)npf_nat_getalgarg(nt);
	if ((call_id = gre_state->call_id[CLIENT_CALL_ID]) != 0) {
		const npf_connkey_t *fw;
		unsigned alen, proto;
		npf_addr_t ips[2];
		uint16_t ids[2];

		/* Note: only IPv4 is supported. */
		fw = npf_conn_getforwkey(con);
		KASSERT(NPF_CONNKEY_ALEN(fw) == sizeof(uint32_t));
		npf_connkey_getkey(fw, &alen, &proto, ips, ids);

		pptp_call_id_put(&ips[NPF_DST], call_id);
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
		.inspect	= pptp_gre_inspect,
		.destroy	= pptp_gre_destroy,
	};

	/* Portmap for the PPTP call ID range. */
	pptp_alg.pm = npf_portmap_create(1, UINT16_MAX);
	if (pptp_alg.pm == NULL) {
		return ENOMEM;
	}
	pptp_alg.tcp = npf_alg_register(npf, "pptp_tcp", &pptp_tcp);
	if (pptp_alg.tcp == NULL) {
		npf_alg_pptp_fini(npf);
		return ENOMEM;
	}
	pptp_alg.gre = npf_alg_register(npf, "pptp_gre", &pptp_gre);
	if (pptp_alg.gre == NULL) {
		npf_alg_pptp_fini(npf);
		return ENOMEM;
	}
	return 0;
}

__dso_public int
npf_alg_pptp_fini(npf_t *npf)
{
	if (pptp_alg.tcp) {
		npf_alg_unregister(npf, pptp_alg.tcp);
		pptp_alg.tcp = NULL;
	}
	if (pptp_alg.gre) {
		npf_alg_unregister(npf, pptp_alg.gre);
		pptp_alg.gre = NULL;
	}
	if (pptp_alg.pm) {
		npf_portmap_destroy(pptp_alg.pm);
		pptp_alg.pm = NULL;
	}
	return 0;
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
