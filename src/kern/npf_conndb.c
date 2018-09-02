/*-
 * Copyright (c) 2010-2014 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This material is based upon work partially supported by The
 * NetBSD Foundation under a contract with Mindaugas Rasiukevicius.
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
 * NPF connection storage.
 */

#ifdef _KERNEL
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: npf_conndb.c,v 1.4 2018/09/29 14:41:36 rmind Exp $");

#include <sys/param.h>
#include <sys/types.h>

#include <sys/atomic.h>
#include <sys/kmem.h>
#include <thmap.h>
#endif

#define __NPF_CONN_PRIVATE
#include "npf_conn.h"
#include "npf_impl.h"

struct npf_conndb {
	thmap_t *		cd_map;
	npf_conn_t *		cd_recent;
	npf_conn_t *		cd_list;
	npf_conn_t *		cd_tail;
};

npf_conndb_t *
npf_conndb_create(void)
{
	npf_conndb_t *cd;

	cd = kmem_zalloc(sizeof(npf_conndb_t), KM_SLEEP);
	cd->cd_map = thmap_create(0, NULL, THMAP_NOCOPY);
	KASSERT(cd->cd_map != NULL);
	return cd;
}

void
npf_conndb_destroy(npf_conndb_t *cd)
{
	KASSERT(cd->cd_recent == NULL);
	KASSERT(cd->cd_list == NULL);
	KASSERT(cd->cd_tail == NULL);

	thmap_destroy(cd->cd_map);
	kmem_free(cd, sizeof(npf_conndb_t));
}

/*
 * npf_conndb_lookup: find a connection given the key.
 */
npf_conn_t *
npf_conndb_lookup(npf_conndb_t *cd, const npf_connkey_t *key, bool *forw)
{
	const unsigned keylen = NPF_CONN_KEYLEN(key);
	npf_connkey_t *foundkey;
	npf_conn_t *con;

	/*
	 * Lookup the connection key in the key-value map.
	 */
	foundkey = thmap_get(cd->cd_map, key, keylen);
	if (!foundkey) {
		return NULL;
	}
	con = foundkey->ck_backptr;
	KASSERT(con != NULL);

	/*
	 * Acquire the reference and return the connection.
	 */
	atomic_inc_uint(&con->c_refcnt);
	*forw = (foundkey == &con->c_forw_entry);
	return con;
}

/*
 * npf_conndb_insert: insert the key representing the connection.
 *
 * => Returns true on success and false on failure.
 */
bool
npf_conndb_insert(npf_conndb_t *cd, npf_connkey_t *key)
{
	const unsigned keylen = NPF_CONN_KEYLEN(key);
	return thmap_put(cd->cd_map, key, keylen, key) == key;
}

/*
 * npf_conndb_remove: find and delete connection key, returning the
 * connection it represents.
 */
npf_conn_t *
npf_conndb_remove(npf_conndb_t *cd, npf_connkey_t *key)
{
	const unsigned keylen = NPF_CONN_KEYLEN(key);
	npf_connkey_t *foundkey;
	npf_conn_t *con;

	foundkey = thmap_del(cd->cd_map, key, keylen);
	if (!foundkey) {
		return NULL;
	}
	con = foundkey->ck_backptr;
	KASSERT(con != NULL);
	return con;
}

/*
 * npf_conndb_enqueue: atomically insert the connection into the
 * singly-linked list of "recent" connections.
 */
void
npf_conndb_enqueue(npf_conndb_t *cd, npf_conn_t *con)
{
	npf_conn_t *head;

	do {
		head = cd->cd_recent;
		con->c_next = head;
	} while (atomic_cas_ptr(&cd->cd_recent, head, con) != head);
}

/*
 * npf_conndb_dequeue: remove the connection from a singly-linked list
 * given the previous element; no concurrent writers are allowed here.
 */
void
npf_conndb_dequeue(npf_conndb_t *cd, npf_conn_t *con, npf_conn_t *prev)
{
	if (prev == NULL) {
		KASSERT(cd->cd_list == con);
		cd->cd_list = con->c_next;
	} else {
		prev->c_next = con->c_next;
	}
}

/*
 * npf_conndb_getlist: atomically take the "recent" connections and add
 * them to the singly-linked list of the connections.
 */
npf_conn_t *
npf_conndb_getlist(npf_conndb_t *cd)
{
	npf_conn_t *con, *prev;

	con = atomic_swap_ptr(&cd->cd_recent, NULL);
	if ((prev = cd->cd_tail) == NULL) {
		KASSERT(cd->cd_list == NULL);
		cd->cd_list = con;
	} else {
		KASSERT(prev->c_next == NULL);
		prev->c_next = con;
	}
	return cd->cd_list;
}

/*
 * npf_conndb_settail: assign a new tail of the singly-linked list.
 */
void
npf_conndb_settail(npf_conndb_t *cd, npf_conn_t *con)
{
	KASSERT(con || cd->cd_list == NULL);
	KASSERT(!con || con->c_next == NULL);
	cd->cd_tail = con;
}
