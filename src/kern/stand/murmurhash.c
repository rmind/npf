/*	$NetBSD: murmurhash.c,v 1.6 2013/10/26 21:06:38 rmind Exp $	*/

/*
 * MurmurHash2 -- from the original code:
 *
 * "MurmurHash2 was written by Austin Appleby, and is placed in the public
 * domain. The author hereby disclaims copyright to this source code."
 *
 * References:
 *	http://code.google.com/p/smhasher/
 *	https://sites.google.com/site/murmurhash/
 */

#include <sys/cdefs.h>
#include <sys/types.h>
#include <endian.h>

#include "npf_stand.h"

#ifndef ALIGNED_POINTER
#define	ALIGNED_POINTER(p,t)	((((uintptr_t)(p)) & (sizeof(t) - 1)) == 0)
#endif

uint32_t
murmurhash2(const void *key, size_t len, uint32_t seed)
{
	/*
	 * Note: 'm' and 'r' are mixing constants generated offline.
	 * They're not really 'magic', they just happen to work well.
	 * Initialize the hash to a 'random' value.
	 */
	const uint32_t m = 0x5bd1e995;
	const int r = 24;

	const uint8_t *data = key;
	uint32_t h = seed ^ (uint32_t)len;

	if (__predict_true(ALIGNED_POINTER(key, uint32_t))) {
		while (len >= sizeof(uint32_t)) {
			uint32_t k = *(const uint32_t *)(const void *)data;

			k = htole32(k);

			k *= m;
			k ^= k >> r;
			k *= m;

			h *= m;
			h ^= k;

			data += sizeof(uint32_t);
			len -= sizeof(uint32_t);
		}
	} else {
		while (len >= sizeof(uint32_t)) {
			uint32_t k;

			k  = data[0];
			k |= data[1] << 8;
			k |= data[2] << 16;
			k |= data[3] << 24;

			k *= m;
			k ^= k >> r;
			k *= m;

			h *= m;
			h ^= k;

			data += sizeof(uint32_t);
			len -= sizeof(uint32_t);
		}
	}

	/* Handle the last few bytes of the input array. */
	switch (len) {
	case 3:
		h ^= data[2] << 16;
		/* FALLTHROUGH */
	case 2:
		h ^= data[1] << 8;
		/* FALLTHROUGH */
	case 1:
		h ^= data[0];
		h *= m;
	}

	/*
	 * Do a few final mixes of the hash to ensure the last few
	 * bytes are well-incorporated.
	 */
	h ^= h >> 13;
	h *= m;
	h ^= h >> 15;

	return h;
}
