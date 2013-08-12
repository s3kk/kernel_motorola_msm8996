/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifndef __XFS_BMAP_UTIL_H__
#define	__XFS_BMAP_UTIL_H__

/* Kernel only BMAP related definitions and functions */

struct xfs_bmbt_irec;
struct xfs_ifork;
struct xfs_inode;
struct xfs_mount;
struct xfs_trans;

/*
 * Argument structure for xfs_bmap_alloc.
 */
struct xfs_bmalloca {
	xfs_fsblock_t		*firstblock; /* i/o first block allocated */
	struct xfs_bmap_free	*flist;	/* bmap freelist */
	struct xfs_trans	*tp;	/* transaction pointer */
	struct xfs_inode	*ip;	/* incore inode pointer */
	struct xfs_bmbt_irec	prev;	/* extent before the new one */
	struct xfs_bmbt_irec	got;	/* extent after, or delayed */

	xfs_fileoff_t		offset;	/* offset in file filling in */
	xfs_extlen_t		length;	/* i/o length asked/allocated */
	xfs_fsblock_t		blkno;	/* starting block of new extent */

	struct xfs_btree_cur	*cur;	/* btree cursor */
	xfs_extnum_t		idx;	/* current extent index */
	int			nallocs;/* number of extents alloc'd */
	int			logflags;/* flags for transaction logging */

	xfs_extlen_t		total;	/* total blocks needed for xaction */
	xfs_extlen_t		minlen;	/* minimum allocation size (blocks) */
	xfs_extlen_t		minleft; /* amount must be left after alloc */
	char			eof;	/* set if allocating past last extent */
	char			wasdel;	/* replacing a delayed allocation */
	char			userdata;/* set if is user data */
	char			aeof;	/* allocated space at eof */
	char			conv;	/* overwriting unwritten extents */
	char			stack_switch;
	int			flags;
	struct completion	*done;
	struct work_struct	work;
	int			result;
};

int	xfs_bmap_finish(struct xfs_trans **tp, struct xfs_bmap_free *flist,
			int *committed);
int	xfs_bmap_rtalloc(struct xfs_bmalloca *ap);
int	xfs_bmapi_allocate(struct xfs_bmalloca *args);
int	__xfs_bmapi_allocate(struct xfs_bmalloca *args);
int	xfs_bmap_eof(struct xfs_inode *ip, xfs_fileoff_t endoff,
		     int whichfork, int *eof);
int	xfs_bmap_count_blocks(struct xfs_trans *tp, struct xfs_inode *ip,
			      int whichfork, int *count);
int	xfs_bmap_punch_delalloc_range(struct xfs_inode *ip,
		xfs_fileoff_t start_fsb, xfs_fileoff_t length);

/* bmap to userspace formatter - copy to user & advance pointer */
typedef int (*xfs_bmap_format_t)(void **, struct getbmapx *, int *);
int	xfs_getbmap(struct xfs_inode *ip, struct getbmapx *bmv,
		xfs_bmap_format_t formatter, void *arg);

/* functions in xfs_bmap.c that are only needed by xfs_bmap_util.c */
void	xfs_bmap_del_free(struct xfs_bmap_free *flist,
			  struct xfs_bmap_free_item *prev,
			  struct xfs_bmap_free_item *free);
int	xfs_bmap_extsize_align(struct xfs_mount *mp, struct xfs_bmbt_irec *gotp,
			       struct xfs_bmbt_irec *prevp, xfs_extlen_t extsz,
			       int rt, int eof, int delay, int convert,
			       xfs_fileoff_t *offp, xfs_extlen_t *lenp);
void	xfs_bmap_adjacent(struct xfs_bmalloca *ap);
int	xfs_bmap_last_extent(struct xfs_trans *tp, struct xfs_inode *ip,
			     int whichfork, struct xfs_bmbt_irec *rec,
			     int *is_empty);

xfs_daddr_t xfs_fsb_to_db(struct xfs_inode *ip, xfs_fsblock_t fsb);

#endif	/* __XFS_BMAP_UTIL_H__ */
