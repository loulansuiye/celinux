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
#include <linux/dcache.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/mman.h> /* for PROT_WRITE */

static struct vm_operations_struct linvfs_file_vm_ops;


STATIC ssize_t
linvfs_read(
	struct file	*filp,
	char		*buf,
	size_t		size,
	loff_t		*offset)
{
	vnode_t		*vp;
	int		error;

	vp = LINVFS_GET_VP(filp->f_dentry->d_inode);
	ASSERT(vp);

	VOP_READ(vp, filp, buf, size, offset, NULL, error);

	return(error);
}


STATIC ssize_t
linvfs_write(
	struct file	*file,
	const char	*buf,
	size_t		count,
	loff_t		*ppos)
{
	struct inode	*inode = file->f_dentry->d_inode;
	loff_t		pos;
	vnode_t		*vp;
	int		err;	/* Use negative errors in this f'n */

	if ((ssize_t) count < 0)
		return -EINVAL;

	if (!access_ok(VERIFY_READ, buf, count))
		return -EFAULT;

	pos = *ppos;
	err = -EINVAL;
	if (pos < 0)
		goto out;

	err = file->f_error;
	if (err) {
		file->f_error = 0;
		goto out;
	}

	vp = LINVFS_GET_VP(inode);
	ASSERT(vp);

	/* We allow multiple direct writers in, there is no
	 * potential call to vmtruncate in that path.
	 */
	if (!(file->f_flags & O_DIRECT))
		down(&inode->i_sem);

	VOP_WRITE(vp, file, buf, count, &pos, NULL, err);
	*ppos = pos;

	if (!(file->f_flags & O_DIRECT))
		up(&inode->i_sem);
out:

	return(err);
}


STATIC int
linvfs_open(
	struct inode	*inode,
	struct file	*filp)
{
	vnode_t		*vp = LINVFS_GET_VP(inode);
	int		error;

	if (!(filp->f_flags & O_LARGEFILE) && inode->i_size > MAX_NON_LFS)
		return -EFBIG;

	ASSERT(vp);
	VOP_OPEN(vp, NULL, error);
	return -error;
}


STATIC int
linvfs_release(
	struct inode	*inode,
	struct file	*filp)
{
	vnode_t		*vp = LINVFS_GET_VP(inode);
	int		error = 0;

	if (vp)
		VOP_RELEASE(vp, error);
	return -error;
}


STATIC int
linvfs_fsync(
	struct file	*filp,
	struct dentry	*dentry,
	int		datasync)
{
	struct inode	*inode = dentry->d_inode;
	vnode_t		*vp = LINVFS_GET_VP(inode);
	int		error;
	int		flags = FSYNC_WAIT;

	if (datasync)
		flags |= FSYNC_DATA;

	ASSERT(vp);

	VOP_FSYNC(vp, flags, NULL, (off_t)0, (off_t)-1, error);

	return -error;
}

/*
 * linvfs_readdir maps to VOP_READDIR().
 * We need to build a uio, cred, ...
 */

#define nextdp(dp)	((struct xfs_dirent *)((char *)(dp) + (dp)->d_reclen))

STATIC int
linvfs_readdir(
	struct file	*filp,
	void		*dirent,
	filldir_t	filldir)
{
	int		error = 0;
	vnode_t		*vp;
	uio_t		uio;
	iovec_t		iov;
	int		eof = 0;
	caddr_t		read_buf;
	int		namelen, size = 0;
	size_t		rlen = PAGE_CACHE_SIZE << 2;
	off_t		start_offset;
	xfs_dirent_t	*dbp = NULL;

	vp = LINVFS_GET_VP(filp->f_dentry->d_inode);
	ASSERT(vp);

	/* Try fairly hard to get memory */
	do {
		if ((read_buf = (caddr_t)kmalloc(rlen, GFP_KERNEL)))
			break;
		rlen >>= 1;
	} while (rlen >= 1024);

	if (read_buf == NULL)
		return -ENOMEM;

	uio.uio_iov = &iov;
	uio.uio_fmode = filp->f_mode;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_offset = filp->f_pos;

	while (!eof) {
		uio.uio_resid = iov.iov_len = rlen;
		iov.iov_base = read_buf;
		uio.uio_iovcnt = 1;

		start_offset = uio.uio_offset;

		VOP_READDIR(vp, &uio, NULL, &eof, error);
		if ((uio.uio_offset == start_offset) || error) {
			size = 0;
			break;
		}

		size = rlen - uio.uio_resid;
		dbp = (xfs_dirent_t *)read_buf;
		while (size > 0) {
			namelen = strlen(dbp->d_name);

			if (filldir(dirent, dbp->d_name, namelen,
					(loff_t) dbp->d_off,
					(ino_t) dbp->d_ino,
					DT_UNKNOWN)) {
				goto done;
			}
			size -= dbp->d_reclen;
			dbp = nextdp(dbp);
		}
	}
done:
	if (!error) {
		if (size == 0)
			filp->f_pos = uio.uio_offset;
		else if (dbp)
			filp->f_pos = dbp->d_off;
	}

	kfree(read_buf);
	return -error;
}

STATIC int
linvfs_file_mmap(
	struct file	*filp,
	struct vm_area_struct *vma)
{
	struct inode	*ip = filp->f_dentry->d_inode;
	vnode_t		*vp = LINVFS_GET_VP(ip);
	vattr_t		va = { .va_mask = AT_UPDATIME };
	int		error;

	if ((vp->v_type == VREG) && (vp->v_vfsp->vfs_flag & VFS_DMI)) {
		error = -xfs_dm_send_mmap_event(vma, 0);
		if (error)
			return error;
	}

	vma->vm_ops = &linvfs_file_vm_ops;

	VOP_SETATTR(vp, &va, AT_UPDATIME, NULL, error);
	UPDATE_ATIME(ip);
	return 0;
}


STATIC int
linvfs_ioctl(
	struct inode	*inode,
	struct file	*filp,
	unsigned int	cmd,
	unsigned long	arg)
{
	int		error;
	vnode_t		*vp = LINVFS_GET_VP(inode);

	ASSERT(vp);
	VOP_IOCTL(vp, inode, filp, cmd, arg, error);
	VMODIFY(vp);

	/* NOTE:  some of the ioctl's return positive #'s as a
	 *	  byte count indicating success, such as
	 *	  readlink_by_handle.  So we don't "sign flip"
	 *	  like most other routines.  This means true
	 *	  errors need to be returned as a negative value.
	 */
	return error;
}

#ifdef HAVE_VMOP_MPROTECT
STATIC int
linvfs_mprotect(
	struct vm_area_struct *vma,
	unsigned int	newflags)
{
	vnode_t		*vp = LINVFS_GET_VP(vma->vm_file->f_dentry->d_inode);
	int		error = 0;

	if ((vp->v_type == VREG) && (vp->v_vfsp->vfs_flag & VFS_DMI)) {
		if ((vma->vm_flags & VM_MAYSHARE) &&
		    (newflags & PROT_WRITE) && !(vma->vm_flags & PROT_WRITE)){
			error = xfs_dm_send_mmap_event(vma, VM_WRITE);
		    }
	}
	return error;
}
#endif /* HAVE_VMOP_MPROTECT */


struct file_operations linvfs_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= linvfs_read,
	.write		= linvfs_write,
	.ioctl		= linvfs_ioctl,
	.mmap		= linvfs_file_mmap,
	.open		= linvfs_open,
	.release	= linvfs_release,
	.fsync		= linvfs_fsync,
};

struct file_operations linvfs_dir_operations = {
	.read		= generic_read_dir,
	.readdir	= linvfs_readdir,
	.ioctl		= linvfs_ioctl,
	.fsync		= linvfs_fsync,
};

static struct vm_operations_struct linvfs_file_vm_ops = {
	.nopage		= filemap_nopage,
#ifdef HAVE_VMOP_MPROTECT
	.mprotect	= linvfs_mprotect,
#endif
};
