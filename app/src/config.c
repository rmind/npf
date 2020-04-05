/*
 * Copyright (c) 2020 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>

#include <arpa/inet.h>
#include <lpm.h>

#include "npf_router.h"
#include "utils.h"

#define	NPF_ROUTER_CONFIG	"/etc/npf-router.conf"

static unsigned
str_tokenize(char *line, char **tokens, unsigned n)
{
	const char *sep = " \t";
	unsigned i = 0;
	char *token;

	while ((token = strsep(&line, sep)) != NULL && i < n) {
		if (*sep == '\0' || strpbrk(token, sep) != NULL) {
			continue;
		}
		tokens[i++] = token;
	}
	return i;
}

static int
parse_command(npf_router_t *router, char **tokens, size_t n)
{
	int if_idx;

	if (strcasecmp(tokens[0], "route") == 0) {
		route_table_t *rtable = router->rtable;
		unsigned char addr[16];
		route_info_t rt;
		unsigned plen;
		size_t alen;

		if (n < 3) {
			return -1;
		}
		if (lpm_strtobin(tokens[1], addr, &alen, &plen) == -1) {
			return -1;
		}
		if ((if_idx = ifnet_register(router, tokens[2])) == -1) {
			warnx("unknown interface '%s'", tokens[2]);
			return -1;
		}

		memset(&rt, 0, sizeof(route_info_t));
		rt.if_idx = if_idx;
		if (n >= 4) {
			if (inet_pton(AF_INET, tokens[3], &rt.next_hop) == -1) {
				warnx("invalid gateway '%s'", tokens[3]);
				return -1;
			}
			rt.addr_len = sizeof(in_addr_t);
		}
		if (route_add(rtable, addr, alen, plen, &rt) == -1) {
			return -1;
		}
		return 0;
	}
	if (strcasecmp(tokens[0], "ifconfig") == 0) {
		if (n < 2) {
			return -1;
		}
		if ((if_idx = ifnet_register(router, tokens[1])) == -1) {
			warnx("unknown interface '%s'", tokens[1]);
			return -1;
		}
		router->ifnet_addrs[if_idx] = strdup(tokens[2]);
		return 0;
	}
	return -1;
}

static int
parse_config(npf_router_t *router, FILE *fp)
{
	char *line = NULL;
	size_t ln = 0, n, linesz = 0;
	ssize_t len;

	while ((len = getline(&line, &linesz, fp)) > 0) {
		char *tokens[] = { NULL, NULL, NULL, NULL };
		ln++;

		if (line[0] == '#') {
			continue;
		}
		line[len - 1] = '\0';

		n = str_tokenize(line, tokens, __arraycount(tokens));
		if (n == 0) {
			continue;
		}
		if (parse_command(router, tokens, n) == -1) {
			warnx("invalid command at line %zu", ln);
			free(line);
			return -1;
		}
	}
	free(line);
	return 0;
}

int
load_config(npf_router_t *router)
{
	const char *fpath = getenv("NPFR_CONFIG");
	ssize_t ret = -1;
	FILE *fp;

	fp = fopen(fpath ? fpath : NPF_ROUTER_CONFIG, "r");
	if (fp) {
		ret = (parse_config(router, fp) || ferror(fp)) ? -1 : 0;
		fclose(fp);
	}
	return ret;
}
