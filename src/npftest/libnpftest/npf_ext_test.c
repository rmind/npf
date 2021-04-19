/*
 * NPF extension tests.
 *
 * Public Domain.
 */

#ifdef _KERNEL
#include <sys/types.h>
#include <sys/kmem.h>
#endif

#include "npf.h"
#include "npf_impl.h"
#include "npf_test.h"

static bool
npf_ratelimit_test(npf_t *npf)
{
	int error;

	error = npf_ext_ratelimit_init(npf);
	assert(error == 0);
	npf_ext_ratelimit_fini(npf);

	return true;
}

bool
npf_ext_test(bool verbose)
{
	npf_t *npf = npf_getkernctx();
	bool ok;

	ok = npf_ratelimit_test(npf);
	CHECK_TRUE(ok);

	(void)verbose;
	return ok;
}
