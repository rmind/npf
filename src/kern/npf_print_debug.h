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

/*
 * File:   npf_print_debug.h
 * Author: alexk
 *
 * Created on June 21, 2016, 1:40 AM
 */

#ifndef NPF_DEBUG_H
#define NPF_DEBUG_H

#include <stdint.h>

#ifdef NPF_PRINT_DEBUG
	
/* debug print contexts */
enum npf_print_debug_context {
	NPF_DC_PPTP_ALG = 0,
	NPF_DC_GRE,
	NPF_DC_ESTABL_CON
};

int
npf_dprintfc(uint32_t context, const char *format, ...);
#define NPF_DPRINTFC(context, ...) npf_dprintfc(context, __VA_ARGS__)

int
npf_dprintfcl(uint32_t context, uint32_t level, const char *format, ...);
#define NPF_DPRINTFCL(context, level, ...) \
		  npf_dprintfcl(context, level, __VA_ARGS__)

void
npf_hex_dump(const char *desc, const void *addr, int len);

void
npf_dhexdumpcl(uint32_t context, uint32_t level, const char *desc, void *addr,
		  int len);
#define NPF_HEX_DUMPCL(context, level, desc, addr, len) \
	npf_dhexdumpcl(context, level, desc, addr, len)
	
#else /* NPF_PRINT_DEBUG */

#define NPF_DPRINTFC(...)
#define NPF_DPRINTFCL(...)
#define NPF_HEX_DUMPCL(...)

#endif /* NPF_PRINT_DEBUG */

#endif /* NPF_DEBUG_H */

