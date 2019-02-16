/*-
 * Copyright (c) 2019 The NetBSD Foundation, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
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

typedef struct npf_regparams {
	struct npf_regparams *	next;
	npf_param_t *		params;
	unsigned		count;
} npf_regparams_t;

typedef struct npf_paraminfo {
	npf_regparams_t *	list;
	thmap_t *		map;
} npf_paraminfo_t;

void
npf_param_init(npf_t *npf)
{
	npf_paraminfo_t *params;

	params = kmem_zalloc(sizeof(npf_paraminfo_t), KM_SLEEP);
	params->map = thmap_create(0, NULL, THMAP_NOCOPY);
	npf->params = params;
}

void
npf_param_fini(npf_t *npf)
{
	npf_paraminfo_t *pinfo = npf->params;
	npf_regparams_t *regparams = pinfo->list;

	while (regparams) {
		npf_param_t *plist = regparams->params;

		for (unsigned i = 0; i < regparams->count; i++) {
			npf_param_t *param = &plist[i];
			const char *name = param->name;
			const size_t namelen = strlen(name);
			void *ret __diagused;

			ret = thmap_del(pinfo->map, name, namelen);
			KASSERT(ret != NULL);
		}
		regparams = regparams->next;
	}
	thmap_destroy(pinfo->map);
	kmem_free(pinfo, sizeof(npf_paraminfo_t));
}

void
npf_param_register(npf_t *npf, npf_param_t *params, unsigned count)
{
	npf_paraminfo_t *pinfo = npf->params;

	for (unsigned i = 0; i < count; i++) {
		npf_param_t *param = &params[i];
		const char *name = param->name;
		const size_t namelen = strlen(name);
		void *ret __diagused;

		ret = thmap_put(pinfo->map, name, namelen, param);
		KASSERT(ret == NULL);
	}
}

static npf_param_t *
npf_param_lookup(npf_t *npf, const char *name)
{
	npf_paraminfo_t *pinfo = npf->params;
	const size_t namelen = strlen(name);
	return thmap_get(pinfo->map, name, namelen);
}

__dso_public int
npf_param_get(npf_t *npf, const char *name, int64_t *val)
{
	npf_param_t *param;

	if ((param = npf_param_lookup(npf, name)) == NULL) {
		return ENOENT;
	}
	*val = *param->valp;
	return 0;
}

__dso_public int
npf_param_set(npf_t *npf, const char *name, int64_t val)
{
	npf_param_t *param;

	if ((param = npf_param_lookup(npf, name)) == NULL) {
		return ENOENT;
	}
	*param->valp = val;
	return 0;
}
