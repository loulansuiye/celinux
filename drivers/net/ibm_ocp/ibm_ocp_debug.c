/*
 * ibm_ocp_debug.c
 *
 * This has all the debug routines that where in *_enet.c
 *
 *      Armin Kuster akuster@mvista.com
 *      April , 2002
 *
 * Copyright 2002 MontaVista Softare Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR   IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT,  INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * TODO: 
 *
 * Version 1.0: (04/15/02)
 * 	initial release 
 * 	These are all the debug routines from *_enet.c and a few new ones. 
 * 	this was done to reduce the *_enet.c code.
 *
 * Version 1.1: 04/24/02
 * 	fixed missind EMAC_DEV macros - Todd
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <asm/io.h>
#include "ibm_ocp_enet.h"
#include "ibm_ocp_mal.h"

int ocp_enet_mdio_read(struct net_device *dev, int reg, uint * value);

void ppc405_phy_dump(struct net_device *dev)
{
	unsigned long i;
	uint data;

	printk(KERN_DEBUG " Prepare for Phy dump....\n");
	for (i = 0; i < 0x1A; i++) {
		if (ocp_enet_mdio_read(dev, mk_mii_read(i), &data))
			return;

		printk(KERN_DEBUG "Phy reg 0x%lx ==> %4x\n", i, data);

		if (i == 0x07)
			i = 0x0f;
	}
}

void
ppc405_eth_desc_dump(struct net_device *dev)
{
	struct ocp_enet_private *fep = dev->priv;
	int curr_slot;

	printk(KERN_DEBUG "dumping the receive descriptors:  current slot is %d\n",
	       fep->rx_slot);
	for (curr_slot = 0; curr_slot < NUM_RX_BUFF; curr_slot++) {
		printk(KERN_DEBUG "Desc %02d: status 0x%04x, length %3d, addr 0x%x\n",
		       curr_slot,
		       fep->rx_desc[curr_slot].ctrl,
		       fep->rx_desc[curr_slot].data_len,
		       (unsigned int) fep->rx_desc[curr_slot].data_ptr);
	}
}

void
ppc405_eth_emac_dump(struct net_device *dev)
{
	struct ocp_enet_private *fep = dev->priv;
	volatile emac_t *emacp = fep->emacp;

	printk(KERN_DEBUG "EMAC DEBUG ********** \n");
	printk(KERN_DEBUG "EMAC_M0  ==> 0x%x\n", in_be32(&emacp->em0mr0));
	printk(KERN_DEBUG "EMAC_M1  ==> 0x%x\n", in_be32(&emacp->em0mr1));
	printk(KERN_DEBUG "EMAC_TXM0==> 0x%x\n", in_be32(&emacp->em0tmr0));
	printk(KERN_DEBUG "EMAC_TXM1==> 0x%x\n", in_be32(&emacp->em0tmr1));
	printk(KERN_DEBUG "EMAC_RXM ==> 0x%x\n", in_be32(&emacp->em0rmr));
	printk(KERN_DEBUG "EMAC_ISR ==> 0x%x\n", in_be32(&emacp->em0isr));
	printk(KERN_DEBUG "EMAC_IER ==> 0x%x\n", in_be32(&emacp->em0iser));
	printk(KERN_DEBUG "EMAC_IAH ==> 0x%x\n", in_be32(&emacp->em0iahr));
	printk(KERN_DEBUG "EMAC_IAL ==> 0x%x\n", in_be32(&emacp->em0ialr));
	printk(KERN_DEBUG "EMAC_VLAN_TPID_REG ==> 0x%x\n", in_be32(&emacp->em0vtpid));
}

void
ppc405_eth_mal_dump(struct net_device *dev)
{
	struct ocp_enet_private *fep = dev->priv;

	printk(KERN_DEBUG " MAL DEBUG ********** \n");
	printk(KERN_DEBUG " MCR      ==> 0x%x\n", 
	       (unsigned int) get_mal_dcrn(fep, DCRN_MALCR));
	printk(KERN_DEBUG " ESR      ==> 0x%x\n", 
	       (unsigned int) get_mal_dcrn(fep, DCRN_MALESR));
	printk(KERN_DEBUG " IER      ==> 0x%x\n", 
	       (unsigned int) get_mal_dcrn(fep, DCRN_MALIER));
#ifdef CONFIG_40x
	printk(KERN_DEBUG " DBR      ==> 0x%x\n", 
	       (unsigned int) get_mal_dcrn(fep, DCRN_MALDBR));
#endif				/* CONFIG_40x */
	printk(KERN_DEBUG " TXCASR   ==> 0x%x\n", 
	       (unsigned int) get_mal_dcrn(fep, DCRN_MALTXCASR));
	printk(KERN_DEBUG " TXCARR   ==> 0x%x\n", 
	       (unsigned int) get_mal_dcrn(fep, DCRN_MALTXCARR));
	printk(KERN_DEBUG " TXEOBISR ==> 0x%x\n", 
	       (unsigned int) get_mal_dcrn(fep, DCRN_MALTXEOBISR));
	printk(KERN_DEBUG " TXDEIR   ==> 0x%x\n", 
	       (unsigned int) get_mal_dcrn(fep, DCRN_MALTXDEIR));
	printk(KERN_DEBUG " RXCASR   ==> 0x%x\n", 
	       (unsigned int) get_mal_dcrn(fep, DCRN_MALRXCASR));
	printk(KERN_DEBUG " RXCARR   ==> 0x%x\n", 
	       (unsigned int) get_mal_dcrn(fep, DCRN_MALRXCARR));
	printk(KERN_DEBUG " RXEOBISR ==> 0x%x\n", 
	       (unsigned int) get_mal_dcrn(fep, DCRN_MALRXEOBISR));
	printk(KERN_DEBUG " RXDEIR   ==> 0x%x\n", 
	       (unsigned int) get_mal_dcrn(fep, DCRN_MALRXDEIR));
	printk(KERN_DEBUG " TXCTP0R  ==> 0x%x\n", 
	       (unsigned int) get_mal_dcrn(fep, DCRN_MALTXCTP0R));
	printk(KERN_DEBUG " TXCTP1R  ==> 0x%x\n", 
	       (unsigned int) get_mal_dcrn(fep, DCRN_MALTXCTP1R));
	printk(KERN_DEBUG " TXCTP2R  ==> 0x%x\n", 
	       (unsigned int) get_mal_dcrn(fep, DCRN_MALTXCTP2R));
	printk(KERN_DEBUG " TXCTP3R  ==> 0x%x\n", 
	       (unsigned int) get_mal_dcrn(fep, DCRN_MALTXCTP3R));
	printk(KERN_DEBUG " RXCTP0R  ==> 0x%x\n", 
	       (unsigned int) get_mal_dcrn(fep, DCRN_MALRXCTP0R));
	printk(KERN_DEBUG " RXCTP1R  ==> 0x%x\n", 
	       (unsigned int) get_mal_dcrn(fep, DCRN_MALRXCTP1R));
	printk(KERN_DEBUG " RCBS0    ==> 0x%x\n", 
	       (unsigned int) get_mal_dcrn(fep, DCRN_MALRCBS0));
	printk(KERN_DEBUG " RCBS1    ==> 0x%x\n", 
	       (unsigned int) get_mal_dcrn(fep, DCRN_MALRCBS1));
}

void
ppc405_serr_dump_0(struct net_device *dev)
{
	struct ocp_enet_private *fep = dev->priv;
	unsigned long int mal_error, plb_error, plb_addr;

	mal_error = get_mal_dcrn(fep, DCRN_MALESR);
	printk(KERN_DEBUG "ppc405_eth_serr: %s channel %ld \n",
	       (mal_error & 0x40000000) ? "Receive" :
	       "Transmit", (mal_error & 0x3e000000) >> 25);
	printk(KERN_DEBUG "  -----  latched error  -----\n");
	if (mal_error & MALESR_DE)
		printk(KERN_DEBUG "  DE: descriptor error\n");
	if (mal_error & MALESR_OEN)
		printk(KERN_DEBUG
		       "  ONE: OPB non-fullword error\n");
	if (mal_error & MALESR_OTE)
		printk(KERN_DEBUG "  OTE: OPB timeout error\n");
	if (mal_error & MALESR_OSE)
		printk(KERN_DEBUG "  OSE: OPB slave error\n");
	
	if (mal_error & MALESR_PEIN) {
		plb_error = mfdcr(DCRN_PLB0_BESR);
		printk(KERN_DEBUG
		       "  PEIN: PLB error, PLB0_BESR is 0x%x\n",
		       (unsigned int) plb_error);
		plb_addr = mfdcr(DCRN_PLB0_BEAR);
		printk(KERN_DEBUG
		       "  PEIN: PLB error, PLB0_BEAR is 0x%x\n",
		       (unsigned int) plb_addr);
	}
}

void
ppc405_serr_dump_1(struct net_device *dev)
{
	struct ocp_enet_private *fep = dev->priv;
	int mal_error = get_mal_dcrn(fep, DCRN_MALESR);

	printk(KERN_DEBUG "  -----  cumulative errors  -----\n");
	if (mal_error & MALESR_DEI)
		printk(KERN_DEBUG
		       "  DEI: descriptor error interrupt\n");
	if (mal_error & MALESR_ONEI)
		printk(KERN_DEBUG
		       "  OPB non-fullword error interrupt\n");
	if (mal_error & MALESR_OTEI)
		printk(KERN_DEBUG
		       "  OTEI: timeout error interrupt\n");
	if (mal_error & MALESR_OSEI)
		printk(KERN_DEBUG
		       "  OSEI: slave error interrupt\n");
	if (mal_error & MALESR_PBEI)
		printk(KERN_DEBUG
		       "  PBEI: PLB bus error interrupt\n");
}

void
ppc405_bl_mac_eth_dump(struct net_device *dev, int em0isr)
{
	printk(KERN_DEBUG "%s: on-chip ethernet error:\n", dev->name);
	
	if (em0isr & EMAC_ISR_OVR)
		printk(KERN_DEBUG "  OVR: overrun\n");
	if (em0isr & EMAC_ISR_PP)
		printk(KERN_DEBUG "  PP: control pause packet\n");
	if (em0isr & EMAC_ISR_BP)
		printk(KERN_DEBUG "  BP: packet error\n");
	if (em0isr & EMAC_ISR_RP)
		printk(KERN_DEBUG "  RP: runt packet\n");
	if (em0isr & EMAC_ISR_SE)
		printk(KERN_DEBUG "  SE: short event\n");
	if (em0isr & EMAC_ISR_ALE)
		printk(KERN_DEBUG
		       "  ALE: odd number of nibbles in packet\n");
	if (em0isr & EMAC_ISR_BFCS)
		printk(KERN_DEBUG "  BFCS: bad FCS\n");
	if (em0isr & EMAC_ISR_PTLE)
		printk(KERN_DEBUG "  PTLE: oversized packet\n");
	if (em0isr & EMAC_ISR_ORE)
		printk(KERN_DEBUG
		       "  ORE: packet length field > max allowed LLC\n");
	if (em0isr & EMAC_ISR_IRE)
		printk(KERN_DEBUG "  IRE: In Range error\n");
	if (em0isr & EMAC_ISR_DBDM)
		printk(KERN_DEBUG "  DBDM: xmit error or SQE\n");
	if (em0isr & EMAC_ISR_DB0)
		printk(KERN_DEBUG
		       "  DB0: xmit error or SQE on TX channel 0\n");
	if (em0isr & EMAC_ISR_SE0)
		printk(KERN_DEBUG
		       "  SE0: Signal Quality Error test failure from TX channel 0\n");
	if (em0isr & EMAC_ISR_TE0)
		printk(KERN_DEBUG "  TE0: xmit channel 0 aborted\n");
	if (em0isr & EMAC_ISR_DB1)
		printk(KERN_DEBUG
		       "  DB1: xmit error or SQE on TX channel \n");
	if (em0isr & EMAC_ISR_SE1)
		printk(KERN_DEBUG
		       "  SE1: Signal Quality Error test failure from TX channel 1\n");
	if (em0isr & EMAC_ISR_TE1)
		printk(KERN_DEBUG "  TE1: xmit channel 1 aborted\n");
	if (em0isr & EMAC_ISR_MOS)
		printk(KERN_DEBUG "  MOS\n");
	if (em0isr & EMAC_ISR_MOF)
		printk(KERN_DEBUG "  MOF\n");
	
	ppc405_eth_emac_dump(dev);
	ppc405_eth_mal_dump(dev);
}
