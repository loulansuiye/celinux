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
#include <linux/locks.h>
#include <linux/xattr.h>


/*
 * Pull the link count and size up from the xfs inode to the linux inode
 */
STATIC void
validate_fields(
	struct inode	*ip)
{
	vnode_t		*vp = LINVFS_GET_VP(ip);
	vattr_t		va;
	int		error;

	va.va_mask = AT_NLINK|AT_SIZE;
	VOP_GETATTR(vp, &va, ATTR_LAZY, NULL, error);
	ip->i_nlink = va.va_nlink;
	ip->i_size = va.va_size;
	ip->i_blocks = va.va_nblocks;
}

#ifdef CONFIG_FS_POSIX_ACL
/*
 * Determine whether a process has a valid fs_struct (kernel daemons
 * like knfsd don't have an fs_struct).
 */
STATIC int inline
has_fs_struct(struct task_struct *task)
{
	return (task->fs != init_task.fs);
}
#endif

STATIC int
linvfs_mknod(
	struct inode	*dir,
	struct dentry	*dentry,
	int		mode,
	int		rdev)
{
	struct inode	*ip;
	vattr_t		va;
	vnode_t		*vp = NULL, *dvp = LINVFS_GET_VP(dir);
	xattr_exists_t	test_default_acl = _ACL_DEFAULT_EXISTS;
	int		have_default_acl = 0;
	int		error = EINVAL;

	if (test_default_acl)
		have_default_acl = test_default_acl(dvp);

#ifdef CONFIG_FS_POSIX_ACL
	/*
	 * Conditionally compiled so that the ACL base kernel changes can be
	 * split out into separate patches - remove this once MS_POSIXACL is
	 * accepted, or some other way to implement this exists.
	 */
	if (IS_POSIXACL(dir) && !have_default_acl && has_fs_struct(current))
		mode &= ~current->fs->umask;
#endif

	bzero(&va, sizeof(va));
	va.va_mask = AT_TYPE|AT_MODE;
	va.va_type = IFTOVT(mode);
	va.va_mode = mode;

	switch (mode & S_IFMT) {
	case S_IFCHR: case S_IFBLK: case S_IFIFO: case S_IFSOCK:
		va.va_rdev = rdev;
		va.va_mask |= AT_RDEV;
		/*FALLTHROUGH*/
	case S_IFREG:
		VOP_CREATE(dvp, dentry, &va, &vp, NULL, error);
		break;
	case S_IFDIR:
		VOP_MKDIR(dvp, dentry, &va, &vp, NULL, error);
		break;
	default:
		error = EINVAL;
		break;
	}

	if (!error) {
		ASSERT(vp);
		ip = LINVFS_GET_IP(vp);
		if (!ip) {
			VN_RELE(vp);
			return -ENOMEM;
		}

		if (S_ISCHR(mode) || S_ISBLK(mode))
			ip->i_rdev = to_kdev_t(rdev);
		/* linvfs_revalidate_core returns (-) errors */
		error = -linvfs_revalidate_core(ip, ATTR_COMM);
		validate_fields(dir);
		d_instantiate(dentry, ip);
		mark_inode_dirty_sync(ip);
		mark_inode_dirty_sync(dir);
	}

	if (!error && have_default_acl) {
		_ACL_DECL	(pdacl);

		if (!_ACL_ALLOC(pdacl)) {
			error = -ENOMEM;
		} else {
			if (_ACL_GET_DEFAULT(dvp, pdacl))
				error = _ACL_INHERIT(vp, &va, pdacl);
			VMODIFY(vp);
			_ACL_FREE(pdacl);
		}
	}
	return -error;
}

STATIC int
linvfs_create(
	struct inode	*dir,
	struct dentry	*dentry,
	int		mode)
{
	return linvfs_mknod(dir, dentry, mode, 0);
}

STATIC int
linvfs_mkdir(
	struct inode	*dir,
	struct dentry	*dentry,
	int		mode)
{
	return linvfs_mknod(dir, dentry, mode|S_IFDIR, 0);
}


STATIC struct dentry *
linvfs_lookup(
	struct inode	*dir,
	struct dentry	*dentry)
{
	int		error;
	vnode_t		*vp, *cvp;
	struct inode	*ip = NULL;

	if (dentry->d_name.len >= MAXNAMELEN)
		return ERR_PTR(-ENAMETOOLONG);

	cvp = NULL;
	vp = LINVFS_GET_VP(dir);
	VOP_LOOKUP(vp, dentry, &cvp, 0, NULL, NULL, error);
	if (!error) {
		ASSERT(cvp);
		ip = LINVFS_GET_IP(cvp);
		if (!ip) {
			VN_RELE(cvp);
			return ERR_PTR(-EACCES);
		}
		error = -linvfs_revalidate_core(ip, ATTR_COMM);
	}
	if (error && (error != ENOENT))
		return ERR_PTR(-error);
	d_add(dentry, ip);	/* Negative entry goes in if ip is NULL */
	return NULL;
}

STATIC int
linvfs_link(
	struct dentry	*old_dentry,
	struct inode	*dir,
	struct dentry	*dentry)
{
	int		error;
	vnode_t		*tdvp;	/* Target directory for new name/link */
	vnode_t		*vp;	/* vp of name being linked */
	struct inode	*ip;	/* inode of guy being linked to */

	ip = old_dentry->d_inode;	/* inode being linked to */
	if (S_ISDIR(ip->i_mode))
		return -EPERM;

	tdvp = LINVFS_GET_VP(dir);
	vp = LINVFS_GET_VP(ip);

	error = 0;
	VOP_LINK(tdvp, vp, dentry, NULL, error);
	if (!error) {
		VMODIFY(tdvp);
		VN_HOLD(vp);
		validate_fields(ip);
		d_instantiate(dentry, ip);
		mark_inode_dirty_sync(ip);
	}
	return -error;
}

STATIC int
linvfs_unlink(
	struct inode	*dir,
	struct dentry	*dentry)
{
	int		error = 0;
	struct inode	*inode;
	vnode_t		*dvp;	/* directory containing name to remove */

	inode = dentry->d_inode;

	dvp = LINVFS_GET_VP(dir);

	VOP_REMOVE(dvp, dentry, NULL, error);

	if (!error) {
		validate_fields(dir);	/* For size only */
		validate_fields(inode);
		mark_inode_dirty_sync(inode);
		mark_inode_dirty_sync(dir);
	}

	return -error;
}

STATIC int
linvfs_symlink(
	struct inode	*dir,
	struct dentry	*dentry,
	const char	*symname)
{
	int		error;
	vnode_t		*dvp;	/* directory containing name to remove */
	vnode_t		*cvp;	/* used to lookup symlink to put in dentry */
	vattr_t		va;
	struct inode	*ip = NULL;

	dvp = LINVFS_GET_VP(dir);

	bzero(&va, sizeof(va));
	va.va_type = VLNK;
	va.va_mode = irix_symlink_mode ? 0777 & ~current->fs->umask : S_IRWXUGO;
	va.va_mask = AT_TYPE|AT_MODE;

	error = 0;
	VOP_SYMLINK(dvp, dentry, &va, (char *)symname, &cvp, NULL, error);
	if (!error) {
		ASSERT(cvp);
		ASSERT(cvp->v_type == VLNK);
		ip = LINVFS_GET_IP(cvp);
		if (!ip) {
			error = ENOMEM;
			VN_RELE(cvp);
		} else {
			/* linvfs_revalidate_core returns (-) errors */
			error = -linvfs_revalidate_core(ip, ATTR_COMM);
			d_instantiate(dentry, ip);
			validate_fields(dir);
			validate_fields(ip); /* size needs update */
			mark_inode_dirty_sync(ip);
			mark_inode_dirty_sync(dir);
		}
	}
	return -error;
}

STATIC int
linvfs_rmdir(
	struct inode	*dir,
	struct dentry	*dentry)
{
	struct inode	*inode = dentry->d_inode;
	vnode_t		*dvp = LINVFS_GET_VP(dir);
	int		error;

	VOP_RMDIR(dvp, dentry, NULL, error);
	if (!error) {
		validate_fields(inode);
		validate_fields(dir);
		mark_inode_dirty_sync(inode);
		mark_inode_dirty_sync(dir);
	}
	return -error;
}

STATIC int
linvfs_rename(
	struct inode	*odir,
	struct dentry	*odentry,
	struct inode	*ndir,
	struct dentry	*ndentry)
{
	int		error;
	vnode_t		*fvp;	/* from directory */
	vnode_t		*tvp;	/* target directory */
	struct inode	*new_inode = NULL;

	fvp = LINVFS_GET_VP(odir);
	tvp = LINVFS_GET_VP(ndir);

	new_inode = ndentry->d_inode;

	VOP_RENAME(fvp, odentry, tvp, ndentry, NULL, error);
	if (error)
		return -error;

	if (new_inode) {
		validate_fields(new_inode);
	}

	validate_fields(odir);
	if (ndir != odir)
		validate_fields(ndir);
	mark_inode_dirty(ndir);
	return 0;
}

STATIC int
linvfs_readlink(
	struct dentry	*dentry,
	char		*buf,
	int		size)
{
	vnode_t		*vp;
	uio_t		uio;
	iovec_t		iov;
	int		error;

	vp = LINVFS_GET_VP(dentry->d_inode);

	iov.iov_base = buf;
	iov.iov_len = size;

	uio.uio_iov = &iov;
	uio.uio_offset = 0;
	uio.uio_segflg = UIO_USERSPACE;
	uio.uio_resid = size;

	VOP_READLINK(vp, &uio, NULL, error);
	if (error)
		return -error;

	return (size - uio.uio_resid);
}

/*
 * careful here - this function can get called recursively, so
 * we need to be very careful about how much stack we use.
 * uio is kmalloced for this reason...
 */
STATIC int
linvfs_follow_link(
	struct dentry		*dentry,
	struct nameidata	*nd)
{
	vnode_t			*vp;
	uio_t			*uio;
	iovec_t			iov;
	int			error;
	char			*link;

	ASSERT(dentry);
	ASSERT(nd);

	link = (char *)kmalloc(MAXNAMELEN+1, GFP_KERNEL);
	if (!link)
		return -ENOMEM;

	uio = (uio_t *)kmalloc(sizeof(uio_t), GFP_KERNEL);
	if (!uio) {
		kfree(link);
		return -ENOMEM;
	}

	vp = LINVFS_GET_VP(dentry->d_inode);

	iov.iov_base = link;
	iov.iov_len = MAXNAMELEN;

	uio->uio_iov = &iov;
	uio->uio_offset = 0;
	uio->uio_segflg = UIO_SYSSPACE;
	uio->uio_resid = MAXNAMELEN;
	uio->uio_fmode = 0;

	VOP_READLINK(vp, uio, NULL, error);
	if (error) {
		kfree(uio);
		kfree(link);
		return -error;
	}

	link[MAXNAMELEN - uio->uio_resid] = '\0';
	kfree(uio);

	/* vfs_follow_link returns (-) errors */
	error = vfs_follow_link(nd, link);
	kfree(link);
	return error;
}

STATIC int
linvfs_permission(
	struct inode	*inode,
	int		mode)
{
	vnode_t		*vp = LINVFS_GET_VP(inode);
	int		error;

	mode <<= 6;		/* convert from linux to vnode access bits */
	VOP_ACCESS(vp, mode, NULL, error);
	return -error;
}

/* Brute force approach for now - copy data into linux inode
 * from the results of a getattr. This gets called out of things
 * like stat.
 */
int
linvfs_revalidate_core(
	struct inode	*inode,
	int		flags)
{
	vnode_t		*vp = LINVFS_GET_VP(inode);

	/* vn_revalidate returns (-) error so this is ok */
	return vn_revalidate(vp, flags);
}

STATIC int
linvfs_revalidate(
	struct dentry	*dentry)
{
	vnode_t		*vp = LINVFS_GET_VP(dentry->d_inode);

	if (unlikely(vp->v_flag & VMODIFIED)) {
		return linvfs_revalidate_core(dentry->d_inode, 0);
	}
	return 0;
}

STATIC int
linvfs_setattr(
	struct dentry	*dentry,
	struct iattr	*attr)
{
	struct inode	*inode = dentry->d_inode;
	vnode_t		*vp = LINVFS_GET_VP(inode);
	vattr_t		vattr;
	unsigned int	ia_valid = attr->ia_valid;
	int		error;
	int		flags = 0;

	memset(&vattr, 0, sizeof(vattr_t));
	if (ia_valid & ATTR_UID) {
		vattr.va_mask |= AT_UID;
		vattr.va_uid = attr->ia_uid;
	}
	if (ia_valid & ATTR_GID) {
		vattr.va_mask |= AT_GID;
		vattr.va_gid = attr->ia_gid;
	}
	if (ia_valid & ATTR_SIZE) {
		vattr.va_mask |= AT_SIZE;
		vattr.va_size = attr->ia_size;
	}
	if (ia_valid & ATTR_ATIME) {
		vattr.va_mask |= AT_ATIME;
		vattr.va_atime.tv_sec = attr->ia_atime;
		vattr.va_atime.tv_nsec = 0;
	}
	if (ia_valid & ATTR_MTIME) {
		vattr.va_mask |= AT_MTIME;
		vattr.va_mtime.tv_sec = attr->ia_mtime;
		vattr.va_mtime.tv_nsec = 0;
	}
	if (ia_valid & ATTR_CTIME) {
		vattr.va_mask |= AT_CTIME;
		vattr.va_ctime.tv_sec = attr->ia_ctime;
		vattr.va_ctime.tv_nsec = 0;
	}
	if (ia_valid & ATTR_MODE) {
		vattr.va_mask |= AT_MODE;
		vattr.va_mode = attr->ia_mode;
		if (!in_group_p(inode->i_gid) && !capable(CAP_FSETID))
			inode->i_mode &= ~S_ISGID;
	}

	if (ia_valid & (ATTR_MTIME_SET | ATTR_ATIME_SET))
		flags = ATTR_UTIME;

	VOP_SETATTR(vp, &vattr, flags, NULL, error);
	if (error)
		return(-error); /* Positive error up from XFS */
	if (ia_valid & ATTR_SIZE) {
		error = vmtruncate(inode, attr->ia_size);
	}

	if (!error) {
		vn_revalidate(vp, 0);
		mark_inode_dirty_sync(inode);
	}
	return error;
}

STATIC void
linvfs_truncate(
	struct inode		*inode)
{
	block_truncate_page(inode->i_mapping, inode->i_size, linvfs_get_block);
}



/*
 * Extended attributes interfaces
 */

#define SYSTEM_NAME	"system."	/* VFS shared names/values */
#define ROOT_NAME	"xfsroot."	/* XFS ondisk names/values */
#define USER_NAME	"user."		/* user's own names/values */
STATIC xattr_namespace_t xfs_namespace_array[] = {
	{ .name= SYSTEM_NAME,	.namelen= sizeof(SYSTEM_NAME)-1,.exists= NULL },
	{ .name= ROOT_NAME,	.namelen= sizeof(ROOT_NAME)-1,	.exists= NULL },
	{ .name= USER_NAME,	.namelen= sizeof(USER_NAME)-1,	.exists= NULL },
	{ .name= NULL }
};
xattr_namespace_t *xfs_namespaces = &xfs_namespace_array[0];

#define POSIXACL_ACCESS		"posix_acl_access"
#define POSIXACL_ACCESS_SIZE	(sizeof(POSIXACL_ACCESS)-1)
#define POSIXACL_DEFAULT	"posix_acl_default"
#define POSIXACL_DEFAULT_SIZE	(sizeof(POSIXACL_DEFAULT)-1)
#define POSIXCAP		"posix_capabilities"
#define POSIXCAP_SIZE		(sizeof(POSIXCAP)-1)
#define POSIXMAC		"posix_mac"
#define POSIXMAC_SIZE		(sizeof(POSIXMAC)-1)
STATIC xattr_namespace_t sys_namespace_array[] = {
	{ .name= POSIXACL_ACCESS,
		.namelen= POSIXACL_ACCESS_SIZE,	 .exists= _ACL_ACCESS_EXISTS },
	{ .name= POSIXACL_DEFAULT,
		.namelen= POSIXACL_DEFAULT_SIZE, .exists= _ACL_DEFAULT_EXISTS },
	{ .name= POSIXCAP,
		.namelen= POSIXCAP_SIZE,	 .exists= _CAP_EXISTS },
	{ .name= POSIXMAC,
		.namelen= POSIXMAC_SIZE,	 .exists= _MAC_EXISTS },
	{ .name= NULL }
};

/*
 * Some checks to prevent people abusing EAs to get over quota:
 * - Don't allow modifying user EAs on devices/symlinks;
 * - Don't allow modifying user EAs if sticky bit set;
 */
STATIC int
capable_user_xattr(
	struct inode	*inode)
{
	if (!S_ISREG(inode->i_mode) && !S_ISDIR(inode->i_mode) &&
	    !capable(CAP_SYS_ADMIN))
		return 0;
	if (S_ISDIR(inode->i_mode) && (inode->i_mode & S_ISVTX) &&
	    (current->fsuid != inode->i_uid) && !capable(CAP_FOWNER))
		return 0;
	return 1;
}

STATIC int
linvfs_setxattr(
	struct dentry	*dentry,
	const char	*name,
	void		*data,
	size_t		size,
	int		flags)
{
	int		error;
	int		xflags = 0;
	char		*p = (char *)name;
	struct inode	*inode = dentry->d_inode;
	vnode_t		*vp = LINVFS_GET_VP(inode);

	if (strncmp(name, xfs_namespaces[SYSTEM_NAMES].name,
			xfs_namespaces[SYSTEM_NAMES].namelen) == 0) {
		error = -EINVAL;
		if (flags & XATTR_CREATE)
			 return error;
		error = -ENOATTR;
		p += xfs_namespaces[SYSTEM_NAMES].namelen;
		if (strcmp(p, POSIXACL_ACCESS) == 0) {
			if (vp->v_flag & VMODIFIED) {
				error = linvfs_revalidate_core(inode, 0);
				if (error)
					return error;
			}
			error = xfs_acl_vset(vp, data, size, _ACL_TYPE_ACCESS);
			if (!error) {
				VMODIFY(vp);
				error = linvfs_revalidate_core(inode, 0);
			}
		}
		else if (strcmp(p, POSIXACL_DEFAULT) == 0) {
			error = linvfs_revalidate_core(inode, 0);
			if (error)
				return error;
			error = xfs_acl_vset(vp, data, size, _ACL_TYPE_DEFAULT);
			if (!error) {
				VMODIFY(vp);
				error = linvfs_revalidate_core(inode, 0);
			}
		}
		else if (strcmp(p, POSIXCAP) == 0) {
			error = xfs_cap_vset(vp, data, size);
		}
		return error;
	}

	/* Convert Linux syscall to XFS internal ATTR flags */
	if (flags & XATTR_CREATE)
		xflags |= ATTR_CREATE;
	if (flags & XATTR_REPLACE)
		xflags |= ATTR_REPLACE;

	if (strncmp(name, xfs_namespaces[ROOT_NAMES].name,
			xfs_namespaces[ROOT_NAMES].namelen) == 0) {
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		xflags |= ATTR_ROOT;
		p += xfs_namespaces[ROOT_NAMES].namelen;
		VOP_ATTR_SET(vp, p, data, size, xflags, NULL, error);
		return -error;
	}
	if (strncmp(name, xfs_namespaces[USER_NAMES].name,
			xfs_namespaces[USER_NAMES].namelen) == 0) {
		if (!capable_user_xattr(inode))
			return -EPERM;
		p += xfs_namespaces[USER_NAMES].namelen;
		VOP_ATTR_SET(vp, p, data, size, xflags, NULL, error);
		return -error;
	}
	return -ENOATTR;
}

STATIC ssize_t
linvfs_getxattr(
	struct dentry	*dentry,
	const char	*name,
	void		*data,
	size_t		size)
{
	ssize_t		error;
	int		xflags = 0;
	char		*p = (char *)name;
	struct inode	*inode = dentry->d_inode;
	vnode_t		*vp = LINVFS_GET_VP(inode);

	if (strncmp(name, xfs_namespaces[SYSTEM_NAMES].name,
			xfs_namespaces[SYSTEM_NAMES].namelen) == 0) {
		error = -ENOATTR;
		p += xfs_namespaces[SYSTEM_NAMES].namelen;
		if (strcmp(p, POSIXACL_ACCESS) == 0) {
			if (vp->v_flag & VMODIFIED) {
				error = linvfs_revalidate_core(inode, 0);
				if (error)
					return error;
			}
			error = xfs_acl_vget(vp, data, size, _ACL_TYPE_ACCESS);
		}
		else if (strcmp(p, POSIXACL_DEFAULT) == 0) {
			if (vp->v_flag & VMODIFIED) {
				error = linvfs_revalidate_core(inode, 0);
				if (error)
					return error;
			}
			error = xfs_acl_vget(vp, data, size, _ACL_TYPE_DEFAULT);
		}
		else if (strcmp(p, POSIXCAP) == 0) {
			error = xfs_cap_vget(vp, data, size);
		}
		return error;
	}

	/* Convert Linux syscall to XFS internal ATTR flags */
	if (!size)
		xflags |= ATTR_KERNOVAL;

	if (strncmp(name, xfs_namespaces[ROOT_NAMES].name,
			xfs_namespaces[ROOT_NAMES].namelen) == 0) {
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		xflags |= ATTR_ROOT;
		p += xfs_namespaces[ROOT_NAMES].namelen;
		VOP_ATTR_GET(vp, p, data, (int *)&size, xflags, NULL, error);
		if (!error)
			error = -size;
		return -error;
	}
	if (strncmp(name, xfs_namespaces[USER_NAMES].name,
			xfs_namespaces[USER_NAMES].namelen) == 0) {
		p += xfs_namespaces[USER_NAMES].namelen;
		if (!capable_user_xattr(inode))
			return -EPERM;
		VOP_ATTR_GET(vp, p, data, (int *)&size, xflags, NULL, error);
		if (!error)
			error = -size;
		return -error;
	}
	return -ENOATTR;
}


STATIC ssize_t
linvfs_listxattr(
	struct dentry		*dentry,
	char			*data,
	size_t			size)
{
	ssize_t			error;
	int			result = 0;
	int			xflags = ATTR_KERNAMELS;
	char			*k = data;
	attrlist_cursor_kern_t	cursor;
	xattr_namespace_t	*sys;
	vnode_t			*vp;

	vp = LINVFS_GET_VP(dentry->d_inode);

	if (!size)
		xflags |= ATTR_KERNOVAL;
	if (capable(CAP_SYS_ADMIN))
		xflags |= ATTR_KERNFULLS;

	memset(&cursor, 0, sizeof(cursor));
	VOP_ATTR_LIST(vp, data, size, xflags, &cursor, NULL, error);
	if (error > 0)
		return -error;
	result += -error;

	k += result;		/* advance start of our buffer */
	for (sys = &sys_namespace_array[0]; sys->name != NULL; sys++) {
		if (sys->exists == NULL || !sys->exists(vp))
			continue;
		result += xfs_namespaces[SYSTEM_NAMES].namelen;
		result += sys->namelen + 1;
		if (size) {
			if (result > size)
				return -ERANGE;
			strcpy(k, xfs_namespaces[SYSTEM_NAMES].name);
			k += xfs_namespaces[SYSTEM_NAMES].namelen;
			strcpy(k, sys->name);
			k += sys->namelen + 1;
		}
	}
	return result;
}

STATIC int
linvfs_removexattr(
	struct dentry	*dentry,
	const char	*name)
{
	int		error;
	int		xflags = 0;
	char		*p = (char *)name;
	struct inode	*inode = dentry->d_inode;
	vnode_t		*vp = LINVFS_GET_VP(inode);

	if (strncmp(name, xfs_namespaces[SYSTEM_NAMES].name,
			xfs_namespaces[SYSTEM_NAMES].namelen) == 0) {
		error = -ENOATTR;
		p += xfs_namespaces[SYSTEM_NAMES].namelen;
		if (strcmp(p, POSIXACL_ACCESS) == 0)
			error = xfs_acl_vremove(vp, _ACL_TYPE_ACCESS);
		else if (strcmp(p, POSIXACL_DEFAULT) == 0)
			error = xfs_acl_vremove(vp, _ACL_TYPE_DEFAULT);
		else if (strcmp(p, POSIXCAP) == 0)
			error = xfs_cap_vremove(vp);
		return error;
	}

	if (strncmp(name, xfs_namespaces[ROOT_NAMES].name,
			xfs_namespaces[ROOT_NAMES].namelen) == 0) {
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		xflags |= ATTR_ROOT;
		p += xfs_namespaces[ROOT_NAMES].namelen;
		VOP_ATTR_REMOVE(vp, p, xflags, NULL, error);
		return -error;
	}
	if (strncmp(name, xfs_namespaces[USER_NAMES].name,
			xfs_namespaces[USER_NAMES].namelen) == 0) {
		p += xfs_namespaces[USER_NAMES].namelen;
		if (!capable_user_xattr(inode))
			return -EPERM;
		VOP_ATTR_REMOVE(vp, p, xflags, NULL, error);
		return -error;
	}
	return -ENOATTR;
}


struct inode_operations linvfs_file_inode_operations =
{
	.permission		= linvfs_permission,
	.truncate		= linvfs_truncate,
	.revalidate		= linvfs_revalidate,
	.setattr		= linvfs_setattr,
	.setxattr		= linvfs_setxattr,
	.getxattr		= linvfs_getxattr,
	.listxattr		= linvfs_listxattr,
	.removexattr		= linvfs_removexattr,
};

struct inode_operations linvfs_dir_inode_operations =
{
	.create			= linvfs_create,
	.lookup			= linvfs_lookup,
	.link			= linvfs_link,
	.unlink			= linvfs_unlink,
	.symlink		= linvfs_symlink,
	.mkdir			= linvfs_mkdir,
	.rmdir			= linvfs_rmdir,
	.mknod			= linvfs_mknod,
	.rename			= linvfs_rename,
	.permission		= linvfs_permission,
	.revalidate		= linvfs_revalidate,
	.setattr		= linvfs_setattr,
	.setxattr		= linvfs_setxattr,
	.getxattr		= linvfs_getxattr,
	.listxattr		= linvfs_listxattr,
	.removexattr		= linvfs_removexattr,
};

struct inode_operations linvfs_symlink_inode_operations =
{
	.readlink		= linvfs_readlink,
	.follow_link		= linvfs_follow_link,
	.permission		= linvfs_permission,
	.revalidate		= linvfs_revalidate,
	.setattr		= linvfs_setattr,
	.setxattr		= linvfs_setxattr,
	.getxattr		= linvfs_getxattr,
	.listxattr		= linvfs_listxattr,
	.removexattr		= linvfs_removexattr,
};
