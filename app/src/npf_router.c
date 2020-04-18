/*
 * Copyright (c) 2020 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <err.h>

#include <rte_eal.h>
#include <rte_launch.h>
#include <rte_mempool.h>
#include <rte_malloc.h>
#include <rte_mbuf.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_ip.h>

#include <sys/stat.h>	// XXX load_npf_config
#include <sys/mman.h>	// XXX load_npf_config
#include <fcntl.h>	// XXX load_npf_config
#include <npf.h>	// XXX load_npf_config

#include "npf_router.h"
#include "utils.h"

#define	BURST_SIZE		(256)
#define	NUM_MBUFS		((8 * 1024) - 1)
#define	MBUF_CACHE_SIZE		(256)

static volatile sig_atomic_t	stop = false;

static void	router_destroy(npf_router_t *);
static int	worker_fini(void *);

static void
sighandler(int sig)
{
	(void)sig;
	stop = true;
}

static void
setup_signals(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_handler = sighandler;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
}

static worker_t *
get_worker_ctx(npf_router_t *router)
{
	const unsigned locore_id = rte_lcore_id();
	const unsigned i = rte_lcore_index(locore_id) - 1;
	return (i < router->worker_count) ? router->worker[i] : NULL;
}

static int
worker_init(void *arg)
{
	npf_router_t *router = arg;
	const unsigned locore_id = rte_lcore_id();
	const unsigned i = rte_lcore_index(locore_id) - 1;
	const int socket_id = rte_socket_id();
	worker_t *w;
	size_t len;

	if (i >= router->worker_count) {
		return 0;
	}

	/* Initialize the worker structure. */
	len = offsetof(worker_t, queue[router->ifnet_count]);
	w = rte_zmalloc_socket("worker-mm", len, 0, socket_id);
	if ((router->worker[i] = w) == NULL) {
		return -1;
	}
	w->router = router;
	w->i = i;

	/* Register the NPF worker. */
	npfk_thread_register(router->npf);
	w->npf = router->npf;

	/*
	 * Initialize the packet queues.
	 */
	for (unsigned q = 0; q < router->ifnet_count; q++) {
		pktqueue_t *pq;

		len = offsetof(worker_t, queue[router->pktqueue_size]);
		pq = rte_zmalloc_socket("pktq-mm", len, 0, socket_id);
		if (pq == NULL) {
			goto err;
		}
		w->queue[q] = pq;
	}
	printf("  worker %u (lcore %u) ready\n", i, locore_id);
	return 0;
err:
	worker_fini(arg);
	return -1;
}

static int
worker_fini(void *arg)
{
	npf_router_t *router = arg;
	worker_t *worker = get_worker_ctx(router);

	if (!worker) {
		return 0;
	}
	npfk_thread_unregister(router->npf);

	for (unsigned q = 0; q < router->ifnet_count; q++) {
		pktqueue_t *pq = worker->queue[q];
		rte_free(pq);
	}
	rte_free(worker);
	return 0;
}

static int
worker_run(void *arg)
{
	npf_router_t *router = arg;
	worker_t *worker = get_worker_ctx(router);

	if (!worker)
		return 0;

	while (!stop) {
		uint16_t port_id;

		/*
		 * Process each interface of interest.
		 */
		RTE_ETH_FOREACH_DEV(port_id) {
			if (ifnet_interesting(router, port_id)) {
				if_input(worker, port_id);
			}
		}
	}
	return 0;
}

static npf_router_t *
router_create(void)
{
	npf_router_t *router;
	unsigned nworkers;
	size_t len;

	if ((nworkers = rte_lcore_count()) <= 1) {
		return NULL;
	}
	nworkers--; // exclude the master

	/*
	 * Allocate the router structure.
	 */
	len = offsetof(npf_router_t, worker[nworkers]);
	if ((router = rte_zmalloc(NULL, len, 0)) == NULL) {
		return NULL;
	}
	router->worker_count = nworkers;
	router->pktqueue_size = BURST_SIZE;
	LIST_INIT(&router->ifnet_list);

	/*
	 * Initialize mbuf pool.
	 */
	router->mbuf_pool = rte_pktmbuf_pool_create("mbuf-pl",
	    NUM_MBUFS * MAX_IFNET_IDS, MBUF_CACHE_SIZE,
	    RTE_CACHE_LINE_ROUNDUP(sizeof(npf_mbuf_priv_t)),
	    RTE_MBUF_DEFAULT_BUF_SIZE, SOCKET_ID_ANY);
	if (!router->mbuf_pool) {
		rte_free(router);
		return NULL;
	}

	/*
	 * NPF instance and its operations.
	 */
	npf_dpdk_init(router);
	npfk_sysinit(1);

	router->npf = npf_dpdk_create(0);
	if (!router->npf) {
		goto err;
	}
	if (npf_alg_icmp_init(router->npf) != 0) {
		goto err;
	}
	router->rtable = route_table_create();
	if (!router->rtable) {
		goto err;
	}
	return router;
err:
	router_destroy(router);
	return NULL;
}

static void
router_destroy(npf_router_t *router)
{
	for (unsigned i = 0; i < MAX_IFNET_IDS; i++) {
		ifnet_t *ifp;

		if ((ifp = router->ifnet_map[i]) != NULL) {
			ifnet_ifdetach(router, ifp);
		}
		free(router->ifnet_addrs[i]);
	}
	if (router->rtable) {
		route_table_destroy(router->rtable);
	}
	if (router->npf) {
		npf_alg_icmp_fini(router->npf);
		npfk_destroy(router->npf);
	}
	npfk_sysfini();
	rte_free(router);
}

static int
load_npf_config(npf_t *npf)
{
	const char config_nvlist_path[] = "/tmp/npf.nvlist";
	npf_error_t errinfo;
	nl_config_t *ncf;
	void *config_ref;
	struct stat sb;
	size_t blen;
	void *blob;
	int fd;

	/*
	 * FIXME/XXX: Rework all this.
	 * Read 'npfctl debug' dump in the /tmp/npf.nvlist file.
	 */

	if ((fd = open(config_nvlist_path, O_RDONLY)) == -1) {
		return -1;
	}
	if (stat(config_nvlist_path, &sb) == -1 || (blen = sb.st_size) == 0) {
		close(fd);
		return -1;
	}
	blob = mmap(NULL, blen, PROT_READ, MAP_FILE | MAP_PRIVATE, fd, 0);
	if (blob == MAP_FAILED) {
		close(fd);
		return -1;
	}
	close(fd);

	ncf = npf_config_import(blob, blen);
	munmap(blob, blen);
	if (ncf == NULL) {
		return -1;
	}

	/*
	 * - Build the config: we get a reference for loading.
	 * - Load the config to the NPF instance.
	 * - Note: npf_load() will consume the config.
	 */
	config_ref = npf_config_build(ncf);
	if (npfk_load(npf, config_ref, &errinfo) != 0) {
		return -1;
	}
	return 0;
}

int
main(int argc, char **argv)
{
	npf_router_t *router;
	unsigned lcore_id, port_id;

	puts("- Initializing DPDK");
	if (rte_eal_init(argc, argv) == -1) {
		rte_exit(EXIT_FAILURE, "rte_eal_init() failed");
	}
	setup_signals();

	/*
	 * Setup the NPF router configuration.
	 */
	puts("- Initializing NPF");
	if ((router = router_create()) == NULL) {
		errx(EXIT_FAILURE, "router_create() failed");
	}

	/*
	 * Load the configuration.
	 */
	if (load_config(router) == -1) {
		errx(EXIT_FAILURE, "failed to load the configuration");
	}
	if (load_npf_config(router->npf) == -1) {
		errx(EXIT_FAILURE, "failed to load the npf.conf");
	}

	/*
	 * Initialize network interfaces.
	 */
	puts("- Initializing network interfaces");
	RTE_ETH_FOREACH_DEV(port_id) {
		if (!ifnet_interesting(router, port_id)) {
			continue;
		}
		if (ifnet_setup(router, port_id, router->worker_count) == -1) {
			rte_exit(EXIT_FAILURE, "ifnet_setup");
		}
		if (ifnet_ifattach(router, port_id) == -1) {
			rte_exit(EXIT_FAILURE, "ifnet_ifattach");
		}
		printf("  configured network interface %u\n", port_id);
		router->ifnet_count++;
	}
	if (router->ifnet_count == 0) {
		errx(EXIT_FAILURE, "no routable interfaces; exiting. ");
	}
	ASSERT(router->ifnet_count < MAX_IFNET_IDS);

	/*
	 * Initialize all workers.
	 */
	puts("- Initializing workers");
	RTE_LCORE_FOREACH_SLAVE(lcore_id) {
		rte_eal_remote_launch(worker_init, router, lcore_id);
	}
	RTE_LCORE_FOREACH_SLAVE(lcore_id) {
		if (rte_eal_wait_lcore(lcore_id) == -1) {
			rte_exit(EXIT_FAILURE, "worker_init");
		}
	}

	/*
	 * Spin up the worker processing.
	 */
	puts("- Starting router");
	RTE_LCORE_FOREACH_SLAVE(lcore_id) {
		rte_eal_remote_launch(worker_run, router, lcore_id);
	}
	rte_eal_mp_wait_lcore();

	/*
	 * Destroy the NPF router resources.
	 */
	puts("- Exiting");
	RTE_LCORE_FOREACH_SLAVE(lcore_id) {
		rte_eal_remote_launch(worker_fini, router, lcore_id);
	}
	rte_eal_mp_wait_lcore();
	router_destroy(router);
	return 0;
}
