/*
 * linux/drivers/media/platform/coda/coda_regs.h
 *
 * Copyright (C) 2012 Vista Silicon SL
 *    Javier Martin <javier.martin@vista-silicon.com>
 *    Xavier Duret
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _REGS_CODA_H_
#define _REGS_CODA_H_

/* HW registers */
#define CODA_REG_BIT_CODE_RUN			0x000
#define		CODA_REG_RUN_ENABLE		(1 << 0)
#define CODA_REG_BIT_CODE_DOWN			0x004
#define		CODA_DOWN_ADDRESS_SET(x)	(((x) & 0xffff) << 16)
#define		CODA_DOWN_DATA_SET(x)		((x) & 0xffff)
#define CODA_REG_BIT_HOST_IN_REQ		0x008
#define CODA_REG_BIT_INT_CLEAR			0x00c
#define		CODA_REG_BIT_INT_CLEAR_SET	0x1
#define CODA_REG_BIT_INT_STATUS		0x010
#define CODA_REG_BIT_CODE_RESET		0x014
#define		CODA_REG_RESET_ENABLE		(1 << 0)
#define CODA_REG_BIT_CUR_PC			0x018

/* Static SW registers */
#define CODA_REG_BIT_CODE_BUF_ADDR		0x100
#define CODA_REG_BIT_WORK_BUF_ADDR		0x104
#define CODA_REG_BIT_PARA_BUF_ADDR		0x108
#define CODA_REG_BIT_STREAM_CTRL		0x10c
#define		CODA7_STREAM_BUF_PIC_RESET	(1 << 4)
#define		CODADX6_STREAM_BUF_PIC_RESET	(1 << 3)
#define		CODA7_STREAM_BUF_PIC_FLUSH	(1 << 3)
#define		CODADX6_STREAM_BUF_PIC_FLUSH	(1 << 2)
#define		CODA7_STREAM_BUF_DYNALLOC_EN	(1 << 5)
#define		CODADX6_STREAM_BUF_DYNALLOC_EN	(1 << 4)
#define 	CODA_STREAM_CHKDIS_OFFSET	(1 << 1)
#define		CODA_STREAM_ENDIAN_SELECT	(1 << 0)
#define CODA_REG_BIT_FRAME_MEM_CTRL		0x110
#define		CODA_IMAGE_ENDIAN_SELECT	(1 << 0)
#define CODA_REG_BIT_RD_PTR(x)			(0x120 + 8 * (x))
#define CODA_REG_BIT_WR_PTR(x)			(0x124 + 8 * (x))
#define CODADX6_REG_BIT_SEARCH_RAM_BASE_ADDR	0x140
#define CODA7_REG_BIT_AXI_SRAM_USE		0x140
#define		CODA7_USE_BIT_ENABLE		(1 << 0)
#define		CODA7_USE_HOST_BIT_ENABLE	(1 << 7)
#define		CODA7_USE_ME_ENABLE		(1 << 4)
#define		CODA7_USE_HOST_ME_ENABLE	(1 << 11)
#define CODA_REG_BIT_BUSY			0x160
#define		CODA_REG_BIT_BUSY_FLAG		1
#define CODA_REG_BIT_RUN_COMMAND		0x164
#define		CODA_COMMAND_SEQ_INIT		1
#define		CODA_COMMAND_SEQ_END		2
#define		CODA_COMMAND_PIC_RUN		3
#define		CODA_COMMAND_SET_FRAME_BUF	4
#define		CODA_COMMAND_ENCODE_HEADER	5
#define		CODA_COMMAND_ENC_PARA_SET	6
#define		CODA_COMMAND_DEC_PARA_SET	7
#define		CODA_COMMAND_DEC_BUF_FLUSH	8
#define		CODA_COMMAND_RC_CHANGE_PARAMETER 9
#define		CODA_COMMAND_FIRMWARE_GET	0xf
#define CODA_REG_BIT_RUN_INDEX			0x168
#define		CODA_INDEX_SET(x)		((x) & 0x3)
#define CODA_REG_BIT_RUN_COD_STD		0x16c
#define		CODADX6_MODE_DECODE_MP4		0
#define		CODADX6_MODE_ENCODE_MP4		1
#define		CODADX6_MODE_DECODE_H264	2
#define		CODADX6_MODE_ENCODE_H264	3
#define		CODA7_MODE_DECODE_H264		0
#define		CODA7_MODE_DECODE_VC1		1
#define		CODA7_MODE_DECODE_MP2		2
#define		CODA7_MODE_DECODE_MP4		3
#define		CODA7_MODE_DECODE_DV3		3
#define		CODA7_MODE_DECODE_RV		4
#define		CODA7_MODE_DECODE_MJPG		5
#define		CODA7_MODE_ENCODE_H264		8
#define		CODA7_MODE_ENCODE_MP4		11
#define		CODA7_MODE_ENCODE_MJPG		13
#define 	CODA_MODE_INVALID		0xffff
#define CODA_REG_BIT_INT_ENABLE		0x170
#define		CODA_INT_INTERRUPT_ENABLE	(1 << 3)

/*
 * Commands' mailbox:
 * registers with offsets in the range 0x180-0x1d0
 * have different meaning depending on the command being
 * issued.
 */

/* Encoder Sequence Initialization */
#define CODA_CMD_ENC_SEQ_BB_START				0x180
#define CODA_CMD_ENC_SEQ_BB_SIZE				0x184
#define CODA_CMD_ENC_SEQ_OPTION				0x188
#define		CODA7_OPTION_GAMMA_OFFSET			8
#define		CODADX6_OPTION_GAMMA_OFFSET			7
#define		CODA_OPTION_LIMITQP_OFFSET			6
#define		CODA_OPTION_RCINTRAQP_OFFSET			5
#define		CODA_OPTION_FMO_OFFSET				4
#define		CODA_OPTION_SLICEREPORT_OFFSET			1
#define CODA_CMD_ENC_SEQ_COD_STD				0x18c
#define		CODA_STD_MPEG4					0
#define		CODA_STD_H263					1
#define		CODA_STD_H264					2
#define		CODA_STD_MJPG					3
#define CODA_CMD_ENC_SEQ_SRC_SIZE				0x190
#define		CODA7_PICWIDTH_OFFSET				16
#define		CODA7_PICWIDTH_MASK				0xffff
#define		CODADX6_PICWIDTH_OFFSET				10
#define		CODADX6_PICWIDTH_MASK				0x3ff
#define		CODA_PICHEIGHT_OFFSET				0
#define		CODA_PICHEIGHT_MASK				0x3ff
#define CODA_CMD_ENC_SEQ_SRC_F_RATE				0x194
#define CODA_CMD_ENC_SEQ_MP4_PARA				0x198
#define		CODA_MP4PARAM_VERID_OFFSET			6
#define		CODA_MP4PARAM_VERID_MASK			0x01
#define		CODA_MP4PARAM_INTRADCVLCTHR_OFFSET		2
#define		CODA_MP4PARAM_INTRADCVLCTHR_MASK		0x07
#define		CODA_MP4PARAM_REVERSIBLEVLCENABLE_OFFSET	1
#define		CODA_MP4PARAM_REVERSIBLEVLCENABLE_MASK		0x01
#define		CODA_MP4PARAM_DATAPARTITIONENABLE_OFFSET	0
#define		CODA_MP4PARAM_DATAPARTITIONENABLE_MASK		0x01
#define CODA_CMD_ENC_SEQ_263_PARA				0x19c
#define		CODA_263PARAM_ANNEXJENABLE_OFFSET		2
#define		CODA_263PARAM_ANNEXJENABLE_MASK		0x01
#define		CODA_263PARAM_ANNEXKENABLE_OFFSET		1
#define		CODA_263PARAM_ANNEXKENABLE_MASK		0x01
#define		CODA_263PARAM_ANNEXTENABLE_OFFSET		0
#define		CODA_263PARAM_ANNEXTENABLE_MASK		0x01
#define CODA_CMD_ENC_SEQ_264_PARA				0x1a0
#define		CODA_264PARAM_DEBLKFILTEROFFSETBETA_OFFSET	12
#define		CODA_264PARAM_DEBLKFILTEROFFSETBETA_MASK	0x0f
#define		CODA_264PARAM_DEBLKFILTEROFFSETALPHA_OFFSET	8
#define		CODA_264PARAM_DEBLKFILTEROFFSETALPHA_MASK	0x0f
#define		CODA_264PARAM_DISABLEDEBLK_OFFSET		6
#define		CODA_264PARAM_DISABLEDEBLK_MASK		0x01
#define		CODA_264PARAM_CONSTRAINEDINTRAPREDFLAG_OFFSET	5
#define		CODA_264PARAM_CONSTRAINEDINTRAPREDFLAG_MASK	0x01
#define		CODA_264PARAM_CHROMAQPOFFSET_OFFSET		0
#define		CODA_264PARAM_CHROMAQPOFFSET_MASK		0x1f
#define CODA_CMD_ENC_SEQ_SLICE_MODE				0x1a4
#define		CODA_SLICING_SIZE_OFFSET			2
#define		CODA_SLICING_SIZE_MASK				0x3fffffff
#define		CODA_SLICING_UNIT_OFFSET			1
#define		CODA_SLICING_UNIT_MASK				0x01
#define		CODA_SLICING_MODE_OFFSET			0
#define		CODA_SLICING_MODE_MASK				0x01
#define CODA_CMD_ENC_SEQ_GOP_SIZE				0x1a8
#define		CODA_GOP_SIZE_OFFSET				0
#define		CODA_GOP_SIZE_MASK				0x3f
#define CODA_CMD_ENC_SEQ_RC_PARA				0x1ac
#define		CODA_RATECONTROL_AUTOSKIP_OFFSET		31
#define		CODA_RATECONTROL_AUTOSKIP_MASK			0x01
#define		CODA_RATECONTROL_INITIALDELAY_OFFSET		16
#define		CODA_RATECONTROL_INITIALDELAY_MASK		0x7f
#define		CODA_RATECONTROL_BITRATE_OFFSET		1
#define		CODA_RATECONTROL_BITRATE_MASK			0x7f
#define		CODA_RATECONTROL_ENABLE_OFFSET			0
#define		CODA_RATECONTROL_ENABLE_MASK			0x01
#define CODA_CMD_ENC_SEQ_RC_BUF_SIZE				0x1b0
#define CODA_CMD_ENC_SEQ_INTRA_REFRESH				0x1b4
#define CODADX6_CMD_ENC_SEQ_FMO					0x1b8
#define		CODA_FMOPARAM_TYPE_OFFSET			4
#define		CODA_FMOPARAM_TYPE_MASK				1
#define		CODA_FMOPARAM_SLICENUM_OFFSET			0
#define		CODA_FMOPARAM_SLICENUM_MASK			0x0f
#define CODA7_CMD_ENC_SEQ_SEARCH_BASE				0x1b8
#define CODA7_CMD_ENC_SEQ_SEARCH_SIZE				0x1bc
#define CODA_CMD_ENC_SEQ_RC_QP_MAX				0x1c8
#define		CODA_QPMAX_OFFSET				0
#define		CODA_QPMAX_MASK					0x3f
#define CODA_CMD_ENC_SEQ_RC_GAMMA				0x1cc
#define		CODA_GAMMA_OFFSET				0
#define		CODA_GAMMA_MASK					0xffff
#define CODA_RET_ENC_SEQ_SUCCESS				0x1c0

/* Encoder Picture Run */
#define CODA_CMD_ENC_PIC_SRC_ADDR_Y	0x180
#define CODA_CMD_ENC_PIC_SRC_ADDR_CB	0x184
#define CODA_CMD_ENC_PIC_SRC_ADDR_CR	0x188
#define CODA_CMD_ENC_PIC_QS		0x18c
#define CODA_CMD_ENC_PIC_ROT_MODE	0x190
#define		CODA_ROT_MIR_ENABLE				(1 << 4)
#define		CODA_ROT_0					(0x0 << 0)
#define		CODA_ROT_90					(0x1 << 0)
#define		CODA_ROT_180					(0x2 << 0)
#define		CODA_ROT_270					(0x3 << 0)
#define		CODA_MIR_NONE					(0x0 << 2)
#define		CODA_MIR_VER					(0x1 << 2)
#define		CODA_MIR_HOR					(0x2 << 2)
#define		CODA_MIR_VER_HOR				(0x3 << 2)
#define CODA_CMD_ENC_PIC_OPTION	0x194
#define CODA_CMD_ENC_PIC_BB_START	0x198
#define CODA_CMD_ENC_PIC_BB_SIZE	0x19c
#define CODA_RET_ENC_PIC_TYPE		0x1c4
#define CODA_RET_ENC_PIC_SLICE_NUM	0x1cc
#define CODA_RET_ENC_PIC_FLAG		0x1d0

/* Set Frame Buffer */
#define CODA_CMD_SET_FRAME_BUF_NUM		0x180
#define CODA_CMD_SET_FRAME_BUF_STRIDE		0x184
#define CODA7_CMD_SET_FRAME_AXI_BIT_ADDR	0x190
#define CODA7_CMD_SET_FRAME_AXI_IPACDC_ADDR	0x194
#define CODA7_CMD_SET_FRAME_AXI_DBKY_ADDR	0x198
#define CODA7_CMD_SET_FRAME_AXI_DBKC_ADDR	0x19c
#define CODA7_CMD_SET_FRAME_AXI_OVL_ADDR	0x1a0
#define CODA7_CMD_SET_FRAME_SOURCE_BUF_STRIDE	0x1a8

/* Encoder Header */
#define CODA_CMD_ENC_HEADER_CODE	0x180
#define		CODA_GAMMA_OFFSET	0
#define		CODA_HEADER_H264_SPS	0
#define		CODA_HEADER_H264_PPS	1
#define		CODA_HEADER_MP4V_VOL	0
#define		CODA_HEADER_MP4V_VOS	1
#define		CODA_HEADER_MP4V_VIS	2
#define CODA_CMD_ENC_HEADER_BB_START	0x184
#define CODA_CMD_ENC_HEADER_BB_SIZE	0x188

/* Get Version */
#define CODA_CMD_FIRMWARE_VERNUM		0x1c0
#define		CODA_FIRMWARE_PRODUCT(x)	(((x) >> 16) & 0xffff)
#define		CODA_FIRMWARE_MAJOR(x)		(((x) >> 12) & 0x0f)
#define		CODA_FIRMWARE_MINOR(x)		(((x) >> 8) & 0x0f)
#define		CODA_FIRMWARE_RELEASE(x)	((x) & 0xff)
#define		CODA_FIRMWARE_VERNUM(product, major, minor, release)	\
			((product) << 16 | ((major) << 12) |		\
			((minor) << 8) | (release))

#endif
