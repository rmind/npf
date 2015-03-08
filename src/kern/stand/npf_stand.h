/*-
 * Copyright (c) 2015 The NetBSD Foundation, Inc.
 * All rights reserved.
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

#ifndef _NPF_STAND_H_
#define _NPF_STAND_H_

/*
 * This file contains wrappers of the kernel interfaces for the
 * standalone version of NPF.  These wrappers use inteded to be portable,
 * using the standard C99 or POSIX interfaces.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <inttypes.h>
#include <limits.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>
#include <assert.h>

#define	__FAVOR_BSD
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#if defined(__linux__)
#include <net/ethernet.h>
#endif

#include <prop/proplib.h>
#include <cdbr.h>

#include "cext.h"

#include "ptree.h"
#include "rbtree.h"
#include "bpf.h"

/*
 * Synchronisation primitives (mutex, condvar, etc).
 */

#define	kmutex_t		pthread_mutex_t
#define	mutex_init(l, t, i)	pthread_mutex_init(l, NULL)
#define	mutex_enter(l)		pthread_mutex_lock(l)
#define	mutex_exit(l)		pthread_mutex_unlock(l)
#define	mutex_owned(l)		true
#define	mutex_destroy(l)	pthread_mutex_destroy(l)

#define	RW_READER		0
#define	RW_WRITER		1

#define	krwlock_t		pthread_rwlock_t
#define	rw_init(l)		pthread_rwlock_init(l, NULL)
#define	rw_destroy(l)		pthread_rwlock_destroy(l)
#define	rw_enter(l, op)		\
    (op) == RW_READER ? pthread_rwlock_rdlock(l) : pthread_rwlock_wrlock(l)
#define	rw_exit(l)		pthread_rwlock_unlock(l)

static inline int
npfkern_pthread_cond_timedwait(pthread_cond_t *t, pthread_mutex_t *l,
    const unsigned msec)
{
	const unsigned sec = msec / 1000;
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	ts.tv_sec += sec;
	ts.tv_nsec = (msec - (sec * 1000)) * 1000000;
	return pthread_cond_timedwait(t, l, &ts);
}

#define	kcondvar_t		pthread_cond_t
#define	cv_init(c, w)		pthread_cond_init(c, NULL)
#define	cv_broadcast(c)		pthread_cond_broadcast(c)
#define	cv_wait(c, l)		pthread_cond_wait(c, l)
#define	cv_timedwait(c, l, t)	npfkern_pthread_cond_timedwait(c, l, t)
#define	cv_signal(c)		pthread_cond_signal(c)
#define	cv_destroy(c)		pthread_cond_destroy(c)

/*
 * FIXME/TODO: To be implemented ..
 */
typedef int pserialize_t;

#define	pserialize_create()	0
#define	pserialize_destroy(p)
#define	pserialize_perform(p)
#define	pserialize_read_enter()	0x50505050
#define	pserialize_read_exit(s)	assert(s == 0x5050505)

/*
 * Atomic operations and memory barriers.
 */

static inline void *
npfkern_atomic_swap_ptr(volatile void *ptr, void *nval)
{
	/* Solaris/NetBSD API uses *ptr, but it represents **ptr. */
	void * volatile *ptrp = (void * volatile *)ptr;
	volatile void *ptrval;
	void *oldval;

	do {
		ptrval = *ptrp;
		oldval = __sync_val_compare_and_swap(ptrp, *ptrp, nval);
	} while (oldval != ptrval);

	return oldval;
}

#define	membar_sync()		__sync_synchronize()
#define	atomic_inc_uint(x)	__sync_fetch_and_add(x, 1)
#define	atomic_dec_uint(x)	__sync_sub_and_fetch(x, 1)
#define	atomic_dec_uint_nv(x)	__sync_sub_and_fetch(x, 1)
#define	atomic_or_uint(x, v)	__sync_fetch_and_or(x, v)
#define	atomic_cas_32(p, o, n)	__sync_val_compare_and_swap(p, o, n)
#define	atomic_cas_ptr(p, o, n)	__sync_val_compare_and_swap(p, o, n)
#define atomic_swap_ptr(x, y)	npfkern_atomic_swap_ptr(x, y)

/*
 * Threads.
 */

typedef struct { pthread_t thr; } lwp_t;

static inline int
npfkern_pthread_create(lwp_t **lret, void (*func)(void *), void *arg)
{
	lwp_t *l = calloc(1, sizeof(lwp_t));
	*lret = l;
	return pthread_create(&l->thr, NULL, (void *(*)(void *))func, arg);
}

#define	kthread_create(pri, flags, ci, func, arg, thr, fmt) \
    npfkern_pthread_create(thr, func, arg)
#define	kthread_join(t)		{ void *__r; pthread_join((t)->thr, &__r); }
#define	kthread_exit(x)		pthread_exit(NULL);

/*
 * Memory allocators and management.
 */

#define	KM_SLEEP	0x00000001
#define	KM_NOSLEEP	0x00000002
#define	PR_WAITOK	KM_SLEEP
#define PR_NOWAIT	KM_NOSLEEP

/*
 * TODO: To be converted to use memory pool ..
 */
struct pool_cache {
	size_t		obj_size;
};

#ifndef pool_cache_t
typedef struct pool_cache *pool_cache_t;
#endif

static inline pool_cache_t
npfkern_pool_cache_init(size_t size)
{
	pool_cache_t p = calloc(1, sizeof(struct pool_cache));
	p->obj_size = size;
	return p;
}

#define	pool_cache_init(size, align, a, b, c, d, p, e, f, g) \
    npfkern_pool_cache_init(size)
#define	pool_cache_destroy(p)		free(p)
#define	pool_cache_get(p, flags)	malloc((p)->obj_size)
#define	pool_cache_put(p, obj)		free(obj)
#define	pool_cache_invalidate(p)

#define	kmem_zalloc(len, flags)		calloc(1, len)
#define	kmem_alloc(len, flags)		malloc(len)
#define	kmem_free(ptr, len)		free(ptr)
#define	kmem_intr_zalloc(len, flags)	kmem_zalloc(len, flags)
#define	kmem_intr_free(ptr, len)	kmem_free(ptr, len)

#define	kmalloc(size, type, flags)	calloc(1, (size))
#define	kfree(ptr, type)		free(ptr)

/*
 * FIXME/TODO: To be implemented using TLS ..
 */
#define	percpu_t			void
#define	percpu_alloc(s)			calloc(1, s)
#define	percpu_free(p, s)		free(p)
#define	percpu_getref(p)		p
#define	percpu_putref(p)
#define	percpu_foreach(p, f, b)		f(p, b, NULL)

static inline int
npfkern_copy(void *dst, const void *src, size_t len)
{
	memcpy(dst, src, len);
	return 0;
}
#define	copyout(k, u, l)		npfkern_copy(u, k, l)
#define	copyin(u, k, l)			npfkern_copy(k, u, l)
#define	copyinstr(u, k, l, d)		(0 & (int)(uintptr_t)strncpy(k, u, l))

/*
 * Random number generator.
 */

#define	cprng_fast32()		((uint32_t)random())

/*
 * Hashing.
 */

enum hashtype { HASH_LIST, HASH_SLIST, HASH_TAILQ };

void *		hashinit(u_int, enum hashtype, bool, u_long *);
void		hashdone(void *, enum hashtype, u_long);
uint32_t	murmurhash2(const void *, size_t, uint32_t);

/*
 * Time operations.
 */

#define	getnanouptime(ts)	clock_gettime(CLOCK_MONOTONIC, (ts))
#undef	mstohz
#define	mstohz(ms)		(ms)
#define	hz			1

static inline int
npfkern_kpause(const char *wmesg, bool intr, int timo, kmutex_t *mtx)
{
	const struct timespec req = { .tv_sec = 0, .tv_nsec = timo * 1000000 };

	(void)wmesg; (void)intr; (void)mtx;
	return nanosleep(&req, NULL);
}

#define	kpause(w, s, t, l)	npfkern_kpause(w, s, t, l)

#ifndef timespecsub
#define	timespecsub(tsp, usp, vsp)					\
	do {								\
		(vsp)->tv_sec = (tsp)->tv_sec - (usp)->tv_sec;		\
		(vsp)->tv_nsec = (tsp)->tv_nsec - (usp)->tv_nsec;	\
		if ((vsp)->tv_nsec < 0) {				\
			(vsp)->tv_sec--;				\
			(vsp)->tv_nsec += 1000000000L;			\
		}							\
	} while (/* CONSTCOND */ 0)
#endif

/*
 * Networking.
 */

#ifndef IPV6_VERSION
#define IPV6_VERSION	0x60
#endif
#ifndef IPV6_DEFHLIM
#define IPV6_DEFHLIM	64
#endif

#define PFIL_IN		0x00000001
#define PFIL_OUT	0x00000002
#define PFIL_ALL	(PFIL_IN|PFIL_OUT)
#define PFIL_IFADDR	0x00000008
#define PFIL_IFNET	0x00000010

#define	pfil_head_t	void

#define	MAX_TCPOPTLEN	40

/*
 * FIXME/TODO: To be implemented ..
 */
#define	if_alloc(x)	calloc(1, sizeof(ifnet_t))
#define	if_alloc_sadl(x)
#define	if_attach(x)
#define	ifunit(name)	NULL
#define	DLT_NULL	0
#define	IFNET_FOREACH(ifp)	if (0)

#define	IFNAMSIZ	8

typedef struct {
	char		if_xname[IFNAMSIZ];
	unsigned	if_index;
	unsigned	if_dlt;
	void *		if_pf_kif;
	void *		next;
} ifnet_t;

#define	ip_reass_packet(p, h)		ENOTSUP
#define	ip_output(m, a, b, c, d, e)	ENOTSUP
#define	icmp_error(m, t, c, a, b)
#define	in_cksum(m, len)	0

#define	ip_defttl		64
#define	max_linkhdr		0

#define	MLEN			512

struct mbuf {
	unsigned	m_flags;
	int		m_type;
	unsigned	m_len;
	void *		m_next;
	struct {
		int	len;
	} m_pkthdr;
	char *		m_data;
	char		m_data0[MLEN];
};

#define	MT_FREE			0
#define	M_UNWRITABLE(m, l)	false
#define	M_NOWAIT		0x00001
#define M_PKTHDR		0x00002
#ifndef M_CANFASTFWD
#define	M_CANFASTFWD		0x00400
#endif
#define	M_CSUM_TCPv4		0x1
#define	M_CSUM_UDPv4		0x2

static inline struct mbuf *
npfkern_m_get(int flags)
{
	struct mbuf *m;
	m = calloc(1, sizeof(struct mbuf));
	m->m_type = 1;
	m->m_flags = flags;
	m->m_data = m->m_data0;
	return m;
}

static inline unsigned
npfkern_m_length(const struct mbuf *m)
{
	const struct mbuf *m0;
	unsigned pktlen = 0;

	if ((m->m_flags & M_PKTHDR) != 0)
		return m->m_pkthdr.len;
	for (m0 = m; m0 != NULL; m0 = m0->m_next)
		pktlen += m0->m_len;
	return pktlen;
}

static inline void
npfkern_m_freem(struct mbuf *m)
{
	struct mbuf *n;

	do {
		n = m->m_next;
		m->m_type = MT_FREE;
		free(m);
		m = n;
	} while (m);
}

#define	m_gethdr(x, y)		npfkern_m_get(M_PKTHDR)
#define	m_get(x, y)		npfkern_m_get(0)
#define	m_freem(m)		npfkern_m_freem(m)
#define	m_length(m)		npfkern_m_length(m)
#define	m_makewritable(a,b,c,d)	false
#define	m_ensure_contig(m, l)	false
#define	mtod(m, t)		((t)((m)->m_data))

/*
 * Misc.
 */

#ifndef COHERENCY_UNIT
#define	COHERENCY_UNIT		CACHE_LINE_SIZE
#endif

#define	__read_mostly
#define	__cacheline_aligned
#define	__diagused
#ifndef	__dead
#define	__dead
#endif

#define	KASSERT			assert
#define	KASSERTMSG(e, m, ...)	assert(e)
#define	panic(x)		abort()

#define	KERNEL_LOCK(a, b)
#define	KERNEL_UNLOCK_ONE(a)

#define	MODULE(c, m, d)
#define	module_autoload(n, c)	ENOTSUP

#define	MODULE_CMD_INIT		1
#define	MODULE_CMD_FINI		2
#define	MODULE_CMD_AUTOLOAD	3
#define	MODULE_CMD_AUTOUNLOAD	4

typedef int modcmd_t;

#define	kauth_authorize_network(c, a, r, a1, a2, a3)	0

#ifndef EPROGMISMATCH
#define	EPROGMISMATCH		ENOTSUP
#endif

#ifndef HASH32_BUF_INIT
#define	HASH32_BUF_INIT		5381
#define	hash32_buf(b, l, s)	murmurhash2(b, l, s)
#endif

#define	ffs32(x)		ffs(x)
#define	fls32(x)		flsl((unsigned long)(x))

struct cpu_info { unsigned id; };

#ifdef __linux__
static inline size_t
strlcpy(char *dst, const char *src, size_t len)
{
	char *p = stpncpy(dst, src, len);
	dst[len - 1] = '\0';
	return strlen(src);
}
#endif

#endif
