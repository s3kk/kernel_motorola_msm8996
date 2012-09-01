/*
 * Copyright (c) 2005-2010 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 *
 * Linux driver for Brocade Fibre Channel Host Bus Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include "bfad_drv.h"
#include "bfad_im.h"
#include "bfa_ioc.h"
#include "bfi_reg.h"
#include "bfa_defs.h"
#include "bfa_defs_svc.h"

BFA_TRC_FILE(CNA, IOC);

/*
 * IOC local definitions
 */
#define BFA_IOC_TOV		3000	/* msecs */
#define BFA_IOC_HWSEM_TOV	500	/* msecs */
#define BFA_IOC_HB_TOV		500	/* msecs */
#define BFA_IOC_TOV_RECOVER	 BFA_IOC_HB_TOV
#define BFA_IOC_POLL_TOV	BFA_TIMER_FREQ

#define bfa_ioc_timer_start(__ioc)					\
	bfa_timer_begin((__ioc)->timer_mod, &(__ioc)->ioc_timer,	\
			bfa_ioc_timeout, (__ioc), BFA_IOC_TOV)
#define bfa_ioc_timer_stop(__ioc)   bfa_timer_stop(&(__ioc)->ioc_timer)

#define bfa_hb_timer_start(__ioc)					\
	bfa_timer_begin((__ioc)->timer_mod, &(__ioc)->hb_timer,		\
			bfa_ioc_hb_check, (__ioc), BFA_IOC_HB_TOV)
#define bfa_hb_timer_stop(__ioc)	bfa_timer_stop(&(__ioc)->hb_timer)

#define BFA_DBG_FWTRC_OFF(_fn)	(BFI_IOC_TRC_OFF + BFA_DBG_FWTRC_LEN * (_fn))

/*
 * Asic specific macros : see bfa_hw_cb.c and bfa_hw_ct.c for details.
 */

#define bfa_ioc_firmware_lock(__ioc)			\
			((__ioc)->ioc_hwif->ioc_firmware_lock(__ioc))
#define bfa_ioc_firmware_unlock(__ioc)			\
			((__ioc)->ioc_hwif->ioc_firmware_unlock(__ioc))
#define bfa_ioc_reg_init(__ioc) ((__ioc)->ioc_hwif->ioc_reg_init(__ioc))
#define bfa_ioc_map_port(__ioc) ((__ioc)->ioc_hwif->ioc_map_port(__ioc))
#define bfa_ioc_notify_fail(__ioc)              \
			((__ioc)->ioc_hwif->ioc_notify_fail(__ioc))
#define bfa_ioc_sync_start(__ioc)               \
			((__ioc)->ioc_hwif->ioc_sync_start(__ioc))
#define bfa_ioc_sync_join(__ioc)                \
			((__ioc)->ioc_hwif->ioc_sync_join(__ioc))
#define bfa_ioc_sync_leave(__ioc)               \
			((__ioc)->ioc_hwif->ioc_sync_leave(__ioc))
#define bfa_ioc_sync_ack(__ioc)                 \
			((__ioc)->ioc_hwif->ioc_sync_ack(__ioc))
#define bfa_ioc_sync_complete(__ioc)            \
			((__ioc)->ioc_hwif->ioc_sync_complete(__ioc))

#define bfa_ioc_mbox_cmd_pending(__ioc)		\
			(!list_empty(&((__ioc)->mbox_mod.cmd_q)) || \
			readl((__ioc)->ioc_regs.hfn_mbox_cmd))

bfa_boolean_t bfa_auto_recover = BFA_TRUE;

/*
 * forward declarations
 */
static void bfa_ioc_hw_sem_get(struct bfa_ioc_s *ioc);
static void bfa_ioc_hwinit(struct bfa_ioc_s *ioc, bfa_boolean_t force);
static void bfa_ioc_timeout(void *ioc);
static void bfa_ioc_poll_fwinit(struct bfa_ioc_s *ioc);
static void bfa_ioc_send_enable(struct bfa_ioc_s *ioc);
static void bfa_ioc_send_disable(struct bfa_ioc_s *ioc);
static void bfa_ioc_send_getattr(struct bfa_ioc_s *ioc);
static void bfa_ioc_hb_monitor(struct bfa_ioc_s *ioc);
static void bfa_ioc_mbox_poll(struct bfa_ioc_s *ioc);
static void bfa_ioc_mbox_flush(struct bfa_ioc_s *ioc);
static void bfa_ioc_recover(struct bfa_ioc_s *ioc);
static void bfa_ioc_event_notify(struct bfa_ioc_s *ioc ,
				enum bfa_ioc_event_e event);
static void bfa_ioc_disable_comp(struct bfa_ioc_s *ioc);
static void bfa_ioc_lpu_stop(struct bfa_ioc_s *ioc);
static void bfa_ioc_debug_save_ftrc(struct bfa_ioc_s *ioc);
static void bfa_ioc_fail_notify(struct bfa_ioc_s *ioc);
static void bfa_ioc_pf_fwmismatch(struct bfa_ioc_s *ioc);

/*
 * IOC state machine definitions/declarations
 */
enum ioc_event {
	IOC_E_RESET		= 1,	/*  IOC reset request		*/
	IOC_E_ENABLE		= 2,	/*  IOC enable request		*/
	IOC_E_DISABLE		= 3,	/*  IOC disable request	*/
	IOC_E_DETACH		= 4,	/*  driver detach cleanup	*/
	IOC_E_ENABLED		= 5,	/*  f/w enabled		*/
	IOC_E_FWRSP_GETATTR	= 6,	/*  IOC get attribute response	*/
	IOC_E_DISABLED		= 7,	/*  f/w disabled		*/
	IOC_E_PFFAILED		= 8,	/*  failure notice by iocpf sm	*/
	IOC_E_HBFAIL		= 9,	/*  heartbeat failure		*/
	IOC_E_HWERROR		= 10,	/*  hardware error interrupt	*/
	IOC_E_TIMEOUT		= 11,	/*  timeout			*/
	IOC_E_HWFAILED		= 12,	/*  PCI mapping failure notice	*/
};

bfa_fsm_state_decl(bfa_ioc, uninit, struct bfa_ioc_s, enum ioc_event);
bfa_fsm_state_decl(bfa_ioc, reset, struct bfa_ioc_s, enum ioc_event);
bfa_fsm_state_decl(bfa_ioc, enabling, struct bfa_ioc_s, enum ioc_event);
bfa_fsm_state_decl(bfa_ioc, getattr, struct bfa_ioc_s, enum ioc_event);
bfa_fsm_state_decl(bfa_ioc, op, struct bfa_ioc_s, enum ioc_event);
bfa_fsm_state_decl(bfa_ioc, fail_retry, struct bfa_ioc_s, enum ioc_event);
bfa_fsm_state_decl(bfa_ioc, fail, struct bfa_ioc_s, enum ioc_event);
bfa_fsm_state_decl(bfa_ioc, disabling, struct bfa_ioc_s, enum ioc_event);
bfa_fsm_state_decl(bfa_ioc, disabled, struct bfa_ioc_s, enum ioc_event);
bfa_fsm_state_decl(bfa_ioc, hwfail, struct bfa_ioc_s, enum ioc_event);

static struct bfa_sm_table_s ioc_sm_table[] = {
	{BFA_SM(bfa_ioc_sm_uninit), BFA_IOC_UNINIT},
	{BFA_SM(bfa_ioc_sm_reset), BFA_IOC_RESET},
	{BFA_SM(bfa_ioc_sm_enabling), BFA_IOC_ENABLING},
	{BFA_SM(bfa_ioc_sm_getattr), BFA_IOC_GETATTR},
	{BFA_SM(bfa_ioc_sm_op), BFA_IOC_OPERATIONAL},
	{BFA_SM(bfa_ioc_sm_fail_retry), BFA_IOC_INITFAIL},
	{BFA_SM(bfa_ioc_sm_fail), BFA_IOC_FAIL},
	{BFA_SM(bfa_ioc_sm_disabling), BFA_IOC_DISABLING},
	{BFA_SM(bfa_ioc_sm_disabled), BFA_IOC_DISABLED},
	{BFA_SM(bfa_ioc_sm_hwfail), BFA_IOC_HWFAIL},
};

/*
 * IOCPF state machine definitions/declarations
 */

#define bfa_iocpf_timer_start(__ioc)					\
	bfa_timer_begin((__ioc)->timer_mod, &(__ioc)->ioc_timer,	\
			bfa_iocpf_timeout, (__ioc), BFA_IOC_TOV)
#define bfa_iocpf_timer_stop(__ioc)	bfa_timer_stop(&(__ioc)->ioc_timer)

#define bfa_iocpf_poll_timer_start(__ioc)				\
	bfa_timer_begin((__ioc)->timer_mod, &(__ioc)->ioc_timer,	\
			bfa_iocpf_poll_timeout, (__ioc), BFA_IOC_POLL_TOV)

#define bfa_sem_timer_start(__ioc)					\
	bfa_timer_begin((__ioc)->timer_mod, &(__ioc)->sem_timer,	\
			bfa_iocpf_sem_timeout, (__ioc), BFA_IOC_HWSEM_TOV)
#define bfa_sem_timer_stop(__ioc)	bfa_timer_stop(&(__ioc)->sem_timer)

/*
 * Forward declareations for iocpf state machine
 */
static void bfa_iocpf_timeout(void *ioc_arg);
static void bfa_iocpf_sem_timeout(void *ioc_arg);
static void bfa_iocpf_poll_timeout(void *ioc_arg);

/*
 * IOCPF state machine events
 */
enum iocpf_event {
	IOCPF_E_ENABLE		= 1,	/*  IOCPF enable request	*/
	IOCPF_E_DISABLE		= 2,	/*  IOCPF disable request	*/
	IOCPF_E_STOP		= 3,	/*  stop on driver detach	*/
	IOCPF_E_FWREADY		= 4,	/*  f/w initialization done	*/
	IOCPF_E_FWRSP_ENABLE	= 5,	/*  enable f/w response	*/
	IOCPF_E_FWRSP_DISABLE	= 6,	/*  disable f/w response	*/
	IOCPF_E_FAIL		= 7,	/*  failure notice by ioc sm	*/
	IOCPF_E_INITFAIL	= 8,	/*  init fail notice by ioc sm	*/
	IOCPF_E_GETATTRFAIL	= 9,	/*  init fail notice by ioc sm	*/
	IOCPF_E_SEMLOCKED	= 10,	/*  h/w semaphore is locked	*/
	IOCPF_E_TIMEOUT		= 11,	/*  f/w response timeout	*/
	IOCPF_E_SEM_ERROR	= 12,	/*  h/w sem mapping error	*/
};

/*
 * IOCPF states
 */
enum bfa_iocpf_state {
	BFA_IOCPF_RESET		= 1,	/*  IOC is in reset state */
	BFA_IOCPF_SEMWAIT	= 2,	/*  Waiting for IOC h/w semaphore */
	BFA_IOCPF_HWINIT	= 3,	/*  IOC h/w is being initialized */
	BFA_IOCPF_READY		= 4,	/*  IOCPF is initialized */
	BFA_IOCPF_INITFAIL	= 5,	/*  IOCPF failed */
	BFA_IOCPF_FAIL		= 6,	/*  IOCPF failed */
	BFA_IOCPF_DISABLING	= 7,	/*  IOCPF is being disabled */
	BFA_IOCPF_DISABLED	= 8,	/*  IOCPF is disabled */
	BFA_IOCPF_FWMISMATCH	= 9,	/*  IOC f/w different from drivers */
};

bfa_fsm_state_decl(bfa_iocpf, reset, struct bfa_iocpf_s, enum iocpf_event);
bfa_fsm_state_decl(bfa_iocpf, fwcheck, struct bfa_iocpf_s, enum iocpf_event);
bfa_fsm_state_decl(bfa_iocpf, mismatch, struct bfa_iocpf_s, enum iocpf_event);
bfa_fsm_state_decl(bfa_iocpf, semwait, struct bfa_iocpf_s, enum iocpf_event);
bfa_fsm_state_decl(bfa_iocpf, hwinit, struct bfa_iocpf_s, enum iocpf_event);
bfa_fsm_state_decl(bfa_iocpf, enabling, struct bfa_iocpf_s, enum iocpf_event);
bfa_fsm_state_decl(bfa_iocpf, ready, struct bfa_iocpf_s, enum iocpf_event);
bfa_fsm_state_decl(bfa_iocpf, initfail_sync, struct bfa_iocpf_s,
						enum iocpf_event);
bfa_fsm_state_decl(bfa_iocpf, initfail, struct bfa_iocpf_s, enum iocpf_event);
bfa_fsm_state_decl(bfa_iocpf, fail_sync, struct bfa_iocpf_s, enum iocpf_event);
bfa_fsm_state_decl(bfa_iocpf, fail, struct bfa_iocpf_s, enum iocpf_event);
bfa_fsm_state_decl(bfa_iocpf, disabling, struct bfa_iocpf_s, enum iocpf_event);
bfa_fsm_state_decl(bfa_iocpf, disabling_sync, struct bfa_iocpf_s,
						enum iocpf_event);
bfa_fsm_state_decl(bfa_iocpf, disabled, struct bfa_iocpf_s, enum iocpf_event);

static struct bfa_sm_table_s iocpf_sm_table[] = {
	{BFA_SM(bfa_iocpf_sm_reset), BFA_IOCPF_RESET},
	{BFA_SM(bfa_iocpf_sm_fwcheck), BFA_IOCPF_FWMISMATCH},
	{BFA_SM(bfa_iocpf_sm_mismatch), BFA_IOCPF_FWMISMATCH},
	{BFA_SM(bfa_iocpf_sm_semwait), BFA_IOCPF_SEMWAIT},
	{BFA_SM(bfa_iocpf_sm_hwinit), BFA_IOCPF_HWINIT},
	{BFA_SM(bfa_iocpf_sm_enabling), BFA_IOCPF_HWINIT},
	{BFA_SM(bfa_iocpf_sm_ready), BFA_IOCPF_READY},
	{BFA_SM(bfa_iocpf_sm_initfail_sync), BFA_IOCPF_INITFAIL},
	{BFA_SM(bfa_iocpf_sm_initfail), BFA_IOCPF_INITFAIL},
	{BFA_SM(bfa_iocpf_sm_fail_sync), BFA_IOCPF_FAIL},
	{BFA_SM(bfa_iocpf_sm_fail), BFA_IOCPF_FAIL},
	{BFA_SM(bfa_iocpf_sm_disabling), BFA_IOCPF_DISABLING},
	{BFA_SM(bfa_iocpf_sm_disabling_sync), BFA_IOCPF_DISABLING},
	{BFA_SM(bfa_iocpf_sm_disabled), BFA_IOCPF_DISABLED},
};

/*
 * IOC State Machine
 */

/*
 * Beginning state. IOC uninit state.
 */

static void
bfa_ioc_sm_uninit_entry(struct bfa_ioc_s *ioc)
{
}

/*
 * IOC is in uninit state.
 */
static void
bfa_ioc_sm_uninit(struct bfa_ioc_s *ioc, enum ioc_event event)
{
	bfa_trc(ioc, event);

	switch (event) {
	case IOC_E_RESET:
		bfa_fsm_set_state(ioc, bfa_ioc_sm_reset);
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}
/*
 * Reset entry actions -- initialize state machine
 */
static void
bfa_ioc_sm_reset_entry(struct bfa_ioc_s *ioc)
{
	bfa_fsm_set_state(&ioc->iocpf, bfa_iocpf_sm_reset);
}

/*
 * IOC is in reset state.
 */
static void
bfa_ioc_sm_reset(struct bfa_ioc_s *ioc, enum ioc_event event)
{
	bfa_trc(ioc, event);

	switch (event) {
	case IOC_E_ENABLE:
		bfa_fsm_set_state(ioc, bfa_ioc_sm_enabling);
		break;

	case IOC_E_DISABLE:
		bfa_ioc_disable_comp(ioc);
		break;

	case IOC_E_DETACH:
		bfa_fsm_set_state(ioc, bfa_ioc_sm_uninit);
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}


static void
bfa_ioc_sm_enabling_entry(struct bfa_ioc_s *ioc)
{
	bfa_fsm_send_event(&ioc->iocpf, IOCPF_E_ENABLE);
}

/*
 * Host IOC function is being enabled, awaiting response from firmware.
 * Semaphore is acquired.
 */
static void
bfa_ioc_sm_enabling(struct bfa_ioc_s *ioc, enum ioc_event event)
{
	bfa_trc(ioc, event);

	switch (event) {
	case IOC_E_ENABLED:
		bfa_fsm_set_state(ioc, bfa_ioc_sm_getattr);
		break;

	case IOC_E_PFFAILED:
		/* !!! fall through !!! */
	case IOC_E_HWERROR:
		ioc->cbfn->enable_cbfn(ioc->bfa, BFA_STATUS_IOC_FAILURE);
		bfa_fsm_set_state(ioc, bfa_ioc_sm_fail);
		if (event != IOC_E_PFFAILED)
			bfa_fsm_send_event(&ioc->iocpf, IOCPF_E_INITFAIL);
		break;

	case IOC_E_HWFAILED:
		ioc->cbfn->enable_cbfn(ioc->bfa, BFA_STATUS_IOC_FAILURE);
		bfa_fsm_set_state(ioc, bfa_ioc_sm_hwfail);
		break;

	case IOC_E_DISABLE:
		bfa_fsm_set_state(ioc, bfa_ioc_sm_disabling);
		break;

	case IOC_E_DETACH:
		bfa_fsm_set_state(ioc, bfa_ioc_sm_uninit);
		bfa_fsm_send_event(&ioc->iocpf, IOCPF_E_STOP);
		break;

	case IOC_E_ENABLE:
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}


static void
bfa_ioc_sm_getattr_entry(struct bfa_ioc_s *ioc)
{
	bfa_ioc_timer_start(ioc);
	bfa_ioc_send_getattr(ioc);
}

/*
 * IOC configuration in progress. Timer is active.
 */
static void
bfa_ioc_sm_getattr(struct bfa_ioc_s *ioc, enum ioc_event event)
{
	bfa_trc(ioc, event);

	switch (event) {
	case IOC_E_FWRSP_GETATTR:
		bfa_ioc_timer_stop(ioc);
		bfa_fsm_set_state(ioc, bfa_ioc_sm_op);
		break;

	case IOC_E_PFFAILED:
	case IOC_E_HWERROR:
		bfa_ioc_timer_stop(ioc);
		/* !!! fall through !!! */
	case IOC_E_TIMEOUT:
		ioc->cbfn->enable_cbfn(ioc->bfa, BFA_STATUS_IOC_FAILURE);
		bfa_fsm_set_state(ioc, bfa_ioc_sm_fail);
		if (event != IOC_E_PFFAILED)
			bfa_fsm_send_event(&ioc->iocpf, IOCPF_E_GETATTRFAIL);
		break;

	case IOC_E_DISABLE:
		bfa_ioc_timer_stop(ioc);
		bfa_fsm_set_state(ioc, bfa_ioc_sm_disabling);
		break;

	case IOC_E_ENABLE:
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}

static void
bfa_ioc_sm_op_entry(struct bfa_ioc_s *ioc)
{
	struct bfad_s *bfad = (struct bfad_s *)ioc->bfa->bfad;

	ioc->cbfn->enable_cbfn(ioc->bfa, BFA_STATUS_OK);
	bfa_ioc_event_notify(ioc, BFA_IOC_E_ENABLED);
	bfa_ioc_hb_monitor(ioc);
	BFA_LOG(KERN_INFO, bfad, bfa_log_level, "IOC enabled\n");
	bfa_ioc_aen_post(ioc, BFA_IOC_AEN_ENABLE);
}

static void
bfa_ioc_sm_op(struct bfa_ioc_s *ioc, enum ioc_event event)
{
	bfa_trc(ioc, event);

	switch (event) {
	case IOC_E_ENABLE:
		break;

	case IOC_E_DISABLE:
		bfa_hb_timer_stop(ioc);
		bfa_fsm_set_state(ioc, bfa_ioc_sm_disabling);
		break;

	case IOC_E_PFFAILED:
	case IOC_E_HWERROR:
		bfa_hb_timer_stop(ioc);
		/* !!! fall through !!! */
	case IOC_E_HBFAIL:
		if (ioc->iocpf.auto_recover)
			bfa_fsm_set_state(ioc, bfa_ioc_sm_fail_retry);
		else
			bfa_fsm_set_state(ioc, bfa_ioc_sm_fail);

		bfa_ioc_fail_notify(ioc);

		if (event != IOC_E_PFFAILED)
			bfa_fsm_send_event(&ioc->iocpf, IOCPF_E_FAIL);
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}


static void
bfa_ioc_sm_disabling_entry(struct bfa_ioc_s *ioc)
{
	struct bfad_s *bfad = (struct bfad_s *)ioc->bfa->bfad;
	bfa_fsm_send_event(&ioc->iocpf, IOCPF_E_DISABLE);
	BFA_LOG(KERN_INFO, bfad, bfa_log_level, "IOC disabled\n");
	bfa_ioc_aen_post(ioc, BFA_IOC_AEN_DISABLE);
}

/*
 * IOC is being disabled
 */
static void
bfa_ioc_sm_disabling(struct bfa_ioc_s *ioc, enum ioc_event event)
{
	bfa_trc(ioc, event);

	switch (event) {
	case IOC_E_DISABLED:
		bfa_fsm_set_state(ioc, bfa_ioc_sm_disabled);
		break;

	case IOC_E_HWERROR:
		/*
		 * No state change.  Will move to disabled state
		 * after iocpf sm completes failure processing and
		 * moves to disabled state.
		 */
		bfa_fsm_send_event(&ioc->iocpf, IOCPF_E_FAIL);
		break;

	case IOC_E_HWFAILED:
		bfa_fsm_set_state(ioc, bfa_ioc_sm_hwfail);
		bfa_ioc_disable_comp(ioc);
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}

/*
 * IOC disable completion entry.
 */
static void
bfa_ioc_sm_disabled_entry(struct bfa_ioc_s *ioc)
{
	bfa_ioc_disable_comp(ioc);
}

static void
bfa_ioc_sm_disabled(struct bfa_ioc_s *ioc, enum ioc_event event)
{
	bfa_trc(ioc, event);

	switch (event) {
	case IOC_E_ENABLE:
		bfa_fsm_set_state(ioc, bfa_ioc_sm_enabling);
		break;

	case IOC_E_DISABLE:
		ioc->cbfn->disable_cbfn(ioc->bfa);
		break;

	case IOC_E_DETACH:
		bfa_fsm_set_state(ioc, bfa_ioc_sm_uninit);
		bfa_fsm_send_event(&ioc->iocpf, IOCPF_E_STOP);
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}


static void
bfa_ioc_sm_fail_retry_entry(struct bfa_ioc_s *ioc)
{
	bfa_trc(ioc, 0);
}

/*
 * Hardware initialization retry.
 */
static void
bfa_ioc_sm_fail_retry(struct bfa_ioc_s *ioc, enum ioc_event event)
{
	bfa_trc(ioc, event);

	switch (event) {
	case IOC_E_ENABLED:
		bfa_fsm_set_state(ioc, bfa_ioc_sm_getattr);
		break;

	case IOC_E_PFFAILED:
	case IOC_E_HWERROR:
		/*
		 * Initialization retry failed.
		 */
		ioc->cbfn->enable_cbfn(ioc->bfa, BFA_STATUS_IOC_FAILURE);
		bfa_fsm_set_state(ioc, bfa_ioc_sm_fail);
		if (event != IOC_E_PFFAILED)
			bfa_fsm_send_event(&ioc->iocpf, IOCPF_E_INITFAIL);
		break;

	case IOC_E_HWFAILED:
		ioc->cbfn->enable_cbfn(ioc->bfa, BFA_STATUS_IOC_FAILURE);
		bfa_fsm_set_state(ioc, bfa_ioc_sm_hwfail);
		break;

	case IOC_E_ENABLE:
		break;

	case IOC_E_DISABLE:
		bfa_fsm_set_state(ioc, bfa_ioc_sm_disabling);
		break;

	case IOC_E_DETACH:
		bfa_fsm_set_state(ioc, bfa_ioc_sm_uninit);
		bfa_fsm_send_event(&ioc->iocpf, IOCPF_E_STOP);
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}


static void
bfa_ioc_sm_fail_entry(struct bfa_ioc_s *ioc)
{
	bfa_trc(ioc, 0);
}

/*
 * IOC failure.
 */
static void
bfa_ioc_sm_fail(struct bfa_ioc_s *ioc, enum ioc_event event)
{
	bfa_trc(ioc, event);

	switch (event) {

	case IOC_E_ENABLE:
		ioc->cbfn->enable_cbfn(ioc->bfa, BFA_STATUS_IOC_FAILURE);
		break;

	case IOC_E_DISABLE:
		bfa_fsm_set_state(ioc, bfa_ioc_sm_disabling);
		break;

	case IOC_E_DETACH:
		bfa_fsm_set_state(ioc, bfa_ioc_sm_uninit);
		bfa_fsm_send_event(&ioc->iocpf, IOCPF_E_STOP);
		break;

	case IOC_E_HWERROR:
		/*
		 * HB failure notification, ignore.
		 */
		break;
	default:
		bfa_sm_fault(ioc, event);
	}
}

static void
bfa_ioc_sm_hwfail_entry(struct bfa_ioc_s *ioc)
{
	bfa_trc(ioc, 0);
}

static void
bfa_ioc_sm_hwfail(struct bfa_ioc_s *ioc, enum ioc_event event)
{
	bfa_trc(ioc, event);

	switch (event) {
	case IOC_E_ENABLE:
		ioc->cbfn->enable_cbfn(ioc->bfa, BFA_STATUS_IOC_FAILURE);
		break;

	case IOC_E_DISABLE:
		ioc->cbfn->disable_cbfn(ioc->bfa);
		break;

	case IOC_E_DETACH:
		bfa_fsm_set_state(ioc, bfa_ioc_sm_uninit);
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}

/*
 * IOCPF State Machine
 */

/*
 * Reset entry actions -- initialize state machine
 */
static void
bfa_iocpf_sm_reset_entry(struct bfa_iocpf_s *iocpf)
{
	iocpf->fw_mismatch_notified = BFA_FALSE;
	iocpf->auto_recover = bfa_auto_recover;
}

/*
 * Beginning state. IOC is in reset state.
 */
static void
bfa_iocpf_sm_reset(struct bfa_iocpf_s *iocpf, enum iocpf_event event)
{
	struct bfa_ioc_s *ioc = iocpf->ioc;

	bfa_trc(ioc, event);

	switch (event) {
	case IOCPF_E_ENABLE:
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_fwcheck);
		break;

	case IOCPF_E_STOP:
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}

/*
 * Semaphore should be acquired for version check.
 */
static void
bfa_iocpf_sm_fwcheck_entry(struct bfa_iocpf_s *iocpf)
{
	struct bfi_ioc_image_hdr_s	fwhdr;
	u32	r32, fwstate, pgnum, pgoff, loff = 0;
	int	i;

	/*
	 * Spin on init semaphore to serialize.
	 */
	r32 = readl(iocpf->ioc->ioc_regs.ioc_init_sem_reg);
	while (r32 & 0x1) {
		udelay(20);
		r32 = readl(iocpf->ioc->ioc_regs.ioc_init_sem_reg);
	}

	/* h/w sem init */
	fwstate = readl(iocpf->ioc->ioc_regs.ioc_fwstate);
	if (fwstate == BFI_IOC_UNINIT) {
		writel(1, iocpf->ioc->ioc_regs.ioc_init_sem_reg);
		goto sem_get;
	}

	bfa_ioc_fwver_get(iocpf->ioc, &fwhdr);

	if (swab32(fwhdr.exec) == BFI_FWBOOT_TYPE_NORMAL) {
		writel(1, iocpf->ioc->ioc_regs.ioc_init_sem_reg);
		goto sem_get;
	}

	/*
	 * Clear fwver hdr
	 */
	pgnum = PSS_SMEM_PGNUM(iocpf->ioc->ioc_regs.smem_pg0, loff);
	pgoff = PSS_SMEM_PGOFF(loff);
	writel(pgnum, iocpf->ioc->ioc_regs.host_page_num_fn);

	for (i = 0; i < sizeof(struct bfi_ioc_image_hdr_s) / sizeof(u32); i++) {
		bfa_mem_write(iocpf->ioc->ioc_regs.smem_page_start, loff, 0);
		loff += sizeof(u32);
	}

	bfa_trc(iocpf->ioc, fwstate);
	bfa_trc(iocpf->ioc, swab32(fwhdr.exec));
	writel(BFI_IOC_UNINIT, iocpf->ioc->ioc_regs.ioc_fwstate);
	writel(BFI_IOC_UNINIT, iocpf->ioc->ioc_regs.alt_ioc_fwstate);

	/*
	 * Unlock the hw semaphore. Should be here only once per boot.
	 */
	readl(iocpf->ioc->ioc_regs.ioc_sem_reg);
	writel(1, iocpf->ioc->ioc_regs.ioc_sem_reg);

	/*
	 * unlock init semaphore.
	 */
	writel(1, iocpf->ioc->ioc_regs.ioc_init_sem_reg);

sem_get:
	bfa_ioc_hw_sem_get(iocpf->ioc);
}

/*
 * Awaiting h/w semaphore to continue with version check.
 */
static void
bfa_iocpf_sm_fwcheck(struct bfa_iocpf_s *iocpf, enum iocpf_event event)
{
	struct bfa_ioc_s *ioc = iocpf->ioc;

	bfa_trc(ioc, event);

	switch (event) {
	case IOCPF_E_SEMLOCKED:
		if (bfa_ioc_firmware_lock(ioc)) {
			if (bfa_ioc_sync_start(ioc)) {
				bfa_ioc_sync_join(ioc);
				bfa_fsm_set_state(iocpf, bfa_iocpf_sm_hwinit);
			} else {
				bfa_ioc_firmware_unlock(ioc);
				writel(1, ioc->ioc_regs.ioc_sem_reg);
				bfa_sem_timer_start(ioc);
			}
		} else {
			writel(1, ioc->ioc_regs.ioc_sem_reg);
			bfa_fsm_set_state(iocpf, bfa_iocpf_sm_mismatch);
		}
		break;

	case IOCPF_E_SEM_ERROR:
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_fail);
		bfa_fsm_send_event(ioc, IOC_E_HWFAILED);
		break;

	case IOCPF_E_DISABLE:
		bfa_sem_timer_stop(ioc);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_reset);
		bfa_fsm_send_event(ioc, IOC_E_DISABLED);
		break;

	case IOCPF_E_STOP:
		bfa_sem_timer_stop(ioc);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_reset);
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}

/*
 * Notify enable completion callback.
 */
static void
bfa_iocpf_sm_mismatch_entry(struct bfa_iocpf_s *iocpf)
{
	/*
	 * Call only the first time sm enters fwmismatch state.
	 */
	if (iocpf->fw_mismatch_notified == BFA_FALSE)
		bfa_ioc_pf_fwmismatch(iocpf->ioc);

	iocpf->fw_mismatch_notified = BFA_TRUE;
	bfa_iocpf_timer_start(iocpf->ioc);
}

/*
 * Awaiting firmware version match.
 */
static void
bfa_iocpf_sm_mismatch(struct bfa_iocpf_s *iocpf, enum iocpf_event event)
{
	struct bfa_ioc_s *ioc = iocpf->ioc;

	bfa_trc(ioc, event);

	switch (event) {
	case IOCPF_E_TIMEOUT:
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_fwcheck);
		break;

	case IOCPF_E_DISABLE:
		bfa_iocpf_timer_stop(ioc);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_reset);
		bfa_fsm_send_event(ioc, IOC_E_DISABLED);
		break;

	case IOCPF_E_STOP:
		bfa_iocpf_timer_stop(ioc);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_reset);
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}

/*
 * Request for semaphore.
 */
static void
bfa_iocpf_sm_semwait_entry(struct bfa_iocpf_s *iocpf)
{
	bfa_ioc_hw_sem_get(iocpf->ioc);
}

/*
 * Awaiting semaphore for h/w initialzation.
 */
static void
bfa_iocpf_sm_semwait(struct bfa_iocpf_s *iocpf, enum iocpf_event event)
{
	struct bfa_ioc_s *ioc = iocpf->ioc;

	bfa_trc(ioc, event);

	switch (event) {
	case IOCPF_E_SEMLOCKED:
		if (bfa_ioc_sync_complete(ioc)) {
			bfa_ioc_sync_join(ioc);
			bfa_fsm_set_state(iocpf, bfa_iocpf_sm_hwinit);
		} else {
			writel(1, ioc->ioc_regs.ioc_sem_reg);
			bfa_sem_timer_start(ioc);
		}
		break;

	case IOCPF_E_SEM_ERROR:
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_fail);
		bfa_fsm_send_event(ioc, IOC_E_HWFAILED);
		break;

	case IOCPF_E_DISABLE:
		bfa_sem_timer_stop(ioc);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_disabling_sync);
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}

static void
bfa_iocpf_sm_hwinit_entry(struct bfa_iocpf_s *iocpf)
{
	iocpf->poll_time = 0;
	bfa_ioc_hwinit(iocpf->ioc, BFA_FALSE);
}

/*
 * Hardware is being initialized. Interrupts are enabled.
 * Holding hardware semaphore lock.
 */
static void
bfa_iocpf_sm_hwinit(struct bfa_iocpf_s *iocpf, enum iocpf_event event)
{
	struct bfa_ioc_s *ioc = iocpf->ioc;

	bfa_trc(ioc, event);

	switch (event) {
	case IOCPF_E_FWREADY:
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_enabling);
		break;

	case IOCPF_E_TIMEOUT:
		writel(1, ioc->ioc_regs.ioc_sem_reg);
		bfa_fsm_send_event(ioc, IOC_E_PFFAILED);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_initfail_sync);
		break;

	case IOCPF_E_DISABLE:
		bfa_iocpf_timer_stop(ioc);
		bfa_ioc_sync_leave(ioc);
		writel(1, ioc->ioc_regs.ioc_sem_reg);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_disabled);
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}

static void
bfa_iocpf_sm_enabling_entry(struct bfa_iocpf_s *iocpf)
{
	bfa_iocpf_timer_start(iocpf->ioc);
	/*
	 * Enable Interrupts before sending fw IOC ENABLE cmd.
	 */
	iocpf->ioc->cbfn->reset_cbfn(iocpf->ioc->bfa);
	bfa_ioc_send_enable(iocpf->ioc);
}

/*
 * Host IOC function is being enabled, awaiting response from firmware.
 * Semaphore is acquired.
 */
static void
bfa_iocpf_sm_enabling(struct bfa_iocpf_s *iocpf, enum iocpf_event event)
{
	struct bfa_ioc_s *ioc = iocpf->ioc;

	bfa_trc(ioc, event);

	switch (event) {
	case IOCPF_E_FWRSP_ENABLE:
		bfa_iocpf_timer_stop(ioc);
		writel(1, ioc->ioc_regs.ioc_sem_reg);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_ready);
		break;

	case IOCPF_E_INITFAIL:
		bfa_iocpf_timer_stop(ioc);
		/*
		 * !!! fall through !!!
		 */

	case IOCPF_E_TIMEOUT:
		writel(1, ioc->ioc_regs.ioc_sem_reg);
		if (event == IOCPF_E_TIMEOUT)
			bfa_fsm_send_event(ioc, IOC_E_PFFAILED);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_initfail_sync);
		break;

	case IOCPF_E_DISABLE:
		bfa_iocpf_timer_stop(ioc);
		writel(1, ioc->ioc_regs.ioc_sem_reg);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_disabling);
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}

static void
bfa_iocpf_sm_ready_entry(struct bfa_iocpf_s *iocpf)
{
	bfa_fsm_send_event(iocpf->ioc, IOC_E_ENABLED);
}

static void
bfa_iocpf_sm_ready(struct bfa_iocpf_s *iocpf, enum iocpf_event event)
{
	struct bfa_ioc_s *ioc = iocpf->ioc;

	bfa_trc(ioc, event);

	switch (event) {
	case IOCPF_E_DISABLE:
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_disabling);
		break;

	case IOCPF_E_GETATTRFAIL:
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_initfail_sync);
		break;

	case IOCPF_E_FAIL:
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_fail_sync);
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}

static void
bfa_iocpf_sm_disabling_entry(struct bfa_iocpf_s *iocpf)
{
	bfa_iocpf_timer_start(iocpf->ioc);
	bfa_ioc_send_disable(iocpf->ioc);
}

/*
 * IOC is being disabled
 */
static void
bfa_iocpf_sm_disabling(struct bfa_iocpf_s *iocpf, enum iocpf_event event)
{
	struct bfa_ioc_s *ioc = iocpf->ioc;

	bfa_trc(ioc, event);

	switch (event) {
	case IOCPF_E_FWRSP_DISABLE:
		bfa_iocpf_timer_stop(ioc);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_disabling_sync);
		break;

	case IOCPF_E_FAIL:
		bfa_iocpf_timer_stop(ioc);
		/*
		 * !!! fall through !!!
		 */

	case IOCPF_E_TIMEOUT:
		writel(BFI_IOC_FAIL, ioc->ioc_regs.ioc_fwstate);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_disabling_sync);
		break;

	case IOCPF_E_FWRSP_ENABLE:
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}

static void
bfa_iocpf_sm_disabling_sync_entry(struct bfa_iocpf_s *iocpf)
{
	bfa_ioc_hw_sem_get(iocpf->ioc);
}

/*
 * IOC hb ack request is being removed.
 */
static void
bfa_iocpf_sm_disabling_sync(struct bfa_iocpf_s *iocpf, enum iocpf_event event)
{
	struct bfa_ioc_s *ioc = iocpf->ioc;

	bfa_trc(ioc, event);

	switch (event) {
	case IOCPF_E_SEMLOCKED:
		bfa_ioc_sync_leave(ioc);
		writel(1, ioc->ioc_regs.ioc_sem_reg);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_disabled);
		break;

	case IOCPF_E_SEM_ERROR:
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_fail);
		bfa_fsm_send_event(ioc, IOC_E_HWFAILED);
		break;

	case IOCPF_E_FAIL:
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}

/*
 * IOC disable completion entry.
 */
static void
bfa_iocpf_sm_disabled_entry(struct bfa_iocpf_s *iocpf)
{
	bfa_ioc_mbox_flush(iocpf->ioc);
	bfa_fsm_send_event(iocpf->ioc, IOC_E_DISABLED);
}

static void
bfa_iocpf_sm_disabled(struct bfa_iocpf_s *iocpf, enum iocpf_event event)
{
	struct bfa_ioc_s *ioc = iocpf->ioc;

	bfa_trc(ioc, event);

	switch (event) {
	case IOCPF_E_ENABLE:
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_semwait);
		break;

	case IOCPF_E_STOP:
		bfa_ioc_firmware_unlock(ioc);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_reset);
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}

static void
bfa_iocpf_sm_initfail_sync_entry(struct bfa_iocpf_s *iocpf)
{
	bfa_ioc_debug_save_ftrc(iocpf->ioc);
	bfa_ioc_hw_sem_get(iocpf->ioc);
}

/*
 * Hardware initialization failed.
 */
static void
bfa_iocpf_sm_initfail_sync(struct bfa_iocpf_s *iocpf, enum iocpf_event event)
{
	struct bfa_ioc_s *ioc = iocpf->ioc;

	bfa_trc(ioc, event);

	switch (event) {
	case IOCPF_E_SEMLOCKED:
		bfa_ioc_notify_fail(ioc);
		bfa_ioc_sync_leave(ioc);
		writel(BFI_IOC_FAIL, ioc->ioc_regs.ioc_fwstate);
		writel(1, ioc->ioc_regs.ioc_sem_reg);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_initfail);
		break;

	case IOCPF_E_SEM_ERROR:
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_fail);
		bfa_fsm_send_event(ioc, IOC_E_HWFAILED);
		break;

	case IOCPF_E_DISABLE:
		bfa_sem_timer_stop(ioc);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_disabling_sync);
		break;

	case IOCPF_E_STOP:
		bfa_sem_timer_stop(ioc);
		bfa_ioc_firmware_unlock(ioc);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_reset);
		break;

	case IOCPF_E_FAIL:
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}

static void
bfa_iocpf_sm_initfail_entry(struct bfa_iocpf_s *iocpf)
{
	bfa_trc(iocpf->ioc, 0);
}

/*
 * Hardware initialization failed.
 */
static void
bfa_iocpf_sm_initfail(struct bfa_iocpf_s *iocpf, enum iocpf_event event)
{
	struct bfa_ioc_s *ioc = iocpf->ioc;

	bfa_trc(ioc, event);

	switch (event) {
	case IOCPF_E_DISABLE:
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_disabled);
		break;

	case IOCPF_E_STOP:
		bfa_ioc_firmware_unlock(ioc);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_reset);
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}

static void
bfa_iocpf_sm_fail_sync_entry(struct bfa_iocpf_s *iocpf)
{
	/*
	 * Mark IOC as failed in hardware and stop firmware.
	 */
	bfa_ioc_lpu_stop(iocpf->ioc);

	/*
	 * Flush any queued up mailbox requests.
	 */
	bfa_ioc_mbox_flush(iocpf->ioc);

	bfa_ioc_hw_sem_get(iocpf->ioc);
}

static void
bfa_iocpf_sm_fail_sync(struct bfa_iocpf_s *iocpf, enum iocpf_event event)
{
	struct bfa_ioc_s *ioc = iocpf->ioc;

	bfa_trc(ioc, event);

	switch (event) {
	case IOCPF_E_SEMLOCKED:
		bfa_ioc_sync_ack(ioc);
		bfa_ioc_notify_fail(ioc);
		if (!iocpf->auto_recover) {
			bfa_ioc_sync_leave(ioc);
			writel(BFI_IOC_FAIL, ioc->ioc_regs.ioc_fwstate);
			writel(1, ioc->ioc_regs.ioc_sem_reg);
			bfa_fsm_set_state(iocpf, bfa_iocpf_sm_fail);
		} else {
			if (bfa_ioc_sync_complete(ioc))
				bfa_fsm_set_state(iocpf, bfa_iocpf_sm_hwinit);
			else {
				writel(1, ioc->ioc_regs.ioc_sem_reg);
				bfa_fsm_set_state(iocpf, bfa_iocpf_sm_semwait);
			}
		}
		break;

	case IOCPF_E_SEM_ERROR:
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_fail);
		bfa_fsm_send_event(ioc, IOC_E_HWFAILED);
		break;

	case IOCPF_E_DISABLE:
		bfa_sem_timer_stop(ioc);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_disabling_sync);
		break;

	case IOCPF_E_FAIL:
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}

static void
bfa_iocpf_sm_fail_entry(struct bfa_iocpf_s *iocpf)
{
	bfa_trc(iocpf->ioc, 0);
}

/*
 * IOC is in failed state.
 */
static void
bfa_iocpf_sm_fail(struct bfa_iocpf_s *iocpf, enum iocpf_event event)
{
	struct bfa_ioc_s *ioc = iocpf->ioc;

	bfa_trc(ioc, event);

	switch (event) {
	case IOCPF_E_DISABLE:
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_disabled);
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}

/*
 *  BFA IOC private functions
 */

/*
 * Notify common modules registered for notification.
 */
static void
bfa_ioc_event_notify(struct bfa_ioc_s *ioc, enum bfa_ioc_event_e event)
{
	struct bfa_ioc_notify_s	*notify;
	struct list_head	*qe;

	list_for_each(qe, &ioc->notify_q) {
		notify = (struct bfa_ioc_notify_s *)qe;
		notify->cbfn(notify->cbarg, event);
	}
}

static void
bfa_ioc_disable_comp(struct bfa_ioc_s *ioc)
{
	ioc->cbfn->disable_cbfn(ioc->bfa);
	bfa_ioc_event_notify(ioc, BFA_IOC_E_DISABLED);
}

bfa_boolean_t
bfa_ioc_sem_get(void __iomem *sem_reg)
{
	u32 r32;
	int cnt = 0;
#define BFA_SEM_SPINCNT	3000

	r32 = readl(sem_reg);

	while ((r32 & 1) && (cnt < BFA_SEM_SPINCNT)) {
		cnt++;
		udelay(2);
		r32 = readl(sem_reg);
	}

	if (!(r32 & 1))
		return BFA_TRUE;

	return BFA_FALSE;
}

static void
bfa_ioc_hw_sem_get(struct bfa_ioc_s *ioc)
{
	u32	r32;

	/*
	 * First read to the semaphore register will return 0, subsequent reads
	 * will return 1. Semaphore is released by writing 1 to the register
	 */
	r32 = readl(ioc->ioc_regs.ioc_sem_reg);
	if (r32 == ~0) {
		WARN_ON(r32 == ~0);
		bfa_fsm_send_event(&ioc->iocpf, IOCPF_E_SEM_ERROR);
		return;
	}
	if (!(r32 & 1)) {
		bfa_fsm_send_event(&ioc->iocpf, IOCPF_E_SEMLOCKED);
		return;
	}

	bfa_sem_timer_start(ioc);
}

/*
 * Initialize LPU local memory (aka secondary memory / SRAM)
 */
static void
bfa_ioc_lmem_init(struct bfa_ioc_s *ioc)
{
	u32	pss_ctl;
	int		i;
#define PSS_LMEM_INIT_TIME  10000

	pss_ctl = readl(ioc->ioc_regs.pss_ctl_reg);
	pss_ctl &= ~__PSS_LMEM_RESET;
	pss_ctl |= __PSS_LMEM_INIT_EN;

	/*
	 * i2c workaround 12.5khz clock
	 */
	pss_ctl |= __PSS_I2C_CLK_DIV(3UL);
	writel(pss_ctl, ioc->ioc_regs.pss_ctl_reg);

	/*
	 * wait for memory initialization to be complete
	 */
	i = 0;
	do {
		pss_ctl = readl(ioc->ioc_regs.pss_ctl_reg);
		i++;
	} while (!(pss_ctl & __PSS_LMEM_INIT_DONE) && (i < PSS_LMEM_INIT_TIME));

	/*
	 * If memory initialization is not successful, IOC timeout will catch
	 * such failures.
	 */
	WARN_ON(!(pss_ctl & __PSS_LMEM_INIT_DONE));
	bfa_trc(ioc, pss_ctl);

	pss_ctl &= ~(__PSS_LMEM_INIT_DONE | __PSS_LMEM_INIT_EN);
	writel(pss_ctl, ioc->ioc_regs.pss_ctl_reg);
}

static void
bfa_ioc_lpu_start(struct bfa_ioc_s *ioc)
{
	u32	pss_ctl;

	/*
	 * Take processor out of reset.
	 */
	pss_ctl = readl(ioc->ioc_regs.pss_ctl_reg);
	pss_ctl &= ~__PSS_LPU0_RESET;

	writel(pss_ctl, ioc->ioc_regs.pss_ctl_reg);
}

static void
bfa_ioc_lpu_stop(struct bfa_ioc_s *ioc)
{
	u32	pss_ctl;

	/*
	 * Put processors in reset.
	 */
	pss_ctl = readl(ioc->ioc_regs.pss_ctl_reg);
	pss_ctl |= (__PSS_LPU0_RESET | __PSS_LPU1_RESET);

	writel(pss_ctl, ioc->ioc_regs.pss_ctl_reg);
}

/*
 * Get driver and firmware versions.
 */
void
bfa_ioc_fwver_get(struct bfa_ioc_s *ioc, struct bfi_ioc_image_hdr_s *fwhdr)
{
	u32	pgnum, pgoff;
	u32	loff = 0;
	int		i;
	u32	*fwsig = (u32 *) fwhdr;

	pgnum = PSS_SMEM_PGNUM(ioc->ioc_regs.smem_pg0, loff);
	pgoff = PSS_SMEM_PGOFF(loff);
	writel(pgnum, ioc->ioc_regs.host_page_num_fn);

	for (i = 0; i < (sizeof(struct bfi_ioc_image_hdr_s) / sizeof(u32));
	     i++) {
		fwsig[i] =
			bfa_mem_read(ioc->ioc_regs.smem_page_start, loff);
		loff += sizeof(u32);
	}
}

/*
 * Returns TRUE if same.
 */
bfa_boolean_t
bfa_ioc_fwver_cmp(struct bfa_ioc_s *ioc, struct bfi_ioc_image_hdr_s *fwhdr)
{
	struct bfi_ioc_image_hdr_s *drv_fwhdr;
	int i;

	drv_fwhdr = (struct bfi_ioc_image_hdr_s *)
		bfa_cb_image_get_chunk(bfa_ioc_asic_gen(ioc), 0);

	for (i = 0; i < BFI_IOC_MD5SUM_SZ; i++) {
		if (fwhdr->md5sum[i] != drv_fwhdr->md5sum[i]) {
			bfa_trc(ioc, i);
			bfa_trc(ioc, fwhdr->md5sum[i]);
			bfa_trc(ioc, drv_fwhdr->md5sum[i]);
			return BFA_FALSE;
		}
	}

	bfa_trc(ioc, fwhdr->md5sum[0]);
	return BFA_TRUE;
}

/*
 * Return true if current running version is valid. Firmware signature and
 * execution context (driver/bios) must match.
 */
static bfa_boolean_t
bfa_ioc_fwver_valid(struct bfa_ioc_s *ioc, u32 boot_env)
{
	struct bfi_ioc_image_hdr_s fwhdr, *drv_fwhdr;

	bfa_ioc_fwver_get(ioc, &fwhdr);
	drv_fwhdr = (struct bfi_ioc_image_hdr_s *)
		bfa_cb_image_get_chunk(bfa_ioc_asic_gen(ioc), 0);

	if (fwhdr.signature != drv_fwhdr->signature) {
		bfa_trc(ioc, fwhdr.signature);
		bfa_trc(ioc, drv_fwhdr->signature);
		return BFA_FALSE;
	}

	if (swab32(fwhdr.bootenv) != boot_env) {
		bfa_trc(ioc, fwhdr.bootenv);
		bfa_trc(ioc, boot_env);
		return BFA_FALSE;
	}

	return bfa_ioc_fwver_cmp(ioc, &fwhdr);
}

/*
 * Conditionally flush any pending message from firmware at start.
 */
static void
bfa_ioc_msgflush(struct bfa_ioc_s *ioc)
{
	u32	r32;

	r32 = readl(ioc->ioc_regs.lpu_mbox_cmd);
	if (r32)
		writel(1, ioc->ioc_regs.lpu_mbox_cmd);
}

static void
bfa_ioc_hwinit(struct bfa_ioc_s *ioc, bfa_boolean_t force)
{
	enum bfi_ioc_state ioc_fwstate;
	bfa_boolean_t fwvalid;
	u32 boot_type;
	u32 boot_env;

	ioc_fwstate = readl(ioc->ioc_regs.ioc_fwstate);

	if (force)
		ioc_fwstate = BFI_IOC_UNINIT;

	bfa_trc(ioc, ioc_fwstate);

	boot_type = BFI_FWBOOT_TYPE_NORMAL;
	boot_env = BFI_FWBOOT_ENV_OS;

	/*
	 * check if firmware is valid
	 */
	fwvalid = (ioc_fwstate == BFI_IOC_UNINIT) ?
		BFA_FALSE : bfa_ioc_fwver_valid(ioc, boot_env);

	if (!fwvalid) {
		bfa_ioc_boot(ioc, boot_type, boot_env);
		bfa_ioc_poll_fwinit(ioc);
		return;
	}

	/*
	 * If hardware initialization is in progress (initialized by other IOC),
	 * just wait for an initialization completion interrupt.
	 */
	if (ioc_fwstate == BFI_IOC_INITING) {
		bfa_ioc_poll_fwinit(ioc);
		return;
	}

	/*
	 * If IOC function is disabled and firmware version is same,
	 * just re-enable IOC.
	 *
	 * If option rom, IOC must not be in operational state. With
	 * convergence, IOC will be in operational state when 2nd driver
	 * is loaded.
	 */
	if (ioc_fwstate == BFI_IOC_DISABLED || ioc_fwstate == BFI_IOC_OP) {

		/*
		 * When using MSI-X any pending firmware ready event should
		 * be flushed. Otherwise MSI-X interrupts are not delivered.
		 */
		bfa_ioc_msgflush(ioc);
		bfa_fsm_send_event(&ioc->iocpf, IOCPF_E_FWREADY);
		return;
	}

	/*
	 * Initialize the h/w for any other states.
	 */
	bfa_ioc_boot(ioc, boot_type, boot_env);
	bfa_ioc_poll_fwinit(ioc);
}

static void
bfa_ioc_timeout(void *ioc_arg)
{
	struct bfa_ioc_s  *ioc = (struct bfa_ioc_s *) ioc_arg;

	bfa_trc(ioc, 0);
	bfa_fsm_send_event(ioc, IOC_E_TIMEOUT);
}

void
bfa_ioc_mbox_send(struct bfa_ioc_s *ioc, void *ioc_msg, int len)
{
	u32 *msgp = (u32 *) ioc_msg;
	u32 i;

	bfa_trc(ioc, msgp[0]);
	bfa_trc(ioc, len);

	WARN_ON(len > BFI_IOC_MSGLEN_MAX);

	/*
	 * first write msg to mailbox registers
	 */
	for (i = 0; i < len / sizeof(u32); i++)
		writel(cpu_to_le32(msgp[i]),
			ioc->ioc_regs.hfn_mbox + i * sizeof(u32));

	for (; i < BFI_IOC_MSGLEN_MAX / sizeof(u32); i++)
		writel(0, ioc->ioc_regs.hfn_mbox + i * sizeof(u32));

	/*
	 * write 1 to mailbox CMD to trigger LPU event
	 */
	writel(1, ioc->ioc_regs.hfn_mbox_cmd);
	(void) readl(ioc->ioc_regs.hfn_mbox_cmd);
}

static void
bfa_ioc_send_enable(struct bfa_ioc_s *ioc)
{
	struct bfi_ioc_ctrl_req_s enable_req;
	struct timeval tv;

	bfi_h2i_set(enable_req.mh, BFI_MC_IOC, BFI_IOC_H2I_ENABLE_REQ,
		    bfa_ioc_portid(ioc));
	enable_req.clscode = cpu_to_be16(ioc->clscode);
	do_gettimeofday(&tv);
	enable_req.tv_sec = be32_to_cpu(tv.tv_sec);
	bfa_ioc_mbox_send(ioc, &enable_req, sizeof(struct bfi_ioc_ctrl_req_s));
}

static void
bfa_ioc_send_disable(struct bfa_ioc_s *ioc)
{
	struct bfi_ioc_ctrl_req_s disable_req;

	bfi_h2i_set(disable_req.mh, BFI_MC_IOC, BFI_IOC_H2I_DISABLE_REQ,
		    bfa_ioc_portid(ioc));
	bfa_ioc_mbox_send(ioc, &disable_req, sizeof(struct bfi_ioc_ctrl_req_s));
}

static void
bfa_ioc_send_getattr(struct bfa_ioc_s *ioc)
{
	struct bfi_ioc_getattr_req_s	attr_req;

	bfi_h2i_set(attr_req.mh, BFI_MC_IOC, BFI_IOC_H2I_GETATTR_REQ,
		    bfa_ioc_portid(ioc));
	bfa_dma_be_addr_set(attr_req.attr_addr, ioc->attr_dma.pa);
	bfa_ioc_mbox_send(ioc, &attr_req, sizeof(attr_req));
}

static void
bfa_ioc_hb_check(void *cbarg)
{
	struct bfa_ioc_s  *ioc = cbarg;
	u32	hb_count;

	hb_count = readl(ioc->ioc_regs.heartbeat);
	if (ioc->hb_count == hb_count) {
		bfa_ioc_recover(ioc);
		return;
	} else {
		ioc->hb_count = hb_count;
	}

	bfa_ioc_mbox_poll(ioc);
	bfa_hb_timer_start(ioc);
}

static void
bfa_ioc_hb_monitor(struct bfa_ioc_s *ioc)
{
	ioc->hb_count = readl(ioc->ioc_regs.heartbeat);
	bfa_hb_timer_start(ioc);
}

/*
 *	Initiate a full firmware download.
 */
static void
bfa_ioc_download_fw(struct bfa_ioc_s *ioc, u32 boot_type,
		    u32 boot_env)
{
	u32 *fwimg;
	u32 pgnum, pgoff;
	u32 loff = 0;
	u32 chunkno = 0;
	u32 i;
	u32 asicmode;

	bfa_trc(ioc, bfa_cb_image_get_size(bfa_ioc_asic_gen(ioc)));
	fwimg = bfa_cb_image_get_chunk(bfa_ioc_asic_gen(ioc), chunkno);

	pgnum = PSS_SMEM_PGNUM(ioc->ioc_regs.smem_pg0, loff);
	pgoff = PSS_SMEM_PGOFF(loff);

	writel(pgnum, ioc->ioc_regs.host_page_num_fn);

	for (i = 0; i < bfa_cb_image_get_size(bfa_ioc_asic_gen(ioc)); i++) {

		if (BFA_IOC_FLASH_CHUNK_NO(i) != chunkno) {
			chunkno = BFA_IOC_FLASH_CHUNK_NO(i);
			fwimg = bfa_cb_image_get_chunk(bfa_ioc_asic_gen(ioc),
					BFA_IOC_FLASH_CHUNK_ADDR(chunkno));
		}

		/*
		 * write smem
		 */
		bfa_mem_write(ioc->ioc_regs.smem_page_start, loff,
			      fwimg[BFA_IOC_FLASH_OFFSET_IN_CHUNK(i)]);

		loff += sizeof(u32);

		/*
		 * handle page offset wrap around
		 */
		loff = PSS_SMEM_PGOFF(loff);
		if (loff == 0) {
			pgnum++;
			writel(pgnum, ioc->ioc_regs.host_page_num_fn);
		}
	}

	writel(PSS_SMEM_PGNUM(ioc->ioc_regs.smem_pg0, 0),
			ioc->ioc_regs.host_page_num_fn);

	/*
	 * Set boot type and device mode at the end.
	 */
	asicmode = BFI_FWBOOT_DEVMODE(ioc->asic_gen, ioc->asic_mode,
				ioc->port0_mode, ioc->port1_mode);
	bfa_mem_write(ioc->ioc_regs.smem_page_start, BFI_FWBOOT_DEVMODE_OFF,
			swab32(asicmode));
	bfa_mem_write(ioc->ioc_regs.smem_page_start, BFI_FWBOOT_TYPE_OFF,
			swab32(boot_type));
	bfa_mem_write(ioc->ioc_regs.smem_page_start, BFI_FWBOOT_ENV_OFF,
			swab32(boot_env));
}


/*
 * Update BFA configuration from firmware configuration.
 */
static void
bfa_ioc_getattr_reply(struct bfa_ioc_s *ioc)
{
	struct bfi_ioc_attr_s	*attr = ioc->attr;

	attr->adapter_prop  = be32_to_cpu(attr->adapter_prop);
	attr->card_type     = be32_to_cpu(attr->card_type);
	attr->maxfrsize	    = be16_to_cpu(attr->maxfrsize);
	ioc->fcmode	= (attr->port_mode == BFI_PORT_MODE_FC);

	bfa_fsm_send_event(ioc, IOC_E_FWRSP_GETATTR);
}

/*
 * Attach time initialization of mbox logic.
 */
static void
bfa_ioc_mbox_attach(struct bfa_ioc_s *ioc)
{
	struct bfa_ioc_mbox_mod_s	*mod = &ioc->mbox_mod;
	int	mc;

	INIT_LIST_HEAD(&mod->cmd_q);
	for (mc = 0; mc < BFI_MC_MAX; mc++) {
		mod->mbhdlr[mc].cbfn = NULL;
		mod->mbhdlr[mc].cbarg = ioc->bfa;
	}
}

/*
 * Mbox poll timer -- restarts any pending mailbox requests.
 */
static void
bfa_ioc_mbox_poll(struct bfa_ioc_s *ioc)
{
	struct bfa_ioc_mbox_mod_s	*mod = &ioc->mbox_mod;
	struct bfa_mbox_cmd_s		*cmd;
	u32			stat;

	/*
	 * If no command pending, do nothing
	 */
	if (list_empty(&mod->cmd_q))
		return;

	/*
	 * If previous command is not yet fetched by firmware, do nothing
	 */
	stat = readl(ioc->ioc_regs.hfn_mbox_cmd);
	if (stat)
		return;

	/*
	 * Enqueue command to firmware.
	 */
	bfa_q_deq(&mod->cmd_q, &cmd);
	bfa_ioc_mbox_send(ioc, cmd->msg, sizeof(cmd->msg));
}

/*
 * Cleanup any pending requests.
 */
static void
bfa_ioc_mbox_flush(struct bfa_ioc_s *ioc)
{
	struct bfa_ioc_mbox_mod_s	*mod = &ioc->mbox_mod;
	struct bfa_mbox_cmd_s		*cmd;

	while (!list_empty(&mod->cmd_q))
		bfa_q_deq(&mod->cmd_q, &cmd);
}

/*
 * Read data from SMEM to host through PCI memmap
 *
 * @param[in]	ioc	memory for IOC
 * @param[in]	tbuf	app memory to store data from smem
 * @param[in]	soff	smem offset
 * @param[in]	sz	size of smem in bytes
 */
static bfa_status_t
bfa_ioc_smem_read(struct bfa_ioc_s *ioc, void *tbuf, u32 soff, u32 sz)
{
	u32 pgnum, loff;
	__be32 r32;
	int i, len;
	u32 *buf = tbuf;

	pgnum = PSS_SMEM_PGNUM(ioc->ioc_regs.smem_pg0, soff);
	loff = PSS_SMEM_PGOFF(soff);
	bfa_trc(ioc, pgnum);
	bfa_trc(ioc, loff);
	bfa_trc(ioc, sz);

	/*
	 *  Hold semaphore to serialize pll init and fwtrc.
	 */
	if (BFA_FALSE == bfa_ioc_sem_get(ioc->ioc_regs.ioc_init_sem_reg)) {
		bfa_trc(ioc, 0);
		return BFA_STATUS_FAILED;
	}

	writel(pgnum, ioc->ioc_regs.host_page_num_fn);

	len = sz/sizeof(u32);
	bfa_trc(ioc, len);
	for (i = 0; i < len; i++) {
		r32 = bfa_mem_read(ioc->ioc_regs.smem_page_start, loff);
		buf[i] = be32_to_cpu(r32);
		loff += sizeof(u32);

		/*
		 * handle page offset wrap around
		 */
		loff = PSS_SMEM_PGOFF(loff);
		if (loff == 0) {
			pgnum++;
			writel(pgnum, ioc->ioc_regs.host_page_num_fn);
		}
	}
	writel(PSS_SMEM_PGNUM(ioc->ioc_regs.smem_pg0, 0),
			ioc->ioc_regs.host_page_num_fn);
	/*
	 *  release semaphore.
	 */
	readl(ioc->ioc_regs.ioc_init_sem_reg);
	writel(1, ioc->ioc_regs.ioc_init_sem_reg);

	bfa_trc(ioc, pgnum);
	return BFA_STATUS_OK;
}

/*
 * Clear SMEM data from host through PCI memmap
 *
 * @param[in]	ioc	memory for IOC
 * @param[in]	soff	smem offset
 * @param[in]	sz	size of smem in bytes
 */
static bfa_status_t
bfa_ioc_smem_clr(struct bfa_ioc_s *ioc, u32 soff, u32 sz)
{
	int i, len;
	u32 pgnum, loff;

	pgnum = PSS_SMEM_PGNUM(ioc->ioc_regs.smem_pg0, soff);
	loff = PSS_SMEM_PGOFF(soff);
	bfa_trc(ioc, pgnum);
	bfa_trc(ioc, loff);
	bfa_trc(ioc, sz);

	/*
	 *  Hold semaphore to serialize pll init and fwtrc.
	 */
	if (BFA_FALSE == bfa_ioc_sem_get(ioc->ioc_regs.ioc_init_sem_reg)) {
		bfa_trc(ioc, 0);
		return BFA_STATUS_FAILED;
	}

	writel(pgnum, ioc->ioc_regs.host_page_num_fn);

	len = sz/sizeof(u32); /* len in words */
	bfa_trc(ioc, len);
	for (i = 0; i < len; i++) {
		bfa_mem_write(ioc->ioc_regs.smem_page_start, loff, 0);
		loff += sizeof(u32);

		/*
		 * handle page offset wrap around
		 */
		loff = PSS_SMEM_PGOFF(loff);
		if (loff == 0) {
			pgnum++;
			writel(pgnum, ioc->ioc_regs.host_page_num_fn);
		}
	}
	writel(PSS_SMEM_PGNUM(ioc->ioc_regs.smem_pg0, 0),
			ioc->ioc_regs.host_page_num_fn);

	/*
	 *  release semaphore.
	 */
	readl(ioc->ioc_regs.ioc_init_sem_reg);
	writel(1, ioc->ioc_regs.ioc_init_sem_reg);
	bfa_trc(ioc, pgnum);
	return BFA_STATUS_OK;
}

static void
bfa_ioc_fail_notify(struct bfa_ioc_s *ioc)
{
	struct bfad_s *bfad = (struct bfad_s *)ioc->bfa->bfad;

	/*
	 * Notify driver and common modules registered for notification.
	 */
	ioc->cbfn->hbfail_cbfn(ioc->bfa);
	bfa_ioc_event_notify(ioc, BFA_IOC_E_FAILED);

	bfa_ioc_debug_save_ftrc(ioc);

	BFA_LOG(KERN_CRIT, bfad, bfa_log_level,
		"Heart Beat of IOC has failed\n");
	bfa_ioc_aen_post(ioc, BFA_IOC_AEN_HBFAIL);

}

static void
bfa_ioc_pf_fwmismatch(struct bfa_ioc_s *ioc)
{
	struct bfad_s *bfad = (struct bfad_s *)ioc->bfa->bfad;
	/*
	 * Provide enable completion callback.
	 */
	ioc->cbfn->enable_cbfn(ioc->bfa, BFA_STATUS_IOC_FAILURE);
	BFA_LOG(KERN_WARNING, bfad, bfa_log_level,
		"Running firmware version is incompatible "
		"with the driver version\n");
	bfa_ioc_aen_post(ioc, BFA_IOC_AEN_FWMISMATCH);
}

bfa_status_t
bfa_ioc_pll_init(struct bfa_ioc_s *ioc)
{

	/*
	 *  Hold semaphore so that nobody can access the chip during init.
	 */
	bfa_ioc_sem_get(ioc->ioc_regs.ioc_init_sem_reg);

	bfa_ioc_pll_init_asic(ioc);

	ioc->pllinit = BFA_TRUE;

	/*
	 * Initialize LMEM
	 */
	bfa_ioc_lmem_init(ioc);

	/*
	 *  release semaphore.
	 */
	readl(ioc->ioc_regs.ioc_init_sem_reg);
	writel(1, ioc->ioc_regs.ioc_init_sem_reg);

	return BFA_STATUS_OK;
}

/*
 * Interface used by diag module to do firmware boot with memory test
 * as the entry vector.
 */
void
bfa_ioc_boot(struct bfa_ioc_s *ioc, u32 boot_type, u32 boot_env)
{
	bfa_ioc_stats(ioc, ioc_boots);

	if (bfa_ioc_pll_init(ioc) != BFA_STATUS_OK)
		return;

	/*
	 * Initialize IOC state of all functions on a chip reset.
	 */
	if (boot_type == BFI_FWBOOT_TYPE_MEMTEST) {
		writel(BFI_IOC_MEMTEST, ioc->ioc_regs.ioc_fwstate);
		writel(BFI_IOC_MEMTEST, ioc->ioc_regs.alt_ioc_fwstate);
	} else {
		writel(BFI_IOC_INITING, ioc->ioc_regs.ioc_fwstate);
		writel(BFI_IOC_INITING, ioc->ioc_regs.alt_ioc_fwstate);
	}

	bfa_ioc_msgflush(ioc);
	bfa_ioc_download_fw(ioc, boot_type, boot_env);
	bfa_ioc_lpu_start(ioc);
}

/*
 * Enable/disable IOC failure auto recovery.
 */
void
bfa_ioc_auto_recover(bfa_boolean_t auto_recover)
{
	bfa_auto_recover = auto_recover;
}



bfa_boolean_t
bfa_ioc_is_operational(struct bfa_ioc_s *ioc)
{
	return bfa_fsm_cmp_state(ioc, bfa_ioc_sm_op);
}

bfa_boolean_t
bfa_ioc_is_initialized(struct bfa_ioc_s *ioc)
{
	u32 r32 = readl(ioc->ioc_regs.ioc_fwstate);

	return ((r32 != BFI_IOC_UNINIT) &&
		(r32 != BFI_IOC_INITING) &&
		(r32 != BFI_IOC_MEMTEST));
}

bfa_boolean_t
bfa_ioc_msgget(struct bfa_ioc_s *ioc, void *mbmsg)
{
	__be32	*msgp = mbmsg;
	u32	r32;
	int		i;

	r32 = readl(ioc->ioc_regs.lpu_mbox_cmd);
	if ((r32 & 1) == 0)
		return BFA_FALSE;

	/*
	 * read the MBOX msg
	 */
	for (i = 0; i < (sizeof(union bfi_ioc_i2h_msg_u) / sizeof(u32));
	     i++) {
		r32 = readl(ioc->ioc_regs.lpu_mbox +
				   i * sizeof(u32));
		msgp[i] = cpu_to_be32(r32);
	}

	/*
	 * turn off mailbox interrupt by clearing mailbox status
	 */
	writel(1, ioc->ioc_regs.lpu_mbox_cmd);
	readl(ioc->ioc_regs.lpu_mbox_cmd);

	return BFA_TRUE;
}

void
bfa_ioc_isr(struct bfa_ioc_s *ioc, struct bfi_mbmsg_s *m)
{
	union bfi_ioc_i2h_msg_u	*msg;
	struct bfa_iocpf_s *iocpf = &ioc->iocpf;

	msg = (union bfi_ioc_i2h_msg_u *) m;

	bfa_ioc_stats(ioc, ioc_isrs);

	switch (msg->mh.msg_id) {
	case BFI_IOC_I2H_HBEAT:
		break;

	case BFI_IOC_I2H_ENABLE_REPLY:
		ioc->port_mode = ioc->port_mode_cfg =
				(enum bfa_mode_s)msg->fw_event.port_mode;
		ioc->ad_cap_bm = msg->fw_event.cap_bm;
		bfa_fsm_send_event(iocpf, IOCPF_E_FWRSP_ENABLE);
		break;

	case BFI_IOC_I2H_DISABLE_REPLY:
		bfa_fsm_send_event(iocpf, IOCPF_E_FWRSP_DISABLE);
		break;

	case BFI_IOC_I2H_GETATTR_REPLY:
		bfa_ioc_getattr_reply(ioc);
		break;

	default:
		bfa_trc(ioc, msg->mh.msg_id);
		WARN_ON(1);
	}
}

/*
 * IOC attach time initialization and setup.
 *
 * @param[in]	ioc	memory for IOC
 * @param[in]	bfa	driver instance structure
 */
void
bfa_ioc_attach(struct bfa_ioc_s *ioc, void *bfa, struct bfa_ioc_cbfn_s *cbfn,
	       struct bfa_timer_mod_s *timer_mod)
{
	ioc->bfa	= bfa;
	ioc->cbfn	= cbfn;
	ioc->timer_mod	= timer_mod;
	ioc->fcmode	= BFA_FALSE;
	ioc->pllinit	= BFA_FALSE;
	ioc->dbg_fwsave_once = BFA_TRUE;
	ioc->iocpf.ioc	= ioc;

	bfa_ioc_mbox_attach(ioc);
	INIT_LIST_HEAD(&ioc->notify_q);

	bfa_fsm_set_state(ioc, bfa_ioc_sm_uninit);
	bfa_fsm_send_event(ioc, IOC_E_RESET);
}

/*
 * Driver detach time IOC cleanup.
 */
void
bfa_ioc_detach(struct bfa_ioc_s *ioc)
{
	bfa_fsm_send_event(ioc, IOC_E_DETACH);
	INIT_LIST_HEAD(&ioc->notify_q);
}

/*
 * Setup IOC PCI properties.
 *
 * @param[in]	pcidev	PCI device information for this IOC
 */
void
bfa_ioc_pci_init(struct bfa_ioc_s *ioc, struct bfa_pcidev_s *pcidev,
		enum bfi_pcifn_class clscode)
{
	ioc->clscode	= clscode;
	ioc->pcidev	= *pcidev;

	/*
	 * Initialize IOC and device personality
	 */
	ioc->port0_mode = ioc->port1_mode = BFI_PORT_MODE_FC;
	ioc->asic_mode  = BFI_ASIC_MODE_FC;

	switch (pcidev->device_id) {
	case BFA_PCI_DEVICE_ID_FC_8G1P:
	case BFA_PCI_DEVICE_ID_FC_8G2P:
		ioc->asic_gen = BFI_ASIC_GEN_CB;
		ioc->fcmode = BFA_TRUE;
		ioc->port_mode = ioc->port_mode_cfg = BFA_MODE_HBA;
		ioc->ad_cap_bm = BFA_CM_HBA;
		break;

	case BFA_PCI_DEVICE_ID_CT:
		ioc->asic_gen = BFI_ASIC_GEN_CT;
		ioc->port0_mode = ioc->port1_mode = BFI_PORT_MODE_ETH;
		ioc->asic_mode  = BFI_ASIC_MODE_ETH;
		ioc->port_mode = ioc->port_mode_cfg = BFA_MODE_CNA;
		ioc->ad_cap_bm = BFA_CM_CNA;
		break;

	case BFA_PCI_DEVICE_ID_CT_FC:
		ioc->asic_gen = BFI_ASIC_GEN_CT;
		ioc->fcmode = BFA_TRUE;
		ioc->port_mode = ioc->port_mode_cfg = BFA_MODE_HBA;
		ioc->ad_cap_bm = BFA_CM_HBA;
		break;

	case BFA_PCI_DEVICE_ID_CT2:
		ioc->asic_gen = BFI_ASIC_GEN_CT2;
		if (clscode == BFI_PCIFN_CLASS_FC &&
		    pcidev->ssid == BFA_PCI_CT2_SSID_FC) {
			ioc->asic_mode  = BFI_ASIC_MODE_FC16;
			ioc->fcmode = BFA_TRUE;
			ioc->port_mode = ioc->port_mode_cfg = BFA_MODE_HBA;
			ioc->ad_cap_bm = BFA_CM_HBA;
		} else {
			ioc->port0_mode = ioc->port1_mode = BFI_PORT_MODE_ETH;
			ioc->asic_mode  = BFI_ASIC_MODE_ETH;
			if (pcidev->ssid == BFA_PCI_CT2_SSID_FCoE) {
				ioc->port_mode =
				ioc->port_mode_cfg = BFA_MODE_CNA;
				ioc->ad_cap_bm = BFA_CM_CNA;
			} else {
				ioc->port_mode =
				ioc->port_mode_cfg = BFA_MODE_NIC;
				ioc->ad_cap_bm = BFA_CM_NIC;
			}
		}
		break;

	default:
		WARN_ON(1);
	}

	/*
	 * Set asic specific interfaces. See bfa_ioc_cb.c and bfa_ioc_ct.c
	 */
	if (ioc->asic_gen == BFI_ASIC_GEN_CB)
		bfa_ioc_set_cb_hwif(ioc);
	else if (ioc->asic_gen == BFI_ASIC_GEN_CT)
		bfa_ioc_set_ct_hwif(ioc);
	else {
		WARN_ON(ioc->asic_gen != BFI_ASIC_GEN_CT2);
		bfa_ioc_set_ct2_hwif(ioc);
		bfa_ioc_ct2_poweron(ioc);
	}

	bfa_ioc_map_port(ioc);
	bfa_ioc_reg_init(ioc);
}

/*
 * Initialize IOC dma memory
 *
 * @param[in]	dm_kva	kernel virtual address of IOC dma memory
 * @param[in]	dm_pa	physical address of IOC dma memory
 */
void
bfa_ioc_mem_claim(struct bfa_ioc_s *ioc,  u8 *dm_kva, u64 dm_pa)
{
	/*
	 * dma memory for firmware attribute
	 */
	ioc->attr_dma.kva = dm_kva;
	ioc->attr_dma.pa = dm_pa;
	ioc->attr = (struct bfi_ioc_attr_s *) dm_kva;
}

void
bfa_ioc_enable(struct bfa_ioc_s *ioc)
{
	bfa_ioc_stats(ioc, ioc_enables);
	ioc->dbg_fwsave_once = BFA_TRUE;

	bfa_fsm_send_event(ioc, IOC_E_ENABLE);
}

void
bfa_ioc_disable(struct bfa_ioc_s *ioc)
{
	bfa_ioc_stats(ioc, ioc_disables);
	bfa_fsm_send_event(ioc, IOC_E_DISABLE);
}


/*
 * Initialize memory for saving firmware trace. Driver must initialize
 * trace memory before call bfa_ioc_enable().
 */
void
bfa_ioc_debug_memclaim(struct bfa_ioc_s *ioc, void *dbg_fwsave)
{
	ioc->dbg_fwsave	    = dbg_fwsave;
	ioc->dbg_fwsave_len = (ioc->iocpf.auto_recover) ? BFA_DBG_FWTRC_LEN : 0;
}

/*
 * Register mailbox message handler functions
 *
 * @param[in]	ioc		IOC instance
 * @param[in]	mcfuncs		message class handler functions
 */
void
bfa_ioc_mbox_register(struct bfa_ioc_s *ioc, bfa_ioc_mbox_mcfunc_t *mcfuncs)
{
	struct bfa_ioc_mbox_mod_s	*mod = &ioc->mbox_mod;
	int				mc;

	for (mc = 0; mc < BFI_MC_MAX; mc++)
		mod->mbhdlr[mc].cbfn = mcfuncs[mc];
}

/*
 * Register mailbox message handler function, to be called by common modules
 */
void
bfa_ioc_mbox_regisr(struct bfa_ioc_s *ioc, enum bfi_mclass mc,
		    bfa_ioc_mbox_mcfunc_t cbfn, void *cbarg)
{
	struct bfa_ioc_mbox_mod_s	*mod = &ioc->mbox_mod;

	mod->mbhdlr[mc].cbfn	= cbfn;
	mod->mbhdlr[mc].cbarg	= cbarg;
}

/*
 * Queue a mailbox command request to firmware. Waits if mailbox is busy.
 * Responsibility of caller to serialize
 *
 * @param[in]	ioc	IOC instance
 * @param[i]	cmd	Mailbox command
 */
void
bfa_ioc_mbox_queue(struct bfa_ioc_s *ioc, struct bfa_mbox_cmd_s *cmd)
{
	struct bfa_ioc_mbox_mod_s	*mod = &ioc->mbox_mod;
	u32			stat;

	/*
	 * If a previous command is pending, queue new command
	 */
	if (!list_empty(&mod->cmd_q)) {
		list_add_tail(&cmd->qe, &mod->cmd_q);
		return;
	}

	/*
	 * If mailbox is busy, queue command for poll timer
	 */
	stat = readl(ioc->ioc_regs.hfn_mbox_cmd);
	if (stat) {
		list_add_tail(&cmd->qe, &mod->cmd_q);
		return;
	}

	/*
	 * mailbox is free -- queue command to firmware
	 */
	bfa_ioc_mbox_send(ioc, cmd->msg, sizeof(cmd->msg));
}

/*
 * Handle mailbox interrupts
 */
void
bfa_ioc_mbox_isr(struct bfa_ioc_s *ioc)
{
	struct bfa_ioc_mbox_mod_s	*mod = &ioc->mbox_mod;
	struct bfi_mbmsg_s		m;
	int				mc;

	if (bfa_ioc_msgget(ioc, &m)) {
		/*
		 * Treat IOC message class as special.
		 */
		mc = m.mh.msg_class;
		if (mc == BFI_MC_IOC) {
			bfa_ioc_isr(ioc, &m);
			return;
		}

		if ((mc > BFI_MC_MAX) || (mod->mbhdlr[mc].cbfn == NULL))
			return;

		mod->mbhdlr[mc].cbfn(mod->mbhdlr[mc].cbarg, &m);
	}

	bfa_ioc_lpu_read_stat(ioc);

	/*
	 * Try to send pending mailbox commands
	 */
	bfa_ioc_mbox_poll(ioc);
}

void
bfa_ioc_error_isr(struct bfa_ioc_s *ioc)
{
	bfa_ioc_stats(ioc, ioc_hbfails);
	ioc->stats.hb_count = ioc->hb_count;
	bfa_fsm_send_event(ioc, IOC_E_HWERROR);
}

/*
 * return true if IOC is disabled
 */
bfa_boolean_t
bfa_ioc_is_disabled(struct bfa_ioc_s *ioc)
{
	return bfa_fsm_cmp_state(ioc, bfa_ioc_sm_disabling) ||
		bfa_fsm_cmp_state(ioc, bfa_ioc_sm_disabled);
}

/*
 * return true if IOC firmware is different.
 */
bfa_boolean_t
bfa_ioc_fw_mismatch(struct bfa_ioc_s *ioc)
{
	return bfa_fsm_cmp_state(ioc, bfa_ioc_sm_reset) ||
		bfa_fsm_cmp_state(&ioc->iocpf, bfa_iocpf_sm_fwcheck) ||
		bfa_fsm_cmp_state(&ioc->iocpf, bfa_iocpf_sm_mismatch);
}

#define bfa_ioc_state_disabled(__sm)		\
	(((__sm) == BFI_IOC_UNINIT) ||		\
	 ((__sm) == BFI_IOC_INITING) ||		\
	 ((__sm) == BFI_IOC_HWINIT) ||		\
	 ((__sm) == BFI_IOC_DISABLED) ||	\
	 ((__sm) == BFI_IOC_FAIL) ||		\
	 ((__sm) == BFI_IOC_CFG_DISABLED))

/*
 * Check if adapter is disabled -- both IOCs should be in a disabled
 * state.
 */
bfa_boolean_t
bfa_ioc_adapter_is_disabled(struct bfa_ioc_s *ioc)
{
	u32	ioc_state;

	if (!bfa_fsm_cmp_state(ioc, bfa_ioc_sm_disabled))
		return BFA_FALSE;

	ioc_state = readl(ioc->ioc_regs.ioc_fwstate);
	if (!bfa_ioc_state_disabled(ioc_state))
		return BFA_FALSE;

	if (ioc->pcidev.device_id != BFA_PCI_DEVICE_ID_FC_8G1P) {
		ioc_state = readl(ioc->ioc_regs.alt_ioc_fwstate);
		if (!bfa_ioc_state_disabled(ioc_state))
			return BFA_FALSE;
	}

	return BFA_TRUE;
}

/*
 * Reset IOC fwstate registers.
 */
void
bfa_ioc_reset_fwstate(struct bfa_ioc_s *ioc)
{
	writel(BFI_IOC_UNINIT, ioc->ioc_regs.ioc_fwstate);
	writel(BFI_IOC_UNINIT, ioc->ioc_regs.alt_ioc_fwstate);
}

#define BFA_MFG_NAME "Brocade"
void
bfa_ioc_get_adapter_attr(struct bfa_ioc_s *ioc,
			 struct bfa_adapter_attr_s *ad_attr)
{
	struct bfi_ioc_attr_s	*ioc_attr;

	ioc_attr = ioc->attr;

	bfa_ioc_get_adapter_serial_num(ioc, ad_attr->serial_num);
	bfa_ioc_get_adapter_fw_ver(ioc, ad_attr->fw_ver);
	bfa_ioc_get_adapter_optrom_ver(ioc, ad_attr->optrom_ver);
	bfa_ioc_get_adapter_manufacturer(ioc, ad_attr->manufacturer);
	memcpy(&ad_attr->vpd, &ioc_attr->vpd,
		      sizeof(struct bfa_mfg_vpd_s));

	ad_attr->nports = bfa_ioc_get_nports(ioc);
	ad_attr->max_speed = bfa_ioc_speed_sup(ioc);

	bfa_ioc_get_adapter_model(ioc, ad_attr->model);
	/* For now, model descr uses same model string */
	bfa_ioc_get_adapter_model(ioc, ad_attr->model_descr);

	ad_attr->card_type = ioc_attr->card_type;
	ad_attr->is_mezz = bfa_mfg_is_mezz(ioc_attr->card_type);

	if (BFI_ADAPTER_IS_SPECIAL(ioc_attr->adapter_prop))
		ad_attr->prototype = 1;
	else
		ad_attr->prototype = 0;

	ad_attr->pwwn = ioc->attr->pwwn;
	ad_attr->mac  = bfa_ioc_get_mac(ioc);

	ad_attr->pcie_gen = ioc_attr->pcie_gen;
	ad_attr->pcie_lanes = ioc_attr->pcie_lanes;
	ad_attr->pcie_lanes_orig = ioc_attr->pcie_lanes_orig;
	ad_attr->asic_rev = ioc_attr->asic_rev;

	bfa_ioc_get_pci_chip_rev(ioc, ad_attr->hw_ver);

	ad_attr->cna_capable = bfa_ioc_is_cna(ioc);
	ad_attr->trunk_capable = (ad_attr->nports > 1) &&
				  !bfa_ioc_is_cna(ioc) && !ad_attr->is_mezz;
}

enum bfa_ioc_type_e
bfa_ioc_get_type(struct bfa_ioc_s *ioc)
{
	if (ioc->clscode == BFI_PCIFN_CLASS_ETH)
		return BFA_IOC_TYPE_LL;

	WARN_ON(ioc->clscode != BFI_PCIFN_CLASS_FC);

	return (ioc->attr->port_mode == BFI_PORT_MODE_FC)
		? BFA_IOC_TYPE_FC : BFA_IOC_TYPE_FCoE;
}

void
bfa_ioc_get_adapter_serial_num(struct bfa_ioc_s *ioc, char *serial_num)
{
	memset((void *)serial_num, 0, BFA_ADAPTER_SERIAL_NUM_LEN);
	memcpy((void *)serial_num,
			(void *)ioc->attr->brcd_serialnum,
			BFA_ADAPTER_SERIAL_NUM_LEN);
}

void
bfa_ioc_get_adapter_fw_ver(struct bfa_ioc_s *ioc, char *fw_ver)
{
	memset((void *)fw_ver, 0, BFA_VERSION_LEN);
	memcpy(fw_ver, ioc->attr->fw_version, BFA_VERSION_LEN);
}

void
bfa_ioc_get_pci_chip_rev(struct bfa_ioc_s *ioc, char *chip_rev)
{
	WARN_ON(!chip_rev);

	memset((void *)chip_rev, 0, BFA_IOC_CHIP_REV_LEN);

	chip_rev[0] = 'R';
	chip_rev[1] = 'e';
	chip_rev[2] = 'v';
	chip_rev[3] = '-';
	chip_rev[4] = ioc->attr->asic_rev;
	chip_rev[5] = '\0';
}

void
bfa_ioc_get_adapter_optrom_ver(struct bfa_ioc_s *ioc, char *optrom_ver)
{
	memset((void *)optrom_ver, 0, BFA_VERSION_LEN);
	memcpy(optrom_ver, ioc->attr->optrom_version,
		      BFA_VERSION_LEN);
}

void
bfa_ioc_get_adapter_manufacturer(struct bfa_ioc_s *ioc, char *manufacturer)
{
	memset((void *)manufacturer, 0, BFA_ADAPTER_MFG_NAME_LEN);
	memcpy(manufacturer, BFA_MFG_NAME, BFA_ADAPTER_MFG_NAME_LEN);
}

void
bfa_ioc_get_adapter_model(struct bfa_ioc_s *ioc, char *model)
{
	struct bfi_ioc_attr_s	*ioc_attr;

	WARN_ON(!model);
	memset((void *)model, 0, BFA_ADAPTER_MODEL_NAME_LEN);

	ioc_attr = ioc->attr;

	snprintf(model, BFA_ADAPTER_MODEL_NAME_LEN, "%s-%u",
			BFA_MFG_NAME, ioc_attr->card_type);
}

enum bfa_ioc_state
bfa_ioc_get_state(struct bfa_ioc_s *ioc)
{
	enum bfa_iocpf_state iocpf_st;
	enum bfa_ioc_state ioc_st = bfa_sm_to_state(ioc_sm_table, ioc->fsm);

	if (ioc_st == BFA_IOC_ENABLING ||
		ioc_st == BFA_IOC_FAIL || ioc_st == BFA_IOC_INITFAIL) {

		iocpf_st = bfa_sm_to_state(iocpf_sm_table, ioc->iocpf.fsm);

		switch (iocpf_st) {
		case BFA_IOCPF_SEMWAIT:
			ioc_st = BFA_IOC_SEMWAIT;
			break;

		case BFA_IOCPF_HWINIT:
			ioc_st = BFA_IOC_HWINIT;
			break;

		case BFA_IOCPF_FWMISMATCH:
			ioc_st = BFA_IOC_FWMISMATCH;
			break;

		case BFA_IOCPF_FAIL:
			ioc_st = BFA_IOC_FAIL;
			break;

		case BFA_IOCPF_INITFAIL:
			ioc_st = BFA_IOC_INITFAIL;
			break;

		default:
			break;
		}
	}

	return ioc_st;
}

void
bfa_ioc_get_attr(struct bfa_ioc_s *ioc, struct bfa_ioc_attr_s *ioc_attr)
{
	memset((void *)ioc_attr, 0, sizeof(struct bfa_ioc_attr_s));

	ioc_attr->state = bfa_ioc_get_state(ioc);
	ioc_attr->port_id = ioc->port_id;
	ioc_attr->port_mode = ioc->port_mode;
	ioc_attr->port_mode_cfg = ioc->port_mode_cfg;
	ioc_attr->cap_bm = ioc->ad_cap_bm;

	ioc_attr->ioc_type = bfa_ioc_get_type(ioc);

	bfa_ioc_get_adapter_attr(ioc, &ioc_attr->adapter_attr);

	ioc_attr->pci_attr.device_id = ioc->pcidev.device_id;
	ioc_attr->pci_attr.pcifn = ioc->pcidev.pci_func;
	bfa_ioc_get_pci_chip_rev(ioc, ioc_attr->pci_attr.chip_rev);
}

mac_t
bfa_ioc_get_mac(struct bfa_ioc_s *ioc)
{
	/*
	 * Check the IOC type and return the appropriate MAC
	 */
	if (bfa_ioc_get_type(ioc) == BFA_IOC_TYPE_FCoE)
		return ioc->attr->fcoe_mac;
	else
		return ioc->attr->mac;
}

mac_t
bfa_ioc_get_mfg_mac(struct bfa_ioc_s *ioc)
{
	mac_t	m;

	m = ioc->attr->mfg_mac;
	if (bfa_mfg_is_old_wwn_mac_model(ioc->attr->card_type))
		m.mac[MAC_ADDRLEN - 1] += bfa_ioc_pcifn(ioc);
	else
		bfa_mfg_increment_wwn_mac(&(m.mac[MAC_ADDRLEN-3]),
			bfa_ioc_pcifn(ioc));

	return m;
}

/*
 * Send AEN notification
 */
void
bfa_ioc_aen_post(struct bfa_ioc_s *ioc, enum bfa_ioc_aen_event event)
{
	struct bfad_s *bfad = (struct bfad_s *)ioc->bfa->bfad;
	struct bfa_aen_entry_s	*aen_entry;
	enum bfa_ioc_type_e ioc_type;

	bfad_get_aen_entry(bfad, aen_entry);
	if (!aen_entry)
		return;

	ioc_type = bfa_ioc_get_type(ioc);
	switch (ioc_type) {
	case BFA_IOC_TYPE_FC:
		aen_entry->aen_data.ioc.pwwn = ioc->attr->pwwn;
		break;
	case BFA_IOC_TYPE_FCoE:
		aen_entry->aen_data.ioc.pwwn = ioc->attr->pwwn;
		aen_entry->aen_data.ioc.mac = bfa_ioc_get_mac(ioc);
		break;
	case BFA_IOC_TYPE_LL:
		aen_entry->aen_data.ioc.mac = bfa_ioc_get_mac(ioc);
		break;
	default:
		WARN_ON(ioc_type != BFA_IOC_TYPE_FC);
		break;
	}

	/* Send the AEN notification */
	aen_entry->aen_data.ioc.ioc_type = ioc_type;
	bfad_im_post_vendor_event(aen_entry, bfad, ++ioc->ioc_aen_seq,
				  BFA_AEN_CAT_IOC, event);
}

/*
 * Retrieve saved firmware trace from a prior IOC failure.
 */
bfa_status_t
bfa_ioc_debug_fwsave(struct bfa_ioc_s *ioc, void *trcdata, int *trclen)
{
	int	tlen;

	if (ioc->dbg_fwsave_len == 0)
		return BFA_STATUS_ENOFSAVE;

	tlen = *trclen;
	if (tlen > ioc->dbg_fwsave_len)
		tlen = ioc->dbg_fwsave_len;

	memcpy(trcdata, ioc->dbg_fwsave, tlen);
	*trclen = tlen;
	return BFA_STATUS_OK;
}


/*
 * Retrieve saved firmware trace from a prior IOC failure.
 */
bfa_status_t
bfa_ioc_debug_fwtrc(struct bfa_ioc_s *ioc, void *trcdata, int *trclen)
{
	u32 loff = BFA_DBG_FWTRC_OFF(bfa_ioc_portid(ioc));
	int tlen;
	bfa_status_t status;

	bfa_trc(ioc, *trclen);

	tlen = *trclen;
	if (tlen > BFA_DBG_FWTRC_LEN)
		tlen = BFA_DBG_FWTRC_LEN;

	status = bfa_ioc_smem_read(ioc, trcdata, loff, tlen);
	*trclen = tlen;
	return status;
}

static void
bfa_ioc_send_fwsync(struct bfa_ioc_s *ioc)
{
	struct bfa_mbox_cmd_s cmd;
	struct bfi_ioc_ctrl_req_s *req = (struct bfi_ioc_ctrl_req_s *) cmd.msg;

	bfi_h2i_set(req->mh, BFI_MC_IOC, BFI_IOC_H2I_DBG_SYNC,
		    bfa_ioc_portid(ioc));
	req->clscode = cpu_to_be16(ioc->clscode);
	bfa_ioc_mbox_queue(ioc, &cmd);
}

static void
bfa_ioc_fwsync(struct bfa_ioc_s *ioc)
{
	u32 fwsync_iter = 1000;

	bfa_ioc_send_fwsync(ioc);

	/*
	 * After sending a fw sync mbox command wait for it to
	 * take effect.  We will not wait for a response because
	 *    1. fw_sync mbox cmd doesn't have a response.
	 *    2. Even if we implement that,  interrupts might not
	 *	 be enabled when we call this function.
	 * So, just keep checking if any mbox cmd is pending, and
	 * after waiting for a reasonable amount of time, go ahead.
	 * It is possible that fw has crashed and the mbox command
	 * is never acknowledged.
	 */
	while (bfa_ioc_mbox_cmd_pending(ioc) && fwsync_iter > 0)
		fwsync_iter--;
}

/*
 * Dump firmware smem
 */
bfa_status_t
bfa_ioc_debug_fwcore(struct bfa_ioc_s *ioc, void *buf,
				u32 *offset, int *buflen)
{
	u32 loff;
	int dlen;
	bfa_status_t status;
	u32 smem_len = BFA_IOC_FW_SMEM_SIZE(ioc);

	if (*offset >= smem_len) {
		*offset = *buflen = 0;
		return BFA_STATUS_EINVAL;
	}

	loff = *offset;
	dlen = *buflen;

	/*
	 * First smem read, sync smem before proceeding
	 * No need to sync before reading every chunk.
	 */
	if (loff == 0)
		bfa_ioc_fwsync(ioc);

	if ((loff + dlen) >= smem_len)
		dlen = smem_len - loff;

	status = bfa_ioc_smem_read(ioc, buf, loff, dlen);

	if (status != BFA_STATUS_OK) {
		*offset = *buflen = 0;
		return status;
	}

	*offset += dlen;

	if (*offset >= smem_len)
		*offset = 0;

	*buflen = dlen;

	return status;
}

/*
 * Firmware statistics
 */
bfa_status_t
bfa_ioc_fw_stats_get(struct bfa_ioc_s *ioc, void *stats)
{
	u32 loff = BFI_IOC_FWSTATS_OFF + \
		BFI_IOC_FWSTATS_SZ * (bfa_ioc_portid(ioc));
	int tlen;
	bfa_status_t status;

	if (ioc->stats_busy) {
		bfa_trc(ioc, ioc->stats_busy);
		return BFA_STATUS_DEVBUSY;
	}
	ioc->stats_busy = BFA_TRUE;

	tlen = sizeof(struct bfa_fw_stats_s);
	status = bfa_ioc_smem_read(ioc, stats, loff, tlen);

	ioc->stats_busy = BFA_FALSE;
	return status;
}

bfa_status_t
bfa_ioc_fw_stats_clear(struct bfa_ioc_s *ioc)
{
	u32 loff = BFI_IOC_FWSTATS_OFF + \
		BFI_IOC_FWSTATS_SZ * (bfa_ioc_portid(ioc));
	int tlen;
	bfa_status_t status;

	if (ioc->stats_busy) {
		bfa_trc(ioc, ioc->stats_busy);
		return BFA_STATUS_DEVBUSY;
	}
	ioc->stats_busy = BFA_TRUE;

	tlen = sizeof(struct bfa_fw_stats_s);
	status = bfa_ioc_smem_clr(ioc, loff, tlen);

	ioc->stats_busy = BFA_FALSE;
	return status;
}

/*
 * Save firmware trace if configured.
 */
static void
bfa_ioc_debug_save_ftrc(struct bfa_ioc_s *ioc)
{
	int		tlen;

	if (ioc->dbg_fwsave_once) {
		ioc->dbg_fwsave_once = BFA_FALSE;
		if (ioc->dbg_fwsave_len) {
			tlen = ioc->dbg_fwsave_len;
			bfa_ioc_debug_fwtrc(ioc, ioc->dbg_fwsave, &tlen);
		}
	}
}

/*
 * Firmware failure detected. Start recovery actions.
 */
static void
bfa_ioc_recover(struct bfa_ioc_s *ioc)
{
	bfa_ioc_stats(ioc, ioc_hbfails);
	ioc->stats.hb_count = ioc->hb_count;
	bfa_fsm_send_event(ioc, IOC_E_HBFAIL);
}

/*
 *  BFA IOC PF private functions
 */
static void
bfa_iocpf_timeout(void *ioc_arg)
{
	struct bfa_ioc_s  *ioc = (struct bfa_ioc_s *) ioc_arg;

	bfa_trc(ioc, 0);
	bfa_fsm_send_event(&ioc->iocpf, IOCPF_E_TIMEOUT);
}

static void
bfa_iocpf_sem_timeout(void *ioc_arg)
{
	struct bfa_ioc_s  *ioc = (struct bfa_ioc_s *) ioc_arg;

	bfa_ioc_hw_sem_get(ioc);
}

static void
bfa_ioc_poll_fwinit(struct bfa_ioc_s *ioc)
{
	u32 fwstate = readl(ioc->ioc_regs.ioc_fwstate);

	bfa_trc(ioc, fwstate);

	if (fwstate == BFI_IOC_DISABLED) {
		bfa_fsm_send_event(&ioc->iocpf, IOCPF_E_FWREADY);
		return;
	}

	if (ioc->iocpf.poll_time >= BFA_IOC_TOV)
		bfa_iocpf_timeout(ioc);
	else {
		ioc->iocpf.poll_time += BFA_IOC_POLL_TOV;
		bfa_iocpf_poll_timer_start(ioc);
	}
}

static void
bfa_iocpf_poll_timeout(void *ioc_arg)
{
	struct bfa_ioc_s *ioc = (struct bfa_ioc_s *) ioc_arg;

	bfa_ioc_poll_fwinit(ioc);
}

/*
 *  bfa timer function
 */
void
bfa_timer_beat(struct bfa_timer_mod_s *mod)
{
	struct list_head *qh = &mod->timer_q;
	struct list_head *qe, *qe_next;
	struct bfa_timer_s *elem;
	struct list_head timedout_q;

	INIT_LIST_HEAD(&timedout_q);

	qe = bfa_q_next(qh);

	while (qe != qh) {
		qe_next = bfa_q_next(qe);

		elem = (struct bfa_timer_s *) qe;
		if (elem->timeout <= BFA_TIMER_FREQ) {
			elem->timeout = 0;
			list_del(&elem->qe);
			list_add_tail(&elem->qe, &timedout_q);
		} else {
			elem->timeout -= BFA_TIMER_FREQ;
		}

		qe = qe_next;	/* go to next elem */
	}

	/*
	 * Pop all the timeout entries
	 */
	while (!list_empty(&timedout_q)) {
		bfa_q_deq(&timedout_q, &elem);
		elem->timercb(elem->arg);
	}
}

/*
 * Should be called with lock protection
 */
void
bfa_timer_begin(struct bfa_timer_mod_s *mod, struct bfa_timer_s *timer,
		    void (*timercb) (void *), void *arg, unsigned int timeout)
{

	WARN_ON(timercb == NULL);
	WARN_ON(bfa_q_is_on_q(&mod->timer_q, timer));

	timer->timeout = timeout;
	timer->timercb = timercb;
	timer->arg = arg;

	list_add_tail(&timer->qe, &mod->timer_q);
}

/*
 * Should be called with lock protection
 */
void
bfa_timer_stop(struct bfa_timer_s *timer)
{
	WARN_ON(list_empty(&timer->qe));

	list_del(&timer->qe);
}

/*
 *	ASIC block related
 */
static void
bfa_ablk_config_swap(struct bfa_ablk_cfg_s *cfg)
{
	struct bfa_ablk_cfg_inst_s *cfg_inst;
	int i, j;
	u16	be16;
	u32	be32;

	for (i = 0; i < BFA_ABLK_MAX; i++) {
		cfg_inst = &cfg->inst[i];
		for (j = 0; j < BFA_ABLK_MAX_PFS; j++) {
			be16 = cfg_inst->pf_cfg[j].pers;
			cfg_inst->pf_cfg[j].pers = be16_to_cpu(be16);
			be16 = cfg_inst->pf_cfg[j].num_qpairs;
			cfg_inst->pf_cfg[j].num_qpairs = be16_to_cpu(be16);
			be16 = cfg_inst->pf_cfg[j].num_vectors;
			cfg_inst->pf_cfg[j].num_vectors = be16_to_cpu(be16);
			be32 = cfg_inst->pf_cfg[j].bw;
			cfg_inst->pf_cfg[j].bw = be16_to_cpu(be32);
		}
	}
}

static void
bfa_ablk_isr(void *cbarg, struct bfi_mbmsg_s *msg)
{
	struct bfa_ablk_s *ablk = (struct bfa_ablk_s *)cbarg;
	struct bfi_ablk_i2h_rsp_s *rsp = (struct bfi_ablk_i2h_rsp_s *)msg;
	bfa_ablk_cbfn_t cbfn;

	WARN_ON(msg->mh.msg_class != BFI_MC_ABLK);
	bfa_trc(ablk->ioc, msg->mh.msg_id);

	switch (msg->mh.msg_id) {
	case BFI_ABLK_I2H_QUERY:
		if (rsp->status == BFA_STATUS_OK) {
			memcpy(ablk->cfg, ablk->dma_addr.kva,
				sizeof(struct bfa_ablk_cfg_s));
			bfa_ablk_config_swap(ablk->cfg);
			ablk->cfg = NULL;
		}
		break;

	case BFI_ABLK_I2H_ADPT_CONFIG:
	case BFI_ABLK_I2H_PORT_CONFIG:
		/* update config port mode */
		ablk->ioc->port_mode_cfg = rsp->port_mode;

	case BFI_ABLK_I2H_PF_DELETE:
	case BFI_ABLK_I2H_PF_UPDATE:
	case BFI_ABLK_I2H_OPTROM_ENABLE:
	case BFI_ABLK_I2H_OPTROM_DISABLE:
		/* No-op */
		break;

	case BFI_ABLK_I2H_PF_CREATE:
		*(ablk->pcifn) = rsp->pcifn;
		ablk->pcifn = NULL;
		break;

	default:
		WARN_ON(1);
	}

	ablk->busy = BFA_FALSE;
	if (ablk->cbfn) {
		cbfn = ablk->cbfn;
		ablk->cbfn = NULL;
		cbfn(ablk->cbarg, rsp->status);
	}
}

static void
bfa_ablk_notify(void *cbarg, enum bfa_ioc_event_e event)
{
	struct bfa_ablk_s *ablk = (struct bfa_ablk_s *)cbarg;

	bfa_trc(ablk->ioc, event);

	switch (event) {
	case BFA_IOC_E_ENABLED:
		WARN_ON(ablk->busy != BFA_FALSE);
		break;

	case BFA_IOC_E_DISABLED:
	case BFA_IOC_E_FAILED:
		/* Fail any pending requests */
		ablk->pcifn = NULL;
		if (ablk->busy) {
			if (ablk->cbfn)
				ablk->cbfn(ablk->cbarg, BFA_STATUS_FAILED);
			ablk->cbfn = NULL;
			ablk->busy = BFA_FALSE;
		}
		break;

	default:
		WARN_ON(1);
		break;
	}
}

u32
bfa_ablk_meminfo(void)
{
	return BFA_ROUNDUP(sizeof(struct bfa_ablk_cfg_s), BFA_DMA_ALIGN_SZ);
}

void
bfa_ablk_memclaim(struct bfa_ablk_s *ablk, u8 *dma_kva, u64 dma_pa)
{
	ablk->dma_addr.kva = dma_kva;
	ablk->dma_addr.pa  = dma_pa;
}

void
bfa_ablk_attach(struct bfa_ablk_s *ablk, struct bfa_ioc_s *ioc)
{
	ablk->ioc = ioc;

	bfa_ioc_mbox_regisr(ablk->ioc, BFI_MC_ABLK, bfa_ablk_isr, ablk);
	bfa_q_qe_init(&ablk->ioc_notify);
	bfa_ioc_notify_init(&ablk->ioc_notify, bfa_ablk_notify, ablk);
	list_add_tail(&ablk->ioc_notify.qe, &ablk->ioc->notify_q);
}

bfa_status_t
bfa_ablk_query(struct bfa_ablk_s *ablk, struct bfa_ablk_cfg_s *ablk_cfg,
		bfa_ablk_cbfn_t cbfn, void *cbarg)
{
	struct bfi_ablk_h2i_query_s *m;

	WARN_ON(!ablk_cfg);

	if (!bfa_ioc_is_operational(ablk->ioc)) {
		bfa_trc(ablk->ioc, BFA_STATUS_IOC_FAILURE);
		return BFA_STATUS_IOC_FAILURE;
	}

	if (ablk->busy) {
		bfa_trc(ablk->ioc, BFA_STATUS_DEVBUSY);
		return  BFA_STATUS_DEVBUSY;
	}

	ablk->cfg = ablk_cfg;
	ablk->cbfn  = cbfn;
	ablk->cbarg = cbarg;
	ablk->busy  = BFA_TRUE;

	m = (struct bfi_ablk_h2i_query_s *)ablk->mb.msg;
	bfi_h2i_set(m->mh, BFI_MC_ABLK, BFI_ABLK_H2I_QUERY,
		    bfa_ioc_portid(ablk->ioc));
	bfa_dma_be_addr_set(m->addr, ablk->dma_addr.pa);
	bfa_ioc_mbox_queue(ablk->ioc, &ablk->mb);

	return BFA_STATUS_OK;
}

bfa_status_t
bfa_ablk_pf_create(struct bfa_ablk_s *ablk, u16 *pcifn,
		u8 port, enum bfi_pcifn_class personality, int bw,
		bfa_ablk_cbfn_t cbfn, void *cbarg)
{
	struct bfi_ablk_h2i_pf_req_s *m;

	if (!bfa_ioc_is_operational(ablk->ioc)) {
		bfa_trc(ablk->ioc, BFA_STATUS_IOC_FAILURE);
		return BFA_STATUS_IOC_FAILURE;
	}

	if (ablk->busy) {
		bfa_trc(ablk->ioc, BFA_STATUS_DEVBUSY);
		return  BFA_STATUS_DEVBUSY;
	}

	ablk->pcifn = pcifn;
	ablk->cbfn = cbfn;
	ablk->cbarg = cbarg;
	ablk->busy  = BFA_TRUE;

	m = (struct bfi_ablk_h2i_pf_req_s *)ablk->mb.msg;
	bfi_h2i_set(m->mh, BFI_MC_ABLK, BFI_ABLK_H2I_PF_CREATE,
		    bfa_ioc_portid(ablk->ioc));
	m->pers = cpu_to_be16((u16)personality);
	m->bw = cpu_to_be32(bw);
	m->port = port;
	bfa_ioc_mbox_queue(ablk->ioc, &ablk->mb);

	return BFA_STATUS_OK;
}

bfa_status_t
bfa_ablk_pf_delete(struct bfa_ablk_s *ablk, int pcifn,
		bfa_ablk_cbfn_t cbfn, void *cbarg)
{
	struct bfi_ablk_h2i_pf_req_s *m;

	if (!bfa_ioc_is_operational(ablk->ioc)) {
		bfa_trc(ablk->ioc, BFA_STATUS_IOC_FAILURE);
		return BFA_STATUS_IOC_FAILURE;
	}

	if (ablk->busy) {
		bfa_trc(ablk->ioc, BFA_STATUS_DEVBUSY);
		return  BFA_STATUS_DEVBUSY;
	}

	ablk->cbfn  = cbfn;
	ablk->cbarg = cbarg;
	ablk->busy  = BFA_TRUE;

	m = (struct bfi_ablk_h2i_pf_req_s *)ablk->mb.msg;
	bfi_h2i_set(m->mh, BFI_MC_ABLK, BFI_ABLK_H2I_PF_DELETE,
		    bfa_ioc_portid(ablk->ioc));
	m->pcifn = (u8)pcifn;
	bfa_ioc_mbox_queue(ablk->ioc, &ablk->mb);

	return BFA_STATUS_OK;
}

bfa_status_t
bfa_ablk_adapter_config(struct bfa_ablk_s *ablk, enum bfa_mode_s mode,
		int max_pf, int max_vf, bfa_ablk_cbfn_t cbfn, void *cbarg)
{
	struct bfi_ablk_h2i_cfg_req_s *m;

	if (!bfa_ioc_is_operational(ablk->ioc)) {
		bfa_trc(ablk->ioc, BFA_STATUS_IOC_FAILURE);
		return BFA_STATUS_IOC_FAILURE;
	}

	if (ablk->busy) {
		bfa_trc(ablk->ioc, BFA_STATUS_DEVBUSY);
		return  BFA_STATUS_DEVBUSY;
	}

	ablk->cbfn  = cbfn;
	ablk->cbarg = cbarg;
	ablk->busy  = BFA_TRUE;

	m = (struct bfi_ablk_h2i_cfg_req_s *)ablk->mb.msg;
	bfi_h2i_set(m->mh, BFI_MC_ABLK, BFI_ABLK_H2I_ADPT_CONFIG,
		    bfa_ioc_portid(ablk->ioc));
	m->mode = (u8)mode;
	m->max_pf = (u8)max_pf;
	m->max_vf = (u8)max_vf;
	bfa_ioc_mbox_queue(ablk->ioc, &ablk->mb);

	return BFA_STATUS_OK;
}

bfa_status_t
bfa_ablk_port_config(struct bfa_ablk_s *ablk, int port, enum bfa_mode_s mode,
		int max_pf, int max_vf, bfa_ablk_cbfn_t cbfn, void *cbarg)
{
	struct bfi_ablk_h2i_cfg_req_s *m;

	if (!bfa_ioc_is_operational(ablk->ioc)) {
		bfa_trc(ablk->ioc, BFA_STATUS_IOC_FAILURE);
		return BFA_STATUS_IOC_FAILURE;
	}

	if (ablk->busy) {
		bfa_trc(ablk->ioc, BFA_STATUS_DEVBUSY);
		return  BFA_STATUS_DEVBUSY;
	}

	ablk->cbfn  = cbfn;
	ablk->cbarg = cbarg;
	ablk->busy  = BFA_TRUE;

	m = (struct bfi_ablk_h2i_cfg_req_s *)ablk->mb.msg;
	bfi_h2i_set(m->mh, BFI_MC_ABLK, BFI_ABLK_H2I_PORT_CONFIG,
		bfa_ioc_portid(ablk->ioc));
	m->port = (u8)port;
	m->mode = (u8)mode;
	m->max_pf = (u8)max_pf;
	m->max_vf = (u8)max_vf;
	bfa_ioc_mbox_queue(ablk->ioc, &ablk->mb);

	return BFA_STATUS_OK;
}

bfa_status_t
bfa_ablk_pf_update(struct bfa_ablk_s *ablk, int pcifn, int bw,
		bfa_ablk_cbfn_t cbfn, void *cbarg)
{
	struct bfi_ablk_h2i_pf_req_s *m;

	if (!bfa_ioc_is_operational(ablk->ioc)) {
		bfa_trc(ablk->ioc, BFA_STATUS_IOC_FAILURE);
		return BFA_STATUS_IOC_FAILURE;
	}

	if (ablk->busy) {
		bfa_trc(ablk->ioc, BFA_STATUS_DEVBUSY);
		return  BFA_STATUS_DEVBUSY;
	}

	ablk->cbfn  = cbfn;
	ablk->cbarg = cbarg;
	ablk->busy  = BFA_TRUE;

	m = (struct bfi_ablk_h2i_pf_req_s *)ablk->mb.msg;
	bfi_h2i_set(m->mh, BFI_MC_ABLK, BFI_ABLK_H2I_PF_UPDATE,
		bfa_ioc_portid(ablk->ioc));
	m->pcifn = (u8)pcifn;
	m->bw = cpu_to_be32(bw);
	bfa_ioc_mbox_queue(ablk->ioc, &ablk->mb);

	return BFA_STATUS_OK;
}

bfa_status_t
bfa_ablk_optrom_en(struct bfa_ablk_s *ablk, bfa_ablk_cbfn_t cbfn, void *cbarg)
{
	struct bfi_ablk_h2i_optrom_s *m;

	if (!bfa_ioc_is_operational(ablk->ioc)) {
		bfa_trc(ablk->ioc, BFA_STATUS_IOC_FAILURE);
		return BFA_STATUS_IOC_FAILURE;
	}

	if (ablk->busy) {
		bfa_trc(ablk->ioc, BFA_STATUS_DEVBUSY);
		return  BFA_STATUS_DEVBUSY;
	}

	ablk->cbfn  = cbfn;
	ablk->cbarg = cbarg;
	ablk->busy  = BFA_TRUE;

	m = (struct bfi_ablk_h2i_optrom_s *)ablk->mb.msg;
	bfi_h2i_set(m->mh, BFI_MC_ABLK, BFI_ABLK_H2I_OPTROM_ENABLE,
		bfa_ioc_portid(ablk->ioc));
	bfa_ioc_mbox_queue(ablk->ioc, &ablk->mb);

	return BFA_STATUS_OK;
}

bfa_status_t
bfa_ablk_optrom_dis(struct bfa_ablk_s *ablk, bfa_ablk_cbfn_t cbfn, void *cbarg)
{
	struct bfi_ablk_h2i_optrom_s *m;

	if (!bfa_ioc_is_operational(ablk->ioc)) {
		bfa_trc(ablk->ioc, BFA_STATUS_IOC_FAILURE);
		return BFA_STATUS_IOC_FAILURE;
	}

	if (ablk->busy) {
		bfa_trc(ablk->ioc, BFA_STATUS_DEVBUSY);
		return  BFA_STATUS_DEVBUSY;
	}

	ablk->cbfn  = cbfn;
	ablk->cbarg = cbarg;
	ablk->busy  = BFA_TRUE;

	m = (struct bfi_ablk_h2i_optrom_s *)ablk->mb.msg;
	bfi_h2i_set(m->mh, BFI_MC_ABLK, BFI_ABLK_H2I_OPTROM_DISABLE,
		bfa_ioc_portid(ablk->ioc));
	bfa_ioc_mbox_queue(ablk->ioc, &ablk->mb);

	return BFA_STATUS_OK;
}

/*
 *	SFP module specific
 */

/* forward declarations */
static void bfa_sfp_getdata_send(struct bfa_sfp_s *sfp);
static void bfa_sfp_media_get(struct bfa_sfp_s *sfp);
static bfa_status_t bfa_sfp_speed_valid(struct bfa_sfp_s *sfp,
				enum bfa_port_speed portspeed);

static void
bfa_cb_sfp_show(struct bfa_sfp_s *sfp)
{
	bfa_trc(sfp, sfp->lock);
	if (sfp->cbfn)
		sfp->cbfn(sfp->cbarg, sfp->status);
	sfp->lock = 0;
	sfp->cbfn = NULL;
}

static void
bfa_cb_sfp_state_query(struct bfa_sfp_s *sfp)
{
	bfa_trc(sfp, sfp->portspeed);
	if (sfp->media) {
		bfa_sfp_media_get(sfp);
		if (sfp->state_query_cbfn)
			sfp->state_query_cbfn(sfp->state_query_cbarg,
					sfp->status);
			sfp->media = NULL;
		}

		if (sfp->portspeed) {
			sfp->status = bfa_sfp_speed_valid(sfp, sfp->portspeed);
			if (sfp->state_query_cbfn)
				sfp->state_query_cbfn(sfp->state_query_cbarg,
						sfp->status);
				sfp->portspeed = BFA_PORT_SPEED_UNKNOWN;
		}

		sfp->state_query_lock = 0;
		sfp->state_query_cbfn = NULL;
}

/*
 *	IOC event handler.
 */
static void
bfa_sfp_notify(void *sfp_arg, enum bfa_ioc_event_e event)
{
	struct bfa_sfp_s *sfp = sfp_arg;

	bfa_trc(sfp, event);
	bfa_trc(sfp, sfp->lock);
	bfa_trc(sfp, sfp->state_query_lock);

	switch (event) {
	case BFA_IOC_E_DISABLED:
	case BFA_IOC_E_FAILED:
		if (sfp->lock) {
			sfp->status = BFA_STATUS_IOC_FAILURE;
			bfa_cb_sfp_show(sfp);
		}

		if (sfp->state_query_lock) {
			sfp->status = BFA_STATUS_IOC_FAILURE;
			bfa_cb_sfp_state_query(sfp);
		}
		break;

	default:
		break;
	}
}

/*
 * SFP's State Change Notification post to AEN
 */
static void
bfa_sfp_scn_aen_post(struct bfa_sfp_s *sfp, struct bfi_sfp_scn_s *rsp)
{
	struct bfad_s *bfad = (struct bfad_s *)sfp->ioc->bfa->bfad;
	struct bfa_aen_entry_s  *aen_entry;
	enum bfa_port_aen_event aen_evt = 0;

	bfa_trc(sfp, (((u64)rsp->pomlvl) << 16) | (((u64)rsp->sfpid) << 8) |
		      ((u64)rsp->event));

	bfad_get_aen_entry(bfad, aen_entry);
	if (!aen_entry)
		return;

	aen_entry->aen_data.port.ioc_type = bfa_ioc_get_type(sfp->ioc);
	aen_entry->aen_data.port.pwwn = sfp->ioc->attr->pwwn;
	aen_entry->aen_data.port.mac = bfa_ioc_get_mac(sfp->ioc);

	switch (rsp->event) {
	case BFA_SFP_SCN_INSERTED:
		aen_evt = BFA_PORT_AEN_SFP_INSERT;
		break;
	case BFA_SFP_SCN_REMOVED:
		aen_evt = BFA_PORT_AEN_SFP_REMOVE;
		break;
	case BFA_SFP_SCN_FAILED:
		aen_evt = BFA_PORT_AEN_SFP_ACCESS_ERROR;
		break;
	case BFA_SFP_SCN_UNSUPPORT:
		aen_evt = BFA_PORT_AEN_SFP_UNSUPPORT;
		break;
	case BFA_SFP_SCN_POM:
		aen_evt = BFA_PORT_AEN_SFP_POM;
		aen_entry->aen_data.port.level = rsp->pomlvl;
		break;
	default:
		bfa_trc(sfp, rsp->event);
		WARN_ON(1);
	}

	/* Send the AEN notification */
	bfad_im_post_vendor_event(aen_entry, bfad, ++sfp->ioc->ioc_aen_seq,
				  BFA_AEN_CAT_PORT, aen_evt);
}

/*
 *	SFP get data send
 */
static void
bfa_sfp_getdata_send(struct bfa_sfp_s *sfp)
{
	struct bfi_sfp_req_s *req = (struct bfi_sfp_req_s *)sfp->mbcmd.msg;

	bfa_trc(sfp, req->memtype);

	/* build host command */
	bfi_h2i_set(req->mh, BFI_MC_SFP, BFI_SFP_H2I_SHOW,
			bfa_ioc_portid(sfp->ioc));

	/* send mbox cmd */
	bfa_ioc_mbox_queue(sfp->ioc, &sfp->mbcmd);
}

/*
 *	SFP is valid, read sfp data
 */
static void
bfa_sfp_getdata(struct bfa_sfp_s *sfp, enum bfi_sfp_mem_e memtype)
{
	struct bfi_sfp_req_s *req = (struct bfi_sfp_req_s *)sfp->mbcmd.msg;

	WARN_ON(sfp->lock != 0);
	bfa_trc(sfp, sfp->state);

	sfp->lock = 1;
	sfp->memtype = memtype;
	req->memtype = memtype;

	/* Setup SG list */
	bfa_alen_set(&req->alen, sizeof(struct sfp_mem_s), sfp->dbuf_pa);

	bfa_sfp_getdata_send(sfp);
}

/*
 *	SFP scn handler
 */
static void
bfa_sfp_scn(struct bfa_sfp_s *sfp, struct bfi_mbmsg_s *msg)
{
	struct bfi_sfp_scn_s *rsp = (struct bfi_sfp_scn_s *) msg;

	switch (rsp->event) {
	case BFA_SFP_SCN_INSERTED:
		sfp->state = BFA_SFP_STATE_INSERTED;
		sfp->data_valid = 0;
		bfa_sfp_scn_aen_post(sfp, rsp);
		break;
	case BFA_SFP_SCN_REMOVED:
		sfp->state = BFA_SFP_STATE_REMOVED;
		sfp->data_valid = 0;
		bfa_sfp_scn_aen_post(sfp, rsp);
		 break;
	case BFA_SFP_SCN_FAILED:
		sfp->state = BFA_SFP_STATE_FAILED;
		sfp->data_valid = 0;
		bfa_sfp_scn_aen_post(sfp, rsp);
		break;
	case BFA_SFP_SCN_UNSUPPORT:
		sfp->state = BFA_SFP_STATE_UNSUPPORT;
		bfa_sfp_scn_aen_post(sfp, rsp);
		if (!sfp->lock)
			bfa_sfp_getdata(sfp, BFI_SFP_MEM_ALL);
		break;
	case BFA_SFP_SCN_POM:
		bfa_sfp_scn_aen_post(sfp, rsp);
		break;
	case BFA_SFP_SCN_VALID:
		sfp->state = BFA_SFP_STATE_VALID;
		if (!sfp->lock)
			bfa_sfp_getdata(sfp, BFI_SFP_MEM_ALL);
		break;
	default:
		bfa_trc(sfp, rsp->event);
		WARN_ON(1);
	}
}

/*
 * SFP show complete
 */
static void
bfa_sfp_show_comp(struct bfa_sfp_s *sfp, struct bfi_mbmsg_s *msg)
{
	struct bfi_sfp_rsp_s *rsp = (struct bfi_sfp_rsp_s *) msg;

	if (!sfp->lock) {
		/*
		 * receiving response after ioc failure
		 */
		bfa_trc(sfp, sfp->lock);
		return;
	}

	bfa_trc(sfp, rsp->status);
	if (rsp->status == BFA_STATUS_OK) {
		sfp->data_valid = 1;
		if (sfp->state == BFA_SFP_STATE_VALID)
			sfp->status = BFA_STATUS_OK;
		else if (sfp->state == BFA_SFP_STATE_UNSUPPORT)
			sfp->status = BFA_STATUS_SFP_UNSUPP;
		else
			bfa_trc(sfp, sfp->state);
	} else {
		sfp->data_valid = 0;
		sfp->status = rsp->status;
		/* sfpshow shouldn't change sfp state */
	}

	bfa_trc(sfp, sfp->memtype);
	if (sfp->memtype == BFI_SFP_MEM_DIAGEXT) {
		bfa_trc(sfp, sfp->data_valid);
		if (sfp->data_valid) {
			u32	size = sizeof(struct sfp_mem_s);
			u8 *des = (u8 *) &(sfp->sfpmem->srlid_base);
			memcpy(des, sfp->dbuf_kva, size);
		}
		/*
		 * Queue completion callback.
		 */
		bfa_cb_sfp_show(sfp);
	} else
		sfp->lock = 0;

	bfa_trc(sfp, sfp->state_query_lock);
	if (sfp->state_query_lock) {
		sfp->state = rsp->state;
		/* Complete callback */
		bfa_cb_sfp_state_query(sfp);
	}
}

/*
 *	SFP query fw sfp state
 */
static void
bfa_sfp_state_query(struct bfa_sfp_s *sfp)
{
	struct bfi_sfp_req_s *req = (struct bfi_sfp_req_s *)sfp->mbcmd.msg;

	/* Should not be doing query if not in _INIT state */
	WARN_ON(sfp->state != BFA_SFP_STATE_INIT);
	WARN_ON(sfp->state_query_lock != 0);
	bfa_trc(sfp, sfp->state);

	sfp->state_query_lock = 1;
	req->memtype = 0;

	if (!sfp->lock)
		bfa_sfp_getdata(sfp, BFI_SFP_MEM_ALL);
}

static void
bfa_sfp_media_get(struct bfa_sfp_s *sfp)
{
	enum bfa_defs_sfp_media_e *media = sfp->media;

	*media = BFA_SFP_MEDIA_UNKNOWN;

	if (sfp->state == BFA_SFP_STATE_UNSUPPORT)
		*media = BFA_SFP_MEDIA_UNSUPPORT;
	else if (sfp->state == BFA_SFP_STATE_VALID) {
		union sfp_xcvr_e10g_code_u e10g;
		struct sfp_mem_s *sfpmem = (struct sfp_mem_s *)sfp->dbuf_kva;
		u16 xmtr_tech = (sfpmem->srlid_base.xcvr[4] & 0x3) << 7 |
				(sfpmem->srlid_base.xcvr[5] >> 1);

		e10g.b = sfpmem->srlid_base.xcvr[0];
		bfa_trc(sfp, e10g.b);
		bfa_trc(sfp, xmtr_tech);
		/* check fc transmitter tech */
		if ((xmtr_tech & SFP_XMTR_TECH_CU) ||
		    (xmtr_tech & SFP_XMTR_TECH_CP) ||
		    (xmtr_tech & SFP_XMTR_TECH_CA))
			*media = BFA_SFP_MEDIA_CU;
		else if ((xmtr_tech & SFP_XMTR_TECH_EL_INTRA) ||
			 (xmtr_tech & SFP_XMTR_TECH_EL_INTER))
			*media = BFA_SFP_MEDIA_EL;
		else if ((xmtr_tech & SFP_XMTR_TECH_LL) ||
			 (xmtr_tech & SFP_XMTR_TECH_LC))
			*media = BFA_SFP_MEDIA_LW;
		else if ((xmtr_tech & SFP_XMTR_TECH_SL) ||
			 (xmtr_tech & SFP_XMTR_TECH_SN) ||
			 (xmtr_tech & SFP_XMTR_TECH_SA))
			*media = BFA_SFP_MEDIA_SW;
		/* Check 10G Ethernet Compilance code */
		else if (e10g.r.e10g_sr)
			*media = BFA_SFP_MEDIA_SW;
		else if (e10g.r.e10g_lrm && e10g.r.e10g_lr)
			*media = BFA_SFP_MEDIA_LW;
		else if (e10g.r.e10g_unall)
			*media = BFA_SFP_MEDIA_UNKNOWN;
		else
			bfa_trc(sfp, 0);
	} else
		bfa_trc(sfp, sfp->state);
}

static bfa_status_t
bfa_sfp_speed_valid(struct bfa_sfp_s *sfp, enum bfa_port_speed portspeed)
{
	struct sfp_mem_s *sfpmem = (struct sfp_mem_s *)sfp->dbuf_kva;
	struct sfp_xcvr_s *xcvr = (struct sfp_xcvr_s *) sfpmem->srlid_base.xcvr;
	union sfp_xcvr_fc3_code_u fc3 = xcvr->fc3;
	union sfp_xcvr_e10g_code_u e10g = xcvr->e10g;

	if (portspeed == BFA_PORT_SPEED_10GBPS) {
		if (e10g.r.e10g_sr || e10g.r.e10g_lr)
			return BFA_STATUS_OK;
		else {
			bfa_trc(sfp, e10g.b);
			return BFA_STATUS_UNSUPP_SPEED;
		}
	}
	if (((portspeed & BFA_PORT_SPEED_16GBPS) && fc3.r.mb1600) ||
	    ((portspeed & BFA_PORT_SPEED_8GBPS) && fc3.r.mb800) ||
	    ((portspeed & BFA_PORT_SPEED_4GBPS) && fc3.r.mb400) ||
	    ((portspeed & BFA_PORT_SPEED_2GBPS) && fc3.r.mb200) ||
	    ((portspeed & BFA_PORT_SPEED_1GBPS) && fc3.r.mb100))
		return BFA_STATUS_OK;
	else {
		bfa_trc(sfp, portspeed);
		bfa_trc(sfp, fc3.b);
		bfa_trc(sfp, e10g.b);
		return BFA_STATUS_UNSUPP_SPEED;
	}
}

/*
 *	SFP hmbox handler
 */
void
bfa_sfp_intr(void *sfparg, struct bfi_mbmsg_s *msg)
{
	struct bfa_sfp_s *sfp = sfparg;

	switch (msg->mh.msg_id) {
	case BFI_SFP_I2H_SHOW:
		bfa_sfp_show_comp(sfp, msg);
		break;

	case BFI_SFP_I2H_SCN:
		bfa_sfp_scn(sfp, msg);
		break;

	default:
		bfa_trc(sfp, msg->mh.msg_id);
		WARN_ON(1);
	}
}

/*
 *	Return DMA memory needed by sfp module.
 */
u32
bfa_sfp_meminfo(void)
{
	return BFA_ROUNDUP(sizeof(struct sfp_mem_s), BFA_DMA_ALIGN_SZ);
}

/*
 *	Attach virtual and physical memory for SFP.
 */
void
bfa_sfp_attach(struct bfa_sfp_s *sfp, struct bfa_ioc_s *ioc, void *dev,
		struct bfa_trc_mod_s *trcmod)
{
	sfp->dev = dev;
	sfp->ioc = ioc;
	sfp->trcmod = trcmod;

	sfp->cbfn = NULL;
	sfp->cbarg = NULL;
	sfp->sfpmem = NULL;
	sfp->lock = 0;
	sfp->data_valid = 0;
	sfp->state = BFA_SFP_STATE_INIT;
	sfp->state_query_lock = 0;
	sfp->state_query_cbfn = NULL;
	sfp->state_query_cbarg = NULL;
	sfp->media = NULL;
	sfp->portspeed = BFA_PORT_SPEED_UNKNOWN;
	sfp->is_elb = BFA_FALSE;

	bfa_ioc_mbox_regisr(sfp->ioc, BFI_MC_SFP, bfa_sfp_intr, sfp);
	bfa_q_qe_init(&sfp->ioc_notify);
	bfa_ioc_notify_init(&sfp->ioc_notify, bfa_sfp_notify, sfp);
	list_add_tail(&sfp->ioc_notify.qe, &sfp->ioc->notify_q);
}

/*
 *	Claim Memory for SFP
 */
void
bfa_sfp_memclaim(struct bfa_sfp_s *sfp, u8 *dm_kva, u64 dm_pa)
{
	sfp->dbuf_kva   = dm_kva;
	sfp->dbuf_pa    = dm_pa;
	memset(sfp->dbuf_kva, 0, sizeof(struct sfp_mem_s));

	dm_kva += BFA_ROUNDUP(sizeof(struct sfp_mem_s), BFA_DMA_ALIGN_SZ);
	dm_pa += BFA_ROUNDUP(sizeof(struct sfp_mem_s), BFA_DMA_ALIGN_SZ);
}

/*
 * Show SFP eeprom content
 *
 * @param[in] sfp   - bfa sfp module
 *
 * @param[out] sfpmem - sfp eeprom data
 *
 */
bfa_status_t
bfa_sfp_show(struct bfa_sfp_s *sfp, struct sfp_mem_s *sfpmem,
		bfa_cb_sfp_t cbfn, void *cbarg)
{

	if (!bfa_ioc_is_operational(sfp->ioc)) {
		bfa_trc(sfp, 0);
		return BFA_STATUS_IOC_NON_OP;
	}

	if (sfp->lock) {
		bfa_trc(sfp, 0);
		return BFA_STATUS_DEVBUSY;
	}

	sfp->cbfn = cbfn;
	sfp->cbarg = cbarg;
	sfp->sfpmem = sfpmem;

	bfa_sfp_getdata(sfp, BFI_SFP_MEM_DIAGEXT);
	return BFA_STATUS_OK;
}

/*
 * Return SFP Media type
 *
 * @param[in] sfp   - bfa sfp module
 *
 * @param[out] media - port speed from user
 *
 */
bfa_status_t
bfa_sfp_media(struct bfa_sfp_s *sfp, enum bfa_defs_sfp_media_e *media,
		bfa_cb_sfp_t cbfn, void *cbarg)
{
	if (!bfa_ioc_is_operational(sfp->ioc)) {
		bfa_trc(sfp, 0);
		return BFA_STATUS_IOC_NON_OP;
	}

	sfp->media = media;
	if (sfp->state == BFA_SFP_STATE_INIT) {
		if (sfp->state_query_lock) {
			bfa_trc(sfp, 0);
			return BFA_STATUS_DEVBUSY;
		} else {
			sfp->state_query_cbfn = cbfn;
			sfp->state_query_cbarg = cbarg;
			bfa_sfp_state_query(sfp);
			return BFA_STATUS_SFP_NOT_READY;
		}
	}

	bfa_sfp_media_get(sfp);
	return BFA_STATUS_OK;
}

/*
 * Check if user set port speed is allowed by the SFP
 *
 * @param[in] sfp   - bfa sfp module
 * @param[in] portspeed - port speed from user
 *
 */
bfa_status_t
bfa_sfp_speed(struct bfa_sfp_s *sfp, enum bfa_port_speed portspeed,
		bfa_cb_sfp_t cbfn, void *cbarg)
{
	WARN_ON(portspeed == BFA_PORT_SPEED_UNKNOWN);

	if (!bfa_ioc_is_operational(sfp->ioc))
		return BFA_STATUS_IOC_NON_OP;

	/* For Mezz card, all speed is allowed */
	if (bfa_mfg_is_mezz(sfp->ioc->attr->card_type))
		return BFA_STATUS_OK;

	/* Check SFP state */
	sfp->portspeed = portspeed;
	if (sfp->state == BFA_SFP_STATE_INIT) {
		if (sfp->state_query_lock) {
			bfa_trc(sfp, 0);
			return BFA_STATUS_DEVBUSY;
		} else {
			sfp->state_query_cbfn = cbfn;
			sfp->state_query_cbarg = cbarg;
			bfa_sfp_state_query(sfp);
			return BFA_STATUS_SFP_NOT_READY;
		}
	}

	if (sfp->state == BFA_SFP_STATE_REMOVED ||
	    sfp->state == BFA_SFP_STATE_FAILED) {
		bfa_trc(sfp, sfp->state);
		return BFA_STATUS_NO_SFP_DEV;
	}

	if (sfp->state == BFA_SFP_STATE_INSERTED) {
		bfa_trc(sfp, sfp->state);
		return BFA_STATUS_DEVBUSY;  /* sfp is reading data */
	}

	/* For eloopback, all speed is allowed */
	if (sfp->is_elb)
		return BFA_STATUS_OK;

	return bfa_sfp_speed_valid(sfp, portspeed);
}

/*
 *	Flash module specific
 */

/*
 * FLASH DMA buffer should be big enough to hold both MFG block and
 * asic block(64k) at the same time and also should be 2k aligned to
 * avoid write segement to cross sector boundary.
 */
#define BFA_FLASH_SEG_SZ	2048
#define BFA_FLASH_DMA_BUF_SZ	\
	BFA_ROUNDUP(0x010000 + sizeof(struct bfa_mfg_block_s), BFA_FLASH_SEG_SZ)

static void
bfa_flash_aen_audit_post(struct bfa_ioc_s *ioc, enum bfa_audit_aen_event event,
			int inst, int type)
{
	struct bfad_s *bfad = (struct bfad_s *)ioc->bfa->bfad;
	struct bfa_aen_entry_s  *aen_entry;

	bfad_get_aen_entry(bfad, aen_entry);
	if (!aen_entry)
		return;

	aen_entry->aen_data.audit.pwwn = ioc->attr->pwwn;
	aen_entry->aen_data.audit.partition_inst = inst;
	aen_entry->aen_data.audit.partition_type = type;

	/* Send the AEN notification */
	bfad_im_post_vendor_event(aen_entry, bfad, ++ioc->ioc_aen_seq,
				  BFA_AEN_CAT_AUDIT, event);
}

static void
bfa_flash_cb(struct bfa_flash_s *flash)
{
	flash->op_busy = 0;
	if (flash->cbfn)
		flash->cbfn(flash->cbarg, flash->status);
}

static void
bfa_flash_notify(void *cbarg, enum bfa_ioc_event_e event)
{
	struct bfa_flash_s	*flash = cbarg;

	bfa_trc(flash, event);
	switch (event) {
	case BFA_IOC_E_DISABLED:
	case BFA_IOC_E_FAILED:
		if (flash->op_busy) {
			flash->status = BFA_STATUS_IOC_FAILURE;
			flash->cbfn(flash->cbarg, flash->status);
			flash->op_busy = 0;
		}
		break;

	default:
		break;
	}
}

/*
 * Send flash attribute query request.
 *
 * @param[in] cbarg - callback argument
 */
static void
bfa_flash_query_send(void *cbarg)
{
	struct bfa_flash_s *flash = cbarg;
	struct bfi_flash_query_req_s *msg =
			(struct bfi_flash_query_req_s *) flash->mb.msg;

	bfi_h2i_set(msg->mh, BFI_MC_FLASH, BFI_FLASH_H2I_QUERY_REQ,
		bfa_ioc_portid(flash->ioc));
	bfa_alen_set(&msg->alen, sizeof(struct bfa_flash_attr_s),
		flash->dbuf_pa);
	bfa_ioc_mbox_queue(flash->ioc, &flash->mb);
}

/*
 * Send flash write request.
 *
 * @param[in] cbarg - callback argument
 */
static void
bfa_flash_write_send(struct bfa_flash_s *flash)
{
	struct bfi_flash_write_req_s *msg =
			(struct bfi_flash_write_req_s *) flash->mb.msg;
	u32	len;

	msg->type = be32_to_cpu(flash->type);
	msg->instance = flash->instance;
	msg->offset = be32_to_cpu(flash->addr_off + flash->offset);
	len = (flash->residue < BFA_FLASH_DMA_BUF_SZ) ?
		flash->residue : BFA_FLASH_DMA_BUF_SZ;
	msg->length = be32_to_cpu(len);

	/* indicate if it's the last msg of the whole write operation */
	msg->last = (len == flash->residue) ? 1 : 0;

	bfi_h2i_set(msg->mh, BFI_MC_FLASH, BFI_FLASH_H2I_WRITE_REQ,
			bfa_ioc_portid(flash->ioc));
	bfa_alen_set(&msg->alen, len, flash->dbuf_pa);
	memcpy(flash->dbuf_kva, flash->ubuf + flash->offset, len);
	bfa_ioc_mbox_queue(flash->ioc, &flash->mb);

	flash->residue -= len;
	flash->offset += len;
}

/*
 * Send flash read request.
 *
 * @param[in] cbarg - callback argument
 */
static void
bfa_flash_read_send(void *cbarg)
{
	struct bfa_flash_s *flash = cbarg;
	struct bfi_flash_read_req_s *msg =
			(struct bfi_flash_read_req_s *) flash->mb.msg;
	u32	len;

	msg->type = be32_to_cpu(flash->type);
	msg->instance = flash->instance;
	msg->offset = be32_to_cpu(flash->addr_off + flash->offset);
	len = (flash->residue < BFA_FLASH_DMA_BUF_SZ) ?
			flash->residue : BFA_FLASH_DMA_BUF_SZ;
	msg->length = be32_to_cpu(len);
	bfi_h2i_set(msg->mh, BFI_MC_FLASH, BFI_FLASH_H2I_READ_REQ,
		bfa_ioc_portid(flash->ioc));
	bfa_alen_set(&msg->alen, len, flash->dbuf_pa);
	bfa_ioc_mbox_queue(flash->ioc, &flash->mb);
}

/*
 * Send flash erase request.
 *
 * @param[in] cbarg - callback argument
 */
static void
bfa_flash_erase_send(void *cbarg)
{
	struct bfa_flash_s *flash = cbarg;
	struct bfi_flash_erase_req_s *msg =
			(struct bfi_flash_erase_req_s *) flash->mb.msg;

	msg->type = be32_to_cpu(flash->type);
	msg->instance = flash->instance;
	bfi_h2i_set(msg->mh, BFI_MC_FLASH, BFI_FLASH_H2I_ERASE_REQ,
			bfa_ioc_portid(flash->ioc));
	bfa_ioc_mbox_queue(flash->ioc, &flash->mb);
}

/*
 * Process flash response messages upon receiving interrupts.
 *
 * @param[in] flasharg - flash structure
 * @param[in] msg - message structure
 */
static void
bfa_flash_intr(void *flasharg, struct bfi_mbmsg_s *msg)
{
	struct bfa_flash_s *flash = flasharg;
	u32	status;

	union {
		struct bfi_flash_query_rsp_s *query;
		struct bfi_flash_erase_rsp_s *erase;
		struct bfi_flash_write_rsp_s *write;
		struct bfi_flash_read_rsp_s *read;
		struct bfi_flash_event_s *event;
		struct bfi_mbmsg_s   *msg;
	} m;

	m.msg = msg;
	bfa_trc(flash, msg->mh.msg_id);

	if (!flash->op_busy && msg->mh.msg_id != BFI_FLASH_I2H_EVENT) {
		/* receiving response after ioc failure */
		bfa_trc(flash, 0x9999);
		return;
	}

	switch (msg->mh.msg_id) {
	case BFI_FLASH_I2H_QUERY_RSP:
		status = be32_to_cpu(m.query->status);
		bfa_trc(flash, status);
		if (status == BFA_STATUS_OK) {
			u32	i;
			struct bfa_flash_attr_s *attr, *f;

			attr = (struct bfa_flash_attr_s *) flash->ubuf;
			f = (struct bfa_flash_attr_s *) flash->dbuf_kva;
			attr->status = be32_to_cpu(f->status);
			attr->npart = be32_to_cpu(f->npart);
			bfa_trc(flash, attr->status);
			bfa_trc(flash, attr->npart);
			for (i = 0; i < attr->npart; i++) {
				attr->part[i].part_type =
					be32_to_cpu(f->part[i].part_type);
				attr->part[i].part_instance =
					be32_to_cpu(f->part[i].part_instance);
				attr->part[i].part_off =
					be32_to_cpu(f->part[i].part_off);
				attr->part[i].part_size =
					be32_to_cpu(f->part[i].part_size);
				attr->part[i].part_len =
					be32_to_cpu(f->part[i].part_len);
				attr->part[i].part_status =
					be32_to_cpu(f->part[i].part_status);
			}
		}
		flash->status = status;
		bfa_flash_cb(flash);
		break;
	case BFI_FLASH_I2H_ERASE_RSP:
		status = be32_to_cpu(m.erase->status);
		bfa_trc(flash, status);
		flash->status = status;
		bfa_flash_cb(flash);
		break;
	case BFI_FLASH_I2H_WRITE_RSP:
		status = be32_to_cpu(m.write->status);
		bfa_trc(flash, status);
		if (status != BFA_STATUS_OK || flash->residue == 0) {
			flash->status = status;
			bfa_flash_cb(flash);
		} else {
			bfa_trc(flash, flash->offset);
			bfa_flash_write_send(flash);
		}
		break;
	case BFI_FLASH_I2H_READ_RSP:
		status = be32_to_cpu(m.read->status);
		bfa_trc(flash, status);
		if (status != BFA_STATUS_OK) {
			flash->status = status;
			bfa_flash_cb(flash);
		} else {
			u32 len = be32_to_cpu(m.read->length);
			bfa_trc(flash, flash->offset);
			bfa_trc(flash, len);
			memcpy(flash->ubuf + flash->offset,
				flash->dbuf_kva, len);
			flash->residue -= len;
			flash->offset += len;
			if (flash->residue == 0) {
				flash->status = status;
				bfa_flash_cb(flash);
			} else
				bfa_flash_read_send(flash);
		}
		break;
	case BFI_FLASH_I2H_BOOT_VER_RSP:
		break;
	case BFI_FLASH_I2H_EVENT:
		status = be32_to_cpu(m.event->status);
		bfa_trc(flash, status);
		if (status == BFA_STATUS_BAD_FWCFG)
			bfa_ioc_aen_post(flash->ioc, BFA_IOC_AEN_FWCFG_ERROR);
		else if (status == BFA_STATUS_INVALID_VENDOR) {
			u32 param;
			param = be32_to_cpu(m.event->param);
			bfa_trc(flash, param);
			bfa_ioc_aen_post(flash->ioc,
				BFA_IOC_AEN_INVALID_VENDOR);
		}
		break;

	default:
		WARN_ON(1);
	}
}

/*
 * Flash memory info API.
 *
 * @param[in] mincfg - minimal cfg variable
 */
u32
bfa_flash_meminfo(bfa_boolean_t mincfg)
{
	/* min driver doesn't need flash */
	if (mincfg)
		return 0;
	return BFA_ROUNDUP(BFA_FLASH_DMA_BUF_SZ, BFA_DMA_ALIGN_SZ);
}

/*
 * Flash attach API.
 *
 * @param[in] flash - flash structure
 * @param[in] ioc  - ioc structure
 * @param[in] dev  - device structure
 * @param[in] trcmod - trace module
 * @param[in] logmod - log module
 */
void
bfa_flash_attach(struct bfa_flash_s *flash, struct bfa_ioc_s *ioc, void *dev,
		struct bfa_trc_mod_s *trcmod, bfa_boolean_t mincfg)
{
	flash->ioc = ioc;
	flash->trcmod = trcmod;
	flash->cbfn = NULL;
	flash->cbarg = NULL;
	flash->op_busy = 0;

	bfa_ioc_mbox_regisr(flash->ioc, BFI_MC_FLASH, bfa_flash_intr, flash);
	bfa_q_qe_init(&flash->ioc_notify);
	bfa_ioc_notify_init(&flash->ioc_notify, bfa_flash_notify, flash);
	list_add_tail(&flash->ioc_notify.qe, &flash->ioc->notify_q);

	/* min driver doesn't need flash */
	if (mincfg) {
		flash->dbuf_kva = NULL;
		flash->dbuf_pa = 0;
	}
}

/*
 * Claim memory for flash
 *
 * @param[in] flash - flash structure
 * @param[in] dm_kva - pointer to virtual memory address
 * @param[in] dm_pa - physical memory address
 * @param[in] mincfg - minimal cfg variable
 */
void
bfa_flash_memclaim(struct bfa_flash_s *flash, u8 *dm_kva, u64 dm_pa,
		bfa_boolean_t mincfg)
{
	if (mincfg)
		return;

	flash->dbuf_kva = dm_kva;
	flash->dbuf_pa = dm_pa;
	memset(flash->dbuf_kva, 0, BFA_FLASH_DMA_BUF_SZ);
	dm_kva += BFA_ROUNDUP(BFA_FLASH_DMA_BUF_SZ, BFA_DMA_ALIGN_SZ);
	dm_pa += BFA_ROUNDUP(BFA_FLASH_DMA_BUF_SZ, BFA_DMA_ALIGN_SZ);
}

/*
 * Get flash attribute.
 *
 * @param[in] flash - flash structure
 * @param[in] attr - flash attribute structure
 * @param[in] cbfn - callback function
 * @param[in] cbarg - callback argument
 *
 * Return status.
 */
bfa_status_t
bfa_flash_get_attr(struct bfa_flash_s *flash, struct bfa_flash_attr_s *attr,
		bfa_cb_flash_t cbfn, void *cbarg)
{
	bfa_trc(flash, BFI_FLASH_H2I_QUERY_REQ);

	if (!bfa_ioc_is_operational(flash->ioc))
		return BFA_STATUS_IOC_NON_OP;

	if (flash->op_busy) {
		bfa_trc(flash, flash->op_busy);
		return BFA_STATUS_DEVBUSY;
	}

	flash->op_busy = 1;
	flash->cbfn = cbfn;
	flash->cbarg = cbarg;
	flash->ubuf = (u8 *) attr;
	bfa_flash_query_send(flash);

	return BFA_STATUS_OK;
}

/*
 * Erase flash partition.
 *
 * @param[in] flash - flash structure
 * @param[in] type - flash partition type
 * @param[in] instance - flash partition instance
 * @param[in] cbfn - callback function
 * @param[in] cbarg - callback argument
 *
 * Return status.
 */
bfa_status_t
bfa_flash_erase_part(struct bfa_flash_s *flash, enum bfa_flash_part_type type,
		u8 instance, bfa_cb_flash_t cbfn, void *cbarg)
{
	bfa_trc(flash, BFI_FLASH_H2I_ERASE_REQ);
	bfa_trc(flash, type);
	bfa_trc(flash, instance);

	if (!bfa_ioc_is_operational(flash->ioc))
		return BFA_STATUS_IOC_NON_OP;

	if (flash->op_busy) {
		bfa_trc(flash, flash->op_busy);
		return BFA_STATUS_DEVBUSY;
	}

	flash->op_busy = 1;
	flash->cbfn = cbfn;
	flash->cbarg = cbarg;
	flash->type = type;
	flash->instance = instance;

	bfa_flash_erase_send(flash);
	bfa_flash_aen_audit_post(flash->ioc, BFA_AUDIT_AEN_FLASH_ERASE,
				instance, type);
	return BFA_STATUS_OK;
}

/*
 * Update flash partition.
 *
 * @param[in] flash - flash structure
 * @param[in] type - flash partition type
 * @param[in] instance - flash partition instance
 * @param[in] buf - update data buffer
 * @param[in] len - data buffer length
 * @param[in] offset - offset relative to the partition starting address
 * @param[in] cbfn - callback function
 * @param[in] cbarg - callback argument
 *
 * Return status.
 */
bfa_status_t
bfa_flash_update_part(struct bfa_flash_s *flash, enum bfa_flash_part_type type,
		u8 instance, void *buf, u32 len, u32 offset,
		bfa_cb_flash_t cbfn, void *cbarg)
{
	bfa_trc(flash, BFI_FLASH_H2I_WRITE_REQ);
	bfa_trc(flash, type);
	bfa_trc(flash, instance);
	bfa_trc(flash, len);
	bfa_trc(flash, offset);

	if (!bfa_ioc_is_operational(flash->ioc))
		return BFA_STATUS_IOC_NON_OP;

	/*
	 * 'len' must be in word (4-byte) boundary
	 * 'offset' must be in sector (16kb) boundary
	 */
	if (!len || (len & 0x03) || (offset & 0x00003FFF))
		return BFA_STATUS_FLASH_BAD_LEN;

	if (type == BFA_FLASH_PART_MFG)
		return BFA_STATUS_EINVAL;

	if (flash->op_busy) {
		bfa_trc(flash, flash->op_busy);
		return BFA_STATUS_DEVBUSY;
	}

	flash->op_busy = 1;
	flash->cbfn = cbfn;
	flash->cbarg = cbarg;
	flash->type = type;
	flash->instance = instance;
	flash->residue = len;
	flash->offset = 0;
	flash->addr_off = offset;
	flash->ubuf = buf;

	bfa_flash_write_send(flash);
	return BFA_STATUS_OK;
}

/*
 * Read flash partition.
 *
 * @param[in] flash - flash structure
 * @param[in] type - flash partition type
 * @param[in] instance - flash partition instance
 * @param[in] buf - read data buffer
 * @param[in] len - data buffer length
 * @param[in] offset - offset relative to the partition starting address
 * @param[in] cbfn - callback function
 * @param[in] cbarg - callback argument
 *
 * Return status.
 */
bfa_status_t
bfa_flash_read_part(struct bfa_flash_s *flash, enum bfa_flash_part_type type,
		u8 instance, void *buf, u32 len, u32 offset,
		bfa_cb_flash_t cbfn, void *cbarg)
{
	bfa_trc(flash, BFI_FLASH_H2I_READ_REQ);
	bfa_trc(flash, type);
	bfa_trc(flash, instance);
	bfa_trc(flash, len);
	bfa_trc(flash, offset);

	if (!bfa_ioc_is_operational(flash->ioc))
		return BFA_STATUS_IOC_NON_OP;

	/*
	 * 'len' must be in word (4-byte) boundary
	 * 'offset' must be in sector (16kb) boundary
	 */
	if (!len || (len & 0x03) || (offset & 0x00003FFF))
		return BFA_STATUS_FLASH_BAD_LEN;

	if (flash->op_busy) {
		bfa_trc(flash, flash->op_busy);
		return BFA_STATUS_DEVBUSY;
	}

	flash->op_busy = 1;
	flash->cbfn = cbfn;
	flash->cbarg = cbarg;
	flash->type = type;
	flash->instance = instance;
	flash->residue = len;
	flash->offset = 0;
	flash->addr_off = offset;
	flash->ubuf = buf;
	bfa_flash_read_send(flash);

	return BFA_STATUS_OK;
}

/*
 *	DIAG module specific
 */

#define BFA_DIAG_MEMTEST_TOV	50000	/* memtest timeout in msec */
#define CT2_BFA_DIAG_MEMTEST_TOV	(9*30*1000)  /* 4.5 min */

/* IOC event handler */
static void
bfa_diag_notify(void *diag_arg, enum bfa_ioc_event_e event)
{
	struct bfa_diag_s *diag = diag_arg;

	bfa_trc(diag, event);
	bfa_trc(diag, diag->block);
	bfa_trc(diag, diag->fwping.lock);
	bfa_trc(diag, diag->tsensor.lock);

	switch (event) {
	case BFA_IOC_E_DISABLED:
	case BFA_IOC_E_FAILED:
		if (diag->fwping.lock) {
			diag->fwping.status = BFA_STATUS_IOC_FAILURE;
			diag->fwping.cbfn(diag->fwping.cbarg,
					diag->fwping.status);
			diag->fwping.lock = 0;
		}

		if (diag->tsensor.lock) {
			diag->tsensor.status = BFA_STATUS_IOC_FAILURE;
			diag->tsensor.cbfn(diag->tsensor.cbarg,
					   diag->tsensor.status);
			diag->tsensor.lock = 0;
		}

		if (diag->block) {
			if (diag->timer_active) {
				bfa_timer_stop(&diag->timer);
				diag->timer_active = 0;
			}

			diag->status = BFA_STATUS_IOC_FAILURE;
			diag->cbfn(diag->cbarg, diag->status);
			diag->block = 0;
		}
		break;

	default:
		break;
	}
}

static void
bfa_diag_memtest_done(void *cbarg)
{
	struct bfa_diag_s *diag = cbarg;
	struct bfa_ioc_s  *ioc = diag->ioc;
	struct bfa_diag_memtest_result *res = diag->result;
	u32	loff = BFI_BOOT_MEMTEST_RES_ADDR;
	u32	pgnum, pgoff, i;

	pgnum = PSS_SMEM_PGNUM(ioc->ioc_regs.smem_pg0, loff);
	pgoff = PSS_SMEM_PGOFF(loff);

	writel(pgnum, ioc->ioc_regs.host_page_num_fn);

	for (i = 0; i < (sizeof(struct bfa_diag_memtest_result) /
			 sizeof(u32)); i++) {
		/* read test result from smem */
		*((u32 *) res + i) =
			bfa_mem_read(ioc->ioc_regs.smem_page_start, loff);
		loff += sizeof(u32);
	}

	/* Reset IOC fwstates to BFI_IOC_UNINIT */
	bfa_ioc_reset_fwstate(ioc);

	res->status = swab32(res->status);
	bfa_trc(diag, res->status);

	if (res->status == BFI_BOOT_MEMTEST_RES_SIG)
		diag->status = BFA_STATUS_OK;
	else {
		diag->status = BFA_STATUS_MEMTEST_FAILED;
		res->addr = swab32(res->addr);
		res->exp = swab32(res->exp);
		res->act = swab32(res->act);
		res->err_status = swab32(res->err_status);
		res->err_status1 = swab32(res->err_status1);
		res->err_addr = swab32(res->err_addr);
		bfa_trc(diag, res->addr);
		bfa_trc(diag, res->exp);
		bfa_trc(diag, res->act);
		bfa_trc(diag, res->err_status);
		bfa_trc(diag, res->err_status1);
		bfa_trc(diag, res->err_addr);
	}
	diag->timer_active = 0;
	diag->cbfn(diag->cbarg, diag->status);
	diag->block = 0;
}

/*
 * Firmware ping
 */

/*
 * Perform DMA test directly
 */
static void
diag_fwping_send(struct bfa_diag_s *diag)
{
	struct bfi_diag_fwping_req_s *fwping_req;
	u32	i;

	bfa_trc(diag, diag->fwping.dbuf_pa);

	/* fill DMA area with pattern */
	for (i = 0; i < (BFI_DIAG_DMA_BUF_SZ >> 2); i++)
		*((u32 *)diag->fwping.dbuf_kva + i) = diag->fwping.data;

	/* Fill mbox msg */
	fwping_req = (struct bfi_diag_fwping_req_s *)diag->fwping.mbcmd.msg;

	/* Setup SG list */
	bfa_alen_set(&fwping_req->alen, BFI_DIAG_DMA_BUF_SZ,
			diag->fwping.dbuf_pa);
	/* Set up dma count */
	fwping_req->count = cpu_to_be32(diag->fwping.count);
	/* Set up data pattern */
	fwping_req->data = diag->fwping.data;

	/* build host command */
	bfi_h2i_set(fwping_req->mh, BFI_MC_DIAG, BFI_DIAG_H2I_FWPING,
		bfa_ioc_portid(diag->ioc));

	/* send mbox cmd */
	bfa_ioc_mbox_queue(diag->ioc, &diag->fwping.mbcmd);
}

static void
diag_fwping_comp(struct bfa_diag_s *diag,
		 struct bfi_diag_fwping_rsp_s *diag_rsp)
{
	u32	rsp_data = diag_rsp->data;
	u8	rsp_dma_status = diag_rsp->dma_status;

	bfa_trc(diag, rsp_data);
	bfa_trc(diag, rsp_dma_status);

	if (rsp_dma_status == BFA_STATUS_OK) {
		u32	i, pat;
		pat = (diag->fwping.count & 0x1) ? ~(diag->fwping.data) :
			diag->fwping.data;
		/* Check mbox data */
		if (diag->fwping.data != rsp_data) {
			bfa_trc(diag, rsp_data);
			diag->fwping.result->dmastatus =
					BFA_STATUS_DATACORRUPTED;
			diag->fwping.status = BFA_STATUS_DATACORRUPTED;
			diag->fwping.cbfn(diag->fwping.cbarg,
					diag->fwping.status);
			diag->fwping.lock = 0;
			return;
		}
		/* Check dma pattern */
		for (i = 0; i < (BFI_DIAG_DMA_BUF_SZ >> 2); i++) {
			if (*((u32 *)diag->fwping.dbuf_kva + i) != pat) {
				bfa_trc(diag, i);
				bfa_trc(diag, pat);
				bfa_trc(diag,
					*((u32 *)diag->fwping.dbuf_kva + i));
				diag->fwping.result->dmastatus =
						BFA_STATUS_DATACORRUPTED;
				diag->fwping.status = BFA_STATUS_DATACORRUPTED;
				diag->fwping.cbfn(diag->fwping.cbarg,
						diag->fwping.status);
				diag->fwping.lock = 0;
				return;
			}
		}
		diag->fwping.result->dmastatus = BFA_STATUS_OK;
		diag->fwping.status = BFA_STATUS_OK;
		diag->fwping.cbfn(diag->fwping.cbarg, diag->fwping.status);
		diag->fwping.lock = 0;
	} else {
		diag->fwping.status = BFA_STATUS_HDMA_FAILED;
		diag->fwping.cbfn(diag->fwping.cbarg, diag->fwping.status);
		diag->fwping.lock = 0;
	}
}

/*
 * Temperature Sensor
 */

static void
diag_tempsensor_send(struct bfa_diag_s *diag)
{
	struct bfi_diag_ts_req_s *msg;

	msg = (struct bfi_diag_ts_req_s *)diag->tsensor.mbcmd.msg;
	bfa_trc(diag, msg->temp);
	/* build host command */
	bfi_h2i_set(msg->mh, BFI_MC_DIAG, BFI_DIAG_H2I_TEMPSENSOR,
		bfa_ioc_portid(diag->ioc));
	/* send mbox cmd */
	bfa_ioc_mbox_queue(diag->ioc, &diag->tsensor.mbcmd);
}

static void
diag_tempsensor_comp(struct bfa_diag_s *diag, bfi_diag_ts_rsp_t *rsp)
{
	if (!diag->tsensor.lock) {
		/* receiving response after ioc failure */
		bfa_trc(diag, diag->tsensor.lock);
		return;
	}

	/*
	 * ASIC junction tempsensor is a reg read operation
	 * it will always return OK
	 */
	diag->tsensor.temp->temp = be16_to_cpu(rsp->temp);
	diag->tsensor.temp->ts_junc = rsp->ts_junc;
	diag->tsensor.temp->ts_brd = rsp->ts_brd;
	diag->tsensor.temp->status = BFA_STATUS_OK;

	if (rsp->ts_brd) {
		if (rsp->status == BFA_STATUS_OK) {
			diag->tsensor.temp->brd_temp =
				be16_to_cpu(rsp->brd_temp);
		} else {
			bfa_trc(diag, rsp->status);
			diag->tsensor.temp->brd_temp = 0;
			diag->tsensor.temp->status = BFA_STATUS_DEVBUSY;
		}
	}
	bfa_trc(diag, rsp->ts_junc);
	bfa_trc(diag, rsp->temp);
	bfa_trc(diag, rsp->ts_brd);
	bfa_trc(diag, rsp->brd_temp);
	diag->tsensor.cbfn(diag->tsensor.cbarg, diag->tsensor.status);
	diag->tsensor.lock = 0;
}

/*
 *	LED Test command
 */
static void
diag_ledtest_send(struct bfa_diag_s *diag, struct bfa_diag_ledtest_s *ledtest)
{
	struct bfi_diag_ledtest_req_s  *msg;

	msg = (struct bfi_diag_ledtest_req_s *)diag->ledtest.mbcmd.msg;
	/* build host command */
	bfi_h2i_set(msg->mh, BFI_MC_DIAG, BFI_DIAG_H2I_LEDTEST,
			bfa_ioc_portid(diag->ioc));

	/*
	 * convert the freq from N blinks per 10 sec to
	 * crossbow ontime value. We do it here because division is need
	 */
	if (ledtest->freq)
		ledtest->freq = 500 / ledtest->freq;

	if (ledtest->freq == 0)
		ledtest->freq = 1;

	bfa_trc(diag, ledtest->freq);
	/* mcpy(&ledtest_req->req, ledtest, sizeof(bfa_diag_ledtest_t)); */
	msg->cmd = (u8) ledtest->cmd;
	msg->color = (u8) ledtest->color;
	msg->portid = bfa_ioc_portid(diag->ioc);
	msg->led = ledtest->led;
	msg->freq = cpu_to_be16(ledtest->freq);

	/* send mbox cmd */
	bfa_ioc_mbox_queue(diag->ioc, &diag->ledtest.mbcmd);
}

static void
diag_ledtest_comp(struct bfa_diag_s *diag, struct bfi_diag_ledtest_rsp_s *msg)
{
	bfa_trc(diag, diag->ledtest.lock);
	diag->ledtest.lock = BFA_FALSE;
	/* no bfa_cb_queue is needed because driver is not waiting */
}

/*
 * Port beaconing
 */
static void
diag_portbeacon_send(struct bfa_diag_s *diag, bfa_boolean_t beacon, u32 sec)
{
	struct bfi_diag_portbeacon_req_s *msg;

	msg = (struct bfi_diag_portbeacon_req_s *)diag->beacon.mbcmd.msg;
	/* build host command */
	bfi_h2i_set(msg->mh, BFI_MC_DIAG, BFI_DIAG_H2I_PORTBEACON,
		bfa_ioc_portid(diag->ioc));
	msg->beacon = beacon;
	msg->period = cpu_to_be32(sec);
	/* send mbox cmd */
	bfa_ioc_mbox_queue(diag->ioc, &diag->beacon.mbcmd);
}

static void
diag_portbeacon_comp(struct bfa_diag_s *diag)
{
	bfa_trc(diag, diag->beacon.state);
	diag->beacon.state = BFA_FALSE;
	if (diag->cbfn_beacon)
		diag->cbfn_beacon(diag->dev, BFA_FALSE, diag->beacon.link_e2e);
}

/*
 *	Diag hmbox handler
 */
void
bfa_diag_intr(void *diagarg, struct bfi_mbmsg_s *msg)
{
	struct bfa_diag_s *diag = diagarg;

	switch (msg->mh.msg_id) {
	case BFI_DIAG_I2H_PORTBEACON:
		diag_portbeacon_comp(diag);
		break;
	case BFI_DIAG_I2H_FWPING:
		diag_fwping_comp(diag, (struct bfi_diag_fwping_rsp_s *) msg);
		break;
	case BFI_DIAG_I2H_TEMPSENSOR:
		diag_tempsensor_comp(diag, (bfi_diag_ts_rsp_t *) msg);
		break;
	case BFI_DIAG_I2H_LEDTEST:
		diag_ledtest_comp(diag, (struct bfi_diag_ledtest_rsp_s *) msg);
		break;
	default:
		bfa_trc(diag, msg->mh.msg_id);
		WARN_ON(1);
	}
}

/*
 * Gen RAM Test
 *
 *   @param[in] *diag           - diag data struct
 *   @param[in] *memtest        - mem test params input from upper layer,
 *   @param[in] pattern         - mem test pattern
 *   @param[in] *result         - mem test result
 *   @param[in] cbfn            - mem test callback functioin
 *   @param[in] cbarg           - callback functioin arg
 *
 *   @param[out]
 */
bfa_status_t
bfa_diag_memtest(struct bfa_diag_s *diag, struct bfa_diag_memtest_s *memtest,
		u32 pattern, struct bfa_diag_memtest_result *result,
		bfa_cb_diag_t cbfn, void *cbarg)
{
	u32	memtest_tov;

	bfa_trc(diag, pattern);

	if (!bfa_ioc_adapter_is_disabled(diag->ioc))
		return BFA_STATUS_ADAPTER_ENABLED;

	/* check to see if there is another destructive diag cmd running */
	if (diag->block) {
		bfa_trc(diag, diag->block);
		return BFA_STATUS_DEVBUSY;
	} else
		diag->block = 1;

	diag->result = result;
	diag->cbfn = cbfn;
	diag->cbarg = cbarg;

	/* download memtest code and take LPU0 out of reset */
	bfa_ioc_boot(diag->ioc, BFI_FWBOOT_TYPE_MEMTEST, BFI_FWBOOT_ENV_OS);

	memtest_tov = (bfa_ioc_asic_gen(diag->ioc) == BFI_ASIC_GEN_CT2) ?
		       CT2_BFA_DIAG_MEMTEST_TOV : BFA_DIAG_MEMTEST_TOV;
	bfa_timer_begin(diag->ioc->timer_mod, &diag->timer,
			bfa_diag_memtest_done, diag, memtest_tov);
	diag->timer_active = 1;
	return BFA_STATUS_OK;
}

/*
 * DIAG firmware ping command
 *
 *   @param[in] *diag           - diag data struct
 *   @param[in] cnt             - dma loop count for testing PCIE
 *   @param[in] data            - data pattern to pass in fw
 *   @param[in] *result         - pt to bfa_diag_fwping_result_t data struct
 *   @param[in] cbfn            - callback function
 *   @param[in] *cbarg          - callback functioin arg
 *
 *   @param[out]
 */
bfa_status_t
bfa_diag_fwping(struct bfa_diag_s *diag, u32 cnt, u32 data,
		struct bfa_diag_results_fwping *result, bfa_cb_diag_t cbfn,
		void *cbarg)
{
	bfa_trc(diag, cnt);
	bfa_trc(diag, data);

	if (!bfa_ioc_is_operational(diag->ioc))
		return BFA_STATUS_IOC_NON_OP;

	if (bfa_asic_id_ct2(bfa_ioc_devid((diag->ioc))) &&
	    ((diag->ioc)->clscode == BFI_PCIFN_CLASS_ETH))
		return BFA_STATUS_CMD_NOTSUPP;

	/* check to see if there is another destructive diag cmd running */
	if (diag->block || diag->fwping.lock) {
		bfa_trc(diag, diag->block);
		bfa_trc(diag, diag->fwping.lock);
		return BFA_STATUS_DEVBUSY;
	}

	/* Initialization */
	diag->fwping.lock = 1;
	diag->fwping.cbfn = cbfn;
	diag->fwping.cbarg = cbarg;
	diag->fwping.result = result;
	diag->fwping.data = data;
	diag->fwping.count = cnt;

	/* Init test results */
	diag->fwping.result->data = 0;
	diag->fwping.result->status = BFA_STATUS_OK;

	/* kick off the first ping */
	diag_fwping_send(diag);
	return BFA_STATUS_OK;
}

/*
 * Read Temperature Sensor
 *
 *   @param[in] *diag           - diag data struct
 *   @param[in] *result         - pt to bfa_diag_temp_t data struct
 *   @param[in] cbfn            - callback function
 *   @param[in] *cbarg          - callback functioin arg
 *
 *   @param[out]
 */
bfa_status_t
bfa_diag_tsensor_query(struct bfa_diag_s *diag,
		struct bfa_diag_results_tempsensor_s *result,
		bfa_cb_diag_t cbfn, void *cbarg)
{
	/* check to see if there is a destructive diag cmd running */
	if (diag->block || diag->tsensor.lock) {
		bfa_trc(diag, diag->block);
		bfa_trc(diag, diag->tsensor.lock);
		return BFA_STATUS_DEVBUSY;
	}

	if (!bfa_ioc_is_operational(diag->ioc))
		return BFA_STATUS_IOC_NON_OP;

	/* Init diag mod params */
	diag->tsensor.lock = 1;
	diag->tsensor.temp = result;
	diag->tsensor.cbfn = cbfn;
	diag->tsensor.cbarg = cbarg;

	/* Send msg to fw */
	diag_tempsensor_send(diag);

	return BFA_STATUS_OK;
}

/*
 * LED Test command
 *
 *   @param[in] *diag           - diag data struct
 *   @param[in] *ledtest        - pt to ledtest data structure
 *
 *   @param[out]
 */
bfa_status_t
bfa_diag_ledtest(struct bfa_diag_s *diag, struct bfa_diag_ledtest_s *ledtest)
{
	bfa_trc(diag, ledtest->cmd);

	if (!bfa_ioc_is_operational(diag->ioc))
		return BFA_STATUS_IOC_NON_OP;

	if (diag->beacon.state)
		return BFA_STATUS_BEACON_ON;

	if (diag->ledtest.lock)
		return BFA_STATUS_LEDTEST_OP;

	/* Send msg to fw */
	diag->ledtest.lock = BFA_TRUE;
	diag_ledtest_send(diag, ledtest);

	return BFA_STATUS_OK;
}

/*
 * Port beaconing command
 *
 *   @param[in] *diag           - diag data struct
 *   @param[in] beacon          - port beaconing 1:ON   0:OFF
 *   @param[in] link_e2e_beacon - link beaconing 1:ON   0:OFF
 *   @param[in] sec             - beaconing duration in seconds
 *
 *   @param[out]
 */
bfa_status_t
bfa_diag_beacon_port(struct bfa_diag_s *diag, bfa_boolean_t beacon,
		bfa_boolean_t link_e2e_beacon, uint32_t sec)
{
	bfa_trc(diag, beacon);
	bfa_trc(diag, link_e2e_beacon);
	bfa_trc(diag, sec);

	if (!bfa_ioc_is_operational(diag->ioc))
		return BFA_STATUS_IOC_NON_OP;

	if (diag->ledtest.lock)
		return BFA_STATUS_LEDTEST_OP;

	if (diag->beacon.state && beacon)       /* beacon alread on */
		return BFA_STATUS_BEACON_ON;

	diag->beacon.state	= beacon;
	diag->beacon.link_e2e	= link_e2e_beacon;
	if (diag->cbfn_beacon)
		diag->cbfn_beacon(diag->dev, beacon, link_e2e_beacon);

	/* Send msg to fw */
	diag_portbeacon_send(diag, beacon, sec);

	return BFA_STATUS_OK;
}

/*
 * Return DMA memory needed by diag module.
 */
u32
bfa_diag_meminfo(void)
{
	return BFA_ROUNDUP(BFI_DIAG_DMA_BUF_SZ, BFA_DMA_ALIGN_SZ);
}

/*
 *	Attach virtual and physical memory for Diag.
 */
void
bfa_diag_attach(struct bfa_diag_s *diag, struct bfa_ioc_s *ioc, void *dev,
	bfa_cb_diag_beacon_t cbfn_beacon, struct bfa_trc_mod_s *trcmod)
{
	diag->dev = dev;
	diag->ioc = ioc;
	diag->trcmod = trcmod;

	diag->block = 0;
	diag->cbfn = NULL;
	diag->cbarg = NULL;
	diag->result = NULL;
	diag->cbfn_beacon = cbfn_beacon;

	bfa_ioc_mbox_regisr(diag->ioc, BFI_MC_DIAG, bfa_diag_intr, diag);
	bfa_q_qe_init(&diag->ioc_notify);
	bfa_ioc_notify_init(&diag->ioc_notify, bfa_diag_notify, diag);
	list_add_tail(&diag->ioc_notify.qe, &diag->ioc->notify_q);
}

void
bfa_diag_memclaim(struct bfa_diag_s *diag, u8 *dm_kva, u64 dm_pa)
{
	diag->fwping.dbuf_kva = dm_kva;
	diag->fwping.dbuf_pa = dm_pa;
	memset(diag->fwping.dbuf_kva, 0, BFI_DIAG_DMA_BUF_SZ);
}

/*
 *	PHY module specific
 */
#define BFA_PHY_DMA_BUF_SZ	0x02000         /* 8k dma buffer */
#define BFA_PHY_LOCK_STATUS	0x018878        /* phy semaphore status reg */

static void
bfa_phy_ntoh32(u32 *obuf, u32 *ibuf, int sz)
{
	int i, m = sz >> 2;

	for (i = 0; i < m; i++)
		obuf[i] = be32_to_cpu(ibuf[i]);
}

static bfa_boolean_t
bfa_phy_present(struct bfa_phy_s *phy)
{
	return (phy->ioc->attr->card_type == BFA_MFG_TYPE_LIGHTNING);
}

static void
bfa_phy_notify(void *cbarg, enum bfa_ioc_event_e event)
{
	struct bfa_phy_s *phy = cbarg;

	bfa_trc(phy, event);

	switch (event) {
	case BFA_IOC_E_DISABLED:
	case BFA_IOC_E_FAILED:
		if (phy->op_busy) {
			phy->status = BFA_STATUS_IOC_FAILURE;
			phy->cbfn(phy->cbarg, phy->status);
			phy->op_busy = 0;
		}
		break;

	default:
		break;
	}
}

/*
 * Send phy attribute query request.
 *
 * @param[in] cbarg - callback argument
 */
static void
bfa_phy_query_send(void *cbarg)
{
	struct bfa_phy_s *phy = cbarg;
	struct bfi_phy_query_req_s *msg =
			(struct bfi_phy_query_req_s *) phy->mb.msg;

	msg->instance = phy->instance;
	bfi_h2i_set(msg->mh, BFI_MC_PHY, BFI_PHY_H2I_QUERY_REQ,
		bfa_ioc_portid(phy->ioc));
	bfa_alen_set(&msg->alen, sizeof(struct bfa_phy_attr_s), phy->dbuf_pa);
	bfa_ioc_mbox_queue(phy->ioc, &phy->mb);
}

/*
 * Send phy write request.
 *
 * @param[in] cbarg - callback argument
 */
static void
bfa_phy_write_send(void *cbarg)
{
	struct bfa_phy_s *phy = cbarg;
	struct bfi_phy_write_req_s *msg =
			(struct bfi_phy_write_req_s *) phy->mb.msg;
	u32	len;
	u16	*buf, *dbuf;
	int	i, sz;

	msg->instance = phy->instance;
	msg->offset = cpu_to_be32(phy->addr_off + phy->offset);
	len = (phy->residue < BFA_PHY_DMA_BUF_SZ) ?
			phy->residue : BFA_PHY_DMA_BUF_SZ;
	msg->length = cpu_to_be32(len);

	/* indicate if it's the last msg of the whole write operation */
	msg->last = (len == phy->residue) ? 1 : 0;

	bfi_h2i_set(msg->mh, BFI_MC_PHY, BFI_PHY_H2I_WRITE_REQ,
		bfa_ioc_portid(phy->ioc));
	bfa_alen_set(&msg->alen, len, phy->dbuf_pa);

	buf = (u16 *) (phy->ubuf + phy->offset);
	dbuf = (u16 *)phy->dbuf_kva;
	sz = len >> 1;
	for (i = 0; i < sz; i++)
		buf[i] = cpu_to_be16(dbuf[i]);

	bfa_ioc_mbox_queue(phy->ioc, &phy->mb);

	phy->residue -= len;
	phy->offset += len;
}

/*
 * Send phy read request.
 *
 * @param[in] cbarg - callback argument
 */
static void
bfa_phy_read_send(void *cbarg)
{
	struct bfa_phy_s *phy = cbarg;
	struct bfi_phy_read_req_s *msg =
			(struct bfi_phy_read_req_s *) phy->mb.msg;
	u32	len;

	msg->instance = phy->instance;
	msg->offset = cpu_to_be32(phy->addr_off + phy->offset);
	len = (phy->residue < BFA_PHY_DMA_BUF_SZ) ?
			phy->residue : BFA_PHY_DMA_BUF_SZ;
	msg->length = cpu_to_be32(len);
	bfi_h2i_set(msg->mh, BFI_MC_PHY, BFI_PHY_H2I_READ_REQ,
		bfa_ioc_portid(phy->ioc));
	bfa_alen_set(&msg->alen, len, phy->dbuf_pa);
	bfa_ioc_mbox_queue(phy->ioc, &phy->mb);
}

/*
 * Send phy stats request.
 *
 * @param[in] cbarg - callback argument
 */
static void
bfa_phy_stats_send(void *cbarg)
{
	struct bfa_phy_s *phy = cbarg;
	struct bfi_phy_stats_req_s *msg =
			(struct bfi_phy_stats_req_s *) phy->mb.msg;

	msg->instance = phy->instance;
	bfi_h2i_set(msg->mh, BFI_MC_PHY, BFI_PHY_H2I_STATS_REQ,
		bfa_ioc_portid(phy->ioc));
	bfa_alen_set(&msg->alen, sizeof(struct bfa_phy_stats_s), phy->dbuf_pa);
	bfa_ioc_mbox_queue(phy->ioc, &phy->mb);
}

/*
 * Flash memory info API.
 *
 * @param[in] mincfg - minimal cfg variable
 */
u32
bfa_phy_meminfo(bfa_boolean_t mincfg)
{
	/* min driver doesn't need phy */
	if (mincfg)
		return 0;

	return BFA_ROUNDUP(BFA_PHY_DMA_BUF_SZ, BFA_DMA_ALIGN_SZ);
}

/*
 * Flash attach API.
 *
 * @param[in] phy - phy structure
 * @param[in] ioc  - ioc structure
 * @param[in] dev  - device structure
 * @param[in] trcmod - trace module
 * @param[in] logmod - log module
 */
void
bfa_phy_attach(struct bfa_phy_s *phy, struct bfa_ioc_s *ioc, void *dev,
		struct bfa_trc_mod_s *trcmod, bfa_boolean_t mincfg)
{
	phy->ioc = ioc;
	phy->trcmod = trcmod;
	phy->cbfn = NULL;
	phy->cbarg = NULL;
	phy->op_busy = 0;

	bfa_ioc_mbox_regisr(phy->ioc, BFI_MC_PHY, bfa_phy_intr, phy);
	bfa_q_qe_init(&phy->ioc_notify);
	bfa_ioc_notify_init(&phy->ioc_notify, bfa_phy_notify, phy);
	list_add_tail(&phy->ioc_notify.qe, &phy->ioc->notify_q);

	/* min driver doesn't need phy */
	if (mincfg) {
		phy->dbuf_kva = NULL;
		phy->dbuf_pa = 0;
	}
}

/*
 * Claim memory for phy
 *
 * @param[in] phy - phy structure
 * @param[in] dm_kva - pointer to virtual memory address
 * @param[in] dm_pa - physical memory address
 * @param[in] mincfg - minimal cfg variable
 */
void
bfa_phy_memclaim(struct bfa_phy_s *phy, u8 *dm_kva, u64 dm_pa,
		bfa_boolean_t mincfg)
{
	if (mincfg)
		return;

	phy->dbuf_kva = dm_kva;
	phy->dbuf_pa = dm_pa;
	memset(phy->dbuf_kva, 0, BFA_PHY_DMA_BUF_SZ);
	dm_kva += BFA_ROUNDUP(BFA_PHY_DMA_BUF_SZ, BFA_DMA_ALIGN_SZ);
	dm_pa += BFA_ROUNDUP(BFA_PHY_DMA_BUF_SZ, BFA_DMA_ALIGN_SZ);
}

bfa_boolean_t
bfa_phy_busy(struct bfa_ioc_s *ioc)
{
	void __iomem	*rb;

	rb = bfa_ioc_bar0(ioc);
	return readl(rb + BFA_PHY_LOCK_STATUS);
}

/*
 * Get phy attribute.
 *
 * @param[in] phy - phy structure
 * @param[in] attr - phy attribute structure
 * @param[in] cbfn - callback function
 * @param[in] cbarg - callback argument
 *
 * Return status.
 */
bfa_status_t
bfa_phy_get_attr(struct bfa_phy_s *phy, u8 instance,
		struct bfa_phy_attr_s *attr, bfa_cb_phy_t cbfn, void *cbarg)
{
	bfa_trc(phy, BFI_PHY_H2I_QUERY_REQ);
	bfa_trc(phy, instance);

	if (!bfa_phy_present(phy))
		return BFA_STATUS_PHY_NOT_PRESENT;

	if (!bfa_ioc_is_operational(phy->ioc))
		return BFA_STATUS_IOC_NON_OP;

	if (phy->op_busy || bfa_phy_busy(phy->ioc)) {
		bfa_trc(phy, phy->op_busy);
		return BFA_STATUS_DEVBUSY;
	}

	phy->op_busy = 1;
	phy->cbfn = cbfn;
	phy->cbarg = cbarg;
	phy->instance = instance;
	phy->ubuf = (uint8_t *) attr;
	bfa_phy_query_send(phy);

	return BFA_STATUS_OK;
}

/*
 * Get phy stats.
 *
 * @param[in] phy - phy structure
 * @param[in] instance - phy image instance
 * @param[in] stats - pointer to phy stats
 * @param[in] cbfn - callback function
 * @param[in] cbarg - callback argument
 *
 * Return status.
 */
bfa_status_t
bfa_phy_get_stats(struct bfa_phy_s *phy, u8 instance,
		struct bfa_phy_stats_s *stats,
		bfa_cb_phy_t cbfn, void *cbarg)
{
	bfa_trc(phy, BFI_PHY_H2I_STATS_REQ);
	bfa_trc(phy, instance);

	if (!bfa_phy_present(phy))
		return BFA_STATUS_PHY_NOT_PRESENT;

	if (!bfa_ioc_is_operational(phy->ioc))
		return BFA_STATUS_IOC_NON_OP;

	if (phy->op_busy || bfa_phy_busy(phy->ioc)) {
		bfa_trc(phy, phy->op_busy);
		return BFA_STATUS_DEVBUSY;
	}

	phy->op_busy = 1;
	phy->cbfn = cbfn;
	phy->cbarg = cbarg;
	phy->instance = instance;
	phy->ubuf = (u8 *) stats;
	bfa_phy_stats_send(phy);

	return BFA_STATUS_OK;
}

/*
 * Update phy image.
 *
 * @param[in] phy - phy structure
 * @param[in] instance - phy image instance
 * @param[in] buf - update data buffer
 * @param[in] len - data buffer length
 * @param[in] offset - offset relative to starting address
 * @param[in] cbfn - callback function
 * @param[in] cbarg - callback argument
 *
 * Return status.
 */
bfa_status_t
bfa_phy_update(struct bfa_phy_s *phy, u8 instance,
		void *buf, u32 len, u32 offset,
		bfa_cb_phy_t cbfn, void *cbarg)
{
	bfa_trc(phy, BFI_PHY_H2I_WRITE_REQ);
	bfa_trc(phy, instance);
	bfa_trc(phy, len);
	bfa_trc(phy, offset);

	if (!bfa_phy_present(phy))
		return BFA_STATUS_PHY_NOT_PRESENT;

	if (!bfa_ioc_is_operational(phy->ioc))
		return BFA_STATUS_IOC_NON_OP;

	/* 'len' must be in word (4-byte) boundary */
	if (!len || (len & 0x03))
		return BFA_STATUS_FAILED;

	if (phy->op_busy || bfa_phy_busy(phy->ioc)) {
		bfa_trc(phy, phy->op_busy);
		return BFA_STATUS_DEVBUSY;
	}

	phy->op_busy = 1;
	phy->cbfn = cbfn;
	phy->cbarg = cbarg;
	phy->instance = instance;
	phy->residue = len;
	phy->offset = 0;
	phy->addr_off = offset;
	phy->ubuf = buf;

	bfa_phy_write_send(phy);
	return BFA_STATUS_OK;
}

/*
 * Read phy image.
 *
 * @param[in] phy - phy structure
 * @param[in] instance - phy image instance
 * @param[in] buf - read data buffer
 * @param[in] len - data buffer length
 * @param[in] offset - offset relative to starting address
 * @param[in] cbfn - callback function
 * @param[in] cbarg - callback argument
 *
 * Return status.
 */
bfa_status_t
bfa_phy_read(struct bfa_phy_s *phy, u8 instance,
		void *buf, u32 len, u32 offset,
		bfa_cb_phy_t cbfn, void *cbarg)
{
	bfa_trc(phy, BFI_PHY_H2I_READ_REQ);
	bfa_trc(phy, instance);
	bfa_trc(phy, len);
	bfa_trc(phy, offset);

	if (!bfa_phy_present(phy))
		return BFA_STATUS_PHY_NOT_PRESENT;

	if (!bfa_ioc_is_operational(phy->ioc))
		return BFA_STATUS_IOC_NON_OP;

	/* 'len' must be in word (4-byte) boundary */
	if (!len || (len & 0x03))
		return BFA_STATUS_FAILED;

	if (phy->op_busy || bfa_phy_busy(phy->ioc)) {
		bfa_trc(phy, phy->op_busy);
		return BFA_STATUS_DEVBUSY;
	}

	phy->op_busy = 1;
	phy->cbfn = cbfn;
	phy->cbarg = cbarg;
	phy->instance = instance;
	phy->residue = len;
	phy->offset = 0;
	phy->addr_off = offset;
	phy->ubuf = buf;
	bfa_phy_read_send(phy);

	return BFA_STATUS_OK;
}

/*
 * Process phy response messages upon receiving interrupts.
 *
 * @param[in] phyarg - phy structure
 * @param[in] msg - message structure
 */
void
bfa_phy_intr(void *phyarg, struct bfi_mbmsg_s *msg)
{
	struct bfa_phy_s *phy = phyarg;
	u32	status;

	union {
		struct bfi_phy_query_rsp_s *query;
		struct bfi_phy_stats_rsp_s *stats;
		struct bfi_phy_write_rsp_s *write;
		struct bfi_phy_read_rsp_s *read;
		struct bfi_mbmsg_s   *msg;
	} m;

	m.msg = msg;
	bfa_trc(phy, msg->mh.msg_id);

	if (!phy->op_busy) {
		/* receiving response after ioc failure */
		bfa_trc(phy, 0x9999);
		return;
	}

	switch (msg->mh.msg_id) {
	case BFI_PHY_I2H_QUERY_RSP:
		status = be32_to_cpu(m.query->status);
		bfa_trc(phy, status);

		if (status == BFA_STATUS_OK) {
			struct bfa_phy_attr_s *attr =
				(struct bfa_phy_attr_s *) phy->ubuf;
			bfa_phy_ntoh32((u32 *)attr, (u32 *)phy->dbuf_kva,
					sizeof(struct bfa_phy_attr_s));
			bfa_trc(phy, attr->status);
			bfa_trc(phy, attr->length);
		}

		phy->status = status;
		phy->op_busy = 0;
		if (phy->cbfn)
			phy->cbfn(phy->cbarg, phy->status);
		break;
	case BFI_PHY_I2H_STATS_RSP:
		status = be32_to_cpu(m.stats->status);
		bfa_trc(phy, status);

		if (status == BFA_STATUS_OK) {
			struct bfa_phy_stats_s *stats =
				(struct bfa_phy_stats_s *) phy->ubuf;
			bfa_phy_ntoh32((u32 *)stats, (u32 *)phy->dbuf_kva,
				sizeof(struct bfa_phy_stats_s));
				bfa_trc(phy, stats->status);
		}

		phy->status = status;
		phy->op_busy = 0;
		if (phy->cbfn)
			phy->cbfn(phy->cbarg, phy->status);
		break;
	case BFI_PHY_I2H_WRITE_RSP:
		status = be32_to_cpu(m.write->status);
		bfa_trc(phy, status);

		if (status != BFA_STATUS_OK || phy->residue == 0) {
			phy->status = status;
			phy->op_busy = 0;
			if (phy->cbfn)
				phy->cbfn(phy->cbarg, phy->status);
		} else {
			bfa_trc(phy, phy->offset);
			bfa_phy_write_send(phy);
		}
		break;
	case BFI_PHY_I2H_READ_RSP:
		status = be32_to_cpu(m.read->status);
		bfa_trc(phy, status);

		if (status != BFA_STATUS_OK) {
			phy->status = status;
			phy->op_busy = 0;
			if (phy->cbfn)
				phy->cbfn(phy->cbarg, phy->status);
		} else {
			u32 len = be32_to_cpu(m.read->length);
			u16 *buf = (u16 *)(phy->ubuf + phy->offset);
			u16 *dbuf = (u16 *)phy->dbuf_kva;
			int i, sz = len >> 1;

			bfa_trc(phy, phy->offset);
			bfa_trc(phy, len);

			for (i = 0; i < sz; i++)
				buf[i] = be16_to_cpu(dbuf[i]);

			phy->residue -= len;
			phy->offset += len;

			if (phy->residue == 0) {
				phy->status = status;
				phy->op_busy = 0;
				if (phy->cbfn)
					phy->cbfn(phy->cbarg, phy->status);
			} else
				bfa_phy_read_send(phy);
		}
		break;
	default:
		WARN_ON(1);
	}
}

/*
 *	DCONF module specific
 */

BFA_MODULE(dconf);

/*
 * DCONF state machine events
 */
enum bfa_dconf_event {
	BFA_DCONF_SM_INIT		= 1,	/* dconf Init */
	BFA_DCONF_SM_FLASH_COMP		= 2,	/* read/write to flash */
	BFA_DCONF_SM_WR			= 3,	/* binding change, map */
	BFA_DCONF_SM_TIMEOUT		= 4,	/* Start timer */
	BFA_DCONF_SM_EXIT		= 5,	/* exit dconf module */
	BFA_DCONF_SM_IOCDISABLE		= 6,	/* IOC disable event */
};

/* forward declaration of DCONF state machine */
static void bfa_dconf_sm_uninit(struct bfa_dconf_mod_s *dconf,
				enum bfa_dconf_event event);
static void bfa_dconf_sm_flash_read(struct bfa_dconf_mod_s *dconf,
				enum bfa_dconf_event event);
static void bfa_dconf_sm_ready(struct bfa_dconf_mod_s *dconf,
				enum bfa_dconf_event event);
static void bfa_dconf_sm_dirty(struct bfa_dconf_mod_s *dconf,
				enum bfa_dconf_event event);
static void bfa_dconf_sm_sync(struct bfa_dconf_mod_s *dconf,
				enum bfa_dconf_event event);
static void bfa_dconf_sm_final_sync(struct bfa_dconf_mod_s *dconf,
				enum bfa_dconf_event event);
static void bfa_dconf_sm_iocdown_dirty(struct bfa_dconf_mod_s *dconf,
				enum bfa_dconf_event event);

static void bfa_dconf_cbfn(void *dconf, bfa_status_t status);
static void bfa_dconf_timer(void *cbarg);
static bfa_status_t bfa_dconf_flash_write(struct bfa_dconf_mod_s *dconf);
static void bfa_dconf_init_cb(void *arg, bfa_status_t status);

/*
 * Beginning state of dconf module. Waiting for an event to start.
 */
static void
bfa_dconf_sm_uninit(struct bfa_dconf_mod_s *dconf, enum bfa_dconf_event event)
{
	bfa_status_t bfa_status;
	bfa_trc(dconf->bfa, event);

	switch (event) {
	case BFA_DCONF_SM_INIT:
		if (dconf->min_cfg) {
			bfa_trc(dconf->bfa, dconf->min_cfg);
			bfa_fsm_send_event(&dconf->bfa->iocfc,
					IOCFC_E_DCONF_DONE);
			return;
		}
		bfa_sm_set_state(dconf, bfa_dconf_sm_flash_read);
		bfa_timer_start(dconf->bfa, &dconf->timer,
			bfa_dconf_timer, dconf, BFA_DCONF_UPDATE_TOV);
		bfa_status = bfa_flash_read_part(BFA_FLASH(dconf->bfa),
					BFA_FLASH_PART_DRV, dconf->instance,
					dconf->dconf,
					sizeof(struct bfa_dconf_s), 0,
					bfa_dconf_init_cb, dconf->bfa);
		if (bfa_status != BFA_STATUS_OK) {
			bfa_timer_stop(&dconf->timer);
			bfa_dconf_init_cb(dconf->bfa, BFA_STATUS_FAILED);
			bfa_sm_set_state(dconf, bfa_dconf_sm_uninit);
			return;
		}
		break;
	case BFA_DCONF_SM_EXIT:
		bfa_fsm_send_event(&dconf->bfa->iocfc, IOCFC_E_DCONF_DONE);
	case BFA_DCONF_SM_IOCDISABLE:
	case BFA_DCONF_SM_WR:
	case BFA_DCONF_SM_FLASH_COMP:
		break;
	default:
		bfa_sm_fault(dconf->bfa, event);
	}
}

/*
 * Read flash for dconf entries and make a call back to the driver once done.
 */
static void
bfa_dconf_sm_flash_read(struct bfa_dconf_mod_s *dconf,
			enum bfa_dconf_event event)
{
	bfa_trc(dconf->bfa, event);

	switch (event) {
	case BFA_DCONF_SM_FLASH_COMP:
		bfa_timer_stop(&dconf->timer);
		bfa_sm_set_state(dconf, bfa_dconf_sm_ready);
		break;
	case BFA_DCONF_SM_TIMEOUT:
		bfa_sm_set_state(dconf, bfa_dconf_sm_ready);
		bfa_fsm_send_event(&dconf->bfa->iocfc, IOCFC_E_IOC_FAILED);
		break;
	case BFA_DCONF_SM_EXIT:
		bfa_timer_stop(&dconf->timer);
		bfa_sm_set_state(dconf, bfa_dconf_sm_uninit);
		bfa_fsm_send_event(&dconf->bfa->iocfc, IOCFC_E_DCONF_DONE);
		break;
	case BFA_DCONF_SM_IOCDISABLE:
		bfa_timer_stop(&dconf->timer);
		bfa_sm_set_state(dconf, bfa_dconf_sm_uninit);
		break;
	default:
		bfa_sm_fault(dconf->bfa, event);
	}
}

/*
 * DCONF Module is in ready state. Has completed the initialization.
 */
static void
bfa_dconf_sm_ready(struct bfa_dconf_mod_s *dconf, enum bfa_dconf_event event)
{
	bfa_trc(dconf->bfa, event);

	switch (event) {
	case BFA_DCONF_SM_WR:
		bfa_timer_start(dconf->bfa, &dconf->timer,
			bfa_dconf_timer, dconf, BFA_DCONF_UPDATE_TOV);
		bfa_sm_set_state(dconf, bfa_dconf_sm_dirty);
		break;
	case BFA_DCONF_SM_EXIT:
		bfa_sm_set_state(dconf, bfa_dconf_sm_uninit);
		bfa_fsm_send_event(&dconf->bfa->iocfc, IOCFC_E_DCONF_DONE);
		break;
	case BFA_DCONF_SM_INIT:
	case BFA_DCONF_SM_IOCDISABLE:
		break;
	default:
		bfa_sm_fault(dconf->bfa, event);
	}
}

/*
 * entries are dirty, write back to the flash.
 */

static void
bfa_dconf_sm_dirty(struct bfa_dconf_mod_s *dconf, enum bfa_dconf_event event)
{
	bfa_trc(dconf->bfa, event);

	switch (event) {
	case BFA_DCONF_SM_TIMEOUT:
		bfa_sm_set_state(dconf, bfa_dconf_sm_sync);
		bfa_dconf_flash_write(dconf);
		break;
	case BFA_DCONF_SM_WR:
		bfa_timer_stop(&dconf->timer);
		bfa_timer_start(dconf->bfa, &dconf->timer,
			bfa_dconf_timer, dconf, BFA_DCONF_UPDATE_TOV);
		break;
	case BFA_DCONF_SM_EXIT:
		bfa_timer_stop(&dconf->timer);
		bfa_timer_start(dconf->bfa, &dconf->timer,
			bfa_dconf_timer, dconf, BFA_DCONF_UPDATE_TOV);
		bfa_sm_set_state(dconf, bfa_dconf_sm_final_sync);
		bfa_dconf_flash_write(dconf);
		break;
	case BFA_DCONF_SM_FLASH_COMP:
		break;
	case BFA_DCONF_SM_IOCDISABLE:
		bfa_timer_stop(&dconf->timer);
		bfa_sm_set_state(dconf, bfa_dconf_sm_iocdown_dirty);
		break;
	default:
		bfa_sm_fault(dconf->bfa, event);
	}
}

/*
 * Sync the dconf entries to the flash.
 */
static void
bfa_dconf_sm_final_sync(struct bfa_dconf_mod_s *dconf,
			enum bfa_dconf_event event)
{
	bfa_trc(dconf->bfa, event);

	switch (event) {
	case BFA_DCONF_SM_IOCDISABLE:
	case BFA_DCONF_SM_FLASH_COMP:
		bfa_timer_stop(&dconf->timer);
	case BFA_DCONF_SM_TIMEOUT:
		bfa_sm_set_state(dconf, bfa_dconf_sm_uninit);
		bfa_fsm_send_event(&dconf->bfa->iocfc, IOCFC_E_DCONF_DONE);
		break;
	default:
		bfa_sm_fault(dconf->bfa, event);
	}
}

static void
bfa_dconf_sm_sync(struct bfa_dconf_mod_s *dconf, enum bfa_dconf_event event)
{
	bfa_trc(dconf->bfa, event);

	switch (event) {
	case BFA_DCONF_SM_FLASH_COMP:
		bfa_sm_set_state(dconf, bfa_dconf_sm_ready);
		break;
	case BFA_DCONF_SM_WR:
		bfa_timer_start(dconf->bfa, &dconf->timer,
			bfa_dconf_timer, dconf, BFA_DCONF_UPDATE_TOV);
		bfa_sm_set_state(dconf, bfa_dconf_sm_dirty);
		break;
	case BFA_DCONF_SM_EXIT:
		bfa_timer_start(dconf->bfa, &dconf->timer,
			bfa_dconf_timer, dconf, BFA_DCONF_UPDATE_TOV);
		bfa_sm_set_state(dconf, bfa_dconf_sm_final_sync);
		break;
	case BFA_DCONF_SM_IOCDISABLE:
		bfa_sm_set_state(dconf, bfa_dconf_sm_iocdown_dirty);
		break;
	default:
		bfa_sm_fault(dconf->bfa, event);
	}
}

static void
bfa_dconf_sm_iocdown_dirty(struct bfa_dconf_mod_s *dconf,
			enum bfa_dconf_event event)
{
	bfa_trc(dconf->bfa, event);

	switch (event) {
	case BFA_DCONF_SM_INIT:
		bfa_timer_start(dconf->bfa, &dconf->timer,
			bfa_dconf_timer, dconf, BFA_DCONF_UPDATE_TOV);
		bfa_sm_set_state(dconf, bfa_dconf_sm_dirty);
		break;
	case BFA_DCONF_SM_EXIT:
		bfa_sm_set_state(dconf, bfa_dconf_sm_uninit);
		bfa_fsm_send_event(&dconf->bfa->iocfc, IOCFC_E_DCONF_DONE);
		break;
	case BFA_DCONF_SM_IOCDISABLE:
		break;
	default:
		bfa_sm_fault(dconf->bfa, event);
	}
}

/*
 * Compute and return memory needed by DRV_CFG module.
 */
static void
bfa_dconf_meminfo(struct bfa_iocfc_cfg_s *cfg, struct bfa_meminfo_s *meminfo,
		  struct bfa_s *bfa)
{
	struct bfa_mem_kva_s *dconf_kva = BFA_MEM_DCONF_KVA(bfa);

	if (cfg->drvcfg.min_cfg)
		bfa_mem_kva_setup(meminfo, dconf_kva,
				sizeof(struct bfa_dconf_hdr_s));
	else
		bfa_mem_kva_setup(meminfo, dconf_kva,
				sizeof(struct bfa_dconf_s));
}

static void
bfa_dconf_attach(struct bfa_s *bfa, void *bfad, struct bfa_iocfc_cfg_s *cfg,
		struct bfa_pcidev_s *pcidev)
{
	struct bfa_dconf_mod_s *dconf = BFA_DCONF_MOD(bfa);

	dconf->bfad = bfad;
	dconf->bfa = bfa;
	dconf->instance = bfa->ioc.port_id;
	bfa_trc(bfa, dconf->instance);

	dconf->dconf = (struct bfa_dconf_s *) bfa_mem_kva_curp(dconf);
	if (cfg->drvcfg.min_cfg) {
		bfa_mem_kva_curp(dconf) += sizeof(struct bfa_dconf_hdr_s);
		dconf->min_cfg = BFA_TRUE;
	} else {
		dconf->min_cfg = BFA_FALSE;
		bfa_mem_kva_curp(dconf) += sizeof(struct bfa_dconf_s);
	}

	bfa_dconf_read_data_valid(bfa) = BFA_FALSE;
	bfa_sm_set_state(dconf, bfa_dconf_sm_uninit);
}

static void
bfa_dconf_init_cb(void *arg, bfa_status_t status)
{
	struct bfa_s *bfa = arg;
	struct bfa_dconf_mod_s *dconf = BFA_DCONF_MOD(bfa);

	bfa_sm_send_event(dconf, BFA_DCONF_SM_FLASH_COMP);
	if (status == BFA_STATUS_OK) {
		bfa_dconf_read_data_valid(bfa) = BFA_TRUE;
		if (dconf->dconf->hdr.signature != BFI_DCONF_SIGNATURE)
			dconf->dconf->hdr.signature = BFI_DCONF_SIGNATURE;
		if (dconf->dconf->hdr.version != BFI_DCONF_VERSION)
			dconf->dconf->hdr.version = BFI_DCONF_VERSION;
	}
	bfa_fsm_send_event(&bfa->iocfc, IOCFC_E_DCONF_DONE);
}

void
bfa_dconf_modinit(struct bfa_s *bfa)
{
	struct bfa_dconf_mod_s *dconf = BFA_DCONF_MOD(bfa);
	bfa_sm_send_event(dconf, BFA_DCONF_SM_INIT);
}
static void
bfa_dconf_start(struct bfa_s *bfa)
{
}

static void
bfa_dconf_stop(struct bfa_s *bfa)
{
}

static void bfa_dconf_timer(void *cbarg)
{
	struct bfa_dconf_mod_s *dconf = cbarg;
	bfa_sm_send_event(dconf, BFA_DCONF_SM_TIMEOUT);
}
static void
bfa_dconf_iocdisable(struct bfa_s *bfa)
{
	struct bfa_dconf_mod_s *dconf = BFA_DCONF_MOD(bfa);
	bfa_sm_send_event(dconf, BFA_DCONF_SM_IOCDISABLE);
}

static void
bfa_dconf_detach(struct bfa_s *bfa)
{
}

static bfa_status_t
bfa_dconf_flash_write(struct bfa_dconf_mod_s *dconf)
{
	bfa_status_t bfa_status;
	bfa_trc(dconf->bfa, 0);

	bfa_status = bfa_flash_update_part(BFA_FLASH(dconf->bfa),
				BFA_FLASH_PART_DRV, dconf->instance,
				dconf->dconf,  sizeof(struct bfa_dconf_s), 0,
				bfa_dconf_cbfn, dconf);
	if (bfa_status != BFA_STATUS_OK)
		WARN_ON(bfa_status);
	bfa_trc(dconf->bfa, bfa_status);

	return bfa_status;
}

bfa_status_t
bfa_dconf_update(struct bfa_s *bfa)
{
	struct bfa_dconf_mod_s *dconf = BFA_DCONF_MOD(bfa);
	bfa_trc(dconf->bfa, 0);
	if (bfa_sm_cmp_state(dconf, bfa_dconf_sm_iocdown_dirty))
		return BFA_STATUS_FAILED;

	if (dconf->min_cfg) {
		bfa_trc(dconf->bfa, dconf->min_cfg);
		return BFA_STATUS_FAILED;
	}

	bfa_sm_send_event(dconf, BFA_DCONF_SM_WR);
	return BFA_STATUS_OK;
}

static void
bfa_dconf_cbfn(void *arg, bfa_status_t status)
{
	struct bfa_dconf_mod_s *dconf = arg;
	WARN_ON(status);
	bfa_sm_send_event(dconf, BFA_DCONF_SM_FLASH_COMP);
}

void
bfa_dconf_modexit(struct bfa_s *bfa)
{
	struct bfa_dconf_mod_s *dconf = BFA_DCONF_MOD(bfa);
	bfa_sm_send_event(dconf, BFA_DCONF_SM_EXIT);
}
