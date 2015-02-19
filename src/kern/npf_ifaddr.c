/*	$NetBSD$	*/

/*-
 * Copyright (c) 2014 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Mindaugas Rasiukevicius.
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

/*
 * NPF network interface handling module.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/types.h>
#include <sys/kmem.h>

#include <net/if.h>

#include "npf_impl.h"

#define	NPF_IF_SLOTS	16

static npf_tableset_t *	ifaddr_tableset	__read_mostly;

void
npf_ifaddr_sysinit(void)
{
	ifaddr_tableset = npf_tableset_create(NPF_IF_SLOTS);
	KASSERT(ifaddr_tableset != NULL);
}

void
npf_ifaddr_sysfini(void)
{
	npf_tableset_destroy(ifaddr_tableset);
}

void
npf_ifaddr_sync(const ifnet_t *ifp)
{
	npf_table_t *ift, *t;
	const char *ifname;
	struct ifaddr *ifa;

	KERNEL_LOCK(1, NULL);

	/* First, check whether this interface is of any interest to us. */
	ifname = ifp->if_xname;
	if (!npf_tableset_getbyname(ifaddr_tableset, ifname)) {
		/* Not present: done. */
		KERNEL_UNLOCK_ONE(NULL);
		return;
	}

	/*
	 * Create a new NPF table for the interface.  We have to perform
	 * the sync of the interface addresses.  Note: currently, the list
	 * is protected by the kernel-lock.
	 */
	ift = npf_table_create(ifname, 0, NPF_TABLE_HASH, NULL, 16);
	KASSERT(ift != NULL);

	IFADDR_FOREACH(ifa, ifp) {
		const struct sockaddr *sa = ifa->ifa_addr;

		if (sa->sa_family == AF_INET) {
			const struct sockaddr_in *sin4 = satosin(sa);
			npf_table_insert(t, sizeof(struct in_addr),
			    (const npf_addr_t *)&sin4->sin_addr,
			    NPF_NO_NETMASK);
		}
		if (sa->sa_family == AF_INET6) {
			const struct sockaddr_in *sin6 = satosin6(sa);
			npf_table_insert(t, sizeof(struct in6_addr),
			    (const npf_addr_t *)&sin6->sin6_addr,
			    NPF_NO_NETMASK);
		}
	}
	KERNEL_UNLOCK_ONE(NULL);

	/* Finally, swap the tables and destroy the old one. */
	t = npf_tableset_swap(ifaddr_tableset, ift);
	npf_config_sync();
	npf_table_destroy(t);
}
