/*
 * Core MMC driver functions
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
 * Author:  Andrew Christian
 *          6 May 2002
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


#include <linux/config.h>
#include <linux/module.h>

#include <linux/version.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/sysctl.h>
#include <linux/pm.h>

#include "mmc_core.h"

#define STATE_CMD_ACTIVE   (1<<0)
#define STATE_CMD_DONE     (1<<1)
#define STATE_INSERT       (1<<2)
#define STATE_EJECT        (1<<3)

static struct mmc_dev          g_mmc_dev;
static struct proc_dir_entry  *proc_mmc_dir;

#ifdef CONFIG_MMC_DEBUG
int g_mmc_debug = CONFIG_MMC_DEBUG_VERBOSE;
#else
int g_mmc_debug = 0;
#endif
EXPORT_SYMBOL(g_mmc_debug);

/**************************************************************************
 * Debugging functions
 **************************************************************************/

static char * mmc_result_strings[] = {
	"NO_RESPONSE",
	"NO_ERROR",
	"ERROR_OUT_OF_RANGE",
	"ERROR_ADDRESS",
	"ERROR_BLOCK_LEN",
	"ERROR_ERASE_SEQ",
	"ERROR_ERASE_PARAM",
	"ERROR_WP_VIOLATION",
	"ERROR_CARD_IS_LOCKED",
	"ERROR_LOCK_UNLOCK_FAILED",
	"ERROR_COM_CRC",
	"ERROR_ILLEGAL_COMMAND",
	"ERROR_CARD_ECC_FAILED",
	"ERROR_CC",
	"ERROR_GENERAL",
	"ERROR_UNDERRUN",
	"ERROR_OVERRUN",
	"ERROR_CID_CSD_OVERWRITE",
	"ERROR_STATE_MISMATCH",
	"ERROR_HEADER_MISMATCH",
	"ERROR_TIMEOUT",
	"ERROR_CRC",
	"ERROR_DRIVER_FAILURE",
};

char * mmc_result_to_string( int i )
{
	return mmc_result_strings[i+1];
}

static char * card_state_strings[] = {
	"empty",
	"idle",
	"ready",
	"ident",
	"stby",
	"tran",
	"data",
	"rcv",
	"prg",
	"dis",
};

static inline char * card_state_to_string( int i )
{
	return card_state_strings[i+1];
}

/**************************************************************************
 * Utility functions
 **************************************************************************/

#define PARSE_U32(_buf,_index) \
	(((u32)_buf[_index]) << 24) | (((u32)_buf[_index+1]) << 16) | \
        (((u32)_buf[_index+2]) << 8) | ((u32)_buf[_index+3]);

#define PARSE_U16(_buf,_index) \
	(((u16)_buf[_index]) << 8) | ((u16)_buf[_index+1]);

int mmc_unpack_csd( struct mmc_request *request, struct mmc_csd *csd )
{
	u8 *buf = request->response;
	
	if ( request->result ) return request->result;

	csd->csd_structure      = (buf[1] & 0xc0) >> 6;
	csd->spec_vers          = (buf[1] & 0x3c) >> 2;
	csd->taac               = buf[2];
	csd->nsac               = buf[3];
	csd->tran_speed         = buf[4];
	csd->ccc                = (((u16)buf[5]) << 4) | ((buf[6] & 0xf0) >> 4);
	csd->read_bl_len        = buf[6] & 0x0f;
	csd->read_bl_partial    = (buf[7] & 0x80) ? 1 : 0;
	csd->write_blk_misalign = (buf[7] & 0x40) ? 1 : 0;
	csd->read_blk_misalign  = (buf[7] & 0x20) ? 1 : 0;
	csd->dsr_imp            = (buf[7] & 0x10) ? 1 : 0;
	csd->c_size             = ((((u16)buf[7]) & 0x03) << 10) | (((u16)buf[8]) << 2) | (((u16)buf[9]) & 0xc0) >> 6;
	csd->vdd_r_curr_min     = (buf[9] & 0x38) >> 3;
	csd->vdd_r_curr_max     = buf[9] & 0x07;
	csd->vdd_w_curr_min     = (buf[10] & 0xe0) >> 5;
	csd->vdd_w_curr_max     = (buf[10] & 0x1c) >> 2;
	csd->c_size_mult        = ((buf[10] & 0x03) << 1) | ((buf[11] & 0x80) >> 7);
	switch ( csd->csd_structure ) {
	case CSD_STRUCT_VER_1_0:
	case CSD_STRUCT_VER_1_1:
		csd->erase.v22.sector_size    = (buf[11] & 0x7c) >> 2;
		csd->erase.v22.erase_grp_size = ((buf[11] & 0x03) << 3) | ((buf[12] & 0xe0) >> 5);
		break;
	case CSD_STRUCT_VER_1_2:
	default:
		csd->erase.v31.erase_grp_size = (buf[11] & 0x7c) >> 2;
		csd->erase.v31.erase_grp_mult = ((buf[11] & 0x03) << 3) | ((buf[12] & 0xe0) >> 5);
		break;
	}
	csd->wp_grp_size        = buf[12] & 0x1f;
	csd->wp_grp_enable      = (buf[13] & 0x80) ? 1 : 0;
	csd->default_ecc        = (buf[13] & 0x60) >> 5;
	csd->r2w_factor         = (buf[13] & 0x1c) >> 2;
	csd->write_bl_len       = ((buf[13] & 0x03) << 2) | ((buf[14] & 0xc0) >> 6);
	csd->write_bl_partial   = (buf[14] & 0x20) ? 1 : 0;
	csd->file_format_grp    = (buf[15] & 0x80) ? 1 : 0;
	csd->copy               = (buf[15] & 0x40) ? 1 : 0;
	csd->perm_write_protect = (buf[15] & 0x20) ? 1 : 0;
	csd->tmp_write_protect  = (buf[15] & 0x10) ? 1 : 0;
	csd->file_format        = (buf[15] & 0x0c) >> 2;
	csd->ecc                = buf[15] & 0x03;

	DEBUG(2,"  csd_structure=%d  spec_vers=%d  taac=%02x  nsac=%02x  tran_speed=%02x\n"
	      "  ccc=%04x  read_bl_len=%d  read_bl_partial=%d  write_blk_misalign=%d\n"
	      "  read_blk_misalign=%d  dsr_imp=%d  c_size=%d  vdd_r_curr_min=%d\n"
	      "  vdd_r_curr_max=%d  vdd_w_curr_min=%d  vdd_w_curr_max=%d  c_size_mult=%d\n"
	      "  wp_grp_size=%d  wp_grp_enable=%d  default_ecc=%d  r2w_factor=%d\n"
	      "  write_bl_len=%d  write_bl_partial=%d  file_format_grp=%d  copy=%d\n"
	      "  perm_write_protect=%d  tmp_write_protect=%d  file_format=%d  ecc=%d\n",
	      csd->csd_structure, csd->spec_vers, 
	      csd->taac, csd->nsac, csd->tran_speed,
	      csd->ccc, csd->read_bl_len, 
	      csd->read_bl_partial, csd->write_blk_misalign,
	      csd->read_blk_misalign, csd->dsr_imp, 
	      csd->c_size, csd->vdd_r_curr_min,
	      csd->vdd_r_curr_max, csd->vdd_w_curr_min, 
	      csd->vdd_w_curr_max, csd->c_size_mult,
	      csd->wp_grp_size, csd->wp_grp_enable,
	      csd->default_ecc, csd->r2w_factor, 
	      csd->write_bl_len, csd->write_bl_partial,
	      csd->file_format_grp, csd->copy, 
	      csd->perm_write_protect, csd->tmp_write_protect,
	      csd->file_format, csd->ecc);
	switch (csd->csd_structure) {
	case CSD_STRUCT_VER_1_0:
	case CSD_STRUCT_VER_1_1:
		DEBUG(2," V22 sector_size=%d erase_grp_size=%d\n", 
		      csd->erase.v22.sector_size, 
		      csd->erase.v22.erase_grp_size);
		break;
	case CSD_STRUCT_VER_1_2:
	default:
		DEBUG(2," V31 erase_grp_size=%d erase_grp_mult=%d\n", 
		      csd->erase.v31.erase_grp_size,
		      csd->erase.v31.erase_grp_mult);
		break;
		
	}

	if ( buf[0] != 0x3f )  return MMC_ERROR_HEADER_MISMATCH;

	return 0;
}

int mmc_unpack_r1( struct mmc_request *request, struct mmc_response_r1 *r1, enum card_state state )
{
	u8 *buf = request->response;

	if ( request->result )        return request->result;

	r1->cmd    = buf[0];
	r1->status = PARSE_U32(buf,1);

	DEBUG(2," cmd=%d status=%08x\n", r1->cmd, r1->status);

	if (R1_STATUS(r1->status)) {
		if ( r1->status & R1_OUT_OF_RANGE )       return MMC_ERROR_OUT_OF_RANGE;
		if ( r1->status & R1_ADDRESS_ERROR )      return MMC_ERROR_ADDRESS;
		if ( r1->status & R1_BLOCK_LEN_ERROR )    return MMC_ERROR_BLOCK_LEN;
		if ( r1->status & R1_ERASE_SEQ_ERROR )    return MMC_ERROR_ERASE_SEQ;
		if ( r1->status & R1_ERASE_PARAM )        return MMC_ERROR_ERASE_PARAM;
		if ( r1->status & R1_WP_VIOLATION )       return MMC_ERROR_WP_VIOLATION;
		if ( r1->status & R1_CARD_IS_LOCKED )     return MMC_ERROR_CARD_IS_LOCKED;
		if ( r1->status & R1_LOCK_UNLOCK_FAILED ) return MMC_ERROR_LOCK_UNLOCK_FAILED;
		if ( r1->status & R1_COM_CRC_ERROR )      return MMC_ERROR_COM_CRC;
		if ( r1->status & R1_ILLEGAL_COMMAND )    return MMC_ERROR_ILLEGAL_COMMAND;
		if ( r1->status & R1_CARD_ECC_FAILED )    return MMC_ERROR_CARD_ECC_FAILED;
		if ( r1->status & R1_CC_ERROR )           return MMC_ERROR_CC;
		if ( r1->status & R1_ERROR )              return MMC_ERROR_GENERAL;
		if ( r1->status & R1_UNDERRUN )           return MMC_ERROR_UNDERRUN;
		if ( r1->status & R1_OVERRUN )            return MMC_ERROR_OVERRUN;
		if ( r1->status & R1_CID_CSD_OVERWRITE )  return MMC_ERROR_CID_CSD_OVERWRITE;
	}

	if ( buf[0] != request->cmd ) return MMC_ERROR_HEADER_MISMATCH;

	/* This should be last - it's the least dangerous error */
	if ( R1_CURRENT_STATE(r1->status) != state ) return MMC_ERROR_STATE_MISMATCH;

	return 0;
}

int mmc_unpack_scr(struct mmc_request *request, struct mmc_response_r1 *r1, enum card_state state, u32 *scr)
{
        u8 *buf = request->response;
	if ( request->result )        return request->result;
        
        *scr = PARSE_U32(buf, 5); /* Save SCR returned by the SD Card */
        return mmc_unpack_r1(request, r1, state);
        
}

int mmc_unpack_r6(struct mmc_request *request, struct mmc_response_r1 *r1, enum card_state state, int *rca)
{
	u8 *buf = request->response;

	if ( request->result )        return request->result;
        
        *rca = PARSE_U16(buf,1);  /* Save RCA returned by the SD Card */
        
        *(buf+1) = 0;
        *(buf+2) = 0;
        
        return mmc_unpack_r1(request, r1, state);
}   
   
int mmc_unpack_cid( struct mmc_request *request, struct mmc_cid *cid )
{
	u8 *buf = request->response;
	int i;

	if ( request->result ) return request->result;

	cid->mid = buf[1];
	cid->oid = PARSE_U16(buf,2);
	for ( i = 0 ; i < 6 ; i++ )
		cid->pnm[i] = buf[4+i];
	cid->pnm[6] = 0;
	cid->prv = buf[10];
	cid->psn = PARSE_U32(buf,11);
	cid->mdt = buf[15];
	
	DEBUG(2," mid=%d oid=%d pnm=%s prv=%d.%d psn=%08x mdt=%d/%d\n",
	      cid->mid, cid->oid, cid->pnm, 
	      (cid->prv>>4), (cid->prv&0xf), 
	      cid->psn, (cid->mdt>>4), (cid->mdt&0xf)+1997);

	if ( buf[0] != 0x3f )  return MMC_ERROR_HEADER_MISMATCH;
      	return 0;
}

int mmc_unpack_r3( struct mmc_request *request, struct mmc_response_r3 *r3 )
{
	u8 *buf = request->response;

	if ( request->result ) return request->result;

	r3->ocr = PARSE_U32(buf,1);
	DEBUG(2," ocr=%08x\n", r3->ocr);

	if ( buf[0] != 0x3f )  return MMC_ERROR_HEADER_MISMATCH;
	return 0;
}

/**************************************************************************/

#define KBPS 1
#define MBPS 1000

static u32 ts_exp[] = { 100*KBPS, 1*MBPS, 10*MBPS, 100*MBPS, 0, 0, 0, 0 };
static u32 ts_mul[] = { 0,    1000, 1200, 1300, 1500, 2000, 2500, 3000, 
			3500, 4000, 4500, 5000, 5500, 6000, 7000, 8000 };

u32 mmc_tran_speed( u8 ts )
{
	u32 rate = ts_exp[(ts & 0x7)] * ts_mul[(ts & 0x78) >> 3];

	if ( rate <= 0 ) {
		DEBUG(0, ": error - unrecognized speed 0x%02x\n", ts);
		return 1;
	}

	return rate;
}

/**************************************************************************/

void mmc_send_cmd( struct mmc_dev *dev, int cmd, u32 arg, 
		   u16 nob, u16 block_len, enum mmc_rsp_t rtype )
{
	dev->request.cmd       = cmd;
	dev->request.arg       = arg;
	dev->request.rtype     = rtype;
	dev->request.nob       = nob;
	dev->request.block_len = block_len;
	dev->request.buffer    = NULL;
	if ( nob && dev->io_request )
		dev->request.buffer = dev->io_request->buffer;

	dev->state  |= STATE_CMD_ACTIVE;
	dev->sdrive->send_cmd(&dev->request);
}

void mmc_finish_io_request( struct mmc_dev *dev, int result )
{
	struct mmc_io_request *t = dev->io_request;
	struct mmc_slot *slot = dev->slot + t->id;

	dev->io_request = NULL;     // Remove the old request (the media driver may requeue)
	if ( slot->media_driver )
		slot->media_driver->io_request_done( t, result );
}


/* Only call this when there is no pending request - it unloads the media driver */
int mmc_check_eject( struct mmc_dev *dev )
{
	unsigned long   flags;
	int             state;
	int             i;

	DEBUG(2," dev state=%x\n", dev->state);

	local_irq_save(flags);
	state = dev->state;
	dev->state = state & ~STATE_EJECT;
	local_irq_restore(flags);

	if ( !(state & STATE_EJECT) )
		return 0;

	for ( i = 0 ; i < dev->num_slots ; i++ ) {
		struct mmc_slot *slot = dev->slot + i;

		if ( slot->flags & MMC_SLOT_FLAG_EJECT ) {
			slot->state = CARD_STATE_EMPTY;
			if ( slot->media_driver ) {
				slot->media_driver->unload( slot );
				slot->media_driver = NULL;
			}
			slot->flags &= ~MMC_SLOT_FLAG_EJECT;
			run_sbin_mmc_hotplug(dev,i,0);
		}
	}
	return 1;
}

int mmc_check_insert( struct mmc_dev *dev )
{
	unsigned long   flags;
	int             state;
	int             i;
	int             card_count = 0;

	DEBUG(2," dev state=%x\n", dev->state);

	local_irq_save(flags);
	state = dev->state;
	dev->state = state & ~STATE_INSERT;
	local_irq_restore(flags);

	if ( !(state & STATE_INSERT) ) 
		return 0;

	for ( i = 0 ; i < dev->num_slots ; i++ ) {
		struct mmc_slot *slot = dev->slot + i;

		if ( slot->flags & MMC_SLOT_FLAG_INSERT ) {
			if  (!dev->sdrive->is_empty(i)) {
				slot->state = CARD_STATE_IDLE;
				card_count++;
			}
			slot->flags &= ~MMC_SLOT_FLAG_INSERT;
		}
	}
	return card_count;
}

/******************************************************************
 *
 * Hotplug callback card insertion/removal
 *
 ******************************************************************/
 
#ifndef CONFIG_OMAP_INNOVATOR
#ifdef CONFIG_HOTPLUG

extern char hotplug_path[];
extern int call_usermodehelper(char *path, char **argv, char **envp);

static void run_sbin_hotplug(struct mmc_dev *dev, int id, int insert)
{
	int i;
	char *argv[3], *envp[8];
	char media[64], slotnum[16];

	if (!hotplug_path[0])
		return;

	DEBUG(0,": hotplug_path=%s id=%d insert=%d\n", hotplug_path, id, insert);

	i = 0;
	argv[i++] = hotplug_path;
	argv[i++] = "mmc";
	argv[i] = 0;

	/* minimal command environment */
	i = 0;
	envp[i++] = "HOME=/";
	envp[i++] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";
	
	/* other stuff we want to pass to /sbin/hotplug */
	sprintf(slotnum, "SLOT=%d", id );
	if ( dev->slot[id].media_driver && dev->slot[id].media_driver->name )
		sprintf(media, "MEDIA=%s", dev->slot[id].media_driver->name );
	else
		sprintf(media, "MEDIA=unknown");

	envp[i++] = slotnum;
	envp[i++] = media;

	if (insert)
		envp[i++] = "ACTION=add";
	else
		envp[i++] = "ACTION=remove";
	envp[i] = 0;

	call_usermodehelper (argv [0], argv, envp);
}

static void mmc_hotplug_task_handler( void *nr )
{
	int insert = ((int) nr) & 0x01;
	int id     = ((int) nr) >> 1;
	DEBUG(2," id=%d insert=%d\n", id, insert );
	run_sbin_hotplug(&g_mmc_dev, id, insert );
}

static struct tq_struct mmc_hotplug_task = {
	routine:  mmc_hotplug_task_handler
};

void run_sbin_mmc_hotplug(struct mmc_dev *dev, int id, int insert )
{
	mmc_hotplug_task.data = (void *) ((id << 1) | insert);
	schedule_task( &mmc_hotplug_task );
}

#else
void run_sbin_mmc_hotplug(struct sleeve_dev *sdev, int insert) { }
#endif /* CONFIG_HOTPLUG */
#else
void run_sbin_mmc_hotplug(struct mmc_dev *sdev,  int id, int insert) { }
#endif

/******************************************************************
 * Common processing tasklet
 * Everything gets serialized through this
 ******************************************************************/

static void mmc_tasklet_action(unsigned long data)
{
	struct mmc_dev *dev = (struct mmc_dev *)data;
	unsigned long   flags;
	int             state;

	DEBUG(2,": dev=%p flags=%02x\n", dev, dev->state);

	/* Grab the current working state */
	local_irq_save(flags);
	state = dev->state;
	if ( state & STATE_CMD_DONE )
		dev->state = state & ~(STATE_CMD_DONE | STATE_CMD_ACTIVE);
	local_irq_restore(flags);

	/* If there is an active command, don't do anything */
	if ( (state & STATE_CMD_ACTIVE) && !(state & STATE_CMD_DONE) )
		return;

	if ( dev->protocol )
		dev->protocol(dev,state);
}

/******************************************************************
 * Callbacks from low-level driver
 * These run at interrupt time
 ******************************************************************/

void mmc_cmd_complete(struct mmc_request *request)
{
	DEBUG(2,": request=%p retval=%d\n", request, request->result);
	g_mmc_dev.state |= STATE_CMD_DONE;
	if ( !g_mmc_dev.suspended )
		tasklet_schedule(&g_mmc_dev.task);
}

void mmc_insert(int slot)
{
	DEBUG(2,": %d\n", slot);
	g_mmc_dev.state |= STATE_INSERT;
	g_mmc_dev.slot[slot].flags |= MMC_SLOT_FLAG_INSERT;
	if ( !g_mmc_dev.suspended )
		tasklet_schedule(&g_mmc_dev.task);
}

void mmc_eject(int slot)
{
	DEBUG(2,": %d\n", slot);
	g_mmc_dev.state |= STATE_EJECT;
	g_mmc_dev.slot[slot].flags |= MMC_SLOT_FLAG_EJECT;
	if ( !g_mmc_dev.suspended )
		tasklet_schedule(&g_mmc_dev.task);
}

EXPORT_SYMBOL(mmc_cmd_complete);
EXPORT_SYMBOL(mmc_insert);
EXPORT_SYMBOL(mmc_eject);

/******************************************************************
 * Called from the media handler
 ******************************************************************/

void mmc_handle_io_request( struct mmc_io_request *t )
{
	DEBUG(2," id=%d cmd=%d sector=%ld nr_sectors=%ld block_len=%ld buf=%p\n",
	      t->id, t->cmd, t->sector, t->nr_sectors, t->block_len, t->buffer);
	
	if ( g_mmc_dev.io_request ) {
		DEBUG(0,": error! io_request in progress\n");
		return;
	}
	
	g_mmc_dev.io_request = t;
	tasklet_schedule(&g_mmc_dev.task);
}

EXPORT_SYMBOL(mmc_handle_io_request);

/******************************************************************
 *  Media handlers
 *  Allow different drivers to register a media handler
 ******************************************************************/

static LIST_HEAD(mmc_media_drivers);

int mmc_match_media_driver( struct mmc_slot *slot )
{
	struct list_head *item;

	DEBUG(2,": slot=%p\n", slot);

	for ( item = mmc_media_drivers.next ; item != &mmc_media_drivers ; item = item->next ) {
		struct mmc_media_driver *drv = list_entry(item, struct mmc_media_driver, node );
		if ( drv->probe(slot) ) {
			slot->media_driver = drv;
			drv->load(slot);
			return 1;
		}
	}
	return 0;
}

int mmc_register_media_driver( struct mmc_media_driver *drv )
{
	list_add_tail( &drv->node, &mmc_media_drivers );
	return 0;
}

void mmc_unregister_media_driver( struct mmc_media_driver *drv )
{
	list_del(&drv->node);
}

EXPORT_SYMBOL(mmc_register_media_driver);
EXPORT_SYMBOL(mmc_unregister_media_driver);

/******************************************************************/

int mmc_register_slot_driver( struct mmc_slot_driver *sdrive, int num_slots )
{
	int i;
	int retval;

	DEBUG(2," max=%d ocr=0x%08x\n", num_slots, sdrive->ocr);

	if ( num_slots > MMC_MAX_SLOTS ) {
		printk(KERN_CRIT __FUNCTION__ ": illegal num of slots %d\n", num_slots );
		return -ENOMEM;
	}

	if ( g_mmc_dev.sdrive ) {
		printk(KERN_ALERT __FUNCTION__ ": slot in use\n");
		return -EBUSY;
	}

	g_mmc_dev.sdrive    = sdrive;
	g_mmc_dev.num_slots = num_slots;

	for ( i = 0 ; i < num_slots ; i++ ) {
		struct mmc_slot *slot = &g_mmc_dev.slot[i];
		memset(slot,0,sizeof(struct mmc_slot));
		slot->id = i;
		slot->state = CARD_STATE_EMPTY;
	}
	retval = sdrive->init();
	if ( retval )
		return retval;

	/* Generate insert events for cards */
	for ( i = 0 ; i < num_slots ; i++ )
		if ( !sdrive->is_empty(i) )
			mmc_insert(i);
	return 0;
}

void mmc_unregister_slot_driver( struct mmc_slot_driver *sdrive )
{
	int i;
	DEBUG(2,"\n");

	for ( i = 0 ; i < g_mmc_dev.num_slots ; i++ )
		mmc_eject(i);

	if ( sdrive == g_mmc_dev.sdrive ) {
		g_mmc_dev.sdrive->cleanup();
		g_mmc_dev.sdrive = NULL;
	}
}

EXPORT_SYMBOL(mmc_register_slot_driver);
EXPORT_SYMBOL(mmc_unregister_slot_driver);

/******************************************************************/

static struct pm_dev *mmc_pm_dev;

static int mmc_pm_callback(struct pm_dev *pm_dev, pm_request_t req, void *data)
{
	int i;
	DEBUG(0,": pm callback %d\n", req );

	switch (req) {
	case PM_SUSPEND: /* Enter D1-D3 */
		g_mmc_dev.suspended = 1;
                break;
	case PM_RESUME:  /* Enter D0 */
		if ( g_mmc_dev.suspended ) {
			g_mmc_dev.suspended = 0;
			g_mmc_dev.state = 0;     // Clear the old state
			for ( i = 0 ; i < g_mmc_dev.num_slots ; i++ ) {
				mmc_eject(i);
				if ( !g_mmc_dev.sdrive->is_empty(i) )
					mmc_insert(i);
			}
		}
		break;
        }
        return 0;
}

/******************************************************************
 * TODO: These would be better handled by driverfs
 * For the moment, we'll just eject and insert everything
 ******************************************************************/

int mmc_do_eject(ctl_table *ctl, int write, struct file * filp, void *buffer, size_t *lenp)
{
	mmc_eject(0);
	return 0;
}

int mmc_do_insert(ctl_table *ctl, int write, struct file * filp, void *buffer, size_t *lenp)
{
	mmc_insert(0);
	return 0;
}


static struct ctl_table mmc_sysctl_table[] =
{
	{ 1, "debug", &g_mmc_debug, sizeof(int), 0666, NULL, &proc_dointvec },
	{ 2, "eject", NULL, 0, 0600, NULL, &mmc_do_eject },
	{ 3, "insert", NULL, 0, 0600, NULL, &mmc_do_insert },
	{0}
};

static struct ctl_table mmc_dir_table[] =
{
	{BUS_MMC, "mmc", NULL, 0, 0555, mmc_sysctl_table},
	{0}
};

static struct ctl_table bus_dir_table[] = 
{
	{CTL_BUS, "bus", NULL, 0, 0555, mmc_dir_table},
        {0}
};

static struct ctl_table_header *mmc_sysctl_header;

static int mmc_proc_read_device(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	struct mmc_dev *dev = (struct mmc_dev *)data;
        char *p = page;
        int len = 0;
	int i;

	if (!dev || !dev->sdrive)
		return 0;

	for ( i = 0 ; i < dev->num_slots ; i++ ) {
		struct mmc_slot *slot = &dev->slot[i];

		p += sprintf(p, "Slot #%d\n", i);
		p += sprintf(p, "  State %s (%d)\n", card_state_to_string(slot->state), slot->state);

		if ( slot->state != CARD_STATE_EMPTY ) {
			p += sprintf(p, "  Media %s\n", (slot->media_driver ? slot->media_driver->name : "unknown"));
			p += sprintf(p, "  CID mid=%d\n", slot->cid.mid);
			p += sprintf(p, "      oid=%d\n", slot->cid.oid);
			p += sprintf(p, "      pnm=%s\n", slot->cid.pnm);
			p += sprintf(p, "      prv=%d.%d\n", slot->cid.prv>>4, slot->cid.prv&0xf);
			p += sprintf(p, "      psn=0x%08x\n", slot->cid.psn);
			p += sprintf(p, "      mdt=%d/%d\n", slot->cid.mdt>>4, (slot->cid.mdt&0xf)+1997);

			p += sprintf(p, "  CSD csd_structure=%d\n", slot->csd.csd_structure);
			p += sprintf(p, "      spec_vers=%d\n", slot->csd.spec_vers);
			p += sprintf(p, "      taac=0x%02x\n", slot->csd.taac);
			p += sprintf(p, "      nsac=0x%02x\n", slot->csd.nsac);
			p += sprintf(p, "      tran_speed=0x%02x\n", slot->csd.tran_speed);
			p += sprintf(p, "      ccc=0x%04x\n", slot->csd.ccc);
			p += sprintf(p, "      read_bl_len=%d\n", slot->csd.read_bl_len);
			p += sprintf(p, "      read_bl_partial=%d\n", slot->csd.read_bl_partial);
			p += sprintf(p, "      write_blk_misalign=%d\n", slot->csd.write_blk_misalign);
			p += sprintf(p, "      read_blk_misalign=%d\n", slot->csd.read_blk_misalign);
			p += sprintf(p, "      dsr_imp=%d\n", slot->csd.dsr_imp);
			p += sprintf(p, "      c_size=%d\n", slot->csd.c_size);
			p += sprintf(p, "      vdd_r_curr_min=%d\n", slot->csd.vdd_r_curr_min);
			p += sprintf(p, "      vdd_r_curr_max=%d\n", slot->csd.vdd_r_curr_max);
			p += sprintf(p, "      vdd_w_curr_min=%d\n", slot->csd.vdd_w_curr_min);
			p += sprintf(p, "      vdd_w_curr_max=%d\n", slot->csd.vdd_w_curr_max);
			p += sprintf(p, "      c_size_mult=%d\n", slot->csd.c_size_mult);
			p += sprintf(p, "      wp_grp_size=%d\n", slot->csd.wp_grp_size);
			p += sprintf(p, "      wp_grp_enable=%d\n", slot->csd.wp_grp_enable);
			p += sprintf(p, "      default_ecc=%d\n", slot->csd.default_ecc);
			p += sprintf(p, "      r2w_factor=%d\n", slot->csd.r2w_factor);
			p += sprintf(p, "      write_bl_len=%d\n", slot->csd.write_bl_len);
			p += sprintf(p, "      write_bl_partial=%d\n", slot->csd.write_bl_partial);
			p += sprintf(p, "      file_format_grp=%d\n", slot->csd.file_format_grp);
			p += sprintf(p, "      copy=%d\n", slot->csd.copy);
			p += sprintf(p, "      perm_write_protect=%d\n", slot->csd.perm_write_protect);
			p += sprintf(p, "      tmp_write_protect=%d\n", slot->csd.tmp_write_protect);
			p += sprintf(p, "      file_format=%d\n", slot->csd.file_format);
			p += sprintf(p, "      ecc=%d\n", slot->csd.ecc);
			switch (slot->csd.csd_structure) {
			case CSD_STRUCT_VER_1_0:
			case CSD_STRUCT_VER_1_1:
				p += sprintf(p, "      sector_size=%d\n", slot->csd.erase.v22.sector_size);
				p += sprintf(p, "      erase_grp_size=%d\n", slot->csd.erase.v22.erase_grp_size);
				break;
			case CSD_STRUCT_VER_1_2:
			default:
				p += sprintf(p, "      erase_grp_size=%d\n", slot->csd.erase.v31.erase_grp_size);
				p += sprintf(p, "      erase_grp_mult=%d\n", slot->csd.erase.v31.erase_grp_mult);
				break;
			}
		}
	}

        len = (p - page) - off;
	*start = page + off;
        return len;
}

/******************************************************************/

void mmc_protocol_single_card( struct mmc_dev *dev, int state_flags );
extern struct mmc_media_module media_module;

static int __init mmc_init(void) 
{
	DEBUG(1,"\n");
	
	mmc_sysctl_header = register_sysctl_table(bus_dir_table, 0 );

	tasklet_init(&g_mmc_dev.task,mmc_tasklet_action,(unsigned long)&g_mmc_dev);
	g_mmc_dev.protocol = mmc_protocol_single_card;

	proc_mmc_dir = proc_mkdir("mmc", proc_bus);
	if ( proc_mmc_dir )
		create_proc_read_entry("device", 0, proc_mmc_dir, mmc_proc_read_device, &g_mmc_dev);

	mmc_pm_dev = pm_register(PM_UNKNOWN_DEV, PM_SYS_UNKNOWN, mmc_pm_callback);

	media_module.init();
	return 0;
}

static void __exit mmc_exit(void)
{
	DEBUG(1,"\n");

	media_module.cleanup();

	unregister_sysctl_table(mmc_sysctl_header);

	tasklet_kill(&g_mmc_dev.task);

	if ( proc_mmc_dir ) {
		remove_proc_entry("device", proc_mmc_dir);
		remove_proc_entry("mmc", proc_bus);
	}

	pm_unregister(mmc_pm_dev);
}

module_init(mmc_init);
module_exit(mmc_exit);
MODULE_LICENSE("GPL");


