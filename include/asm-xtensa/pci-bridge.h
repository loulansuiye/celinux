#ifndef __ASM_XTENSA_PCI_BRIDGE_H
#define __ASM_XTENSA_PCI_BRIDGE_H

/*
 * include/asm-xtensa/pci-bridge.h
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 */


#ifdef __KERNEL__

struct device_node;
struct pci_controller;

/*
 * pci_io_base returns the memory address at which you can access
 * the I/O space for PCI bus number `bus' (or NULL on error).
 */
extern void *pci_bus_io_base(unsigned int bus);
extern unsigned long pci_bus_io_base_phys(unsigned int bus);
extern unsigned long pci_bus_mem_base_phys(unsigned int bus);

/*
 * pciauto_bus_scan() enumerates the pci space.
 */
extern int pciauto_bus_scan(struct pci_controller *, int);

/* Get the PCI host controller for a bus */
extern struct pci_controller* pci_bus_to_hose(int bus);

/*
 * Structure of a PCI controller (host bridge)
 */
struct pci_controller {
	int index;			/* used for pci_controller_num */
	struct pci_controller *next;
        struct pci_bus *bus;
	void *arch_data;

	int first_busno;
	int last_busno;
        
	void *io_base_virt;
	unsigned long io_base_phys;
	
	/* Some machines (PReP) have a non 1:1 mapping of
	 * the PCI memory space in the CPU bus space
	 */
	unsigned long pci_mem_offset;

	struct pci_ops *ops;
	volatile unsigned int *cfg_addr;
	volatile unsigned char *cfg_data;

	/* Currently, we limit ourselves to 1 IO range and 3 mem
	 * ranges since the common pci_bus structure can't handle more
	 */
	struct resource	io_resource;
	struct resource mem_resources[3];
	int mem_resource_count;

	/* Host bridge I/O and Memory space
	 * Used for BAR placement algorithms
	 */
	struct resource io_space;
	struct resource mem_space;
};

/* These are used for config access before all the PCI probing
   has been done. */
int early_read_config_byte(struct pci_controller *hose, int bus, int dev_fn, int where, u8 *val);
int early_read_config_word(struct pci_controller *hose, int bus, int dev_fn, int where, u16 *val);
int early_read_config_dword(struct pci_controller *hose, int bus, int dev_fn, int where, u32 *val);
int early_write_config_byte(struct pci_controller *hose, int bus, int dev_fn, int where, u8 val);
int early_write_config_word(struct pci_controller *hose, int bus, int dev_fn, int where, u16 val);
int early_write_config_dword(struct pci_controller *hose, int bus, int dev_fn, int where, u32 val);

#endif /* __KERNEL__ */

#endif /* __ASM_XTENSA_PCI_BRIDGE_H */
