/*
 *   Copyright (C) International Business Machines Corp., 2000-2004
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or 
 *   (at your option) any later version.
 * 
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software 
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/fs.h>
#include <linux/quotaops.h>
#include "jfs_incore.h"
#include "jfs_inode.h"
#include "jfs_filsys.h"
#include "jfs_imap.h"
#include "jfs_dinode.h"
#include "jfs_debug.h"


void jfs_set_inode_flags(struct inode *inode)
{
	unsigned int flags = JFS_IP(inode)->mode2;

	inode->i_flags &= ~(S_IMMUTABLE | S_APPEND |
		S_NOATIME | S_DIRSYNC | S_SYNC);

	if (flags & JFS_IMMUTABLE_FL)
		inode->i_flags |= S_IMMUTABLE;
	if (flags & JFS_APPEND_FL)
		inode->i_flags |= S_APPEND;
	if (flags & JFS_NOATIME_FL)
		inode->i_flags |= S_NOATIME;
	if (flags & JFS_DIRSYNC_FL)
		inode->i_flags |= S_DIRSYNC;
	if (flags & JFS_SYNC_FL)
		inode->i_flags |= S_SYNC;
}

/*
 * NAME:	ialloc()
 *
 * FUNCTION:	Allocate a new inode
 *
 */
struct inode *ialloc(struct inode *parent, umode_t mode)
{
	struct super_block *sb = parent->i_sb;
	struct inode *inode;
	struct jfs_inode_info *jfs_inode;
	int rc;

	inode = new_inode(sb);
	if (!inode) {
		jfs_warn("ialloc: new_inode returned NULL!");
		return inode;
	}

	jfs_inode = JFS_IP(inode);

	rc = diAlloc(parent, S_ISDIR(mode), inode);
	if (rc) {
		jfs_warn("ialloc: diAlloc returned %d!", rc);
		make_bad_inode(inode);
		iput(inode);
		return NULL;
	}

	inode->i_uid = current->fsuid;
	if (parent->i_mode & S_ISGID) {
		inode->i_gid = parent->i_gid;
		if (S_ISDIR(mode))
			mode |= S_ISGID;
	} else
		inode->i_gid = current->fsgid;

	/*
	 * New inodes need to save sane values on disk when
	 * uid & gid mount options are used
	 */
	jfs_inode->saved_uid = inode->i_uid;
	jfs_inode->saved_gid = inode->i_gid;

	/*
	 * Allocate inode to quota.
	 */
	if (DQUOT_ALLOC_INODE(inode)) {
		DQUOT_DROP(inode);
		inode->i_flags |= S_NOQUOTA;
		inode->i_nlink = 0;
		iput(inode);
		return NULL;
	}

	inode->i_mode = mode;
	/* inherit flags from parent */
	jfs_inode->mode2 = JFS_IP(parent)->mode2 & JFS_FL_INHERIT;

	if (S_ISDIR(mode)) {
		jfs_inode->mode2 |= IDIRECTORY;
		jfs_inode->mode2 &= ~JFS_DIRSYNC_FL;
	}
	else {
		jfs_inode->mode2 |= INLINEEA | ISPARSE;
		if (S_ISLNK(mode))
			jfs_inode->mode2 &= ~(JFS_IMMUTABLE_FL|JFS_APPEND_FL);
	}
	jfs_inode->mode2 |= mode;

	inode->i_blksize = sb->s_blocksize;
	inode->i_blocks = 0;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	jfs_inode->otime = inode->i_ctime.tv_sec;
	inode->i_generation = JFS_SBI(sb)->gengen++;

	jfs_inode->cflag = 0;

	/* Zero remaining fields */
	memset(&jfs_inode->acl, 0, sizeof(dxd_t));
	memset(&jfs_inode->ea, 0, sizeof(dxd_t));
	jfs_inode->next_index = 0;
	jfs_inode->acltype = 0;
	jfs_inode->btorder = 0;
	jfs_inode->btindex = 0;
	jfs_inode->bxflag = 0;
	jfs_inode->blid = 0;
	jfs_inode->atlhead = 0;
	jfs_inode->atltail = 0;
	jfs_inode->xtlid = 0;
	jfs_set_inode_flags(inode);

	jfs_info("ialloc returns inode = 0x%p\n", inode);

	return inode;
}
