/*
 * Copyright (c) 2020 Mindaugas Rasiukevicius <rmind at netbsd org>
 * Copyright (c) 2015 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
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
