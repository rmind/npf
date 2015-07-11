/*
 * Copyright (c) 2014 Mindaugas Rasiukevicius <rmind at netbsd org>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

/*
 * mempool: per-thread memory pool for fixed-size object allocation
 * with reservation mechanism.
 *
 * Per-thread memory reservations can be performed using mempool_ensure().
 * Successful memory reservation *guarantees* that subsequent calls to
 * mempool_alloc() from the same thread will not fail.  Therefore, such
 * mechanism provides deterministic memory allocation in a critical path.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <string.h>

#include "mempool.h"
#include "tls.h"
#include "cext.h"

struct mempool {
	size_t		objsize;
	unsigned	ncache;
	tls_key_t *	tls_key;
	const mempool_ops_t *ops;
};

typedef struct {
	unsigned	nitems;
	void **		objptr;
} mempool_tls_t;

static void		mempool_def_free(void *, size_t);

static const mempool_ops_t mempool_defops = {
	.alloc	= malloc,
	.free	= mempool_def_free
};

/*
 * mempool_create: create a memory pool given the object size and using
 * the specified operations.
 *
 * => If ops is NULL, then malloc/free are used.
 * => The number of items to cache is per-thread.
 */
mempool_t *
mempool_create(const mempool_ops_t *ops, size_t objsize, unsigned ncache)
{
	mempool_t *mp;

	if ((mp = zalloc(sizeof(mempool_t))) == NULL) {
		return NULL;
	}
	mp->tls_key = tls_create(sizeof(mempool_tls_t));
	if (mp->tls_key == NULL) {
		free(mp);
		return NULL;
	}

	mp->ops = ops ? ops : &mempool_defops;
	mp->objsize = objsize;
	mp->ncache = ncache;
	return mp;
}

/*
 * mempool_destroy: destroy the memory pool; each thread which used
 * the pool must have exited at this point.
 */
void
mempool_destroy(mempool_t *mp)
{
	if (!mp)
		return;
	tls_destroy(mp->tls_key);
	free(mp);
}

/*
 * Helpers:
 * - mempool_def_free: wrapper around free(3).
 * - mempool_get_local: get TLS and allocate object cache if needed.
 */

static void
mempool_def_free(void *obj, size_t __unused size)
{
	free(obj);
}

static inline mempool_tls_t *
mempool_get_local(mempool_t *mp)
{
	mempool_tls_t *tp;

	tp = tls_get(mp->tls_key);
	if (__predict_false(tp == NULL)) {
		return NULL;
	}
	if (__predict_false(tp->objptr == NULL)) {
		tp->objptr = zalloc(mp->ncache * sizeof(void *));
		if (tp->objptr == NULL) {
			return NULL;
		}
	}
	return tp;
}

/*
 * mempool_alloc: consume an object from the per-thread reserved memory
 * or allocate from the memory pool.
 *
 * => If 'resonly' is true, then only the reserved memory will be used.
 * => Returns the object on success or NULL on failure.
 */
void *
mempool_alloc(mempool_t *mp, mempool_opt_t opt)
{
	mempool_tls_t *tp;
	unsigned nitems;

	tp = mempool_get_local(mp);
	if (__predict_false(tp == NULL)) {
		return NULL;
	}
	nitems = tp->nitems;
	if (__predict_true(nitems)) {
		tp->nitems = --nitems;
		return tp->objptr[nitems];
	}
	if (opt == MEMP_RESERVED) {
		return NULL;
	}
	return mp->ops->alloc(mp->objsize);
}

/*
 * mempool_free: release the object (move back to the pool or free).
 */
void
mempool_free(mempool_t *mp, void *obj)
{
	mempool_tls_t *tp;
	unsigned nitems;

	tp = mempool_get_local(mp);
#ifdef DEBUG
	/* Diagnostics: check for the double-free and fill the magic. */
	if (tp) for (unsigned i = 0; i < tp->nitems; i++) {
		ASSERT(tp->objptr[i] != obj);
	}
	memset(obj, 0xa5, mp->objsize);
#endif
	if (tp && (nitems = tp->nitems) < mp->ncache) {
		tp->objptr[nitems] = obj;
		tp->nitems = ++nitems;
		return;
	}
	mp->ops->free(obj, mp->objsize);
}

/*
 * mempool_ensure: ensure the given number of objects in the per-thread
 * reserve.  Returns true on success or false on allocation failure.
 */
bool
mempool_ensure(mempool_t *mp, size_t count)
{
	mempool_tls_t *tp;
	unsigned reserved;

	tp = mempool_get_local(mp);
	if (__predict_false(tp == NULL)) {
		return NULL;
	}
	if (count > mp->ncache) {
		return false;
	}

	reserved = tp->nitems;
	if (__predict_false(count > reserved)) {
		const size_t len = mp->objsize;
		unsigned n = count - reserved;
		void *obj;

		while (n--) {
			if ((obj = mp->ops->alloc(len)) == NULL) {
				return false;
			}
			tp->objptr[reserved++] = obj;
		}
		tp->nitems = reserved;
	}
	return true;
}

/*
 * mempool_cancel: cancel any per-thread memory reservations i.e.
 * release (free) the reserved objects.
 */
void
mempool_cancel(mempool_t *mp)
{
	const size_t len = mp->objsize;
	mempool_tls_t *tp;
	unsigned n;

	tp = tls_get(mp->tls_key);
	if (tp == NULL) {
		return;
	}
	n = tp->nitems;
	while (n--) {
		void *obj = tp->objptr[n];
		mp->ops->free(obj, len);
	}
	tp->nitems = 0;
}
