/*-
 * Copyright (c) 2009-2018 The NetBSD Foundation, Inc.
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
 * NPF tableset module.
 *
 * Notes
 *
 *	The tableset is an array of tables.  After the creation, the array
 *	is immutable.  The caller is responsible to synchronise the access
 *	to the tableset.
 */

#ifdef _KERNEL
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: npf_tableset.c,v 1.28 2018/09/29 14:41:36 rmind Exp $");

#include <sys/param.h>
#include <sys/types.h>

#include <sys/atomic.h>
#include <sys/cdbr.h>
#include <sys/kmem.h>
#include <sys/pool.h>
#include <sys/queue.h>
#include <sys/mutex.h>
#include <sys/thmap.h>

#include "lpm.h"
#endif

#include "npf_impl.h"

typedef struct npf_tblent {
	LIST_ENTRY(npf_tblent)	te_listent;
	uint16_t		te_preflen;
	uint16_t		te_alen;
	npf_addr_t		te_addr;
} npf_tblent_t;

struct npf_table {
	/*
	 * The storage type can be: a) hashmap b) LPM c) cdb.
	 * There are separate trees for IPv4 and IPv6.
	 */
	union {
		thmap_t *		t_map;
		lpm_t *			t_lpm;
		struct {
			void *		t_blob;
			size_t		t_bsize;
			struct cdbr *	t_cdb;
		};
		struct {
			npf_tblent_t **	t_elements;
			size_t		t_allocated;
		};
	} /* C11 */;
	LIST_HEAD(, npf_tblent)		t_list;
	unsigned			t_nitems;

	/*
	 * Table ID, type and lock.  The ID may change during the
	 * config reload, it is protected by the npf_config_lock.
	 */
	int			t_type;
	unsigned		t_id;
	kmutex_t		t_lock;

	/* Reference count and table name. */
	unsigned		t_refcnt;
	char			t_name[NPF_TABLE_MAXNAMELEN];
};

struct npf_tableset {
	u_int			ts_nitems;
	npf_table_t *		ts_map[];
};

#define	NPF_TABLESET_SIZE(n)	\
    (offsetof(npf_tableset_t, ts_map[n]) * sizeof(npf_table_t *))

#define	NPF_ADDRLEN2IDX(alen)	((alen) >> 4)

#define	NPF_IFADDR_STEP		4

static pool_cache_t		tblent_cache	__read_mostly;

/*
 * npf_table_sysinit: initialise tableset structures.
 */
void
npf_tableset_sysinit(void)
{
	tblent_cache = pool_cache_init(sizeof(npf_tblent_t), coherency_unit,
	    0, 0, "npftblpl", NULL, IPL_NONE, NULL, NULL, NULL);
}

void
npf_tableset_sysfini(void)
{
	pool_cache_destroy(tblent_cache);
}

npf_tableset_t *
npf_tableset_create(u_int nitems)
{
	npf_tableset_t *ts = kmem_zalloc(NPF_TABLESET_SIZE(nitems), KM_SLEEP);
	ts->ts_nitems = nitems;
	return ts;
}

void
npf_tableset_destroy(npf_tableset_t *ts)
{
	/*
	 * Destroy all tables (no references should be held, since the
	 * ruleset should be destroyed before).
	 */
	for (u_int tid = 0; tid < ts->ts_nitems; tid++) {
		npf_table_t *t = ts->ts_map[tid];

		if (t && atomic_dec_uint_nv(&t->t_refcnt) == 0) {
			npf_table_destroy(t);
		}
	}
	kmem_free(ts, NPF_TABLESET_SIZE(ts->ts_nitems));
}

/*
 * npf_tableset_insert: insert the table into the specified tableset.
 *
 * => Returns 0 on success.  Fails and returns error if ID is already used.
 */
int
npf_tableset_insert(npf_tableset_t *ts, npf_table_t *t)
{
	const u_int tid = t->t_id;
	int error;

	KASSERT((u_int)tid < ts->ts_nitems);

	if (ts->ts_map[tid] == NULL) {
		atomic_inc_uint(&t->t_refcnt);
		ts->ts_map[tid] = t;
		error = 0;
	} else {
		error = EEXIST;
	}
	return error;
}

npf_table_t *
npf_tableset_swap(npf_tableset_t *ts, npf_table_t *newt)
{
	const u_int tid = newt->t_id;
	npf_table_t *oldt = ts->ts_map[tid];

	KASSERT(tid < ts->ts_nitems);
	KASSERT(oldt->t_id == newt->t_id);

	newt->t_refcnt = oldt->t_refcnt;
	oldt->t_refcnt = 0;

	return atomic_swap_ptr(&ts->ts_map[tid], newt);
}

/*
 * npf_tableset_getbyname: look for a table in the set given the name.
 */
npf_table_t *
npf_tableset_getbyname(npf_tableset_t *ts, const char *name)
{
	npf_table_t *t;

	for (u_int tid = 0; tid < ts->ts_nitems; tid++) {
		if ((t = ts->ts_map[tid]) == NULL)
			continue;
		if (strcmp(name, t->t_name) == 0)
			return t;
	}
	return NULL;
}

npf_table_t *
npf_tableset_getbyid(npf_tableset_t *ts, u_int tid)
{
	if (__predict_true(tid < ts->ts_nitems)) {
		return ts->ts_map[tid];
	}
	return NULL;
}

/*
 * npf_tableset_reload: iterate all tables and if the new table is of the
 * same type and has no items, then we preserve the old one and its entries.
 *
 * => The caller is responsible for providing synchronisation.
 */
void
npf_tableset_reload(npf_t *npf, npf_tableset_t *nts, npf_tableset_t *ots)
{
	for (u_int tid = 0; tid < nts->ts_nitems; tid++) {
		npf_table_t *t, *ot;

		if ((t = nts->ts_map[tid]) == NULL) {
			continue;
		}

		/* If our table has entries, just load it. */
		if (t->t_nitems) {
			continue;
		}

		/* Look for a currently existing table with such name. */
		ot = npf_tableset_getbyname(ots, t->t_name);
		if (ot == NULL) {
			/* Not found: we have a new table. */
			continue;
		}

		/* Found.  Did the type change? */
		if (t->t_type != ot->t_type) {
			/* Yes, load the new. */
			continue;
		}

		/*
		 * Preserve the current table.  Acquire a reference since
		 * we are keeping it in the old table set.  Update its ID.
		 */
		atomic_inc_uint(&ot->t_refcnt);
		nts->ts_map[tid] = ot;

		KASSERT(npf_config_locked_p(npf));
		ot->t_id = tid;

		/* Destroy the new table (we hold the only reference). */
		t->t_refcnt--;
		npf_table_destroy(t);
	}
}

int
npf_tableset_export(npf_t *npf, const npf_tableset_t *ts, nvlist_t *npf_dict)
{
	const npf_table_t *t;

	KASSERT(npf_config_locked_p(npf));

	for (u_int tid = 0; tid < ts->ts_nitems; tid++) {
		nvlist_t *table;

		if ((t = ts->ts_map[tid]) == NULL) {
			continue;
		}
		table = nvlist_create(0);
		nvlist_add_string(table, "name", t->t_name);
		nvlist_add_number(table, "type", t->t_type);
		nvlist_add_number(table, "id", tid);

		nvlist_append_nvlist_array(npf_dict, "tables", table);
		nvlist_destroy(table);
	}
	return 0;
}

/*
 * Few helper routines.
 */

static void
table_ipset_flush(npf_table_t *t)
{
	npf_tblent_t *ent;

	while ((ent = LIST_FIRST(&t->t_list)) != NULL) {
		thmap_del(t->t_map, &ent->te_addr, ent->te_alen);
		LIST_REMOVE(ent, te_listent);
		pool_cache_put(tblent_cache, ent);
	}
	t->t_nitems = 0;
}

static void
table_tree_flush(npf_table_t *t)
{
	npf_tblent_t *ent;

	while ((ent = LIST_FIRST(&t->t_list)) != NULL) {
		LIST_REMOVE(ent, te_listent);
		pool_cache_put(tblent_cache, ent);
	}
	lpm_clear(t->t_lpm, NULL, NULL);
	t->t_nitems = 0;
}

static void
table_ifaddr_flush(npf_table_t *t)
{
	if (!t->t_allocated) {
		KASSERT(t->t_elements == NULL);
		return;
	}
	for (unsigned i = 0; i < t->t_nitems; i++) {
		npf_tblent_t *ent = t->t_elements[i];
		LIST_REMOVE(ent, te_listent);
		pool_cache_put(tblent_cache, ent);
	}
	kmem_free(t->t_elements, t->t_allocated * sizeof(npf_tblent_t *));
	t->t_elements = NULL;
	t->t_allocated = 0;
	t->t_nitems = 0;
}

/*
 * npf_table_create: create table with a specified ID.
 */
npf_table_t *
npf_table_create(const char *name, u_int tid, int type,
    const void *blob, size_t size)
{
	npf_table_t *t;

	t = kmem_zalloc(sizeof(npf_table_t), KM_SLEEP);
	strlcpy(t->t_name, name, NPF_TABLE_MAXNAMELEN);

	switch (type) {
	case NPF_TABLE_LPM:
		if ((t->t_lpm = lpm_create()) == NULL) {
			goto out;
		}
		LIST_INIT(&t->t_list);
		break;
	case NPF_TABLE_IPSET:
		t->t_map = thmap_create(0, NULL, THMAP_NOCOPY);
		if (t->t_map == NULL) {
			goto out;
		}
		break;
	case NPF_TABLE_CONST:
		t->t_blob = kmem_alloc(size, KM_SLEEP);
		if (t->t_blob == NULL) {
			goto out;
		}
		memcpy(t->t_blob, blob, size);
		t->t_bsize = size;

		t->t_cdb = cdbr_open_mem(t->t_blob, size,
		    CDBR_DEFAULT, NULL, NULL);
		if (t->t_cdb == NULL) {
			kmem_free(t->t_blob, t->t_bsize);
			goto out;
		}
		t->t_nitems = cdbr_entries(t->t_cdb);
		break;
	case NPF_TABLE_IFADDR:
		break;
	default:
		KASSERT(false);
	}
	mutex_init(&t->t_lock, MUTEX_DEFAULT, IPL_NONE);
	t->t_type = type;
	t->t_id = tid;
	return t;
out:
	kmem_free(t, sizeof(npf_table_t));
	return NULL;
}

/*
 * npf_table_destroy: free all table entries and table itself.
 */
void
npf_table_destroy(npf_table_t *t)
{
	KASSERT(t->t_refcnt == 0);

	switch (t->t_type) {
	case NPF_TABLE_IPSET:
		table_ipset_flush(t);
		thmap_destroy(t->t_map);
		break;
	case NPF_TABLE_LPM:
		table_tree_flush(t);
		lpm_destroy(t->t_lpm);
		break;
	case NPF_TABLE_CONST:
		cdbr_close(t->t_cdb);
		kmem_free(t->t_blob, t->t_bsize);
		break;
	case NPF_TABLE_IFADDR:
		table_ifaddr_flush(t);
		break;
	default:
		KASSERT(false);
	}
	mutex_destroy(&t->t_lock);
	kmem_free(t, sizeof(npf_table_t));
}

u_int
npf_table_getid(npf_table_t *t)
{
	return t->t_id;
}

/*
 * npf_table_check: validate the name, ID and type.
 */
int
npf_table_check(npf_tableset_t *ts, const char *name, uint64_t tid, uint64_t type)
{
	if (tid >= ts->ts_nitems) {
		return EINVAL;
	}
	if (ts->ts_map[tid] != NULL) {
		return EEXIST;
	}
	switch (type) {
	case NPF_TABLE_LPM:
	case NPF_TABLE_IPSET:
	case NPF_TABLE_CONST:
		break;
	default:
		return EINVAL;
	}
	if (strlen(name) >= NPF_TABLE_MAXNAMELEN) {
		return ENAMETOOLONG;
	}
	if (npf_tableset_getbyname(ts, name)) {
		return EEXIST;
	}
	return 0;
}

static int
table_cidr_check(const u_int aidx, const npf_addr_t *addr,
    const npf_netmask_t mask)
{
	if (aidx > 1) {
		return EINVAL;
	}
	if (mask > NPF_MAX_NETMASK && mask != NPF_NO_NETMASK) {
		return EINVAL;
	}

	/*
	 * For IPv4 (aidx = 0) - 32 and for IPv6 (aidx = 1) - 128.
	 * If it is a host - shall use NPF_NO_NETMASK.
	 */
	if (mask > (aidx ? 128 : 32) && mask != NPF_NO_NETMASK) {
		return EINVAL;
	}
	return 0;
}

/*
 * npf_table_insert: add an IP CIDR entry into the table.
 */
int
npf_table_insert(npf_table_t *t, const int alen,
    const npf_addr_t *addr, const npf_netmask_t mask)
{
	const u_int aidx = NPF_ADDRLEN2IDX(alen);
	npf_tblent_t *ent;
	int error;

	error = table_cidr_check(aidx, addr, mask);
	if (error) {
		return error;
	}
	ent = pool_cache_get(tblent_cache, PR_WAITOK);
	memcpy(&ent->te_addr, addr, alen);
	ent->te_alen = alen;
	ent->te_preflen = 0;

	/*
	 * Insert the entry.  Return an error on duplicate.
	 */
	mutex_enter(&t->t_lock);
	switch (t->t_type) {
	case NPF_TABLE_IPSET:
		/*
		 * Hashmap supports only IPs.
		 */
		if (mask != NPF_NO_NETMASK) {
			error = EINVAL;
			break;
		}
		if (thmap_put(t->t_map, addr, alen, ent) == ent) {
			LIST_INSERT_HEAD(&t->t_list, ent, te_listent);
			t->t_nitems++;
		} else {
			error = EEXIST;
		}
		break;
	case NPF_TABLE_LPM: {
		const unsigned preflen =
		    (mask == NPF_NO_NETMASK) ? (alen * 8) : mask;
		ent->te_preflen = preflen;

		if (lpm_lookup(t->t_lpm, addr, alen) == NULL &&
		    lpm_insert(t->t_lpm, addr, alen, preflen, ent) == 0) {
			LIST_INSERT_HEAD(&t->t_list, ent, te_listent);
			t->t_nitems++;
			error = 0;
		} else {
			error = EEXIST;
		}
		break;
	}
	case NPF_TABLE_CONST:
		error = EINVAL;
		break;
	case NPF_TABLE_IFADDR:
		/*
		 * No need to check for duplicates.
		 */
		if (t->t_allocated <= t->t_nitems) {
			npf_tblent_t **elements;
			size_t toalloc, newsize;

			toalloc = roundup2(t->t_allocated + 1, NPF_IFADDR_STEP);
			newsize = toalloc * sizeof(npf_tblent_t *);
			elements = kmem_zalloc(newsize, KM_SLEEP);
			for (unsigned i = 0; i < t->t_nitems; i++) {
				elements[i] = t->t_elements[i];
			}
			kmem_free(t->t_elements,
			    t->t_allocated * sizeof(npf_tblent_t *));
			t->t_elements = elements;
			t->t_allocated = toalloc;
		}
		t->t_elements[t->t_nitems] = ent;
		t->t_nitems++;
		break;
	default:
		KASSERT(false);
	}
	mutex_exit(&t->t_lock);

	if (error) {
		pool_cache_put(tblent_cache, ent);
	}
	return error;
}

/*
 * npf_table_remove: remove the IP CIDR entry from the table.
 */
int
npf_table_remove(npf_table_t *t, const int alen,
    const npf_addr_t *addr, const npf_netmask_t mask)
{
	const u_int aidx = NPF_ADDRLEN2IDX(alen);
	npf_tblent_t *ent = NULL;
	int error = ENOENT;

	error = table_cidr_check(aidx, addr, mask);
	if (error) {
		return error;
	}

	mutex_enter(&t->t_lock);
	switch (t->t_type) {
	case NPF_TABLE_IPSET:
		ent = thmap_del(t->t_map, addr, alen);
		if (__predict_true(ent != NULL)) {
			LIST_REMOVE(ent, te_listent);
			t->t_nitems--;
		}
		break;
	case NPF_TABLE_LPM:
		ent = lpm_lookup(t->t_lpm, addr, alen);
		if (__predict_true(ent != NULL)) {
			LIST_REMOVE(ent, te_listent);
			lpm_remove(t->t_lpm, &ent->te_addr,
			    ent->te_alen, ent->te_preflen);
			t->t_nitems--;
		}
		break;
	case NPF_TABLE_CONST:
	case NPF_TABLE_IFADDR:
		error = EINVAL;
		break;
	default:
		KASSERT(false);
		ent = NULL;
	}
	mutex_exit(&t->t_lock);

	if (ent) {
		pool_cache_put(tblent_cache, ent);
	}
	return error;
}

/*
 * npf_table_lookup: find the table according to ID, lookup and match
 * the contents with the specified IP address.
 */
int
npf_table_lookup(npf_table_t *t, const int alen, const npf_addr_t *addr)
{
	const u_int aidx = NPF_ADDRLEN2IDX(alen);
	const void *data;
	size_t dlen;
	bool found;

	if (__predict_false(aidx > 1)) {
		return EINVAL;
	}

	switch (t->t_type) {
	case NPF_TABLE_IPSET:
		found = thmap_get(t->t_map, addr, alen) != NULL;
		break;
	case NPF_TABLE_LPM:
		mutex_enter(&t->t_lock);
		found = lpm_lookup(t->t_lpm, addr, alen) != NULL;
		mutex_exit(&t->t_lock);
		break;
	case NPF_TABLE_CONST:
		if (cdbr_find(t->t_cdb, addr, alen, &data, &dlen) == 0) {
			found = dlen == (u_int)alen &&
			    memcmp(addr, data, dlen) == 0;
		} else {
			found = false;
		}
		break;
	case NPF_TABLE_IFADDR:
		found = false;
		for (unsigned i = 0; i < t->t_nitems; i++) {
			const npf_tblent_t *elm = t->t_elements[i];

			if (elm->te_alen != alen) {
				continue;
			}
			if (memcmp(&elm->te_addr, addr, alen)) {
				continue;
			}
			found = true;
			break;
		}
		break;
	default:
		KASSERT(false);
		found = false;
	}

	return found ? 0 : ENOENT;
}

npf_addr_t *
npf_table_getsome(npf_table_t *t, const int alen, unsigned idx)
{
	npf_tblent_t *elm;

	KASSERT(t->t_type == NPF_TABLE_IFADDR);

	/*
	 * No need to acquire the lock, since the table is immutable.
	 */
	if (t->t_nitems == 0) {
		return NULL;
	}
	elm = t->t_elements[idx % t->t_nitems];
	return &elm->te_addr;
}

static int
table_ent_copyout(const npf_addr_t *addr, const int alen, npf_netmask_t mask,
    void *ubuf, size_t len, size_t *off)
{
	void *ubufp = (uint8_t *)ubuf + *off;
	npf_ioctl_ent_t uent;

	if ((*off += sizeof(npf_ioctl_ent_t)) > len) {
		return ENOMEM;
	}
	uent.alen = alen;
	memcpy(&uent.addr, addr, sizeof(npf_addr_t));
	uent.mask = mask;

	return copyout(&uent, ubufp, sizeof(npf_ioctl_ent_t));
}

static int
table_generic_list(const npf_table_t *t, void *ubuf, size_t len)
{
	npf_tblent_t *ent;
	size_t off = 0;
	int error = 0;

	LIST_FOREACH(ent, &t->t_list, te_listent) {
		error = table_ent_copyout(&ent->te_addr,
		    ent->te_alen, ent->te_preflen, ubuf, len, &off);
		if (error)
			break;
	}
	return error;
}

static int
table_cdb_list(npf_table_t *t, void *ubuf, size_t len)
{
	size_t off = 0, dlen;
	const void *data;
	int error = 0;

	for (size_t i = 0; i < t->t_nitems; i++) {
		if (cdbr_get(t->t_cdb, i, &data, &dlen) != 0) {
			return EINVAL;
		}
		error = table_ent_copyout(data, dlen, 0, ubuf, len, &off);
		if (error)
			break;
	}
	return error;
}

static int
table_ifaddr_list(const npf_table_t *t, void *ubuf, size_t len)
{
	size_t off = 0;
	int error = 0;

	for (unsigned i = 0; i < t->t_nitems; i++) {
		npf_tblent_t *ent = t->t_elements[i];
		error = table_ent_copyout(&ent->te_addr,
		    ent->te_alen, 0, ubuf, len, &off);
		if (error)
			break;
	}
	return error;
}

/*
 * npf_table_list: copy a list of all table entries into a userspace buffer.
 */
int
npf_table_list(npf_table_t *t, void *ubuf, size_t len)
{
	int error = 0;

	mutex_enter(&t->t_lock);
	switch (t->t_type) {
	case NPF_TABLE_IPSET:
		error = table_generic_list(t, ubuf, len);
		break;
	case NPF_TABLE_LPM:
		error = table_generic_list(t, ubuf, len);
		break;
	case NPF_TABLE_CONST:
		error = table_cdb_list(t, ubuf, len);
		break;
	case NPF_TABLE_IFADDR:
		error = table_ifaddr_list(t, ubuf, len);
		break;
	default:
		KASSERT(false);
	}
	mutex_exit(&t->t_lock);

	return error;
}

/*
 * npf_table_flush: remove all table entries.
 */
int
npf_table_flush(npf_table_t *t)
{
	int error = 0;

	mutex_enter(&t->t_lock);
	switch (t->t_type) {
	case NPF_TABLE_IPSET:
		table_ipset_flush(t);
		break;
	case NPF_TABLE_LPM:
		table_tree_flush(t);
		break;
	case NPF_TABLE_CONST:
	case NPF_TABLE_IFADDR:
		error = EINVAL;
		break;
	default:
		KASSERT(false);
	}
	mutex_exit(&t->t_lock);
	return error;
}
