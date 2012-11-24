#ifndef __ASM_XTENSA_BUGS_H
#define __ASM_XTENSA_BUGS_H

/*
 * include/asm-xtensa/bugs.h
 *
 * This is included by init/main.c to check for architecture-dependent
 * bugs.
 *
 * Needs:
 *	void check_bugs(void);
 *
 * Xtensa processors don't have any bugs.  :)
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 */


#include <asm/processor.h>

static void __init check_bugs(void)
{
}

#endif /* __ASM_XTENSA_BUGS_H */