/*
 * Copyright (c) 2012 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

/*
 * NPF random blocking extension - kernel module.
 * This is also a demo extension.
 */

#ifdef _KERNEL
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/types.h>
#include <sys/cprng.h>
#include <sys/atomic.h>
#include <sys/module.h>
#include <sys/kmem.h>
#endif

#include "npf_impl.h"

/*
 * NPF extension module definition and the identifier.
 */
NPF_EXT_MODULE(npf_ext_rndblock, "");

#define	NPFEXT_RNDBLOCK_VER		1

static void *		npf_ext_rndblock_id;

#define	PERCENTAGE_BASE	10000

/*
 * Meta-data structure, containing parameters.
 */
typedef struct {
	unsigned int	mod;
	unsigned long	counter;
	unsigned int	percentage;
} npf_ext_rndblock_t;

/*
 * npf_ext_rndblock_ctor: a constructor to parse and store any parameters
 * associated with a rule procedure, which is being newly created.
 */
static int
npf_ext_rndblock_ctor(npf_rproc_t *rp, const nvlist_t *params)
{
	npf_ext_rndblock_t *meta;

	/*
	 * Allocate and a associate a structure for the parameter
	 * and our meta-data.
	 */
	meta = kmem_zalloc(sizeof(npf_ext_rndblock_t), KM_SLEEP);
	meta->mod = dnvlist_get_number(params, "mod", 0);
	meta->percentage = dnvlist_get_number(params, "percentage", 0);
	npf_rproc_assign(rp, meta);

	return 0;
}

/*
 * npf_ext_rndblock_dtor: a destructor for our rule procedure.
 */
static void
npf_ext_rndblock_dtor(npf_rproc_t *rp, void *meta)
{
	/* Free our meta-data, associated with the procedure. */
	kmem_free(meta, sizeof(npf_ext_rndblock_t));
}

/*
 * npf_ext_rndblock: main routine implementing the extension functionality.
 */
static bool
npf_ext_rndblock(npf_cache_t *npc, void *meta, const npf_match_info_t *mi,
    int *decision)
{
	npf_ext_rndblock_t *rndblock = meta;
	unsigned long c;

	/* Skip, if already blocking. */
	if (*decision == NPF_DECISION_BLOCK) {
		return true;
	}

	/*
	 * Sample demo:
	 *
	 * Drop the packets according to the given module or percentage.
	 *
	 * Rule procedures may be executed concurrently in an SMP system.
	 * Use atomic operation to increment the counter.
	 */
	c = atomic_inc_ulong_nv(&rndblock->counter);

	if (rndblock->mod) {
		if ((c % rndblock->mod) == 0) {
			*decision = NPF_DECISION_BLOCK;
		}
	}

	if (rndblock->percentage) {
		uint32_t w = cprng_fast32() % PERCENTAGE_BASE;
		if (w <= rndblock->percentage) {
			*decision = NPF_DECISION_BLOCK;
		}
	}

	return true;
}

__dso_public int
npf_ext_rndblock_init(npf_t *npf)
{
	static const npf_ext_ops_t npf_rndblock_ops = {
		.version	= NPFEXT_RNDBLOCK_VER,
		.ctx		= NULL,
		.ctor		= npf_ext_rndblock_ctor,
		.dtor		= npf_ext_rndblock_dtor,
		.proc		= npf_ext_rndblock
	};

	/*
	 * Initialize the NPF extension.  Register the "rndblock" extension
	 * calls (constructor, destructor, the processing routine, etc).
	 */
	npf_ext_rndblock_id = npf_ext_register(npf, "rndblock",
	    &npf_rndblock_ops);
	return npf_ext_rndblock_id ? 0 : EEXIST;
}

__dso_public int
npf_ext_rndblock_fini(npf_t *npf)
{
	/*
	 * Remove the rndblock extension.  NPF may return an if there
	 * are active references and it cannot drain them.
	 */
	return npf_ext_unregister(npf, npf_ext_rndblock_id);
}

#ifdef _KERNEL
/*
 * Kernel module interface.
 */
static int
npf_ext_rndblock_modcmd(modcmd_t cmd, void *arg)
{
	npf_t *npf = npf_getkernctx();

	switch (cmd) {
	case MODULE_CMD_INIT:
		return npf_ext_rndblock_init(npf);
	case MODULE_CMD_FINI:
		return npf_ext_rndblock_fini(npf);
	case MODULE_CMD_AUTOUNLOAD:
		/* Allow auto-unload only if NPF permits it. */
		return npf_autounload_p() ? 0 : EBUSY;
	default:
		return ENOTTY;
	}
	return 0;
}
#endif
