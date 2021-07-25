/*
 * Copyright (c) 2019-2020 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#ifdef _KERNEL
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/types.h>

#include <sys/kmem.h>
#include <sys/queue.h>
#include <sys/thmap.h>
#endif

#include "npf_impl.h"

typedef struct npf_paramreg {
	struct npf_paramreg *	next;
	unsigned		count;
	npf_param_t		params[];
} npf_paramreg_t;

struct npf_paraminfo {
	npf_paramreg_t *	list;
	thmap_t *		map;
};

static inline void
npf_param_general_register(npf_t *npf)
{
	npf_param_t param_map[] = {
		{
			"ip4.reassembly",
			&npf->ip4_reassembly,
			.default_val = 1, // true
			.min = 0, .max = 1
		},
		{
			"ip4.drop_options",
			&npf->ip4_drop_options,
			.default_val = 0, // false
			.min = 0, .max = 1
		},
		{
			"ip6.reassembly",
			&npf->ip6_reassembly,
			.default_val = 0, // false
			.min = 0, .max = 1
		},
		{
			"ip6.drop_options",
			&npf->ip6_drop_options,
			.default_val = 0, // false
			.min = 0, .max = 1
		},
	};
	npf_param_register(npf, param_map, __arraycount(param_map));
}

void
npf_param_init(npf_t *npf)
{
	npf_paraminfo_t *paraminfo;

	paraminfo = kmem_zalloc(sizeof(npf_paraminfo_t), KM_SLEEP);
	paraminfo->map = thmap_create(0, NULL, THMAP_NOCOPY);
	npf->paraminfo = paraminfo;

	/* Register some general parameters. */
	npf_param_general_register(npf);
}

void
npf_param_fini(npf_t *npf)
{
	npf_paraminfo_t *pinfo = npf->paraminfo;
	npf_paramreg_t *paramreg = pinfo->list;

	while (paramreg) {
		npf_param_t *plist = paramreg->params;
		npf_paramreg_t *next = paramreg->next;
		size_t len;

		/* Remove the parameters from the map. */
		for (unsigned i = 0; i < paramreg->count; i++) {
			npf_param_t *param = &plist[i];
			const char *name = param->name;
			void *ret __diagused;

			ret = thmap_del(pinfo->map, name, strlen(name));
			KASSERT(ret != NULL);
		}

		/* Destroy this registry. */
		len = offsetof(npf_paramreg_t, params[paramreg->count]);
		kmem_free(paramreg, len);

		/* Next .. */
		paramreg = next;
	}
	thmap_destroy(pinfo->map);
	kmem_free(pinfo, sizeof(npf_paraminfo_t));
}

int
npf_params_export(const npf_t *npf, nvlist_t *nv)
{
	nvlist_t *params, *dparams;

	/*
	 * Export both the active and default values.  The latter are to
	 * accommodate npfctl so it could distinguish what has been set.
	 */
	params = nvlist_create(0);
	dparams = nvlist_create(0);
	for (npf_paramreg_t *pr = npf->paraminfo->list; pr; pr = pr->next) {
		for (unsigned i = 0; i < pr->count; i++) {
			const npf_param_t *param = &pr->params[i];
			const uint64_t val = *param->valp;
			const uint64_t defval = param->default_val;

			nvlist_add_number(params, param->name, val);
			nvlist_add_number(dparams, param->name, defval);
		}
	}
	nvlist_add_nvlist(nv, "params", params);
	nvlist_add_nvlist(nv, "params-defaults", dparams);
	return 0;
}

void *
npf_param_allocgroup(npf_t *npf, npf_paramgroup_t group, size_t len)
{
	void *params = kmem_zalloc(len, KM_SLEEP);
	npf->params[group] = params;
	return params;
}

void
npf_param_freegroup(npf_t *npf, npf_paramgroup_t group, size_t len)
{
	kmem_free(npf->params[group], len);
	npf->params[group] = NULL; // diagnostic
}

/*
 * npf_param_register: register an array of named parameters.
 */
void
npf_param_register(npf_t *npf, npf_param_t *params, unsigned count)
{
	npf_paraminfo_t *pinfo = npf->paraminfo;
	npf_paramreg_t *paramreg;
	size_t len;

	/*
	 * Copy over the parameters.
	 */
	len = offsetof(npf_paramreg_t, params[count]);
	paramreg = kmem_zalloc(len, KM_SLEEP);
	memcpy(paramreg->params, params, sizeof(npf_param_t) * count);
	paramreg->count = count;
	params = NULL; // dead

	/*
	 * Map the parameter names to the variables.
	 * Assign the default values.
	 */
	for (unsigned i = 0; i < count; i++) {
		npf_param_t *param = &paramreg->params[i];
		const char *name = param->name;
		void *ret __diagused;

		ret = thmap_put(pinfo->map, name, strlen(name), param);
		KASSERT(ret == param);

		/* Assign the default value. */
		KASSERT(param->default_val >= param->min);
		KASSERT(param->default_val <= param->max);
		*param->valp = param->default_val;
	}

	/* Insert the registry of params into the list. */
	paramreg->next = pinfo->list;
	pinfo->list = paramreg;
}

/*
 * NPF param API.
 */

static npf_param_t *
npf_param_lookup(npf_t *npf, const char *name)
{
	npf_paraminfo_t *pinfo = npf->paraminfo;
	const size_t namelen = strlen(name);
	return thmap_get(pinfo->map, name, namelen);
}

int
npf_param_check(npf_t *npf, const char *name, int val)
{
	npf_param_t *param;

	if ((param = npf_param_lookup(npf, name)) == NULL) {
		return ENOENT;
	}
	if (val < param->min || val > param->max) {
		return EINVAL;
	}
	return 0;
}

__dso_public int
npfk_param_get(npf_t *npf, const char *name, int *val)
{
	npf_param_t *param;

	if ((param = npf_param_lookup(npf, name)) == NULL) {
		return ENOENT;
	}
	*val = *param->valp;
	return 0;
}

__dso_public int
npfk_param_set(npf_t *npf, const char *name, int val)
{
	npf_param_t *param;

	if ((param = npf_param_lookup(npf, name)) == NULL) {
		return ENOENT;
	}
	if (val < param->min || val > param->max) {
		return EINVAL;
	}
	*param->valp = val;
	return 0;
}
