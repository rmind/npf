/*-
 * Copyright (c) 2019-2021 Mindaugas Rasiukevicius <rmind at noxt eu>
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
 * NPF rate limiting (traffic policing).
 *
 * Implements Committed Access Rate (CAR) with RED-like dropping (in order
 * to avoid tail-drop).  It is expected to be more polite to the TCP traffic.
 * The CAR algorithm itself is a variant of the token bucket algorithm.
 *
 * Reference:
 *
 *	S. Vegesna, 2001, IP Quality of Service; Cisco Press; pages 36-37.
 */

#ifdef _KERNEL
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/types.h>
#include <sys/module.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#endif

#include "npf.h"
#include "npf_impl.h"

NPF_EXT_MODULE(npf_ext_ratelimit, "");

#define	NPFEXT_RATELIMIT_VER		1

static void *		npf_ext_ratelimit_id;

typedef struct {
	/*
	 * Variables:
	 * - Committed token bucket counter.
	 * - Compounded debt counter.
	 * - Last refill time.
	 */
	int64_t		tc;
	uint64_t	compounded;
	uint64_t	tslast;

	/*
	 * Constants:
	 *
	 * - Committed information rate (bits/s); normalized to the
	 *   number of tokens (-- equivalent to bytes) in a time unit.
	 *
	 * - Committed burst size (bytes)
	 *
	 * - Extended burst size (bytes)
	 */
	uint64_t	cir_tok;
	uint64_t	cbs;
	uint64_t	ebs;
} car_state_t;

typedef struct {
	car_state_t	car;
	kmutex_t	lock;
} npf_ext_ratelimit_t;

#define	MSEC_IN_SEC	(1000)

static int
npf_ext_ratelimit_ctor(npf_rproc_t *rproc, const nvlist_t *params)
{
	npf_ext_ratelimit_t *rl;
	car_state_t *car;
	uint64_t bitrate;

	rl = kmem_zalloc(sizeof(npf_ext_ratelimit_t), KM_SLEEP);
	mutex_init(&rl->lock, MUTEX_DEFAULT, IPL_SOFTNET);
	car = &rl->car;

	/*
	 * Get the bit rate (CIR).
	 *
	 * It is normalized to the number of tokens in a millisecond.
	 * Note: millisecond-level resolution is sufficient to handle
	 * kilobits.  Rate limiting at less than a kilobit has little
	 * practical use and is not supported.
	 */
	bitrate = dnvlist_get_number(params, "bitrate", 0);
	car->cir_tok = (bitrate >> 3) / MSEC_IN_SEC;

	car->cbs = dnvlist_get_number(params, "normal-burst", 0);
	car->ebs = dnvlist_get_number(params, "extended-burst", 0);

	/*
	 * Industry-standard defaults:
	 *
	 * normal burst (CBS) = bit-rate * (1-byte / 8-bits) * 1.5 second
	 * extended burst (EBS) = 2 * normal burst
	 *
	 * Note: buckets are in bytes, hence the division by 8.
	 */

	if (!car->cbs) {
		car->cbs = (bitrate >> 3) + (bitrate >> 4);
	}
	if (!car->ebs) {
		car->ebs = car->cbs * 2;
	}

	npf_rproc_assign(rproc, rl);
	return 0;
}

static void
npf_ext_ratelimit_dtor(npf_rproc_t *rproc, void *meta)
{
	npf_ext_ratelimit_t *rl = meta;

	mutex_destroy(&rl->lock);
	kmem_free(rl, sizeof(npf_ext_ratelimit_t));
}

/*
 * car_ratelimit: run the CAR algorithm on a given packet.
 *
 * => Returns true if packet does not exceed the limit; false otherwise.
 */
static bool
car_ratelimit(car_state_t *car, const uint64_t tsnow, const size_t nbytes)
{
	uint64_t actual_debt, compounded_debt;
	int64_t remaining;

	/*
	 * Calculate the time difference and convert it into tokens.
	 * Refill committed token bucket, limiting it to the normal burst.
	 */
	if (tsnow > car->tslast) {
		uint64_t tokens = (tsnow - car->tslast) * car->cir_tok;
		int64_t tc = car->tc + tokens;
		car->tc = MIN(tc, (int64_t)car->cbs);
		car->tslast = tsnow;
	}

	/* Re-check. */
	remaining = car->tc - (int64_t)nbytes;
	if (remaining >= 0) {
		car->tc -= nbytes;
		return true; // green
	}

	/*
	 * Extended burst logic.  Some concepts:
	 *
	 *	Actual debt -- the number of tokens currently borrowed
	 *	since the last packet drop; it is represented by a negative
	 *	'tc' value which gets reduced with the new tokens over time.
	 *
	 *	Compounded debt -- the sum of all actual debts since the
	 *	last packet drop.
	 *
	 * The compounded debt is reset to zero after each packet drop.
	 * On the receive of a new borrowing packet, the compounded debt is
	 * initialized to the actual debt.  The actual debt is never reset.
	 *
	 * Logic:
	 *
	 * - If the actual debt is greater than EBS, then packets are dropped
	 *   until the actual debt is reduced by token accumulation.
	 *
	 * - If the compounded debt is greater than EBS, then the packet is
	 *   dropped and the compounded debt is set to 0.
	 *
	 * - Otherwise, the packet is sent and the actual debt is incremented
	 *   by the packet length; the compounded debt is incremented by the
	 *   newly calculated actual debt.
	 *
	 * With such logic, packets are considered 'red' with the probability
	 * equal to the consumption of the extended burst (see the reference,
	 * page 38).
	 */

	/* Note: computing the *proposed* debt values. */
	actual_debt = -car->tc + nbytes;
	compounded_debt = car->compounded + actual_debt;

	if (actual_debt > car->ebs) {
		/* Certainly red. */
		car->compounded = 0;
		return false;
	}

	if (compounded_debt > car->ebs) {
		/* Probably red. */
		car->compounded = 0;
		return false;
	}

	car->tc = -(int64_t)actual_debt;
	car->compounded = compounded_debt;

	return true; // yellow
}

static bool
npf_ext_ratelimit(npf_cache_t *npc, void *meta, const npf_match_info_t *mi,
    int *decision)
{
	npf_ext_ratelimit_t *rl = meta;
	struct timespec ts;
	uint64_t ts_msec;
	size_t pktlen;

	/* Skip, if already blocking. */
	if (*decision == NPF_DECISION_BLOCK) {
		return true;
	}
	pktlen = nbuf_datalen(npc->npc_nbuf);

	/* Get the current time and convert to milliseconds. */
	getnanouptime(&ts);
	ts_msec = (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);

	/* Run the rate-limiting algorithm. */
	mutex_enter(&rl->lock);
	if (!car_ratelimit(&rl->car, ts_msec, pktlen)) {
		*decision = NPF_DECISION_BLOCK;
	}
	mutex_exit(&rl->lock);
	return true;
}

__dso_public int
npf_ext_ratelimit_init(npf_t *npf)
{
	static const npf_ext_ops_t npf_ratelimit_ops = {
		.version	= NPFEXT_RATELIMIT_VER,
		.ctx		= NULL,
		.ctor		= npf_ext_ratelimit_ctor,
		.dtor		= npf_ext_ratelimit_dtor,
		.proc		= npf_ext_ratelimit
	};
	npf_ext_ratelimit_id = npf_ext_register(npf, "ratelimit",
	    &npf_ratelimit_ops);
	return npf_ext_ratelimit_id ? 0 : EEXIST;
}

__dso_public int
npf_ext_ratelimit_fini(npf_t *npf)
{
	return npf_ext_unregister(npf, npf_ext_ratelimit_id);
}

#ifdef _KERNEL
static int
npf_ext_ratelimit_modcmd(modcmd_t cmd, void *arg)
{
	npf_t *npf = npf_getkernctx();

	switch (cmd) {
	case MODULE_CMD_INIT:
		return npf_ext_ratelimit_init(npf);
	case MODULE_CMD_FINI:
		return npf_ext_ratelimit_fini(npf);
	case MODULE_CMD_AUTOUNLOAD:
		return npf_autounload_p() ? 0 : EBUSY;
	default:
		return ENOTTY;
	}
	return 0;
}
#endif
