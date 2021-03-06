/*
 * Copyright (c) 2000-2002 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.	 Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */

#include <xfs.h>

#ifdef CONFIG_PROC_FS
struct xfsstats xfsstats;
#endif

/*
 * xfs_get_dir_entry is used to get a reference to an inode given
 * its parent directory inode and the name of the file.	 It does
 * not lock the child inode, and it unlocks the directory before
 * returning.  The directory's generation number is returned for
 * use by a later call to xfs_lock_dir_and_entry.
 */
int
xfs_get_dir_entry(
	struct dentry		*dentry,
	xfs_inode_t		**ipp)
{
	vnode_t			*vp;
	bhv_desc_t		*bdp;

	ASSERT(dentry->d_inode);

	vp = LINVFS_GET_VP(dentry->d_inode);
	bdp = vn_bhv_lookup_unlocked(VN_BHV_HEAD(vp), &xfs_vnodeops);
	if (!bdp) {
		*ipp = NULL;
		return XFS_ERROR(ENOENT);
	}
	VN_HOLD(vp);
	*ipp = XFS_BHVTOI(bdp);
	return 0;
}

/*
 * Wrapper around xfs_dir_lookup.
 *
 * If DLF_IGET is set, then this routine will also return the inode.
 * Note that the inode will not be locked. Note, however, that the
 * vnode will have an additional reference in this case.
 */
int
xfs_dir_lookup_int(
	bhv_desc_t		*dir_bdp,
	int			flags,
	struct dentry		*dentry,
	xfs_ino_t		*inum,
	xfs_inode_t		**ipp)
{
	vnode_t		*dir_vp;
	xfs_inode_t	*dp;
	char		*name = (char *) dentry->d_name.name;
	int		name_len = dentry->d_name.len;
	int		error;
	int		do_iget;
	uint		lock_mode;
	bhv_desc_t	*bdp;

	dir_vp = BHV_TO_VNODE(dir_bdp);
	vn_trace_entry(dir_vp, "xfs_dir_lookup_int",
		       (inst_t *)__return_address);

	do_iget = flags & DLF_IGET;
	error = 0;

	if (flags & DLF_LOCK_SHARED) {
		lock_mode = XFS_ILOCK_SHARED;
	} else {
		lock_mode = XFS_ILOCK_EXCL;
	}

	dp = XFS_BHVTOI(dir_bdp);
	bdp = NULL;

	/*
	 * If all else fails, call the directory code.
	 */

	error = XFS_DIR_LOOKUP(dp->i_mount, NULL, dp, name, name_len, inum);
	if (!error && do_iget) {
		/*
		 * Unlock the directory. We do this because we can't
		 * hold the directory lock while doing the vn_get()
		 * in xfs_iget().  Doing so could cause us to hold
		 * a lock while waiting for the inode to finish
		 * being inactive while it's waiting for a log
		 * reservation in the inactive routine.
		 */
		xfs_iunlock(dp, lock_mode);

		if (bdp) {
			VN_RELE(BHV_TO_VNODE(bdp));
			bdp = NULL;
		}

		error = xfs_iget(dp->i_mount, NULL, *inum, 0, ipp, 0);

		xfs_ilock(dp, lock_mode);

		if (error) {
			*ipp = NULL;
			return error;
		}

		if ((*ipp)->i_d.di_mode == 0) {
			/*
			 * The inode has been freed.  Something is
			 * wrong so just get out of here.
			 */
			xfs_iunlock(dp, lock_mode);
			xfs_iput_new(*ipp, 0);
			*ipp = NULL;
			xfs_ilock(dp, lock_mode);
			error = XFS_ERROR(ENOENT);
		} else {
			bdp = XFS_ITOBHV(*ipp);
			bdp = NULL;
		}
	}
	if (bdp) {
		/* The only time we should get here is if the dir_lookup
		 * failed.
		 */
		ASSERT(error);
		xfs_iunlock(dp, lock_mode);
		VN_RELE(BHV_TO_VNODE(bdp));
		xfs_ilock(dp, lock_mode);
	}
	return error;
}

/*
 * Allocates a new inode from disk and return a pointer to the
 * incore copy. This routine will internally commit the current
 * transaction and allocate a new one if the Space Manager needed
 * to do an allocation to replenish the inode free-list.
 *
 * This routine is designed to be called from xfs_create and
 * xfs_create_dir.
 *
 */
int
xfs_dir_ialloc(
	xfs_trans_t	**tpp,		/* input: current transaction;
					   output: may be a new transaction. */
	xfs_inode_t	*dp,		/* directory within whose allocate
					   the inode. */
	mode_t		mode,
	nlink_t		nlink,
	dev_t		rdev,
	cred_t		*credp,
	prid_t		prid,		/* project id */
	int		okalloc,	/* ok to allocate new space */
	xfs_inode_t	**ipp,		/* pointer to inode; it will be
					   locked. */
	int		*committed)

{
	xfs_trans_t	*tp;
	xfs_trans_t	*ntp;
	xfs_inode_t	*ip;
	xfs_buf_t	*ialloc_context = NULL;
	boolean_t	call_again = B_FALSE;
	int		code;
	uint		log_res;
	uint		log_count;
	void		*dqinfo;
	uint		tflags;

	tp = *tpp;
	ASSERT(tp->t_flags & XFS_TRANS_PERM_LOG_RES);

	/*
	 * xfs_ialloc will return a pointer to an incore inode if
	 * the Space Manager has an available inode on the free
	 * list. Otherwise, it will do an allocation and replenish
	 * the freelist.  Since we can only do one allocation per
	 * transaction without deadlocks, we will need to commit the
	 * current transaction and start a new one.  We will then
	 * need to call xfs_ialloc again to get the inode.
	 *
	 * If xfs_ialloc did an allocation to replenish the freelist,
	 * it returns the bp containing the head of the freelist as
	 * ialloc_context. We will hold a lock on it across the
	 * transaction commit so that no other process can steal
	 * the inode(s) that we've just allocated.
	 */
	code = xfs_ialloc(tp, dp, mode, nlink, rdev, credp, prid, okalloc,
			  &ialloc_context, &call_again, &ip);

	/*
	 * Return an error if we were unable to allocate a new inode.
	 * This should only happen if we run out of space on disk or
	 * encounter a disk error.
	 */
	if (code) {
		*ipp = NULL;
		return code;
	}
	if (!call_again && (ip == NULL)) {
		*ipp = NULL;
		return XFS_ERROR(ENOSPC);
	}

	/*
	 * If call_again is set, then we were unable to get an
	 * inode in one operation.  We need to commit the current
	 * transaction and call xfs_ialloc() again.  It is guaranteed
	 * to succeed the second time.
	 */
	if (call_again) {

		/*
		 * Normally, xfs_trans_commit releases all the locks.
		 * We call bhold to hang on to the ialloc_context across
		 * the commit.	Holding this buffer prevents any other
		 * processes from doing any allocations in this
		 * allocation group.
		 */
		xfs_trans_bhold(tp, ialloc_context);
		/*
		 * Save the log reservation so we can use
		 * them in the next transaction.
		 */
		log_res = xfs_trans_get_log_res(tp);
		log_count = xfs_trans_get_log_count(tp);

		/*
		 * We want the quota changes to be associated with the next
		 * transaction, NOT this one. So, detach the dqinfo from this
		 * and attach it to the next transaction.
		 */
		dqinfo = NULL;
		tflags = 0;
		if (tp->t_dqinfo) {
			dqinfo = (void *)tp->t_dqinfo;
			tp->t_dqinfo = NULL;
			tflags = tp->t_flags & XFS_TRANS_DQ_DIRTY;
			tp->t_flags &= ~(XFS_TRANS_DQ_DIRTY);
		}

		ntp = xfs_trans_dup(tp);
		code = xfs_trans_commit(tp, 0, NULL);
		tp = ntp;
		if (committed != NULL) {
			*committed = 1;
		}
		/*
		 * If we get an error during the commit processing,
		 * release the buffer that is still held and return
		 * to the caller.
		 */
		if (code) {
			xfs_buf_relse(ialloc_context);
			if (dqinfo) {
				tp->t_dqinfo = dqinfo;
				xfs_trans_free_dqinfo(tp);
			}
			*tpp = ntp;
			*ipp = NULL;
			return code;
		}
		code = xfs_trans_reserve(tp, 0, log_res, 0,
					 XFS_TRANS_PERM_LOG_RES, log_count);
		/*
		 * Re-attach the quota info that we detached from prev trx.
		 */
		if (dqinfo) {
			tp->t_dqinfo = dqinfo;
			tp->t_flags |= tflags;
		}

		if (code) {
			xfs_buf_relse(ialloc_context);
			*tpp = ntp;
			*ipp = NULL;
			return code;
		}
		xfs_trans_bjoin (tp, ialloc_context);

		/*
		 * Call ialloc again. Since we've locked out all
		 * other allocations in this allocation group,
		 * this call should always succeed.
		 */
		code = xfs_ialloc(tp, dp, mode, nlink, rdev, credp, prid,
				  okalloc, &ialloc_context, &call_again, &ip);

		/*
		 * If we get an error at this point, return to the caller
		 * so that the current transaction can be aborted.
		 */
		if (code) {
			*tpp = tp;
			*ipp = NULL;
			return code;
		}
		ASSERT ((!call_again) && (ip != NULL));

	} else {
		if (committed != NULL) {
			*committed = 0;
		}
	}

	*ipp = ip;
	*tpp = tp;

	return 0;
}

/*
 * Decrement the link count on an inode & log the change.
 * If this causes the link count to go to zero, initiate the
 * logging activity required to truncate a file.
 */
int				/* error */
xfs_droplink(
	xfs_trans_t *tp,
	xfs_inode_t *ip)
{
	int	error;

	xfs_ichgtime(ip, XFS_ICHGTIME_CHG);

	ASSERT (ip->i_d.di_nlink > 0);
	ip->i_d.di_nlink--;
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);

	error = 0;
	if (ip->i_d.di_nlink == 0) {
		/*
		 * We're dropping the last link to this file.
		 * Move the on-disk inode to the AGI unlinked list.
		 * From xfs_inactive() we will pull the inode from
		 * the list and free it.
		 */
		error = xfs_iunlink(tp, ip);
	}
	return error;
}

/*
 * This gets called when the inode's version needs to be changed from 1 to 2.
 * Currently this happens when the nlink field overflows the old 16-bit value
 * or when chproj is called to change the project for the first time.
 * As a side effect the superblock version will also get rev'd
 * to contain the NLINK bit.
 */
void
xfs_bump_ino_vers2(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip)
{
	xfs_mount_t	*mp;
	unsigned long		s;

	ASSERT(ismrlocked (&ip->i_lock, MR_UPDATE));
	ASSERT(ip->i_d.di_version == XFS_DINODE_VERSION_1);

	ip->i_d.di_version = XFS_DINODE_VERSION_2;
	ip->i_d.di_onlink = 0;
	bzero(&(ip->i_d.di_pad[0]), sizeof(ip->i_d.di_pad));
	mp = tp->t_mountp;
	if (!XFS_SB_VERSION_HASNLINK(&mp->m_sb)) {
		s = XFS_SB_LOCK(mp);
		if (!XFS_SB_VERSION_HASNLINK(&mp->m_sb)) {
			XFS_SB_VERSION_ADDNLINK(&mp->m_sb);
			XFS_SB_UNLOCK(mp, s);
			xfs_mod_sb(tp, XFS_SB_VERSIONNUM);
		} else {
			XFS_SB_UNLOCK(mp, s);
		}
	}
	/* Caller must log the inode */
}

/*
 * Increment the link count on an inode & log the change.
 */
int
xfs_bumplink(
	xfs_trans_t *tp,
	xfs_inode_t *ip)
{
	if (ip->i_d.di_nlink >= XFS_MAXLINK)
		return XFS_ERROR(EMLINK);
	xfs_ichgtime(ip, XFS_ICHGTIME_CHG);

	ASSERT(ip->i_d.di_nlink > 0);
	ip->i_d.di_nlink++;
	if ((ip->i_d.di_version == XFS_DINODE_VERSION_1) &&
	    (ip->i_d.di_nlink > XFS_MAXLINK_1)) {
		/*
		 * The inode has increased its number of links beyond
		 * what can fit in an old format inode.	 It now needs
		 * to be converted to a version 2 inode with a 32 bit
		 * link count.	If this is the first inode in the file
		 * system to do this, then we need to bump the superblock
		 * version number as well.
		 */
		xfs_bump_ino_vers2(tp, ip);
	}

	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	return 0;
}

/*
 * Try to truncate the given file to 0 length.	Currently called
 * only out of xfs_remove when it has to truncate a file to free
 * up space for the remove to proceed.
 */
int
xfs_truncate_file(
	xfs_mount_t	*mp,
	xfs_inode_t	*ip)
{
	xfs_trans_t	*tp;
	int		error;

#ifdef QUOTADEBUG
	/*
	 * This is called to truncate the quotainodes too.
	 */
	if (XFS_IS_UQUOTA_ON(mp)) {
		if (ip->i_ino != mp->m_sb.sb_uquotino)
			ASSERT(ip->i_udquot);
	}
	if (XFS_IS_GQUOTA_ON(mp)) {
		if (ip->i_ino != mp->m_sb.sb_gquotino)
			ASSERT(ip->i_gdquot);
	}
#endif
	/*
	 * Make the call to xfs_itruncate_start before starting the
	 * transaction, because we cannot make the call while we're
	 * in a transaction.
	 */
	xfs_ilock(ip, XFS_IOLOCK_EXCL);
	xfs_itruncate_start(ip, XFS_ITRUNC_DEFINITE, (xfs_fsize_t)0);

	tp = xfs_trans_alloc(mp, XFS_TRANS_TRUNCATE_FILE);
	if ((error = xfs_trans_reserve(tp, 0, XFS_ITRUNCATE_LOG_RES(mp), 0,
				      XFS_TRANS_PERM_LOG_RES,
				      XFS_ITRUNCATE_LOG_COUNT))) {
		xfs_trans_cancel(tp, 0);
		xfs_iunlock(ip, XFS_IOLOCK_EXCL);
		return error;
	}

	/*
	 * Follow the normal truncate locking protocol.	 Since we
	 * hold the inode in the transaction, we know that it's number
	 * of references will stay constant.
	 */
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);
	xfs_trans_ihold(tp, ip);
	/*
	 * Signal a sync xaction.  The only case where that isn't
	 * the case is if we're truncating an already unlinked file
	 * on a wsync fs.  In that case, we know the blocks can't
	 * reappear in the file because the links to file are
	 * permanently toast.  Currently, we're always going to
	 * want a sync transaction because this code is being
	 * called from places where nlink is guaranteed to be 1
	 * but I'm leaving the tests in to protect against future
	 * changes -- rcc.
	 */
	error = xfs_itruncate_finish(&tp, ip, (xfs_fsize_t)0,
				     XFS_DATA_FORK,
				     ((ip->i_d.di_nlink != 0 ||
				       !(mp->m_flags & XFS_MOUNT_WSYNC))
				      ? 1 : 0));
	if (error) {
		xfs_trans_cancel(tp, XFS_TRANS_RELEASE_LOG_RES |
				 XFS_TRANS_ABORT);
	} else {
		xfs_ichgtime(ip, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
		error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES,
					 NULL);
	}
	xfs_iunlock(ip, XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);

	return error;
}
