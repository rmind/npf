/*
 * Copyright (c) 2010-2012 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

/*
 * NPF state engine to track connection.
 */

#ifdef _KERNEL
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mutex.h>
#endif

#include "npf_impl.h"

/*
 * Generic connection states and timeout table.
 *
 * Note: used for connection-less protocols.
 */

#define	NPF_ANY_CONN_CLOSED		0
#define	NPF_ANY_CONN_NEW		1
#define	NPF_ANY_CONN_ESTABLISHED	2
#define	NPF_ANY_CONN_NSTATES		3

/*
 * Parameters.
 */
typedef struct {
	int		timeouts[NPF_ANY_CONN_NSTATES];
	int		gre_timeout;
} npf_state_params_t;

/*
 * Generic FSM.
 */
static const uint8_t npf_generic_fsm[NPF_ANY_CONN_NSTATES][2] = {
	[NPF_ANY_CONN_CLOSED] = {
		[NPF_FLOW_FORW]		= NPF_ANY_CONN_NEW,
	},
	[NPF_ANY_CONN_NEW] = {
		[NPF_FLOW_FORW]		= NPF_ANY_CONN_NEW,
		[NPF_FLOW_BACK]		= NPF_ANY_CONN_ESTABLISHED,
	},
	[NPF_ANY_CONN_ESTABLISHED] = {
		[NPF_FLOW_FORW]		= NPF_ANY_CONN_ESTABLISHED,
		[NPF_FLOW_BACK]		= NPF_ANY_CONN_ESTABLISHED,
	},
};

/*
 * State sampler for debugging.
 */
#if defined(_NPF_TESTING)
static void (*npf_state_sample)(npf_state_t *, bool) = NULL;
#define	NPF_STATE_SAMPLE(n, r) if (npf_state_sample) (*npf_state_sample)(n, r);
#else
#define	NPF_STATE_SAMPLE(n, r)
#endif

void
npf_state_sysinit(npf_t *npf)
{
	npf_state_params_t *params = npf_param_allocgroup(npf,
	    NPF_PARAMS_GENERIC_STATE, sizeof(npf_state_params_t));
	npf_param_t param_map[] = {
		/*
		 * Generic timeout (in seconds).
		 */
		{
			"state.generic.timeout.closed",
			&params->timeouts[NPF_ANY_CONN_CLOSED],
			.default_val = 0,
			.min = 0, .max = INT_MAX
		},
		{
			"state.generic.timeout.new",
			&params->timeouts[NPF_ANY_CONN_NEW],
			.default_val = 30,
			.min = 0, .max = INT_MAX
		},
		{
			"state.generic.timeout.established",
			&params->timeouts[NPF_ANY_CONN_ESTABLISHED],
			.default_val = 60,
			.min = 0, .max = INT_MAX
		},
		{
			"state.generic.timeout.gre",
			&params->gre_timeout,
			.default_val = 24 * 60 * 60,
			.min = 0, .max = INT_MAX
		},
	};
	npf_param_register(npf, param_map, __arraycount(param_map));
	npf_state_tcp_sysinit(npf);
}

void
npf_state_sysfini(npf_t *npf)
{
	const size_t len = sizeof(npf_state_params_t);
	npf_param_freegroup(npf, NPF_PARAMS_GENERIC_STATE, len);
	npf_state_tcp_sysfini(npf);
}

/*
 * npf_state_init: initialise the state structure.
 *
 * Should normally be called on a first packet, which also determines the
 * direction in a case of connection-orientated protocol.  Returns true on
 * success and false otherwise (e.g. if protocol is not supported).
 */
bool
npf_state_init(npf_cache_t *npc, npf_state_t *nst)
{
	const int proto = npc->npc_proto;
	bool ret;

	KASSERT(npf_iscached(npc, NPC_IP46));
	KASSERT(npf_iscached(npc, NPC_LAYER4));

	memset(nst, 0, sizeof(npf_state_t));

	switch (proto) {
	case IPPROTO_TCP:
		/* Pass to TCP state tracking engine. */
		ret = npf_state_tcp(npc, nst, NPF_FLOW_FORW);
		break;
	case IPPROTO_UDP:
	case IPPROTO_ICMP:
	case IPPROTO_GRE:
		/* Generic. */
		nst->nst_state = npf_generic_fsm[nst->nst_state][NPF_FLOW_FORW];
		ret = true;
		break;
	default:
		ret = false;
	}
	NPF_STATE_SAMPLE(nst, ret);
	return ret;
}

void
npf_state_destroy(npf_state_t *nst)
{
	nst->nst_state = 0;
}

/*
 * npf_state_inspect: inspect the packet according to the protocol state.
 *
 * Return true if packet is considered to match the state (e.g. for TCP,
 * the packet belongs to the tracked connection) and false otherwise.
 */
bool
npf_state_inspect(npf_cache_t *npc, npf_state_t *nst, const npf_flow_t flow)
{
	const int proto = npc->npc_proto;
	bool ret;

	switch (proto) {
	case IPPROTO_TCP:
		/* Pass to TCP state tracking engine. */
		ret = npf_state_tcp(npc, nst, flow);
		break;
	case IPPROTO_UDP:
	case IPPROTO_ICMP:
	case IPPROTO_GRE:
		/* Generic. */
		nst->nst_state = npf_generic_fsm[nst->nst_state][flow];
		ret = true;
		break;
	default:
		ret = false;
	}
	NPF_STATE_SAMPLE(nst, ret);

	return ret;
}

/*
 * npf_state_etime: return the expiration time depending on the state.
 */
int
npf_state_etime(npf_t *npf, const npf_state_t *nst, const int proto)
{
	const npf_state_params_t *params;
	const unsigned state = nst->nst_state;
	int timeout = 0;

	switch (proto) {
	case IPPROTO_TCP:
		/* Pass to TCP state tracking engine. */
		timeout = npf_state_tcp_timeout(npf, nst);
		break;
	case IPPROTO_UDP:
	case IPPROTO_ICMP:
		/* Generic. */
		params = npf->params[NPF_PARAMS_GENERIC_STATE];
		timeout = params->timeouts[state];
		break;
	case IPPROTO_GRE:
		params = npf->params[NPF_PARAMS_GENERIC_STATE];
		timeout = params->gre_timeout;
		break;
	default:
		KASSERT(false);
	}
	return timeout;
}

void
npf_state_dump(const npf_state_t *nst)
{
#if defined(DDB) || defined(_NPF_TESTING)
	const npf_tcpstate_t *fst = &nst->nst_tcpst[0];
	const npf_tcpstate_t *tst = &nst->nst_tcpst[1];

	printf("\tstate (%p) %d:\n\t\t"
	    "F { end %u maxend %u mwin %u wscale %u }\n\t\t"
	    "T { end %u maxend %u mwin %u wscale %u }\n",
	    nst, nst->nst_state,
	    fst->nst_end, fst->nst_maxend, fst->nst_maxwin, fst->nst_wscale,
	    tst->nst_end, tst->nst_maxend, tst->nst_maxwin, tst->nst_wscale
	);
#endif
}

#if defined(_NPF_TESTING)
void
npf_state_setsampler(void (*func)(npf_state_t *, bool))
{
	npf_state_sample = func;
}
#endif
