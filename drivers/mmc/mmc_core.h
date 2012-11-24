/*
 * Header for MultiMediaCard (MMC)
 *
 * Copyright 2002 Hewlett-Packard Company
 *
 * Use consistent with the GNU GPL is permitted,
 * provided that this copyright notice is
 * preserved in its entirety in all copies and derived works.
 *
 * HEWLETT-PACKARD COMPANY MAKES NO WARRANTIES, EXPRESSED OR IMPLIED,
 * AS TO THE USEFULNESS OR CORRECTNESS OF THIS CODE OR ITS
 * FITNESS FOR ANY PARTICULAR PURPOSE.
 *
 * Many thanks to Alessandro Rubini and Jonathan Corbet!
 *
 * Based strongly on code by:
 *
 * Author: Yong-iL Joh <tolkien@mizi.com>
 * Date  : $Date: 2002/10/03 05:21:14 $ 
 *
 * Author:  Andrew Christian
 *          15 May 2002
 */

/*
 *	SD Cards specific functions implemented
 *
 * Copyright 2003 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *	   source@mvista.com
 *
 *  This program is free software; you can redistribute	 it and/or modify it
 *  under  the terms of	 the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the	License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED	  ``AS	IS'' AND   ANY	EXPRESS OR IMPLIED
 *  WARRANTIES,	  INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO	EVENT  SHALL   THE AUTHOR  BE	 LIABLE FOR ANY	  DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED	  TO, PROCUREMENT OF  SUBSTITUTE GOODS	OR SERVICES; LOSS OF
 *  USE, DATA,	OR PROFITS; OR	BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN	 CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef MMC_MMC_CORE_H
#define MMC_MMC_CORE_H

#include <linux/mmc/mmc_ll.h>
#include "mmc_media.h"

#define ID_TO_RCA(x) ((x)+1)

struct mmc_dev {
	struct mmc_slot_driver   *sdrive;
	struct mmc_slot           slot[MMC_MAX_SLOTS];
	struct mmc_request        request;               // Active request to the low-level driver
	struct mmc_io_request    *io_request;            // Active transfer request from the high-level media io
	struct tasklet_struct     task;
	int    num_slots;                 // Copied from the slot driver; used when slot driver shuts down

	/* State maintenance */
	int    state;  
	int    suspended;
	void (*protocol)(struct mmc_dev *, int);
};

char * mmc_result_to_string( int );
int    mmc_unpack_csd( struct mmc_request *request, struct mmc_csd *csd );
int    mmc_unpack_r1( struct mmc_request *request, struct mmc_response_r1 *r1, enum card_state state );
int    mmc_unpack_r6( struct mmc_request *request, struct mmc_response_r1 *r1, enum card_state state, int *rca);
int    mmc_unpack_scr( struct mmc_request *request, struct mmc_response_r1 *r1, enum card_state state, u32 *scr);
int    mmc_unpack_cid( struct mmc_request *request, struct mmc_cid *cid );
int    mmc_unpack_r3( struct mmc_request *request, struct mmc_response_r3 *r3 );

void   mmc_send_cmd( struct mmc_dev *dev, int cmd, u32 arg, 
		     u16 nob, u16 block_len, enum mmc_rsp_t rtype );
void   mmc_finish_io_request( struct mmc_dev *dev, int result );
int    mmc_check_eject( struct mmc_dev *dev );
int    mmc_check_insert( struct mmc_dev *dev );
u32    mmc_tran_speed( u8 ts );
int    mmc_match_media_driver( struct mmc_slot *slot );
void   run_sbin_mmc_hotplug(struct mmc_dev *dev, int id, int insert);

static inline void mmc_simple_cmd( struct mmc_dev *dev, int cmd, u32 arg, enum mmc_rsp_t rtype )
{
	mmc_send_cmd( dev, cmd, arg, 0, 0, rtype );
}

#endif  /* MMC_MMC_CORE_H */

