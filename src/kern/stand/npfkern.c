/*-
 * Copyright (c) 2020 Mindaugas Rasiukevicius <rmind at netbsd org>
 * Copyright (c) 2015 The NetBSD Foundation, Inc.
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

#include <sys/types.h>
#include <stdbool.h>
#include <inttypes.h>

#include "../npf_impl.h"
#include "../npfkern.h"

struct npf;

/*
 * npfk_socket_load: receive the NPF configuration change request
 * and process it (e.g. (re)load the active configuration).
 *
 * => Returns 0 on success and -1 on error.
 * => Assumes a blocking socket.
 */
__dso_public int
npfk_socket_load(npf_t *npf, int sock)
{
	nvlist_t *req, *resp;
	uint64_t op;
	int error;

	req = nvlist_recv(sock, 0);
	if (__predict_false(req == NULL)) {
		return -1;
	}
	resp = nvlist_create(0);
	op = dnvlist_get_number(req, "operation", UINT64_MAX);
	(void)npfctl_run_op(npf, op, req, resp);
	nvlist_destroy(req);

	error = nvlist_send(sock, resp);
	nvlist_destroy(resp);
	return error;
}

bool
npf_active_p(void)
{
	return true;
}

void
npf_ifaddr_syncall(struct npf *npf)
{
	(void)npf;
}
