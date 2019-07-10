/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   npf_gre.h
 * Author: alexk
 *
 * Created on June 19, 2019, 12:12 PM
 */

#ifndef NPF_PPTP_GRE_H
#define NPF_PPTP_GRE_H

#include "stand/cext.h"

/*
 * PPTP GRE header
 */
struct pptp_gre_hdr {
	uint16_t flags_ver;
	uint16_t proto;
	uint16_t payload_len;
	uint16_t call_id;
	uint16_t seq_num;
}
__packed;

struct pptp_gre_context {
	uint16_t client_call_id;
	uint16_t server_call_id;
}
__packed;

#endif /* NPF_PPTP_GRE_H */

