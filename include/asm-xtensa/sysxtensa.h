#ifndef __ASM_XTENSA_SYSXTENSA_H
#define __ASM_XTENSA_SYSXTENSA_H

/*
 * include/asm-xtensa/sysxtensa.h
 *
 * Definitions for the Xtensa sysxtensa call
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 Tensilica Inc.
 */


#define XTENSA_RESERVED		0	/* don't use this */
#define XTENSA_ATOMIC_SET	1	/* atomically set variable */
#define XTENSA_ATOMIC_EXG_ADD	2	/* atomically exchange memory and add */
#define XTENSA_ATOMIC_ADD	3	/* atomically add to memory */
#define XTENSA_ATOMIC_CMP_SWP	4	/* atomically compare and swap */

/* Keep this count last. */
#define XTENSA_SYSXTENSA_COUNT	5	/* count of sysxtensa functions */


#endif /* __ASM_XTENSA_SYSXTENSA_H */
