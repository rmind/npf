/*
 * Copyright (c) 2014 Mindaugas Rasiukevicius <rmind at netbsd org>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

/*
 * Thread-local storage - a wrapper around the system interface.  This
 * can be removed once there is wide enough support for C11 TLS (tss_t).
 *
 * If the system has native a TLS support (__HAVE_NATIVE_TLS is defined),
 * then we use a fixed-size (MAX_TLS_SPACE) TLS space and provide an
 * allocator around it.  Currently, freeing of the space is not supported,
 * but we have a limited number of TLS uses in the application.
 */

#include <stdlib.h>

#include "tls.h"
#include "cext.h"

#define	MAX_TLS_SPACE	(2048)

static __thread uint8_t	tls_space[MAX_TLS_SPACE]; /* zeroed */
static unsigned		tls_used;

struct tls_key {
	unsigned	offset;
	unsigned	size;
};

tls_key_t *
tls_create(size_t size)
{
	unsigned off, noff;
	tls_key_t *tk;

	if ((tk = zalloc(sizeof(tls_key_t))) == NULL) {
		return NULL;
	}

	do {
		const unsigned abytes = sizeof(long) - 1;

		/* Ensure the next offset is long-word aligned. */
		off = tls_used;
		noff = ((uintptr_t)(off + size) + abytes) & ~abytes;

		/* Is it past the free space? */
		if (noff > sizeof(tls_space)) {
			free(tk);
			return NULL;
		}
	} while (!atomic_compare_exchange_weak(&tls_used, off, noff));

	tk->offset = off;
	tk->size = size;
	return tk;
}

void *
tls_get(tls_key_t *tk)
{
	ASSERT(tk->offset + tk->size <= sizeof(tls_space));
	return &tls_space[tk->offset];
}

void
tls_destroy(tls_key_t *tk)
{
	/* Deallocation is not supported yet (no real need for now). */
	free(tk);
}
