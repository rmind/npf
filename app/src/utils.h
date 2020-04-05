/*
 * Copyright (c) 2020 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#ifndef _UTILS_H_
#define _UTILS_H_

#include <assert.h>

/*
 * A regular assert (debug/diagnostic only).
 */

#if defined(DEBUG)
#define	ASSERT		assert
#else
#define	ASSERT(x)
#endif

/*
 * Branch prediction macros.
 */

#ifndef __predict_true
#define	__predict_true(x)	__builtin_expect((x) != 0, 1)
#define	__predict_false(x)	__builtin_expect((x) != 0, 0)
#endif

/*
 * Various C helpers and attribute macros.
 */

#ifndef __unused
#define	__unused		__attribute__((__unused__))
#endif

#ifndef __arraycount
#define	__arraycount(__x)	(sizeof(__x) / sizeof(__x[0]))
#endif

void	dump_eth_addr(const char *, const struct ether_addr *);
void	dump_ip4_addr(const char *, int, const void *);
void	dump_pkt(char, const struct rte_mbuf *);

#endif
