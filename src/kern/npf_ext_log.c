/*
 * Copyright (c) 2010-2012 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

/*
 * NPF logging extension.
 */

#ifdef _KERNEL
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/types.h>
#include <sys/module.h>

#include <sys/conf.h>
#include <sys/kmem.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/queue.h>

#include <net/if.h>
#include <net/if_types.h>
#include <net/bpf.h>
#endif

#include "npf_impl.h"
#include "if_npflog.h"

NPF_EXT_MODULE(npf_ext_log, "");

#define	NPFEXT_LOG_VER		1

static void *		npf_ext_log_id;

typedef struct {
	unsigned int	if_idx;
} npf_ext_log_t;

static int
npf_log_ctor(npf_rproc_t *rp, const nvlist_t *params)
{
	npf_ext_log_t *meta;

	meta = kmem_zalloc(sizeof(npf_ext_log_t), KM_SLEEP);
	meta->if_idx = dnvlist_get_number(params, "log-interface", 0);
	npf_rproc_assign(rp, meta);
	return 0;
}

static void
npf_log_dtor(npf_rproc_t *rp, void *meta)
{
	kmem_free(meta, sizeof(npf_ext_log_t));
}

static bool
npf_log(npf_cache_t *npc, void *meta, const npf_match_info_t *mi, int *decision)
{
	struct mbuf *m = nbuf_head_mbuf(npc->npc_nbuf);
	const npf_ext_log_t *log = meta;
	struct psref psref;
	ifnet_t *ifp;
	struct npfloghdr hdr;

	memset(&hdr, 0, sizeof(hdr));
	/* Set the address family. */
	if (npf_iscached(npc, NPC_IP4)) {
		hdr.af = AF_INET;
	} else if (npf_iscached(npc, NPC_IP6)) {
		hdr.af = AF_INET6;
	} else {
		hdr.af = AF_UNSPEC;
	}

	hdr.length = NPFLOG_REAL_HDRLEN;
	hdr.action = *decision == NPF_DECISION_PASS ?
	    0 /* pass */ : 1 /* block */;
	hdr.reason = 0;	/* match */

	struct nbuf *nb = npc->npc_nbuf;
	npf_ifmap_copyname(npc->npc_ctx, nb ? nb->nb_ifid : 0,
	    hdr.ifname, sizeof(hdr.ifname));

	hdr.rulenr = htonl((uint32_t)mi->mi_rid);
	hdr.subrulenr = htonl((uint32_t)(mi->mi_rid >> 32));
	strlcpy(hdr.ruleset, "rules", sizeof(hdr.ruleset));

	hdr.uid = UID_MAX;
	hdr.pid = (pid_t)-1;
	hdr.rule_uid = UID_MAX;
	hdr.rule_pid = (pid_t)-1;

	switch (mi->mi_di) {
	default:
	case PFIL_IN|PFIL_OUT:
		hdr.dir = 0;
		break;
	case PFIL_IN:
		hdr.dir = 1;
		break;
	case PFIL_OUT:
		hdr.dir = 2;
		break;
	}

	KERNEL_LOCK(1, NULL);

	/* Find a pseudo-interface to log. */
	ifp = if_get_byindex(log->if_idx, &psref);
	if (ifp == NULL) {
		/* No interface. */
		KERNEL_UNLOCK_ONE(NULL);
		return true;
	}

	if_statadd2(ifp, if_opackets, 1, if_obytes, m->m_pkthdr.len);
	if (ifp->if_bpf) {
		/* Pass through BPF. */
		bpf_mtap2(ifp->if_bpf, &hdr, NPFLOG_HDRLEN, m, BPF_D_OUT);
	}
	if_put(ifp, &psref);

	KERNEL_UNLOCK_ONE(NULL);

	return true;
}

__dso_public int
npf_ext_log_init(npf_t *npf)
{
	static const npf_ext_ops_t npf_log_ops = {
		.version	= NPFEXT_LOG_VER,
		.ctx		= NULL,
		.ctor		= npf_log_ctor,
		.dtor		= npf_log_dtor,
		.proc		= npf_log
	};
	npf_ext_log_id = npf_ext_register(npf, "log", &npf_log_ops);
	return npf_ext_log_id ? 0 : EEXIST;
}

__dso_public int
npf_ext_log_fini(npf_t *npf)
{
	return npf_ext_unregister(npf, npf_ext_log_id);
}

#ifdef _KERNEL
static int
npf_ext_log_modcmd(modcmd_t cmd, void *arg)
{
	npf_t *npf = npf_getkernctx();

	switch (cmd) {
	case MODULE_CMD_INIT:
		return npf_ext_log_init(npf);
	case MODULE_CMD_FINI:
		return npf_ext_log_fini(npf);
		break;
	case MODULE_CMD_AUTOUNLOAD:
		return npf_autounload_p() ? 0 : EBUSY;
	default:
		return ENOTTY;
	}
	return 0;
}
#endif
