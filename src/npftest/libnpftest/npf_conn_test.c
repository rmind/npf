/*
 * NPF connection tests.
 *
 * Public Domain.
 */

#ifdef _KERNEL
#include <sys/types.h>
#endif

#include "npf_impl.h"
#include "npf_conn.h"
#include "npf_test.h"

/*
 * 1 connection  -- no G/C.
 * 2 connections -- 1x G/C.
 * 2 connections -- 2x G/C
 * 512 + 1 connections -- all G/C
 */

static void
run_conn_gc(void)
{
	npf_t *npf = npf_getkernctx();
	npf_conndb_t *cd;

	cd = npf_conndb_create();
	npf_conndb_gc(npf, cd, false, false);
	npf_conndb_destroy(cd);
}

bool
npf_conn_test(bool verbose)
{
	(void)verbose;
	run_conn_gc();
	return true;
}
