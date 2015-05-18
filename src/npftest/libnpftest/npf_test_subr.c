/*	$NetBSD: npf_test_subr.c,v 1.11 2014/08/10 16:44:37 tls Exp $	*/

/*
 * NPF initialisation and handler routines.
 *
 * Public Domain.
 */

#ifdef _KERNEL
#include <sys/types.h>
#include <sys/cprng.h>
#include <net/if.h>
#include <net/if_types.h>
#endif

#include "npf_impl.h"
#include "npf_test.h"

/* State of the current stream. */
static npf_state_t	cstream_state;
static void *		cstream_ptr;
static bool		cstream_retval;

static long		(*_random_func)(void);
static int		(*_pton_func)(int, const char *, void *);
static const char *	(*_ntop_func)(int, const void *, char *, socklen_t);

static void		npf_state_sample(npf_state_t *, bool);

struct ifnet {
	char		name[32];
	void *		arg;
	struct ifnet *	next;
};

static struct ifnet *	npftest_ifnet_list = NULL;

static const char *	npftest_ifop_getname(ifnet_t *);
static void		npftest_ifop_flush(void *);
static void *		npftest_ifop_getmeta(const ifnet_t *);
static void		npftest_ifop_setmeta(ifnet_t *, void *);

static const npf_ifops_t npftest_ifops = {
	.getname	= npftest_ifop_getname,
	.lookup		= npf_test_getif,
	.flush		= npftest_ifop_flush,
	.getmeta	= npftest_ifop_getmeta,
	.setmeta	= npftest_ifop_setmeta,
};

void
npf_test_init(int (*pton_func)(int, const char *, void *),
    const char *(*ntop_func)(int, const void *, char *, socklen_t),
    long (*rndfunc)(void))
{
	npf_t *npf;

	npf_worker_sysinit(1);
	npf = npf_create(&npftest_mbufops, &npftest_ifops);
	npf_setkernctx(npf);

	npf_state_setsampler(npf_state_sample);
	_pton_func = pton_func;
	_ntop_func = ntop_func;
	_random_func = rndfunc;
}

void
npf_test_fini(void)
{
	npf_t *npf = npf_getkernctx();
	npf_destroy(npf);
	npf_sysfini();
}

int
npf_test_load(const void *xml)
{
	prop_dictionary_t npf_dict = prop_dictionary_internalize(xml);
	return npfctl_load(npf_getkernctx(), 0, npf_dict);
}

ifnet_t *
npf_test_addif(const char *ifname, bool reg, bool verbose)
{
	npf_t *npf = npf_getkernctx();
	ifnet_t *ifp = calloc(1, sizeof(struct ifnet));

	/*
	 * This is a "fake" interface with explicitly set index.
	 * Note: test modules may not setup pfil(9) hooks and if_attach()
	 * may not trigger npf_ifmap_attach(), so we call it manually.
	 */
	strlcpy(ifp->name, ifname, sizeof(ifp->name));
	ifp->next = npftest_ifnet_list;
	npftest_ifnet_list = ifp;

	npf_ifmap_attach(npf, ifp);
	if (reg) {
		npf_ifmap_register(npf, ifname);
	}

	if (verbose) {
		printf("+ Interface %s\n", ifname);
	}
	return ifp;
}

static const char *
npftest_ifop_getname(ifnet_t *ifp)
{
	return ifp->name;
}

ifnet_t *
npf_test_getif(const char *ifname)
{
	ifnet_t *ifp = npftest_ifnet_list;

	while (ifp) {
		if (!strcmp(ifp->name, ifname))
			break;
		ifp = ifp->next;
	}
	return ifp;
}

static void
npftest_ifop_flush(void *arg)
{
	ifnet_t *ifp = npftest_ifnet_list;

	while (ifp) {
		ifp->arg = arg;
		ifp = ifp->next;
	}
}

static void *
npftest_ifop_getmeta(const ifnet_t *ifp)
{
	return ifp->arg;
}

static void
npftest_ifop_setmeta(ifnet_t *ifp, void *arg)
{
	ifp->arg = arg;
}

/*
 * State sampler - this routine is called from inside of NPF state engine.
 */
static void
npf_state_sample(npf_state_t *nst, bool retval)
{
	/* Pointer will serve as an ID. */
	cstream_ptr = nst;
	memcpy(&cstream_state, nst, sizeof(npf_state_t));
	cstream_retval = retval;
}

int
npf_test_statetrack(const void *data, size_t len, ifnet_t *ifp,
    bool forw, int64_t *result)
{
	npf_t *npf = npf_getkernctx();
	struct mbuf *m;
	int i = 0, error;

	m = mbuf_getwithdata(data, len);
	error = npf_packet_handler(npf, &m, ifp, forw ? PFIL_OUT : PFIL_IN);
	if (error) {
		assert(m == NULL);
		return error;
	}
	assert(m != NULL);
	m_freem(m);

	const int di = forw ? NPF_FLOW_FORW : NPF_FLOW_BACK;
	npf_tcpstate_t *fstate = &cstream_state.nst_tcpst[di];
	npf_tcpstate_t *tstate = &cstream_state.nst_tcpst[!di];

	result[i++] = (intptr_t)cstream_ptr;
	result[i++] = cstream_retval;
	result[i++] = cstream_state.nst_state;

	result[i++] = fstate->nst_end;
	result[i++] = fstate->nst_maxend;
	result[i++] = fstate->nst_maxwin;
	result[i++] = fstate->nst_wscale;

	result[i++] = tstate->nst_end;
	result[i++] = tstate->nst_maxend;
	result[i++] = tstate->nst_maxwin;
	result[i++] = tstate->nst_wscale;

	return 0;
}

int
npf_inet_pton(int af, const char *src, void *dst)
{
	return _pton_func(af, src, dst);
}

const char *
npf_inet_ntop(int af, const void *src, char *dst, socklen_t size)
{
	return _ntop_func(af, src, dst, size);
}

#ifdef _KERNEL
/*
 * Need to override cprng_fast32() -- we need deterministic PRNG.
 */
uint32_t
cprng_fast32(void)
{
	return (uint32_t)(_random_func ? _random_func() : random());
}
#endif
