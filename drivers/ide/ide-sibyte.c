/*
 * Copyright (C) 2001, 2002, 2003 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

/*  Derived loosely from ide-pmac.c, so:
 *  
 *  Copyright (C) 1998 Paul Mackerras.
 *  Copyright (C) 1995-1998 Mark Lord
 */
#include <linux/kernel.h>
#include <linux/ide.h>

#include <asm/sibyte/board.h>

ide_hwif_t *sb_ide_hwif = NULL;

static unsigned long ide_base;
#define SIBYTE_IDE_BASE        (KSEG1ADDR(ide_base)-mips_io_port_base)
#define SIBYTE_IDE_REG(pcaddr) (SIBYTE_IDE_BASE + ((pcaddr)<<5))

extern void sibyte_ideproc(ide_ide_action_t action, ide_drive_t *drive,
			   void *buffer, unsigned int count);

void __init sibyte_ide_probe(void)
{
	int i;
	ide_hwif_t *hwif;
	/* 
	 * Find the first untaken slot in hwifs 
	 */
	for (i = 0; i < MAX_HWIFS; i++) {
		if (!ide_hwifs[i].io_ports[IDE_DATA_OFFSET]) {
			break;
		}
	}
	if (i == MAX_HWIFS) {
		printk("No space for SiByte onboard IDE driver in ide_hwifs[].  Not enabled.\n");
		return;
	}

	ide_base = IDE_PHYS|IO_SPACE_BASE;

	/*
	 * Set up our stuff; we're a little odd because our io_ports
	 * aren't in the usual place, and byte-swapping isn't
	 * necessary.
	 */
	hwif = &ide_hwifs[i];
	hwif->hw.io_ports[IDE_DATA_OFFSET]    = SIBYTE_IDE_REG(0x1f0);
	hwif->hw.io_ports[IDE_ERROR_OFFSET]   = SIBYTE_IDE_REG(0x1f1);
	hwif->hw.io_ports[IDE_NSECTOR_OFFSET] = SIBYTE_IDE_REG(0x1f2);
	hwif->hw.io_ports[IDE_SECTOR_OFFSET]  = SIBYTE_IDE_REG(0x1f3);
	hwif->hw.io_ports[IDE_LCYL_OFFSET]    = SIBYTE_IDE_REG(0x1f4);
	hwif->hw.io_ports[IDE_HCYL_OFFSET]    = SIBYTE_IDE_REG(0x1f5);
	hwif->hw.io_ports[IDE_SELECT_OFFSET]  = SIBYTE_IDE_REG(0x1f6);
	hwif->hw.io_ports[IDE_STATUS_OFFSET]  = SIBYTE_IDE_REG(0x1f7);
	hwif->hw.io_ports[IDE_CONTROL_OFFSET] = SIBYTE_IDE_REG(0x3f6);
	hwif->hw.irq                          = K_INT_GB_IDE;
	hwif->irq                             = K_INT_GB_IDE;
	hwif->noprobe                         = 0;
	hwif->hw.ack_intr                     = NULL;
	/* Use our own non-byte-swapping routines */
	hwif->ideproc                         = sibyte_ideproc;

	memcpy(hwif->io_ports, hwif->hw.io_ports, sizeof(hwif->io_ports));
	printk("SiByte onboard IDE configured as device %i\n", i);
	sb_ide_hwif = hwif;
}
