/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <asm/semaphore.h>

#include "gfs2.h"
#include "bmap.h"
#include "dir.h"
#include "format.h"
#include "glock.h"
#include "glops.h"
#include "inode.h"
#include "log.h"
#include "meta_io.h"
#include "quota.h"
#include "recovery.h"
#include "rgrp.h"
#include "super.h"
#include "trans.h"
#include "unlinked.h"

/**
 * gfs2_tune_init - Fill a gfs2_tune structure with default values
 * @gt: tune
 *
 */

void gfs2_tune_init(struct gfs2_tune *gt)
{
	spin_lock_init(&gt->gt_spin);

	gt->gt_ilimit = 100;
	gt->gt_ilimit_tries = 3;
	gt->gt_ilimit_min = 1;
	gt->gt_demote_secs = 300;
	gt->gt_incore_log_blocks = 1024;
	gt->gt_log_flush_secs = 60;
	gt->gt_jindex_refresh_secs = 60;
	gt->gt_scand_secs = 15;
	gt->gt_recoverd_secs = 60;
	gt->gt_logd_secs = 1;
	gt->gt_quotad_secs = 5;
	gt->gt_inoded_secs = 15;
	gt->gt_quota_simul_sync = 64;
	gt->gt_quota_warn_period = 10;
	gt->gt_quota_scale_num = 1;
	gt->gt_quota_scale_den = 1;
	gt->gt_quota_cache_secs = 300;
	gt->gt_quota_quantum = 60;
	gt->gt_atime_quantum = 3600;
	gt->gt_new_files_jdata = 0;
	gt->gt_new_files_directio = 0;
	gt->gt_max_atomic_write = 4 << 20;
	gt->gt_max_readahead = 1 << 18;
	gt->gt_lockdump_size = 131072;
	gt->gt_stall_secs = 600;
	gt->gt_complain_secs = 10;
	gt->gt_reclaim_limit = 5000;
	gt->gt_entries_per_readdir = 32;
	gt->gt_prefetch_secs = 10;
	gt->gt_greedy_default = HZ / 10;
	gt->gt_greedy_quantum = HZ / 40;
	gt->gt_greedy_max = HZ / 4;
	gt->gt_statfs_quantum = 30;
	gt->gt_statfs_slow = 0;
}

/**
 * gfs2_check_sb - Check superblock
 * @sdp: the filesystem
 * @sb: The superblock
 * @silent: Don't print a message if the check fails
 *
 * Checks the version code of the FS is one that we understand how to
 * read and that the sizes of the various on-disk structures have not
 * changed.
 */

int gfs2_check_sb(struct gfs2_sbd *sdp, struct gfs2_sb *sb, int silent)
{
	unsigned int x;

	if (sb->sb_header.mh_magic != GFS2_MAGIC ||
	    sb->sb_header.mh_type != GFS2_METATYPE_SB) {
		if (!silent)
			printk(KERN_WARNING "GFS2: not a GFS2 filesystem\n");
		return -EINVAL;
	}

	/*  If format numbers match exactly, we're done.  */

	if (sb->sb_fs_format == GFS2_FORMAT_FS &&
	    sb->sb_multihost_format == GFS2_FORMAT_MULTI)
		return 0;

	if (sb->sb_fs_format != GFS2_FORMAT_FS) {
		for (x = 0; gfs2_old_fs_formats[x]; x++)
			if (gfs2_old_fs_formats[x] == sb->sb_fs_format)
				break;

		if (!gfs2_old_fs_formats[x]) {
			printk(KERN_WARNING
			       "GFS2: code version (%u, %u) is incompatible "
			       "with ondisk format (%u, %u)\n",
			       GFS2_FORMAT_FS, GFS2_FORMAT_MULTI,
			       sb->sb_fs_format, sb->sb_multihost_format);
			printk(KERN_WARNING
			       "GFS2: I don't know how to upgrade this FS\n");
			return -EINVAL;
		}
	}

	if (sb->sb_multihost_format != GFS2_FORMAT_MULTI) {
		for (x = 0; gfs2_old_multihost_formats[x]; x++)
			if (gfs2_old_multihost_formats[x] ==
			    sb->sb_multihost_format)
				break;

		if (!gfs2_old_multihost_formats[x]) {
			printk(KERN_WARNING
			       "GFS2: code version (%u, %u) is incompatible "
			       "with ondisk format (%u, %u)\n",
			       GFS2_FORMAT_FS, GFS2_FORMAT_MULTI,
			       sb->sb_fs_format, sb->sb_multihost_format);
			printk(KERN_WARNING
			       "GFS2: I don't know how to upgrade this FS\n");
			return -EINVAL;
		}
	}

	if (!sdp->sd_args.ar_upgrade) {
		printk(KERN_WARNING
		       "GFS2: code version (%u, %u) is incompatible "
		       "with ondisk format (%u, %u)\n",
		       GFS2_FORMAT_FS, GFS2_FORMAT_MULTI,
		       sb->sb_fs_format, sb->sb_multihost_format);
		printk(KERN_INFO
		       "GFS2: Use the \"upgrade\" mount option to upgrade "
		       "the FS\n");
		printk(KERN_INFO "GFS2: See the manual for more details\n");
		return -EINVAL;
	}

	return 0;
}

/**
 * gfs2_read_sb - Read super block
 * @sdp: The GFS2 superblock
 * @gl: the glock for the superblock (assumed to be held)
 * @silent: Don't print message if mount fails
 *
 */

int gfs2_read_sb(struct gfs2_sbd *sdp, struct gfs2_glock *gl, int silent)
{
	struct buffer_head *bh;
	uint32_t hash_blocks, ind_blocks, leaf_blocks;
	uint32_t tmp_blocks;
	unsigned int x;
	int error;

	error = gfs2_meta_read(gl, GFS2_SB_ADDR >> sdp->sd_fsb2bb_shift,
			       DIO_FORCE | DIO_START | DIO_WAIT, &bh);
	if (error) {
		if (!silent)
			fs_err(sdp, "can't read superblock\n");
		return error;
	}

	gfs2_assert(sdp, sizeof(struct gfs2_sb) <= bh->b_size);
	gfs2_sb_in(&sdp->sd_sb, bh->b_data);
	brelse(bh);

	error = gfs2_check_sb(sdp, &sdp->sd_sb, silent);
	if (error)
		return error;

	sdp->sd_fsb2bb_shift = sdp->sd_sb.sb_bsize_shift -
			       GFS2_BASIC_BLOCK_SHIFT;
	sdp->sd_fsb2bb = 1 << sdp->sd_fsb2bb_shift;
	sdp->sd_diptrs = (sdp->sd_sb.sb_bsize -
			  sizeof(struct gfs2_dinode)) / sizeof(uint64_t);
	sdp->sd_inptrs = (sdp->sd_sb.sb_bsize -
			  sizeof(struct gfs2_meta_header)) / sizeof(uint64_t);
	sdp->sd_jbsize = sdp->sd_sb.sb_bsize - sizeof(struct gfs2_meta_header);
	sdp->sd_hash_bsize = sdp->sd_sb.sb_bsize / 2;
	sdp->sd_hash_bsize_shift = sdp->sd_sb.sb_bsize_shift - 1;
	sdp->sd_hash_ptrs = sdp->sd_hash_bsize / sizeof(uint64_t);
	sdp->sd_ut_per_block = (sdp->sd_sb.sb_bsize -
				sizeof(struct gfs2_meta_header)) /
			       sizeof(struct gfs2_unlinked_tag);
	sdp->sd_qc_per_block = (sdp->sd_sb.sb_bsize -
				sizeof(struct gfs2_meta_header)) /
			       sizeof(struct gfs2_quota_change);

	/* Compute maximum reservation required to add a entry to a directory */

	hash_blocks = DIV_RU(sizeof(uint64_t) * (1 << GFS2_DIR_MAX_DEPTH),
			     sdp->sd_jbsize);

	ind_blocks = 0;
	for (tmp_blocks = hash_blocks; tmp_blocks > sdp->sd_diptrs;) {
		tmp_blocks = DIV_RU(tmp_blocks, sdp->sd_inptrs);
		ind_blocks += tmp_blocks;
	}

	leaf_blocks = 2 + GFS2_DIR_MAX_DEPTH;

	sdp->sd_max_dirres = hash_blocks + ind_blocks + leaf_blocks;

	sdp->sd_heightsize[0] = sdp->sd_sb.sb_bsize -
				sizeof(struct gfs2_dinode);
	sdp->sd_heightsize[1] = sdp->sd_sb.sb_bsize * sdp->sd_diptrs;
	for (x = 2;; x++) {
		uint64_t space, d;
		uint32_t m;

		space = sdp->sd_heightsize[x - 1] * sdp->sd_inptrs;
		d = space;
		m = do_div(d, sdp->sd_inptrs);

		if (d != sdp->sd_heightsize[x - 1] || m)
			break;
		sdp->sd_heightsize[x] = space;
	}
	sdp->sd_max_height = x;
	gfs2_assert(sdp, sdp->sd_max_height <= GFS2_MAX_META_HEIGHT);

	sdp->sd_jheightsize[0] = sdp->sd_sb.sb_bsize -
				 sizeof(struct gfs2_dinode);
	sdp->sd_jheightsize[1] = sdp->sd_jbsize * sdp->sd_diptrs;
	for (x = 2;; x++) {
		uint64_t space, d;
		uint32_t m;

		space = sdp->sd_jheightsize[x - 1] * sdp->sd_inptrs;
		d = space;
		m = do_div(d, sdp->sd_inptrs);

		if (d != sdp->sd_jheightsize[x - 1] || m)
			break;
		sdp->sd_jheightsize[x] = space;
	}
	sdp->sd_max_jheight = x;
	gfs2_assert(sdp, sdp->sd_max_jheight <= GFS2_MAX_META_HEIGHT);

	return 0;
}

int gfs2_do_upgrade(struct gfs2_sbd *sdp, struct gfs2_glock *sb_gl)
{
	return 0;
}

/**
 * gfs2_jindex_hold - Grab a lock on the jindex
 * @sdp: The GFS2 superblock
 * @ji_gh: the holder for the jindex glock
 *
 * This is very similar to the gfs2_rindex_hold() function, except that
 * in general we hold the jindex lock for longer periods of time and
 * we grab it far less frequently (in general) then the rgrp lock.
 *
 * Returns: errno
 */

int gfs2_jindex_hold(struct gfs2_sbd *sdp, struct gfs2_holder *ji_gh)
{
	struct gfs2_inode *dip = get_v2ip(sdp->sd_jindex);
	struct qstr name;
	char buf[20];
	struct gfs2_jdesc *jd;
	int error;

	name.name = buf;

	mutex_lock(&sdp->sd_jindex_mutex);

	for (;;) {
		error = gfs2_glock_nq_init(dip->i_gl, LM_ST_SHARED,
					   GL_LOCAL_EXCL, ji_gh);
		if (error)
			break;

		name.len = sprintf(buf, "journal%u", sdp->sd_journals);

		error = gfs2_dir_search(get_v2ip(sdp->sd_jindex),
					&name, NULL, NULL);
		if (error == -ENOENT) {
			error = 0;
			break;
		}

		gfs2_glock_dq_uninit(ji_gh);

		if (error)
			break;

		error = -ENOMEM;
		jd = kzalloc(sizeof(struct gfs2_jdesc), GFP_KERNEL);
		if (!jd)
			break;

		error = gfs2_lookupi(sdp->sd_jindex, &name, 1, &jd->jd_inode);
		if (error) {
			kfree(jd);
			break;
		}

		spin_lock(&sdp->sd_jindex_spin);
		jd->jd_jid = sdp->sd_journals++;
		list_add_tail(&jd->jd_list, &sdp->sd_jindex_list);
		spin_unlock(&sdp->sd_jindex_spin);
	}

	mutex_unlock(&sdp->sd_jindex_mutex);

	return error;
}

/**
 * gfs2_jindex_free - Clear all the journal index information
 * @sdp: The GFS2 superblock
 *
 */

void gfs2_jindex_free(struct gfs2_sbd *sdp)
{
	struct list_head list;
	struct gfs2_jdesc *jd;

	spin_lock(&sdp->sd_jindex_spin);
	list_add(&list, &sdp->sd_jindex_list);
	list_del_init(&sdp->sd_jindex_list);
	sdp->sd_journals = 0;
	spin_unlock(&sdp->sd_jindex_spin);

	while (!list_empty(&list)) {
		jd = list_entry(list.next, struct gfs2_jdesc, jd_list);
		list_del(&jd->jd_list);
		iput(jd->jd_inode);
		kfree(jd);
	}
}

static struct gfs2_jdesc *jdesc_find_i(struct list_head *head, unsigned int jid)
{
	struct gfs2_jdesc *jd;
	int found = 0;

	list_for_each_entry(jd, head, jd_list) {
		if (jd->jd_jid == jid) {
			found = 1;
			break;
		}
	}

	if (!found)
		jd = NULL;

	return jd;
}

struct gfs2_jdesc *gfs2_jdesc_find(struct gfs2_sbd *sdp, unsigned int jid)
{
	struct gfs2_jdesc *jd;

	spin_lock(&sdp->sd_jindex_spin);
	jd = jdesc_find_i(&sdp->sd_jindex_list, jid);
	spin_unlock(&sdp->sd_jindex_spin);

	return jd;
}

void gfs2_jdesc_make_dirty(struct gfs2_sbd *sdp, unsigned int jid)
{
	struct gfs2_jdesc *jd;

	spin_lock(&sdp->sd_jindex_spin);
	jd = jdesc_find_i(&sdp->sd_jindex_list, jid);
	if (jd)
		jd->jd_dirty = 1;
	spin_unlock(&sdp->sd_jindex_spin);
}

struct gfs2_jdesc *gfs2_jdesc_find_dirty(struct gfs2_sbd *sdp)
{
	struct gfs2_jdesc *jd;
	int found = 0;

	spin_lock(&sdp->sd_jindex_spin);

	list_for_each_entry(jd, &sdp->sd_jindex_list, jd_list) {
		if (jd->jd_dirty) {
			jd->jd_dirty = 0;
			found = 1;
			break;
		}
	}
	spin_unlock(&sdp->sd_jindex_spin);

	if (!found)
		jd = NULL;

	return jd;
}

int gfs2_jdesc_check(struct gfs2_jdesc *jd)
{
	struct gfs2_inode *ip = get_v2ip(jd->jd_inode);
	struct gfs2_sbd *sdp = ip->i_sbd;
	int ar;
	int error;

	if (ip->i_di.di_size < (8 << 20) ||
	    ip->i_di.di_size > (1 << 30) ||
	    (ip->i_di.di_size & (sdp->sd_sb.sb_bsize - 1))) {
		gfs2_consist_inode(ip);
		return -EIO;
	}
	jd->jd_blocks = ip->i_di.di_size >> sdp->sd_sb.sb_bsize_shift;

	error = gfs2_write_alloc_required(ip,
					  0, ip->i_di.di_size,
					  &ar);
	if (!error && ar) {
		gfs2_consist_inode(ip);
		error = -EIO;
	}

	return error;
}

int gfs2_lookup_master_dir(struct gfs2_sbd *sdp)
{
	struct inode *inode = NULL;
	struct gfs2_glock *gl;
	int error;

	error = gfs2_glock_get(sdp,
			       sdp->sd_sb.sb_master_dir.no_addr,
			       &gfs2_inode_glops, CREATE, &gl);
	if (!error) {
		error = gfs2_lookup_simple(sdp->sd_root_dir, ".gfs2_admin",
					   &inode);
		sdp->sd_master_dir = inode;
		gfs2_glock_put(gl);
	}

	return error;
}

/**
 * gfs2_make_fs_rw - Turn a Read-Only FS into a Read-Write one
 * @sdp: the filesystem
 *
 * Returns: errno
 */

int gfs2_make_fs_rw(struct gfs2_sbd *sdp)
{
	struct gfs2_glock *j_gl = get_v2ip(sdp->sd_jdesc->jd_inode)->i_gl;
	struct gfs2_holder t_gh;
	struct gfs2_log_header head;
	int error;

	error = gfs2_glock_nq_init(sdp->sd_trans_gl, LM_ST_SHARED,
				   GL_LOCAL_EXCL | GL_NEVER_RECURSE, &t_gh);
	if (error)
		return error;

	gfs2_meta_cache_flush(get_v2ip(sdp->sd_jdesc->jd_inode));
	j_gl->gl_ops->go_inval(j_gl, DIO_METADATA | DIO_DATA);

	error = gfs2_find_jhead(sdp->sd_jdesc, &head);
	if (error)
		goto fail;

	if (!(head.lh_flags & GFS2_LOG_HEAD_UNMOUNT)) {
		gfs2_consist(sdp);
		error = -EIO;
		goto fail;
	}

	/*  Initialize some head of the log stuff  */
	sdp->sd_log_sequence = head.lh_sequence + 1;
	gfs2_log_pointers_init(sdp, head.lh_blkno);

	error = gfs2_unlinked_init(sdp);
	if (error)
		goto fail;
	error = gfs2_quota_init(sdp);
	if (error)
		goto fail_unlinked;

	set_bit(SDF_JOURNAL_LIVE, &sdp->sd_flags);

	gfs2_glock_dq_uninit(&t_gh);

	return 0;

 fail_unlinked:
	gfs2_unlinked_cleanup(sdp);

 fail:
	t_gh.gh_flags |= GL_NOCACHE;
	gfs2_glock_dq_uninit(&t_gh);

	return error;
}

/**
 * gfs2_make_fs_ro - Turn a Read-Write FS into a Read-Only one
 * @sdp: the filesystem
 *
 * Returns: errno
 */

int gfs2_make_fs_ro(struct gfs2_sbd *sdp)
{
	struct gfs2_holder t_gh;
	int error;

	gfs2_unlinked_dealloc(sdp);
	gfs2_quota_sync(sdp);
	gfs2_statfs_sync(sdp);

	error = gfs2_glock_nq_init(sdp->sd_trans_gl, LM_ST_SHARED,
				GL_LOCAL_EXCL | GL_NEVER_RECURSE | GL_NOCACHE,
				&t_gh);
	if (error && !test_bit(SDF_SHUTDOWN, &sdp->sd_flags))
		return error;

	gfs2_meta_syncfs(sdp);
	gfs2_log_shutdown(sdp);

	clear_bit(SDF_JOURNAL_LIVE, &sdp->sd_flags);

	if (t_gh.gh_gl)
		gfs2_glock_dq_uninit(&t_gh);

	gfs2_unlinked_cleanup(sdp);
	gfs2_quota_cleanup(sdp);

	return error;
}

int gfs2_statfs_init(struct gfs2_sbd *sdp)
{
	struct gfs2_inode *m_ip = get_v2ip(sdp->sd_statfs_inode);
	struct gfs2_statfs_change *m_sc = &sdp->sd_statfs_master;
	struct gfs2_inode *l_ip = get_v2ip(sdp->sd_sc_inode);
	struct gfs2_statfs_change *l_sc = &sdp->sd_statfs_local;
	struct buffer_head *m_bh, *l_bh;
	struct gfs2_holder gh;
	int error;

	error = gfs2_glock_nq_init(m_ip->i_gl, LM_ST_EXCLUSIVE, GL_NOCACHE,
				   &gh);
	if (error)
		return error;

	error = gfs2_meta_inode_buffer(m_ip, &m_bh);
	if (error)
		goto out;

	if (sdp->sd_args.ar_spectator) {
		spin_lock(&sdp->sd_statfs_spin);
		gfs2_statfs_change_in(m_sc, m_bh->b_data +
				      sizeof(struct gfs2_dinode));
		spin_unlock(&sdp->sd_statfs_spin);
	} else {
		error = gfs2_meta_inode_buffer(l_ip, &l_bh);
		if (error)
			goto out_m_bh;

		spin_lock(&sdp->sd_statfs_spin);
		gfs2_statfs_change_in(m_sc, m_bh->b_data +
				      sizeof(struct gfs2_dinode));
		gfs2_statfs_change_in(l_sc, l_bh->b_data +
				      sizeof(struct gfs2_dinode));
		spin_unlock(&sdp->sd_statfs_spin);

		brelse(l_bh);
	}

 out_m_bh:
	brelse(m_bh);

 out:
	gfs2_glock_dq_uninit(&gh);

	return 0;
}

void gfs2_statfs_change(struct gfs2_sbd *sdp, int64_t total, int64_t free,
			int64_t dinodes)
{
	struct gfs2_inode *l_ip = get_v2ip(sdp->sd_sc_inode);
	struct gfs2_statfs_change *l_sc = &sdp->sd_statfs_local;
	struct buffer_head *l_bh;
	int error;

	error = gfs2_meta_inode_buffer(l_ip, &l_bh);
	if (error)
		return;

	mutex_lock(&sdp->sd_statfs_mutex);
	gfs2_trans_add_bh(l_ip->i_gl, l_bh, 1);
	mutex_unlock(&sdp->sd_statfs_mutex);

	spin_lock(&sdp->sd_statfs_spin);
	l_sc->sc_total += total;
	l_sc->sc_free += free;
	l_sc->sc_dinodes += dinodes;
	gfs2_statfs_change_out(l_sc, l_bh->b_data +
			       sizeof(struct gfs2_dinode));	
	spin_unlock(&sdp->sd_statfs_spin);

	brelse(l_bh);
}

int gfs2_statfs_sync(struct gfs2_sbd *sdp)
{
	struct gfs2_inode *m_ip = get_v2ip(sdp->sd_statfs_inode);
	struct gfs2_inode *l_ip = get_v2ip(sdp->sd_sc_inode);
	struct gfs2_statfs_change *m_sc = &sdp->sd_statfs_master;
	struct gfs2_statfs_change *l_sc = &sdp->sd_statfs_local;
	struct gfs2_holder gh;
	struct buffer_head *m_bh, *l_bh;
	int error;

	error = gfs2_glock_nq_init(m_ip->i_gl, LM_ST_EXCLUSIVE, GL_NOCACHE,
				   &gh);
	if (error)
		return error;

	error = gfs2_meta_inode_buffer(m_ip, &m_bh);
	if (error)
		goto out;

	spin_lock(&sdp->sd_statfs_spin);
	gfs2_statfs_change_in(m_sc, m_bh->b_data +
			      sizeof(struct gfs2_dinode));	
	if (!l_sc->sc_total && !l_sc->sc_free && !l_sc->sc_dinodes) {
		spin_unlock(&sdp->sd_statfs_spin);
		goto out_bh;
	}
	spin_unlock(&sdp->sd_statfs_spin);

	error = gfs2_meta_inode_buffer(l_ip, &l_bh);
	if (error)
		goto out_bh;

	error = gfs2_trans_begin(sdp, 2 * RES_DINODE, 0);
	if (error)
		goto out_bh2;

	mutex_lock(&sdp->sd_statfs_mutex);
	gfs2_trans_add_bh(l_ip->i_gl, l_bh, 1);
	mutex_unlock(&sdp->sd_statfs_mutex);

	spin_lock(&sdp->sd_statfs_spin);
	m_sc->sc_total += l_sc->sc_total;
	m_sc->sc_free += l_sc->sc_free;
	m_sc->sc_dinodes += l_sc->sc_dinodes;
	memset(l_sc, 0, sizeof(struct gfs2_statfs_change));
	memset(l_bh->b_data + sizeof(struct gfs2_dinode),
	       0, sizeof(struct gfs2_statfs_change));
	spin_unlock(&sdp->sd_statfs_spin);

	gfs2_trans_add_bh(m_ip->i_gl, m_bh, 1);
	gfs2_statfs_change_out(m_sc, m_bh->b_data + sizeof(struct gfs2_dinode));

	gfs2_trans_end(sdp);

 out_bh2:
	brelse(l_bh);

 out_bh:
	brelse(m_bh);

 out:
	gfs2_glock_dq_uninit(&gh);

	return error;
}

/**
 * gfs2_statfs_i - Do a statfs
 * @sdp: the filesystem
 * @sg: the sg structure
 *
 * Returns: errno
 */

int gfs2_statfs_i(struct gfs2_sbd *sdp, struct gfs2_statfs_change *sc)
{
	struct gfs2_statfs_change *m_sc = &sdp->sd_statfs_master;
	struct gfs2_statfs_change *l_sc = &sdp->sd_statfs_local;

	spin_lock(&sdp->sd_statfs_spin);

	*sc = *m_sc;
	sc->sc_total += l_sc->sc_total;
	sc->sc_free += l_sc->sc_free;
	sc->sc_dinodes += l_sc->sc_dinodes;

	spin_unlock(&sdp->sd_statfs_spin);

	if (sc->sc_free < 0)
		sc->sc_free = 0;
	if (sc->sc_free > sc->sc_total)
		sc->sc_free = sc->sc_total;
	if (sc->sc_dinodes < 0)
		sc->sc_dinodes = 0;

	return 0;
}

/**
 * statfs_fill - fill in the sg for a given RG
 * @rgd: the RG
 * @sc: the sc structure
 *
 * Returns: 0 on success, -ESTALE if the LVB is invalid
 */

static int statfs_slow_fill(struct gfs2_rgrpd *rgd,
			    struct gfs2_statfs_change *sc)
{
	gfs2_rgrp_verify(rgd);
	sc->sc_total += rgd->rd_ri.ri_data;
	sc->sc_free += rgd->rd_rg.rg_free;
	sc->sc_dinodes += rgd->rd_rg.rg_dinodes;
	return 0;
}

/**
 * gfs2_statfs_slow - Stat a filesystem using asynchronous locking
 * @sdp: the filesystem
 * @sc: the sc info that will be returned
 *
 * Any error (other than a signal) will cause this routine to fall back
 * to the synchronous version.
 *
 * FIXME: This really shouldn't busy wait like this.
 *
 * Returns: errno
 */

int gfs2_statfs_slow(struct gfs2_sbd *sdp, struct gfs2_statfs_change *sc)
{
	struct gfs2_holder ri_gh;
	struct gfs2_rgrpd *rgd_next;
	struct gfs2_holder *gha, *gh;
	unsigned int slots = 64;
	unsigned int x;
	int done;
	int error = 0, err;

	memset(sc, 0, sizeof(struct gfs2_statfs_change));
	gha = kcalloc(slots, sizeof(struct gfs2_holder), GFP_KERNEL);
	if (!gha)
		return -ENOMEM;

	error = gfs2_rindex_hold(sdp, &ri_gh);
	if (error)
		goto out;

	rgd_next = gfs2_rgrpd_get_first(sdp);

	for (;;) {
		done = 1;

		for (x = 0; x < slots; x++) {
			gh = gha + x;

			if (gh->gh_gl && gfs2_glock_poll(gh)) {
				err = gfs2_glock_wait(gh);
				if (err) {
					gfs2_holder_uninit(gh);
					error = err;
				} else {
					if (!error)
						error = statfs_slow_fill(get_gl2rgd(gh->gh_gl), sc);
					gfs2_glock_dq_uninit(gh);
				}
			}

			if (gh->gh_gl)
				done = 0;
			else if (rgd_next && !error) {
				error = gfs2_glock_nq_init(rgd_next->rd_gl,
							   LM_ST_SHARED,
							   GL_ASYNC,
							   gh);
				rgd_next = gfs2_rgrpd_get_next(rgd_next);
				done = 0;
			}

			if (signal_pending(current))
				error = -ERESTARTSYS;
		}

		if (done)
			break;

		yield();
	}

	gfs2_glock_dq_uninit(&ri_gh);

 out:
	kfree(gha);

	return error;
}

struct lfcc {
	struct list_head list;
	struct gfs2_holder gh;
};

/**
 * gfs2_lock_fs_check_clean - Stop all writes to the FS and check that all
 *                            journals are clean
 * @sdp: the file system
 * @state: the state to put the transaction lock into
 * @t_gh: the hold on the transaction lock
 *
 * Returns: errno
 */

int gfs2_lock_fs_check_clean(struct gfs2_sbd *sdp, struct gfs2_holder *t_gh)
{
	struct gfs2_holder ji_gh;
	struct gfs2_jdesc *jd;
	struct lfcc *lfcc;
	LIST_HEAD(list);
	struct gfs2_log_header lh;
	int error;

	error = gfs2_jindex_hold(sdp, &ji_gh);
	if (error)
		return error;

	list_for_each_entry(jd, &sdp->sd_jindex_list, jd_list) {
		lfcc = kmalloc(sizeof(struct lfcc), GFP_KERNEL);
		if (!lfcc) {
			error = -ENOMEM;
			goto out;
		}
		error = gfs2_glock_nq_init(get_v2ip(jd->jd_inode)->i_gl,
					   LM_ST_SHARED, 0,
					   &lfcc->gh);
		if (error) {
			kfree(lfcc);
			goto out;
		}
		list_add(&lfcc->list, &list);
	}

	error = gfs2_glock_nq_init(sdp->sd_trans_gl, LM_ST_DEFERRED,
			       LM_FLAG_PRIORITY | GL_NEVER_RECURSE | GL_NOCACHE,
			       t_gh);

	list_for_each_entry(jd, &sdp->sd_jindex_list, jd_list) {
		error = gfs2_jdesc_check(jd);
		if (error)
			break;
		error = gfs2_find_jhead(jd, &lh);
		if (error)
			break;
		if (!(lh.lh_flags & GFS2_LOG_HEAD_UNMOUNT)) {
			error = -EBUSY;
			break;
		}
	}

	if (error)
		gfs2_glock_dq_uninit(t_gh);

 out:
	while (!list_empty(&list)) {
		lfcc = list_entry(list.next, struct lfcc, list);
		list_del(&lfcc->list);
		gfs2_glock_dq_uninit(&lfcc->gh);
		kfree(lfcc);
	}
	gfs2_glock_dq_uninit(&ji_gh);

	return error;
}

/**
 * gfs2_freeze_fs - freezes the file system
 * @sdp: the file system
 *
 * This function flushes data and meta data for all machines by
 * aquiring the transaction log exclusively.  All journals are
 * ensured to be in a clean state as well.
 *
 * Returns: errno
 */

int gfs2_freeze_fs(struct gfs2_sbd *sdp)
{
	int error = 0;

	mutex_lock(&sdp->sd_freeze_lock);

	if (!sdp->sd_freeze_count++) {
		error = gfs2_lock_fs_check_clean(sdp, &sdp->sd_freeze_gh);
		if (error)
			sdp->sd_freeze_count--;
	}

	mutex_unlock(&sdp->sd_freeze_lock);

	return error;
}

/**
 * gfs2_unfreeze_fs - unfreezes the file system
 * @sdp: the file system
 *
 * This function allows the file system to proceed by unlocking
 * the exclusively held transaction lock.  Other GFS2 nodes are
 * now free to acquire the lock shared and go on with their lives.
 *
 */

void gfs2_unfreeze_fs(struct gfs2_sbd *sdp)
{
	mutex_lock(&sdp->sd_freeze_lock);

	if (sdp->sd_freeze_count && !--sdp->sd_freeze_count)
		gfs2_glock_dq_uninit(&sdp->sd_freeze_gh);

	mutex_unlock(&sdp->sd_freeze_lock);
}

