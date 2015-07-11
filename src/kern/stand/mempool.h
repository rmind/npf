/*
 * Copyright (c) 2014 Mindaugas Rasiukevicius <rmind at netbsd org>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#ifndef	_MEMPOOL_H_
#define	_MEMPOOL_H_

#include <stdlib.h>
#include <stdbool.h>

struct mempool;
typedef struct mempool mempool_t;

typedef enum {
	MEMP_WAITOK,
	MEMP_RESERVED
} mempool_opt_t;

typedef struct {
	void *	(*alloc)(size_t);
	void	(*free)(void *, size_t);
} mempool_ops_t;

mempool_t *	mempool_create(const mempool_ops_t *, size_t, unsigned);
void *		mempool_alloc(mempool_t *, mempool_opt_t);
void		mempool_free(mempool_t *, void *);
bool		mempool_ensure(mempool_t *, size_t);
void		mempool_cancel(mempool_t *);
void		mempool_destroy(mempool_t *);

#endif
