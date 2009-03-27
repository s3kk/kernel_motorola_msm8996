/*
 *  Copyright (C) 2000-2002	   Michael Cornwell <cornwell@acm.org>
 *  Copyright (C) 2000-2002	   Andre Hedrick <andre@linux-ide.org>
 *  Copyright (C) 2001-2002	   Klaus Smolin
 *					IBM Storage Technology Division
 *  Copyright (C) 2003-2004, 2007  Bartlomiej Zolnierkiewicz
 *
 *  The big the bad and the ugly.
 */

#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/scatterlist.h>

#include <asm/uaccess.h>
#include <asm/io.h>

void ide_tf_dump(const char *s, struct ide_taskfile *tf)
{
#ifdef DEBUG
	printk("%s: tf: feat 0x%02x nsect 0x%02x lbal 0x%02x "
		"lbam 0x%02x lbah 0x%02x dev 0x%02x cmd 0x%02x\n",
		s, tf->feature, tf->nsect, tf->lbal,
		tf->lbam, tf->lbah, tf->device, tf->command);
	printk("%s: hob: nsect 0x%02x lbal 0x%02x "
		"lbam 0x%02x lbah 0x%02x\n",
		s, tf->hob_nsect, tf->hob_lbal,
		tf->hob_lbam, tf->hob_lbah);
#endif
}

int taskfile_lib_get_identify (ide_drive_t *drive, u8 *buf)
{
	struct ide_cmd cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.tf.nsect = 0x01;
	if (drive->media == ide_disk)
		cmd.tf.command = ATA_CMD_ID_ATA;
	else
		cmd.tf.command = ATA_CMD_ID_ATAPI;
	cmd.tf_flags	= IDE_TFLAG_TF | IDE_TFLAG_DEVICE;
	cmd.data_phase	= TASKFILE_IN;

	return ide_raw_taskfile(drive, &cmd, buf, 1);
}

static ide_startstop_t task_no_data_intr(ide_drive_t *);
static ide_startstop_t pre_task_out_intr(ide_drive_t *, struct ide_cmd *);
static ide_startstop_t task_in_intr(ide_drive_t *);

ide_startstop_t do_rw_taskfile(ide_drive_t *drive, struct ide_cmd *orig_cmd)
{
	ide_hwif_t *hwif = drive->hwif;
	struct ide_cmd *cmd = &hwif->cmd;
	struct ide_taskfile *tf = &cmd->tf;
	ide_handler_t *handler = NULL;
	const struct ide_tp_ops *tp_ops = hwif->tp_ops;
	const struct ide_dma_ops *dma_ops = hwif->dma_ops;

	if (orig_cmd->data_phase == TASKFILE_MULTI_IN ||
	    orig_cmd->data_phase == TASKFILE_MULTI_OUT) {
		if (!drive->mult_count) {
			printk(KERN_ERR "%s: multimode not set!\n",
					drive->name);
			return ide_stopped;
		}
	}

	if (orig_cmd->ftf_flags & IDE_FTFLAG_FLAGGED)
		orig_cmd->ftf_flags |= IDE_FTFLAG_SET_IN_FLAGS;

	memcpy(cmd, orig_cmd, sizeof(*cmd));

	if ((cmd->tf_flags & IDE_TFLAG_DMA_PIO_FALLBACK) == 0) {
		ide_tf_dump(drive->name, tf);
		tp_ops->set_irq(hwif, 1);
		SELECT_MASK(drive, 0);
		tp_ops->tf_load(drive, cmd);
	}

	switch (cmd->data_phase) {
	case TASKFILE_MULTI_OUT:
	case TASKFILE_OUT:
		tp_ops->exec_command(hwif, tf->command);
		ndelay(400);	/* FIXME */
		return pre_task_out_intr(drive, cmd);
	case TASKFILE_MULTI_IN:
	case TASKFILE_IN:
		handler = task_in_intr;
		/* fall-through */
	case TASKFILE_NO_DATA:
		if (handler == NULL)
			handler = task_no_data_intr;
		ide_execute_command(drive, tf->command, handler,
				    WAIT_WORSTCASE, NULL);
		return ide_started;
	default:
		if ((drive->dev_flags & IDE_DFLAG_USING_DMA) == 0 ||
		    ide_build_sglist(drive, hwif->rq) == 0 ||
		    dma_ops->dma_setup(drive))
			return ide_stopped;
		dma_ops->dma_exec_cmd(drive, tf->command);
		dma_ops->dma_start(drive);
		return ide_started;
	}
}
EXPORT_SYMBOL_GPL(do_rw_taskfile);

/*
 * Handler for commands without a data phase
 */
static ide_startstop_t task_no_data_intr(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	struct ide_cmd *cmd = &hwif->cmd;
	struct ide_taskfile *tf = &cmd->tf;
	int custom = (cmd->tf_flags & IDE_TFLAG_CUSTOM_HANDLER) ? 1 : 0;
	int retries = (custom && tf->command == ATA_CMD_INIT_DEV_PARAMS) ? 5 : 1;
	u8 stat;

	local_irq_enable_in_hardirq();

	while (1) {
		stat = hwif->tp_ops->read_status(hwif);
		if ((stat & ATA_BUSY) == 0 || retries-- == 0)
			break;
		udelay(10);
	};

	if (!OK_STAT(stat, ATA_DRDY, BAD_STAT)) {
		if (custom && tf->command == ATA_CMD_SET_MULTI) {
			drive->mult_req = drive->mult_count = 0;
			drive->special.b.recalibrate = 1;
			(void)ide_dump_status(drive, __func__, stat);
			return ide_stopped;
		} else if (custom && tf->command == ATA_CMD_INIT_DEV_PARAMS) {
			if ((stat & (ATA_ERR | ATA_DRQ)) == 0) {
				ide_set_handler(drive, &task_no_data_intr,
						WAIT_WORSTCASE, NULL);
				return ide_started;
			}
		}
		return ide_error(drive, "task_no_data_intr", stat);
	}

	if (custom && tf->command == ATA_CMD_IDLEIMMEDIATE) {
		hwif->tp_ops->tf_read(drive, cmd);
		if (tf->lbal != 0xc4) {
			printk(KERN_ERR "%s: head unload failed!\n",
			       drive->name);
			ide_tf_dump(drive->name, tf);
		} else
			drive->dev_flags |= IDE_DFLAG_PARKED;
	} else if (custom && tf->command == ATA_CMD_SET_MULTI)
		drive->mult_count = drive->mult_req;

	if (custom == 0 || tf->command == ATA_CMD_IDLEIMMEDIATE) {
		struct request *rq = hwif->rq;
		u8 err = ide_read_error(drive);

		if (blk_pm_request(rq))
			ide_complete_pm_rq(drive, rq);
		else {
			if (rq->cmd_type == REQ_TYPE_ATA_TASKFILE)
				ide_complete_cmd(drive, cmd, stat, err);
			ide_complete_rq(drive, err);
		}
	}

	return ide_stopped;
}

static u8 wait_drive_not_busy(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	int retries;
	u8 stat;

	/*
	 * Last sector was transfered, wait until device is ready.  This can
	 * take up to 6 ms on some ATAPI devices, so we will wait max 10 ms.
	 */
	for (retries = 0; retries < 1000; retries++) {
		stat = hwif->tp_ops->read_status(hwif);

		if (stat & ATA_BUSY)
			udelay(10);
		else
			break;
	}

	if (stat & ATA_BUSY)
		printk(KERN_ERR "%s: drive still BUSY!\n", drive->name);

	return stat;
}

static void ide_pio_sector(ide_drive_t *drive, struct ide_cmd *cmd,
			   unsigned int write)
{
	ide_hwif_t *hwif = drive->hwif;
	struct scatterlist *sg = hwif->sg_table;
	struct scatterlist *cursg = cmd->cursg;
	struct page *page;
#ifdef CONFIG_HIGHMEM
	unsigned long flags;
#endif
	unsigned int offset;
	u8 *buf;

	cursg = cmd->cursg;
	if (!cursg) {
		cursg = sg;
		cmd->cursg = sg;
	}

	page = sg_page(cursg);
	offset = cursg->offset + cmd->cursg_ofs * SECTOR_SIZE;

	/* get the current page and offset */
	page = nth_page(page, (offset >> PAGE_SHIFT));
	offset %= PAGE_SIZE;

#ifdef CONFIG_HIGHMEM
	local_irq_save(flags);
#endif
	buf = kmap_atomic(page, KM_BIO_SRC_IRQ) + offset;

	cmd->nleft--;
	cmd->cursg_ofs++;

	if ((cmd->cursg_ofs * SECTOR_SIZE) == cursg->length) {
		cmd->cursg = sg_next(cmd->cursg);
		cmd->cursg_ofs = 0;
	}

	/* do the actual data transfer */
	if (write)
		hwif->tp_ops->output_data(drive, cmd, buf, SECTOR_SIZE);
	else
		hwif->tp_ops->input_data(drive, cmd, buf, SECTOR_SIZE);

	kunmap_atomic(buf, KM_BIO_SRC_IRQ);
#ifdef CONFIG_HIGHMEM
	local_irq_restore(flags);
#endif
}

static void ide_pio_multi(ide_drive_t *drive, struct ide_cmd *cmd,
			  unsigned int write)
{
	unsigned int nsect;

	nsect = min_t(unsigned int, cmd->nleft, drive->mult_count);
	while (nsect--)
		ide_pio_sector(drive, cmd, write);
}

static void ide_pio_datablock(ide_drive_t *drive, struct ide_cmd *cmd,
			      unsigned int write)
{
	u8 saved_io_32bit = drive->io_32bit;

	if (cmd->tf_flags & IDE_TFLAG_FS)
		cmd->rq->errors = 0;

	if (cmd->tf_flags & IDE_TFLAG_IO_16BIT)
		drive->io_32bit = 0;

	touch_softlockup_watchdog();

	switch (cmd->data_phase) {
	case TASKFILE_MULTI_IN:
	case TASKFILE_MULTI_OUT:
		ide_pio_multi(drive, cmd, write);
		break;
	default:
		ide_pio_sector(drive, cmd, write);
		break;
	}

	drive->io_32bit = saved_io_32bit;
}

static ide_startstop_t task_error(ide_drive_t *drive, struct ide_cmd *cmd,
				  const char *s, u8 stat)
{
	if (cmd->tf_flags & IDE_TFLAG_FS) {
		int sectors = cmd->nsect - cmd->nleft;

		switch (cmd->data_phase) {
		case TASKFILE_IN:
			if (cmd->nleft)
				break;
			/* fall through */
		case TASKFILE_OUT:
			sectors--;
			break;
		case TASKFILE_MULTI_IN:
			if (cmd->nleft)
				break;
			/* fall through */
		case TASKFILE_MULTI_OUT:
			sectors -= drive->mult_count;
		default:
			break;
		}

		if (sectors > 0)
			ide_end_request(drive, 1, sectors);
	}
	return ide_error(drive, s, stat);
}

void ide_finish_cmd(ide_drive_t *drive, struct ide_cmd *cmd, u8 stat)
{
	if ((cmd->tf_flags & IDE_TFLAG_FS) == 0) {
		u8 err = ide_read_error(drive);

		ide_complete_cmd(drive, cmd, stat, err);
		ide_complete_rq(drive, err);
		return;
	}

	ide_end_request(drive, 1, cmd->rq->nr_sectors);
}

/*
 * We got an interrupt on a task_in case, but no errors and no DRQ.
 *
 * It might be a spurious irq (shared irq), but it might be a
 * command that had no output.
 */
static ide_startstop_t task_in_unexpected(ide_drive_t *drive,
					  struct ide_cmd *cmd, u8 stat)
{
	/* Command all done? */
	if (OK_STAT(stat, ATA_DRDY, ATA_BUSY)) {
		ide_finish_cmd(drive, cmd, stat);
		return ide_stopped;
	}

	/* Assume it was a spurious irq */
	ide_set_handler(drive, &task_in_intr, WAIT_WORSTCASE, NULL);
	return ide_started;
}

/*
 * Handler for command with PIO data-in phase (Read/Read Multiple).
 */
static ide_startstop_t task_in_intr(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	struct ide_cmd *cmd = &drive->hwif->cmd;
	u8 stat = hwif->tp_ops->read_status(hwif);

	/* Error? */
	if (stat & ATA_ERR)
		return task_error(drive, cmd, __func__, stat);

	/* Didn't want any data? Odd. */
	if ((stat & ATA_DRQ) == 0)
		return task_in_unexpected(drive, cmd, stat);

	ide_pio_datablock(drive, cmd, 0);

	/* Are we done? Check status and finish transfer. */
	if (cmd->nleft == 0) {
		stat = wait_drive_not_busy(drive);
		if (!OK_STAT(stat, 0, BAD_STAT))
			return task_error(drive, cmd, __func__, stat);
		ide_finish_cmd(drive, cmd, stat);
		return ide_stopped;
	}

	/* Still data left to transfer. */
	ide_set_handler(drive, &task_in_intr, WAIT_WORSTCASE, NULL);

	return ide_started;
}

/*
 * Handler for command with PIO data-out phase (Write/Write Multiple).
 */
static ide_startstop_t task_out_intr (ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	struct ide_cmd *cmd = &drive->hwif->cmd;
	u8 stat = hwif->tp_ops->read_status(hwif);

	if (!OK_STAT(stat, DRIVE_READY, drive->bad_wstat))
		return task_error(drive, cmd, __func__, stat);

	/* Deal with unexpected ATA data phase. */
	if (((stat & ATA_DRQ) == 0) ^ (cmd->nleft == 0))
		return task_error(drive, cmd, __func__, stat);

	if (cmd->nleft == 0) {
		ide_finish_cmd(drive, cmd, stat);
		return ide_stopped;
	}

	/* Still data left to transfer. */
	ide_pio_datablock(drive, cmd, 1);
	ide_set_handler(drive, &task_out_intr, WAIT_WORSTCASE, NULL);

	return ide_started;
}

static ide_startstop_t pre_task_out_intr(ide_drive_t *drive,
					 struct ide_cmd *cmd)
{
	ide_startstop_t startstop;

	if (ide_wait_stat(&startstop, drive, ATA_DRQ,
			  drive->bad_wstat, WAIT_DRQ)) {
		printk(KERN_ERR "%s: no DRQ after issuing %sWRITE%s\n",
			drive->name,
			cmd->data_phase == TASKFILE_MULTI_OUT ? "MULT" : "",
			(drive->dev_flags & IDE_DFLAG_LBA48) ? "_EXT" : "");
		return startstop;
	}

	if ((drive->dev_flags & IDE_DFLAG_UNMASK) == 0)
		local_irq_disable();

	ide_set_handler(drive, &task_out_intr, WAIT_WORSTCASE, NULL);
	ide_pio_datablock(drive, cmd, 1);

	return ide_started;
}

int ide_raw_taskfile(ide_drive_t *drive, struct ide_cmd *cmd, u8 *buf,
		     u16 nsect)
{
	struct request *rq;
	int error;

	rq = blk_get_request(drive->queue, READ, __GFP_WAIT);
	rq->cmd_type = REQ_TYPE_ATA_TASKFILE;
	rq->buffer = buf;

	/*
	 * (ks) We transfer currently only whole sectors.
	 * This is suffient for now.  But, it would be great,
	 * if we would find a solution to transfer any size.
	 * To support special commands like READ LONG.
	 */
	rq->hard_nr_sectors = rq->nr_sectors = nsect;
	rq->hard_cur_sectors = rq->current_nr_sectors = nsect;

	if (cmd->tf_flags & IDE_TFLAG_WRITE)
		rq->cmd_flags |= REQ_RW;

	rq->special = cmd;
	cmd->rq = rq;

	error = blk_execute_rq(drive->queue, NULL, rq, 0);
	blk_put_request(rq);

	return error;
}

EXPORT_SYMBOL(ide_raw_taskfile);

int ide_no_data_taskfile(ide_drive_t *drive, struct ide_cmd *cmd)
{
	cmd->data_phase = TASKFILE_NO_DATA;

	return ide_raw_taskfile(drive, cmd, NULL, 0);
}
EXPORT_SYMBOL_GPL(ide_no_data_taskfile);

#ifdef CONFIG_IDE_TASK_IOCTL
int ide_taskfile_ioctl(ide_drive_t *drive, unsigned long arg)
{
	ide_task_request_t	*req_task;
	struct ide_cmd		cmd;
	u8 *outbuf		= NULL;
	u8 *inbuf		= NULL;
	u8 *data_buf		= NULL;
	int err			= 0;
	int tasksize		= sizeof(struct ide_task_request_s);
	unsigned int taskin	= 0;
	unsigned int taskout	= 0;
	u16 nsect		= 0;
	char __user *buf = (char __user *)arg;

//	printk("IDE Taskfile ...\n");

	req_task = kzalloc(tasksize, GFP_KERNEL);
	if (req_task == NULL) return -ENOMEM;
	if (copy_from_user(req_task, buf, tasksize)) {
		kfree(req_task);
		return -EFAULT;
	}

	taskout = req_task->out_size;
	taskin  = req_task->in_size;
	
	if (taskin > 65536 || taskout > 65536) {
		err = -EINVAL;
		goto abort;
	}

	if (taskout) {
		int outtotal = tasksize;
		outbuf = kzalloc(taskout, GFP_KERNEL);
		if (outbuf == NULL) {
			err = -ENOMEM;
			goto abort;
		}
		if (copy_from_user(outbuf, buf + outtotal, taskout)) {
			err = -EFAULT;
			goto abort;
		}
	}

	if (taskin) {
		int intotal = tasksize + taskout;
		inbuf = kzalloc(taskin, GFP_KERNEL);
		if (inbuf == NULL) {
			err = -ENOMEM;
			goto abort;
		}
		if (copy_from_user(inbuf, buf + intotal, taskin)) {
			err = -EFAULT;
			goto abort;
		}
	}

	memset(&cmd, 0, sizeof(cmd));

	memcpy(&cmd.tf_array[0], req_task->hob_ports,
	       HDIO_DRIVE_HOB_HDR_SIZE - 2);
	memcpy(&cmd.tf_array[6], req_task->io_ports,
	       HDIO_DRIVE_TASK_HDR_SIZE);

	cmd.data_phase = req_task->data_phase;
	cmd.tf_flags   = IDE_TFLAG_IO_16BIT | IDE_TFLAG_DEVICE |
			 IDE_TFLAG_IN_TF;

	if (drive->dev_flags & IDE_DFLAG_LBA48)
		cmd.tf_flags |= (IDE_TFLAG_LBA48 | IDE_TFLAG_IN_HOB);

	if (req_task->out_flags.all) {
		cmd.ftf_flags |= IDE_FTFLAG_FLAGGED;

		if (req_task->out_flags.b.data)
			cmd.ftf_flags |= IDE_FTFLAG_OUT_DATA;

		if (req_task->out_flags.b.nsector_hob)
			cmd.tf_flags |= IDE_TFLAG_OUT_HOB_NSECT;
		if (req_task->out_flags.b.sector_hob)
			cmd.tf_flags |= IDE_TFLAG_OUT_HOB_LBAL;
		if (req_task->out_flags.b.lcyl_hob)
			cmd.tf_flags |= IDE_TFLAG_OUT_HOB_LBAM;
		if (req_task->out_flags.b.hcyl_hob)
			cmd.tf_flags |= IDE_TFLAG_OUT_HOB_LBAH;

		if (req_task->out_flags.b.error_feature)
			cmd.tf_flags |= IDE_TFLAG_OUT_FEATURE;
		if (req_task->out_flags.b.nsector)
			cmd.tf_flags |= IDE_TFLAG_OUT_NSECT;
		if (req_task->out_flags.b.sector)
			cmd.tf_flags |= IDE_TFLAG_OUT_LBAL;
		if (req_task->out_flags.b.lcyl)
			cmd.tf_flags |= IDE_TFLAG_OUT_LBAM;
		if (req_task->out_flags.b.hcyl)
			cmd.tf_flags |= IDE_TFLAG_OUT_LBAH;
	} else {
		cmd.tf_flags |= IDE_TFLAG_OUT_TF;
		if (cmd.tf_flags & IDE_TFLAG_LBA48)
			cmd.tf_flags |= IDE_TFLAG_OUT_HOB;
	}

	if (req_task->in_flags.b.data)
		cmd.ftf_flags |= IDE_FTFLAG_IN_DATA;

	switch(req_task->data_phase) {
		case TASKFILE_MULTI_OUT:
			if (!drive->mult_count) {
				/* (hs): give up if multcount is not set */
				printk(KERN_ERR "%s: %s Multimode Write " \
					"multcount is not set\n",
					drive->name, __func__);
				err = -EPERM;
				goto abort;
			}
			/* fall through */
		case TASKFILE_OUT:
			/* fall through */
		case TASKFILE_OUT_DMAQ:
		case TASKFILE_OUT_DMA:
			nsect = taskout / SECTOR_SIZE;
			data_buf = outbuf;
			break;
		case TASKFILE_MULTI_IN:
			if (!drive->mult_count) {
				/* (hs): give up if multcount is not set */
				printk(KERN_ERR "%s: %s Multimode Read failure " \
					"multcount is not set\n",
					drive->name, __func__);
				err = -EPERM;
				goto abort;
			}
			/* fall through */
		case TASKFILE_IN:
			/* fall through */
		case TASKFILE_IN_DMAQ:
		case TASKFILE_IN_DMA:
			nsect = taskin / SECTOR_SIZE;
			data_buf = inbuf;
			break;
		case TASKFILE_NO_DATA:
			break;
		default:
			err = -EFAULT;
			goto abort;
	}

	if (req_task->req_cmd == IDE_DRIVE_TASK_NO_DATA)
		nsect = 0;
	else if (!nsect) {
		nsect = (cmd.tf.hob_nsect << 8) | cmd.tf.nsect;

		if (!nsect) {
			printk(KERN_ERR "%s: in/out command without data\n",
					drive->name);
			err = -EFAULT;
			goto abort;
		}
	}

	if (req_task->req_cmd == IDE_DRIVE_TASK_RAW_WRITE)
		cmd.tf_flags |= IDE_TFLAG_WRITE;

	err = ide_raw_taskfile(drive, &cmd, data_buf, nsect);

	memcpy(req_task->hob_ports, &cmd.tf_array[0],
	       HDIO_DRIVE_HOB_HDR_SIZE - 2);
	memcpy(req_task->io_ports, &cmd.tf_array[6],
	       HDIO_DRIVE_TASK_HDR_SIZE);

	if ((cmd.ftf_flags & IDE_FTFLAG_SET_IN_FLAGS) &&
	    req_task->in_flags.all == 0) {
		req_task->in_flags.all = IDE_TASKFILE_STD_IN_FLAGS;
		if (drive->dev_flags & IDE_DFLAG_LBA48)
			req_task->in_flags.all |= (IDE_HOB_STD_IN_FLAGS << 8);
	}

	if (copy_to_user(buf, req_task, tasksize)) {
		err = -EFAULT;
		goto abort;
	}
	if (taskout) {
		int outtotal = tasksize;
		if (copy_to_user(buf + outtotal, outbuf, taskout)) {
			err = -EFAULT;
			goto abort;
		}
	}
	if (taskin) {
		int intotal = tasksize + taskout;
		if (copy_to_user(buf + intotal, inbuf, taskin)) {
			err = -EFAULT;
			goto abort;
		}
	}
abort:
	kfree(req_task);
	kfree(outbuf);
	kfree(inbuf);

//	printk("IDE Taskfile ioctl ended. rc = %i\n", err);

	return err;
}
#endif
