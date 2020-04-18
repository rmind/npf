/*
 * Copyright (c) 2020 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include <lpm.h>

#include "npf_router.h"
#include "utils.h"

struct route_table {
	lpm_t *		lpm;
	unsigned	nitems;
};

typedef struct rtentry {
	unsigned	flags;
	route_info_t	rt;
} rtentry_t;

route_table_t *
route_table_create(void)
{
	route_table_t *rtbl;

	rtbl = calloc(1, sizeof(route_table_t));
	if (rtbl == NULL) {
		return NULL;
	}
	rtbl->lpm = lpm_create();
	return rtbl;
}

static void
route_table_dtor(void *arg __unused, const void *key __unused,
    size_t len __unused, void *val)
{
	free(val);
}

void
route_table_destroy(route_table_t *rtbl)
{
	lpm_clear(rtbl->lpm, route_table_dtor, NULL);
	lpm_destroy(rtbl->lpm);
	free(rtbl);
}

int
route_add(route_table_t *rtbl, const void *addr, unsigned alen,
    unsigned preflen, const route_info_t *rt)
{
	rtentry_t *rtentry;

	if ((rtentry = calloc(1, sizeof(rtentry_t))) == NULL) {
		return -1;
	}
	memcpy(&rtentry->rt, rt, sizeof(route_info_t));

	if (lpm_insert(rtbl->lpm, addr, alen, preflen, rtentry) == -1) {
		free(rtentry);
		return -1;
	}
	return 0;
}

int
route_lookup(route_table_t *rtbl, const void *addr, unsigned alen,
    route_info_t *output_rt)
{
	rtentry_t *rtentry;

	rtentry = lpm_lookup(rtbl->lpm, addr, alen);
	if (rtentry == NULL) {
		return -1;
	}
	memcpy(output_rt, &rtentry->rt, sizeof(route_info_t));
	return 0;
}
