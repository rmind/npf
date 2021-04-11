/*-
 * Copyright (c) 2021 The NetBSD Foundation, Inc.
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

#include <sys/cdefs.h>
__RCSID("$NetBSD$");

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <errno.h>

#include <npf.h>

int		npfext_ratelimit_init(void);
nl_ext_t *	npfext_ratelimit_construct(const char *);
int		npfext_ratelimit_param(nl_ext_t *, const char *, const char *);

int
npfext_ratelimit_init(void)
{
	/* Nothing to initialisz. */
	return 0;
}

nl_ext_t *
npfext_ratelimit_construct(const char *name)
{
	assert(strcmp(name, "ratelimit") == 0);
	return npf_ext_construct(name);
}

int
npfext_ratelimit_param(nl_ext_t *ext, const char *param, const char *val)
{
	static const char *params[] = {
		"bitrate", "normal-burst", "extended-burst"
	};

	for (unsigned i = 0; i < __arraycount(params); i++) {
		const char *name = params[i];
		uint64_t nval;

		if (strcmp(name, param) != 0) {
			continue;
		}
		if (!val || (nval = atoll(val)) == 0) {
			return EINVAL;
		}
		npf_ext_param_u64(ext, name, nval);
		return 0;
	}

	/* Invalid parameter, if not found. */
	return EINVAL;
}
