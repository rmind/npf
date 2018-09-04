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

#define	NPF_GC_STEP		512

struct npf_conndb {
	thmap_t *		cd_map;
	npf_conn_t *		cd_new;

	/*
	 * The head and tail of the connection list.  The list is
	 * protected by the conn_lock, but the "new" connections are
	 * transferred to it atomically.
	 */
	npf_conn_t *		cd_list;
	npf_conn_t *		cd_tail;
	npf_conn_t *		cd_prev;
	npf_conn_t *		cd_gclist;
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
	KASSERT(cd->cd_new == NULL);
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
 * singly-linked list of the "new" connections.
 */
void
npf_conndb_enqueue(npf_conndb_t *cd, npf_conn_t *con)
{
	npf_conn_t *head;

	do {
		head = cd->cd_new;
		con->c_next = head;
	} while (atomic_cas_ptr(&cd->cd_new, head, con) != head);
}

/*
 * npf_conndb_getlist: return the list of all connections.
 */
npf_conn_t *
npf_conndb_getlist(npf_conndb_t *cd)
{
	return cd->cd_list;
}

/*
 * npf_conndb_getall: return the list of all connections, but before that,
 * atomically take the "new" connections and add them to the singly-linked
 * list of the all connections.
 */
static void
npf_conndb_update(npf_conndb_t *cd)
{
	npf_conn_t *con, *prev;

	/*
	 * FIXME: Unable to maintain the tail pointer without the settail..
	 */

	con = atomic_swap_ptr(&cd->cd_recent, NULL);
	if ((prev = cd->cd_tail) == NULL) {
		KASSERT(cd->cd_list == NULL);
		cd->cd_list = con;
	} else {
		KASSERT(prev->c_next == NULL);
		prev->c_next = con;
	}
}

/*
 * npf_conndb_getnext: return the next connection since the last call.
 * If called for the first, returns the first connection in the list.
 * Once the last connection is reached, it starts from the beginning.
 */
static inline npf_conn_t *
npf_conndb_getnext(npf_conndb_t *cd)
{
	npf_conn_t *itercon = cd->cd_iter;
	cd->cd_iter = itercon->cd_next;
	return cd->cd_iter;

	if (!con->c_next) {
		cd->cd_tail = con;
	}
	if (cd->cd_next;cd->cd_iter;
}

void
npf_conndb_togc(npf_conndb_t *cd, npf_conn_t *con)
{
	/* FIXME: Dequeue from the current list. */

	/* Insert into the G/C list. */
	con->c_next = cd->cd_gclist;
	cd->cd_gclist = con;
}

/*
 * npf_conndb_gc: garbage collect the expired connections.
 *
 * => Must run in a single-threaded manner.
 * => If 'flush' is true, then destroy all connections.
 * => If 'sync' is true, then perform passive serialisation.
 */
void
npf_conndb_gc(npf_t *npf, npf_conndb_t *cd, bool flush, bool sync)
{
	unsigned target = NPF_GC_STEP;
	struct timespec tsnow;
	void *gcref;

	getnanouptime(&tsnow);

	/*
	 * Scan the connections:
	 * - Limit the scan to the G/C step size.
	 * - Stop if we scanned all of them.
	 */
	mutex_exit(&npf->conn_lock);
	npf_conndb_update(cd);
	while (target--) {
		npf_conn_t *con = npf_conndb_getnext(cd);

		/* Can we G/C this connection? */
		if (flush || npf_conn_gc(con, tsnow.tv_sec)) {
			/* Yes: move to the G/C list. */
			npf_conndb_togc(cd, con);
		}
	}
	mutex_exit(&npf->conn_lock);

	/*
	 * Ensure it is safe to destroy the connections.
	 * Note: drop the conn_lock (see the lock order).
	 */
	gcref = thmap_stage_gc(cd->cd_map);
	if (sync) {
		npf_config_enter(npf);
		npf_config_sync(npf);
		npf_config_exit(npf);
	}
	thmap_gc(cd->cd_map, gcref);

	/*
	 * If there is nothing to G/C, then reduce the worker interval.
	 * We do not go below the lower watermark.
	 */
	if (!cd->cd_gclist) {
		// TODO: npf->next_gc = MAX(npf->next_gc >> 1, NPF_MIN_GC_TIME);
		return;
	}

	/*
	 * Garbage collect all expired connections.
	 * May need to wait for the references to drain.
	 */
	while ((con = cd->cd_gclist) != NULL) {
		/*
		 * Destroy only if removed and no references.  Otherwise,
		 * just do it next time, unless we are destroying all.
		 */
		if (__predict_false(con->c_refcnt)) {
			if (!flush) {
				break;
			}
			kpause("npfcongc", false, 1, NULL);
			continue;
		}
		cd->cd_gclist = con->c_next;
		npf_conn_destroy(npf, con);
	}
}
