#
# Copyright (c) 2000-2002 Silicon Graphics, Inc.  All Rights Reserved.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of version 2 of the GNU General Public License as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it would be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#
# Further, this software is distributed without any warranty that it is
# free of the rightful claim of any third person regarding infringement
# or the like.	Any license provided herein, whether implied or
# otherwise, applies only to this software file.  Patent licenses, if
# any, provided herein do not apply to combinations of this program with
# other software, or any other product whatsoever.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write the Free Software Foundation, Inc., 59
# Temple Place - Suite 330, Boston MA 02111-1307, USA.
#
# Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
# Mountain View, CA  94043, or:
#
# http://www.sgi.com
#
# For further information regarding this notice, see:
#
# http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
#
# Makefile for the linux pagebuf routines.
#

# Debug options:
#	-DDEBUG
#	-DPAGEBUF_TRACKING
#	-DPAGEBUF_TRACE
ifeq ($(CONFIG_PAGEBUF_DEBUG),y)
	EXTRA_CFLAGS += -g -DDEBUG -DSTATIC="" \
		-DPAGEBUF_TRACKING -DPAGEBUF_TRACE
endif
EXTRA_CFLAGS += -I..

O_TARGET			:= pagebuf.o

ifneq ($(MAKECMDGOALS),modules_install)
  obj-m				:= $(O_TARGET)
endif

export-objs			+= page_buf.o
obj-y				+= page_buf.o \
				   page_buf_locking.o

include $(TOPDIR)/Rules.make
