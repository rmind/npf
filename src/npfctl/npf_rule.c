/*	$NetBSD$	*/

/*-
 * Copyright (c) 2013 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This material is based upon work partially supported by The
 * NetBSD Foundation under a contract with Mindaugas Rasiukevicius.
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

/*
 * npfctl(8) dynamic ruleset interface.
 */

#include <sys/cdefs.h>
__RCSID("$NetBSD$");

#include <stdio.h>
#include <stdlib.h>

static nl_rule_t *
npfctl_parse_rule(int argc, char **argv)
{
	char rule_string[1024];
	nl_rule_t *rl;

	/* Get the rule string and parse it. */
	if (!join(rule_string, sizeof(rule_string), argc, argv)) {
		errx(EXIT_FAILURE, "command too long");
	}
	npfctl_parse_string(rule_string);
	if ((rl = npfctl_rule_ref()) == NULL) {
		errx(EXIT_FAILURE, "could not parse the rule");
	}
	return rl;
}

static void
npfctl_generate_key(nl_rule_t *rl, void *key)
{
	void *rule_meta;
	size_t len;

	if ((rule_meta = npf_rule_export(rl, &len)) == NULL) {
		errx(EXIT_FAILURE, "error generating rule key");
	}
	__CTASSERT(NPF_RULE_MAXKEYLEN >= SHA_DIGEST_LENGTH);
	memset(key, 0, NPF_RULE_MAXKEYLEN);
	SHA1(rule_meta, len, key);
	free(rule_meta);
}

void
npfctl_rule(int fd, int argc, char **argv)
{
	static const struct ruleops_s {
		const char *	cmd;
		int		action;
		bool		extra_arg;
	} ruleops[] = {
		{ "add",	NPF_CMD_RULE_ADD,	true	},
		{ "rem",	NPF_CMD_RULE_REMKEY,	true	},
		{ "del",	NPF_CMD_RULE_REMKEY,	true	},
		{ "rem-id",	NPF_CMD_RULE_REMOVE,	true	},
		{ "list",	NPF_CMD_RULE_LIST,	false	},
		{ "flush",	NPF_CMD_RULE_FLUSH,	false	},
		{ NULL,		0,			0	}
	};
	uint8_t key[NPF_RULE_MAXKEYLEN];
	const char *ruleset_name = argv[0];
	const char *cmd = argv[1];
	int error, action = 0;
	uint64_t rule_id;
	nl_rule_t *rl;

	for (int n = 0; ruleops[n].cmd != NULL; n++) {
		if (strcmp(cmd, ruleops[n].cmd) == 0) {
			action = ruleops[n].action;
			break;
		}
	}
	if (!action || (ruleops[n].extra_arg && argc == 0)) {
		usage();
	}

	switch (action) {
	case NPF_CMD_RULE_ADD:
		rl = npfctl_parse_rule(argc, argv);
		npfctl_generate_key(rl, key);
		npf_rule_setkey(rl, key, sizeof(key));
		error = npf_ruleset_add(fd, ruleset_name, rl, &rule_id);
		break;
	case NPF_CMD_RULE_REMKEY:
		rl = npfctl_parse_rule(argc, argv);
		npfctl_generate_key(rl, key);
		error = npf_ruleset_remkey(fd, ruleset_name, key, sizeof(key));
		break;
	case NPF_CMD_RULE_REMOVE:
		rule_id = strtoull(argv[0], NULL, 16);
		error = npf_ruleset_remove(fd, ruleset_name, rule_id);
		break;
	case NPF_CMD_RULE_LIST:
		error = npfctl_ruleset_show(fd, ruleset_name);
		break;
	case NPF_CMD_RULE_FLUSH:
		error = npf_ruleset_flush(fd, ruleset_name);
		break;
	default:
		assert(false);
	}

	switch (error) {
	case 0:
		/* Success. */
		break;
	case ESRCH:
		errx(EXIT_FAILURE, "ruleset \"%s\" not found", ruleset_name);
	case ENOENT:
		errx(EXIT_FAILURE, "rule was not found");
	default:
		errx(EXIT_FAILURE, "rule operation: %s", strerror(error));
	}
	if (action == NPF_CMD_RULE_ADD) {
		printf("OK %" PRIx64 "\n", rule_id);
	}
}
