/*
 * Copyright (c) 2014 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

/*
 * Thread-local storage - a wrapper around the system interface.  See
 * tls.c source file for details.  This is as an alternative based on
 * the TLS support provided by the pthread(3) API.
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "tls.h"
#include "cext.h"

struct tls_key {
	pthread_key_t	key;
	unsigned	size;
};

tls_key_t *
tls_create(size_t size)
{
	tls_key_t *tk;

	if ((tk = zalloc(sizeof(tls_key_t))) == NULL) {
		return NULL;
	}
	if (pthread_key_create(&tk->key, free) != 0) {
		free(tk);
		return NULL;
	}
	ASSERT(size > 0);
	tk->size = size;
	return tk;
}

void *
tls_get(tls_key_t *tk)
{
	void *obj;

	obj = pthread_getspecific(tk->key);
	if (__predict_false(obj == NULL)) {
		obj = zalloc(tk->size);
		pthread_setspecific(tk->key, obj);
	}
	return obj;
}

void
tls_destroy(tls_key_t *tk)
{
	pthread_key_delete(tk->key);
	free(tk);
}
