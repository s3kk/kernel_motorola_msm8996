/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <scsi/sas.h>
#include "sas.h"
#include "sci_base_state.h"
#include "sci_base_state_machine.h"
#include "scic_phy.h"
#include "scic_sds_controller.h"
#include "scic_sds_phy.h"
#include "scic_sds_port.h"
#include "remote_node_context.h"
#include "sci_environment.h"
#include "sci_util.h"
#include "scu_event_codes.h"

#define SCIC_SDS_PHY_MIN_TIMER_COUNT  (SCI_MAX_PHYS)
#define SCIC_SDS_PHY_MAX_TIMER_COUNT  (SCI_MAX_PHYS)

/* Maximum arbitration wait time in micro-seconds */
#define SCIC_SDS_PHY_MAX_ARBITRATION_WAIT_TIME  (700)

enum sas_linkrate sci_phy_linkrate(struct scic_sds_phy *sci_phy)
{
	return sci_phy->max_negotiated_speed;
}

/*
 * *****************************************************************************
 * * SCIC SDS PHY Internal Methods
 * ***************************************************************************** */

/**
 * This method will initialize the phy transport layer registers
 * @sci_phy:
 * @transport_layer_registers
 *
 * enum sci_status
 */
static enum sci_status scic_sds_phy_transport_layer_initialization(
	struct scic_sds_phy *sci_phy,
	struct scu_transport_layer_registers __iomem *transport_layer_registers)
{
	u32 tl_control;

	sci_phy->transport_layer_registers = transport_layer_registers;

	writel(SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX,
		&sci_phy->transport_layer_registers->stp_rni);

	/*
	 * Hardware team recommends that we enable the STP prefetch for all
	 * transports
	 */
	tl_control = readl(&sci_phy->transport_layer_registers->control);
	tl_control |= SCU_TLCR_GEN_BIT(STP_WRITE_DATA_PREFETCH);
	writel(tl_control, &sci_phy->transport_layer_registers->control);

	return SCI_SUCCESS;
}

/**
 * This method will initialize the phy link layer registers
 * @sci_phy:
 * @link_layer_registers:
 *
 * enum sci_status
 */
static enum sci_status
scic_sds_phy_link_layer_initialization(struct scic_sds_phy *sci_phy,
				       struct scu_link_layer_registers __iomem *link_layer_registers)
{
	struct scic_sds_controller *scic =
		sci_phy->owning_port->owning_controller;
	int phy_idx = sci_phy->phy_index;
	struct sci_phy_user_params *phy_user =
		&scic->user_parameters.sds1.phys[phy_idx];
	struct sci_phy_oem_params *phy_oem =
		&scic->oem_parameters.sds1.phys[phy_idx];
	u32 phy_configuration;
	struct scic_phy_cap phy_cap;
	u32 parity_check = 0;
	u32 parity_count = 0;
	u32 llctl, link_rate;
	u32 clksm_value = 0;

	sci_phy->link_layer_registers = link_layer_registers;

	/* Set our IDENTIFY frame data */
	#define SCI_END_DEVICE 0x01

	writel(SCU_SAS_TIID_GEN_BIT(SMP_INITIATOR) |
	       SCU_SAS_TIID_GEN_BIT(SSP_INITIATOR) |
	       SCU_SAS_TIID_GEN_BIT(STP_INITIATOR) |
	       SCU_SAS_TIID_GEN_BIT(DA_SATA_HOST) |
	       SCU_SAS_TIID_GEN_VAL(DEVICE_TYPE, SCI_END_DEVICE),
	       &sci_phy->link_layer_registers->transmit_identification);

	/* Write the device SAS Address */
	writel(0xFEDCBA98,
	       &sci_phy->link_layer_registers->sas_device_name_high);
	writel(phy_idx, &sci_phy->link_layer_registers->sas_device_name_low);

	/* Write the source SAS Address */
	writel(phy_oem->sas_address.high,
		&sci_phy->link_layer_registers->source_sas_address_high);
	writel(phy_oem->sas_address.low,
		&sci_phy->link_layer_registers->source_sas_address_low);

	/* Clear and Set the PHY Identifier */
	writel(0, &sci_phy->link_layer_registers->identify_frame_phy_id);
	writel(SCU_SAS_TIPID_GEN_VALUE(ID, phy_idx),
		&sci_phy->link_layer_registers->identify_frame_phy_id);

	/* Change the initial state of the phy configuration register */
	phy_configuration =
		readl(&sci_phy->link_layer_registers->phy_configuration);

	/* Hold OOB state machine in reset */
	phy_configuration |=  SCU_SAS_PCFG_GEN_BIT(OOB_RESET);
	writel(phy_configuration,
		&sci_phy->link_layer_registers->phy_configuration);

	/* Configure the SNW capabilities */
	phy_cap.all = 0;
	phy_cap.start = 1;
	phy_cap.gen3_no_ssc = 1;
	phy_cap.gen2_no_ssc = 1;
	phy_cap.gen1_no_ssc = 1;
	if (scic->oem_parameters.sds1.controller.do_enable_ssc == true) {
		phy_cap.gen3_ssc = 1;
		phy_cap.gen2_ssc = 1;
		phy_cap.gen1_ssc = 1;
	}

	/*
	 * The SAS specification indicates that the phy_capabilities that
	 * are transmitted shall have an even parity.  Calculate the parity. */
	parity_check = phy_cap.all;
	while (parity_check != 0) {
		if (parity_check & 0x1)
			parity_count++;
		parity_check >>= 1;
	}

	/*
	 * If parity indicates there are an odd number of bits set, then
	 * set the parity bit to 1 in the phy capabilities. */
	if ((parity_count % 2) != 0)
		phy_cap.parity = 1;

	writel(phy_cap.all, &sci_phy->link_layer_registers->phy_capabilities);

	/* Set the enable spinup period but disable the ability to send
	 * notify enable spinup
	 */
	writel(SCU_ENSPINUP_GEN_VAL(COUNT,
			phy_user->notify_enable_spin_up_insertion_frequency),
		&sci_phy->link_layer_registers->notify_enable_spinup_control);

	/* Write the ALIGN Insertion Ferequency for connected phy and
	 * inpendent of connected state
	 */
	clksm_value = SCU_ALIGN_INSERTION_FREQUENCY_GEN_VAL(CONNECTED,
			phy_user->in_connection_align_insertion_frequency);

	clksm_value |= SCU_ALIGN_INSERTION_FREQUENCY_GEN_VAL(GENERAL,
			phy_user->align_insertion_frequency);

	writel(clksm_value, &sci_phy->link_layer_registers->clock_skew_management);

	/* @todo Provide a way to write this register correctly */
	writel(0x02108421,
		&sci_phy->link_layer_registers->afe_lookup_table_control);

	llctl = SCU_SAS_LLCTL_GEN_VAL(NO_OUTBOUND_TASK_TIMEOUT,
		(u8)scic->user_parameters.sds1.no_outbound_task_timeout);

	switch(phy_user->max_speed_generation) {
	case SCIC_SDS_PARM_GEN3_SPEED:
		link_rate = SCU_SAS_LINK_LAYER_CONTROL_MAX_LINK_RATE_GEN3;
		break;
	case SCIC_SDS_PARM_GEN2_SPEED:
		link_rate = SCU_SAS_LINK_LAYER_CONTROL_MAX_LINK_RATE_GEN2;
		break;
	default:
		link_rate = SCU_SAS_LINK_LAYER_CONTROL_MAX_LINK_RATE_GEN1;
		break;
	}
	llctl |= SCU_SAS_LLCTL_GEN_VAL(MAX_LINK_RATE, link_rate);
	writel(llctl, &sci_phy->link_layer_registers->link_layer_control);

	if (is_a0() || is_a2()) {
		/* Program the max ARB time for the PHY to 700us so we inter-operate with
		 * the PMC expander which shuts down PHYs if the expander PHY generates too
		 * many breaks.  This time value will guarantee that the initiator PHY will
		 * generate the break.
		 */
		writel(SCIC_SDS_PHY_MAX_ARBITRATION_WAIT_TIME,
			&sci_phy->link_layer_registers->maximum_arbitration_wait_timer_timeout);
	}

	/*
	 * Set the link layer hang detection to 500ms (0x1F4) from its default
	 * value of 128ms.  Max value is 511 ms.
	 */
	writel(0x1F4, &sci_phy->link_layer_registers->link_layer_hang_detection_timeout);

	/* We can exit the initial state to the stopped state */
	sci_base_state_machine_change_state(&sci_phy->state_machine,
					    SCI_BASE_PHY_STATE_STOPPED);

	return SCI_SUCCESS;
}

/**
 * This function will handle the sata SIGNATURE FIS timeout condition.  It will
 * restart the starting substate machine since we dont know what has actually
 * happening.
 */
static void scic_sds_phy_sata_timeout(void *phy)
{
	struct scic_sds_phy *sci_phy = phy;

	dev_dbg(sciphy_to_dev(sci_phy),
		 "%s: SCIC SDS Phy 0x%p did not receive signature fis before "
		 "timeout.\n",
		 __func__,
		 sci_phy);

	sci_base_state_machine_stop(&sci_phy->starting_substate_machine);

	sci_base_state_machine_change_state(&sci_phy->state_machine,
					    SCI_BASE_PHY_STATE_STARTING);
}

/**
 * This method returns the port currently containing this phy. If the phy is
 *    currently contained by the dummy port, then the phy is considered to not
 *    be part of a port.
 * @sci_phy: This parameter specifies the phy for which to retrieve the
 *    containing port.
 *
 * This method returns a handle to a port that contains the supplied phy.
 * NULL This value is returned if the phy is not part of a real
 * port (i.e. it's contained in the dummy port). !NULL All other
 * values indicate a handle/pointer to the port containing the phy.
 */
struct scic_sds_port *scic_sds_phy_get_port(
	struct scic_sds_phy *sci_phy)
{
	if (scic_sds_port_get_index(sci_phy->owning_port) == SCIC_SDS_DUMMY_PORT)
		return NULL;

	return sci_phy->owning_port;
}

/**
 * This method will assign a port to the phy object.
 * @out]: sci_phy This parameter specifies the phy for which to assign a port
 *    object.
 *
 *
 */
void scic_sds_phy_set_port(
	struct scic_sds_phy *sci_phy,
	struct scic_sds_port *sci_port)
{
	sci_phy->owning_port = sci_port;

	if (sci_phy->bcn_received_while_port_unassigned) {
		sci_phy->bcn_received_while_port_unassigned = false;
		scic_sds_port_broadcast_change_received(sci_phy->owning_port, sci_phy);
	}
}

/**
 * This method will initialize the constructed phy
 * @sci_phy:
 * @link_layer_registers:
 *
 * enum sci_status
 */
enum sci_status scic_sds_phy_initialize(
	struct scic_sds_phy *sci_phy,
	struct scu_transport_layer_registers __iomem *transport_layer_registers,
	struct scu_link_layer_registers __iomem *link_layer_registers)
{
	struct scic_sds_controller *scic = scic_sds_phy_get_controller(sci_phy);
	struct isci_host *ihost = scic->ihost;

	/* Create the SIGNATURE FIS Timeout timer for this phy */
	sci_phy->sata_timeout_timer =
		isci_timer_create(
			ihost,
			sci_phy,
			scic_sds_phy_sata_timeout);

	/* Perfrom the initialization of the TL hardware */
	scic_sds_phy_transport_layer_initialization(
			sci_phy,
			transport_layer_registers);

	/* Perofrm the initialization of the PE hardware */
	scic_sds_phy_link_layer_initialization(sci_phy, link_layer_registers);

	/*
	 * There is nothing that needs to be done in this state just
	 * transition to the stopped state. */
	sci_base_state_machine_change_state(&sci_phy->state_machine,
					    SCI_BASE_PHY_STATE_STOPPED);

	return SCI_SUCCESS;
}

/**
 * This method assigns the direct attached device ID for this phy.
 *
 * @sci_phy The phy for which the direct attached device id is to
 *       be assigned.
 * @device_id The direct attached device ID to assign to the phy.
 *       This will either be the RNi for the device or an invalid RNi if there
 *       is no current device assigned to the phy.
 */
void scic_sds_phy_setup_transport(
	struct scic_sds_phy *sci_phy,
	u32 device_id)
{
	u32 tl_control;

	writel(device_id, &sci_phy->transport_layer_registers->stp_rni);

	/*
	 * The read should guarantee that the first write gets posted
	 * before the next write
	 */
	tl_control = readl(&sci_phy->transport_layer_registers->control);
	tl_control |= SCU_TLCR_GEN_BIT(CLEAR_TCI_NCQ_MAPPING_TABLE);
	writel(tl_control, &sci_phy->transport_layer_registers->control);
}

/**
 *
 * @sci_phy: The phy object to be suspended.
 *
 * This function will perform the register reads/writes to suspend the SCU
 * hardware protocol engine. none
 */
static void scic_sds_phy_suspend(
	struct scic_sds_phy *sci_phy)
{
	u32 scu_sas_pcfg_value;

	scu_sas_pcfg_value =
		readl(&sci_phy->link_layer_registers->phy_configuration);
	scu_sas_pcfg_value |= SCU_SAS_PCFG_GEN_BIT(SUSPEND_PROTOCOL_ENGINE);
	writel(scu_sas_pcfg_value,
		&sci_phy->link_layer_registers->phy_configuration);

	scic_sds_phy_setup_transport(
			sci_phy,
			SCIC_SDS_REMOTE_NODE_CONTEXT_INVALID_INDEX);
}

void scic_sds_phy_resume(struct scic_sds_phy *sci_phy)
{
	u32 scu_sas_pcfg_value;

	scu_sas_pcfg_value =
		readl(&sci_phy->link_layer_registers->phy_configuration);
	scu_sas_pcfg_value &= ~SCU_SAS_PCFG_GEN_BIT(SUSPEND_PROTOCOL_ENGINE);
	writel(scu_sas_pcfg_value,
		&sci_phy->link_layer_registers->phy_configuration);
}

void scic_sds_phy_get_sas_address(struct scic_sds_phy *sci_phy,
				  struct sci_sas_address *sas_address)
{
	sas_address->high = readl(&sci_phy->link_layer_registers->source_sas_address_high);
	sas_address->low = readl(&sci_phy->link_layer_registers->source_sas_address_low);
}

void scic_sds_phy_get_attached_sas_address(struct scic_sds_phy *sci_phy,
					   struct sci_sas_address *sas_address)
{
	struct sas_identify_frame *iaf;
	struct isci_phy *iphy = sci_phy->iphy;

	iaf = &iphy->frame_rcvd.iaf;
	memcpy(sas_address, iaf->sas_addr, SAS_ADDR_SIZE);
}

void scic_sds_phy_get_protocols(
	struct scic_sds_phy *sci_phy,
	struct sci_sas_identify_address_frame_protocols *protocols)
{
	protocols->u.all =
		(u16)(readl(&sci_phy->
			link_layer_registers->transmit_identification) &
				0x0000FFFF);
}

void scic_sds_phy_get_attached_phy_protocols(
	struct scic_sds_phy *sci_phy,
	struct sci_sas_identify_address_frame_protocols *protocols)
{
	protocols->u.all = 0;

	if (sci_phy->protocol == SCIC_SDS_PHY_PROTOCOL_SAS) {
		struct isci_phy *iphy = sci_phy->iphy;
		struct sas_identify_frame *iaf;

		iaf = &iphy->frame_rcvd.iaf;
		memcpy(&protocols->u.all, &iaf->initiator_bits, 2);
	} else if (sci_phy->protocol == SCIC_SDS_PHY_PROTOCOL_SATA)
		protocols->u.bits.stp_target = 1;
}

/*
 * *****************************************************************************
 * * SCIC SDS PHY Handler Redirects
 * ***************************************************************************** */

/**
 * This method will attempt to start the phy object. This request is only valid
 *    when the phy is in the stopped state
 * @sci_phy:
 *
 * enum sci_status
 */
enum sci_status scic_sds_phy_start(struct scic_sds_phy *sci_phy)
{
	return sci_phy->state_handlers->start_handler(sci_phy);
}

/**
 * This method will attempt to stop the phy object.
 * @sci_phy:
 *
 * enum sci_status SCI_SUCCESS if the phy is going to stop SCI_INVALID_STATE
 * if the phy is not in a valid state to stop
 */
enum sci_status scic_sds_phy_stop(struct scic_sds_phy *sci_phy)
{
	return sci_phy->state_handlers->stop_handler(sci_phy);
}

/**
 * This method will attempt to reset the phy.  This request is only valid when
 *    the phy is in an ready state
 * @sci_phy:
 *
 * enum sci_status
 */
enum sci_status scic_sds_phy_reset(
	struct scic_sds_phy *sci_phy)
{
	return sci_phy->state_handlers->reset_handler(sci_phy);
}

/**
 * This method will process the event code received.
 * @sci_phy:
 * @event_code:
 *
 * enum sci_status
 */
enum sci_status scic_sds_phy_event_handler(
	struct scic_sds_phy *sci_phy,
	u32 event_code)
{
	return sci_phy->state_handlers->event_handler(sci_phy, event_code);
}

/**
 * This method will process the frame index received.
 * @sci_phy:
 * @frame_index:
 *
 * enum sci_status
 */
enum sci_status scic_sds_phy_frame_handler(
	struct scic_sds_phy *sci_phy,
	u32 frame_index)
{
	return sci_phy->state_handlers->frame_handler(sci_phy, frame_index);
}

/**
 * This method will give the phy permission to consume power
 * @sci_phy:
 *
 * enum sci_status
 */
enum sci_status scic_sds_phy_consume_power_handler(
	struct scic_sds_phy *sci_phy)
{
	return sci_phy->state_handlers->consume_power_handler(sci_phy);
}

/*
 * *****************************************************************************
 * * SCIC SDS PHY HELPER FUNCTIONS
 * ***************************************************************************** */


/**
 *
 * @sci_phy: The phy object that received SAS PHY DETECTED.
 *
 * This method continues the link training for the phy as if it were a SAS PHY
 * instead of a SATA PHY. This is done because the completion queue had a SAS
 * PHY DETECTED event when the state machine was expecting a SATA PHY event.
 * none
 */
static void scic_sds_phy_start_sas_link_training(
	struct scic_sds_phy *sci_phy)
{
	u32 phy_control;

	phy_control =
		readl(&sci_phy->link_layer_registers->phy_configuration);
	phy_control |= SCU_SAS_PCFG_GEN_BIT(SATA_SPINUP_HOLD);
	writel(phy_control,
		&sci_phy->link_layer_registers->phy_configuration);

	sci_base_state_machine_change_state(
		&sci_phy->starting_substate_machine,
		SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SAS_SPEED_EN
		);

	sci_phy->protocol = SCIC_SDS_PHY_PROTOCOL_SAS;
}

/**
 *
 * @sci_phy: The phy object that received a SATA SPINUP HOLD event
 *
 * This method continues the link training for the phy as if it were a SATA PHY
 * instead of a SAS PHY.  This is done because the completion queue had a SATA
 * SPINUP HOLD event when the state machine was expecting a SAS PHY event. none
 */
static void scic_sds_phy_start_sata_link_training(
	struct scic_sds_phy *sci_phy)
{
	sci_base_state_machine_change_state(
		&sci_phy->starting_substate_machine,
		SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_POWER
		);

	sci_phy->protocol = SCIC_SDS_PHY_PROTOCOL_SATA;
}

/**
 * scic_sds_phy_complete_link_training - perform processing common to
 *    all protocols upon completion of link training.
 * @sci_phy: This parameter specifies the phy object for which link training
 *    has completed.
 * @max_link_rate: This parameter specifies the maximum link rate to be
 *    associated with this phy.
 * @next_state: This parameter specifies the next state for the phy's starting
 *    sub-state machine.
 *
 */
static void scic_sds_phy_complete_link_training(
	struct scic_sds_phy *sci_phy,
	enum sas_linkrate max_link_rate,
	u32 next_state)
{
	sci_phy->max_negotiated_speed = max_link_rate;

	sci_base_state_machine_change_state(&sci_phy->starting_substate_machine,
					    next_state);
}

static void scic_sds_phy_restart_starting_state(
	struct scic_sds_phy *sci_phy)
{
	/* Stop the current substate machine */
	sci_base_state_machine_stop(&sci_phy->starting_substate_machine);

	/* Re-enter the base state machine starting state */
	sci_base_state_machine_change_state(&sci_phy->state_machine,
					    SCI_BASE_PHY_STATE_STARTING);
}

/* ****************************************************************************
   * SCIC SDS PHY general handlers
   ************************************************************************** */
static enum sci_status scic_sds_phy_starting_substate_general_stop_handler(
	struct scic_sds_phy *phy)
{
	sci_base_state_machine_stop(&phy->starting_substate_machine);

	sci_base_state_machine_change_state(&phy->state_machine,
						 SCI_BASE_PHY_STATE_STOPPED);

	return SCI_SUCCESS;
}

/*
 * *****************************************************************************
 * * SCIC SDS PHY EVENT_HANDLERS
 * ***************************************************************************** */

/**
 *
 * @phy: This struct scic_sds_phy object which has received an event.
 * @event_code: This is the event code which the phy object is to decode.
 *
 * This method is called when an event notification is received for the phy
 * object when in the state SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SPEED_EN. -
 * decode the event - sas phy detected causes a state transition to the wait
 * for speed event notification. - any other events log a warning message and
 * set a failure status enum sci_status SCI_SUCCESS on any valid event notification
 * SCI_FAILURE on any unexpected event notifation
 */
static enum sci_status scic_sds_phy_starting_substate_await_ossp_event_handler(
	struct scic_sds_phy *sci_phy,
	u32 event_code)
{
	u32 result = SCI_SUCCESS;

	switch (scu_get_event_code(event_code)) {
	case SCU_EVENT_SAS_PHY_DETECTED:
		scic_sds_phy_start_sas_link_training(sci_phy);
		sci_phy->is_in_link_training = true;
		break;

	case SCU_EVENT_SATA_SPINUP_HOLD:
		scic_sds_phy_start_sata_link_training(sci_phy);
		sci_phy->is_in_link_training = true;
		break;

	default:
		dev_dbg(sciphy_to_dev(sci_phy),
			"%s: PHY starting substate machine received "
			"unexpected event_code %x\n",
			__func__,
			event_code);

		result = SCI_FAILURE;
		break;
	}

	return result;
}

/**
 *
 * @phy: This struct scic_sds_phy object which has received an event.
 * @event_code: This is the event code which the phy object is to decode.
 *
 * This method is called when an event notification is received for the phy
 * object when in the state SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SPEED_EN. -
 * decode the event - sas phy detected returns us back to this state. - speed
 * event detected causes a state transition to the wait for iaf. - identify
 * timeout is an un-expected event and the state machine is restarted. - link
 * failure events restart the starting state machine - any other events log a
 * warning message and set a failure status enum sci_status SCI_SUCCESS on any valid
 * event notification SCI_FAILURE on any unexpected event notifation
 */
static enum sci_status scic_sds_phy_starting_substate_await_sas_phy_speed_event_handler(
	struct scic_sds_phy *sci_phy,
	u32 event_code)
{
	u32 result = SCI_SUCCESS;

	switch (scu_get_event_code(event_code)) {
	case SCU_EVENT_SAS_PHY_DETECTED:
		/*
		 * Why is this being reported again by the controller?
		 * We would re-enter this state so just stay here */
		break;

	case SCU_EVENT_SAS_15:
	case SCU_EVENT_SAS_15_SSC:
		scic_sds_phy_complete_link_training(
			sci_phy,
			SAS_LINK_RATE_1_5_GBPS,
			SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_IAF_UF);
		break;

	case SCU_EVENT_SAS_30:
	case SCU_EVENT_SAS_30_SSC:
		scic_sds_phy_complete_link_training(
			sci_phy,
			SAS_LINK_RATE_3_0_GBPS,
			SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_IAF_UF);
		break;

	case SCU_EVENT_SAS_60:
	case SCU_EVENT_SAS_60_SSC:
		scic_sds_phy_complete_link_training(
			sci_phy,
			SAS_LINK_RATE_6_0_GBPS,
			SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_IAF_UF);
		break;

	case SCU_EVENT_SATA_SPINUP_HOLD:
		/*
		 * We were doing SAS PHY link training and received a SATA PHY event
		 * continue OOB/SN as if this were a SATA PHY */
		scic_sds_phy_start_sata_link_training(sci_phy);
		break;

	case SCU_EVENT_LINK_FAILURE:
		/* Link failure change state back to the starting state */
		scic_sds_phy_restart_starting_state(sci_phy);
		break;

	default:
		dev_warn(sciphy_to_dev(sci_phy),
			 "%s: PHY starting substate machine received "
			 "unexpected event_code %x\n",
			 __func__,
			 event_code);

		result = SCI_FAILURE;
		break;
	}

	return result;
}

/**
 *
 * @phy: This struct scic_sds_phy object which has received an event.
 * @event_code: This is the event code which the phy object is to decode.
 *
 * This method is called when an event notification is received for the phy
 * object when in the state SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_IAF_UF. -
 * decode the event - sas phy detected event backs up the state machine to the
 * await speed notification. - identify timeout is an un-expected event and the
 * state machine is restarted. - link failure events restart the starting state
 * machine - any other events log a warning message and set a failure status
 * enum sci_status SCI_SUCCESS on any valid event notification SCI_FAILURE on any
 * unexpected event notifation
 */
static enum sci_status scic_sds_phy_starting_substate_await_iaf_uf_event_handler(
	struct scic_sds_phy *sci_phy,
	u32 event_code)
{
	u32 result = SCI_SUCCESS;

	switch (scu_get_event_code(event_code)) {
	case SCU_EVENT_SAS_PHY_DETECTED:
		/* Backup the state machine */
		scic_sds_phy_start_sas_link_training(sci_phy);
		break;

	case SCU_EVENT_SATA_SPINUP_HOLD:
		/*
		 * We were doing SAS PHY link training and received a SATA PHY event
		 * continue OOB/SN as if this were a SATA PHY */
		scic_sds_phy_start_sata_link_training(sci_phy);
		break;

	case SCU_EVENT_RECEIVED_IDENTIFY_TIMEOUT:
	case SCU_EVENT_LINK_FAILURE:
	case SCU_EVENT_HARD_RESET_RECEIVED:
		/* Start the oob/sn state machine over again */
		scic_sds_phy_restart_starting_state(sci_phy);
		break;

	default:
		dev_warn(sciphy_to_dev(sci_phy),
			 "%s: PHY starting substate machine received "
			 "unexpected event_code %x\n",
			 __func__,
			 event_code);

		result = SCI_FAILURE;
		break;
	}

	return result;
}

/**
 *
 * @phy: This struct scic_sds_phy object which has received an event.
 * @event_code: This is the event code which the phy object is to decode.
 *
 * This method is called when an event notification is received for the phy
 * object when in the state SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_POWER. -
 * decode the event - link failure events restart the starting state machine -
 * any other events log a warning message and set a failure status enum sci_status
 * SCI_SUCCESS on a link failure event SCI_FAILURE on any unexpected event
 * notifation
 */
static enum sci_status scic_sds_phy_starting_substate_await_sas_power_event_handler(
	struct scic_sds_phy *sci_phy,
	u32 event_code)
{
	u32 result = SCI_SUCCESS;

	switch (scu_get_event_code(event_code)) {
	case SCU_EVENT_LINK_FAILURE:
		/* Link failure change state back to the starting state */
		scic_sds_phy_restart_starting_state(sci_phy);
		break;

	default:
		dev_warn(sciphy_to_dev(sci_phy),
			"%s: PHY starting substate machine received unexpected "
			"event_code %x\n",
			__func__,
			event_code);

		result = SCI_FAILURE;
		break;
	}

	return result;
}

/**
 *
 * @phy: This struct scic_sds_phy object which has received an event.
 * @event_code: This is the event code which the phy object is to decode.
 *
 * This method is called when an event notification is received for the phy
 * object when in the state SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_POWER. -
 * decode the event - link failure events restart the starting state machine -
 * sata spinup hold events are ignored since they are expected - any other
 * events log a warning message and set a failure status enum sci_status SCI_SUCCESS
 * on a link failure event SCI_FAILURE on any unexpected event notifation
 */
static enum sci_status scic_sds_phy_starting_substate_await_sata_power_event_handler(
	struct scic_sds_phy *sci_phy,
	u32 event_code)
{
	u32 result = SCI_SUCCESS;

	switch (scu_get_event_code(event_code)) {
	case SCU_EVENT_LINK_FAILURE:
		/* Link failure change state back to the starting state */
		scic_sds_phy_restart_starting_state(sci_phy);
		break;

	case SCU_EVENT_SATA_SPINUP_HOLD:
		/* These events are received every 10ms and are expected while in this state */
		break;

	case SCU_EVENT_SAS_PHY_DETECTED:
		/*
		 * There has been a change in the phy type before OOB/SN for the
		 * SATA finished start down the SAS link traning path. */
		scic_sds_phy_start_sas_link_training(sci_phy);
		break;

	default:
		dev_warn(sciphy_to_dev(sci_phy),
			 "%s: PHY starting substate machine received "
			 "unexpected event_code %x\n",
			 __func__,
			 event_code);

		result = SCI_FAILURE;
		break;
	}

	return result;
}

/**
 * scic_sds_phy_starting_substate_await_sata_phy_event_handler -
 * @phy: This struct scic_sds_phy object which has received an event.
 * @event_code: This is the event code which the phy object is to decode.
 *
 * This method is called when an event notification is received for the phy
 * object when in the state SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_PHY_EN. -
 * decode the event - link failure events restart the starting state machine -
 * sata spinup hold events are ignored since they are expected - sata phy
 * detected event change to the wait speed event - any other events log a
 * warning message and set a failure status enum sci_status SCI_SUCCESS on a link
 * failure event SCI_FAILURE on any unexpected event notifation
 */
static enum sci_status scic_sds_phy_starting_substate_await_sata_phy_event_handler(
	struct scic_sds_phy *sci_phy, u32 event_code)
{
	u32 result = SCI_SUCCESS;

	switch (scu_get_event_code(event_code)) {
	case SCU_EVENT_LINK_FAILURE:
		/* Link failure change state back to the starting state */
		scic_sds_phy_restart_starting_state(sci_phy);
		break;

	case SCU_EVENT_SATA_SPINUP_HOLD:
		/* These events might be received since we dont know how many may be in
		 * the completion queue while waiting for power
		 */
		break;

	case SCU_EVENT_SATA_PHY_DETECTED:
		sci_phy->protocol = SCIC_SDS_PHY_PROTOCOL_SATA;

		/* We have received the SATA PHY notification change state */
		sci_base_state_machine_change_state(&sci_phy->starting_substate_machine,
						    SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_SPEED_EN);
		break;

	case SCU_EVENT_SAS_PHY_DETECTED:
		/* There has been a change in the phy type before OOB/SN for the
		 * SATA finished start down the SAS link traning path.
		 */
		scic_sds_phy_start_sas_link_training(sci_phy);
		break;

	default:
		dev_warn(sciphy_to_dev(sci_phy),
			 "%s: PHY starting substate machine received "
			 "unexpected event_code %x\n",
			 __func__,
			 event_code);

		result = SCI_FAILURE;
		break;
	}

	return result;
}

/**
 *
 * @phy: This struct scic_sds_phy object which has received an event.
 * @event_code: This is the event code which the phy object is to decode.
 *
 * This method is called when an event notification is received for the phy
 * object when in the state SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_SPEED_EN.
 * - decode the event - sata phy detected returns us back to this state. -
 * speed event detected causes a state transition to the wait for signature. -
 * link failure events restart the starting state machine - any other events
 * log a warning message and set a failure status enum sci_status SCI_SUCCESS on any
 * valid event notification SCI_FAILURE on any unexpected event notifation
 */
static enum sci_status scic_sds_phy_starting_substate_await_sata_speed_event_handler(
	struct scic_sds_phy *sci_phy,
	u32 event_code)
{
	u32 result = SCI_SUCCESS;

	switch (scu_get_event_code(event_code)) {
	case SCU_EVENT_SATA_PHY_DETECTED:
		/*
		 * The hardware reports multiple SATA PHY detected events
		 * ignore the extras */
		break;

	case SCU_EVENT_SATA_15:
	case SCU_EVENT_SATA_15_SSC:
		scic_sds_phy_complete_link_training(
			sci_phy,
			SAS_LINK_RATE_1_5_GBPS,
			SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SIG_FIS_UF);
		break;

	case SCU_EVENT_SATA_30:
	case SCU_EVENT_SATA_30_SSC:
		scic_sds_phy_complete_link_training(
			sci_phy,
			SAS_LINK_RATE_3_0_GBPS,
			SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SIG_FIS_UF);
		break;

	case SCU_EVENT_SATA_60:
	case SCU_EVENT_SATA_60_SSC:
		scic_sds_phy_complete_link_training(
			sci_phy,
			SAS_LINK_RATE_6_0_GBPS,
			SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SIG_FIS_UF);
		break;

	case SCU_EVENT_LINK_FAILURE:
		/* Link failure change state back to the starting state */
		scic_sds_phy_restart_starting_state(sci_phy);
		break;

	case SCU_EVENT_SAS_PHY_DETECTED:
		/*
		 * There has been a change in the phy type before OOB/SN for the
		 * SATA finished start down the SAS link traning path. */
		scic_sds_phy_start_sas_link_training(sci_phy);
		break;

	default:
		dev_warn(sciphy_to_dev(sci_phy),
			 "%s: PHY starting substate machine received "
			 "unexpected event_code %x\n",
			 __func__,
			 event_code);

		result = SCI_FAILURE;
		break;
	}

	return result;
}

/**
 * scic_sds_phy_starting_substate_await_sig_fis_event_handler -
 * @phy: This struct scic_sds_phy object which has received an event.
 * @event_code: This is the event code which the phy object is to decode.
 *
 * This method is called when an event notification is received for the phy
 * object when in the state SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SIG_FIS_UF. -
 * decode the event - sas phy detected event backs up the state machine to the
 * await speed notification. - identify timeout is an un-expected event and the
 * state machine is restarted. - link failure events restart the starting state
 * machine - any other events log a warning message and set a failure status
 * enum sci_status SCI_SUCCESS on any valid event notification SCI_FAILURE on any
 * unexpected event notifation
 */
static enum sci_status scic_sds_phy_starting_substate_await_sig_fis_event_handler(
	struct scic_sds_phy *sci_phy, u32 event_code)
{
	u32 result = SCI_SUCCESS;

	switch (scu_get_event_code(event_code)) {
	case SCU_EVENT_SATA_PHY_DETECTED:
		/* Backup the state machine */
		sci_base_state_machine_change_state(&sci_phy->starting_substate_machine,
						    SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_SPEED_EN);
		break;

	case SCU_EVENT_LINK_FAILURE:
		/* Link failure change state back to the starting state */
		scic_sds_phy_restart_starting_state(sci_phy);
		break;

	default:
		dev_warn(sciphy_to_dev(sci_phy),
			 "%s: PHY starting substate machine received "
			 "unexpected event_code %x\n",
			 __func__,
			 event_code);

		result = SCI_FAILURE;
		break;
	}

	return result;
}


/*
 * *****************************************************************************
 * *  SCIC SDS PHY FRAME_HANDLERS
 * ***************************************************************************** */

/**
 *
 * @phy: This is struct scic_sds_phy object which is being requested to decode the
 *    frame data.
 * @frame_index: This is the index of the unsolicited frame which was received
 *    for this phy.
 *
 * This method decodes the unsolicited frame when the struct scic_sds_phy is in the
 * SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_IAF_UF. - Get the UF Header - If the UF
 * is an IAF - Copy IAF data to local phy object IAF data buffer. - Change
 * starting substate to wait power. - else - log warning message of unexpected
 * unsolicted frame - release frame buffer enum sci_status SCI_SUCCESS
 */
static enum sci_status scic_sds_phy_starting_substate_await_iaf_uf_frame_handler(
	struct scic_sds_phy *sci_phy, u32 frame_index)
{
	enum sci_status result;
	u32 *frame_words;
	struct sas_identify_frame *identify_frame;
	struct isci_phy *iphy = sci_phy->iphy;

	result = scic_sds_unsolicited_frame_control_get_header(
		&(scic_sds_phy_get_controller(sci_phy)->uf_control),
		frame_index,
		(void **)&frame_words);

	if (result != SCI_SUCCESS) {
		return result;
	}

	frame_words[0] = SCIC_SWAP_DWORD(frame_words[0]);
	identify_frame = (struct sas_identify_frame *)frame_words;

	if (identify_frame->frame_type == 0) {
		u32 state;

		/* Byte swap the rest of the frame so we can make
		 * a copy of the buffer
		 */
		frame_words[1] = SCIC_SWAP_DWORD(frame_words[1]);
		frame_words[2] = SCIC_SWAP_DWORD(frame_words[2]);
		frame_words[3] = SCIC_SWAP_DWORD(frame_words[3]);
		frame_words[4] = SCIC_SWAP_DWORD(frame_words[4]);
		frame_words[5] = SCIC_SWAP_DWORD(frame_words[5]);

		memcpy(&iphy->frame_rcvd.iaf, identify_frame, sizeof(*identify_frame));

		if (identify_frame->smp_tport) {
			/* We got the IAF for an expander PHY go to the final state since
			 * there are no power requirements for expander phys.
			 */
			state = SCIC_SDS_PHY_STARTING_SUBSTATE_FINAL;
		} else {
			/* We got the IAF we can now go to the await spinup semaphore state */
			state = SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SAS_POWER;
		}
		sci_base_state_machine_change_state(&sci_phy->starting_substate_machine,
						    state);
		result = SCI_SUCCESS;
	} else
		dev_warn(sciphy_to_dev(sci_phy),
			"%s: PHY starting substate machine received "
			"unexpected frame id %x\n",
			__func__,
			frame_index);

	/* Regardless of the result release this frame since we are done with it */
	scic_sds_controller_release_frame(scic_sds_phy_get_controller(sci_phy),
					  frame_index);

	return result;
}

/**
 *
 * @phy: This is struct scic_sds_phy object which is being requested to decode the
 *    frame data.
 * @frame_index: This is the index of the unsolicited frame which was received
 *    for this phy.
 *
 * This method decodes the unsolicited frame when the struct scic_sds_phy is in the
 * SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SIG_FIS_UF. - Get the UF Header - If
 * the UF is an SIGNATURE FIS - Copy IAF data to local phy object SIGNATURE FIS
 * data buffer. - else - log warning message of unexpected unsolicted frame -
 * release frame buffer enum sci_status SCI_SUCCESS Must decode the SIGNATURE FIS
 * data
 */
static enum sci_status scic_sds_phy_starting_substate_await_sig_fis_frame_handler(
	struct scic_sds_phy *sci_phy,
	u32 frame_index)
{
	enum sci_status result;
	struct dev_to_host_fis *frame_header;
	u32 *fis_frame_data;
	struct isci_phy *iphy = sci_phy->iphy;

	result = scic_sds_unsolicited_frame_control_get_header(
		&(scic_sds_phy_get_controller(sci_phy)->uf_control),
		frame_index,
		(void **)&frame_header);

	if (result != SCI_SUCCESS)
		return result;

	if ((frame_header->fis_type == FIS_REGD2H) &&
	    !(frame_header->status & ATA_BUSY)) {
		scic_sds_unsolicited_frame_control_get_buffer(
			&(scic_sds_phy_get_controller(sci_phy)->uf_control),
			frame_index,
			(void **)&fis_frame_data);

		scic_sds_controller_copy_sata_response(&iphy->frame_rcvd.fis,
						       frame_header,
						       fis_frame_data);

		/* got IAF we can now go to the await spinup semaphore state */
		sci_base_state_machine_change_state(&sci_phy->starting_substate_machine,
						    SCIC_SDS_PHY_STARTING_SUBSTATE_FINAL);

		result = SCI_SUCCESS;
	} else
		dev_warn(sciphy_to_dev(sci_phy),
			 "%s: PHY starting substate machine received "
			 "unexpected frame id %x\n",
			 __func__,
			 frame_index);

	/* Regardless of the result we are done with this frame with it */
	scic_sds_controller_release_frame(scic_sds_phy_get_controller(sci_phy),
					  frame_index);

	return result;
}

/*
 * *****************************************************************************
 * * SCIC SDS PHY POWER_HANDLERS
 * ***************************************************************************** */

/*
 * This method is called by the struct scic_sds_controller when the phy object is
 * granted power. - The notify enable spinups are turned on for this phy object
 * - The phy state machine is transitioned to the
 * SCIC_SDS_PHY_STARTING_SUBSTATE_FINAL. enum sci_status SCI_SUCCESS
 */
static enum sci_status scic_sds_phy_starting_substate_await_sas_power_consume_power_handler(
	struct scic_sds_phy *sci_phy)
{
	u32 enable_spinup;

	enable_spinup = readl(&sci_phy->link_layer_registers->notify_enable_spinup_control);
	enable_spinup |= SCU_ENSPINUP_GEN_BIT(ENABLE);
	writel(enable_spinup, &sci_phy->link_layer_registers->notify_enable_spinup_control);

	/* Change state to the final state this substate machine has run to completion */
	sci_base_state_machine_change_state(&sci_phy->starting_substate_machine,
					    SCIC_SDS_PHY_STARTING_SUBSTATE_FINAL);

	return SCI_SUCCESS;
}

/*
 * This method is called by the struct scic_sds_controller when the phy object is
 * granted power. - The phy state machine is transitioned to the
 * SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_PHY_EN. enum sci_status SCI_SUCCESS
 */
static enum sci_status scic_sds_phy_starting_substate_await_sata_power_consume_power_handler(
	struct scic_sds_phy *sci_phy)
{
	u32 scu_sas_pcfg_value;

	/* Release the spinup hold state and reset the OOB state machine */
	scu_sas_pcfg_value =
		readl(&sci_phy->link_layer_registers->phy_configuration);
	scu_sas_pcfg_value &=
		~(SCU_SAS_PCFG_GEN_BIT(SATA_SPINUP_HOLD) | SCU_SAS_PCFG_GEN_BIT(OOB_ENABLE));
	scu_sas_pcfg_value |= SCU_SAS_PCFG_GEN_BIT(OOB_RESET);
	writel(scu_sas_pcfg_value,
		&sci_phy->link_layer_registers->phy_configuration);

	/* Now restart the OOB operation */
	scu_sas_pcfg_value &= ~SCU_SAS_PCFG_GEN_BIT(OOB_RESET);
	scu_sas_pcfg_value |= SCU_SAS_PCFG_GEN_BIT(OOB_ENABLE);
	writel(scu_sas_pcfg_value,
		&sci_phy->link_layer_registers->phy_configuration);

	/* Change state to the final state this substate machine has run to completion */
	sci_base_state_machine_change_state(&sci_phy->starting_substate_machine,
					    SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_PHY_EN);

	return SCI_SUCCESS;
}

static enum sci_status default_phy_handler(struct scic_sds_phy *sci_phy,
					   const char *func)
{
	dev_dbg(sciphy_to_dev(sci_phy),
		 "%s: in wrong state: %d\n", func,
		 sci_base_state_machine_get_state(&sci_phy->state_machine));
	return SCI_FAILURE_INVALID_STATE;
}

static enum sci_status
scic_sds_phy_default_start_handler(struct scic_sds_phy *sci_phy)
{
	return default_phy_handler(sci_phy, __func__);
}

static enum sci_status
scic_sds_phy_default_stop_handler(struct scic_sds_phy *sci_phy)
{
	return default_phy_handler(sci_phy, __func__);
}

static enum sci_status
scic_sds_phy_default_reset_handler(struct scic_sds_phy *sci_phy)
{
	return default_phy_handler(sci_phy, __func__);
}

static enum sci_status
scic_sds_phy_default_destroy_handler(struct scic_sds_phy *sci_phy)
{
	return default_phy_handler(sci_phy, __func__);
}

static enum sci_status
scic_sds_phy_default_frame_handler(struct scic_sds_phy *sci_phy,
				   u32 frame_index)
{
	struct scic_sds_controller *scic = scic_sds_phy_get_controller(sci_phy);

	default_phy_handler(sci_phy, __func__);
	scic_sds_controller_release_frame(scic, frame_index);

	return SCI_FAILURE_INVALID_STATE;
}

static enum sci_status
scic_sds_phy_default_event_handler(struct scic_sds_phy *sci_phy,
				   u32 event_code)
{
	return default_phy_handler(sci_phy, __func__);
}

static enum sci_status
scic_sds_phy_default_consume_power_handler(struct scic_sds_phy *sci_phy)
{
	return default_phy_handler(sci_phy, __func__);
}



static const struct scic_sds_phy_state_handler scic_sds_phy_starting_substate_handler_table[] = {
	[SCIC_SDS_PHY_STARTING_SUBSTATE_INITIAL] = {
		.start_handler		= scic_sds_phy_default_start_handler,
		.stop_handler		= scic_sds_phy_starting_substate_general_stop_handler,
		.reset_handler		= scic_sds_phy_default_reset_handler,
		.destruct_handler	= scic_sds_phy_default_destroy_handler,
		.frame_handler		= scic_sds_phy_default_frame_handler,
		.event_handler		= scic_sds_phy_default_event_handler,
		.consume_power_handler	= scic_sds_phy_default_consume_power_handler
	},
	[SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_OSSP_EN] = {
		.start_handler		= scic_sds_phy_default_start_handler,
		.stop_handler		= scic_sds_phy_starting_substate_general_stop_handler,
		.reset_handler		= scic_sds_phy_default_reset_handler,
		.destruct_handler	= scic_sds_phy_default_destroy_handler,
		.frame_handler		= scic_sds_phy_default_frame_handler,
		.event_handler		= scic_sds_phy_starting_substate_await_ossp_event_handler,
		.consume_power_handler	= scic_sds_phy_default_consume_power_handler
	},
	[SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SAS_SPEED_EN] = {
		.start_handler		= scic_sds_phy_default_start_handler,
		.stop_handler		= scic_sds_phy_starting_substate_general_stop_handler,
		.reset_handler		= scic_sds_phy_default_reset_handler,
		.destruct_handler	= scic_sds_phy_default_destroy_handler,
		.frame_handler		= scic_sds_phy_default_frame_handler,
		.event_handler		= scic_sds_phy_starting_substate_await_sas_phy_speed_event_handler,
		.consume_power_handler	= scic_sds_phy_default_consume_power_handler
	},
	[SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_IAF_UF] = {
		.start_handler		= scic_sds_phy_default_start_handler,
		.stop_handler		= scic_sds_phy_default_stop_handler,
		.reset_handler		= scic_sds_phy_default_reset_handler,
		.destruct_handler	= scic_sds_phy_default_destroy_handler,
		.frame_handler		= scic_sds_phy_starting_substate_await_iaf_uf_frame_handler,
		.event_handler		= scic_sds_phy_starting_substate_await_iaf_uf_event_handler,
		.consume_power_handler	= scic_sds_phy_default_consume_power_handler
	},
	[SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SAS_POWER] = {
		.start_handler		= scic_sds_phy_default_start_handler,
		.stop_handler		= scic_sds_phy_starting_substate_general_stop_handler,
		.reset_handler		= scic_sds_phy_default_reset_handler,
		.destruct_handler	= scic_sds_phy_default_destroy_handler,
		.frame_handler		= scic_sds_phy_default_frame_handler,
		.event_handler		= scic_sds_phy_starting_substate_await_sas_power_event_handler,
		.consume_power_handler	= scic_sds_phy_starting_substate_await_sas_power_consume_power_handler
	},
	[SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_POWER] = {
		.start_handler		= scic_sds_phy_default_start_handler,
		.stop_handler		= scic_sds_phy_starting_substate_general_stop_handler,
		.reset_handler		= scic_sds_phy_default_reset_handler,
		.destruct_handler	= scic_sds_phy_default_destroy_handler,
		.frame_handler		= scic_sds_phy_default_frame_handler,
		.event_handler		= scic_sds_phy_starting_substate_await_sata_power_event_handler,
		.consume_power_handler	= scic_sds_phy_starting_substate_await_sata_power_consume_power_handler
	},
	[SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_PHY_EN] = {
		.start_handler		= scic_sds_phy_default_start_handler,
		.stop_handler		= scic_sds_phy_starting_substate_general_stop_handler,
		.reset_handler		= scic_sds_phy_default_reset_handler,
		.destruct_handler	= scic_sds_phy_default_destroy_handler,
		.frame_handler		= scic_sds_phy_default_frame_handler,
		.event_handler		= scic_sds_phy_starting_substate_await_sata_phy_event_handler,
		.consume_power_handler	= scic_sds_phy_default_consume_power_handler
	},
	[SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_SPEED_EN] = {
		.start_handler		= scic_sds_phy_default_start_handler,
		.stop_handler		= scic_sds_phy_starting_substate_general_stop_handler,
		.reset_handler		= scic_sds_phy_default_reset_handler,
		.destruct_handler	= scic_sds_phy_default_destroy_handler,
		.frame_handler		= scic_sds_phy_default_frame_handler,
		.event_handler		= scic_sds_phy_starting_substate_await_sata_speed_event_handler,
		.consume_power_handler	= scic_sds_phy_default_consume_power_handler
	},
	[SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SIG_FIS_UF] = {
		.start_handler		= scic_sds_phy_default_start_handler,
		.stop_handler		= scic_sds_phy_starting_substate_general_stop_handler,
		.reset_handler		= scic_sds_phy_default_reset_handler,
		.destruct_handler	= scic_sds_phy_default_destroy_handler,
		.frame_handler		= scic_sds_phy_starting_substate_await_sig_fis_frame_handler,
		.event_handler		= scic_sds_phy_starting_substate_await_sig_fis_event_handler,
		.consume_power_handler	= scic_sds_phy_default_consume_power_handler
	},
	[SCIC_SDS_PHY_STARTING_SUBSTATE_FINAL] = {
		.start_handler		= scic_sds_phy_default_start_handler,
		.stop_handler		= scic_sds_phy_starting_substate_general_stop_handler,
		.reset_handler		= scic_sds_phy_default_reset_handler,
		.destruct_handler	= scic_sds_phy_default_destroy_handler,
		.frame_handler		 = scic_sds_phy_default_frame_handler,
		.event_handler		 = scic_sds_phy_default_event_handler,
		.consume_power_handler	 = scic_sds_phy_default_consume_power_handler
	}
};

/**
 * scic_sds_phy_set_starting_substate_handlers() -
 *
 * This macro sets the starting substate handlers by state_id
 */
#define scic_sds_phy_set_starting_substate_handlers(phy, state_id) \
	scic_sds_phy_set_state_handlers(\
		(phy), \
		&scic_sds_phy_starting_substate_handler_table[(state_id)] \
		)

/*
 * ****************************************************************************
 * *  PHY STARTING SUBSTATE METHODS
 * **************************************************************************** */

/**
 * scic_sds_phy_starting_initial_substate_enter -
 * @object: This is the object which is cast to a struct scic_sds_phy object.
 *
 * This method will perform the actions required by the struct scic_sds_phy on
 * entering the SCIC_SDS_PHY_STARTING_SUBSTATE_INITIAL. - The initial state
 * handlers are put in place for the struct scic_sds_phy object. - The state is
 * changed to the wait phy type event notification. none
 */
static void scic_sds_phy_starting_initial_substate_enter(void *object)
{
	struct scic_sds_phy *sci_phy = object;

	scic_sds_phy_set_starting_substate_handlers(
		sci_phy, SCIC_SDS_PHY_STARTING_SUBSTATE_INITIAL);

	/* This is just an temporary state go off to the starting state */
	sci_base_state_machine_change_state(&sci_phy->starting_substate_machine,
					    SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_OSSP_EN);
}

/**
 *
 * @object: This is the object which is cast to a struct scic_sds_phy object.
 *
 * This method will perform the actions required by the struct scic_sds_phy on
 * entering the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_PHY_TYPE_EN. - Set the
 * struct scic_sds_phy object state handlers for this state. none
 */
static void scic_sds_phy_starting_await_ossp_en_substate_enter(void *object)
{
	struct scic_sds_phy *sci_phy = object;

	scic_sds_phy_set_starting_substate_handlers(
		sci_phy, SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_OSSP_EN
		);
}

/**
 *
 * @object: This is the object which is cast to a struct scic_sds_phy object.
 *
 * This method will perform the actions required by the struct scic_sds_phy on
 * entering the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SPEED_EN. - Set the
 * struct scic_sds_phy object state handlers for this state. none
 */
static void scic_sds_phy_starting_await_sas_speed_en_substate_enter(
		void *object)
{
	struct scic_sds_phy *sci_phy = object;

	scic_sds_phy_set_starting_substate_handlers(
		sci_phy, SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SAS_SPEED_EN
		);
}

/**
 *
 * @object: This is the object which is cast to a struct scic_sds_phy object.
 *
 * This method will perform the actions required by the struct scic_sds_phy on
 * entering the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_IAF_UF. - Set the
 * struct scic_sds_phy object state handlers for this state. none
 */
static void scic_sds_phy_starting_await_iaf_uf_substate_enter(void *object)
{
	struct scic_sds_phy *sci_phy = object;

	scic_sds_phy_set_starting_substate_handlers(
		sci_phy, SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_IAF_UF
		);
}

/**
 *
 * @object: This is the object which is cast to a struct scic_sds_phy object.
 *
 * This method will perform the actions required by the struct scic_sds_phy on
 * entering the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SAS_POWER. - Set the
 * struct scic_sds_phy object state handlers for this state. - Add this phy object to
 * the power control queue none
 */
static void scic_sds_phy_starting_await_sas_power_substate_enter(void *object)
{
	struct scic_sds_phy *sci_phy = object;

	scic_sds_phy_set_starting_substate_handlers(
		sci_phy, SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SAS_POWER
		);

	scic_sds_controller_power_control_queue_insert(
		scic_sds_phy_get_controller(sci_phy),
		sci_phy
		);
}

/**
 *
 * @object: This is the object which is cast to a struct scic_sds_phy object.
 *
 * This method will perform the actions required by the struct scic_sds_phy on exiting
 * the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SAS_POWER. - Remove the
 * struct scic_sds_phy object from the power control queue. none
 */
static void scic_sds_phy_starting_await_sas_power_substate_exit(void *object)
{
	struct scic_sds_phy *sci_phy = object;

	scic_sds_controller_power_control_queue_remove(
		scic_sds_phy_get_controller(sci_phy), sci_phy
		);
}

/**
 *
 * @object: This is the object which is cast to a struct scic_sds_phy object.
 *
 * This method will perform the actions required by the struct scic_sds_phy on
 * entering the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_POWER. - Set the
 * struct scic_sds_phy object state handlers for this state. - Add this phy object to
 * the power control queue none
 */
static void scic_sds_phy_starting_await_sata_power_substate_enter(void *object)
{
	struct scic_sds_phy *sci_phy = object;

	scic_sds_phy_set_starting_substate_handlers(
		sci_phy, SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_POWER
		);

	scic_sds_controller_power_control_queue_insert(
		scic_sds_phy_get_controller(sci_phy),
		sci_phy
		);
}

/**
 *
 * @object: This is the object which is cast to a struct scic_sds_phy object.
 *
 * This method will perform the actions required by the struct scic_sds_phy on exiting
 * the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_POWER. - Remove the
 * struct scic_sds_phy object from the power control queue. none
 */
static void scic_sds_phy_starting_await_sata_power_substate_exit(void *object)
{
	struct scic_sds_phy *sci_phy = object;

	scic_sds_controller_power_control_queue_remove(
		scic_sds_phy_get_controller(sci_phy),
		sci_phy
		);
}

/**
 *
 * @object: This is the object which is cast to a struct scic_sds_phy object.
 *
 * This function will perform the actions required by the struct scic_sds_phy on
 * entering the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_PHY_EN. - Set the
 * struct scic_sds_phy object state handlers for this state. none
 */
static void scic_sds_phy_starting_await_sata_phy_substate_enter(void *object)
{
	struct scic_sds_phy *sci_phy = object;

	scic_sds_phy_set_starting_substate_handlers(
			sci_phy,
			SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_PHY_EN);

	isci_timer_start(sci_phy->sata_timeout_timer,
			 SCIC_SDS_SATA_LINK_TRAINING_TIMEOUT);
}

/**
 *
 * @object: This is the object which is cast to a struct scic_sds_phy object.
 *
 * This method will perform the actions required by the struct scic_sds_phy
 * on exiting
 * the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_SPEED_EN. - stop the timer
 * that was started on entry to await sata phy event notification none
 */
static inline void scic_sds_phy_starting_await_sata_phy_substate_exit(
		void *object)
{
	struct scic_sds_phy *sci_phy = object;

	isci_timer_stop(sci_phy->sata_timeout_timer);
}

/**
 *
 * @object: This is the object which is cast to a struct scic_sds_phy object.
 *
 * This method will perform the actions required by the struct scic_sds_phy on
 * entering the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_SPEED_EN. - Set the
 * struct scic_sds_phy object state handlers for this state. none
 */
static void scic_sds_phy_starting_await_sata_speed_substate_enter(void *object)
{
	struct scic_sds_phy *sci_phy = object;

	scic_sds_phy_set_starting_substate_handlers(
			sci_phy,
			SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_SPEED_EN);

	isci_timer_start(sci_phy->sata_timeout_timer,
			 SCIC_SDS_SATA_LINK_TRAINING_TIMEOUT);
}

/**
 *
 * @object: This is the object which is cast to a struct scic_sds_phy object.
 *
 * This function will perform the actions required by the
 * struct scic_sds_phy on exiting
 * the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_SPEED_EN. - stop the timer
 * that was started on entry to await sata phy event notification none
 */
static inline void scic_sds_phy_starting_await_sata_speed_substate_exit(
	void *object)
{
	struct scic_sds_phy *sci_phy = object;

	isci_timer_stop(sci_phy->sata_timeout_timer);
}

/**
 *
 * @object: This is the object which is cast to a struct scic_sds_phy object.
 *
 * This function will perform the actions required by the struct scic_sds_phy on
 * entering the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SIG_FIS_UF. - Set the
 * struct scic_sds_phy object state handlers for this state.
 * - Start the SIGNATURE FIS
 * timeout timer none
 */
static void scic_sds_phy_starting_await_sig_fis_uf_substate_enter(void *object)
{
	bool continue_to_ready_state;
	struct scic_sds_phy *sci_phy = object;

	scic_sds_phy_set_starting_substate_handlers(
			sci_phy,
			SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SIG_FIS_UF);

	continue_to_ready_state = scic_sds_port_link_detected(
		sci_phy->owning_port,
		sci_phy);

	if (continue_to_ready_state) {
		/*
		 * Clear the PE suspend condition so we can actually
		 * receive SIG FIS
		 * The hardware will not respond to the XRDY until the PE
		 * suspend condition is cleared.
		 */
		scic_sds_phy_resume(sci_phy);

		isci_timer_start(sci_phy->sata_timeout_timer,
				 SCIC_SDS_SIGNATURE_FIS_TIMEOUT);
	} else
		sci_phy->is_in_link_training = false;
}

/**
 *
 * @object: This is the object which is cast to a struct scic_sds_phy object.
 *
 * This function will perform the actions required by the
 * struct scic_sds_phy on exiting
 * the SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SIG_FIS_UF. - Stop the SIGNATURE
 * FIS timeout timer. none
 */
static inline void scic_sds_phy_starting_await_sig_fis_uf_substate_exit(
	void *object)
{
	struct scic_sds_phy *sci_phy = object;

	isci_timer_stop(sci_phy->sata_timeout_timer);
}

/**
 *
 * @object: This is the object which is cast to a struct scic_sds_phy object.
 *
 * This method will perform the actions required by the struct scic_sds_phy on
 * entering the SCIC_SDS_PHY_STARTING_SUBSTATE_FINAL. - Set the struct scic_sds_phy
 * object state handlers for this state. - Change base state machine to the
 * ready state. none
 */
static void scic_sds_phy_starting_final_substate_enter(void *object)
{
	struct scic_sds_phy *sci_phy = object;

	scic_sds_phy_set_starting_substate_handlers(sci_phy,
						    SCIC_SDS_PHY_STARTING_SUBSTATE_FINAL);

	/* State machine has run to completion so exit out and change
	 * the base state machine to the ready state
	 */
	sci_base_state_machine_change_state(&sci_phy->state_machine,
					    SCI_BASE_PHY_STATE_READY);
}

/* --------------------------------------------------------------------------- */

static const struct sci_base_state scic_sds_phy_starting_substates[] = {
	[SCIC_SDS_PHY_STARTING_SUBSTATE_INITIAL] = {
		.enter_state = scic_sds_phy_starting_initial_substate_enter,
	},
	[SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_OSSP_EN] = {
		.enter_state = scic_sds_phy_starting_await_ossp_en_substate_enter,
	},
	[SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SAS_SPEED_EN] = {
		.enter_state = scic_sds_phy_starting_await_sas_speed_en_substate_enter,
	},
	[SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_IAF_UF] = {
		.enter_state = scic_sds_phy_starting_await_iaf_uf_substate_enter,
	},
	[SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SAS_POWER] = {
		.enter_state = scic_sds_phy_starting_await_sas_power_substate_enter,
		.exit_state  = scic_sds_phy_starting_await_sas_power_substate_exit,
	},
	[SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_POWER] = {
		.enter_state = scic_sds_phy_starting_await_sata_power_substate_enter,
		.exit_state  = scic_sds_phy_starting_await_sata_power_substate_exit
	},
	[SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_PHY_EN] = {
		.enter_state = scic_sds_phy_starting_await_sata_phy_substate_enter,
		.exit_state  = scic_sds_phy_starting_await_sata_phy_substate_exit
	},
	[SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SATA_SPEED_EN] = {
		.enter_state = scic_sds_phy_starting_await_sata_speed_substate_enter,
		.exit_state  = scic_sds_phy_starting_await_sata_speed_substate_exit
	},
	[SCIC_SDS_PHY_STARTING_SUBSTATE_AWAIT_SIG_FIS_UF] = {
		.enter_state = scic_sds_phy_starting_await_sig_fis_uf_substate_enter,
		.exit_state  = scic_sds_phy_starting_await_sig_fis_uf_substate_exit
	},
	[SCIC_SDS_PHY_STARTING_SUBSTATE_FINAL] = {
		.enter_state = scic_sds_phy_starting_final_substate_enter,
	}
};

/*
 * This method takes the struct scic_sds_phy from a stopped state and
 * attempts to start it. - The phy state machine is transitioned to the
 * SCI_BASE_PHY_STATE_STARTING. enum sci_status SCI_SUCCESS
 */
static enum sci_status
scic_sds_phy_stopped_state_start_handler(struct scic_sds_phy *sci_phy)
{
	struct isci_host *ihost;
	struct scic_sds_controller *scic;

	scic = scic_sds_phy_get_controller(sci_phy),
	ihost = scic->ihost;

	/* Create the SIGNATURE FIS Timeout timer for this phy */
	sci_phy->sata_timeout_timer = isci_timer_create(ihost, sci_phy,
							scic_sds_phy_sata_timeout);

	if (sci_phy->sata_timeout_timer)
		sci_base_state_machine_change_state(&sci_phy->state_machine,
						    SCI_BASE_PHY_STATE_STARTING);

	return SCI_SUCCESS;
}

static enum sci_status
scic_sds_phy_stopped_state_destroy_handler(struct scic_sds_phy *sci_phy)
{
	return SCI_SUCCESS;
}

static enum sci_status
scic_sds_phy_ready_state_stop_handler(struct scic_sds_phy *sci_phy)
{
	sci_base_state_machine_change_state(&sci_phy->state_machine,
					    SCI_BASE_PHY_STATE_STOPPED);

	return SCI_SUCCESS;
}

static enum sci_status
scic_sds_phy_ready_state_reset_handler(struct scic_sds_phy *sci_phy)
{
	sci_base_state_machine_change_state(&sci_phy->state_machine,
					    SCI_BASE_PHY_STATE_RESETTING);

	return SCI_SUCCESS;
}

/**
 * scic_sds_phy_ready_state_event_handler -
 * @phy: This is the struct scic_sds_phy object which has received the event.
 *
 * This method request the struct scic_sds_phy handle the received event.  The only
 * event that we are interested in while in the ready state is the link failure
 * event. - decoded event is a link failure - transition the struct scic_sds_phy back
 * to the SCI_BASE_PHY_STATE_STARTING state. - any other event received will
 * report a warning message enum sci_status SCI_SUCCESS if the event received is a
 * link failure SCI_FAILURE_INVALID_STATE for any other event received.
 */
static enum sci_status scic_sds_phy_ready_state_event_handler(struct scic_sds_phy *sci_phy,
							      u32 event_code)
{
	enum sci_status result = SCI_FAILURE;

	switch (scu_get_event_code(event_code)) {
	case SCU_EVENT_LINK_FAILURE:
		/* Link failure change state back to the starting state */
		sci_base_state_machine_change_state(&sci_phy->state_machine,
						    SCI_BASE_PHY_STATE_STARTING);
		result = SCI_SUCCESS;
		break;

	case SCU_EVENT_BROADCAST_CHANGE:
		/* Broadcast change received. Notify the port. */
		if (scic_sds_phy_get_port(sci_phy) != NULL)
			scic_sds_port_broadcast_change_received(sci_phy->owning_port, sci_phy);
		else
			sci_phy->bcn_received_while_port_unassigned = true;
		break;

	default:
		dev_warn(sciphy_to_dev(sci_phy),
			 "%sP SCIC PHY 0x%p ready state machine received "
			 "unexpected event_code %x\n",
			 __func__, sci_phy, event_code);

		result = SCI_FAILURE_INVALID_STATE;
		break;
	}

	return result;
}

static enum sci_status scic_sds_phy_resetting_state_event_handler(struct scic_sds_phy *sci_phy,
								  u32 event_code)
{
	enum sci_status result = SCI_FAILURE;

	switch (scu_get_event_code(event_code)) {
	case SCU_EVENT_HARD_RESET_TRANSMITTED:
		/* Link failure change state back to the starting state */
		sci_base_state_machine_change_state(&sci_phy->state_machine,
						    SCI_BASE_PHY_STATE_STARTING);
		result = SCI_SUCCESS;
		break;

	default:
		dev_warn(sciphy_to_dev(sci_phy),
			 "%s: SCIC PHY 0x%p resetting state machine received "
			 "unexpected event_code %x\n",
			 __func__, sci_phy, event_code);

		result = SCI_FAILURE_INVALID_STATE;
		break;
	}

	return result;
}

/* --------------------------------------------------------------------------- */

static const struct scic_sds_phy_state_handler scic_sds_phy_state_handler_table[] = {
	[SCI_BASE_PHY_STATE_INITIAL] = {
		.start_handler = scic_sds_phy_default_start_handler,
		.stop_handler  = scic_sds_phy_default_stop_handler,
		.reset_handler = scic_sds_phy_default_reset_handler,
		.destruct_handler = scic_sds_phy_default_destroy_handler,
		.frame_handler		 = scic_sds_phy_default_frame_handler,
		.event_handler		 = scic_sds_phy_default_event_handler,
		.consume_power_handler	 = scic_sds_phy_default_consume_power_handler
	},
	[SCI_BASE_PHY_STATE_STOPPED]  = {
		.start_handler = scic_sds_phy_stopped_state_start_handler,
		.stop_handler  = scic_sds_phy_default_stop_handler,
		.reset_handler = scic_sds_phy_default_reset_handler,
		.destruct_handler = scic_sds_phy_stopped_state_destroy_handler,
		.frame_handler		 = scic_sds_phy_default_frame_handler,
		.event_handler		 = scic_sds_phy_default_event_handler,
		.consume_power_handler	 = scic_sds_phy_default_consume_power_handler
	},
	[SCI_BASE_PHY_STATE_STARTING] = {
		.start_handler = scic_sds_phy_default_start_handler,
		.stop_handler  = scic_sds_phy_default_stop_handler,
		.reset_handler = scic_sds_phy_default_reset_handler,
		.destruct_handler = scic_sds_phy_default_destroy_handler,
		.frame_handler		 = scic_sds_phy_default_frame_handler,
		.event_handler		 = scic_sds_phy_default_event_handler,
		.consume_power_handler	 = scic_sds_phy_default_consume_power_handler
	},
	[SCI_BASE_PHY_STATE_READY] = {
		.start_handler = scic_sds_phy_default_start_handler,
		.stop_handler  = scic_sds_phy_ready_state_stop_handler,
		.reset_handler = scic_sds_phy_ready_state_reset_handler,
		.destruct_handler = scic_sds_phy_default_destroy_handler,
		.frame_handler		 = scic_sds_phy_default_frame_handler,
		.event_handler		 = scic_sds_phy_ready_state_event_handler,
		.consume_power_handler	 = scic_sds_phy_default_consume_power_handler
	},
	[SCI_BASE_PHY_STATE_RESETTING] = {
		.start_handler = scic_sds_phy_default_start_handler,
		.stop_handler  = scic_sds_phy_default_stop_handler,
		.reset_handler = scic_sds_phy_default_reset_handler,
		.destruct_handler = scic_sds_phy_default_destroy_handler,
		.frame_handler		 = scic_sds_phy_default_frame_handler,
		.event_handler		 = scic_sds_phy_resetting_state_event_handler,
		.consume_power_handler	 = scic_sds_phy_default_consume_power_handler
	},
	[SCI_BASE_PHY_STATE_FINAL] = {
		.start_handler = scic_sds_phy_default_start_handler,
		.stop_handler  = scic_sds_phy_default_stop_handler,
		.reset_handler = scic_sds_phy_default_reset_handler,
		.destruct_handler = scic_sds_phy_default_destroy_handler,
		.frame_handler		 = scic_sds_phy_default_frame_handler,
		.event_handler		 = scic_sds_phy_default_event_handler,
		.consume_power_handler	 = scic_sds_phy_default_consume_power_handler
	}
};

/*
 * ****************************************************************************
 * *  PHY STATE PRIVATE METHODS
 * **************************************************************************** */

/**
 *
 * @sci_phy: This is the struct scic_sds_phy object to stop.
 *
 * This method will stop the struct scic_sds_phy object. This does not reset the
 * protocol engine it just suspends it and places it in a state where it will
 * not cause the end device to power up. none
 */
static void scu_link_layer_stop_protocol_engine(
	struct scic_sds_phy *sci_phy)
{
	u32 scu_sas_pcfg_value;
	u32 enable_spinup_value;

	/* Suspend the protocol engine and place it in a sata spinup hold state */
	scu_sas_pcfg_value =
		readl(&sci_phy->link_layer_registers->phy_configuration);
	scu_sas_pcfg_value |=
		(SCU_SAS_PCFG_GEN_BIT(OOB_RESET) |
		 SCU_SAS_PCFG_GEN_BIT(SUSPEND_PROTOCOL_ENGINE) |
		 SCU_SAS_PCFG_GEN_BIT(SATA_SPINUP_HOLD));
	writel(scu_sas_pcfg_value,
	       &sci_phy->link_layer_registers->phy_configuration);

	/* Disable the notify enable spinup primitives */
	enable_spinup_value = readl(&sci_phy->link_layer_registers->notify_enable_spinup_control);
	enable_spinup_value &= ~SCU_ENSPINUP_GEN_BIT(ENABLE);
	writel(enable_spinup_value, &sci_phy->link_layer_registers->notify_enable_spinup_control);
}

/**
 *
 *
 * This method will start the OOB/SN state machine for this struct scic_sds_phy object.
 */
static void scu_link_layer_start_oob(
	struct scic_sds_phy *sci_phy)
{
	u32 scu_sas_pcfg_value;

	scu_sas_pcfg_value =
		readl(&sci_phy->link_layer_registers->phy_configuration);
	scu_sas_pcfg_value |= SCU_SAS_PCFG_GEN_BIT(OOB_ENABLE);
	scu_sas_pcfg_value &=
		~(SCU_SAS_PCFG_GEN_BIT(OOB_RESET) |
		SCU_SAS_PCFG_GEN_BIT(HARD_RESET));
	writel(scu_sas_pcfg_value,
	       &sci_phy->link_layer_registers->phy_configuration);
}

/**
 *
 *
 * This method will transmit a hard reset request on the specified phy. The SCU
 * hardware requires that we reset the OOB state machine and set the hard reset
 * bit in the phy configuration register. We then must start OOB over with the
 * hard reset bit set.
 */
static void scu_link_layer_tx_hard_reset(
	struct scic_sds_phy *sci_phy)
{
	u32 phy_configuration_value;

	/*
	 * SAS Phys must wait for the HARD_RESET_TX event notification to transition
	 * to the starting state. */
	phy_configuration_value =
		readl(&sci_phy->link_layer_registers->phy_configuration);
	phy_configuration_value |=
		(SCU_SAS_PCFG_GEN_BIT(HARD_RESET) |
		 SCU_SAS_PCFG_GEN_BIT(OOB_RESET));
	writel(phy_configuration_value,
	       &sci_phy->link_layer_registers->phy_configuration);

	/* Now take the OOB state machine out of reset */
	phy_configuration_value |= SCU_SAS_PCFG_GEN_BIT(OOB_ENABLE);
	phy_configuration_value &= ~SCU_SAS_PCFG_GEN_BIT(OOB_RESET);
	writel(phy_configuration_value,
	       &sci_phy->link_layer_registers->phy_configuration);
}

/*
 * ****************************************************************************
 * *  PHY BASE STATE METHODS
 * **************************************************************************** */

/**
 *
 * @object: This is the object which is cast to a struct scic_sds_phy object.
 *
 * This method will perform the actions required by the struct scic_sds_phy on
 * entering the SCI_BASE_PHY_STATE_INITIAL. - This function sets the state
 * handlers for the phy object base state machine initial state. none
 */
static void scic_sds_phy_initial_state_enter(void *object)
{
	struct scic_sds_phy *sci_phy = object;

	scic_sds_phy_set_base_state_handlers(sci_phy, SCI_BASE_PHY_STATE_INITIAL);
}

/**
 *
 * @object: This is the object which is cast to a struct scic_sds_phy object.
 *
 * This function will perform the actions required by the struct scic_sds_phy on
 * entering the SCI_BASE_PHY_STATE_INITIAL. - This function sets the state
 * handlers for the phy object base state machine initial state. - The SCU
 * hardware is requested to stop the protocol engine. none
 */
static void scic_sds_phy_stopped_state_enter(void *object)
{
	struct scic_sds_phy *sci_phy = object;
	struct scic_sds_controller *scic = scic_sds_phy_get_controller(sci_phy);
	struct isci_host *ihost = scic->ihost;

	/*
	 * @todo We need to get to the controller to place this PE in a
	 * reset state
	 */

	scic_sds_phy_set_base_state_handlers(sci_phy,
					     SCI_BASE_PHY_STATE_STOPPED);

	if (sci_phy->sata_timeout_timer != NULL) {
		isci_del_timer(ihost, sci_phy->sata_timeout_timer);

		sci_phy->sata_timeout_timer = NULL;
	}

	scu_link_layer_stop_protocol_engine(sci_phy);

	if (sci_phy->state_machine.previous_state_id !=
			SCI_BASE_PHY_STATE_INITIAL)
		scic_sds_controller_link_down(
				scic_sds_phy_get_controller(sci_phy),
				scic_sds_phy_get_port(sci_phy),
				sci_phy);
}

/**
 *
 * @object: This is the object which is cast to a struct scic_sds_phy object.
 *
 * This method will perform the actions required by the struct scic_sds_phy on
 * entering the SCI_BASE_PHY_STATE_STARTING. - This function sets the state
 * handlers for the phy object base state machine starting state. - The SCU
 * hardware is requested to start OOB/SN on this protocl engine. - The phy
 * starting substate machine is started. - If the previous state was the ready
 * state then the struct scic_sds_controller is informed that the phy has gone link
 * down. none
 */
static void scic_sds_phy_starting_state_enter(void *object)
{
	struct scic_sds_phy *sci_phy = object;

	scic_sds_phy_set_base_state_handlers(sci_phy, SCI_BASE_PHY_STATE_STARTING);

	scu_link_layer_stop_protocol_engine(sci_phy);
	scu_link_layer_start_oob(sci_phy);

	/* We don't know what kind of phy we are going to be just yet */
	sci_phy->protocol = SCIC_SDS_PHY_PROTOCOL_UNKNOWN;
	sci_phy->bcn_received_while_port_unassigned = false;

	/* Change over to the starting substate machine to continue */
	sci_base_state_machine_start(&sci_phy->starting_substate_machine);

	if (sci_phy->state_machine.previous_state_id
	    == SCI_BASE_PHY_STATE_READY) {
		scic_sds_controller_link_down(
			scic_sds_phy_get_controller(sci_phy),
			scic_sds_phy_get_port(sci_phy),
			sci_phy
			);
	}
}

/**
 *
 * @object: This is the object which is cast to a struct scic_sds_phy object.
 *
 * This method will perform the actions required by the struct scic_sds_phy on
 * entering the SCI_BASE_PHY_STATE_READY. - This function sets the state
 * handlers for the phy object base state machine ready state. - The SCU
 * hardware protocol engine is resumed. - The struct scic_sds_controller is informed
 * that the phy object has gone link up. none
 */
static void scic_sds_phy_ready_state_enter(void *object)
{
	struct scic_sds_phy *sci_phy = object;

	scic_sds_phy_set_base_state_handlers(sci_phy, SCI_BASE_PHY_STATE_READY);

	scic_sds_controller_link_up(
		scic_sds_phy_get_controller(sci_phy),
		scic_sds_phy_get_port(sci_phy),
		sci_phy
		);
}

/**
 *
 * @object: This is the object which is cast to a struct scic_sds_phy object.
 *
 * This method will perform the actions required by the struct scic_sds_phy on exiting
 * the SCI_BASE_PHY_STATE_INITIAL. This function suspends the SCU hardware
 * protocol engine represented by this struct scic_sds_phy object. none
 */
static void scic_sds_phy_ready_state_exit(void *object)
{
	struct scic_sds_phy *sci_phy = object;

	scic_sds_phy_suspend(sci_phy);
}

/**
 *
 * @object: This is the object which is cast to a struct scic_sds_phy object.
 *
 * This method will perform the actions required by the struct scic_sds_phy on
 * entering the SCI_BASE_PHY_STATE_RESETTING. - This function sets the state
 * handlers for the phy object base state machine resetting state. none
 */
static void scic_sds_phy_resetting_state_enter(void *object)
{
	struct scic_sds_phy *sci_phy = object;

	scic_sds_phy_set_base_state_handlers(sci_phy, SCI_BASE_PHY_STATE_RESETTING);

	/*
	 * The phy is being reset, therefore deactivate it from the port.
	 * In the resetting state we don't notify the user regarding
	 * link up and link down notifications. */
	scic_sds_port_deactivate_phy(sci_phy->owning_port, sci_phy, false);

	if (sci_phy->protocol == SCIC_SDS_PHY_PROTOCOL_SAS) {
		scu_link_layer_tx_hard_reset(sci_phy);
	} else {
		/*
		 * The SCU does not need to have a discrete reset state so
		 * just go back to the starting state.
		 */
		sci_base_state_machine_change_state(
				&sci_phy->state_machine,
				SCI_BASE_PHY_STATE_STARTING);
	}
}

/**
 *
 * @object: This is the object which is cast to a struct scic_sds_phy object.
 *
 * This method will perform the actions required by the struct scic_sds_phy on
 * entering the SCI_BASE_PHY_STATE_FINAL. - This function sets the state
 * handlers for the phy object base state machine final state. none
 */
static void scic_sds_phy_final_state_enter(void *object)
{
	struct scic_sds_phy *sci_phy = object;

	scic_sds_phy_set_base_state_handlers(sci_phy, SCI_BASE_PHY_STATE_FINAL);

	/* Nothing to do here */
}

/* --------------------------------------------------------------------------- */

static const struct sci_base_state scic_sds_phy_state_table[] = {
	[SCI_BASE_PHY_STATE_INITIAL] = {
		.enter_state = scic_sds_phy_initial_state_enter,
	},
	[SCI_BASE_PHY_STATE_STOPPED] = {
		.enter_state = scic_sds_phy_stopped_state_enter,
	},
	[SCI_BASE_PHY_STATE_STARTING] = {
		.enter_state = scic_sds_phy_starting_state_enter,
	},
	[SCI_BASE_PHY_STATE_READY] = {
		.enter_state = scic_sds_phy_ready_state_enter,
		.exit_state = scic_sds_phy_ready_state_exit,
	},
	[SCI_BASE_PHY_STATE_RESETTING] = {
		.enter_state = scic_sds_phy_resetting_state_enter,
	},
	[SCI_BASE_PHY_STATE_FINAL] = {
		.enter_state = scic_sds_phy_final_state_enter,
	},
};

void scic_sds_phy_construct(struct scic_sds_phy *sci_phy,
			    struct scic_sds_port *owning_port, u8 phy_index)
{
	sci_base_state_machine_construct(&sci_phy->state_machine,
					 sci_phy,
					 scic_sds_phy_state_table,
					 SCI_BASE_PHY_STATE_INITIAL);

	sci_base_state_machine_start(&sci_phy->state_machine);

	/* Copy the rest of the input data to our locals */
	sci_phy->owning_port = owning_port;
	sci_phy->phy_index = phy_index;
	sci_phy->bcn_received_while_port_unassigned = false;
	sci_phy->protocol = SCIC_SDS_PHY_PROTOCOL_UNKNOWN;
	sci_phy->link_layer_registers = NULL;
	sci_phy->max_negotiated_speed = SAS_LINK_RATE_UNKNOWN;
	sci_phy->sata_timeout_timer = NULL;

	/* Initialize the the substate machines */
	sci_base_state_machine_construct(&sci_phy->starting_substate_machine,
					 sci_phy,
					 scic_sds_phy_starting_substates,
					 SCIC_SDS_PHY_STARTING_SUBSTATE_INITIAL);
}
