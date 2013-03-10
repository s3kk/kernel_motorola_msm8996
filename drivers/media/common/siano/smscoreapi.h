/****************************************************************

Siano Mobile Silicon, Inc.
MDTV receiver kernel modules.
Copyright (C) 2006-2008, Uri Shkolnik, Anatoly Greenblat

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

 This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

****************************************************************/

#ifndef __SMS_CORE_API_H__
#define __SMS_CORE_API_H__

#include <linux/device.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/timer.h>

#include <asm/page.h>

#include "smsir.h"

#define kmutex_init(_p_) mutex_init(_p_)
#define kmutex_lock(_p_) mutex_lock(_p_)
#define kmutex_trylock(_p_) mutex_trylock(_p_)
#define kmutex_unlock(_p_) mutex_unlock(_p_)

#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif

#define SMS_PROTOCOL_MAX_RAOUNDTRIP_MS			(10000)
#define SMS_ALLOC_ALIGNMENT				128
#define SMS_DMA_ALIGNMENT				16
#define SMS_ALIGN_ADDRESS(addr) \
	((((uintptr_t)(addr)) + (SMS_DMA_ALIGNMENT-1)) & ~(SMS_DMA_ALIGNMENT-1))

#define SMS_DEVICE_FAMILY1				0
#define SMS_DEVICE_FAMILY2				1
#define SMS_ROM_NO_RESPONSE				2
#define SMS_DEVICE_NOT_READY				0x8000000

enum sms_device_type_st {
	SMS_UNKNOWN_TYPE = -1,
	SMS_STELLAR = 0,
	SMS_NOVA_A0,
	SMS_NOVA_B0,
	SMS_VEGA,
	SMS_VENICE,
	SMS_MING,
	SMS_PELE,
	SMS_RIO,
	SMS_DENVER_1530,
	SMS_DENVER_2160,
	SMS_NUM_OF_DEVICE_TYPES
};

enum sms_power_mode_st {
	SMS_POWER_MODE_ACTIVE,
	SMS_POWER_MODE_SUSPENDED
};

struct smscore_device_t;
struct smscore_client_t;
struct smscore_buffer_t;

typedef int (*hotplug_t)(struct smscore_device_t *coredev,
			 struct device *device, int arrival);

typedef int (*setmode_t)(void *context, int mode);
typedef void (*detectmode_t)(void *context, int *mode);
typedef int (*sendrequest_t)(void *context, void *buffer, size_t size);
typedef int (*loadfirmware_t)(void *context, void *buffer, size_t size);
typedef int (*preload_t)(void *context);
typedef int (*postload_t)(void *context);

typedef int (*onresponse_t)(void *context, struct smscore_buffer_t *cb);
typedef void (*onremove_t)(void *context);

struct smscore_buffer_t {
	/* public members, once passed to clients can be changed freely */
	struct list_head entry;
	int size;
	int offset;

	/* private members, read-only for clients */
	void *p;
	dma_addr_t phys;
	unsigned long offset_in_common;
};

struct smsdevice_params_t {
	struct device	*device;

	int				buffer_size;
	int				num_buffers;

	char			devpath[32];
	unsigned long	flags;

	setmode_t		setmode_handler;
	detectmode_t	detectmode_handler;
	sendrequest_t	sendrequest_handler;
	preload_t		preload_handler;
	postload_t		postload_handler;

	void			*context;
	enum sms_device_type_st device_type;
};

struct smsclient_params_t {
	int				initial_id;
	int				data_type;
	onresponse_t	onresponse_handler;
	onremove_t		onremove_handler;
	void			*context;
};

struct smscore_device_t {
	struct list_head entry;

	struct list_head clients;
	struct list_head subclients;
	spinlock_t clientslock;

	struct list_head buffers;
	spinlock_t bufferslock;
	int num_buffers;

	void *common_buffer;
	int common_buffer_size;
	dma_addr_t common_buffer_phys;

	void *context;
	struct device *device;

	char devpath[32];
	unsigned long device_flags;

	setmode_t setmode_handler;
	detectmode_t detectmode_handler;
	sendrequest_t sendrequest_handler;
	preload_t preload_handler;
	postload_t postload_handler;

	int mode, modes_supported;

	/* host <--> device messages */
	struct completion version_ex_done, data_download_done, trigger_done;
	struct completion data_validity_done, device_ready_done;
	struct completion init_device_done, reload_start_done, resume_done;
	struct completion gpio_configuration_done, gpio_set_level_done;
	struct completion gpio_get_level_done, ir_init_done;

	/* Buffer management */
	wait_queue_head_t buffer_mng_waitq;

	/* GPIO */
	int gpio_get_res;

	/* Target hardware board */
	int board_id;

	/* Firmware */
	u8 *fw_buf;
	u32 fw_buf_size;
	u16 fw_version;

	/* Infrared (IR) */
	struct ir_t ir;

	/*
	 * Identify if device is USB or not.
	 * Used by smsdvb-sysfs to know the root node for debugfs
	 */
	bool is_usb_device;

	int led_state;
};

/* GPIO definitions for antenna frequency domain control (SMS8021) */
#define SMS_ANTENNA_GPIO_0					1
#define SMS_ANTENNA_GPIO_1					0

enum sms_bandwidth_mode {
	BW_8_MHZ = 0,
	BW_7_MHZ = 1,
	BW_6_MHZ = 2,
	BW_5_MHZ = 3,
	BW_ISDBT_1SEG = 4,
	BW_ISDBT_3SEG = 5,
	BW_2_MHZ = 6,
	BW_FM_RADIO = 7,
	BW_ISDBT_13SEG = 8,
	BW_1_5_MHZ = 15,
	BW_UNKNOWN = 0xffff
};


#define MSG_HDR_FLAG_SPLIT_MSG				4

#define MAX_GPIO_PIN_NUMBER					31

#define HIF_TASK							11
#define HIF_TASK_SLAVE					22
#define HIF_TASK_SLAVE2					33
#define HIF_TASK_SLAVE3					44
#define SMS_HOST_LIB						150
#define DVBT_BDA_CONTROL_MSG_ID				201

#define SMS_MAX_PAYLOAD_SIZE				240
#define SMS_TUNE_TIMEOUT					500

enum msg_types {
	MSG_TYPE_BASE_VAL = 500,
	MSG_SMS_GET_VERSION_REQ = 503,
	MSG_SMS_GET_VERSION_RES = 504,
	MSG_SMS_MULTI_BRIDGE_CFG = 505,
	MSG_SMS_GPIO_CONFIG_REQ = 507,
	MSG_SMS_GPIO_CONFIG_RES = 508,
	MSG_SMS_GPIO_SET_LEVEL_REQ = 509,
	MSG_SMS_GPIO_SET_LEVEL_RES = 510,
	MSG_SMS_GPIO_GET_LEVEL_REQ = 511,
	MSG_SMS_GPIO_GET_LEVEL_RES = 512,
	MSG_SMS_EEPROM_BURN_IND = 513,
	MSG_SMS_LOG_ENABLE_CHANGE_REQ = 514,
	MSG_SMS_LOG_ENABLE_CHANGE_RES = 515,
	MSG_SMS_SET_MAX_TX_MSG_LEN_REQ = 516,
	MSG_SMS_SET_MAX_TX_MSG_LEN_RES = 517,
	MSG_SMS_SPI_HALFDUPLEX_TOKEN_HOST_TO_DEVICE = 518,
	MSG_SMS_SPI_HALFDUPLEX_TOKEN_DEVICE_TO_HOST = 519,
	MSG_SMS_BACKGROUND_SCAN_FLAG_CHANGE_REQ = 520,
	MSG_SMS_BACKGROUND_SCAN_FLAG_CHANGE_RES = 521,
	MSG_SMS_BACKGROUND_SCAN_SIGNAL_DETECTED_IND = 522,
	MSG_SMS_BACKGROUND_SCAN_NO_SIGNAL_IND = 523,
	MSG_SMS_CONFIGURE_RF_SWITCH_REQ = 524,
	MSG_SMS_CONFIGURE_RF_SWITCH_RES = 525,
	MSG_SMS_MRC_PATH_DISCONNECT_REQ = 526,
	MSG_SMS_MRC_PATH_DISCONNECT_RES = 527,
	MSG_SMS_RECEIVE_1SEG_THROUGH_FULLSEG_REQ = 528,
	MSG_SMS_RECEIVE_1SEG_THROUGH_FULLSEG_RES = 529,
	MSG_SMS_RECEIVE_VHF_VIA_VHF_INPUT_REQ = 530,
	MSG_SMS_RECEIVE_VHF_VIA_VHF_INPUT_RES = 531,
	MSG_WR_REG_RFT_REQ = 533,
	MSG_WR_REG_RFT_RES = 534,
	MSG_RD_REG_RFT_REQ = 535,
	MSG_RD_REG_RFT_RES = 536,
	MSG_RD_REG_ALL_RFT_REQ = 537,
	MSG_RD_REG_ALL_RFT_RES = 538,
	MSG_HELP_INT = 539,
	MSG_RUN_SCRIPT_INT = 540,
	MSG_SMS_EWS_INBAND_REQ = 541,
	MSG_SMS_EWS_INBAND_RES = 542,
	MSG_SMS_RFS_SELECT_REQ = 543,
	MSG_SMS_RFS_SELECT_RES = 544,
	MSG_SMS_MB_GET_VER_REQ = 545,
	MSG_SMS_MB_GET_VER_RES = 546,
	MSG_SMS_MB_WRITE_CFGFILE_REQ = 547,
	MSG_SMS_MB_WRITE_CFGFILE_RES = 548,
	MSG_SMS_MB_READ_CFGFILE_REQ = 549,
	MSG_SMS_MB_READ_CFGFILE_RES = 550,
	MSG_SMS_RD_MEM_REQ = 552,
	MSG_SMS_RD_MEM_RES = 553,
	MSG_SMS_WR_MEM_REQ = 554,
	MSG_SMS_WR_MEM_RES = 555,
	MSG_SMS_UPDATE_MEM_REQ = 556,
	MSG_SMS_UPDATE_MEM_RES = 557,
	MSG_SMS_ISDBT_ENABLE_FULL_PARAMS_SET_REQ = 558,
	MSG_SMS_ISDBT_ENABLE_FULL_PARAMS_SET_RES = 559,
	MSG_SMS_RF_TUNE_REQ = 561,
	MSG_SMS_RF_TUNE_RES = 562,
	MSG_SMS_ISDBT_ENABLE_HIGH_MOBILITY_REQ = 563,
	MSG_SMS_ISDBT_ENABLE_HIGH_MOBILITY_RES = 564,
	MSG_SMS_ISDBT_SB_RECEPTION_REQ = 565,
	MSG_SMS_ISDBT_SB_RECEPTION_RES = 566,
	MSG_SMS_GENERIC_EPROM_WRITE_REQ = 567,
	MSG_SMS_GENERIC_EPROM_WRITE_RES = 568,
	MSG_SMS_GENERIC_EPROM_READ_REQ = 569,
	MSG_SMS_GENERIC_EPROM_READ_RES = 570,
	MSG_SMS_EEPROM_WRITE_REQ = 571,
	MSG_SMS_EEPROM_WRITE_RES = 572,
	MSG_SMS_CUSTOM_READ_REQ = 574,
	MSG_SMS_CUSTOM_READ_RES = 575,
	MSG_SMS_CUSTOM_WRITE_REQ = 576,
	MSG_SMS_CUSTOM_WRITE_RES = 577,
	MSG_SMS_INIT_DEVICE_REQ = 578,
	MSG_SMS_INIT_DEVICE_RES = 579,
	MSG_SMS_ATSC_SET_ALL_IP_REQ = 580,
	MSG_SMS_ATSC_SET_ALL_IP_RES = 581,
	MSG_SMS_ATSC_START_ENSEMBLE_REQ = 582,
	MSG_SMS_ATSC_START_ENSEMBLE_RES = 583,
	MSG_SMS_SET_OUTPUT_MODE_REQ = 584,
	MSG_SMS_SET_OUTPUT_MODE_RES = 585,
	MSG_SMS_ATSC_IP_FILTER_GET_LIST_REQ = 586,
	MSG_SMS_ATSC_IP_FILTER_GET_LIST_RES = 587,
	MSG_SMS_SUB_CHANNEL_START_REQ = 589,
	MSG_SMS_SUB_CHANNEL_START_RES = 590,
	MSG_SMS_SUB_CHANNEL_STOP_REQ = 591,
	MSG_SMS_SUB_CHANNEL_STOP_RES = 592,
	MSG_SMS_ATSC_IP_FILTER_ADD_REQ = 593,
	MSG_SMS_ATSC_IP_FILTER_ADD_RES = 594,
	MSG_SMS_ATSC_IP_FILTER_REMOVE_REQ = 595,
	MSG_SMS_ATSC_IP_FILTER_REMOVE_RES = 596,
	MSG_SMS_ATSC_IP_FILTER_REMOVE_ALL_REQ = 597,
	MSG_SMS_ATSC_IP_FILTER_REMOVE_ALL_RES = 598,
	MSG_SMS_WAIT_CMD = 599,
	MSG_SMS_ADD_PID_FILTER_REQ = 601,
	MSG_SMS_ADD_PID_FILTER_RES = 602,
	MSG_SMS_REMOVE_PID_FILTER_REQ = 603,
	MSG_SMS_REMOVE_PID_FILTER_RES = 604,
	MSG_SMS_FAST_INFORMATION_CHANNEL_REQ = 605,
	MSG_SMS_FAST_INFORMATION_CHANNEL_RES = 606,
	MSG_SMS_DAB_CHANNEL = 607,
	MSG_SMS_GET_PID_FILTER_LIST_REQ = 608,
	MSG_SMS_GET_PID_FILTER_LIST_RES = 609,
	MSG_SMS_POWER_DOWN_REQ = 610,
	MSG_SMS_POWER_DOWN_RES = 611,
	MSG_SMS_ATSC_SLT_EXIST_IND = 612,
	MSG_SMS_ATSC_NO_SLT_IND = 613,
	MSG_SMS_GET_STATISTICS_REQ = 615,
	MSG_SMS_GET_STATISTICS_RES = 616,
	MSG_SMS_SEND_DUMP = 617,
	MSG_SMS_SCAN_START_REQ = 618,
	MSG_SMS_SCAN_START_RES = 619,
	MSG_SMS_SCAN_STOP_REQ = 620,
	MSG_SMS_SCAN_STOP_RES = 621,
	MSG_SMS_SCAN_PROGRESS_IND = 622,
	MSG_SMS_SCAN_COMPLETE_IND = 623,
	MSG_SMS_LOG_ITEM = 624,
	MSG_SMS_DAB_SUBCHANNEL_RECONFIG_REQ = 628,
	MSG_SMS_DAB_SUBCHANNEL_RECONFIG_RES = 629,
	MSG_SMS_HO_PER_SLICES_IND = 630,
	MSG_SMS_HO_INBAND_POWER_IND = 631,
	MSG_SMS_MANUAL_DEMOD_REQ = 632,
	MSG_SMS_HO_TUNE_ON_REQ = 636,
	MSG_SMS_HO_TUNE_ON_RES = 637,
	MSG_SMS_HO_TUNE_OFF_REQ = 638,
	MSG_SMS_HO_TUNE_OFF_RES = 639,
	MSG_SMS_HO_PEEK_FREQ_REQ = 640,
	MSG_SMS_HO_PEEK_FREQ_RES = 641,
	MSG_SMS_HO_PEEK_FREQ_IND = 642,
	MSG_SMS_MB_ATTEN_SET_REQ = 643,
	MSG_SMS_MB_ATTEN_SET_RES = 644,
	MSG_SMS_ENABLE_STAT_IN_I2C_REQ = 649,
	MSG_SMS_ENABLE_STAT_IN_I2C_RES = 650,
	MSG_SMS_SET_ANTENNA_CONFIG_REQ = 651,
	MSG_SMS_SET_ANTENNA_CONFIG_RES = 652,
	MSG_SMS_GET_STATISTICS_EX_REQ = 653,
	MSG_SMS_GET_STATISTICS_EX_RES = 654,
	MSG_SMS_SLEEP_RESUME_COMP_IND = 655,
	MSG_SMS_SWITCH_HOST_INTERFACE_REQ = 656,
	MSG_SMS_SWITCH_HOST_INTERFACE_RES = 657,
	MSG_SMS_DATA_DOWNLOAD_REQ = 660,
	MSG_SMS_DATA_DOWNLOAD_RES = 661,
	MSG_SMS_DATA_VALIDITY_REQ = 662,
	MSG_SMS_DATA_VALIDITY_RES = 663,
	MSG_SMS_SWDOWNLOAD_TRIGGER_REQ = 664,
	MSG_SMS_SWDOWNLOAD_TRIGGER_RES = 665,
	MSG_SMS_SWDOWNLOAD_BACKDOOR_REQ = 666,
	MSG_SMS_SWDOWNLOAD_BACKDOOR_RES = 667,
	MSG_SMS_GET_VERSION_EX_REQ = 668,
	MSG_SMS_GET_VERSION_EX_RES = 669,
	MSG_SMS_CLOCK_OUTPUT_CONFIG_REQ = 670,
	MSG_SMS_CLOCK_OUTPUT_CONFIG_RES = 671,
	MSG_SMS_I2C_SET_FREQ_REQ = 685,
	MSG_SMS_I2C_SET_FREQ_RES = 686,
	MSG_SMS_GENERIC_I2C_REQ = 687,
	MSG_SMS_GENERIC_I2C_RES = 688,
	MSG_SMS_DVBT_BDA_DATA = 693,
	MSG_SW_RELOAD_REQ = 697,
	MSG_SMS_DATA_MSG = 699,
	MSG_TABLE_UPLOAD_REQ = 700,
	MSG_TABLE_UPLOAD_RES = 701,
	MSG_SW_RELOAD_START_REQ = 702,
	MSG_SW_RELOAD_START_RES = 703,
	MSG_SW_RELOAD_EXEC_REQ = 704,
	MSG_SW_RELOAD_EXEC_RES = 705,
	MSG_SMS_SPI_INT_LINE_SET_REQ = 710,
	MSG_SMS_SPI_INT_LINE_SET_RES = 711,
	MSG_SMS_GPIO_CONFIG_EX_REQ = 712,
	MSG_SMS_GPIO_CONFIG_EX_RES = 713,
	MSG_SMS_WATCHDOG_ACT_REQ = 716,
	MSG_SMS_WATCHDOG_ACT_RES = 717,
	MSG_SMS_LOOPBACK_REQ = 718,
	MSG_SMS_LOOPBACK_RES = 719,
	MSG_SMS_RAW_CAPTURE_START_REQ = 720,
	MSG_SMS_RAW_CAPTURE_START_RES = 721,
	MSG_SMS_RAW_CAPTURE_ABORT_REQ = 722,
	MSG_SMS_RAW_CAPTURE_ABORT_RES = 723,
	MSG_SMS_RAW_CAPTURE_COMPLETE_IND = 728,
	MSG_SMS_DATA_PUMP_IND = 729,
	MSG_SMS_DATA_PUMP_REQ = 730,
	MSG_SMS_DATA_PUMP_RES = 731,
	MSG_SMS_FLASH_DL_REQ = 732,
	MSG_SMS_EXEC_TEST_1_REQ = 734,
	MSG_SMS_EXEC_TEST_1_RES = 735,
	MSG_SMS_ENBALE_TS_INTERFACE_REQ = 736,
	MSG_SMS_ENBALE_TS_INTERFACE_RES = 737,
	MSG_SMS_SPI_SET_BUS_WIDTH_REQ = 738,
	MSG_SMS_SPI_SET_BUS_WIDTH_RES = 739,
	MSG_SMS_SEND_EMM_REQ = 740,
	MSG_SMS_SEND_EMM_RES = 741,
	MSG_SMS_DISABLE_TS_INTERFACE_REQ = 742,
	MSG_SMS_DISABLE_TS_INTERFACE_RES = 743,
	MSG_SMS_IS_BUF_FREE_REQ = 744,
	MSG_SMS_IS_BUF_FREE_RES = 745,
	MSG_SMS_EXT_ANTENNA_REQ = 746,
	MSG_SMS_EXT_ANTENNA_RES = 747,
	MSG_SMS_CMMB_GET_NET_OF_FREQ_REQ_OBSOLETE = 748,
	MSG_SMS_CMMB_GET_NET_OF_FREQ_RES_OBSOLETE = 749,
	MSG_SMS_BATTERY_LEVEL_REQ = 750,
	MSG_SMS_BATTERY_LEVEL_RES = 751,
	MSG_SMS_CMMB_INJECT_TABLE_REQ_OBSOLETE = 752,
	MSG_SMS_CMMB_INJECT_TABLE_RES_OBSOLETE = 753,
	MSG_SMS_FM_RADIO_BLOCK_IND = 754,
	MSG_SMS_HOST_NOTIFICATION_IND = 755,
	MSG_SMS_CMMB_GET_CONTROL_TABLE_REQ_OBSOLETE = 756,
	MSG_SMS_CMMB_GET_CONTROL_TABLE_RES_OBSOLETE = 757,
	MSG_SMS_CMMB_GET_NETWORKS_REQ = 760,
	MSG_SMS_CMMB_GET_NETWORKS_RES = 761,
	MSG_SMS_CMMB_START_SERVICE_REQ = 762,
	MSG_SMS_CMMB_START_SERVICE_RES = 763,
	MSG_SMS_CMMB_STOP_SERVICE_REQ = 764,
	MSG_SMS_CMMB_STOP_SERVICE_RES = 765,
	MSG_SMS_CMMB_ADD_CHANNEL_FILTER_REQ = 768,
	MSG_SMS_CMMB_ADD_CHANNEL_FILTER_RES = 769,
	MSG_SMS_CMMB_REMOVE_CHANNEL_FILTER_REQ = 770,
	MSG_SMS_CMMB_REMOVE_CHANNEL_FILTER_RES = 771,
	MSG_SMS_CMMB_START_CONTROL_INFO_REQ = 772,
	MSG_SMS_CMMB_START_CONTROL_INFO_RES = 773,
	MSG_SMS_CMMB_STOP_CONTROL_INFO_REQ = 774,
	MSG_SMS_CMMB_STOP_CONTROL_INFO_RES = 775,
	MSG_SMS_ISDBT_TUNE_REQ = 776,
	MSG_SMS_ISDBT_TUNE_RES = 777,
	MSG_SMS_TRANSMISSION_IND = 782,
	MSG_SMS_PID_STATISTICS_IND = 783,
	MSG_SMS_POWER_DOWN_IND = 784,
	MSG_SMS_POWER_DOWN_CONF = 785,
	MSG_SMS_POWER_UP_IND = 786,
	MSG_SMS_POWER_UP_CONF = 787,
	MSG_SMS_POWER_MODE_SET_REQ = 790,
	MSG_SMS_POWER_MODE_SET_RES = 791,
	MSG_SMS_DEBUG_HOST_EVENT_REQ = 792,
	MSG_SMS_DEBUG_HOST_EVENT_RES = 793,
	MSG_SMS_NEW_CRYSTAL_REQ = 794,
	MSG_SMS_NEW_CRYSTAL_RES = 795,
	MSG_SMS_CONFIG_SPI_REQ = 796,
	MSG_SMS_CONFIG_SPI_RES = 797,
	MSG_SMS_I2C_SHORT_STAT_IND = 798,
	MSG_SMS_START_IR_REQ = 800,
	MSG_SMS_START_IR_RES = 801,
	MSG_SMS_IR_SAMPLES_IND = 802,
	MSG_SMS_CMMB_CA_SERVICE_IND = 803,
	MSG_SMS_SLAVE_DEVICE_DETECTED = 804,
	MSG_SMS_INTERFACE_LOCK_IND = 805,
	MSG_SMS_INTERFACE_UNLOCK_IND = 806,
	MSG_SMS_SEND_ROSUM_BUFF_REQ = 810,
	MSG_SMS_SEND_ROSUM_BUFF_RES = 811,
	MSG_SMS_ROSUM_BUFF = 812,
	MSG_SMS_SET_AES128_KEY_REQ = 815,
	MSG_SMS_SET_AES128_KEY_RES = 816,
	MSG_SMS_MBBMS_WRITE_REQ = 817,
	MSG_SMS_MBBMS_WRITE_RES = 818,
	MSG_SMS_MBBMS_READ_IND = 819,
	MSG_SMS_IQ_STREAM_START_REQ = 820,
	MSG_SMS_IQ_STREAM_START_RES = 821,
	MSG_SMS_IQ_STREAM_STOP_REQ = 822,
	MSG_SMS_IQ_STREAM_STOP_RES = 823,
	MSG_SMS_IQ_STREAM_DATA_BLOCK = 824,
	MSG_SMS_GET_EEPROM_VERSION_REQ = 825,
	MSG_SMS_GET_EEPROM_VERSION_RES = 826,
	MSG_SMS_SIGNAL_DETECTED_IND = 827,
	MSG_SMS_NO_SIGNAL_IND = 828,
	MSG_SMS_MRC_SHUTDOWN_SLAVE_REQ = 830,
	MSG_SMS_MRC_SHUTDOWN_SLAVE_RES = 831,
	MSG_SMS_MRC_BRINGUP_SLAVE_REQ = 832,
	MSG_SMS_MRC_BRINGUP_SLAVE_RES = 833,
	MSG_SMS_EXTERNAL_LNA_CTRL_REQ = 834,
	MSG_SMS_EXTERNAL_LNA_CTRL_RES = 835,
	MSG_SMS_SET_PERIODIC_STATISTICS_REQ = 836,
	MSG_SMS_SET_PERIODIC_STATISTICS_RES = 837,
	MSG_SMS_CMMB_SET_AUTO_OUTPUT_TS0_REQ = 838,
	MSG_SMS_CMMB_SET_AUTO_OUTPUT_TS0_RES = 839,
	LOCAL_TUNE = 850,
	LOCAL_IFFT_H_ICI = 851,
	MSG_RESYNC_REQ = 852,
	MSG_SMS_CMMB_GET_MRC_STATISTICS_REQ = 853,
	MSG_SMS_CMMB_GET_MRC_STATISTICS_RES = 854,
	MSG_SMS_LOG_EX_ITEM = 855,
	MSG_SMS_DEVICE_DATA_LOSS_IND = 856,
	MSG_SMS_MRC_WATCHDOG_TRIGGERED_IND = 857,
	MSG_SMS_USER_MSG_REQ = 858,
	MSG_SMS_USER_MSG_RES = 859,
	MSG_SMS_SMART_CARD_INIT_REQ = 860,
	MSG_SMS_SMART_CARD_INIT_RES = 861,
	MSG_SMS_SMART_CARD_WRITE_REQ = 862,
	MSG_SMS_SMART_CARD_WRITE_RES = 863,
	MSG_SMS_SMART_CARD_READ_IND = 864,
	MSG_SMS_TSE_ENABLE_REQ = 866,
	MSG_SMS_TSE_ENABLE_RES = 867,
	MSG_SMS_CMMB_GET_SHORT_STATISTICS_REQ = 868,
	MSG_SMS_CMMB_GET_SHORT_STATISTICS_RES = 869,
	MSG_SMS_LED_CONFIG_REQ = 870,
	MSG_SMS_LED_CONFIG_RES = 871,
	MSG_PWM_ANTENNA_REQ = 872,
	MSG_PWM_ANTENNA_RES = 873,
	MSG_SMS_CMMB_SMD_SN_REQ = 874,
	MSG_SMS_CMMB_SMD_SN_RES = 875,
	MSG_SMS_CMMB_SET_CA_CW_REQ = 876,
	MSG_SMS_CMMB_SET_CA_CW_RES = 877,
	MSG_SMS_CMMB_SET_CA_SALT_REQ = 878,
	MSG_SMS_CMMB_SET_CA_SALT_RES = 879,
	MSG_SMS_NSCD_INIT_REQ = 880,
	MSG_SMS_NSCD_INIT_RES = 881,
	MSG_SMS_NSCD_PROCESS_SECTION_REQ = 882,
	MSG_SMS_NSCD_PROCESS_SECTION_RES = 883,
	MSG_SMS_DBD_CREATE_OBJECT_REQ = 884,
	MSG_SMS_DBD_CREATE_OBJECT_RES = 885,
	MSG_SMS_DBD_CONFIGURE_REQ = 886,
	MSG_SMS_DBD_CONFIGURE_RES = 887,
	MSG_SMS_DBD_SET_KEYS_REQ = 888,
	MSG_SMS_DBD_SET_KEYS_RES = 889,
	MSG_SMS_DBD_PROCESS_HEADER_REQ = 890,
	MSG_SMS_DBD_PROCESS_HEADER_RES = 891,
	MSG_SMS_DBD_PROCESS_DATA_REQ = 892,
	MSG_SMS_DBD_PROCESS_DATA_RES = 893,
	MSG_SMS_DBD_PROCESS_GET_DATA_REQ = 894,
	MSG_SMS_DBD_PROCESS_GET_DATA_RES = 895,
	MSG_SMS_NSCD_OPEN_SESSION_REQ = 896,
	MSG_SMS_NSCD_OPEN_SESSION_RES = 897,
	MSG_SMS_SEND_HOST_DATA_TO_DEMUX_REQ = 898,
	MSG_SMS_SEND_HOST_DATA_TO_DEMUX_RES = 899,
	MSG_LAST_MSG_TYPE = 900,
};

#define SMS_INIT_MSG_EX(ptr, type, src, dst, len) do { \
	(ptr)->msgType = type; (ptr)->msgSrcId = src; (ptr)->msgDstId = dst; \
	(ptr)->msgLength = len; (ptr)->msgFlags = 0; \
} while (0)

#define SMS_INIT_MSG(ptr, type, len) \
	SMS_INIT_MSG_EX(ptr, type, 0, HIF_TASK, len)

enum SMS_DVB3_EVENTS {
	DVB3_EVENT_INIT = 0,
	DVB3_EVENT_SLEEP,
	DVB3_EVENT_HOTPLUG,
	DVB3_EVENT_FE_LOCK,
	DVB3_EVENT_FE_UNLOCK,
	DVB3_EVENT_UNC_OK,
	DVB3_EVENT_UNC_ERR
};

enum SMS_DEVICE_MODE {
	DEVICE_MODE_NONE = -1,
	DEVICE_MODE_DVBT = 0,
	DEVICE_MODE_DVBH,
	DEVICE_MODE_DAB_TDMB,
	DEVICE_MODE_DAB_TDMB_DABIP,
	DEVICE_MODE_DVBT_BDA,
	DEVICE_MODE_ISDBT,
	DEVICE_MODE_ISDBT_BDA,
	DEVICE_MODE_CMMB,
	DEVICE_MODE_RAW_TUNER,
	DEVICE_MODE_FM_RADIO,
	DEVICE_MODE_FM_RADIO_BDA,
	DEVICE_MODE_ATSC,
	DEVICE_MODE_MAX,
};

struct SmsMsgHdr_ST {
	u16	msgType;
	u8	msgSrcId;
	u8	msgDstId;
	u16	msgLength; /* Length of entire message, including header */
	u16	msgFlags;
};

struct SmsMsgData_ST {
	struct SmsMsgHdr_ST xMsgHeader;
	u32 msgData[1];
};

struct SmsMsgData_ST2 {
	struct SmsMsgHdr_ST xMsgHeader;
	u32 msgData[2];
};

struct SmsMsgData_ST4 {
	struct SmsMsgHdr_ST xMsgHeader;
	u32 msgData[4];
};

struct SmsDataDownload_ST {
	struct SmsMsgHdr_ST	xMsgHeader;
	u32			MemAddr;
	u8			Payload[SMS_MAX_PAYLOAD_SIZE];
};

struct SmsVersionRes_ST {
	struct SmsMsgHdr_ST	xMsgHeader;

	u16		ChipModel; /* e.g. 0x1102 for SMS-1102 "Nova" */
	u8		Step; /* 0 - Step A */
	u8		MetalFix; /* 0 - Metal 0 */

	/* FirmwareId 0xFF if ROM, otherwise the
	 * value indicated by SMSHOSTLIB_DEVICE_MODES_E */
	u8 FirmwareId;
	/* SupportedProtocols Bitwise OR combination of
					     * supported protocols */
	u8 SupportedProtocols;

	u8		VersionMajor;
	u8		VersionMinor;
	u8		VersionPatch;
	u8		VersionFieldPatch;

	u8		RomVersionMajor;
	u8		RomVersionMinor;
	u8		RomVersionPatch;
	u8		RomVersionFieldPatch;

	u8		TextLabel[34];
};

struct SmsFirmware_ST {
	u32			CheckSum;
	u32			Length;
	u32			StartAddress;
	u8			Payload[1];
};

/* Statistics information returned as response for
 * SmsHostApiGetStatistics_Req */
struct SMSHOSTLIB_STATISTICS_ST {
	u32 Reserved;		/* Reserved */

	/* Common parameters */
	u32 IsRfLocked;		/* 0 - not locked, 1 - locked */
	u32 IsDemodLocked;	/* 0 - not locked, 1 - locked */
	u32 IsExternalLNAOn;	/* 0 - external LNA off, 1 - external LNA on */

	/* Reception quality */
	s32 SNR;		/* dB */
	u32 BER;		/* Post Viterbi BER [1E-5] */
	u32 FIB_CRC;		/* CRC errors percentage, valid only for DAB */
	u32 TS_PER;		/* Transport stream PER,
	0xFFFFFFFF indicate N/A, valid only for DVB-T/H */
	u32 MFER;		/* DVB-H frame error rate in percentage,
	0xFFFFFFFF indicate N/A, valid only for DVB-H */
	s32 RSSI;		/* dBm */
	s32 InBandPwr;		/* In band power in dBM */
	s32 CarrierOffset;	/* Carrier Offset in bin/1024 */

	/* Transmission parameters */
	u32 Frequency;		/* Frequency in Hz */
	u32 Bandwidth;		/* Bandwidth in MHz, valid only for DVB-T/H */
	u32 TransmissionMode;	/* Transmission Mode, for DAB modes 1-4,
	for DVB-T/H FFT mode carriers in Kilos */
	u32 ModemState;		/* from SMSHOSTLIB_DVB_MODEM_STATE_ET,
	valid only for DVB-T/H */
	u32 GuardInterval;	/* Guard Interval from
	SMSHOSTLIB_GUARD_INTERVALS_ET, 	valid only for DVB-T/H */
	u32 CodeRate;		/* Code Rate from SMSHOSTLIB_CODE_RATE_ET,
	valid only for DVB-T/H */
	u32 LPCodeRate;		/* Low Priority Code Rate from
	SMSHOSTLIB_CODE_RATE_ET, valid only for DVB-T/H */
	u32 Hierarchy;		/* Hierarchy from SMSHOSTLIB_HIERARCHY_ET,
	valid only for DVB-T/H */
	u32 Constellation;	/* Constellation from
	SMSHOSTLIB_CONSTELLATION_ET, valid only for DVB-T/H */

	/* Burst parameters, valid only for DVB-H */
	u32 BurstSize;		/* Current burst size in bytes,
	valid only for DVB-H */
	u32 BurstDuration;	/* Current burst duration in mSec,
	valid only for DVB-H */
	u32 BurstCycleTime;	/* Current burst cycle time in mSec,
	valid only for DVB-H */
	u32 CalculatedBurstCycleTime;/* Current burst cycle time in mSec,
	as calculated by demodulator, valid only for DVB-H */
	u32 NumOfRows;		/* Number of rows in MPE table,
	valid only for DVB-H */
	u32 NumOfPaddCols;	/* Number of padding columns in MPE table,
	valid only for DVB-H */
	u32 NumOfPunctCols;	/* Number of puncturing columns in MPE table,
	valid only for DVB-H */
	u32 ErrorTSPackets;	/* Number of erroneous
	transport-stream packets */
	u32 TotalTSPackets;	/* Total number of transport-stream packets */
	u32 NumOfValidMpeTlbs;	/* Number of MPE tables which do not include
	errors after MPE RS decoding */
	u32 NumOfInvalidMpeTlbs;/* Number of MPE tables which include errors
	after MPE RS decoding */
	u32 NumOfCorrectedMpeTlbs;/* Number of MPE tables which were
	corrected by MPE RS decoding */
	/* Common params */
	u32 BERErrorCount;	/* Number of errornous SYNC bits. */
	u32 BERBitCount;	/* Total number of SYNC bits. */

	/* Interface information */
	u32 SmsToHostTxErrors;	/* Total number of transmission errors. */

	/* DAB/T-DMB */
	u32 PreBER; 		/* DAB/T-DMB only: Pre Viterbi BER [1E-5] */

	/* DVB-H TPS parameters */
	u32 CellId;		/* TPS Cell ID in bits 15..0, bits 31..16 zero;
	 if set to 0xFFFFFFFF cell_id not yet recovered */
	u32 DvbhSrvIndHP;	/* DVB-H service indication info, bit 1 -
	Time Slicing indicator, bit 0 - MPE-FEC indicator */
	u32 DvbhSrvIndLP;	/* DVB-H service indication info, bit 1 -
	Time Slicing indicator, bit 0 - MPE-FEC indicator */

	u32 NumMPEReceived;	/* DVB-H, Num MPE section received */

	u32 ReservedFields[10];	/* Reserved */
};

struct SmsMsgStatisticsInfo_ST {
	u32 RequestResult;

	struct SMSHOSTLIB_STATISTICS_ST Stat;

	/* Split the calc of the SNR in DAB */
	u32 Signal; /* dB */
	u32 Noise; /* dB */

};

struct SMSHOSTLIB_ISDBT_LAYER_STAT_ST {
	/* Per-layer information */
	u32 CodeRate; /* Code Rate from SMSHOSTLIB_CODE_RATE_ET,
		       * 255 means layer does not exist */
	u32 Constellation; /* Constellation from SMSHOSTLIB_CONSTELLATION_ET,
			    * 255 means layer does not exist */
	u32 BER; /* Post Viterbi BER [1E-5], 0xFFFFFFFF indicate N/A */
	u32 BERErrorCount; /* Post Viterbi Error Bits Count */
	u32 BERBitCount; /* Post Viterbi Total Bits Count */
	u32 PreBER; /* Pre Viterbi BER [1E-5], 0xFFFFFFFF indicate N/A */
	u32 TS_PER; /* Transport stream PER [%], 0xFFFFFFFF indicate N/A */
	u32 ErrorTSPackets; /* Number of erroneous transport-stream packets */
	u32 TotalTSPackets; /* Total number of transport-stream packets */
	u32 TILdepthI; /* Time interleaver depth I parameter,
			* 255 means layer does not exist */
	u32 NumberOfSegments; /* Number of segments in layer A,
			       * 255 means layer does not exist */
	u32 TMCCErrors; /* TMCC errors */
};

struct SMSHOSTLIB_STATISTICS_ISDBT_ST {
	u32 StatisticsType; /* Enumerator identifying the type of the
				* structure.  Values are the same as
				* SMSHOSTLIB_DEVICE_MODES_E
				*
				* This field MUST always be first in any
				* statistics structure */

	u32 FullSize; /* Total size of the structure returned by the modem.
		       * If the size requested by the host is smaller than
		       * FullSize, the struct will be truncated */

	/* Common parameters */
	u32 IsRfLocked; /* 0 - not locked, 1 - locked */
	u32 IsDemodLocked; /* 0 - not locked, 1 - locked */
	u32 IsExternalLNAOn; /* 0 - external LNA off, 1 - external LNA on */

	/* Reception quality */
	s32  SNR; /* dB */
	s32  RSSI; /* dBm */
	s32  InBandPwr; /* In band power in dBM */
	s32  CarrierOffset; /* Carrier Offset in Hz */

	/* Transmission parameters */
	u32 Frequency; /* Frequency in Hz */
	u32 Bandwidth; /* Bandwidth in MHz */
	u32 TransmissionMode; /* ISDB-T transmission mode */
	u32 ModemState; /* 0 - Acquisition, 1 - Locked */
	u32 GuardInterval; /* Guard Interval, 1 divided by value */
	u32 SystemType; /* ISDB-T system type (ISDB-T / ISDB-Tsb) */
	u32 PartialReception; /* TRUE - partial reception, FALSE otherwise */
	u32 NumOfLayers; /* Number of ISDB-T layers in the network */

	/* Per-layer information */
	/* Layers A, B and C */
	struct SMSHOSTLIB_ISDBT_LAYER_STAT_ST	LayerInfo[3];
	/* Per-layer statistics, see SMSHOSTLIB_ISDBT_LAYER_STAT_ST */

	/* Interface information */
	u32 SmsToHostTxErrors; /* Total number of transmission errors. */
};

struct SMSHOSTLIB_STATISTICS_ISDBT_EX_ST {
	u32 StatisticsType; /* Enumerator identifying the type of the
				* structure.  Values are the same as
				* SMSHOSTLIB_DEVICE_MODES_E
				*
				* This field MUST always be first in any
				* statistics structure */

	u32 FullSize; /* Total size of the structure returned by the modem.
		       * If the size requested by the host is smaller than
		       * FullSize, the struct will be truncated */

	/* Common parameters */
	u32 IsRfLocked; /* 0 - not locked, 1 - locked */
	u32 IsDemodLocked; /* 0 - not locked, 1 - locked */
	u32 IsExternalLNAOn; /* 0 - external LNA off, 1 - external LNA on */

	/* Reception quality */
	s32  SNR; /* dB */
	s32  RSSI; /* dBm */
	s32  InBandPwr; /* In band power in dBM */
	s32  CarrierOffset; /* Carrier Offset in Hz */

	/* Transmission parameters */
	u32 Frequency; /* Frequency in Hz */
	u32 Bandwidth; /* Bandwidth in MHz */
	u32 TransmissionMode; /* ISDB-T transmission mode */
	u32 ModemState; /* 0 - Acquisition, 1 - Locked */
	u32 GuardInterval; /* Guard Interval, 1 divided by value */
	u32 SystemType; /* ISDB-T system type (ISDB-T / ISDB-Tsb) */
	u32 PartialReception; /* TRUE - partial reception, FALSE otherwise */
	u32 NumOfLayers; /* Number of ISDB-T layers in the network */

	u32 SegmentNumber; /* Segment number for ISDB-Tsb */
	u32 TuneBW;	   /* Tuned bandwidth - BW_ISDBT_1SEG / BW_ISDBT_3SEG */

	/* Per-layer information */
	/* Layers A, B and C */
	struct SMSHOSTLIB_ISDBT_LAYER_STAT_ST	LayerInfo[3];
	/* Per-layer statistics, see SMSHOSTLIB_ISDBT_LAYER_STAT_ST */

	/* Interface information */
	u32 Reserved1;    /* Was SmsToHostTxErrors - obsolete . */
 /* Proprietary information */
	u32 ExtAntenna;    /* Obsolete field. */
	u32 ReceptionQuality;
	u32 EwsAlertActive;   /* Signals if EWS alert is currently on */
	u32 LNAOnOff;	/* Internal LNA state: 0: OFF, 1: ON */

	u32 RfAgcLevel;	 /* RF AGC Level [linear units], full gain = 65535 (20dB) */
	u32 BbAgcLevel;    /* Baseband AGC level [linear units], full gain = 65535 (71.5dB) */
	u32 FwErrorsCounter;   /* Application errors - should be always zero */
	u8 FwErrorsHistoryArr[8]; /* Last FW errors IDs - first is most recent, last is oldest */

	s32  MRC_SNR;     /* dB */
	u32 SNRFullRes;    /* dB x 65536 */
	u32 Reserved4[4];
};


struct PID_STATISTICS_DATA_S {
	struct PID_BURST_S {
		u32 size;
		u32 padding_cols;
		u32 punct_cols;
		u32 duration;
		u32 cycle;
		u32 calc_cycle;
	} burst;

	u32 tot_tbl_cnt;
	u32 invalid_tbl_cnt;
	u32 tot_cor_tbl;
};

struct PID_DATA_S {
	u32 pid;
	u32 num_rows;
	struct PID_STATISTICS_DATA_S pid_statistics;
};

#define CORRECT_STAT_RSSI(_stat) ((_stat).RSSI *= -1)
#define CORRECT_STAT_BANDWIDTH(_stat) (_stat.Bandwidth = 8 - _stat.Bandwidth)
#define CORRECT_STAT_TRANSMISSON_MODE(_stat) \
	if (_stat.TransmissionMode == 0) \
		_stat.TransmissionMode = 2; \
	else if (_stat.TransmissionMode == 1) \
		_stat.TransmissionMode = 8; \
		else \
			_stat.TransmissionMode = 4;

struct TRANSMISSION_STATISTICS_S {
	u32 Frequency;		/* Frequency in Hz */
	u32 Bandwidth;		/* Bandwidth in MHz */
	u32 TransmissionMode;	/* FFT mode carriers in Kilos */
	u32 GuardInterval;	/* Guard Interval from
	SMSHOSTLIB_GUARD_INTERVALS_ET */
	u32 CodeRate;		/* Code Rate from SMSHOSTLIB_CODE_RATE_ET */
	u32 LPCodeRate;		/* Low Priority Code Rate from
	SMSHOSTLIB_CODE_RATE_ET */
	u32 Hierarchy;		/* Hierarchy from SMSHOSTLIB_HIERARCHY_ET */
	u32 Constellation;	/* Constellation from
	SMSHOSTLIB_CONSTELLATION_ET */

	/* DVB-H TPS parameters */
	u32 CellId;		/* TPS Cell ID in bits 15..0, bits 31..16 zero;
	 if set to 0xFFFFFFFF cell_id not yet recovered */
	u32 DvbhSrvIndHP;	/* DVB-H service indication info, bit 1 -
	 Time Slicing indicator, bit 0 - MPE-FEC indicator */
	u32 DvbhSrvIndLP;	/* DVB-H service indication info, bit 1 -
	 Time Slicing indicator, bit 0 - MPE-FEC indicator */
	u32 IsDemodLocked;	/* 0 - not locked, 1 - locked */
};

struct RECEPTION_STATISTICS_S {
	u32 IsRfLocked;		/* 0 - not locked, 1 - locked */
	u32 IsDemodLocked;	/* 0 - not locked, 1 - locked */
	u32 IsExternalLNAOn;	/* 0 - external LNA off, 1 - external LNA on */

	u32 ModemState;		/* from SMSHOSTLIB_DVB_MODEM_STATE_ET */
	s32 SNR;		/* dB */
	u32 BER;		/* Post Viterbi BER [1E-5] */
	u32 BERErrorCount;	/* Number of erronous SYNC bits. */
	u32 BERBitCount;	/* Total number of SYNC bits. */
	u32 TS_PER;		/* Transport stream PER,
	0xFFFFFFFF indicate N/A */
	u32 MFER;		/* DVB-H frame error rate in percentage,
	0xFFFFFFFF indicate N/A, valid only for DVB-H */
	s32 RSSI;		/* dBm */
	s32 InBandPwr;		/* In band power in dBM */
	s32 CarrierOffset;	/* Carrier Offset in bin/1024 */
	u32 ErrorTSPackets;	/* Number of erroneous
	transport-stream packets */
	u32 TotalTSPackets;	/* Total number of transport-stream packets */

	s32 MRC_SNR;		/* dB */
	s32 MRC_RSSI;		/* dBm */
	s32 MRC_InBandPwr;	/* In band power in dBM */
};

struct RECEPTION_STATISTICS_EX_S {
	u32 IsRfLocked;		/* 0 - not locked, 1 - locked */
	u32 IsDemodLocked;	/* 0 - not locked, 1 - locked */
	u32 IsExternalLNAOn;	/* 0 - external LNA off, 1 - external LNA on */

	u32 ModemState;		/* from SMSHOSTLIB_DVB_MODEM_STATE_ET */
	s32 SNR;		/* dB */
	u32 BER;		/* Post Viterbi BER [1E-5] */
	u32 BERErrorCount;	/* Number of erronous SYNC bits. */
	u32 BERBitCount;	/* Total number of SYNC bits. */
	u32 TS_PER;		/* Transport stream PER,
	0xFFFFFFFF indicate N/A */
	u32 MFER;		/* DVB-H frame error rate in percentage,
	0xFFFFFFFF indicate N/A, valid only for DVB-H */
	s32 RSSI;		/* dBm */
	s32 InBandPwr;		/* In band power in dBM */
	s32 CarrierOffset;	/* Carrier Offset in bin/1024 */
	u32 ErrorTSPackets;	/* Number of erroneous
	transport-stream packets */
	u32 TotalTSPackets;	/* Total number of transport-stream packets */

	s32  RefDevPPM;
	s32  FreqDevHz;

	s32 MRC_SNR;		/* dB */
	s32 MRC_RSSI;		/* dBm */
	s32 MRC_InBandPwr;	/* In band power in dBM */
};


/* Statistics information returned as response for
 * SmsHostApiGetStatisticsEx_Req for DVB applications, SMS1100 and up */
struct SMSHOSTLIB_STATISTICS_DVB_S {
	/* Reception */
	struct RECEPTION_STATISTICS_S ReceptionData;

	/* Transmission parameters */
	struct TRANSMISSION_STATISTICS_S TransmissionData;

	/* Burst parameters, valid only for DVB-H */
#define	SRVM_MAX_PID_FILTERS 8
	struct PID_DATA_S PidData[SRVM_MAX_PID_FILTERS];
};

/* Statistics information returned as response for
 * SmsHostApiGetStatisticsEx_Req for DVB applications, SMS1100 and up */
struct SMSHOSTLIB_STATISTICS_DVB_EX_S {
	/* Reception */
	struct RECEPTION_STATISTICS_EX_S ReceptionData;

	/* Transmission parameters */
	struct TRANSMISSION_STATISTICS_S TransmissionData;

	/* Burst parameters, valid only for DVB-H */
#define	SRVM_MAX_PID_FILTERS 8
	struct PID_DATA_S PidData[SRVM_MAX_PID_FILTERS];
};

struct SRVM_SIGNAL_STATUS_S {
	u32 result;
	u32 snr;
	u32 tsPackets;
	u32 etsPackets;
	u32 constellation;
	u32 hpCode;
	u32 tpsSrvIndLP;
	u32 tpsSrvIndHP;
	u32 cellId;
	u32 reason;

	s32 inBandPower;
	u32 requestId;
};

struct SMSHOSTLIB_I2C_REQ_ST {
	u32	DeviceAddress; /* I2c device address */
	u32	WriteCount; /* number of bytes to write */
	u32	ReadCount; /* number of bytes to read */
	u8	Data[1];
};

struct SMSHOSTLIB_I2C_RES_ST {
	u32	Status; /* non-zero value in case of failure */
	u32	ReadCount; /* number of bytes read */
	u8	Data[1];
};


struct smscore_config_gpio {
#define SMS_GPIO_DIRECTION_INPUT  0
#define SMS_GPIO_DIRECTION_OUTPUT 1
	u8 direction;

#define SMS_GPIO_PULLUPDOWN_NONE     0
#define SMS_GPIO_PULLUPDOWN_PULLDOWN 1
#define SMS_GPIO_PULLUPDOWN_PULLUP   2
#define SMS_GPIO_PULLUPDOWN_KEEPER   3
	u8 pullupdown;

#define SMS_GPIO_INPUTCHARACTERISTICS_NORMAL  0
#define SMS_GPIO_INPUTCHARACTERISTICS_SCHMITT 1
	u8 inputcharacteristics;

	/* 10xx */
#define SMS_GPIO_OUTPUT_SLEW_RATE_FAST 0
#define SMS_GPIO_OUTPUT_SLEW_WRATE_SLOW 1

	/* 11xx */
#define SMS_GPIO_OUTPUT_SLEW_RATE_0_45_V_NS	0
#define SMS_GPIO_OUTPUT_SLEW_RATE_0_9_V_NS	1
#define SMS_GPIO_OUTPUT_SLEW_RATE_1_7_V_NS	2
#define SMS_GPIO_OUTPUT_SLEW_RATE_3_3_V_NS	3

	u8 outputslewrate;

	/* 10xx */
#define SMS_GPIO_OUTPUTDRIVING_S_4mA  0
#define SMS_GPIO_OUTPUTDRIVING_S_8mA  1
#define SMS_GPIO_OUTPUTDRIVING_S_12mA 2
#define SMS_GPIO_OUTPUTDRIVING_S_16mA 3

	/* 11xx*/
#define SMS_GPIO_OUTPUTDRIVING_1_5mA	0
#define SMS_GPIO_OUTPUTDRIVING_2_8mA	1
#define SMS_GPIO_OUTPUTDRIVING_4mA	2
#define SMS_GPIO_OUTPUTDRIVING_7mA	3
#define SMS_GPIO_OUTPUTDRIVING_10mA	4
#define SMS_GPIO_OUTPUTDRIVING_11mA	5
#define SMS_GPIO_OUTPUTDRIVING_14mA	6
#define SMS_GPIO_OUTPUTDRIVING_16mA	7

	u8 outputdriving;
};

char *smscore_translate_msg(enum msg_types msgtype);

extern int smscore_registry_getmode(char *devpath);

extern int smscore_register_hotplug(hotplug_t hotplug);
extern void smscore_unregister_hotplug(hotplug_t hotplug);

extern int smscore_register_device(struct smsdevice_params_t *params,
				   struct smscore_device_t **coredev);
extern void smscore_unregister_device(struct smscore_device_t *coredev);

extern int smscore_start_device(struct smscore_device_t *coredev);
extern int smscore_load_firmware(struct smscore_device_t *coredev,
				 char *filename,
				 loadfirmware_t loadfirmware_handler);

extern int smscore_set_device_mode(struct smscore_device_t *coredev, int mode);
extern int smscore_get_device_mode(struct smscore_device_t *coredev);

extern int smscore_register_client(struct smscore_device_t *coredev,
				    struct smsclient_params_t *params,
				    struct smscore_client_t **client);
extern void smscore_unregister_client(struct smscore_client_t *client);

extern int smsclient_sendrequest(struct smscore_client_t *client,
				 void *buffer, size_t size);
extern void smscore_onresponse(struct smscore_device_t *coredev,
			       struct smscore_buffer_t *cb);

extern int smscore_get_common_buffer_size(struct smscore_device_t *coredev);
extern int smscore_map_common_buffer(struct smscore_device_t *coredev,
				      struct vm_area_struct *vma);
extern int smscore_send_fw_file(struct smscore_device_t *coredev,
				u8 *ufwbuf, int size);

extern
struct smscore_buffer_t *smscore_getbuffer(struct smscore_device_t *coredev);
extern void smscore_putbuffer(struct smscore_device_t *coredev,
			      struct smscore_buffer_t *cb);

/* old GPIO management */
int smscore_configure_gpio(struct smscore_device_t *coredev, u32 pin,
			   struct smscore_config_gpio *pinconfig);
int smscore_set_gpio(struct smscore_device_t *coredev, u32 pin, int level);

/* new GPIO management */
extern int smscore_gpio_configure(struct smscore_device_t *coredev, u8 PinNum,
		struct smscore_config_gpio *pGpioConfig);
extern int smscore_gpio_set_level(struct smscore_device_t *coredev, u8 PinNum,
		u8 NewLevel);
extern int smscore_gpio_get_level(struct smscore_device_t *coredev, u8 PinNum,
		u8 *level);

void smscore_set_board_id(struct smscore_device_t *core, int id);
int smscore_get_board_id(struct smscore_device_t *core);

int smscore_led_state(struct smscore_device_t *core, int led);


/* ------------------------------------------------------------------------ */

#define DBG_INFO 1
#define DBG_ADV  2

#define sms_printk(kern, fmt, arg...) \
	printk(kern "%s: " fmt "\n", __func__, ##arg)

#define dprintk(kern, lvl, fmt, arg...) do {\
	if (sms_dbg & lvl) \
		sms_printk(kern, fmt, ##arg); } while (0)

#define sms_log(fmt, arg...) sms_printk(KERN_INFO, fmt, ##arg)
#define sms_err(fmt, arg...) \
	sms_printk(KERN_ERR, "line: %d: " fmt, __LINE__, ##arg)
#define sms_warn(fmt, arg...)  sms_printk(KERN_WARNING, fmt, ##arg)
#define sms_info(fmt, arg...) \
	dprintk(KERN_INFO, DBG_INFO, fmt, ##arg)
#define sms_debug(fmt, arg...) \
	dprintk(KERN_DEBUG, DBG_ADV, fmt, ##arg)


#endif /* __SMS_CORE_API_H__ */
