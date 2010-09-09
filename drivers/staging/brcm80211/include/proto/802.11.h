/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _802_11_H_
#define _802_11_H_

#ifndef _TYPEDEFS_H_
#include <typedefs.h>
#endif

#ifndef _NET_ETHERNET_H_
#include <proto/ethernet.h>
#endif

#include <proto/wpa.h>

#include <packed_section_start.h>

#define DOT11_TU_TO_US			1024

#define DOT11_A3_HDR_LEN		24
#define DOT11_A4_HDR_LEN		30
#define DOT11_MAC_HDR_LEN		DOT11_A3_HDR_LEN
#define DOT11_FCS_LEN			4
#define DOT11_ICV_LEN			4
#define DOT11_ICV_AES_LEN		8
#define DOT11_QOS_LEN			2
#define DOT11_HTC_LEN			4

#define DOT11_KEY_INDEX_SHIFT		6
#define DOT11_IV_LEN			4
#define DOT11_IV_TKIP_LEN		8
#define DOT11_IV_AES_OCB_LEN		4
#define DOT11_IV_AES_CCM_LEN		8
#define DOT11_IV_MAX_LEN		8

#define DOT11_MAX_MPDU_BODY_LEN		2304

#define DOT11_MAX_MPDU_LEN		(DOT11_A4_HDR_LEN + \
					 DOT11_QOS_LEN + \
					 DOT11_IV_AES_CCM_LEN + \
					 DOT11_MAX_MPDU_BODY_LEN + \
					 DOT11_ICV_LEN + \
					 DOT11_FCS_LEN)

#define DOT11_MAX_SSID_LEN		32

#define DOT11_DEFAULT_RTS_LEN		2347
#define DOT11_MAX_RTS_LEN		2347

#define DOT11_MIN_FRAG_LEN		256
#define DOT11_MAX_FRAG_LEN		2346
#define DOT11_DEFAULT_FRAG_LEN		2346

#define DOT11_MIN_BEACON_PERIOD		1
#define DOT11_MAX_BEACON_PERIOD		0xFFFF

#define DOT11_MIN_DTIM_PERIOD		1
#define DOT11_MAX_DTIM_PERIOD		0xFF

#define DOT11_LLC_SNAP_HDR_LEN		8
#define DOT11_OUI_LEN			3
BWL_PRE_PACKED_STRUCT struct dot11_llc_snap_header {
	uint8 dsap;
	uint8 ssap;
	uint8 ctl;
	uint8 oui[DOT11_OUI_LEN];
	uint16 type;
} BWL_POST_PACKED_STRUCT;

#define RFC1042_HDR_LEN	(ETHER_HDR_LEN + DOT11_LLC_SNAP_HDR_LEN)

BWL_PRE_PACKED_STRUCT struct dot11_header {
	uint16 fc;
	uint16 durid;
	struct ether_addr a1;
	struct ether_addr a2;
	struct ether_addr a3;
	uint16 seq;
	struct ether_addr a4;
} BWL_POST_PACKED_STRUCT;

BWL_PRE_PACKED_STRUCT struct dot11_rts_frame {
	uint16 fc;
	uint16 durid;
	struct ether_addr ra;
	struct ether_addr ta;
} BWL_POST_PACKED_STRUCT;
#define	DOT11_RTS_LEN		16

BWL_PRE_PACKED_STRUCT struct dot11_cts_frame {
	uint16 fc;
	uint16 durid;
	struct ether_addr ra;
} BWL_POST_PACKED_STRUCT;
#define	DOT11_CTS_LEN		10

BWL_PRE_PACKED_STRUCT struct dot11_ack_frame {
	uint16 fc;
	uint16 durid;
	struct ether_addr ra;
} BWL_POST_PACKED_STRUCT;
#define	DOT11_ACK_LEN		10

BWL_PRE_PACKED_STRUCT struct dot11_ps_poll_frame {
	uint16 fc;
	uint16 durid;
	struct ether_addr bssid;
	struct ether_addr ta;
} BWL_POST_PACKED_STRUCT;
#define	DOT11_PS_POLL_LEN	16

BWL_PRE_PACKED_STRUCT struct dot11_cf_end_frame {
	uint16 fc;
	uint16 durid;
	struct ether_addr ra;
	struct ether_addr bssid;
} BWL_POST_PACKED_STRUCT;
#define	DOT11_CS_END_LEN	16

BWL_PRE_PACKED_STRUCT struct dot11_action_wifi_vendor_specific {
	uint8 category;
	uint8 OUI[3];
	uint8 type;
	uint8 subtype;
	uint8 data[1040];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_action_wifi_vendor_specific
 dot11_action_wifi_vendor_specific_t;

BWL_PRE_PACKED_STRUCT struct dot11_action_vs_frmhdr {
	uint8 category;
	uint8 OUI[3];
	uint8 type;
	uint8 subtype;
	uint8 data[1];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_action_vs_frmhdr dot11_action_vs_frmhdr_t;
#define DOT11_ACTION_VS_HDR_LEN	6

#define BCM_ACTION_OUI_BYTE0	0x00
#define BCM_ACTION_OUI_BYTE1	0x90
#define BCM_ACTION_OUI_BYTE2	0x4c

#define DOT11_BA_CTL_POLICY_NORMAL	0x0000
#define DOT11_BA_CTL_POLICY_NOACK	0x0001
#define DOT11_BA_CTL_POLICY_MASK	0x0001

#define DOT11_BA_CTL_MTID		0x0002
#define DOT11_BA_CTL_COMPRESSED		0x0004

#define DOT11_BA_CTL_NUMMSDU_MASK	0x0FC0
#define DOT11_BA_CTL_NUMMSDU_SHIFT	6

#define DOT11_BA_CTL_TID_MASK		0xF000
#define DOT11_BA_CTL_TID_SHIFT		12

BWL_PRE_PACKED_STRUCT struct dot11_ctl_header {
	uint16 fc;
	uint16 durid;
	struct ether_addr ra;
	struct ether_addr ta;
} BWL_POST_PACKED_STRUCT;
#define DOT11_CTL_HDR_LEN	16

BWL_PRE_PACKED_STRUCT struct dot11_bar {
	uint16 bar_control;
	uint16 seqnum;
} BWL_POST_PACKED_STRUCT;
#define DOT11_BAR_LEN		4

#define DOT11_BA_BITMAP_LEN	128
#define DOT11_BA_CMP_BITMAP_LEN	8

BWL_PRE_PACKED_STRUCT struct dot11_ba {
	uint16 ba_control;
	uint16 seqnum;
	uint8 bitmap[DOT11_BA_BITMAP_LEN];
} BWL_POST_PACKED_STRUCT;
#define DOT11_BA_LEN		4

BWL_PRE_PACKED_STRUCT struct dot11_management_header {
	uint16 fc;
	uint16 durid;
	struct ether_addr da;
	struct ether_addr sa;
	struct ether_addr bssid;
	uint16 seq;
} BWL_POST_PACKED_STRUCT;
#define	DOT11_MGMT_HDR_LEN	24

BWL_PRE_PACKED_STRUCT struct dot11_bcn_prb {
	uint32 timestamp[2];
	uint16 beacon_interval;
	uint16 capability;
} BWL_POST_PACKED_STRUCT;
#define	DOT11_BCN_PRB_LEN	12
#define	DOT11_BCN_PRB_FIXED_LEN	12

BWL_PRE_PACKED_STRUCT struct dot11_auth {
	uint16 alg;
	uint16 seq;
	uint16 status;
} BWL_POST_PACKED_STRUCT;
#define DOT11_AUTH_FIXED_LEN	6

BWL_PRE_PACKED_STRUCT struct dot11_assoc_req {
	uint16 capability;
	uint16 listen;
} BWL_POST_PACKED_STRUCT;
#define DOT11_ASSOC_REQ_FIXED_LEN	4

BWL_PRE_PACKED_STRUCT struct dot11_reassoc_req {
	uint16 capability;
	uint16 listen;
	struct ether_addr ap;
} BWL_POST_PACKED_STRUCT;
#define DOT11_REASSOC_REQ_FIXED_LEN	10

BWL_PRE_PACKED_STRUCT struct dot11_assoc_resp {
	uint16 capability;
	uint16 status;
	uint16 aid;
} BWL_POST_PACKED_STRUCT;
#define DOT11_ASSOC_RESP_FIXED_LEN	6

BWL_PRE_PACKED_STRUCT struct dot11_action_measure {
	uint8 category;
	uint8 action;
	uint8 token;
	uint8 data[1];
} BWL_POST_PACKED_STRUCT;
#define DOT11_ACTION_MEASURE_LEN	3

BWL_PRE_PACKED_STRUCT struct dot11_action_ht_ch_width {
	uint8 category;
	uint8 action;
	uint8 ch_width;
} BWL_POST_PACKED_STRUCT;

BWL_PRE_PACKED_STRUCT struct dot11_action_ht_mimops {
	uint8 category;
	uint8 action;
	uint8 control;
} BWL_POST_PACKED_STRUCT;

#define SM_PWRSAVE_ENABLE	1
#define SM_PWRSAVE_MODE		2

BWL_PRE_PACKED_STRUCT struct dot11_power_cnst {
	uint8 id;
	uint8 len;
	uint8 power;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_power_cnst dot11_power_cnst_t;

BWL_PRE_PACKED_STRUCT struct dot11_power_cap {
	uint8 min;
	uint8 max;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_power_cap dot11_power_cap_t;

BWL_PRE_PACKED_STRUCT struct dot11_tpc_rep {
	uint8 id;
	uint8 len;
	uint8 tx_pwr;
	uint8 margin;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_tpc_rep dot11_tpc_rep_t;
#define DOT11_MNG_IE_TPC_REPORT_LEN	2

BWL_PRE_PACKED_STRUCT struct dot11_supp_channels {
	uint8 id;
	uint8 len;
	uint8 first_channel;
	uint8 num_channels;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_supp_channels dot11_supp_channels_t;

BWL_PRE_PACKED_STRUCT struct dot11_extch {
	uint8 id;
	uint8 len;
	uint8 extch;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_extch dot11_extch_ie_t;

BWL_PRE_PACKED_STRUCT struct dot11_brcm_extch {
	uint8 id;
	uint8 len;
	uint8 oui[3];
	uint8 type;
	uint8 extch;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_brcm_extch dot11_brcm_extch_ie_t;

#define DOT11_EXTCH_IE_LEN	1
#define DOT11_EXT_CH_MASK	0x03
#define DOT11_EXT_CH_UPPER	0x01
#define DOT11_EXT_CH_LOWER	0x03
#define DOT11_EXT_CH_NONE	0x00

BWL_PRE_PACKED_STRUCT struct dot11_action_frmhdr {
	uint8 category;
	uint8 action;
	uint8 data[1];
} BWL_POST_PACKED_STRUCT;
#define DOT11_ACTION_FRMHDR_LEN	2

BWL_PRE_PACKED_STRUCT struct dot11_channel_switch {
	uint8 id;
	uint8 len;
	uint8 mode;
	uint8 channel;
	uint8 count;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_channel_switch dot11_chan_switch_ie_t;

#define DOT11_SWITCH_IE_LEN	3

#define DOT11_CSA_MODE_ADVISORY		0
#define DOT11_CSA_MODE_NO_TX		1

BWL_PRE_PACKED_STRUCT struct dot11_action_switch_channel {
	uint8 category;
	uint8 action;
	dot11_chan_switch_ie_t chan_switch_ie;
	dot11_brcm_extch_ie_t extch_ie;
} BWL_POST_PACKED_STRUCT;

BWL_PRE_PACKED_STRUCT struct dot11_csa_body {
	uint8 mode;
	uint8 reg;
	uint8 channel;
	uint8 count;
} BWL_POST_PACKED_STRUCT;

BWL_PRE_PACKED_STRUCT struct dot11_ext_csa {
	uint8 id;
	uint8 len;
	struct dot11_csa_body b;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_ext_csa dot11_ext_csa_ie_t;
#define DOT11_EXT_CSA_IE_LEN	4

BWL_PRE_PACKED_STRUCT struct dot11_action_ext_csa {
	uint8 category;
	uint8 action;
	dot11_ext_csa_ie_t chan_switch_ie;
} BWL_POST_PACKED_STRUCT;

BWL_PRE_PACKED_STRUCT struct dot11y_action_ext_csa {
	uint8 category;
	uint8 action;
	struct dot11_csa_body b;
} BWL_POST_PACKED_STRUCT;

BWL_PRE_PACKED_STRUCT struct dot11_obss_coex {
	uint8 id;
	uint8 len;
	uint8 info;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_obss_coex dot11_obss_coex_t;
#define DOT11_OBSS_COEXINFO_LEN	1

#define	DOT11_OBSS_COEX_INFO_REQ		0x01
#define	DOT11_OBSS_COEX_40MHZ_INTOLERANT	0x02
#define	DOT11_OBSS_COEX_20MHZ_WIDTH_REQ	0x04

BWL_PRE_PACKED_STRUCT struct dot11_obss_chanlist {
	uint8 id;
	uint8 len;
	uint8 regclass;
	uint8 chanlist[1];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_obss_chanlist dot11_obss_chanlist_t;
#define DOT11_OBSS_CHANLIST_FIXED_LEN	1

BWL_PRE_PACKED_STRUCT struct dot11_extcap_ie {
	uint8 id;
	uint8 len;
	uint8 cap;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_extcap_ie dot11_extcap_ie_t;
#define DOT11_EXTCAP_LEN	1

#define DOT11_MEASURE_TYPE_BASIC 	0
#define DOT11_MEASURE_TYPE_CCA 		1
#define DOT11_MEASURE_TYPE_RPI		2
#define DOT11_MEASURE_TYPE_CHLOAD		3
#define DOT11_MEASURE_TYPE_NOISE		4
#define DOT11_MEASURE_TYPE_BEACON		5
#define DOT11_MEASURE_TYPE_FRAME	6
#define DOT11_MEASURE_TYPE_STATS		7
#define DOT11_MEASURE_TYPE_LCI		8
#define DOT11_MEASURE_TYPE_TXSTREAM		9
#define DOT11_MEASURE_TYPE_PAUSE		255

#define DOT11_MEASURE_MODE_PARALLEL 	(1<<0)
#define DOT11_MEASURE_MODE_ENABLE 	(1<<1)
#define DOT11_MEASURE_MODE_REQUEST	(1<<2)
#define DOT11_MEASURE_MODE_REPORT 	(1<<3)
#define DOT11_MEASURE_MODE_DUR 	(1<<4)

#define DOT11_MEASURE_MODE_LATE 	(1<<0)
#define DOT11_MEASURE_MODE_INCAPABLE	(1<<1)
#define DOT11_MEASURE_MODE_REFUSED	(1<<2)

#define DOT11_MEASURE_BASIC_MAP_BSS	((uint8)(1<<0))
#define DOT11_MEASURE_BASIC_MAP_OFDM	((uint8)(1<<1))
#define DOT11_MEASURE_BASIC_MAP_UKNOWN	((uint8)(1<<2))
#define DOT11_MEASURE_BASIC_MAP_RADAR	((uint8)(1<<3))
#define DOT11_MEASURE_BASIC_MAP_UNMEAS	((uint8)(1<<4))

BWL_PRE_PACKED_STRUCT struct dot11_meas_req {
	uint8 id;
	uint8 len;
	uint8 token;
	uint8 mode;
	uint8 type;
	uint8 channel;
	uint8 start_time[8];
	uint16 duration;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_meas_req dot11_meas_req_t;
#define DOT11_MNG_IE_MREQ_LEN 14

#define DOT11_MNG_IE_MREQ_FIXED_LEN 3

BWL_PRE_PACKED_STRUCT struct dot11_meas_rep {
	uint8 id;
	uint8 len;
	uint8 token;
	uint8 mode;
	uint8 type;
	BWL_PRE_PACKED_STRUCT union {
		BWL_PRE_PACKED_STRUCT struct {
			uint8 channel;
			uint8 start_time[8];
			uint16 duration;
			uint8 map;
		} BWL_POST_PACKED_STRUCT basic;
		uint8 data[1];
	} BWL_POST_PACKED_STRUCT rep;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_meas_rep dot11_meas_rep_t;

#define DOT11_MNG_IE_MREP_FIXED_LEN	3

BWL_PRE_PACKED_STRUCT struct dot11_meas_rep_basic {
	uint8 channel;
	uint8 start_time[8];
	uint16 duration;
	uint8 map;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_meas_rep_basic dot11_meas_rep_basic_t;
#define DOT11_MEASURE_BASIC_REP_LEN	12

BWL_PRE_PACKED_STRUCT struct dot11_quiet {
	uint8 id;
	uint8 len;
	uint8 count;
	uint8 period;
	uint16 duration;
	uint16 offset;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_quiet dot11_quiet_t;

BWL_PRE_PACKED_STRUCT struct chan_map_tuple {
	uint8 channel;
	uint8 map;
} BWL_POST_PACKED_STRUCT;
typedef struct chan_map_tuple chan_map_tuple_t;

BWL_PRE_PACKED_STRUCT struct dot11_ibss_dfs {
	uint8 id;
	uint8 len;
	uint8 eaddr[ETHER_ADDR_LEN];
	uint8 interval;
	chan_map_tuple_t map[1];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_ibss_dfs dot11_ibss_dfs_t;

#define WME_OUI			"\x00\x50\xf2"
#define WME_VER			1
#define WME_TYPE		2
#define WME_SUBTYPE_IE		0
#define WME_SUBTYPE_PARAM_IE	1
#define WME_SUBTYPE_TSPEC	2

#define AC_BE			0
#define AC_BK			1
#define AC_VI			2
#define AC_VO			3
#define AC_COUNT		4

typedef uint8 ac_bitmap_t;

#define AC_BITMAP_NONE		0x0
#define AC_BITMAP_ALL		0xf
#define AC_BITMAP_TST(ab, ac)	(((ab) & (1 << (ac))) != 0)
#define AC_BITMAP_SET(ab, ac)	(((ab) |= (1 << (ac))))
#define AC_BITMAP_RESET(ab, ac) (((ab) &= ~(1 << (ac))))

BWL_PRE_PACKED_STRUCT struct wme_ie {
	uint8 oui[3];
	uint8 type;
	uint8 subtype;
	uint8 version;
	uint8 qosinfo;
} BWL_POST_PACKED_STRUCT;
typedef struct wme_ie wme_ie_t;
#define WME_IE_LEN 7

BWL_PRE_PACKED_STRUCT struct edcf_acparam {
	uint8 ACI;
	uint8 ECW;
	uint16 TXOP;
} BWL_POST_PACKED_STRUCT;
typedef struct edcf_acparam edcf_acparam_t;

BWL_PRE_PACKED_STRUCT struct wme_param_ie {
	uint8 oui[3];
	uint8 type;
	uint8 subtype;
	uint8 version;
	uint8 qosinfo;
	uint8 rsvd;
	edcf_acparam_t acparam[AC_COUNT];
} BWL_POST_PACKED_STRUCT;
typedef struct wme_param_ie wme_param_ie_t;
#define WME_PARAM_IE_LEN            24

#define WME_QI_AP_APSD_MASK         0x80
#define WME_QI_AP_APSD_SHIFT        7
#define WME_QI_AP_COUNT_MASK        0x0f
#define WME_QI_AP_COUNT_SHIFT       0

#define WME_QI_STA_MAXSPLEN_MASK    0x60
#define WME_QI_STA_MAXSPLEN_SHIFT   5
#define WME_QI_STA_APSD_ALL_MASK    0xf
#define WME_QI_STA_APSD_ALL_SHIFT   0
#define WME_QI_STA_APSD_BE_MASK     0x8
#define WME_QI_STA_APSD_BE_SHIFT    3
#define WME_QI_STA_APSD_BK_MASK     0x4
#define WME_QI_STA_APSD_BK_SHIFT    2
#define WME_QI_STA_APSD_VI_MASK     0x2
#define WME_QI_STA_APSD_VI_SHIFT    1
#define WME_QI_STA_APSD_VO_MASK     0x1
#define WME_QI_STA_APSD_VO_SHIFT    0

#define EDCF_AIFSN_MIN               1
#define EDCF_AIFSN_MAX               15
#define EDCF_AIFSN_MASK              0x0f
#define EDCF_ACM_MASK                0x10
#define EDCF_ACI_MASK                0x60
#define EDCF_ACI_SHIFT               5
#define EDCF_AIFSN_SHIFT             12

#define EDCF_ECW_MIN                 0
#define EDCF_ECW_MAX                 15
#define EDCF_ECW2CW(exp)             ((1 << (exp)) - 1)
#define EDCF_ECWMIN_MASK             0x0f
#define EDCF_ECWMAX_MASK             0xf0
#define EDCF_ECWMAX_SHIFT            4

#define EDCF_TXOP_MIN                0
#define EDCF_TXOP_MAX                65535
#define EDCF_TXOP2USEC(txop)         ((txop) << 5)

#define NON_EDCF_AC_BE_ACI_STA          0x02

#define EDCF_AC_BE_ACI_STA           0x03
#define EDCF_AC_BE_ECW_STA           0xA4
#define EDCF_AC_BE_TXOP_STA          0x0000
#define EDCF_AC_BK_ACI_STA           0x27
#define EDCF_AC_BK_ECW_STA           0xA4
#define EDCF_AC_BK_TXOP_STA          0x0000
#define EDCF_AC_VI_ACI_STA           0x42
#define EDCF_AC_VI_ECW_STA           0x43
#define EDCF_AC_VI_TXOP_STA          0x005e
#define EDCF_AC_VO_ACI_STA           0x62
#define EDCF_AC_VO_ECW_STA           0x32
#define EDCF_AC_VO_TXOP_STA          0x002f

#define EDCF_AC_BE_ACI_AP            0x03
#define EDCF_AC_BE_ECW_AP            0x64
#define EDCF_AC_BE_TXOP_AP           0x0000
#define EDCF_AC_BK_ACI_AP            0x27
#define EDCF_AC_BK_ECW_AP            0xA4
#define EDCF_AC_BK_TXOP_AP           0x0000
#define EDCF_AC_VI_ACI_AP            0x41
#define EDCF_AC_VI_ECW_AP            0x43
#define EDCF_AC_VI_TXOP_AP           0x005e
#define EDCF_AC_VO_ACI_AP            0x61
#define EDCF_AC_VO_ECW_AP            0x32
#define EDCF_AC_VO_TXOP_AP           0x002f

BWL_PRE_PACKED_STRUCT struct edca_param_ie {
	uint8 qosinfo;
	uint8 rsvd;
	edcf_acparam_t acparam[AC_COUNT];
} BWL_POST_PACKED_STRUCT;
typedef struct edca_param_ie edca_param_ie_t;
#define EDCA_PARAM_IE_LEN            18

BWL_PRE_PACKED_STRUCT struct qos_cap_ie {
	uint8 qosinfo;
} BWL_POST_PACKED_STRUCT;
typedef struct qos_cap_ie qos_cap_ie_t;

BWL_PRE_PACKED_STRUCT struct dot11_qbss_load_ie {
	uint8 id;
	uint8 length;
	uint16 station_count;
	uint8 channel_utilization;
	uint16 aac;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_qbss_load_ie dot11_qbss_load_ie_t;

#define FIXED_MSDU_SIZE 0x8000
#define MSDU_SIZE_MASK	0x7fff

#define	INTEGER_SHIFT	13
#define FRACTION_MASK	0x1FFF

BWL_PRE_PACKED_STRUCT struct dot11_management_notification {
	uint8 category;
	uint8 action;
	uint8 token;
	uint8 status;
	uint8 data[1];
} BWL_POST_PACKED_STRUCT;
#define DOT11_MGMT_NOTIFICATION_LEN 4

#define WME_ADDTS_REQUEST	0
#define WME_ADDTS_RESPONSE	1
#define WME_DELTS_REQUEST	2

#define WME_ADMISSION_ACCEPTED		0
#define WME_INVALID_PARAMETERS		1
#define WME_ADMISSION_REFUSED		3

#define BCN_PRB_SSID(body) ((char*)(body) + DOT11_BCN_PRB_LEN)

#define DOT11_OPEN_SYSTEM	0
#define DOT11_SHARED_KEY	1
#define DOT11_OPEN_SHARED	2
#define DOT11_CHALLENGE_LEN	128

#define FC_PVER_MASK		0x3
#define FC_PVER_SHIFT		0
#define FC_TYPE_MASK		0xC
#define FC_TYPE_SHIFT		2
#define FC_SUBTYPE_MASK		0xF0
#define FC_SUBTYPE_SHIFT	4
#define FC_TODS			0x100
#define FC_TODS_SHIFT		8
#define FC_FROMDS		0x200
#define FC_FROMDS_SHIFT		9
#define FC_MOREFRAG		0x400
#define FC_MOREFRAG_SHIFT	10
#define FC_RETRY		0x800
#define FC_RETRY_SHIFT		11
#define FC_PM			0x1000
#define FC_PM_SHIFT		12
#define FC_MOREDATA		0x2000
#define FC_MOREDATA_SHIFT	13
#define FC_WEP			0x4000
#define FC_WEP_SHIFT		14
#define FC_ORDER		0x8000
#define FC_ORDER_SHIFT		15

#define SEQNUM_SHIFT		4
#define SEQNUM_MAX		0x1000
#define FRAGNUM_MASK		0xF

#define FC_TYPE_MNG		0
#define FC_TYPE_CTL		1
#define FC_TYPE_DATA		2

#define FC_SUBTYPE_ASSOC_REQ		0
#define FC_SUBTYPE_ASSOC_RESP		1
#define FC_SUBTYPE_REASSOC_REQ		2
#define FC_SUBTYPE_REASSOC_RESP		3
#define FC_SUBTYPE_PROBE_REQ		4
#define FC_SUBTYPE_PROBE_RESP		5
#define FC_SUBTYPE_BEACON		8
#define FC_SUBTYPE_ATIM			9
#define FC_SUBTYPE_DISASSOC		10
#define FC_SUBTYPE_AUTH			11
#define FC_SUBTYPE_DEAUTH		12
#define FC_SUBTYPE_ACTION		13
#define FC_SUBTYPE_ACTION_NOACK		14

#define FC_SUBTYPE_CTL_WRAPPER		7
#define FC_SUBTYPE_BLOCKACK_REQ		8
#define FC_SUBTYPE_BLOCKACK		9
#define FC_SUBTYPE_PS_POLL		10
#define FC_SUBTYPE_RTS			11
#define FC_SUBTYPE_CTS			12
#define FC_SUBTYPE_ACK			13
#define FC_SUBTYPE_CF_END		14
#define FC_SUBTYPE_CF_END_ACK		15

#define FC_SUBTYPE_DATA			0
#define FC_SUBTYPE_DATA_CF_ACK		1
#define FC_SUBTYPE_DATA_CF_POLL		2
#define FC_SUBTYPE_DATA_CF_ACK_POLL	3
#define FC_SUBTYPE_NULL			4
#define FC_SUBTYPE_CF_ACK		5
#define FC_SUBTYPE_CF_POLL		6
#define FC_SUBTYPE_CF_ACK_POLL		7
#define FC_SUBTYPE_QOS_DATA		8
#define FC_SUBTYPE_QOS_DATA_CF_ACK	9
#define FC_SUBTYPE_QOS_DATA_CF_POLL	10
#define FC_SUBTYPE_QOS_DATA_CF_ACK_POLL	11
#define FC_SUBTYPE_QOS_NULL		12
#define FC_SUBTYPE_QOS_CF_POLL		14
#define FC_SUBTYPE_QOS_CF_ACK_POLL	15

#define FC_SUBTYPE_ANY_QOS(s)		(((s) & 8) != 0)
#define FC_SUBTYPE_ANY_NULL(s)		(((s) & 4) != 0)
#define FC_SUBTYPE_ANY_CF_POLL(s)	(((s) & 2) != 0)
#define FC_SUBTYPE_ANY_CF_ACK(s)	(((s) & 1) != 0)

#define FC_KIND_MASK		(FC_TYPE_MASK | FC_SUBTYPE_MASK)

#define FC_KIND(t, s)	(((t) << FC_TYPE_SHIFT) | ((s) << FC_SUBTYPE_SHIFT))

#define FC_SUBTYPE(fc)	(((fc) & FC_SUBTYPE_MASK) >> FC_SUBTYPE_SHIFT)
#define FC_TYPE(fc)	(((fc) & FC_TYPE_MASK) >> FC_TYPE_SHIFT)

#define FC_ASSOC_REQ	FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_ASSOC_REQ)
#define FC_ASSOC_RESP	FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_ASSOC_RESP)
#define FC_REASSOC_REQ	FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_REASSOC_REQ)
#define FC_REASSOC_RESP	FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_REASSOC_RESP)
#define FC_PROBE_REQ	FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_PROBE_REQ)
#define FC_PROBE_RESP	FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_PROBE_RESP)
#define FC_BEACON	FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_BEACON)
#define FC_DISASSOC	FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_DISASSOC)
#define FC_AUTH		FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_AUTH)
#define FC_DEAUTH	FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_DEAUTH)
#define FC_ACTION	FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_ACTION)
#define FC_ACTION_NOACK	FC_KIND(FC_TYPE_MNG, FC_SUBTYPE_ACTION_NOACK)

#define FC_CTL_WRAPPER	FC_KIND(FC_TYPE_CTL, FC_SUBTYPE_CTL_WRAPPER)
#define FC_BLOCKACK_REQ	FC_KIND(FC_TYPE_CTL, FC_SUBTYPE_BLOCKACK_REQ)
#define FC_BLOCKACK	FC_KIND(FC_TYPE_CTL, FC_SUBTYPE_BLOCKACK)
#define FC_PS_POLL	FC_KIND(FC_TYPE_CTL, FC_SUBTYPE_PS_POLL)
#define FC_RTS		FC_KIND(FC_TYPE_CTL, FC_SUBTYPE_RTS)
#define FC_CTS		FC_KIND(FC_TYPE_CTL, FC_SUBTYPE_CTS)
#define FC_ACK		FC_KIND(FC_TYPE_CTL, FC_SUBTYPE_ACK)
#define FC_CF_END	FC_KIND(FC_TYPE_CTL, FC_SUBTYPE_CF_END)
#define FC_CF_END_ACK	FC_KIND(FC_TYPE_CTL, FC_SUBTYPE_CF_END_ACK)

#define FC_DATA		FC_KIND(FC_TYPE_DATA, FC_SUBTYPE_DATA)
#define FC_NULL_DATA	FC_KIND(FC_TYPE_DATA, FC_SUBTYPE_NULL)
#define FC_DATA_CF_ACK	FC_KIND(FC_TYPE_DATA, FC_SUBTYPE_DATA_CF_ACK)
#define FC_QOS_DATA	FC_KIND(FC_TYPE_DATA, FC_SUBTYPE_QOS_DATA)
#define FC_QOS_NULL	FC_KIND(FC_TYPE_DATA, FC_SUBTYPE_QOS_NULL)

#define QOS_PRIO_SHIFT		0
#define QOS_PRIO_MASK		0x0007
#define QOS_PRIO(qos)		(((qos) & QOS_PRIO_MASK) >> QOS_PRIO_SHIFT)

#define QOS_TID_SHIFT		0
#define QOS_TID_MASK		0x000f
#define QOS_TID(qos)		(((qos) & QOS_TID_MASK) >> QOS_TID_SHIFT)

#define QOS_EOSP_SHIFT		4
#define QOS_EOSP_MASK		0x0010
#define QOS_EOSP(qos)		(((qos) & QOS_EOSP_MASK) >> QOS_EOSP_SHIFT)

#define QOS_ACK_NORMAL_ACK	0
#define QOS_ACK_NO_ACK		1
#define QOS_ACK_NO_EXP_ACK	2
#define QOS_ACK_BLOCK_ACK	3
#define QOS_ACK_SHIFT		5
#define QOS_ACK_MASK		0x0060
#define QOS_ACK(qos)		(((qos) & QOS_ACK_MASK) >> QOS_ACK_SHIFT)

#define QOS_AMSDU_SHIFT		7
#define QOS_AMSDU_MASK		0x0080

#define DOT11_MNG_AUTH_ALGO_LEN		2
#define DOT11_MNG_AUTH_SEQ_LEN		2
#define DOT11_MNG_BEACON_INT_LEN	2
#define DOT11_MNG_CAP_LEN		2
#define DOT11_MNG_AP_ADDR_LEN		6
#define DOT11_MNG_LISTEN_INT_LEN	2
#define DOT11_MNG_REASON_LEN		2
#define DOT11_MNG_AID_LEN		2
#define DOT11_MNG_STATUS_LEN		2
#define DOT11_MNG_TIMESTAMP_LEN		8

#define DOT11_AID_MASK			0x3fff

#define DOT11_RC_RESERVED		0
#define DOT11_RC_UNSPECIFIED		1
#define DOT11_RC_AUTH_INVAL		2
#define DOT11_RC_DEAUTH_LEAVING		3
#define DOT11_RC_INACTIVITY		4
#define DOT11_RC_BUSY			5
#define DOT11_RC_INVAL_CLASS_2		6
#define DOT11_RC_INVAL_CLASS_3		7
#define DOT11_RC_DISASSOC_LEAVING	8
#define DOT11_RC_NOT_AUTH		9
#define DOT11_RC_BAD_PC			10
#define DOT11_RC_BAD_CHANNELS		11

#define DOT11_RC_UNSPECIFIED_QOS	32
#define DOT11_RC_INSUFFCIENT_BW		33
#define DOT11_RC_EXCESSIVE_FRAMES	34
#define DOT11_RC_TX_OUTSIDE_TXOP	35
#define DOT11_RC_LEAVING_QBSS		36
#define DOT11_RC_BAD_MECHANISM		37
#define DOT11_RC_SETUP_NEEDED		38
#define DOT11_RC_TIMEOUT		39

#define DOT11_RC_MAX			23

#define DOT11_SC_SUCCESS		0
#define DOT11_SC_FAILURE		1
#define DOT11_SC_CAP_MISMATCH		10
#define DOT11_SC_REASSOC_FAIL		11
#define DOT11_SC_ASSOC_FAIL		12
#define DOT11_SC_AUTH_MISMATCH		13
#define DOT11_SC_AUTH_SEQ		14
#define DOT11_SC_AUTH_CHALLENGE_FAIL	15
#define DOT11_SC_AUTH_TIMEOUT		16
#define DOT11_SC_ASSOC_BUSY_FAIL	17
#define DOT11_SC_ASSOC_RATE_MISMATCH	18
#define DOT11_SC_ASSOC_SHORT_REQUIRED	19
#define DOT11_SC_ASSOC_PBCC_REQUIRED	20
#define DOT11_SC_ASSOC_AGILITY_REQUIRED	21
#define DOT11_SC_ASSOC_SPECTRUM_REQUIRED	22
#define DOT11_SC_ASSOC_BAD_POWER_CAP	23
#define DOT11_SC_ASSOC_BAD_SUP_CHANNELS	24
#define DOT11_SC_ASSOC_SHORTSLOT_REQUIRED	25
#define DOT11_SC_ASSOC_ERPBCC_REQUIRED	26
#define DOT11_SC_ASSOC_DSSOFDM_REQUIRED	27

#define	DOT11_SC_DECLINED		37
#define	DOT11_SC_INVALID_PARAMS		38

#define DOT11_MNG_DS_PARAM_LEN			1
#define DOT11_MNG_IBSS_PARAM_LEN		2

#define DOT11_MNG_TIM_FIXED_LEN			3
#define DOT11_MNG_TIM_DTIM_COUNT		0
#define DOT11_MNG_TIM_DTIM_PERIOD		1
#define DOT11_MNG_TIM_BITMAP_CTL		2
#define DOT11_MNG_TIM_PVB			3

#define TLV_TAG_OFF		0
#define TLV_LEN_OFF		1
#define TLV_HDR_LEN		2
#define TLV_BODY_OFF		2

#define DOT11_MNG_SSID_ID			0
#define DOT11_MNG_RATES_ID			1
#define DOT11_MNG_FH_PARMS_ID			2
#define DOT11_MNG_DS_PARMS_ID			3
#define DOT11_MNG_CF_PARMS_ID			4
#define DOT11_MNG_TIM_ID			5
#define DOT11_MNG_IBSS_PARMS_ID			6
#define DOT11_MNG_COUNTRY_ID			7
#define DOT11_MNG_HOPPING_PARMS_ID		8
#define DOT11_MNG_HOPPING_TABLE_ID		9
#define DOT11_MNG_REQUEST_ID			10
#define DOT11_MNG_QBSS_LOAD_ID 			11
#define DOT11_MNG_EDCA_PARAM_ID			12
#define DOT11_MNG_CHALLENGE_ID			16
#define DOT11_MNG_PWR_CONSTRAINT_ID		32
#define DOT11_MNG_PWR_CAP_ID			33
#define DOT11_MNG_TPC_REQUEST_ID 		34
#define DOT11_MNG_TPC_REPORT_ID			35
#define DOT11_MNG_SUPP_CHANNELS_ID		36
#define DOT11_MNG_CHANNEL_SWITCH_ID		37
#define DOT11_MNG_MEASURE_REQUEST_ID		38
#define DOT11_MNG_MEASURE_REPORT_ID		39
#define DOT11_MNG_QUIET_ID			40
#define DOT11_MNG_IBSS_DFS_ID			41
#define DOT11_MNG_ERP_ID			42
#define DOT11_MNG_TS_DELAY_ID			43
#define	DOT11_MNG_HT_CAP			45
#define DOT11_MNG_QOS_CAP_ID			46
#define DOT11_MNG_NONERP_ID			47
#define DOT11_MNG_RSN_ID			48
#define DOT11_MNG_EXT_RATES_ID			50
#define DOT11_MNG_AP_CHREP_ID		51
#define DOT11_MNG_NBR_REP_ID		52
#define	DOT11_MNG_REGCLASS_ID			59
#define DOT11_MNG_EXT_CSA_ID			60
#define	DOT11_MNG_HT_ADD			61
#define	DOT11_MNG_EXT_CHANNEL_OFFSET		62

#define DOT11_MNG_RRM_CAP_ID		70
#define	DOT11_MNG_HT_BSS_COEXINFO_ID		72
#define	DOT11_MNG_HT_BSS_CHANNEL_REPORT_ID	73
#define	DOT11_MNG_HT_OBSS_ID			74
#define	DOT11_MNG_EXT_CAP			127
#define DOT11_MNG_WPA_ID			221
#define DOT11_MNG_PROPR_ID			221

#define DOT11_MNG_VS_ID				221

#define DOT11_RATE_BASIC			0x80
#define DOT11_RATE_MASK				0x7F

#define DOT11_MNG_ERP_LEN			1
#define DOT11_MNG_NONERP_PRESENT		0x01
#define DOT11_MNG_USE_PROTECTION		0x02
#define DOT11_MNG_BARKER_PREAMBLE		0x04

#define DOT11_MGN_TS_DELAY_LEN		4
#define TS_DELAY_FIELD_SIZE			4

#define DOT11_CAP_ESS				0x0001
#define DOT11_CAP_IBSS				0x0002
#define DOT11_CAP_POLLABLE			0x0004
#define DOT11_CAP_POLL_RQ			0x0008
#define DOT11_CAP_PRIVACY			0x0010
#define DOT11_CAP_SHORT				0x0020
#define DOT11_CAP_PBCC				0x0040
#define DOT11_CAP_AGILITY			0x0080
#define DOT11_CAP_SPECTRUM			0x0100
#define DOT11_CAP_SHORTSLOT			0x0400
#define DOT11_CAP_RRM			0x1000
#define DOT11_CAP_CCK_OFDM			0x2000

#define DOT11_OBSS_COEX_MNG_SUPPORT	0x01

#define DOT11_ACTION_HDR_LEN		2

#define DOT11_ACTION_CAT_ERR_MASK	0x80
#define DOT11_ACTION_CAT_MASK		0x7F
#define DOT11_ACTION_CAT_SPECT_MNG	0
#define DOT11_ACTION_CAT_QOS		1
#define DOT11_ACTION_CAT_DLS		2
#define DOT11_ACTION_CAT_BLOCKACK	3
#define DOT11_ACTION_CAT_PUBLIC		4
#define DOT11_ACTION_CAT_RRM		5
#define DOT11_ACTION_CAT_HT		7
#define DOT11_ACTION_NOTIFICATION	17
#define DOT11_ACTION_CAT_VS		127

#define DOT11_SM_ACTION_M_REQ		0
#define DOT11_SM_ACTION_M_REP		1
#define DOT11_SM_ACTION_TPC_REQ		2
#define DOT11_SM_ACTION_TPC_REP		3
#define DOT11_SM_ACTION_CHANNEL_SWITCH	4
#define DOT11_SM_ACTION_EXT_CSA		5

#define DOT11_ACTION_ID_HT_CH_WIDTH	0
#define DOT11_ACTION_ID_HT_MIMO_PS	1

#define DOT11_PUB_ACTION_BSS_COEX_MNG	0
#define DOT11_PUB_ACTION_CHANNEL_SWITCH	4

#define DOT11_BA_ACTION_ADDBA_REQ	0
#define DOT11_BA_ACTION_ADDBA_RESP	1
#define DOT11_BA_ACTION_DELBA		2

#define DOT11_ADDBA_PARAM_AMSDU_SUP	0x0001
#define DOT11_ADDBA_PARAM_POLICY_MASK	0x0002
#define DOT11_ADDBA_PARAM_POLICY_SHIFT	1
#define DOT11_ADDBA_PARAM_TID_MASK	0x003c
#define DOT11_ADDBA_PARAM_TID_SHIFT	2
#define DOT11_ADDBA_PARAM_BSIZE_MASK	0xffc0
#define DOT11_ADDBA_PARAM_BSIZE_SHIFT	6

#define DOT11_ADDBA_POLICY_DELAYED	0
#define DOT11_ADDBA_POLICY_IMMEDIATE	1

BWL_PRE_PACKED_STRUCT struct dot11_addba_req {
	uint8 category;
	uint8 action;
	uint8 token;
	uint16 addba_param_set;
	uint16 timeout;
	uint16 start_seqnum;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_addba_req dot11_addba_req_t;
#define DOT11_ADDBA_REQ_LEN		9

BWL_PRE_PACKED_STRUCT struct dot11_addba_resp {
	uint8 category;
	uint8 action;
	uint8 token;
	uint16 status;
	uint16 addba_param_set;
	uint16 timeout;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_addba_resp dot11_addba_resp_t;
#define DOT11_ADDBA_RESP_LEN		9

#define DOT11_DELBA_PARAM_INIT_MASK	0x0800
#define DOT11_DELBA_PARAM_INIT_SHIFT	11
#define DOT11_DELBA_PARAM_TID_MASK	0xf000
#define DOT11_DELBA_PARAM_TID_SHIFT	12

BWL_PRE_PACKED_STRUCT struct dot11_delba {
	uint8 category;
	uint8 action;
	uint16 delba_param_set;
	uint16 reason;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_delba dot11_delba_t;
#define DOT11_DELBA_LEN			6

#define DOT11_RRM_CAP_LEN		5
BWL_PRE_PACKED_STRUCT struct dot11_rrm_cap_ie {
	uint8 cap[DOT11_RRM_CAP_LEN];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rrm_cap_ie dot11_rrm_cap_ie_t;

#define DOT11_RRM_CAP_LINK			0
#define DOT11_RRM_CAP_NEIGHBOR_REPORT	1
#define DOT11_RRM_CAP_PARALLEL		2
#define DOT11_RRM_CAP_REPEATED		3
#define DOT11_RRM_CAP_BCN_PASSIVE	4
#define DOT11_RRM_CAP_BCN_ACTIVE	5
#define DOT11_RRM_CAP_BCN_TABLE		6
#define DOT11_RRM_CAP_BCN_REP_COND	7
#define DOT11_RRM_CAP_AP_CHANREP	16

#define DOT11_RM_ACTION_RM_REQ		0
#define DOT11_RM_ACTION_RM_REP		1
#define DOT11_RM_ACTION_LM_REQ		2
#define DOT11_RM_ACTION_LM_REP		3
#define DOT11_RM_ACTION_NR_REQ		4
#define DOT11_RM_ACTION_NR_REP		5

BWL_PRE_PACKED_STRUCT struct dot11_rm_action {
	uint8 category;
	uint8 action;
	uint8 token;
	uint8 data[1];
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rm_action dot11_rm_action_t;
#define DOT11_RM_ACTION_LEN 3

BWL_PRE_PACKED_STRUCT struct dot11_rmreq {
	uint8 category;
	uint8 action;
	uint8 token;
	uint16 reps;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rmreq dot11_rmreq_t;
#define DOT11_RMREQ_LEN	5

BWL_PRE_PACKED_STRUCT struct dot11_rm_ie {
	uint8 id;
	uint8 len;
	uint8 token;
	uint8 mode;
	uint8 type;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rm_ie dot11_rm_ie_t;
#define DOT11_RM_IE_LEN	5

#define DOT11_RMREQ_MODE_PARALLEL	1
#define DOT11_RMREQ_MODE_ENABLE		2
#define DOT11_RMREQ_MODE_REQUEST	4
#define DOT11_RMREQ_MODE_REPORT		8
#define DOT11_RMREQ_MODE_DURMAND	0x10

#define DOT11_RMREP_MODE_LATE		1
#define DOT11_RMREP_MODE_INCAPABLE	2
#define DOT11_RMREP_MODE_REFUSED	4

BWL_PRE_PACKED_STRUCT struct dot11_rmreq_bcn {
	uint8 id;
	uint8 len;
	uint8 token;
	uint8 mode;
	uint8 type;
	uint8 reg;
	uint8 channel;
	uint16 interval;
	uint16 duration;
	uint8 bcn_mode;
	struct ether_addr bssid;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rmreq_bcn dot11_rmreq_bcn_t;
#define DOT11_RMREQ_BCN_LEN	18

BWL_PRE_PACKED_STRUCT struct dot11_rmrep_bcn {
	uint8 reg;
	uint8 channel;
	uint32 starttime[2];
	uint16 duration;
	uint8 frame_info;
	uint8 rcpi;
	uint8 rsni;
	struct ether_addr bssid;
	uint8 antenna_id;
	uint32 parent_tsf;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rmrep_bcn dot11_rmrep_bcn_t;
#define DOT11_RMREP_BCN_LEN	26

#define DOT11_RMREQ_BCN_PASSIVE	0
#define DOT11_RMREQ_BCN_ACTIVE	1
#define DOT11_RMREQ_BCN_TABLE	2

#define DOT11_RMREQ_BCN_SSID_ID	0
#define DOT11_RMREQ_BCN_REPINFO_ID	1
#define DOT11_RMREQ_BCN_REPDET_ID	2
#define DOT11_RMREQ_BCN_REQUEST_ID	10
#define DOT11_RMREQ_BCN_APCHREP_ID	51

#define DOT11_RMREQ_BCN_REPDET_FIXED	0
#define DOT11_RMREQ_BCN_REPDET_REQUEST	1
#define DOT11_RMREQ_BCN_REPDET_ALL	2

#define DOT11_RMREP_BCN_FRM_BODY	1

BWL_PRE_PACKED_STRUCT struct dot11_rmrep_nbr {
	struct ether_addr bssid;
	uint32 bssid_info;
	uint8 reg;
	uint8 channel;
	uint8 phytype;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_rmrep_nbr dot11_rmrep_nbr_t;
#define DOT11_RMREP_NBR_LEN	13

#define DOT11_BSSTYPE_INFRASTRUCTURE		0
#define DOT11_BSSTYPE_INDEPENDENT		1
#define DOT11_BSSTYPE_ANY			2
#define DOT11_SCANTYPE_ACTIVE			0
#define DOT11_SCANTYPE_PASSIVE			1

BWL_PRE_PACKED_STRUCT struct dot11_lmreq {
	uint8 category;
	uint8 action;
	uint8 token;
	uint8 txpwr;
	uint8 maxtxpwr;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_lmreq dot11_lmreq_t;
#define DOT11_LMREQ_LEN	5

BWL_PRE_PACKED_STRUCT struct dot11_lmrep {
	uint8 category;
	uint8 action;
	uint8 token;
	dot11_tpc_rep_t tpc;
	uint8 rxant;
	uint8 txant;
	uint8 rcpi;
	uint8 rsni;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_lmrep dot11_lmrep_t;
#define DOT11_LMREP_LEN	11

#define PREN_PREAMBLE		24
#define PREN_MM_EXT		12
#define PREN_PREAMBLE_EXT	4

#define RIFS_11N_TIME		2

#define APHY_SLOT_TIME		9
#define APHY_SIFS_TIME		16
#define APHY_DIFS_TIME		(APHY_SIFS_TIME + (2 * APHY_SLOT_TIME))
#define APHY_PREAMBLE_TIME	16
#define APHY_SIGNAL_TIME	4
#define APHY_SYMBOL_TIME	4
#define APHY_SERVICE_NBITS	16
#define APHY_TAIL_NBITS		6
#define	APHY_CWMIN		15

#define BPHY_SLOT_TIME		20
#define BPHY_SIFS_TIME		10
#define BPHY_DIFS_TIME		50
#define BPHY_PLCP_TIME		192
#define BPHY_PLCP_SHORT_TIME	96
#define	BPHY_CWMIN		31

#define DOT11_OFDM_SIGNAL_EXTENSION	6

#define PHY_CWMAX		1023

#define	DOT11_MAXNUMFRAGS	16

typedef struct d11cnt {
	uint32 txfrag;
	uint32 txmulti;
	uint32 txfail;
	uint32 txretry;
	uint32 txretrie;
	uint32 rxdup;
	uint32 txrts;
	uint32 txnocts;
	uint32 txnoack;
	uint32 rxfrag;
	uint32 rxmulti;
	uint32 rxcrc;
	uint32 txfrmsnt;
	uint32 rxundec;
} d11cnt_t;

#define AB_GUARDCOUNT	10

BWL_PRE_PACKED_STRUCT struct vndr_ie {
	uchar id;
	uchar len;
	uchar oui[3];
	uchar data[1];
} BWL_POST_PACKED_STRUCT;
typedef struct vndr_ie vndr_ie_t;

#define VNDR_IE_HDR_LEN		2
#define VNDR_IE_MIN_LEN		3
#define VNDR_IE_MAX_LEN		256

#define MCSSET_LEN	16
#define MAX_MCS_NUM	(128)

BWL_PRE_PACKED_STRUCT struct ht_cap_ie {
	uint16 cap;
	uint8 params;
	uint8 supp_mcs[MCSSET_LEN];
	uint16 ext_htcap;
	uint32 txbf_cap;
	uint8 as_cap;
} BWL_POST_PACKED_STRUCT;
typedef struct ht_cap_ie ht_cap_ie_t;

#define HT_CAP_IE_LEN		26
#define HT_CAP_IE_TYPE		51

#define HT_CAP_LDPC_CODING	0x0001
#define HT_CAP_40MHZ		0x0002
#define HT_CAP_MIMO_PS_MASK	0x000C
#define HT_CAP_MIMO_PS_SHIFT	0x0002
#define HT_CAP_MIMO_PS_OFF	0x0003
#define HT_CAP_MIMO_PS_RTS	0x0001
#define HT_CAP_MIMO_PS_ON	0x0000
#define HT_CAP_GF		0x0010
#define HT_CAP_SHORT_GI_20	0x0020
#define HT_CAP_SHORT_GI_40	0x0040
#define HT_CAP_TX_STBC		0x0080
#define HT_CAP_RX_STBC_MASK	0x0300
#define HT_CAP_RX_STBC_SHIFT	8
#define HT_CAP_DELAYED_BA	0x0400
#define HT_CAP_MAX_AMSDU	0x0800
#define HT_CAP_DSSS_CCK	0x1000
#define HT_CAP_PSMP		0x2000
#define HT_CAP_40MHZ_INTOLERANT 0x4000
#define HT_CAP_LSIG_TXOP	0x8000

#define HT_CAP_RX_STBC_NO		0x0
#define HT_CAP_RX_STBC_ONE_STREAM	0x1
#define HT_CAP_RX_STBC_TWO_STREAM	0x2
#define HT_CAP_RX_STBC_THREE_STREAM	0x3

#define HT_MAX_AMSDU		7935
#define HT_MIN_AMSDU		3835

#define HT_PARAMS_RX_FACTOR_MASK	0x03
#define HT_PARAMS_DENSITY_MASK		0x1C
#define HT_PARAMS_DENSITY_SHIFT	2

#define AMPDU_MAX_MPDU_DENSITY	7
#define AMPDU_RX_FACTOR_8K	0
#define AMPDU_RX_FACTOR_16K	1
#define AMPDU_RX_FACTOR_32K	2
#define AMPDU_RX_FACTOR_64K	3
#define AMPDU_RX_FACTOR_BASE	8*1024

#define AMPDU_DELIMITER_LEN	4

BWL_PRE_PACKED_STRUCT struct ht_add_ie {
	uint8 ctl_ch;
	uint8 byte1;
	uint16 opmode;
	uint16 misc_bits;
	uint8 basic_mcs[MCSSET_LEN];
} BWL_POST_PACKED_STRUCT;
typedef struct ht_add_ie ht_add_ie_t;

#define HT_ADD_IE_LEN	22
#define HT_ADD_IE_TYPE	52

#define HT_BW_ANY		0x04
#define HT_RIFS_PERMITTED     	0x08

#define HT_OPMODE_MASK	        0x0003
#define HT_OPMODE_SHIFT		0
#define HT_OPMODE_PURE		0x0000
#define HT_OPMODE_OPTIONAL	0x0001
#define HT_OPMODE_HT20IN40	0x0002
#define HT_OPMODE_MIXED	0x0003
#define HT_OPMODE_NONGF	0x0004
#define DOT11N_TXBURST		0x0008
#define DOT11N_OBSS_NONHT	0x0010

#define HT_BASIC_STBC_MCS	0x007f
#define HT_DUAL_STBC_PROT	0x0080
#define HT_SECOND_BCN		0x0100
#define HT_LSIG_TXOP		0x0200
#define HT_PCO_ACTIVE		0x0400
#define HT_PCO_PHASE		0x0800

#define DOT11N_2G_TXBURST_LIMIT	6160
#define DOT11N_5G_TXBURST_LIMIT	3080

#define GET_HT_OPMODE(add_ie)	\
	((ltoh16_ua(&add_ie->opmode) & HT_OPMODE_MASK)  >> HT_OPMODE_SHIFT)
#define HT_MIXEDMODE_PRESENT(add_ie)	\
	((ltoh16_ua(&add_ie->opmode) & HT_OPMODE_MASK) == HT_OPMODE_MIXED)
#define HT_HT20_PRESENT(add_ie)	\
	((ltoh16_ua(&add_ie->opmode) & HT_OPMODE_MASK) == HT_OPMODE_HT20IN40)
#define HT_OPTIONAL_PRESENT(add_ie)	\
	((ltoh16_ua(&add_ie->opmode) & HT_OPMODE_MASK) == HT_OPMODE_OPTIONAL)
#define HT_USE_PROTECTION(add_ie)	\
	(HT_HT20_PRESENT((add_ie)) || HT_MIXEDMODE_PRESENT((add_ie)))
#define HT_NONGF_PRESENT(add_ie)	\
	((ltoh16_ua(&add_ie->opmode) & HT_OPMODE_NONGF) == HT_OPMODE_NONGF)
#define DOT11N_TXBURST_PRESENT(add_ie)	\
	((ltoh16_ua(&add_ie->opmode) & DOT11N_TXBURST) == DOT11N_TXBURST)
#define DOT11N_OBSS_NONHT_PRESENT(add_ie)	\
	((ltoh16_ua(&add_ie->opmode) & DOT11N_OBSS_NONHT) == DOT11N_OBSS_NONHT)

BWL_PRE_PACKED_STRUCT struct obss_params {
	uint16 passive_dwell;
	uint16 active_dwell;
	uint16 bss_widthscan_interval;
	uint16 passive_total;
	uint16 active_total;
	uint16 chanwidth_transition_dly;
	uint16 activity_threshold;
} BWL_POST_PACKED_STRUCT;
typedef struct obss_params obss_params_t;

BWL_PRE_PACKED_STRUCT struct dot11_obss_ie {
	uint8 id;
	uint8 len;
	obss_params_t obss_params;
} BWL_POST_PACKED_STRUCT;
typedef struct dot11_obss_ie dot11_obss_ie_t;
#define DOT11_OBSS_SCAN_IE_LEN	sizeof(obss_params_t)

#define HT_CTRL_LA_TRQ		0x00000002
#define HT_CTRL_LA_MAI		0x0000003C
#define HT_CTRL_LA_MAI_SHIFT	2
#define HT_CTRL_LA_MAI_MRQ	0x00000004
#define HT_CTRL_LA_MAI_MSI	0x00000038
#define HT_CTRL_LA_MFSI		0x000001C0
#define HT_CTRL_LA_MFSI_SHIFT	6
#define HT_CTRL_LA_MFB_ASELC	0x0000FE00
#define HT_CTRL_LA_MFB_ASELC_SH	9
#define HT_CTRL_LA_ASELC_CMD	0x00000C00
#define HT_CTRL_LA_ASELC_DATA	0x0000F000
#define HT_CTRL_CAL_POS		0x00030000
#define HT_CTRL_CAL_SEQ		0x000C0000
#define HT_CTRL_CSI_STEERING	0x00C00000
#define HT_CTRL_CSI_STEER_SHIFT	22
#define HT_CTRL_CSI_STEER_NFB	0
#define HT_CTRL_CSI_STEER_CSI	1
#define HT_CTRL_CSI_STEER_NCOM	2
#define HT_CTRL_CSI_STEER_COM	3
#define HT_CTRL_NDP_ANNOUNCE	0x01000000
#define HT_CTRL_AC_CONSTRAINT	0x40000000
#define HT_CTRL_RDG_MOREPPDU	0x80000000

#define HT_OPMODE_OPTIONAL	0x0001
#define HT_OPMODE_HT20IN40	0x0002
#define HT_OPMODE_MIXED	0x0003
#define HT_OPMODE_NONGF	0x0004
#define DOT11N_TXBURST		0x0008
#define DOT11N_OBSS_NONHT	0x0010

#define WPA_VERSION		1
#define WPA_OUI			"\x00\x50\xF2"

#define WPA2_VERSION		1
#define WPA2_VERSION_LEN	2
#define WPA2_OUI		"\x00\x0F\xAC"

#define WPA_OUI_LEN	3

#define WFA_OUI			"\x00\x50\xF2"
#define WFA_OUI_LEN	3

#define WFA_OUI_TYPE_WPA	1
#define WFA_OUI_TYPE_WPS	4
#define WFA_OUI_TYPE_P2P	9

#define RSN_AKM_NONE		0
#define RSN_AKM_UNSPECIFIED	1
#define RSN_AKM_PSK		2

#define DOT11_MAX_DEFAULT_KEYS	4
#define DOT11_MAX_KEY_SIZE	32
#define DOT11_MAX_IV_SIZE	16
#define DOT11_EXT_IV_FLAG	(1<<5)
#define DOT11_WPA_KEY_RSC_LEN   8

#define WEP1_KEY_SIZE		5
#define WEP1_KEY_HEX_SIZE	10
#define WEP128_KEY_SIZE		13
#define WEP128_KEY_HEX_SIZE	26
#define TKIP_MIC_SIZE		8
#define TKIP_EOM_SIZE		7
#define TKIP_EOM_FLAG		0x5a
#define TKIP_KEY_SIZE		32
#define TKIP_MIC_AUTH_TX	16
#define TKIP_MIC_AUTH_RX	24
#define TKIP_MIC_SUP_RX		TKIP_MIC_AUTH_TX
#define TKIP_MIC_SUP_TX		TKIP_MIC_AUTH_RX
#define AES_KEY_SIZE		16
#define AES_MIC_SIZE		8

#define WCN_OUI			"\x00\x50\xf2"
#define WCN_TYPE		4

#include <packed_section_end.h>

#endif				/* _802_11_H_ */
