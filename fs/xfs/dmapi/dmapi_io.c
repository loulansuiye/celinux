/*
 * Copyright (c) 2000 Silicon Graphics, Inc.  All Rights Reserved.
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

#include "dmapi_private.h"


int
dm_read_invis_rvp(
	dm_sessid_t	sid,
	void		*hanp,
	size_t		hlen,
	dm_token_t	token,
	dm_off_t	off,
	dm_size_t	len,
	void		*bufp,
	int		*rvp)
{
	dm_fsys_vector_t *fsys_vector;
	dm_tokdata_t	*tdp;
	int		error;

	error = dm_app_get_tdp(sid, hanp, hlen, token, DM_TDT_REG,
		DM_RIGHT_SHARED, &tdp);
	if (error != 0)
		return(error);

	VN_BHV_READ_LOCK(VN_BHV_HEAD(tdp->td_vp));
	fsys_vector = dm_fsys_vector(tdp->td_vp);
	error = fsys_vector->read_invis_rvp(tdp->td_vp, tdp->td_right,
		off, len, bufp, rvp);
	VN_BHV_READ_UNLOCK(VN_BHV_HEAD(tdp->td_vp));

	dm_app_put_tdp(tdp);
	return(error);
}


int
dm_write_invis_rvp(
	dm_sessid_t	sid,
	void		*hanp,
	size_t		hlen,
	dm_token_t	token,
	int		flags,
	dm_off_t	off,
	dm_size_t	len,
	void		*bufp,
	int		*rvp)
{
	dm_fsys_vector_t *fsys_vector;
	dm_tokdata_t	*tdp;
	int		error;

	error = dm_app_get_tdp(sid, hanp, hlen, token, DM_TDT_REG,
		DM_RIGHT_EXCL, &tdp);
	if (error != 0)
		return(error);

	VN_BHV_READ_LOCK(VN_BHV_HEAD(tdp->td_vp));
	fsys_vector = dm_fsys_vector(tdp->td_vp);
	error = fsys_vector->write_invis_rvp(tdp->td_vp, tdp->td_right,
		flags, off, len, bufp, rvp);
	VN_BHV_READ_UNLOCK(VN_BHV_HEAD(tdp->td_vp));

	dm_app_put_tdp(tdp);
	return(error);
}


int
dm_sync_by_handle (
	dm_sessid_t	sid,
	void		*hanp,
	size_t		hlen,
	dm_token_t	token)
{
	dm_fsys_vector_t *fsys_vector;
	dm_tokdata_t	*tdp;
	int		error;

	error = dm_app_get_tdp(sid, hanp, hlen, token, DM_TDT_REG,
		DM_RIGHT_EXCL, &tdp);
	if (error != 0)
		return(error);

	VN_BHV_READ_LOCK(VN_BHV_HEAD(tdp->td_vp));
	fsys_vector = dm_fsys_vector(tdp->td_vp);
	error = fsys_vector->sync_by_handle(tdp->td_vp, tdp->td_right);
	VN_BHV_READ_UNLOCK(VN_BHV_HEAD(tdp->td_vp));

	dm_app_put_tdp(tdp);
	return(error);
}


int
dm_get_dioinfo (
	dm_sessid_t	sid,
	void		*hanp,
	size_t		hlen,
	dm_token_t	token,
	dm_dioinfo_t	*diop)
{
	dm_fsys_vector_t *fsys_vector;
	dm_tokdata_t	*tdp;
	int		error;

	error = dm_app_get_tdp(sid, hanp, hlen, token, DM_TDT_REG,
		DM_RIGHT_SHARED, &tdp);
	if (error != 0)
		return(error);

	VN_BHV_READ_LOCK(VN_BHV_HEAD(tdp->td_vp));
	fsys_vector = dm_fsys_vector(tdp->td_vp);
	error = fsys_vector->get_dioinfo(tdp->td_vp, tdp->td_right, diop);
	VN_BHV_READ_UNLOCK(VN_BHV_HEAD(tdp->td_vp));

	dm_app_put_tdp(tdp);
	return(error);
}
