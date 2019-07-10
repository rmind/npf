/*-
 * Copyright (c) 2019 Alex Kiselev alex@therouter.net
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

#ifdef NPF_PRINT_DEBUG

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

#include "npf_print_debug.h"

static bool g_print_debug_contex[] = {
	true,		/* NPF_DC_PPTP_ALG */
	true,		/* NPF_DC_GRE */
	true,		/* NPF_DC_ESTABL_CON */
};

static uint32_t g_print_debug_level = 50;

void
npf_hex_dump(const char *desc, const void *addr, int len)
{
    int i;
    unsigned char buff[17];
    const unsigned char *pc = (const unsigned char *)addr;

    /* Output description if given. */
    if (desc != NULL)
        printf ("%s:\n", desc);

    if (len == 0) {
        printf("  ZERO LENGTH\n");
        return;
    }

    if (len < 0) {
        printf("  NEGATIVE LENGTH: %i\n",len);
        return;
    }

    /* Process every byte in the data. */
    for (i = 0; i < len; i++) {
        /* Multiple of 16 means new line (with line offset). */

        if ((i % 16) == 0) {
            /* Just don't print ASCII for the zeroth line. */
            if (i != 0)
                printf ("  %s\n", buff);

            /* Output the offset. */
            printf ("  %04x ", i);
        }

        /* Now the hex code for the specific character. */
        printf (" %02x", pc[i]);

        /* And store a printable ASCII character for later. */
        if ((pc[i] < 0x20) || (pc[i] > 0x7e))
            buff[i % 16] = '.';
        else
            buff[i % 16] = pc[i];
        buff[(i % 16) + 1] = '\0';
    }

    /* Pad out last line if not exactly 16 characters. */
    while ((i % 16) != 0) {
        printf ("   ");
        i++;
    }

    /* And print the final ASCII bit. */
    printf ("  %s\n", buff);
}

int
npf_dprintfc(uint32_t context, const char *format, ...)
{
	int ret;

	if (g_print_debug_contex[context]) {
		va_list args;
		va_start (args, format);
		ret = vprintf(format, args);
		va_end(args);
	} else
		ret = 0;

	return ret;
}

int
npf_dprintfcl(uint32_t context, uint32_t level, const char *format, ...)
{
	int ret;

	if (g_print_debug_contex[context] && level <= g_print_debug_level) {
		va_list args;
		va_start (args, format);
		ret = vprintf(format, args);
		va_end(args);
	} else
		ret = 0;

	return ret;
}

void
npf_dhexdumpcl(uint32_t context, uint32_t level, const char *desc, void *addr,
		  int len)
{
	if (g_print_debug_contex[context] && level <= g_print_debug_level)
		npf_hex_dump(desc, addr, len);
}

#endif /* NPF_PRINT_DEBUG */
