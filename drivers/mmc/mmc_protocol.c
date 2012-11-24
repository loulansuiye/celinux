/*
 * MMC State machine functions
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
 * This part of the code is separated from mmc_core.o so we can
 * plug in different state machines (e.g., SPI, SD)
 *
 * This code assumes that you have exactly one card slot, no more.
 *
 * Author:  Andrew Christian
 *          6 May 2002
 */

/*
 * SD Cards protocol implemented
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


#include <linux/config.h>
#include <linux/module.h>

#include <linux/version.h>
#include <linux/proc_fs.h>

#include "mmc_core.h"

static void * mmc_cim_default_state( struct mmc_dev *dev, int first );

/******************************************************************
 *
 * Useful utility functions
 *
 ******************************************************************/

static int mmc_has_valid_request( struct mmc_dev *dev )
{
	struct mmc_io_request *request = dev->io_request;
	struct mmc_slot *slot;

	DEBUG(2," (%p)\n", request);

	if ( !request ) return 0;

	slot = dev->slot + request->id;

	if ( !slot->media_driver ) {
		DEBUG(0,": card doesn't have media driver\n");
		return 0;
	}

	return 1;
}

static void mmc_configure_card( struct mmc_dev *dev, int slot )
{
	u32 rate;
	struct mmc_slot *slot_data = dev->slot;

	DEBUG(2,": slot=%d\n", slot);

	/* Fix the clock rate */
	rate = mmc_tran_speed(dev->slot[slot].csd.tran_speed);
	if ( rate < MMC_CLOCK_SLOW )
		rate = MMC_CLOCK_SLOW;
	if (( slot_data->sd == 0) && ( rate > MMC_CLOCK_FAST ))
		rate = MMC_CLOCK_FAST;
        if (( slot_data->sd ) && (rate > SD_CLOCK_FAST))
		rate = SD_CLOCK_FAST;

	dev->sdrive->set_clock(rate);
	
	/* Match the drive media */
	mmc_match_media_driver(&dev->slot[slot]);
	run_sbin_mmc_hotplug(dev, slot, 1);
}

/* The blocks requested by the kernel may or may not
   match what we can do.  Unfortunately, filesystems play
   fast and loose with block sizes, so we're stuck with this */

static void mmc_fix_request_block_size( struct mmc_dev *dev )
{
	struct mmc_io_request *t = dev->io_request;
	struct mmc_slot *slot = dev->slot + t->id;
	u16 block_len;

	DEBUG(1, ": io_request id=%d cmd=%d sector=%ld nr_sectors=%ld block_len=%ld buf=%p\n",
	      t->id, t->cmd, t->sector, t->nr_sectors, t->block_len, t->buffer);

	switch( t->cmd ) {
	case READ:
		block_len = 1 << slot->csd.read_bl_len;
		break;
	case WRITE:
		block_len = 1 << slot->csd.write_bl_len;
		break;
	default:
		DEBUG(0,": unrecognized command %d\n", t->cmd);
		return;
	}

	if ( block_len < t->block_len ) {
		int scale = t->block_len / block_len;
		DEBUG(1,": scaling by %d from block_len=%d to %ld\n", 
		      scale, block_len, t->block_len);
		t->block_len   = block_len;
		t->sector     *= scale;
		t->nr_sectors *= scale;
	}
}


/******************************************************************
 * State machine routines to read and write data
 *
 *  SET_BLOCKLEN only needs to be done once for each card.
 *  SET_BLOCK_COUNT is only valid in MMC 3.1; most cards don't support this,
 *  so we don't use it.
 * 
 *  In the 2.x cards we have a choice between STREAMING mode and
 *  SINGLE mode.  There's an obvious performance possibility in 
 *  using streaming mode, but at this time we're just using the SINGLE
 *  mode.
 ******************************************************************/

static void * mmc_cim_read_write_block( struct mmc_dev *dev, int first )
{
	struct mmc_io_request *t = dev->io_request;
	struct mmc_response_r1 r1;
	struct mmc_slot *slot = dev->slot + t->id;
	int    retval = 0;
	int    i;

	DEBUG(2," first=%d\n",first);

	if ( first ) {
		mmc_fix_request_block_size( dev );

		switch ( slot->state ) {
		case CARD_STATE_STBY:
			mmc_simple_cmd(dev, MMC_SELECT_CARD, slot->rca, RESPONSE_R1B );
			break;
		case CARD_STATE_TRAN:
			mmc_simple_cmd(dev, MMC_SET_BLOCKLEN, t->block_len, RESPONSE_R1 );
			break;
		default:
			DEBUG(0,": invalid card state %d\n", slot->state);
			goto read_block_error;
			break;
		}
		return NULL;
	}

	switch (dev->request.cmd) {
	case MMC_SELECT_CARD:
		if ( (retval = mmc_unpack_r1( &dev->request, &r1, slot->state )) )
			goto read_block_error;

		for ( i = 0 ; i < dev->num_slots ; i++ )
			dev->slot[i].state = ( i == t->id ? CARD_STATE_TRAN : CARD_STATE_STBY );
           
                if(slot->sd)
		{ 
                  slot->scr = 0;
		  mmc_simple_cmd(dev, MMC_APP_CMD,  slot->rca, RESPONSE_R1);
                }
                else
		{ 
                   mmc_simple_cmd(dev, MMC_SET_BLOCKLEN, t->block_len, RESPONSE_R1 );
                }
		break;

        case MMC_APP_CMD:
        	retval = mmc_unpack_r1(&dev->request,&r1,slot->state);

		if ( retval ) {
                   DEBUG(0, ": unable to MMC_APP_CMD error=%d (%s)\n", 
			      retval, mmc_result_to_string(retval));
                   goto read_block_error;
		}

                if (slot->scr & (1 << 18))
		{
		  mmc_simple_cmd(dev, SET_BUS_WIDTH,  2, RESPONSE_R1);
                }
                else if (slot->scr)
		{
                  mmc_simple_cmd(dev, SET_BUS_WIDTH, 0, RESPONSE_R1);
                }   
                else
		{       
		  /* Get SCR */
                  mmc_simple_cmd(dev, SEND_SCR, 0, RESPONSE_R1);
                }

		break;

	case SEND_SCR:
	        retval = mmc_unpack_scr(&dev->request, &r1, slot->state, &slot->scr);
                if ( retval ) {
                   DEBUG(0, ": unable to SEND_SCR error=%d (%s)\n", 
			      retval, mmc_result_to_string(retval));
                   goto read_block_error;
		}

                DEBUG(2, ": Get SCR from SD: 0x%x Satus: %x\n", slot->scr, r1.status);

                mmc_simple_cmd(dev, MMC_APP_CMD, slot->rca, RESPONSE_R1);
                break;

        case SET_BUS_WIDTH:
                retval = mmc_unpack_r1(&dev->request,&r1,slot->state);
                if ( retval ) {
                   DEBUG(0, ": unable to SET_BUS_WIDTH error=%d (%s)\n", 
			      retval, mmc_result_to_string(retval));
                   goto read_block_error; 
		}
		else
		{
                   mmc_simple_cmd(dev, MMC_SET_BLOCKLEN, t->block_len, RESPONSE_R1 );
		}    
                break;
              
	case MMC_SET_BLOCKLEN:
		if ( (retval = mmc_unpack_r1( &dev->request, &r1, slot->state )) )
			goto read_block_error;

		mmc_send_cmd(dev, (t->cmd == READ ? MMC_READ_SINGLE_BLOCK : MMC_WRITE_BLOCK), 
			     t->sector * t->block_len, 1, t->block_len, RESPONSE_R1 );
		break;

	case MMC_READ_SINGLE_BLOCK:
	case MMC_WRITE_BLOCK:
		if ( (retval = mmc_unpack_r1( &dev->request, &r1, slot->state )) )
			goto read_block_error;

		t->nr_sectors--;
		t->sector++;
		t->buffer += t->block_len;

		if ( t->nr_sectors ) {
			mmc_send_cmd(dev, (t->cmd == READ ? MMC_READ_SINGLE_BLOCK : MMC_WRITE_BLOCK), 
				     t->sector * t->block_len, 1, t->block_len, RESPONSE_R1 );
		}
		else {
			mmc_finish_io_request( dev, 1 );
			if ( mmc_has_valid_request(dev) )
				return mmc_cim_read_write_block;
			return mmc_cim_default_state;
		}
		break;

	default:
		goto read_block_error;
		break;
	}
	return NULL;

read_block_error:
	DEBUG(0,": failure during cmd %d, error %d (%s)\n", 
	      dev->request.cmd, retval, mmc_result_to_string(retval));
	mmc_finish_io_request( dev, 0 );   // Failure
	return mmc_cim_default_state;
}

/* Update the card's status information in preparation to running a read/write cycle */

static void * mmc_cim_get_status( struct mmc_dev *dev, int first )
{
	struct mmc_slot *slot = dev->slot + dev->io_request->id;
	struct mmc_response_r1 r1;
	int retval = MMC_NO_ERROR;

	DEBUG(2," first=%d\n",first);

	if ( first ) {
		mmc_simple_cmd(dev, MMC_SEND_STATUS, slot->rca, RESPONSE_R1 );
		return NULL;
	}

	switch (dev->request.cmd) {
	case MMC_SEND_STATUS:
		retval = mmc_unpack_r1(&dev->request,&r1,slot->state);
		if ( !retval || retval == MMC_ERROR_STATE_MISMATCH ) {
			slot->state = R1_CURRENT_STATE(r1.status);
			return mmc_cim_read_write_block;
		}
		break;

	default:
		break;
	}

	DEBUG(0, ": failure during cmd %d, error=%d (%s)\n", dev->request.cmd,
	      retval, mmc_result_to_string(retval));
	mmc_finish_io_request(dev,0);
	return mmc_cim_default_state;
}

static void * mmc_cim_handle_request( struct mmc_dev *dev, int first )
{
	DEBUG(2," first=%d\n",first);

	if ( !first && !mmc_has_valid_request(dev)) {
		DEBUG(0, ": invalid request\n");
		mmc_finish_io_request(dev,0);
		return mmc_cim_default_state;
	}

	if ( first )
		return mmc_cim_get_status;

	return mmc_cim_read_write_block;
}

/******************************************************************
 *
 * State machine routines to initialize card(s)
 *
 ******************************************************************/

/*
  CIM_SINGLE_CARD_ACQ  (frequency at 400 kHz)
  --- Must enter from GO_IDLE_STATE ---
  1. SD_SEND_OP_COND (SD Card) [CMD55] + [CMD41]
  2. SEND_OP_COND (Full Range) [CMD1]   {optional}
  3. SEND_OP_COND (Set Range ) [CMD1]
     If busy, delay and repeat step 2
  4. ALL_SEND_CID              [CMD2]
     If timeout, set an error (no cards found)
  5. SET_RELATIVE_ADDR         [CMD3]
  6. SEND_CSD                  [CMD9]
  7. SET_DSR                   [CMD4]    Only call this if (csd.dsr_imp).
  8. Set clock frequency (check available in csd.tran_speed)
 */

static void * mmc_cim_single_card_acq( struct mmc_dev *dev, int first )
{
	struct mmc_response_r3 r3;
	struct mmc_response_r1 r1;
	struct mmc_slot *slot = dev->slot;     /* Must be slot 0 */
	int retval;

	DEBUG(2,"\n");

	if ( first ) {
		mmc_simple_cmd(dev, MMC_GO_IDLE_STATE, 0, RESPONSE_NONE);
                slot->sd = 0; 
		return NULL;
	}

	switch (dev->request.cmd) {
	case MMC_GO_IDLE_STATE: /* No response to parse */
		if ( (dev->sdrive->flags & MMC_SDFLAG_VOLTAGE ))
			DEBUG(0,": error - current driver doesn't do OCR\n");
		mmc_simple_cmd(dev, MMC_APP_CMD,  0, RESPONSE_R1);
		break;

        case MMC_APP_CMD:
        	retval = mmc_unpack_r1(&dev->request,&r1,slot->state);
		if ( retval ) {
			DEBUG(0, ": unable to MMC_APP_CMD error=%d (%s)\n", 
			      retval, mmc_result_to_string(retval));
			mmc_simple_cmd(dev, MMC_SEND_OP_COND, dev->sdrive->ocr, RESPONSE_R3);
		}
                else { 
	            mmc_simple_cmd(dev, SD_SEND_OP_COND, 0x00ff8000, RESPONSE_R3);
                }
		break;

        case SD_SEND_OP_COND:
                retval = mmc_unpack_r3(&dev->request, &r3);
                if ( retval ) {

                  /* Try MMC card */
                    mmc_simple_cmd(dev, MMC_SEND_OP_COND, dev->sdrive->ocr, RESPONSE_R3);
                    break;
		}

                DEBUG(2,": read ocr value = 0x%08x\n", r3.ocr);

		if (!(r3.ocr & MMC_CARD_BUSY)) {
			mmc_simple_cmd(dev, MMC_APP_CMD, 0, RESPONSE_R1);
		}
		else {

		  /* Set the data bus width to 4 bits */
                  slot->sd = 1; /* SD Card ready */
                  slot->state = CARD_STATE_READY;
		  mmc_simple_cmd(dev, MMC_ALL_SEND_CID, 0, RESPONSE_R2_CID);

		}
		break;

	case MMC_SEND_OP_COND:
		retval = mmc_unpack_r3(&dev->request, &r3);
		if ( retval ) {
			DEBUG(0,": failed SEND_OP_COND error=%d (%s)\n", 
			      retval, mmc_result_to_string(retval));
			return mmc_cim_default_state;
		}

		DEBUG(2,": read ocr value = 0x%08x\n", r3.ocr);
		if (!(r3.ocr & MMC_CARD_BUSY)) {
	                mmc_simple_cmd(dev, MMC_SEND_OP_COND, dev->sdrive->ocr, RESPONSE_R3);
		}
		else {
		        slot->sd = 0; /* MMC Card ready */
			slot->state = CARD_STATE_READY;
			mmc_simple_cmd(dev, MMC_ALL_SEND_CID, 0, RESPONSE_R2_CID);
		}
		break;
		
	case MMC_ALL_SEND_CID: 
		retval = mmc_unpack_cid( &dev->request, &slot->cid );
		if ( retval ) {
			DEBUG(0,": unable to ALL_SEND_CID error=%d (%s)\n", 
			      retval, mmc_result_to_string(retval));
			return mmc_cim_default_state;
		}
		slot->state = CARD_STATE_IDENT;
		if(slot->sd)
		{
                   mmc_simple_cmd(dev, MMC_SET_RELATIVE_ADDR, 0, RESPONSE_R6);
                }
                else
		{
		   mmc_simple_cmd(dev, MMC_SET_RELATIVE_ADDR, ID_TO_RCA(slot->id) << 16, RESPONSE_R1);
                } 
		break;

        case MMC_SET_RELATIVE_ADDR:
	        if (slot->sd)
		{
		  retval = mmc_unpack_r6(&dev->request, &r1, slot->state, &slot->rca);
                  slot->rca = slot->rca << 16; 
                  DEBUG(2, ": Get RCA from SD: 0x%04x Status: %x\n", slot->rca, r1.status);
                }
                else
		{
		  retval = mmc_unpack_r1(&dev->request,&r1,slot->state);
		  slot->rca = ID_TO_RCA(slot->id) << 16;
	        }
		if ( retval ) {
			DEBUG(0, ": unable to SET_RELATIVE_ADDR error=%d (%s)\n", 
			      retval, mmc_result_to_string(retval));
			return mmc_cim_default_state;
		}

		slot->state = CARD_STATE_STBY;
                mmc_simple_cmd(dev, MMC_SEND_CSD, slot->rca, RESPONSE_R2_CSD);
             
		break;
        
	case MMC_SEND_CSD:
		retval = mmc_unpack_csd(&dev->request, &slot->csd);
		if ( retval ) {
			DEBUG(0, ": unable to SEND_CSD error=%d (%s)\n", 
			      retval, mmc_result_to_string(retval));
			return mmc_cim_default_state;
		}
		if ( slot->csd.dsr_imp ) {
			DEBUG(0, ": driver doesn't support setting DSR\n");
				// mmc_simple_cmd(dev, MMC_SET_DSR, 0, RESPONSE_NONE);
		}
		mmc_configure_card( dev, 0 );
		return mmc_cim_default_state;

	default:
		DEBUG(0, ": error!  Illegal last cmd %d\n", dev->request.cmd);
		return mmc_cim_default_state;
	}
	return NULL;
}

/*
  CIM_INIT_STACK       (frequency at 400 kHz)

  1. GO_IDLE_STATE (CMD0)
  2. Do CIM_SINGLE_CARD_ACQ
*/

static void * mmc_cim_init_stack( struct mmc_dev *dev, int first )
{
	DEBUG(2,"\n");

	if ( first ) {
		mmc_simple_cmd(dev, MMC_CIM_RESET, 0, RESPONSE_NONE);
		return NULL;
	}

	switch (dev->request.cmd) {
	case MMC_CIM_RESET:
		if ( dev->slot[0].state == CARD_STATE_EMPTY )
			return mmc_cim_default_state;

		dev->slot[0].state = CARD_STATE_IDLE;
		return mmc_cim_single_card_acq;

	default:
		DEBUG(0,": invalid state %d\n", dev->request.cmd);
		break;
	}

	return NULL;
}

/******************************************************************
 *  Default state - start here
 ******************************************************************/

static void * mmc_cim_default_state( struct mmc_dev *dev, int first )
{
	DEBUG(2,"\n");

	mmc_check_eject(dev);

	if (mmc_check_insert(dev))
		return mmc_cim_init_stack;
	else if (mmc_has_valid_request(dev))
		return mmc_cim_handle_request;

	return NULL;
}


/******************************************************************
 *  State function handler
 ******************************************************************/

typedef void *(*state_func_t)(struct mmc_dev *, int);
static state_func_t g_single_card = &mmc_cim_default_state;

void mmc_protocol_single_card( struct mmc_dev *dev, int state_flags )
{
	state_func_t    sf;

	sf = g_single_card(dev,0);
	while ( sf ) {
		g_single_card = sf;
		sf = g_single_card(dev,1);
	}
}
