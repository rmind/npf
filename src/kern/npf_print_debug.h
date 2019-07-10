/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
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

