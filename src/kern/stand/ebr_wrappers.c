/*
 * Copyright (c) 2019 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#include <sys/cdefs.h>
#include <stdbool.h>
#include <assert.h>

#include <qsbr/ebr.h>

#include "../npf_impl.h"

/*
 * Epoch-Based Reclamation (EBR) wrappers.
 */

ebr_t *
npf_ebr_create(void)
{
	return ebr_create();
}

void
npf_ebr_destroy(ebr_t *ebr)
{
	return ebr_destroy(ebr);
}

void
npf_ebr_register(ebr_t *ebr)
{
	ebr_register(ebr);
}

void
npf_ebr_unregister(ebr_t *ebr)
{
	ebr_unregister(ebr);
}

int
npf_ebr_enter(ebr_t *ebr)
{
	ebr_enter(ebr);
	return NPF_DIAG_MAGIC_VAL;
}

void
npf_ebr_exit(ebr_t *ebr, int s)
{
	assert(s == NPF_DIAG_MAGIC_VAL);
	ebr_exit(ebr);
}

void
npf_ebr_full_sync(ebr_t *ebr)
{
	ebr_full_sync(ebr, 1);
}

bool
npf_ebr_incrit_p(ebr_t *ebr)
{
	return ebr_incrit_p(ebr);
}
