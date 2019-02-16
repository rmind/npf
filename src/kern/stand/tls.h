/*
 * Copyright (c) 2014 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#ifndef	_TLS_H_
#define	_TLS_H_

typedef struct tls_key tls_key_t;

tls_key_t *	tls_create(size_t);
void *		tls_get(tls_key_t *);
void		tls_destroy(tls_key_t *);

#endif
