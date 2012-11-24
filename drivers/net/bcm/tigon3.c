/******************************************************************************/
/*                                                                            */
/* Broadcom BCM5700 Linux Network Driver, Copyright (c) 2000 Broadcom         */
/* Corporation.                                                               */
/* All rights reserved.                                                       */
/*                                                                            */
/* This program is free software; you can redistribute it and/or modify       */
/* it under the terms of the GNU General Public License as published by       */
/* the Free Software Foundation, located in the file LICENSE.                 */
/*                                                                            */
/* History:                                                                   */
/******************************************************************************/

#include "mm.h"



/******************************************************************************/
/* Local functions. */
/******************************************************************************/

LM_STATUS LM_Abort(PLM_DEVICE_BLOCK pDevice);
LM_STATUS LM_QueueRxPackets(PLM_DEVICE_BLOCK pDevice);

static LM_STATUS LM_InitBcm540xPhy(PLM_DEVICE_BLOCK pDevice);

__inline static LM_VOID LM_ServiceRxInterrupt(PLM_DEVICE_BLOCK pDevice);
__inline static LM_VOID LM_ServiceTxInterrupt(PLM_DEVICE_BLOCK pDevice);

static LM_STATUS LM_ForceAutoNeg(PLM_DEVICE_BLOCK pDevice);
static LM_UINT32 GetPhyAdFlowCntrlSettings(PLM_DEVICE_BLOCK pDevice);
STATIC LM_STATUS LM_SetFlowControl(PLM_DEVICE_BLOCK pDevice,
    LM_UINT32 LocalPhyAd, LM_UINT32 RemotePhyAd);
STATIC LM_STATUS LM_SetupFiberPhy(PLM_DEVICE_BLOCK pDevice);
STATIC LM_STATUS LM_SetupCopperPhy(PLM_DEVICE_BLOCK pDevice);
STATIC LM_VOID LM_ResetPhy(LM_DEVICE_BLOCK *pDevice);
STATIC LM_STATUS LM_PhyAdvertiseAll(LM_DEVICE_BLOCK *pDevice);
STATIC LM_STATUS LM_InitBcm800xPhy(PLM_DEVICE_BLOCK pDevice);
STATIC PLM_ADAPTER_INFO LM_GetAdapterInfoBySsid(LM_UINT16 Svid, LM_UINT16 Ssid);
STATIC LM_STATUS LM_SwitchClocks(PLM_DEVICE_BLOCK pDevice);
LM_VOID LM_SwitchVaux(PLM_DEVICE_BLOCK pDevice);
STATIC LM_STATUS LM_DmaTest(PLM_DEVICE_BLOCK pDevice, PLM_UINT8 pBufferVirt,
           LM_PHYSICAL_ADDRESS BufferPhy, LM_UINT32 BufferSize);
STATIC LM_STATUS LM_HaltCpu(PLM_DEVICE_BLOCK pDevice,LM_UINT32 cpu_number);
STATIC LM_STATUS LM_ResetChip(PLM_DEVICE_BLOCK pDevice);
STATIC LM_STATUS LM_DisableChip(PLM_DEVICE_BLOCK pDevice);
STATIC LM_STATUS LM_DisableFW(PLM_DEVICE_BLOCK pDevice);
STATIC LM_STATUS LM_Test4GBoundary(PLM_DEVICE_BLOCK pDevice, PLM_PACKET pPacket,
    PT3_SND_BD pSendBd);


/******************************************************************************/
/* External functions. */
/******************************************************************************/

LM_STATUS LM_LoadRlsFirmware(PLM_DEVICE_BLOCK pDevice);

LM_UINT32
LM_ByteSwap(LM_UINT32 Value32)
{
    return ((Value32 & 0xff) << 24)| ((Value32 & 0xff00) << 8)|
        ((Value32 & 0xff0000) >> 8) | ((Value32 >> 24) & 0xff);
}

LM_UINT32
LM_RegRd(PLM_DEVICE_BLOCK pDevice, LM_UINT32 Register)
{
#if PCIX_TARGET_WORKAROUND
    if (pDevice->UndiFix)
    {
        return (LM_RegRdInd(pDevice, Register));
    }
    else
#endif
    {
        return (REG_RD_OFFSET(pDevice, Register));
    }
}

LM_VOID
LM_RegWr(PLM_DEVICE_BLOCK pDevice, LM_UINT32 Register, LM_UINT32 Value32)
{
#if PCIX_TARGET_WORKAROUND
    if (pDevice->EnablePciXFix)
    {
        LM_RegWrInd(pDevice, Register, Value32);
    }
    else
#endif
    {
        REG_WR_OFFSET(pDevice, Register, Value32);
    }
}

/******************************************************************************/
/* Description:                                                               */
/*                                                                            */
/* Return:                                                                    */
/******************************************************************************/
LM_UINT32
LM_RegRdInd(
PLM_DEVICE_BLOCK pDevice,
LM_UINT32 Register) {
    LM_UINT32 Value32;

#if PCIX_TARGET_WORKAROUND
    MM_ACQUIRE_UNDI_LOCK(pDevice);
#endif
    MM_WriteConfig32(pDevice, T3_PCI_REG_ADDR_REG, Register);
    MM_ReadConfig32(pDevice, T3_PCI_REG_DATA_REG, &Value32);
#if PCIX_TARGET_WORKAROUND
    MM_RELEASE_UNDI_LOCK(pDevice);
#endif

    return Value32;
} /* LM_RegRdInd */



/******************************************************************************/
/* Description:                                                               */
/*                                                                            */
/* Return:                                                                    */
/******************************************************************************/
LM_VOID
LM_RegWrInd(
PLM_DEVICE_BLOCK pDevice,
LM_UINT32 Register,
LM_UINT32 Value32) {

#if PCIX_TARGET_WORKAROUND
    MM_ACQUIRE_UNDI_LOCK(pDevice);
#endif
    MM_WriteConfig32(pDevice, T3_PCI_REG_ADDR_REG, Register);
    MM_WriteConfig32(pDevice, T3_PCI_REG_DATA_REG, Value32);
#if PCIX_TARGET_WORKAROUND
    MM_RELEASE_UNDI_LOCK(pDevice);
#endif
} /* LM_RegWrInd */



/******************************************************************************/
/* Description:                                                               */
/*                                                                            */
/* Return:                                                                    */
/******************************************************************************/
LM_UINT32
LM_MemRdInd(
PLM_DEVICE_BLOCK pDevice,
LM_UINT32 MemAddr) {
    LM_UINT32 Value32;

    MM_ACQUIRE_UNDI_LOCK(pDevice);
#ifdef BIG_ENDIAN_HOST
    MM_WriteConfig32(pDevice, T3_PCI_MEM_WIN_ADDR_REG, MemAddr);
    Value32 = REG_RD(pDevice, PciCfg.MemWindowData);
    /*    Value32 = REG_RD(pDevice,uIntMem.Mbuf[(MemAddr & 0x7fff)/4]); */
#else
    MM_WriteConfig32(pDevice, T3_PCI_MEM_WIN_ADDR_REG, MemAddr);
    MM_ReadConfig32(pDevice, T3_PCI_MEM_WIN_DATA_REG, &Value32);
#endif
    MM_RELEASE_UNDI_LOCK(pDevice);

    return Value32;
} /* LM_MemRdInd */



/******************************************************************************/
/* Description:                                                               */
/*                                                                            */
/* Return:                                                                    */
/******************************************************************************/
LM_VOID
LM_MemWrInd(
PLM_DEVICE_BLOCK pDevice,
LM_UINT32 MemAddr,
LM_UINT32 Value32) {
    MM_ACQUIRE_UNDI_LOCK(pDevice);
#ifdef BIG_ENDIAN_HOST
    REG_WR(pDevice,PciCfg.MemWindowBaseAddr,MemAddr);
    REG_WR(pDevice,uIntMem.Mbuf[(MemAddr & 0x7fff)/4],Value32);
#else
    MM_WriteConfig32(pDevice, T3_PCI_MEM_WIN_ADDR_REG, MemAddr);
    MM_WriteConfig32(pDevice, T3_PCI_MEM_WIN_DATA_REG, Value32);
#endif
    MM_RELEASE_UNDI_LOCK(pDevice);
} /* LM_MemWrInd */



/******************************************************************************/
/* Description:                                                               */
/*                                                                            */
/* Return:                                                                    */
/******************************************************************************/
LM_STATUS
LM_QueueRxPackets(
PLM_DEVICE_BLOCK pDevice) {
    LM_STATUS Lmstatus;
    PLM_PACKET pPacket;
    PT3_RCV_BD pRcvBd = 0;
    LM_UINT32 StdBdAdded = 0;
#if T3_JUMBO_RCV_RCB_ENTRY_COUNT
    LM_UINT32 JumboBdAdded = 0;
#endif /* T3_JUMBO_RCV_RCB_ENTRY_COUNT */

    Lmstatus = LM_STATUS_SUCCESS;

    pPacket = (PLM_PACKET) QQ_PopHead(&pDevice->RxPacketFreeQ.Container);
    while(pPacket) {
        switch(pPacket->u.Rx.RcvProdRing) {
#if T3_JUMBO_RCV_RCB_ENTRY_COUNT
            case T3_JUMBO_RCV_PROD_RING:        /* Jumbo Receive Ring. */
                /* Initialize the buffer descriptor. */
                pRcvBd = 
                    &pDevice->pRxJumboBdVirt[pDevice->RxJumboProdIdx];
                pRcvBd->Flags = RCV_BD_FLAG_END | RCV_BD_FLAG_JUMBO_RING;
                pRcvBd->Len = (LM_UINT16) pDevice->RxJumboBufferSize;

                /* Update the producer index. */
                pDevice->RxJumboProdIdx = (pDevice->RxJumboProdIdx + 1) & 
                    T3_JUMBO_RCV_RCB_ENTRY_COUNT_MASK;

                JumboBdAdded++;
                break;
#endif /* T3_JUMBO_RCV_RCB_ENTRY_COUNT */

            case T3_STD_RCV_PROD_RING:      /* Standard Receive Ring. */
                /* Initialize the buffer descriptor. */
                pRcvBd = &pDevice->pRxStdBdVirt[pDevice->RxStdProdIdx];
                pRcvBd->Flags = RCV_BD_FLAG_END;
                pRcvBd->Len = MAX_STD_RCV_BUFFER_SIZE;

                /* Update the producer index. */
                pDevice->RxStdProdIdx = (pDevice->RxStdProdIdx + 1) & 
                    T3_STD_RCV_RCB_ENTRY_COUNT_MASK;

                StdBdAdded++;
                break;

            case T3_UNKNOWN_RCV_PROD_RING:
            default:
                Lmstatus = LM_STATUS_FAILURE;
                break;
        } /* switch */

        /* Bail out if there is any error. */
        if(Lmstatus != LM_STATUS_SUCCESS)
        {
            break;
        }

        /* Initialize the receive buffer pointer */
        MM_MapRxDma(pDevice, pPacket, &pRcvBd->HostAddr);

        /* The opaque field may point to an offset from a fix addr. */
        pRcvBd->Opaque = (LM_UINT32) (MM_UINT_PTR(pPacket) - 
            MM_UINT_PTR(pDevice->pPacketDescBase));

        pPacket = (PLM_PACKET) QQ_PopHead(&pDevice->RxPacketFreeQ.Container);
    } /* while */

    wmb();
    /* Update the procedure index. */
    if(StdBdAdded)
    {
        MB_REG_WR(pDevice, Mailbox.RcvStdProdIdx.Low,
            pDevice->RxStdProdIdx);
        if (pDevice->FlushPostedWrites)
        {
            MB_REG_RD(pDevice, Mailbox.RcvStdProdIdx.Low);
        }
    }
#if T3_JUMBO_RCV_RCB_ENTRY_COUNT
    if(JumboBdAdded)
    {
        MB_REG_WR(pDevice, Mailbox.RcvJumboProdIdx.Low,
            pDevice->RxJumboProdIdx);
        if (pDevice->FlushPostedWrites)
        {
            MB_REG_RD(pDevice, Mailbox.RcvJumboProdIdx.Low);
        }
    }
#endif /* T3_JUMBO_RCV_RCB_ENTRY_COUNT */

    return Lmstatus;
} /* LM_QueueRxPackets */


/******************************************************************************/
/* Description:                                                               */
/*                                                                            */
/* Return:                                                                    */
/******************************************************************************/
STATIC LM_VOID
LM_NvramInit(
    PLM_DEVICE_BLOCK pDevice)
{
    LM_UINT32 Value32;

    /* Intialize clock period and state machine. */
    Value32 = SEEPROM_ADDR_CLK_PERD(SEEPROM_CLOCK_PERIOD) |
        SEEPROM_ADDR_FSM_RESET;
    REG_WR(pDevice, Grc.EepromAddr, Value32);

    MM_Wait(100);

    /* Serial eeprom access using the Grc.EepromAddr/EepromData registers. */
    Value32 = REG_RD(pDevice, Grc.LocalCtrl);
    REG_WR(pDevice, Grc.LocalCtrl, Value32 | GRC_MISC_LOCAL_CTRL_AUTO_SEEPROM);

    /* Set the 5701 compatibility mode if we are using EEPROM. */
    if(T3_ASIC_REV(pDevice->ChipRevId) != T3_ASIC_REV_5700 &&
        T3_ASIC_REV(pDevice->ChipRevId) != T3_ASIC_REV_5701)
    {
        Value32 = REG_RD(pDevice, Nvram.Config1);
        if((Value32 & FLASH_INTERFACE_ENABLE) == 0)
        {
            /* Use the new interface to read EEPROM. */
            Value32 &= ~FLASH_COMPAT_BYPASS;

            REG_WR(pDevice, Nvram.Config1, Value32);
        }
    }

    MM_Wait(100);

} /* LM_NvRamInit */


/******************************************************************************/
/* Description:                                                               */
/*                                                                            */
/* Return:                                                                    */
/******************************************************************************/
STATIC LM_STATUS
LM_EepromRead(
    PLM_DEVICE_BLOCK pDevice,
    LM_UINT32 Offset,
    LM_UINT32 *pData)
{
    LM_UINT32 Value32;
    LM_UINT32 Addr;
    LM_UINT32 Dev;
    LM_UINT32 j;

    if(Offset > SEEPROM_CHIP_SIZE)
    {
        return LM_STATUS_FAILURE;
    }

    Dev = Offset / SEEPROM_CHIP_SIZE;
    Addr = Offset % SEEPROM_CHIP_SIZE;
    
    Value32 = REG_RD(pDevice, Grc.EepromAddr);
    Value32 &= ~(SEEPROM_ADDR_ADDRESS_MASK | SEEPROM_ADDR_DEV_ID_MASK |
        SEEPROM_ADDR_RW_MASK);
    REG_WR(pDevice, Grc.EepromAddr, Value32 | SEEPROM_ADDR_DEV_ID(Dev) |
        SEEPROM_ADDR_ADDRESS(Addr) | SEEPROM_ADDR_START | SEEPROM_ADDR_READ);

    for(j = 0; j < 1000; j++)
    {
        Value32 = REG_RD(pDevice, Grc.EepromAddr);
        if(Value32 & SEEPROM_ADDR_COMPLETE)
        {
            break;
        }
        MM_Wait(20);
    }

    if(Value32 & SEEPROM_ADDR_COMPLETE)
    {
        Value32 = REG_RD(pDevice, Grc.EepromData);
        *pData = Value32;

        return LM_STATUS_SUCCESS;
    }

    return LM_STATUS_FAILURE;
} /* LM_EepromRead */

#ifdef ETHTOOL_SEEPROM

STATIC LM_STATUS
LM_EepromWriteBlock(
    PLM_DEVICE_BLOCK pDevice,
    LM_UINT32 Offset,
    LM_UINT32 *pData,
    LM_UINT32 Size)
{
    LM_UINT32 Value32 = 0;
    LM_UINT32 Addr;
    LM_UINT32 Dev;
    LM_UINT32 i, j;

    if(Offset > SEEPROM_CHIP_SIZE)
    {
        return LM_STATUS_FAILURE;
    }

    /* Enable EEPROM write. */
    if(pDevice->EepromWp)
    {
        REG_WR(pDevice, Grc.LocalCtrl, pDevice->GrcLocalCtrl |
                    GRC_MISC_LOCAL_CTRL_GPIO_OE1);
        MM_Wait(40);

        Value32 = REG_RD(pDevice, Grc.LocalCtrl);
        if(Value32 & GRC_MISC_LOCAL_CTRL_GPIO_OUTPUT1)
        {
            return LM_STATUS_FAILURE;
        }
    }

    for (i = 0; i < Size ; i++, pData++, Offset += 4) {

        Dev = Offset / SEEPROM_CHIP_SIZE;
        Addr = Offset % SEEPROM_CHIP_SIZE;

        /* Set the write value to the eeprom */    
        REG_WR(pDevice, Grc.EepromData, *pData);  

        Value32 = REG_RD(pDevice, Grc.EepromAddr);
        Value32 &= ~(SEEPROM_ADDR_ADDRESS_MASK | SEEPROM_ADDR_DEV_ID_MASK |
                            SEEPROM_ADDR_RW_MASK);

        REG_WR(pDevice, Grc.EepromAddr, Value32 | SEEPROM_ADDR_DEV_ID(Dev) |
                        SEEPROM_ADDR_ADDRESS(Addr) | SEEPROM_ADDR_START | SEEPROM_ADDR_WRITE);

        for(j = 0; j < 1000; j++)
        {
            Value32 = REG_RD(pDevice, Grc.EepromAddr);
            if(Value32 & SEEPROM_ADDR_COMPLETE)
            {
                break;
            }
            MM_Wait(10);
        }
    } 

    /* Write-protect EEPROM. */
    if(pDevice->EepromWp)
    {
        REG_WR(pDevice, Grc.LocalCtrl, pDevice->GrcLocalCtrl |
            GRC_MISC_LOCAL_CTRL_GPIO_OE1 | GRC_MISC_LOCAL_CTRL_GPIO_OUTPUT1);
        MM_Wait(40);
    }

    if(Value32 & SEEPROM_ADDR_COMPLETE)
    {
        return LM_STATUS_SUCCESS;
    }

    return LM_STATUS_FAILURE;
} /* LM_EepromWriteBlock */
#endif

/******************************************************************************/
/* Description:                                                               */
/*                                                                            */
/* Return:                                                                    */
/******************************************************************************/
LM_STATUS
LM_NvramRead(
    PLM_DEVICE_BLOCK pDevice,
    LM_UINT32 Offset,
    LM_UINT32 *pData)
{
    LM_UINT32 Value32;
    LM_STATUS Status;
    LM_UINT32 j;

    if(T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5700 ||
        T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5701)
    {
        Status = LM_EepromRead(pDevice, Offset, pData);
    }
    else
    {
        /* Determine if we have flash or EEPROM. */
        Value32 = REG_RD(pDevice, Nvram.Config1);
        if(Value32 & FLASH_INTERFACE_ENABLE)
        {
            if(Value32 & FLASH_SSRAM_BUFFERED_MODE)
            {
                Offset = ((Offset/BUFFERED_FLASH_PAGE_SIZE) <<
                    BUFFERED_FLASH_PAGE_POS) +
                    (Offset % BUFFERED_FLASH_PAGE_SIZE);
            }
        }

        REG_WR(pDevice, Nvram.SwArb, SW_ARB_REQ_SET1);
        for (j = 0; j < 1000; j++)
        {
            if (REG_RD(pDevice, Nvram.SwArb) & SW_ARB_GNT1)
            {
                break;
            }
            MM_Wait(20);
        }
        if (j == 1000)
        {
            return LM_STATUS_FAILURE;
        }

        /* Read from flash or EEPROM with the new 5703/02 interface. */
        REG_WR(pDevice, Nvram.Addr, Offset & NVRAM_ADDRESS_MASK);

        REG_WR(pDevice, Nvram.Cmd, NVRAM_CMD_RD | NVRAM_CMD_DO_IT |
            NVRAM_CMD_FIRST | NVRAM_CMD_LAST | NVRAM_CMD_DONE);

        /* Wait for the done bit to clear. */
        for(j = 0; j < 1000; j++)
        {
            MM_Wait(10);

            Value32 = REG_RD(pDevice, Nvram.Cmd);
            if(Value32 & NVRAM_CMD_DONE)
            {
                MM_Wait(10);

                *pData = REG_RD(pDevice, Nvram.ReadData);

                /* Change the endianess. */
                *pData = LM_ByteSwap(*pData);

                break;
            }
        }

        REG_WR(pDevice, Nvram.SwArb, SW_ARB_REQ_CLR1);
        if(Value32 & NVRAM_CMD_DONE)
        {
            Status = LM_STATUS_SUCCESS;
        }
        else
        {
            Status = LM_STATUS_FAILURE;
        }
    }

    return Status;
} /* LM_NvramRead */


#ifdef ETHTOOL_SEEPROM
/******************************************************************************/
/* Description:                                                               */
/*                                                                            */
/* Return:                                                                    */
/******************************************************************************/
LM_STATUS
LM_NvramWriteBlock(
    PLM_DEVICE_BLOCK pDevice,
    LM_UINT32 Offset,
    LM_UINT32 *pData,
    LM_UINT32 Size)
{
    LM_UINT32 Value32 = 0;
    LM_UINT32 ControlReg;
    LM_UINT32 AddrOffset;
    LM_STATUS Status;
    LM_UINT32  i , j;
    LM_UINT32 BufferedFlash;

    if(T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5700 ||
        T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5701)
    {
        Status = LM_EepromWriteBlock(pDevice, Offset, pData, Size);
    }
    else
    {
        /* Request access to the flash interface. */
        REG_WR(pDevice, Nvram.SwArb, SW_ARB_REQ_SET1);

        for(j = 0; j < 1000; j++)
        {
            Value32 = REG_RD(pDevice, Nvram.SwArb);
            if(Value32 & SW_ARB_GNT1)
            {
                break;
            }
            MM_Wait(20);
        }

        if(!(Value32 & SW_ARB_GNT1))
        {
            return LM_STATUS_FAILURE;
        }

        /* Enable EEPROM write. */
        if(pDevice->EepromWp)
        {
            REG_WR(pDevice, Grc.LocalCtrl, pDevice->GrcLocalCtrl |
                        GRC_MISC_LOCAL_CTRL_GPIO_OE1);
            MM_Wait(40);

            Value32 = REG_RD(pDevice, Grc.LocalCtrl);
            if(Value32 & GRC_MISC_LOCAL_CTRL_GPIO_OUTPUT1)
            {
                return LM_STATUS_FAILURE;
             }
        }

        BufferedFlash = FALSE;
        Value32 = REG_RD(pDevice, Nvram.Config1);
        if(Value32 & FLASH_INTERFACE_ENABLE)
        {
            if(Value32 & FLASH_SSRAM_BUFFERED_MODE)
            {
                BufferedFlash = TRUE;
            }
        }

        for (i = 0 ; i < Size ; i++, pData++, Offset += 4) {

            /* Change the endianess. */
            Value32 = LM_ByteSwap(*pData);

            /* set the desired write data value to the flash */
            REG_WR(pDevice, Nvram.WriteData, Value32);

            /* set targeted address */
            AddrOffset = Offset;

            /* Determine if we have flash or buffered flash. */
            if(BufferedFlash)
            {
                AddrOffset = ((Offset/BUFFERED_FLASH_PAGE_SIZE) <<
                                        BUFFERED_FLASH_PAGE_POS) +
                                    (Offset % BUFFERED_FLASH_PAGE_SIZE);
            }

            /* Write to flash or EEPROM with the new 5703/02 interface. */
            REG_WR(pDevice, Nvram.Addr, AddrOffset & NVRAM_ADDRESS_MASK);

            ControlReg = NVRAM_CMD_DO_IT | NVRAM_CMD_DONE | NVRAM_CMD_WR;
            if(i == 0)
            {
                ControlReg |= NVRAM_CMD_FIRST;
            }
            if(i == (Size - 1))  
            {
                ControlReg |= NVRAM_CMD_LAST;
            }

            if(BufferedFlash)
            {
                /* Set CMD_FIRST when we are at the beginning of a page. */
                if(!(AddrOffset & BUFFERED_FLASH_BYTE_ADDR_MASK))
                {
                    ControlReg |= NVRAM_CMD_FIRST;
                }
                else if((AddrOffset & BUFFERED_FLASH_BYTE_ADDR_MASK) ==
                    (BUFFERED_FLASH_PAGE_SIZE - 4))
                {
                    ControlReg |=  NVRAM_CMD_LAST;
                }
            }

            REG_WR(pDevice, Nvram.Cmd,  ControlReg);

            /* Wait for the done bit to go High. */
            for(j = 0; j < 4000 ; j++)
            {
                MM_Wait(10);

                Value32 = REG_RD(pDevice, Nvram.Cmd);

                if(Value32 & NVRAM_CMD_DONE)
                {
                    MM_Wait(5);
                    break;
                }
            }
        }  

        /* Write-protect EEPROM. */
        if(pDevice->EepromWp)
        {
            REG_WR(pDevice, Grc.LocalCtrl, pDevice->GrcLocalCtrl |
                GRC_MISC_LOCAL_CTRL_GPIO_OE1 | GRC_MISC_LOCAL_CTRL_GPIO_OUTPUT1);
            MM_Wait(40);
        }

        /* Relinquish nvram interface. */
        REG_WR(pDevice, Nvram.SwArb, SW_ARB_REQ_CLR1);

        if(Value32 & NVRAM_CMD_DONE)
        {
            Status = LM_STATUS_SUCCESS;
        }
        else
        {
            Status = LM_STATUS_FAILURE;
        }
    }

    return Status;
} /* LM_NvramWriteBlock */
#endif

STATIC void
LM_ReadVPD(PLM_DEVICE_BLOCK pDevice)
{
#ifdef BCM_PROC_FS
    LM_UINT32 Vpd_arr[256/4];
    LM_UINT8 *Vpd = (LM_UINT8 *) &Vpd_arr[0];
    LM_UINT32 *Vpd_dptr = &Vpd_arr[0];
    LM_UINT32 Value32;
    unsigned int j;

    /* Read PN from VPD */
    for (j = 0; j < 256; j += 4, Vpd_dptr++ )
    {
        if (LM_NvramRead(pDevice, 0x100 + j, &Value32) != LM_STATUS_SUCCESS) {
            printk(KERN_ERR "VPD read failed\n");
            return;
        }
        *Vpd_dptr = cpu_to_le32(Value32);
    }
    for (j = 0; j < 256; )
    {
        unsigned int Vpd_r_len;
        unsigned int Vpd_r_end;

        if ((Vpd[j] == 0x82) || (Vpd[j] == 0x91))
        {
            j = j + 3 + Vpd[j + 1] + (Vpd[j + 2] << 8);
        }
        else if (Vpd[j] == 0x90)
        {
            Vpd_r_len =  Vpd[j + 1] + (Vpd[j + 2] << 8);
            j += 3;
            Vpd_r_end = Vpd_r_len + j;
            while (j < Vpd_r_end)
            {
                if ((Vpd[j] == 'P') && (Vpd[j + 1] == 'N'))
                {
                    unsigned int len = Vpd[j + 2];

                    if (len <= 24)
                    {
                        memcpy(pDevice->PartNo, &Vpd[j + 3], len);
                    }
                    break;
                }
                else
                {
                    if (Vpd[j + 2] == 0)
                    {
                        break;
                    }
                    j = j + Vpd[j + 2];
                }
            }
            break;
        }
        else {
            break;
        }
    }
#endif
}

STATIC void
LM_ReadBootCodeVersion(PLM_DEVICE_BLOCK pDevice)
{
#ifdef BCM_PROC_FS
    LM_UINT32 Value32, offset, ver_offset, start_addr;
    int i;

    if (LM_NvramRead(pDevice, 0x0, &Value32) != LM_STATUS_SUCCESS)
        return;
    if (Value32 != 0xaa559966)
        return;
    if (LM_NvramRead(pDevice, 0xc, &offset) != LM_STATUS_SUCCESS)
        return;

    offset = LM_ByteSwap(offset);

    if((T3_ASIC_REV(pDevice->ChipRevId) != T3_ASIC_REV_5700) &&
        (T3_ASIC_REV(pDevice->ChipRevId) != T3_ASIC_REV_5701) &&
        ((REG_RD(pDevice, Nvram.Config1) & BUFFERED_FLASH) == BUFFERED_FLASH))
    {
        offset = (offset >> BUFFERED_FLASH_PAGE_POS) *
            BUFFERED_FLASH_PAGE_SIZE + (offset & BUFFERED_FLASH_BYTE_ADDR_MASK);
    }
    if (LM_NvramRead(pDevice, offset, &Value32) != LM_STATUS_SUCCESS)
        return;
    if ((((Value32 & ~0xf0000) == 0x0300000e) ||
        ((Value32 & ~0xff0000) == 0x0300000c)) &&
        (LM_NvramRead(pDevice, offset + 4, &Value32) == LM_STATUS_SUCCESS) &&
        (Value32 == 0)) {

        if (LM_NvramRead(pDevice, offset + 8, &ver_offset) != LM_STATUS_SUCCESS)
            return;
        if (LM_NvramRead(pDevice, 4, &start_addr) != LM_STATUS_SUCCESS)
            return;
        ver_offset = LM_ByteSwap(ver_offset);
        start_addr = LM_ByteSwap(start_addr);
        offset += ver_offset - start_addr;
        for (i = 0; i < 16; i += 4) {
            if (LM_NvramRead(pDevice, offset + i, &Value32) !=
                LM_STATUS_SUCCESS)
            {
                return;
            }
            *((LM_UINT32 *) &pDevice->BootCodeVer[i]) = cpu_to_le32(Value32);
        }
    }
    else {
        char c;

        if (LM_NvramRead(pDevice, 0x94, &Value32) != LM_STATUS_SUCCESS)
            return;

        i = 0;
        c = ((Value32 & 0xff0000) >> 16);

        if (c < 10) {
            pDevice->BootCodeVer[i++] = c + '0';
        }
        else {
            pDevice->BootCodeVer[i++] = (c / 10) + '0';
            pDevice->BootCodeVer[i++] = (c % 10) + '0';
        }
        pDevice->BootCodeVer[i++] = '.';
        c = (Value32 & 0xff000000) >> 24;
        if (c < 10) {
            pDevice->BootCodeVer[i++] = '0';
            pDevice->BootCodeVer[i++] = c + '0';
        }
        else {
            pDevice->BootCodeVer[i++] = (c / 10) + '0';
            pDevice->BootCodeVer[i++] = (c % 10) + '0';
        }
        pDevice->BootCodeVer[i] = 0;
    }
#endif
}

STATIC void
LM_GetBusSpeed(PLM_DEVICE_BLOCK pDevice)
{
#ifdef BCM_PROC_FS
    LM_UINT32 PciState = pDevice->PciState;
    LM_UINT32 ClockCtrl;
    char *SpeedStr = "";

    if (PciState & T3_PCI_STATE_32BIT_PCI_BUS)
    {
        strcpy(pDevice->BusSpeedStr, "32-bit ");
    }
    else
    {
        strcpy(pDevice->BusSpeedStr, "64-bit ");
    }
    if (PciState & T3_PCI_STATE_CONVENTIONAL_PCI_MODE)
    {
        strcat(pDevice->BusSpeedStr, "PCI ");
        if (PciState & T3_PCI_STATE_HIGH_BUS_SPEED) 
        {
            SpeedStr = "66MHz";
        }
        else
        {
            SpeedStr = "33MHz";
        }
    }
    else
    {
        strcat(pDevice->BusSpeedStr, "PCIX ");
        if (pDevice->BondId == GRC_MISC_BD_ID_5704CIOBE)
        {
            SpeedStr = "133MHz";
        }
        else
        {
            ClockCtrl = pDevice->ClockCtrl & 0x1f;
            switch (ClockCtrl)
            {
            case 0:
                SpeedStr = "33MHz";
                break;

            case 2:
                SpeedStr = "50MHz";
                break;

            case 4:
                SpeedStr = "66MHz";
                break;

            case 6:
                SpeedStr = "100MHz";
                break;

            case 7:
                SpeedStr = "133MHz";
                break;
            }
        }
    }
    strcat(pDevice->BusSpeedStr, SpeedStr);
#endif
}

/******************************************************************************/
/* Description:                                                               */
/*    This routine initializes default parameters and reads the PCI           */
/*    configurations.                                                         */
/*                                                                            */
/* Return:                                                                    */
/*    LM_STATUS_SUCCESS                                                       */
/******************************************************************************/
LM_STATUS
LM_GetAdapterInfo(
PLM_DEVICE_BLOCK pDevice)
{
    PLM_ADAPTER_INFO pAdapterInfo;
    LM_UINT32 Value32;
    LM_STATUS Status;
    LM_UINT32 EeSigFound;
    LM_UINT32 EePhyTypeSerdes = 0;
    LM_UINT32 EePhyLedMode = 0;
    LM_UINT32 EePhyId = 0;

    /* Get Device Id and Vendor Id */
    Status = MM_ReadConfig32(pDevice, PCI_VENDOR_ID_REG, &Value32);
    if(Status != LM_STATUS_SUCCESS)
    {
        return Status;
    }
    pDevice->PciVendorId = (LM_UINT16) Value32;
    pDevice->PciDeviceId = (LM_UINT16) (Value32 >> 16);

    Status = MM_ReadConfig32(pDevice, PCI_REV_ID_REG, &Value32);
    if(Status != LM_STATUS_SUCCESS)
    {
        return Status;
    }
    pDevice->PciRevId = (LM_UINT8) Value32;

    /* Get chip revision id. */
    Status = MM_ReadConfig32(pDevice, T3_PCI_MISC_HOST_CTRL_REG, &Value32);
    pDevice->ChipRevId = Value32 >> 16;

    /* Get subsystem vendor. */
    Status = MM_ReadConfig32(pDevice, PCI_SUBSYSTEM_VENDOR_ID_REG, &Value32);
    if(Status != LM_STATUS_SUCCESS)
    {
        return Status;
    }
    pDevice->SubsystemVendorId = (LM_UINT16) Value32;

    /* Get PCI subsystem id. */
    pDevice->SubsystemId = (LM_UINT16) (Value32 >> 16);

    /* Get the cache line size. */
    MM_ReadConfig32(pDevice, PCI_CACHE_LINE_SIZE_REG, &Value32);
    pDevice->CacheLineSize = (LM_UINT8) Value32;
    pDevice->SavedCacheLineReg = Value32;

    if(pDevice->ChipRevId != T3_CHIP_ID_5703_A1 &&
        pDevice->ChipRevId != T3_CHIP_ID_5703_A2 &&
        pDevice->ChipRevId != T3_CHIP_ID_5704_A0)
    {
        pDevice->UndiFix = FALSE;
    }
#if !PCIX_TARGET_WORKAROUND
    pDevice->UndiFix = FALSE;
#endif
    /* Map the memory base to system address space. */
    if (!pDevice->UndiFix)
    {
        Status = MM_MapMemBase(pDevice);
        if(Status != LM_STATUS_SUCCESS)
        {
            return Status;
        }
        /* Initialize the memory view pointer. */
        pDevice->pMemView = (PT3_STD_MEM_MAP) pDevice->pMappedMemBase;
    }

#if PCIX_TARGET_WORKAROUND
    /* store whether we are in PCI are PCI-X mode */
    pDevice->EnablePciXFix = FALSE;

    MM_ReadConfig32(pDevice, T3_PCI_STATE_REG, &Value32);
    if((Value32 & T3_PCI_STATE_CONVENTIONAL_PCI_MODE) == 0)
    {
        /* Enable PCI-X workaround only if we are running on 5700 BX. */
        if(T3_CHIP_REV(pDevice->ChipRevId) == T3_CHIP_REV_5700_BX)
        {
            pDevice->EnablePciXFix = TRUE;
        }
    }
    if (pDevice->UndiFix)
    {
        pDevice->EnablePciXFix = TRUE;
    }
#endif
    /* Bx bug: due to the "byte_enable bug" in PCI-X mode, the power */
    /* management register may be clobbered which may cause the */
    /* BCM5700 to go into D3 state.  While in this state, we will */
    /* not have memory mapped register access.  As a workaround, we */
    /* need to restore the device to D0 state. */
    MM_ReadConfig32(pDevice, T3_PCI_PM_STATUS_CTRL_REG, &Value32);
    Value32 |= T3_PM_PME_ASSERTED;
    Value32 &= ~T3_PM_POWER_STATE_MASK;
    Value32 |= T3_PM_POWER_STATE_D0;
    MM_WriteConfig32(pDevice, T3_PCI_PM_STATUS_CTRL_REG, Value32);

    /* read the current PCI command word */
    MM_ReadConfig32(pDevice, PCI_COMMAND_REG, &Value32);

    /* Make sure bus-mastering is enabled. */
    Value32 |= PCI_BUSMASTER_ENABLE;

#if PCIX_TARGET_WORKAROUND
    /* if we are in PCI-X mode, also make sure mem-mapping and SERR#/PERR#
        are enabled */
    if (pDevice->EnablePciXFix == TRUE) {
        Value32 |= (PCI_MEM_SPACE_ENABLE | PCI_SYSTEM_ERROR_ENABLE | 
                    PCI_PARITY_ERROR_ENABLE);
    }
    if (pDevice->UndiFix)
    {
        Value32 &= ~PCI_MEM_SPACE_ENABLE;
    }

#endif

    if(pDevice->EnableMWI)
    {
        Value32 |= PCI_MEMORY_WRITE_INVALIDATE;
    }
    else {
        Value32 &= (~PCI_MEMORY_WRITE_INVALIDATE);
    }

    /* save the value we are going to write into the PCI command word */	
    pDevice->PciCommandStatusWords = Value32;	

    Status = MM_WriteConfig32(pDevice, PCI_COMMAND_REG, Value32);
    if(Status != LM_STATUS_SUCCESS)
    {
        return Status;
    }

    /* Set power state to D0. */
    LM_SetPowerState(pDevice, LM_POWER_STATE_D0);

    /* Setup the mode registers. */
    pDevice->MiscHostCtrl = 
        MISC_HOST_CTRL_MASK_PCI_INT | 
        MISC_HOST_CTRL_ENABLE_ENDIAN_WORD_SWAP | 
#ifdef BIG_ENDIAN_HOST
        MISC_HOST_CTRL_ENABLE_ENDIAN_BYTE_SWAP |  
#endif /* BIG_ENDIAN_HOST */
        MISC_HOST_CTRL_ENABLE_INDIRECT_ACCESS |
        MISC_HOST_CTRL_ENABLE_PCI_STATE_REG_RW;
	/* write to PCI misc host ctr first in order to enable indirect accesses */
    MM_WriteConfig32(pDevice, T3_PCI_MISC_HOST_CTRL_REG, pDevice->MiscHostCtrl);

    REG_WR(pDevice, PciCfg.MiscHostCtrl, pDevice->MiscHostCtrl);

#ifdef BIG_ENDIAN_HOST
    Value32 = GRC_MODE_BYTE_SWAP_NON_FRAME_DATA | 
              GRC_MODE_WORD_SWAP_NON_FRAME_DATA;
#else
    Value32 = GRC_MODE_BYTE_SWAP_NON_FRAME_DATA | GRC_MODE_BYTE_SWAP_DATA;
#endif
    REG_WR(pDevice, Grc.Mode, Value32);

    if(T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5700)
    {
        REG_WR(pDevice, Grc.LocalCtrl, GRC_MISC_LOCAL_CTRL_GPIO_OUTPUT1 |
            GRC_MISC_LOCAL_CTRL_GPIO_OE1);
    }
    MM_Wait(40);

    /* Enable indirect memory access */
    REG_WR(pDevice, MemArbiter.Mode, T3_MEM_ARBITER_MODE_ENABLE);

    LM_SwitchClocks(pDevice);

    REG_WR(pDevice, PciCfg.MemWindowBaseAddr, 0);

#if PCIX_TARGET_WORKAROUND
    MM_ReadConfig32(pDevice, T3_PCI_STATE_REG, &Value32);
    if ((pDevice->EnablePciXFix == FALSE) &&
        ((Value32 & T3_PCI_STATE_CONVENTIONAL_PCI_MODE) == 0))
    {
        if (pDevice->ChipRevId == T3_CHIP_ID_5701_A0 ||
            pDevice->ChipRevId == T3_CHIP_ID_5701_B0 ||
            pDevice->ChipRevId == T3_CHIP_ID_5701_B2 ||
            pDevice->ChipRevId == T3_CHIP_ID_5701_B5)
        {
            __raw_writel(0, &(pDevice->pMemView->uIntMem.MemBlock32K[0x300]));
            __raw_writel(0, &(pDevice->pMemView->uIntMem.MemBlock32K[0x301]));
            __raw_writel(0xffffffff, &(pDevice->pMemView->uIntMem.MemBlock32K[0x301]));
            if (__raw_readl(&(pDevice->pMemView->uIntMem.MemBlock32K[0x300])))
            {
                pDevice->EnablePciXFix = TRUE;
            }
        }
    }
#endif

    LM_NvramInit(pDevice);

    /* Get the node address.  First try to get in from the shared memory. */
    /* If the signature is not present, then get it from the NVRAM. */
    Value32 = MEM_RD_OFFSET(pDevice, T3_MAC_ADDR_HIGH_MAILBOX);
    if((Value32 >> 16) == 0x484b)
    {

        pDevice->NodeAddress[0] = (LM_UINT8) (Value32 >> 8);
        pDevice->NodeAddress[1] = (LM_UINT8) Value32;

        Value32 = MEM_RD_OFFSET(pDevice, T3_MAC_ADDR_LOW_MAILBOX);

        pDevice->NodeAddress[2] = (LM_UINT8) (Value32 >> 24);
        pDevice->NodeAddress[3] = (LM_UINT8) (Value32 >> 16);
        pDevice->NodeAddress[4] = (LM_UINT8) (Value32 >> 8);
        pDevice->NodeAddress[5] = (LM_UINT8) Value32;

        Status = LM_STATUS_SUCCESS;
    }
    else
    {
        int MacOffset;

        MacOffset = 0x7c;
        if (T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5704)
        {
            if (REG_RD(pDevice, PciCfg.DualMacCtrl) & T3_DUAL_MAC_ID)
            {
                MacOffset = 0xcc;
            }
        }
        Status = LM_NvramRead(pDevice, MacOffset, &Value32);
        if(Status == LM_STATUS_SUCCESS)
        {
            pDevice->NodeAddress[0] = (LM_UINT8) (Value32 >> 16);
            pDevice->NodeAddress[1] = (LM_UINT8) (Value32 >> 24);

            Status = LM_NvramRead(pDevice, MacOffset + 4, &Value32);

            pDevice->NodeAddress[2] = (LM_UINT8) Value32;
            pDevice->NodeAddress[3] = (LM_UINT8) (Value32 >> 8);
            pDevice->NodeAddress[4] = (LM_UINT8) (Value32 >> 16);
            pDevice->NodeAddress[5] = (LM_UINT8) (Value32 >> 24);
        }
    }

    if(Status != LM_STATUS_SUCCESS)
    {
        printk(KERN_ERR "Cannot get MAC addr from NVRAM\n");
        return LM_STATUS_FAILURE;
    }

    memcpy(pDevice->PermanentNodeAddress, pDevice->NodeAddress, 6);

    /* Initialize the default values. */
    pDevice->NoTxPseudoHdrChksum = FALSE;
    pDevice->NoRxPseudoHdrChksum = FALSE;
    pDevice->NicSendBd = FALSE;
    pDevice->TxPacketDescCnt = DEFAULT_TX_PACKET_DESC_COUNT;
    pDevice->RxStdDescCnt = DEFAULT_STD_RCV_DESC_COUNT;
    pDevice->RxCoalescingTicks = DEFAULT_RX_COALESCING_TICKS;
    pDevice->TxCoalescingTicks = DEFAULT_TX_COALESCING_TICKS;
    pDevice->RxMaxCoalescedFrames = DEFAULT_RX_MAX_COALESCED_FRAMES;
    pDevice->TxMaxCoalescedFrames = DEFAULT_TX_MAX_COALESCED_FRAMES;
    pDevice->RxCoalescingTicksDuringInt = BAD_DEFAULT_VALUE;
    pDevice->TxCoalescingTicksDuringInt = BAD_DEFAULT_VALUE;
    pDevice->RxMaxCoalescedFramesDuringInt = BAD_DEFAULT_VALUE;
    pDevice->TxMaxCoalescedFramesDuringInt = BAD_DEFAULT_VALUE;
    pDevice->StatsCoalescingTicks = DEFAULT_STATS_COALESCING_TICKS;
    pDevice->EnableMWI = FALSE;
    pDevice->TxMtu = MAX_ETHERNET_PACKET_SIZE_NO_CRC;
    pDevice->RxMtu = MAX_ETHERNET_PACKET_SIZE_NO_CRC;
    pDevice->DisableAutoNeg = FALSE;
    pDevice->PhyIntMode = T3_PHY_INT_MODE_AUTO;
    pDevice->LinkChngMode = T3_LINK_CHNG_MODE_AUTO;
    pDevice->LedMode = LED_MODE_AUTO;
    pDevice->ResetPhyOnInit = TRUE;
    pDevice->ResetPhyOnLinkDown = FALSE;
    pDevice->DelayPciGrant = TRUE;
    pDevice->UseTaggedStatus = FALSE;
    pDevice->OneDmaAtOnce = FALSE;

    pDevice->RequestedLineSpeed = LM_LINE_SPEED_AUTO;
    pDevice->FlowControlCap = LM_FLOW_CONTROL_AUTO_PAUSE;
    pDevice->EnableTbi = FALSE;
#if INCLUDE_TBI_SUPPORT
    pDevice->PollTbiLink = BAD_DEFAULT_VALUE;
#endif

    if ((T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5703) ||
        (T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5704))
    {
        pDevice->ResetPhyOnLinkDown = TRUE;
    }

    switch (T3_ASIC_REV(pDevice->ChipRevId))
    {
    case T3_ASIC_REV_5704:
        pDevice->MbufBase = T3_NIC_MBUF_POOL_ADDR;
        pDevice->MbufSize = T3_NIC_MBUF_POOL_SIZE64;
        break;
    default:
        pDevice->MbufBase = T3_NIC_MBUF_POOL_ADDR;
        pDevice->MbufSize = T3_NIC_MBUF_POOL_SIZE96;
        break;
    }

    pDevice->LinkStatus = LM_STATUS_LINK_DOWN;
    pDevice->QueueRxPackets = TRUE;

    pDevice->EnableWireSpeed = TRUE;

#if T3_JUMBO_RCV_RCB_ENTRY_COUNT
    pDevice->RxJumboDescCnt = DEFAULT_JUMBO_RCV_DESC_COUNT;
#endif /* T3_JUMBO_RCV_RCB_ENTRY_COUNT */

    pDevice->BondId = REG_RD(pDevice, Grc.MiscCfg) & GRC_MISC_BD_ID_MASK;

    if(((T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5701) &&
        ((pDevice->BondId == 0x10000) || (pDevice->BondId == 0x18000))) ||
        ((T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5703) &&
        ((pDevice->BondId == 0x14000) || (pDevice->BondId == 0x1c000))))
    {
        return LM_STATUS_UNKNOWN_ADAPTER;
    }
    if(T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5703)
    {
        if ((pDevice->BondId == 0x8000) || (pDevice->BondId == 0x4000))
        {
            pDevice->No1000 = TRUE;
        }
    }

    pDevice->SplitModeEnable = SPLIT_MODE_DISABLE;
    if ((T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5704) &&
        (pDevice->BondId == GRC_MISC_BD_ID_5704CIOBE))
    {
        pDevice->SplitModeEnable = SPLIT_MODE_ENABLE;
        pDevice->SplitModeMaxReq = SPLIT_MODE_5704_MAX_REQ;
    }

    /* Get Eeprom info. */
    Value32 = MEM_RD_OFFSET(pDevice, T3_NIC_DATA_SIG_ADDR);
    if (Value32 == T3_NIC_DATA_SIG)
    {
        EeSigFound = TRUE;
        Value32 = MEM_RD_OFFSET(pDevice, T3_NIC_DATA_NIC_CFG_ADDR);

        /* Determine PHY type. */
        switch (Value32 & T3_NIC_CFG_PHY_TYPE_MASK)
        {
            case T3_NIC_CFG_PHY_TYPE_COPPER:
                EePhyTypeSerdes = FALSE;
                break;

            case T3_NIC_CFG_PHY_TYPE_FIBER:
                EePhyTypeSerdes = TRUE;
                break;

            default:
                EePhyTypeSerdes = FALSE;
                break;
        }

        /* Determine PHY led mode. */
        if(T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5700 ||
            T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5701)
        {
            switch(Value32 & T3_NIC_CFG_LED_MODE_MASK)
            {
                case T3_NIC_CFG_LED_MODE_TRIPLE_SPEED:
                    EePhyLedMode = LED_MODE_THREE_LINK;
                    break;

                case T3_NIC_CFG_LED_MODE_LINK_SPEED:
                    EePhyLedMode = LED_MODE_LINK10;
                    break;

                default:
                    EePhyLedMode = LED_MODE_AUTO;
                    break;
            }
        }
        else
        {
            switch(Value32 & T3_NIC_CFG_LED_MODE_MASK)
            {
                case T3_NIC_CFG_LED_MODE_OPEN_DRAIN:
                    EePhyLedMode = LED_MODE_OPEN_DRAIN;
                    break;

                case T3_NIC_CFG_LED_MODE_OUTPUT:
                    EePhyLedMode = LED_MODE_OUTPUT;
                    break;

                default:
                    EePhyLedMode = LED_MODE_AUTO;
                    break;
            }
        }
        if(pDevice->ChipRevId == T3_CHIP_ID_5703_A1 ||
            pDevice->ChipRevId == T3_CHIP_ID_5703_A2)
        {
            /* Enable EEPROM write protection. */
            if(Value32 & T3_NIC_EEPROM_WP)
            {
                pDevice->EepromWp = TRUE;
            }
        }
        pDevice->EnableAsf = FALSE;
#ifdef BCM_ASF
        if (Value32 & T3_NIC_CFG_ENABLE_ASF)
        {
            pDevice->EnableAsf = TRUE;
        }
#endif
        if (Value32 & T3_NIC_FIBER_WOL_CAPABLE)
        {
            pDevice->FiberWolCapable = 1;
        }

        /* Get the PHY Id. */
        Value32 = MEM_RD_OFFSET(pDevice, T3_NIC_DATA_PHY_ID_ADDR);
        if (Value32)
        {
            EePhyId = (((Value32 & T3_NIC_PHY_ID1_MASK) >> 16) &
                PHY_ID1_OUI_MASK) << 10;

            Value32 = Value32 & T3_NIC_PHY_ID2_MASK;

            EePhyId |= ((Value32 & PHY_ID2_OUI_MASK) << 16) |
              (Value32 & PHY_ID2_MODEL_MASK) | (Value32 & PHY_ID2_REV_MASK);
        }
        else
        {
            EePhyId = 0;
        }
    }
    else
    {
        EeSigFound = FALSE;
    }

    /* Set the PHY address. */
    pDevice->PhyAddr = PHY_DEVICE_ID;

    /* Disable auto polling. */
    pDevice->MiMode = 0xc0000;
    REG_WR(pDevice, MacCtrl.MiMode, pDevice->MiMode);
    MM_Wait(40);

    /* Get the PHY id. */
    LM_ReadPhy(pDevice, PHY_ID1_REG, &Value32);
    pDevice->PhyId = (Value32 & PHY_ID1_OUI_MASK) << 10;

    LM_ReadPhy(pDevice, PHY_ID2_REG, &Value32);
    pDevice->PhyId |= ((Value32 & PHY_ID2_OUI_MASK) << 16) |
      (Value32 & PHY_ID2_MODEL_MASK) | (Value32 & PHY_ID2_REV_MASK);

    /* Set the EnableTbi flag to false if we have a copper PHY. */
    switch(pDevice->PhyId & PHY_ID_MASK)
    {
        case PHY_BCM5400_PHY_ID:
        case PHY_BCM5401_PHY_ID:
        case PHY_BCM5411_PHY_ID:
        case PHY_BCM5701_PHY_ID:
        case PHY_BCM5703_PHY_ID:
        case PHY_BCM5704_PHY_ID:
            pDevice->EnableTbi = FALSE;
            break;

        case PHY_BCM8002_PHY_ID:
            pDevice->EnableTbi = TRUE;
            break;

        default:

            if (EeSigFound)
            {
                pDevice->PhyId = EePhyId;
                pDevice->EnableTbi = EePhyTypeSerdes;
            }
            else if ((pAdapterInfo = LM_GetAdapterInfoBySsid(
                pDevice->SubsystemVendorId,
                pDevice->SubsystemId)))
            {
                pDevice->PhyId = pAdapterInfo->PhyId;
                pDevice->EnableTbi = pAdapterInfo->Serdes;
            }
            break;
    }

    /* Bail out if we don't know the copper PHY id. */
    if(UNKNOWN_PHY_ID(pDevice->PhyId) && !pDevice->EnableTbi)
    {
        return LM_STATUS_FAILURE;
    }

    if(T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5703)
    {
        if((pDevice->SavedCacheLineReg & 0xff00) < 0x4000)
        {
            pDevice->SavedCacheLineReg &= 0xffff00ff;
            pDevice->SavedCacheLineReg |= 0x4000;
        }
    }

    pDevice->ReceiveMask = LM_ACCEPT_MULTICAST | LM_ACCEPT_BROADCAST |
        LM_ACCEPT_UNICAST | LM_KEEP_VLAN_TAG;
    /* Change driver parameters. */
    Status = MM_GetConfig(pDevice);
    if(Status != LM_STATUS_SUCCESS)
    {
        return Status;
    }

    /* Save the current phy link status. */
    if(!pDevice->EnableTbi)
    {
        LM_ReadPhy(pDevice, PHY_STATUS_REG, &Value32);
        LM_ReadPhy(pDevice, PHY_STATUS_REG, &Value32);

        /* If we don't have link reset the PHY. */
        if(!(Value32 & PHY_STATUS_LINK_PASS) || pDevice->ResetPhyOnInit)
        {

            LM_ResetPhy(pDevice);

            if (LM_PhyAdvertiseAll(pDevice) != LM_STATUS_SUCCESS)
            {
                Value32 = PHY_AN_AD_PROTOCOL_802_3_CSMA_CD |
                    PHY_AN_AD_ALL_SPEEDS;
                Value32 |= GetPhyAdFlowCntrlSettings(pDevice);
                LM_WritePhy(pDevice, PHY_AN_AD_REG, Value32);
                pDevice->advertising = Value32;

                Value32 = BCM540X_AN_AD_ALL_1G_SPEEDS ;
#if INCLUDE_5701_AX_FIX
                if(pDevice->ChipRevId == T3_CHIP_ID_5701_A0 ||
                    pDevice->ChipRevId == T3_CHIP_ID_5701_B0)
                {
                    Value32 |= BCM540X_CONFIG_AS_MASTER |
                        BCM540X_ENABLE_CONFIG_AS_MASTER;
                }
#endif
                LM_WritePhy(pDevice, BCM540X_1000BASET_CTRL_REG, Value32);
                pDevice->advertising1000 = Value32;

                LM_WritePhy(pDevice, PHY_CTRL_REG, PHY_CTRL_AUTO_NEG_ENABLE |
                    PHY_CTRL_RESTART_AUTO_NEG);
            }

            if(T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5703)
            {
                LM_WritePhy(pDevice, 0x18, 0x0c00);
                LM_WritePhy(pDevice, 0x17, 0x201f);
                LM_WritePhy(pDevice, 0x15, 0x2aaa);
            }
            if(pDevice->ChipRevId == T3_CHIP_ID_5704_A0)
            {
                LM_WritePhy(pDevice, 0x1c, 0x8d68);
                LM_WritePhy(pDevice, 0x1c, 0x8d68);
            }
            /* Enable Ethernet@WireSpeed. */
            if(pDevice->EnableWireSpeed)
            {
                LM_WritePhy(pDevice, 0x18, 0x7007);
                LM_ReadPhy(pDevice, 0x18, &Value32);
                LM_WritePhy(pDevice, 0x18, Value32 | BIT_15 | BIT_4);
            }
        }
        LM_ReadPhy(pDevice, PHY_AN_AD_REG, &pDevice->advertising);
        LM_ReadPhy(pDevice, BCM540X_1000BASET_CTRL_REG,
            &pDevice->advertising1000);
    }

    /* Turn off tap power management. */
    if((pDevice->PhyId & PHY_ID_MASK) == PHY_BCM5401_PHY_ID)
    {
        LM_WritePhy(pDevice, BCM5401_AUX_CTRL, 0x0c20);
        LM_WritePhy(pDevice, BCM540X_DSP_ADDRESS_REG, 0x0012);
        LM_WritePhy(pDevice, BCM540X_DSP_RW_PORT, 0x1804);
        LM_WritePhy(pDevice, BCM540X_DSP_ADDRESS_REG, 0x0013);
        LM_WritePhy(pDevice, BCM540X_DSP_RW_PORT, 0x1204);
        LM_WritePhy(pDevice, BCM540X_DSP_ADDRESS_REG, 0x8006);
        LM_WritePhy(pDevice, BCM540X_DSP_RW_PORT, 0x0132);
        LM_WritePhy(pDevice, BCM540X_DSP_ADDRESS_REG, 0x8006);
        LM_WritePhy(pDevice, BCM540X_DSP_RW_PORT, 0x0232);
        LM_WritePhy(pDevice, BCM540X_DSP_ADDRESS_REG, 0x201f);
        LM_WritePhy(pDevice, BCM540X_DSP_RW_PORT, 0x0a20);

        MM_Wait(40);
    }

#if INCLUDE_TBI_SUPPORT
    pDevice->IgnoreTbiLinkChange = FALSE;

    if(pDevice->EnableTbi)
    {
        if (!pDevice->FiberWolCapable)
        {
            pDevice->WakeUpModeCap = LM_WAKE_UP_MODE_NONE;
        }
        pDevice->PhyIntMode = T3_PHY_INT_MODE_LINK_READY;
        if ((pDevice->PollTbiLink == BAD_DEFAULT_VALUE) ||
            pDevice->DisableAutoNeg)
        {
            pDevice->PollTbiLink = FALSE;
        }
    }
    else
    {
        pDevice->PollTbiLink = FALSE;
    }
#endif /* INCLUDE_TBI_SUPPORT */

    /* UseTaggedStatus is only valid for 5701 and later. */
    if (T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5700)
    {
        pDevice->UseTaggedStatus = FALSE;

        pDevice->CoalesceMode = 0;
    }
    else
    {
        pDevice->CoalesceMode = HOST_COALESCE_CLEAR_TICKS_ON_RX_BD_EVENT |
            HOST_COALESCE_CLEAR_TICKS_ON_TX_BD_EVENT;
    }

    /* Set the status block size. */
    if(T3_CHIP_REV(pDevice->ChipRevId) != T3_CHIP_REV_5700_AX &&
        T3_CHIP_REV(pDevice->ChipRevId) != T3_CHIP_REV_5700_BX)
    {
        pDevice->CoalesceMode |= HOST_COALESCE_32_BYTE_STATUS_MODE;
    }

    /* Check the DURING_INT coalescing ticks parameters. */
    if(pDevice->UseTaggedStatus)
    {
        if(pDevice->RxCoalescingTicksDuringInt == BAD_DEFAULT_VALUE)
        {
            pDevice->RxCoalescingTicksDuringInt =
                DEFAULT_RX_COALESCING_TICKS_DURING_INT;
        }

        if(pDevice->TxCoalescingTicksDuringInt == BAD_DEFAULT_VALUE)
        {
            pDevice->TxCoalescingTicksDuringInt =
                DEFAULT_TX_COALESCING_TICKS_DURING_INT;
        }

        if(pDevice->RxMaxCoalescedFramesDuringInt == BAD_DEFAULT_VALUE)
        {
            pDevice->RxMaxCoalescedFramesDuringInt =
                DEFAULT_RX_MAX_COALESCED_FRAMES_DURING_INT;
        }

        if(pDevice->TxMaxCoalescedFramesDuringInt == BAD_DEFAULT_VALUE)
        {
            pDevice->TxMaxCoalescedFramesDuringInt =
                DEFAULT_TX_MAX_COALESCED_FRAMES_DURING_INT;
        }
    }
    else
    {
        if(pDevice->RxCoalescingTicksDuringInt == BAD_DEFAULT_VALUE)
        {
            pDevice->RxCoalescingTicksDuringInt = 0;
        }

        if(pDevice->TxCoalescingTicksDuringInt == BAD_DEFAULT_VALUE)
        {
            pDevice->TxCoalescingTicksDuringInt = 0;
        }

        if(pDevice->RxMaxCoalescedFramesDuringInt == BAD_DEFAULT_VALUE)
        {
            pDevice->RxMaxCoalescedFramesDuringInt = 0;
        }

        if(pDevice->TxMaxCoalescedFramesDuringInt == BAD_DEFAULT_VALUE)
        {
            pDevice->TxMaxCoalescedFramesDuringInt = 0;
        }
    }

#if T3_JUMBO_RCV_RCB_ENTRY_COUNT
    if(pDevice->RxMtu <= (MAX_STD_RCV_BUFFER_SIZE - 8 /* CRC */))
    {
        pDevice->RxJumboDescCnt = 0;
        if(pDevice->RxMtu <= MAX_ETHERNET_PACKET_SIZE_NO_CRC)
        {
            pDevice->RxMtu = MAX_ETHERNET_PACKET_SIZE_NO_CRC;
        }
    }
    else
    {
        pDevice->RxJumboBufferSize = (pDevice->RxMtu + 8 /* CRC + VLAN */ +
            COMMON_CACHE_LINE_SIZE-1) & ~COMMON_CACHE_LINE_MASK;

        if(pDevice->RxJumboBufferSize > MAX_JUMBO_RCV_BUFFER_SIZE)
        {
            pDevice->RxJumboBufferSize = DEFAULT_JUMBO_RCV_BUFFER_SIZE;
            pDevice->RxMtu = pDevice->RxJumboBufferSize - 8 /* CRC + VLAN */;
        }
        pDevice->TxMtu = pDevice->RxMtu;

    }
#else
    pDevice->RxMtu = MAX_ETHERNET_PACKET_SIZE_NO_CRC;
#endif /* T3_JUMBO_RCV_RCB_ENTRY_COUNT */

    pDevice->RxPacketDescCnt = 
#if T3_JUMBO_RCV_RCB_ENTRY_COUNT
        pDevice->RxJumboDescCnt +
#endif /* T3_JUMBO_RCV_RCB_ENTRY_COUNT */
        pDevice->RxStdDescCnt;

    if(pDevice->TxMtu < MAX_ETHERNET_PACKET_SIZE_NO_CRC)
    {
        pDevice->TxMtu = MAX_ETHERNET_PACKET_SIZE_NO_CRC;
    }

    if(pDevice->TxMtu > MAX_JUMBO_TX_BUFFER_SIZE)
    {
        pDevice->TxMtu = MAX_JUMBO_TX_BUFFER_SIZE;
    }

    /* Configure the proper ways to get link change interrupt. */
    if(pDevice->PhyIntMode == T3_PHY_INT_MODE_AUTO)
    {
        if(T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5700)
        {
            pDevice->PhyIntMode = T3_PHY_INT_MODE_MI_INTERRUPT;
        }
        else
        {
            pDevice->PhyIntMode = T3_PHY_INT_MODE_LINK_READY;
        }
    }
    else if(pDevice->PhyIntMode == T3_PHY_INT_MODE_AUTO_POLLING)
    {
        /* Auto-polling does not work on 5700_AX and 5700_BX. */
        if(T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5700)
        {
            pDevice->PhyIntMode = T3_PHY_INT_MODE_MI_INTERRUPT;
        }
    }

    /* Determine the method to get link change status. */
    if(pDevice->LinkChngMode == T3_LINK_CHNG_MODE_AUTO)
    {
        /* The link status bit in the status block does not work on 5700_AX */
        /* and 5700_BX chips. */
        if(T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5700)
        {
            pDevice->LinkChngMode = T3_LINK_CHNG_MODE_USE_STATUS_REG;
        }
        else
        {
            pDevice->LinkChngMode = T3_LINK_CHNG_MODE_USE_STATUS_BLOCK;
        }
    }

    if(pDevice->PhyIntMode == T3_PHY_INT_MODE_MI_INTERRUPT ||
        T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5700)
    {
        pDevice->LinkChngMode = T3_LINK_CHNG_MODE_USE_STATUS_REG;
    }

    /* Configure PHY led mode. */
    if(pDevice->LedMode == LED_MODE_AUTO)
    {
        if(T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5700 ||
            T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5701)
        {
            if(pDevice->SubsystemVendorId == T3_SVID_DELL)
            {
                pDevice->LedMode = LED_MODE_LINK10;
            }
            else
            {
                pDevice->LedMode = LED_MODE_THREE_LINK;

                if(EeSigFound && EePhyLedMode != LED_MODE_AUTO)
                {
                    pDevice->LedMode = EePhyLedMode;
                }
            }

            /* bug? 5701 in LINK10 mode does not seem to work when */
            /* PhyIntMode is LINK_READY. */
            if(T3_ASIC_REV(pDevice->ChipRevId) != T3_ASIC_REV_5700 &&
#if INCLUDE_TBI_SUPPORT
                pDevice->EnableTbi == FALSE &&
#endif
                pDevice->LedMode == LED_MODE_LINK10)
            {
                pDevice->PhyIntMode = T3_PHY_INT_MODE_MI_INTERRUPT;
                pDevice->LinkChngMode = T3_LINK_CHNG_MODE_USE_STATUS_REG;
            }

            if(pDevice->EnableTbi)
            {
                pDevice->LedMode = LED_MODE_THREE_LINK;
            }
        }
        else
        {
            if(EeSigFound && EePhyLedMode != LED_MODE_AUTO)
            {
                pDevice->LedMode = EePhyLedMode;
            }
            else
            {
                pDevice->LedMode = LED_MODE_OPEN_DRAIN;
            }
        }
    }

#ifdef BCM_WOL
    if (T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5700 ||
        pDevice->ChipRevId == T3_CHIP_ID_5701_A0 ||
        pDevice->ChipRevId == T3_CHIP_ID_5701_B0 ||
        pDevice->ChipRevId == T3_CHIP_ID_5701_B2)
    {
        pDevice->WolSpeed = WOL_SPEED_10MB;
    }
    else
    {
        pDevice->WolSpeed = WOL_SPEED_100MB;
    }
#endif

    pDevice->PciState = REG_RD(pDevice, PciCfg.PciState);
    LM_ReadVPD(pDevice);
    LM_ReadBootCodeVersion(pDevice);
    LM_GetBusSpeed(pDevice);

    return LM_STATUS_SUCCESS;
} /* LM_GetAdapterInfo */

STATIC PLM_ADAPTER_INFO
LM_GetAdapterInfoBySsid(
    LM_UINT16 Svid,
    LM_UINT16 Ssid)
{
    static LM_ADAPTER_INFO AdapterArr[] =
    {
        { T3_SVID_BROADCOM, T3_SSID_BROADCOM_BCM95700A6, PHY_BCM5401_PHY_ID, 0},
        { T3_SVID_BROADCOM, T3_SSID_BROADCOM_BCM95701A5, PHY_BCM5701_PHY_ID, 0},
        { T3_SVID_BROADCOM, T3_SSID_BROADCOM_BCM95700T6, PHY_BCM8002_PHY_ID, 1},
        { T3_SVID_BROADCOM, T3_SSID_BROADCOM_BCM95700A9, 0, 1 },
        { T3_SVID_BROADCOM, T3_SSID_BROADCOM_BCM95701T1, PHY_BCM5701_PHY_ID, 0},
        { T3_SVID_BROADCOM, T3_SSID_BROADCOM_BCM95701T8, PHY_BCM5701_PHY_ID, 0},
        { T3_SVID_BROADCOM, T3_SSID_BROADCOM_BCM95701A7, 0, 1},
        { T3_SVID_BROADCOM, T3_SSID_BROADCOM_BCM95701A10, PHY_BCM5701_PHY_ID, 0},
        { T3_SVID_BROADCOM, T3_SSID_BROADCOM_BCM95701A12, PHY_BCM5701_PHY_ID, 0},
        { T3_SVID_BROADCOM, T3_SSID_BROADCOM_BCM95703Ax1, PHY_BCM5703_PHY_ID, 0},
        { T3_SVID_BROADCOM, T3_SSID_BROADCOM_BCM95703Ax2, PHY_BCM5703_PHY_ID, 0},

        { T3_SVID_3COM, T3_SSID_3COM_3C996T, PHY_BCM5401_PHY_ID, 0 },
        { T3_SVID_3COM, T3_SSID_3COM_3C996BT, PHY_BCM5701_PHY_ID, 0 },
        { T3_SVID_3COM, T3_SSID_3COM_3C996SX, 0, 1 },
        { T3_SVID_3COM, T3_SSID_3COM_3C1000T, PHY_BCM5701_PHY_ID, 0 },
        { T3_SVID_3COM, T3_SSID_3COM_3C940BR01, PHY_BCM5701_PHY_ID, 0 },

        { T3_SVID_DELL, T3_SSID_DELL_VIPER, PHY_BCM5401_PHY_ID, 0 },
        { T3_SVID_DELL, T3_SSID_DELL_JAGUAR, PHY_BCM5401_PHY_ID, 0 },
        { T3_SVID_DELL, T3_SSID_DELL_MERLOT, PHY_BCM5411_PHY_ID, 0 },
        { T3_SVID_DELL, T3_SSID_DELL_SLIM_MERLOT, PHY_BCM5411_PHY_ID, 0 },

        { T3_SVID_COMPAQ, T3_SSID_COMPAQ_BANSHEE, PHY_BCM5701_PHY_ID, 0 },
        { T3_SVID_COMPAQ, T3_SSID_COMPAQ_BANSHEE_2, PHY_BCM5701_PHY_ID, 0 },
        { T3_SVID_COMPAQ, T3_SSID_COMPAQ_CHANGELING, 0, 1 },
        { T3_SVID_COMPAQ, T3_SSID_COMPAQ_NC7780, PHY_BCM5701_PHY_ID, 0 },
        { T3_SVID_COMPAQ, T3_SSID_COMPAQ_NC7780_2, PHY_BCM5701_PHY_ID, 0 },

    };
    LM_UINT32 j;

    for(j = 0; j < sizeof(AdapterArr)/sizeof(LM_ADAPTER_INFO); j++)
    {
        if(AdapterArr[j].Svid == Svid && AdapterArr[j].Ssid == Ssid)
        {
            return &AdapterArr[j];
        }
    }

    return NULL;
}


/******************************************************************************/
/* Description:                                                               */
/*    This routine sets up receive/transmit buffer descriptions queues.       */
/*                                                                            */
/* Return:                                                                    */
/*    LM_STATUS_SUCCESS                                                       */
/******************************************************************************/
LM_STATUS
LM_InitializeAdapter(
PLM_DEVICE_BLOCK pDevice)
{
    LM_PHYSICAL_ADDRESS MemPhy;
    PLM_UINT8 pMemVirt;
    PLM_PACKET pPacket;
    LM_STATUS Status;
    LM_UINT32 Size;
    LM_UINT32 j, Value32;

    /* Set power state to D0. */
    LM_SetPowerState(pDevice, LM_POWER_STATE_D0);

    /* Intialize the queues. */
    QQ_InitQueue(&pDevice->RxPacketReceivedQ.Container, 
        MAX_RX_PACKET_DESC_COUNT);
    QQ_InitQueue(&pDevice->RxPacketFreeQ.Container,
        MAX_RX_PACKET_DESC_COUNT);

    QQ_InitQueue(&pDevice->TxPacketFreeQ.Container,MAX_TX_PACKET_DESC_COUNT);
    QQ_InitQueue(&pDevice->TxPacketActiveQ.Container,MAX_TX_PACKET_DESC_COUNT);
    QQ_InitQueue(&pDevice->TxPacketXmittedQ.Container,MAX_TX_PACKET_DESC_COUNT);

    pDevice->RcvRetRcbEntryCount = T3_RCV_RETURN_RCB_ENTRY_COUNT;
    pDevice->RcvRetRcbEntryCountMask = T3_RCV_RETURN_RCB_ENTRY_COUNT_MASK;

    /* Allocate shared memory for: status block, the buffers for receive */
    /* rings -- standard, mini, jumbo, and return rings. */
    Size = T3_STATUS_BLOCK_SIZE + sizeof(T3_STATS_BLOCK) +
        T3_STD_RCV_RCB_ENTRY_COUNT * sizeof(T3_RCV_BD) +
#if T3_JUMBO_RCV_RCB_ENTRY_COUNT
        T3_JUMBO_RCV_RCB_ENTRY_COUNT * sizeof(T3_RCV_BD) +
#endif /* T3_JUMBO_RCV_RCB_ENTRY_COUNT */
        (pDevice->RcvRetRcbEntryCount * sizeof(T3_RCV_BD));

    /* Memory for host based Send BD. */
    if(pDevice->NicSendBd == FALSE)
    {
        Size += sizeof(T3_SND_BD) * T3_SEND_RCB_ENTRY_COUNT;
    }

    /* Allocate the memory block. */
    Status = MM_AllocateSharedMemory(pDevice, Size, (PLM_VOID) &pMemVirt, &MemPhy, FALSE);
    if(Status != LM_STATUS_SUCCESS)
    {
        return Status;
    }

    /* Program DMA Read/Write */
    if (pDevice->PciState & T3_PCI_STATE_NOT_PCI_X_BUS)
    {
        pDevice->DmaReadWriteCtrl = 0x763f000f;    
    }
    else
    {
        if((T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5703) ||
            (T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5704))
        {
            pDevice->DmaReadWriteCtrl = 0x761f0000; 
        }
        else
        {
            pDevice->DmaReadWriteCtrl = 0x761b000f;    
        }
        if(pDevice->ChipRevId == T3_CHIP_ID_5703_A1 ||
            pDevice->ChipRevId == T3_CHIP_ID_5703_A2 ||
            pDevice->ChipRevId == T3_CHIP_ID_5703_A3 ||
            pDevice->ChipRevId == T3_CHIP_ID_5704_A0 ||
            pDevice->ChipRevId == T3_CHIP_ID_5704_A1)
        {
            Value32 = REG_RD(pDevice, PciCfg.ClockCtrl) & 0x1f;
            if ((Value32 == 0x6) || (Value32 == 0x7))
            {
                pDevice->OneDmaAtOnce = TRUE;
            }
        }
    }
    if(T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5703)
    {
        pDevice->DmaReadWriteCtrl &= 0xfffffff0;
    }

    if(pDevice->OneDmaAtOnce)
    {
        pDevice->DmaReadWriteCtrl |= DMA_CTRL_WRITE_ONE_DMA_AT_ONCE;
    }
    REG_WR(pDevice, PciCfg.DmaReadWriteCtrl, pDevice->DmaReadWriteCtrl);

    LM_SwitchClocks(pDevice);

    if (LM_DmaTest(pDevice, pMemVirt, MemPhy, 0x400) != LM_STATUS_SUCCESS)
    {
        return LM_STATUS_FAILURE;
    }

    /* Status block. */
    pDevice->pStatusBlkVirt = (PT3_STATUS_BLOCK) pMemVirt;
    pDevice->StatusBlkPhy = MemPhy;
    pMemVirt += T3_STATUS_BLOCK_SIZE;
    LM_INC_PHYSICAL_ADDRESS(&MemPhy, T3_STATUS_BLOCK_SIZE);

    /* Statistics block. */
    pDevice->pStatsBlkVirt = (PT3_STATS_BLOCK) pMemVirt;
    pDevice->StatsBlkPhy = MemPhy;
    pMemVirt += sizeof(T3_STATS_BLOCK);
    LM_INC_PHYSICAL_ADDRESS(&MemPhy, sizeof(T3_STATS_BLOCK));

    /* Receive standard BD buffer. */
    pDevice->pRxStdBdVirt = (PT3_RCV_BD) pMemVirt;
    pDevice->RxStdBdPhy = MemPhy;

    pMemVirt += T3_STD_RCV_RCB_ENTRY_COUNT * sizeof(T3_RCV_BD);
    LM_INC_PHYSICAL_ADDRESS(&MemPhy, 
        T3_STD_RCV_RCB_ENTRY_COUNT * sizeof(T3_RCV_BD));

#if T3_JUMBO_RCV_RCB_ENTRY_COUNT
    /* Receive jumbo BD buffer. */
    pDevice->pRxJumboBdVirt = (PT3_RCV_BD) pMemVirt;
    pDevice->RxJumboBdPhy = MemPhy;

    pMemVirt += T3_JUMBO_RCV_RCB_ENTRY_COUNT * sizeof(T3_RCV_BD);
    LM_INC_PHYSICAL_ADDRESS(&MemPhy, 
        T3_JUMBO_RCV_RCB_ENTRY_COUNT * sizeof(T3_RCV_BD));
#endif /* T3_JUMBO_RCV_RCB_ENTRY_COUNT */

    /* Receive return BD buffer. */
    pDevice->pRcvRetBdVirt = (PT3_RCV_BD) pMemVirt;
    pDevice->RcvRetBdPhy = MemPhy;

    pMemVirt += pDevice->RcvRetRcbEntryCount * sizeof(T3_RCV_BD);
    LM_INC_PHYSICAL_ADDRESS(&MemPhy, 
        pDevice->RcvRetRcbEntryCount * sizeof(T3_RCV_BD));

    /* Set up Send BD. */
    if(pDevice->NicSendBd == FALSE)
    {
        pDevice->pSendBdVirt = (PT3_SND_BD) pMemVirt;
        pDevice->SendBdPhy = MemPhy;

        pMemVirt += sizeof(T3_SND_BD) * T3_SEND_RCB_ENTRY_COUNT;
        LM_INC_PHYSICAL_ADDRESS(&MemPhy, 
            sizeof(T3_SND_BD) * T3_SEND_RCB_ENTRY_COUNT);
    }
#ifdef BCM_NIC_SEND_BD
    else
    {
        pDevice->pSendBdVirt = (PT3_SND_BD)
            pDevice->pMemView->uIntMem.First32k.BufferDesc;
        pDevice->SendBdPhy.High = 0;
        pDevice->SendBdPhy.Low = T3_NIC_SND_BUFFER_DESC_ADDR;
    }
#endif

    /* Allocate memory for packet descriptors. */
    Size = (pDevice->RxPacketDescCnt + 
        pDevice->TxPacketDescCnt) * MM_PACKET_DESC_SIZE;
    Status = MM_AllocateMemory(pDevice, Size, (PLM_VOID *) &pPacket);
    if(Status != LM_STATUS_SUCCESS)
    {
        return Status;
    }
    pDevice->pPacketDescBase = (PLM_VOID) pPacket;

    /* Create transmit packet descriptors from the memory block and add them */
    /* to the TxPacketFreeQ for each send ring. */
    for(j = 0; j < pDevice->TxPacketDescCnt; j++)
    {
        /* Ring index. */
        pPacket->Flags = 0;

        /* Queue the descriptor in the TxPacketFreeQ of the 'k' ring. */
        QQ_PushTail(&pDevice->TxPacketFreeQ.Container, pPacket);

        /* Get the pointer to the next descriptor.  MM_PACKET_DESC_SIZE */
        /* is the total size of the packet descriptor including the */
        /* os-specific extensions in the UM_PACKET structure. */
        pPacket = (PLM_PACKET) ((PLM_UINT8) pPacket + MM_PACKET_DESC_SIZE);
    } /* for(j.. */

    /* Create receive packet descriptors from the memory block and add them */
    /* to the RxPacketFreeQ.  Create the Standard packet descriptors. */
    for(j = 0; j < pDevice->RxStdDescCnt; j++)
    {
        /* Receive producer ring. */
        pPacket->u.Rx.RcvProdRing = T3_STD_RCV_PROD_RING;

        /* Receive buffer size. */
        pPacket->u.Rx.RxBufferSize = MAX_STD_RCV_BUFFER_SIZE;

        /* Add the descriptor to RxPacketFreeQ. */
        QQ_PushTail(&pDevice->RxPacketFreeQ.Container, pPacket);

        /* Get the pointer to the next descriptor.  MM_PACKET_DESC_SIZE */
        /* is the total size of the packet descriptor including the */
        /* os-specific extensions in the UM_PACKET structure. */
        pPacket = (PLM_PACKET) ((PLM_UINT8) pPacket + MM_PACKET_DESC_SIZE);
    } /* for */

#if T3_JUMBO_RCV_RCB_ENTRY_COUNT
    /* Create the Jumbo packet descriptors. */
    for(j = 0; j < pDevice->RxJumboDescCnt; j++)
    {
        /* Receive producer ring. */
        pPacket->u.Rx.RcvProdRing = T3_JUMBO_RCV_PROD_RING;

        /* Receive buffer size. */
        pPacket->u.Rx.RxBufferSize = pDevice->RxJumboBufferSize;

        /* Add the descriptor to RxPacketFreeQ. */
        QQ_PushTail(&pDevice->RxPacketFreeQ.Container, pPacket);

        /* Get the pointer to the next descriptor.  MM_PACKET_DESC_SIZE */
        /* is the total size of the packet descriptor including the */
        /* os-specific extensions in the UM_PACKET structure. */
        pPacket = (PLM_PACKET) ((PLM_UINT8) pPacket + MM_PACKET_DESC_SIZE);
    } /* for */
#endif /* T3_JUMBO_RCV_RCB_ENTRY_COUNT */

    /* Initialize the rest of the packet descriptors. */
    Status = MM_InitializeUmPackets(pDevice);
    if(Status != LM_STATUS_SUCCESS)
    {
        return Status;
    } /* if */

    /* Default receive mask. */
    pDevice->ReceiveMask &= LM_KEEP_VLAN_TAG;
    pDevice->ReceiveMask |= LM_ACCEPT_MULTICAST | LM_ACCEPT_BROADCAST |
        LM_ACCEPT_UNICAST;

    /* Make sure we are in the first 32k memory window or NicSendBd. */
    REG_WR(pDevice, PciCfg.MemWindowBaseAddr, 0);

    /* Initialize the hardware. */
    Status = LM_ResetAdapter(pDevice);
    if(Status != LM_STATUS_SUCCESS)
    {
        return Status;
    }

    /* We are done with initialization. */
    pDevice->InitDone = TRUE;

    return LM_STATUS_SUCCESS;
} /* LM_InitializeAdapter */


LM_STATUS
LM_DisableChip(PLM_DEVICE_BLOCK pDevice)
{
    LM_UINT32 data;

    pDevice->RxMode &= ~RX_MODE_ENABLE;
    REG_WR(pDevice, MacCtrl.RxMode, pDevice->RxMode);
    if(!(REG_RD(pDevice, MacCtrl.RxMode) & RX_MODE_ENABLE))
    {
        MM_Wait(20);
    }
    data = REG_RD(pDevice, RcvBdIn.Mode);
    data &= ~RCV_BD_IN_MODE_ENABLE;
    REG_WR(pDevice, RcvBdIn.Mode,data);
    if(!(REG_RD(pDevice, RcvBdIn.Mode) & RCV_BD_IN_MODE_ENABLE))
    {
        MM_Wait(20);
    }
    data = REG_RD(pDevice, RcvListPlmt.Mode);
    data &= ~RCV_LIST_PLMT_MODE_ENABLE;
    REG_WR(pDevice, RcvListPlmt.Mode,data);
    if(!(REG_RD(pDevice, RcvListPlmt.Mode) & RCV_LIST_PLMT_MODE_ENABLE))
    {
        MM_Wait(20);
    }
    data = REG_RD(pDevice, RcvListSel.Mode);
    data &= ~RCV_LIST_SEL_MODE_ENABLE;
    REG_WR(pDevice, RcvListSel.Mode,data);
    if(!(REG_RD(pDevice, RcvListSel.Mode) & RCV_LIST_SEL_MODE_ENABLE))
    {
        MM_Wait(20);
    }
    data = REG_RD(pDevice, RcvDataBdIn.Mode);
    data &= ~RCV_DATA_BD_IN_MODE_ENABLE;
    REG_WR(pDevice, RcvDataBdIn.Mode,data);
    if(!(REG_RD(pDevice, RcvDataBdIn.Mode) & RCV_DATA_BD_IN_MODE_ENABLE))
    {
        MM_Wait(20);
    }
    data = REG_RD(pDevice, RcvDataComp.Mode);
    data &= ~RCV_DATA_COMP_MODE_ENABLE;
    REG_WR(pDevice, RcvDataComp.Mode,data);
    if(!(REG_RD(pDevice, RcvDataBdIn.Mode) & RCV_DATA_COMP_MODE_ENABLE))
    {
        MM_Wait(20);
    }
    data = REG_RD(pDevice, RcvBdComp.Mode);
    data &= ~RCV_BD_COMP_MODE_ENABLE;
    REG_WR(pDevice, RcvBdComp.Mode,data);
    if(!(REG_RD(pDevice, RcvBdComp.Mode) & RCV_BD_COMP_MODE_ENABLE))
    {
        MM_Wait(20);
    }     
    data = REG_RD(pDevice, SndBdSel.Mode);
    data &= ~SND_BD_SEL_MODE_ENABLE;
    REG_WR(pDevice, SndBdSel.Mode, data);
    if(!(REG_RD(pDevice, SndBdSel.Mode) & SND_BD_SEL_MODE_ENABLE))
    {
        MM_Wait(20);
    }
    data = REG_RD(pDevice, SndBdIn.Mode);
    data &= ~SND_BD_IN_MODE_ENABLE;
    REG_WR(pDevice, SndBdIn.Mode, data);
    if(!(REG_RD(pDevice, SndBdIn.Mode) & SND_BD_IN_MODE_ENABLE))
    {
        MM_Wait(20);
    }
    data = REG_RD(pDevice, SndDataIn.Mode);
    data &= ~T3_SND_DATA_IN_MODE_ENABLE;
    REG_WR(pDevice, SndDataIn.Mode,data);
    if(!(REG_RD(pDevice, SndDataIn.Mode) & T3_SND_DATA_IN_MODE_ENABLE))
    {
        MM_Wait(20);
    }
    data = REG_RD(pDevice, DmaRead.Mode);
    data &= ~DMA_READ_MODE_ENABLE;
    REG_WR(pDevice, DmaRead.Mode, data);
    if(!(REG_RD(pDevice, DmaRead.Mode) & DMA_READ_MODE_ENABLE))
    {
        MM_Wait(20);
    }
    data = REG_RD(pDevice, SndDataComp.Mode);
    data &= ~SND_DATA_COMP_MODE_ENABLE;
    REG_WR(pDevice, SndDataComp.Mode, data);
    if(!(REG_RD(pDevice, SndDataComp.Mode) & SND_DATA_COMP_MODE_ENABLE))
    {
        MM_Wait(20);
    }
    data = REG_RD(pDevice,DmaComp.Mode);
    data &= ~DMA_COMP_MODE_ENABLE;
    REG_WR(pDevice, DmaComp.Mode, data);
    if(!(REG_RD(pDevice, DmaComp.Mode) & DMA_COMP_MODE_ENABLE))
    {
        MM_Wait(20);
    }
    data = REG_RD(pDevice, SndBdComp.Mode);
    data &= ~SND_BD_COMP_MODE_ENABLE;
    REG_WR(pDevice, SndBdComp.Mode, data);
    if(!(REG_RD(pDevice, SndBdComp.Mode) & SND_BD_COMP_MODE_ENABLE))
    {
        MM_Wait(20);
    }
    /* Clear TDE bit */
    pDevice->MacMode &= ~MAC_MODE_ENABLE_TDE;
    REG_WR(pDevice, MacCtrl.Mode, pDevice->MacMode);
    pDevice->TxMode &= ~TX_MODE_ENABLE;
    REG_WR(pDevice, MacCtrl.TxMode, pDevice->TxMode);
    if(!(REG_RD(pDevice, MacCtrl.TxMode) & TX_MODE_ENABLE))
    {
        MM_Wait(20);
    }
    data = REG_RD(pDevice, HostCoalesce.Mode);
    data &= ~HOST_COALESCE_ENABLE;
    REG_WR(pDevice, HostCoalesce.Mode, data);
    if(!(REG_RD(pDevice, SndBdIn.Mode) & HOST_COALESCE_ENABLE))
    {
        MM_Wait(20);
    }
    data = REG_RD(pDevice, DmaWrite.Mode);
    data &= ~DMA_WRITE_MODE_ENABLE;
    REG_WR(pDevice, DmaWrite.Mode,data);
    if(!(REG_RD(pDevice, DmaWrite.Mode) & DMA_WRITE_MODE_ENABLE))
    {
        MM_Wait(20);
    }
    data = REG_RD(pDevice, MbufClusterFree.Mode);
    data &= ~MBUF_CLUSTER_FREE_MODE_ENABLE;
    REG_WR(pDevice, MbufClusterFree.Mode,data);
    if(!(REG_RD(pDevice, MbufClusterFree.Mode) & MBUF_CLUSTER_FREE_MODE_ENABLE))
    {
        MM_Wait(20);
    }     
    /* Reset all FTQs */
    REG_WR(pDevice, Ftq.Reset, 0xffffffff);
    REG_WR(pDevice, Ftq.Reset, 0x0);

    data = REG_RD(pDevice, BufMgr.Mode);
    data &= ~BUFMGR_MODE_ENABLE;
    REG_WR(pDevice, BufMgr.Mode,data);
    if(!(REG_RD(pDevice, BufMgr.Mode) & BUFMGR_MODE_ENABLE))
    {
        MM_Wait(20);
    }
    data = REG_RD(pDevice, MemArbiter.Mode);
    data &= ~T3_MEM_ARBITER_MODE_ENABLE;
    REG_WR(pDevice, MemArbiter.Mode, data);
    if(!(REG_RD(pDevice, MemArbiter.Mode) & T3_MEM_ARBITER_MODE_ENABLE)) 
    {
        MM_Wait(20);
    }       
    return LM_STATUS_SUCCESS;
}

LM_STATUS
LM_DisableFW(PLM_DEVICE_BLOCK pDevice)
{
#ifdef BCM_ASF
    int j;
    LM_UINT32 Value32;

    if (pDevice->EnableAsf)
    {
        MEM_WR_OFFSET(pDevice, T3_CMD_MAILBOX, T3_CMD_NICDRV_PAUSE_FW);
        Value32 = REG_RD(pDevice, Grc.RxCpuEvent);
        REG_WR(pDevice, Grc.RxCpuEvent, Value32 | BIT_14);
        for (j = 0; j < 100; j++)
        {
            Value32 = REG_RD(pDevice, Grc.RxCpuEvent);
            if (!(Value32 & BIT_14))
            {
                break;
            }
            MM_Wait(1);
        }
    }
#endif
    return LM_STATUS_SUCCESS;
}

/******************************************************************************/
/* Description:                                                               */
/*    This function reinitializes the adapter.                                */
/*                                                                            */
/* Return:                                                                    */
/*    LM_STATUS_SUCCESS                                                       */
/******************************************************************************/
LM_STATUS
LM_ResetAdapter(
PLM_DEVICE_BLOCK pDevice)
{
    LM_UINT32 Value32;
    LM_UINT32 j, k;

    /* Disable interrupt. */
    LM_DisableInterrupt(pDevice);

    LM_DisableFW(pDevice);

    /* May get a spurious interrupt */
    pDevice->pStatusBlkVirt->Status = STATUS_BLOCK_UPDATED;

    /* Disable transmit and receive DMA engines.  Abort all pending requests. */
    if(pDevice->InitDone)
    {
        LM_Abort(pDevice);
    }

    pDevice->ShuttingDown = FALSE;

    LM_ResetChip(pDevice);

#ifdef BCM_ASF
    if (pDevice->EnableAsf)
    {
        MEM_WR_OFFSET(pDevice, T3_DRV_STATE_MAILBOX, T3_DRV_STATE_START);
    }
    else
#endif
    {
        MEM_WR_OFFSET(pDevice, T3_DRV_STATE_MAILBOX, T3_DRV_STATE_SUSPEND);
    }

    /* Bug: Athlon fix for B3 silicon only.  This bit does not do anything */
    /* in other chip revisions. */
    if(pDevice->DelayPciGrant)
    {
        REG_WR(pDevice, PciCfg.ClockCtrl, pDevice->ClockCtrl | BIT_31);
    }

    if(pDevice->ChipRevId == T3_CHIP_ID_5704_A0)
    {
        if (!(pDevice->PciState & T3_PCI_STATE_CONVENTIONAL_PCI_MODE))
        {
            Value32 = REG_RD(pDevice, PciCfg.PciState);
            Value32 |= T3_PCI_STATE_RETRY_SAME_DMA;
            REG_WR(pDevice, PciCfg.PciState, Value32);
        }
    }

    /* Enable TaggedStatus mode. */
    if(pDevice->UseTaggedStatus)
    {
        pDevice->MiscHostCtrl |= MISC_HOST_CTRL_ENABLE_TAGGED_STATUS_MODE;
    }

    /* Restore PCI configuration registers. */
    MM_WriteConfig32(pDevice, PCI_CACHE_LINE_SIZE_REG,
        pDevice->SavedCacheLineReg);
//    LM_RegWrInd(pDevice, PCI_SUBSYSTEM_VENDOR_ID_REG, 
//        (pDevice->SubsystemId << 16) | pDevice->SubsystemVendorId);
    MM_WriteConfig32(pDevice, PCI_SUBSYSTEM_VENDOR_ID_REG, 
        (pDevice->SubsystemId << 16) | pDevice->SubsystemVendorId);

    /* Clear the statistics block. */
    for(j = 0x0300; j < 0x0b00; j++)
    {
        MEM_WR_OFFSET(pDevice, j, 0);
    }

    /* Initialize the statistis Block */
    pDevice->pStatusBlkVirt->Status = 0;
    pDevice->pStatusBlkVirt->RcvStdConIdx = 0;
    pDevice->pStatusBlkVirt->RcvJumboConIdx = 0;
    pDevice->pStatusBlkVirt->RcvMiniConIdx = 0;

    for(j = 0; j < 16; j++)
    {
       pDevice->pStatusBlkVirt->Idx[j].RcvProdIdx = 0;
       pDevice->pStatusBlkVirt->Idx[j].SendConIdx = 0;
    }

    for(k = 0; k < T3_STD_RCV_RCB_ENTRY_COUNT ;k++)
    {
       pDevice->pRxStdBdVirt[k].HostAddr.High = 0;
       pDevice->pRxStdBdVirt[k].HostAddr.Low = 0;
    }

#if T3_JUMBO_RCV_RCB_ENTRY_COUNT
    /* Receive jumbo BD buffer. */
    for(k = 0; k < T3_JUMBO_RCV_RCB_ENTRY_COUNT; k++)
    {
        pDevice->pRxJumboBdVirt[k].HostAddr.High = 0;
        pDevice->pRxJumboBdVirt[k].HostAddr.Low = 0;
    }
#endif

    REG_WR(pDevice, PciCfg.DmaReadWriteCtrl, pDevice->DmaReadWriteCtrl);    

    /* GRC mode control register. */
    Value32 = 
#ifdef BIG_ENDIAN_HOST
        GRC_MODE_BYTE_SWAP_NON_FRAME_DATA | 
        GRC_MODE_WORD_SWAP_NON_FRAME_DATA |
        GRC_MODE_BYTE_SWAP_DATA |
        GRC_MODE_WORD_SWAP_DATA |
#else
        GRC_MODE_WORD_SWAP_NON_FRAME_DATA |
        GRC_MODE_BYTE_SWAP_DATA |
        GRC_MODE_WORD_SWAP_DATA |
#endif
        GRC_MODE_INT_ON_MAC_ATTN |
        GRC_MODE_HOST_STACK_UP;

    /* Configure send BD mode. */
    if(pDevice->NicSendBd == FALSE)
    {
        Value32 |= GRC_MODE_HOST_SEND_BDS;
    }
#ifdef BCM_NIC_SEND_BD
    else
    {
        Value32 |= GRC_MODE_4X_NIC_BASED_SEND_RINGS;
    }
#endif

    /* Configure pseudo checksum mode. */
    if(pDevice->NoTxPseudoHdrChksum)
    {
        Value32 |= GRC_MODE_TX_NO_PSEUDO_HEADER_CHKSUM;
    }

    if(pDevice->NoRxPseudoHdrChksum)
    {
        Value32 |= GRC_MODE_RX_NO_PSEUDO_HEADER_CHKSUM;
    }

    REG_WR(pDevice, Grc.Mode, Value32);

    /* Setup the timer prescalar register. */
    REG_WR(pDevice, Grc.MiscCfg, 65 << 1);      /* Clock is alwasy 66Mhz. */

    /* Set up the MBUF pool base address and size. */
    REG_WR(pDevice, BufMgr.MbufPoolAddr, pDevice->MbufBase);
    REG_WR(pDevice, BufMgr.MbufPoolSize, pDevice->MbufSize);

    /* Set up the DMA descriptor pool base address and size. */
    REG_WR(pDevice, BufMgr.DmaDescPoolAddr, T3_NIC_DMA_DESC_POOL_ADDR);
    REG_WR(pDevice, BufMgr.DmaDescPoolSize, T3_NIC_DMA_DESC_POOL_SIZE);
    
    /* Configure MBUF and Threshold watermarks */
    /* Configure the DMA read MBUF low water mark. */
    if(pDevice->TxMtu < MAX_ETHERNET_PACKET_BUFFER_SIZE)
    {
        REG_WR(pDevice, BufMgr.MbufReadDmaLowWaterMark,
            T3_DEF_DMA_MBUF_LOW_WMARK);
        REG_WR(pDevice, BufMgr.MbufMacRxLowWaterMark,
            T3_DEF_RX_MAC_MBUF_LOW_WMARK);
        REG_WR(pDevice, BufMgr.MbufHighWaterMark,
            T3_DEF_MBUF_HIGH_WMARK);
    }
    else
    {
        REG_WR(pDevice, BufMgr.MbufReadDmaLowWaterMark,
            T3_DEF_DMA_MBUF_LOW_WMARK_JUMBO);
        REG_WR(pDevice, BufMgr.MbufMacRxLowWaterMark,
            T3_DEF_RX_MAC_MBUF_LOW_WMARK_JUMBO);
        REG_WR(pDevice, BufMgr.MbufHighWaterMark,
            T3_DEF_MBUF_HIGH_WMARK_JUMBO);
    }

    REG_WR(pDevice, BufMgr.DmaLowWaterMark, T3_DEF_DMA_DESC_LOW_WMARK);
    REG_WR(pDevice, BufMgr.DmaHighWaterMark, T3_DEF_DMA_DESC_HIGH_WMARK);

    /* Enable buffer manager. */
    REG_WR(pDevice, BufMgr.Mode, BUFMGR_MODE_ENABLE | BUFMGR_MODE_ATTN_ENABLE);

    for(j = 0 ;j < 2000; j++)
    {
        if(REG_RD(pDevice, BufMgr.Mode) & BUFMGR_MODE_ENABLE)
            break;
        MM_Wait(10);
    }

    if(j >= 2000)
    {
        return LM_STATUS_FAILURE;
    }

    /* Enable the FTQs. */
    REG_WR(pDevice, Ftq.Reset, 0xffffffff);
    REG_WR(pDevice, Ftq.Reset, 0);

    /* Wait until FTQ is ready */
    for(j = 0; j < 2000; j++)
    {
        if(REG_RD(pDevice, Ftq.Reset) == 0)
            break;
        MM_Wait(10);
    }

    if(j >= 2000)
    {
        return LM_STATUS_FAILURE;
    }

    /* Receive BD Ring replenish threshold. */
    REG_WR(pDevice, RcvBdIn.StdRcvThreshold, pDevice->RxStdDescCnt/8);

    /* Initialize the Standard Receive RCB. */
    REG_WR(pDevice, RcvDataBdIn.StdRcvRcb.HostRingAddr.High, 
        pDevice->RxStdBdPhy.High);
    REG_WR(pDevice, RcvDataBdIn.StdRcvRcb.HostRingAddr.Low, 
        pDevice->RxStdBdPhy.Low);
    REG_WR(pDevice, RcvDataBdIn.StdRcvRcb.NicRingAddr,
        (LM_UINT32) T3_NIC_STD_RCV_BUFFER_DESC_ADDR);

    REG_WR(pDevice, RcvDataBdIn.StdRcvRcb.u.MaxLen_Flags,
        MAX_STD_RCV_BUFFER_SIZE << 16);

    /* Initialize the Jumbo Receive RCB. */
    REG_WR(pDevice, RcvDataBdIn.JumboRcvRcb.u.MaxLen_Flags,
        T3_RCB_FLAG_RING_DISABLED);
#if T3_JUMBO_RCV_RCB_ENTRY_COUNT
    REG_WR(pDevice, RcvDataBdIn.JumboRcvRcb.HostRingAddr.High, 
        pDevice->RxJumboBdPhy.High);
    REG_WR(pDevice, RcvDataBdIn.JumboRcvRcb.HostRingAddr.Low, 
        pDevice->RxJumboBdPhy.Low);
    REG_WR(pDevice, RcvDataBdIn.JumboRcvRcb.u.MaxLen_Flags, 0);
    REG_WR(pDevice, RcvDataBdIn.JumboRcvRcb.NicRingAddr,
        (LM_UINT32) T3_NIC_JUMBO_RCV_BUFFER_DESC_ADDR);

    REG_WR(pDevice, RcvBdIn.JumboRcvThreshold, pDevice->RxJumboDescCnt/8);

#endif /* T3_JUMBO_RCV_RCB_ENTRY_COUNT */

    /* Initialize the Mini Receive RCB. */
    REG_WR(pDevice, RcvDataBdIn.MiniRcvRcb.u.MaxLen_Flags,
        T3_RCB_FLAG_RING_DISABLED);

    /* Disable all the unused rings. */
    for(j = 0; j < T3_MAX_SEND_RCB_COUNT; j++) {
        MEM_WR(pDevice, SendRcb[j].u.MaxLen_Flags,
            T3_RCB_FLAG_RING_DISABLED);
    } /* for */

    /* Initialize the indices. */
    pDevice->SendProdIdx = 0;
    pDevice->SendConIdx = 0;

    MB_REG_WR(pDevice, Mailbox.SendHostProdIdx[0].Low, 0); 
    MB_REG_WR(pDevice, Mailbox.SendNicProdIdx[0].Low, 0);

    /* Set up host or NIC based send RCB. */
    if(pDevice->NicSendBd == FALSE)
    {
        MEM_WR(pDevice, SendRcb[0].HostRingAddr.High, 
            pDevice->SendBdPhy.High);
        MEM_WR(pDevice, SendRcb[0].HostRingAddr.Low, 
            pDevice->SendBdPhy.Low);

        /* Set up the NIC ring address in the RCB. */
        MEM_WR(pDevice, SendRcb[0].NicRingAddr,T3_NIC_SND_BUFFER_DESC_ADDR);

        /* Setup the RCB. */
        MEM_WR(pDevice, SendRcb[0].u.MaxLen_Flags,
            T3_SEND_RCB_ENTRY_COUNT << 16);

        /* Set up the NIC ring address in the RCB. */
        MEM_WR(pDevice, SendRcb[0].NicRingAddr,T3_NIC_SND_BUFFER_DESC_ADDR);

        for(k = 0; k < T3_SEND_RCB_ENTRY_COUNT; k++)
        {
            pDevice->pSendBdVirt[k].HostAddr.High = 0;
            pDevice->pSendBdVirt[k].HostAddr.Low = 0;
        }
    }
#ifdef BCM_NIC_SEND_BD
    else
    {
        MEM_WR(pDevice, SendRcb[0].HostRingAddr.High, 0);
        MEM_WR(pDevice, SendRcb[0].HostRingAddr.Low, 0);
        MEM_WR(pDevice, SendRcb[0].NicRingAddr,
            pDevice->SendBdPhy.Low);

        for(k = 0; k < T3_SEND_RCB_ENTRY_COUNT; k++)
        {
            __raw_writel(0, &(pDevice->pSendBdVirt[k].HostAddr.High));
            __raw_writel(0, &(pDevice->pSendBdVirt[k].HostAddr.Low));
            __raw_writel(0, &(pDevice->pSendBdVirt[k].u1.Len_Flags));
            pDevice->ShadowSendBd[k].HostAddr.High = 0;
            pDevice->ShadowSendBd[k].u1.Len_Flags = 0;
        }
    }
#endif
    atomic_set(&pDevice->SendBdLeft, T3_SEND_RCB_ENTRY_COUNT-1);

    /* Configure the receive return rings. */
    for(j = 0; j < T3_MAX_RCV_RETURN_RCB_COUNT; j++)
    {
        MEM_WR(pDevice, RcvRetRcb[j].u.MaxLen_Flags, T3_RCB_FLAG_RING_DISABLED);
    }

    pDevice->RcvRetConIdx = 0;

    MEM_WR(pDevice, RcvRetRcb[0].HostRingAddr.High, 
        pDevice->RcvRetBdPhy.High);
    MEM_WR(pDevice, RcvRetRcb[0].HostRingAddr.Low,
        pDevice->RcvRetBdPhy.Low);

    MEM_WR(pDevice, RcvRetRcb[0].NicRingAddr, 0);

    /* Setup the RCB. */
    MEM_WR(pDevice, RcvRetRcb[0].u.MaxLen_Flags,
        pDevice->RcvRetRcbEntryCount << 16);

    /* Reinitialize RX ring producer index */
    MB_REG_WR(pDevice, Mailbox.RcvStdProdIdx.Low, 0);
    MB_REG_WR(pDevice, Mailbox.RcvJumboProdIdx.Low, 0);
    MB_REG_WR(pDevice, Mailbox.RcvMiniProdIdx.Low, 0);

#if T3_JUMBO_RCV_RCB_ENTRY_COUNT
    pDevice->RxJumboProdIdx = 0;
    pDevice->RxJumboQueuedCnt = 0;
#endif

    /* Reinitialize our copy of the indices. */
    pDevice->RxStdProdIdx = 0;
    pDevice->RxStdQueuedCnt = 0;

#if T3_JUMBO_RCV_ENTRY_COUNT
    pDevice->RxJumboProdIdx = 0;
#endif /* T3_JUMBO_RCV_ENTRY_COUNT */

    /* Configure the MAC address. */
    LM_SetMacAddress(pDevice, pDevice->NodeAddress);

    /* Initialize the transmit random backoff seed. */
    Value32 = (pDevice->NodeAddress[0] + pDevice->NodeAddress[1] + 
        pDevice->NodeAddress[2] + pDevice->NodeAddress[3] + 
        pDevice->NodeAddress[4] + pDevice->NodeAddress[5]) & 
        MAC_TX_BACKOFF_SEED_MASK;
    REG_WR(pDevice, MacCtrl.TxBackoffSeed, Value32);

    /* Receive MTU.  Frames larger than the MTU is marked as oversized. */
    REG_WR(pDevice, MacCtrl.MtuSize, pDevice->RxMtu + 8);   /* CRC + VLAN. */

    /* Configure Time slot/IPG per 802.3 */
    REG_WR(pDevice, MacCtrl.TxLengths, 0x2620);

    /*
     * Configure Receive Rules so that packets don't match 
     * Programmble rule will be queued to Return Ring 1 
     */
    REG_WR(pDevice, MacCtrl.RcvRuleCfg, RX_RULE_DEFAULT_CLASS);

    /* 
     * Configure to have 16 Classes of Services (COS) and one
     * queue per class.  Bad frames are queued to RRR#1.
     * And frames don't match rules are also queued to COS#1.
     */
    REG_WR(pDevice, RcvListPlmt.Config, 0x181);
    
    /* Enable Receive Placement Statistics */
    REG_WR(pDevice, RcvListPlmt.StatsEnableMask,0xffffff);
    REG_WR(pDevice, RcvListPlmt.StatsCtrl, RCV_LIST_STATS_ENABLE);

    /* Enable Send Data Initator Statistics */
    REG_WR(pDevice, SndDataIn.StatsEnableMask,0xffffff);
    REG_WR(pDevice, SndDataIn.StatsCtrl,
        T3_SND_DATA_IN_STATS_CTRL_ENABLE | \
        T3_SND_DATA_IN_STATS_CTRL_FASTER_UPDATE);

    /* Disable the host coalescing state machine before configuring it's */
    /* parameters. */
    REG_WR(pDevice, HostCoalesce.Mode, 0); 
    for(j = 0; j < 2000; j++)
    {
        Value32 = REG_RD(pDevice, HostCoalesce.Mode);
        if(!(Value32 & HOST_COALESCE_ENABLE))
        {
            break;
        }
        MM_Wait(10);
    }

    /* Host coalescing configurations. */
    REG_WR(pDevice, HostCoalesce.RxCoalescingTicks, pDevice->RxCoalescingTicks);
    REG_WR(pDevice, HostCoalesce.TxCoalescingTicks, pDevice->TxCoalescingTicks);
    REG_WR(pDevice, HostCoalesce.RxMaxCoalescedFrames,
        pDevice->RxMaxCoalescedFrames);
    REG_WR(pDevice, HostCoalesce.TxMaxCoalescedFrames,
        pDevice->TxMaxCoalescedFrames);
    REG_WR(pDevice, HostCoalesce.RxCoalescedTickDuringInt,
        pDevice->RxCoalescingTicksDuringInt);
    REG_WR(pDevice, HostCoalesce.TxCoalescedTickDuringInt,
        pDevice->TxCoalescingTicksDuringInt);
    REG_WR(pDevice, HostCoalesce.RxMaxCoalescedFramesDuringInt,
        pDevice->RxMaxCoalescedFramesDuringInt);
    REG_WR(pDevice, HostCoalesce.TxMaxCoalescedFramesDuringInt,
        pDevice->TxMaxCoalescedFramesDuringInt);

    /* Initialize the address of the status block.  The NIC will DMA */
    /* the status block to this memory which resides on the host. */
    REG_WR(pDevice, HostCoalesce.StatusBlkHostAddr.High, 
        pDevice->StatusBlkPhy.High);
    REG_WR(pDevice, HostCoalesce.StatusBlkHostAddr.Low,
        pDevice->StatusBlkPhy.Low);

    /* Initialize the address of the statistics block.  The NIC will DMA */
    /* the statistics to this block of memory. */
    REG_WR(pDevice, HostCoalesce.StatsBlkHostAddr.High, 
        pDevice->StatsBlkPhy.High);
    REG_WR(pDevice, HostCoalesce.StatsBlkHostAddr.Low,
        pDevice->StatsBlkPhy.Low);

    REG_WR(pDevice, HostCoalesce.StatsCoalescingTicks,
        pDevice->StatsCoalescingTicks);

    REG_WR(pDevice, HostCoalesce.StatsBlkNicAddr, 0x300);
    REG_WR(pDevice, HostCoalesce.StatusBlkNicAddr,0xb00);

    /* Enable Host Coalesing state machine */
    REG_WR(pDevice, HostCoalesce.Mode, HOST_COALESCE_ENABLE |
        pDevice->CoalesceMode);

    /* Enable the Receive BD Completion state machine. */
    REG_WR(pDevice, RcvBdComp.Mode, RCV_BD_COMP_MODE_ENABLE |
        RCV_BD_COMP_MODE_ATTN_ENABLE);

    /* Enable the Receive List Placement state machine. */
    REG_WR(pDevice, RcvListPlmt.Mode, RCV_LIST_PLMT_MODE_ENABLE);

    /* Enable the Receive List Selector state machine. */
    REG_WR(pDevice, RcvListSel.Mode, RCV_LIST_SEL_MODE_ENABLE |
        RCV_LIST_SEL_MODE_ATTN_ENABLE);

    /* Enable transmit DMA, clear statistics. */
    pDevice->MacMode =  MAC_MODE_ENABLE_TX_STATISTICS |
        MAC_MODE_ENABLE_RX_STATISTICS | MAC_MODE_ENABLE_TDE |
        MAC_MODE_ENABLE_RDE | MAC_MODE_ENABLE_FHDE;
    REG_WR(pDevice, MacCtrl.Mode, pDevice->MacMode |
        MAC_MODE_CLEAR_RX_STATISTICS | MAC_MODE_CLEAR_TX_STATISTICS);

    /* GRC miscellaneous local control register. */
    pDevice->GrcLocalCtrl = GRC_MISC_LOCAL_CTRL_INT_ON_ATTN |
        GRC_MISC_LOCAL_CTRL_AUTO_SEEPROM;

    if(T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5700)
    {
        pDevice->GrcLocalCtrl |= GRC_MISC_LOCAL_CTRL_GPIO_OE1 |
            GRC_MISC_LOCAL_CTRL_GPIO_OUTPUT1;
    }

    REG_WR(pDevice, Grc.LocalCtrl, pDevice->GrcLocalCtrl);
    MM_Wait(40);

    /* Reset RX counters. */
    for(j = 0; j < sizeof(LM_RX_COUNTERS); j++)
    {
        ((PLM_UINT8) &pDevice->RxCounters)[j] = 0;
    }

    /* Reset TX counters. */
    for(j = 0; j < sizeof(LM_TX_COUNTERS); j++)
    {
        ((PLM_UINT8) &pDevice->TxCounters)[j] = 0;
    }

    MB_REG_WR(pDevice, Mailbox.Interrupt[0].Low, 0);
    pDevice->LastTag = 0;

    /* Enable the DMA Completion state machine. */
    REG_WR(pDevice, DmaComp.Mode, DMA_COMP_MODE_ENABLE);

    /* Enable the DMA Write state machine. */
    Value32 = DMA_WRITE_MODE_ENABLE |
        DMA_WRITE_MODE_TARGET_ABORT_ATTN_ENABLE |
        DMA_WRITE_MODE_MASTER_ABORT_ATTN_ENABLE |
        DMA_WRITE_MODE_PARITY_ERROR_ATTN_ENABLE |
        DMA_WRITE_MODE_ADDR_OVERFLOW_ATTN_ENABLE |
        DMA_WRITE_MODE_FIFO_OVERRUN_ATTN_ENABLE |
        DMA_WRITE_MODE_FIFO_UNDERRUN_ATTN_ENABLE |
        DMA_WRITE_MODE_FIFO_OVERREAD_ATTN_ENABLE |
        DMA_WRITE_MODE_LONG_READ_ATTN_ENABLE;
    REG_WR(pDevice, DmaWrite.Mode, Value32);

    if (!(pDevice->PciState & T3_PCI_STATE_CONVENTIONAL_PCI_MODE))
    {
        if (T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5703)
        {
            Value32 = REG_RD(pDevice, PciCfg.PciXCapabilities);
            Value32 &= ~PCIX_CMD_MAX_BURST_MASK;
            Value32 |= PCIX_CMD_MAX_BURST_CPIOB << PCIX_CMD_MAX_BURST_SHL;
            REG_WR(pDevice, PciCfg.PciXCapabilities, Value32);
        }
        else if (T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5704)
        {
            Value32 = REG_RD(pDevice, PciCfg.PciXCapabilities);
            Value32 &= ~(PCIX_CMD_MAX_SPLIT_MASK | PCIX_CMD_MAX_BURST_MASK);
            Value32 |= ((PCIX_CMD_MAX_BURST_CPIOB << PCIX_CMD_MAX_BURST_SHL) &
                PCIX_CMD_MAX_BURST_MASK);
            if (pDevice->SplitModeEnable == SPLIT_MODE_ENABLE)
            {
                Value32 |= (pDevice->SplitModeMaxReq << PCIX_CMD_MAX_SPLIT_SHL)
                   & PCIX_CMD_MAX_SPLIT_MASK;
            }
            REG_WR(pDevice, PciCfg.PciXCapabilities, Value32);
        }
    }

    /* Enable the Read DMA state machine. */
    Value32 = DMA_READ_MODE_ENABLE |
        DMA_READ_MODE_TARGET_ABORT_ATTN_ENABLE |
        DMA_READ_MODE_MASTER_ABORT_ATTN_ENABLE |
        DMA_READ_MODE_PARITY_ERROR_ATTN_ENABLE |
        DMA_READ_MODE_ADDR_OVERFLOW_ATTN_ENABLE |
        DMA_READ_MODE_FIFO_OVERRUN_ATTN_ENABLE |
        DMA_READ_MODE_FIFO_UNDERRUN_ATTN_ENABLE |
        DMA_READ_MODE_FIFO_OVERREAD_ATTN_ENABLE |
        DMA_READ_MODE_LONG_READ_ATTN_ENABLE;

    if (pDevice->SplitModeEnable == SPLIT_MODE_ENABLE)
    {
        Value32 |= DMA_READ_MODE_SPLIT_ENABLE;
    }
    REG_WR(pDevice, DmaRead.Mode, Value32);

    /* Enable the Receive Data Completion state machine. */
    REG_WR(pDevice, RcvDataComp.Mode, RCV_DATA_COMP_MODE_ENABLE |
        RCV_DATA_COMP_MODE_ATTN_ENABLE);

    /* Enable the Mbuf Cluster Free state machine. */
    REG_WR(pDevice, MbufClusterFree.Mode, MBUF_CLUSTER_FREE_MODE_ENABLE);

    /* Enable the Send Data Completion state machine. */
    REG_WR(pDevice, SndDataComp.Mode, SND_DATA_COMP_MODE_ENABLE);

    /* Enable the Send BD Completion state machine. */
    REG_WR(pDevice, SndBdComp.Mode, SND_BD_COMP_MODE_ENABLE |
        SND_BD_COMP_MODE_ATTN_ENABLE);

    /* Enable the Receive BD Initiator state machine. */
    REG_WR(pDevice, RcvBdIn.Mode, RCV_BD_IN_MODE_ENABLE |
        RCV_BD_IN_MODE_BD_IN_DIABLED_RCB_ATTN_ENABLE);

    /* Enable the Receive Data and Receive BD Initiator state machine. */
    REG_WR(pDevice, RcvDataBdIn.Mode, RCV_DATA_BD_IN_MODE_ENABLE |
        RCV_DATA_BD_IN_MODE_INVALID_RING_SIZE);

    /* Enable the Send Data Initiator state machine. */
    REG_WR(pDevice, SndDataIn.Mode, T3_SND_DATA_IN_MODE_ENABLE);

    /* Enable the Send BD Initiator state machine. */
    REG_WR(pDevice, SndBdIn.Mode, SND_BD_IN_MODE_ENABLE |
        SND_BD_IN_MODE_ATTN_ENABLE);

    /* Enable the Send BD Selector state machine. */
    REG_WR(pDevice, SndBdSel.Mode, SND_BD_SEL_MODE_ENABLE |
        SND_BD_SEL_MODE_ATTN_ENABLE);

#if INCLUDE_5701_AX_FIX
    /* Load the firmware for the 5701_A0 workaround. */
    if(pDevice->ChipRevId == T3_CHIP_ID_5701_A0)
    {
        LM_LoadRlsFirmware(pDevice);
    }
#endif

    /* Enable the transmitter. */
    pDevice->TxMode = TX_MODE_ENABLE;
    REG_WR(pDevice, MacCtrl.TxMode, pDevice->TxMode);
    
    /* Enable the receiver. */
    pDevice->RxMode = (pDevice->RxMode & RX_MODE_KEEP_VLAN_TAG) |
        RX_MODE_ENABLE;
    REG_WR(pDevice, MacCtrl.RxMode, pDevice->RxMode);

#ifdef BCM_WOL
    if (pDevice->RestoreOnWakeUp)
    {
        pDevice->RestoreOnWakeUp = FALSE;
        pDevice->DisableAutoNeg = pDevice->WakeUpDisableAutoNeg;
        pDevice->RequestedLineSpeed = pDevice->WakeUpRequestedLineSpeed;
        pDevice->RequestedDuplexMode = pDevice->WakeUpRequestedDuplexMode;
    }
#endif

    /* Disable auto polling. */
    pDevice->MiMode = 0xc0000;
    REG_WR(pDevice, MacCtrl.MiMode, pDevice->MiMode);

    if(T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5700 ||
        T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5701)
    {
        Value32 = LED_CTRL_PHY_MODE_1;
    }
    else
    {
        if(pDevice->LedMode == LED_MODE_OUTPUT)
        {
            Value32 = LED_CTRL_PHY_MODE_2;
        }
        else
        {
            Value32 = LED_CTRL_PHY_MODE_1;
        }
    }
    REG_WR(pDevice, MacCtrl.LedCtrl, Value32);
    
    /* Activate Link to enable MAC state machine */
    REG_WR(pDevice, MacCtrl.MiStatus, MI_STATUS_ENABLE_LINK_STATUS_ATTN);

    if (pDevice->EnableTbi)
    {
        REG_WR(pDevice, MacCtrl.RxMode, RX_MODE_RESET);
        MM_Wait(10);
        REG_WR(pDevice, MacCtrl.RxMode, pDevice->RxMode);
        if (pDevice->ChipRevId == T3_CHIP_ID_5703_A1)
        {
            REG_WR(pDevice, MacCtrl.SerdesCfg, 0x616000);
        }
    }

    if (pDevice->EnableAsf)
    {
        LM_ResetPhy(pDevice);
    }

    /* Setup the phy chip. */
    LM_SetupPhy(pDevice);

    if (!pDevice->EnableTbi) {
        /* Clear CRC stats */
        LM_ReadPhy(pDevice, 0x1e, &Value32);
        LM_WritePhy(pDevice, 0x1e, Value32 | 0x8000);
        LM_ReadPhy(pDevice, 0x14, &Value32);
    }

    /* Set up the receive mask. */
    LM_SetReceiveMask(pDevice, pDevice->ReceiveMask);

    /* Queue Rx packet buffers. */
    if(pDevice->QueueRxPackets)
    {
        LM_QueueRxPackets(pDevice);
    }

    return LM_STATUS_SUCCESS;
} /* LM_ResetAdapter */


/******************************************************************************/
/* Description:                                                               */
/*    This routine disables the adapter from generating interrupts.           */
/*                                                                            */
/* Return:                                                                    */
/*    LM_STATUS_SUCCESS                                                       */
/******************************************************************************/
LM_STATUS
LM_DisableInterrupt(
    PLM_DEVICE_BLOCK pDevice)
{
    REG_WR(pDevice, PciCfg.MiscHostCtrl, pDevice->MiscHostCtrl | 
        MISC_HOST_CTRL_MASK_PCI_INT);
    MB_REG_WR(pDevice, Mailbox.Interrupt[0].Low, 1);
    if (pDevice->FlushPostedWrites)
    {
        MB_REG_RD(pDevice, Mailbox.Interrupt[0].Low);
    }

    return LM_STATUS_SUCCESS;
}



/******************************************************************************/
/* Description:                                                               */
/*    This routine enables the adapter to generate interrupts.                */
/*                                                                            */
/* Return:                                                                    */
/*    LM_STATUS_SUCCESS                                                       */
/******************************************************************************/
LM_STATUS
LM_EnableInterrupt(
    PLM_DEVICE_BLOCK pDevice)
{
    MB_REG_WR(pDevice, Mailbox.Interrupt[0].Low, pDevice->LastTag << 24);
    REG_WR(pDevice, PciCfg.MiscHostCtrl, pDevice->MiscHostCtrl &
        ~MISC_HOST_CTRL_MASK_PCI_INT);

    if(pDevice->pStatusBlkVirt->Status & STATUS_BLOCK_UPDATED)
    {
        REG_WR(pDevice, Grc.LocalCtrl, pDevice->GrcLocalCtrl |
            GRC_MISC_LOCAL_CTRL_SET_INT);
    }
    else
    {
        REG_WR(pDevice, HostCoalesce.Mode, pDevice->CoalesceMode |
            HOST_COALESCE_ENABLE | HOST_COALESCE_NOW);
    }

    if (pDevice->FlushPostedWrites)
    {
        MB_REG_RD(pDevice, Mailbox.Interrupt[0].Low);
    }

    return LM_STATUS_SUCCESS;
}



/******************************************************************************/
/* Description:                                                               */
/*    This routine puts a packet on the wire if there is a transmit DMA       */
/*    descriptor available; otherwise the packet is queued for later          */
/*    transmission.  If the second argue is NULL, this routine will put       */
/*    the queued packet on the wire if possible.                              */
/*                                                                            */
/* Return:                                                                    */
/*    LM_STATUS_SUCCESS                                                       */
/******************************************************************************/
LM_STATUS
LM_SendPacket(PLM_DEVICE_BLOCK pDevice, PLM_PACKET pPacket)
{
    LM_UINT32 FragCount;
    PT3_SND_BD pSendBd, pTmpSendBd;
#ifdef BCM_NIC_SEND_BD
    PT3_SND_BD pShadowSendBd;
    T3_SND_BD NicSendBdArr[MAX_FRAGMENT_COUNT];
#endif
    LM_UINT32 StartIdx, Idx;

    while (1)
    {
        /* Initalize the send buffer descriptors. */
        StartIdx = Idx = pDevice->SendProdIdx;

#ifdef BCM_NIC_SEND_BD
        if (pDevice->NicSendBd)
        {
            pTmpSendBd = pSendBd = &NicSendBdArr[0];
        }
        else
#endif
        {
            pTmpSendBd = pSendBd = &pDevice->pSendBdVirt[Idx];
        }

        /* Next producer index. */
        for(FragCount = 0; ; )
        {
            LM_UINT32 Value32, Len;

            /* Initialize the pointer to the send buffer fragment. */
            MM_MapTxDma(pDevice, pPacket, &pSendBd->HostAddr, &Len, FragCount);

            pSendBd->u2.VlanTag = pPacket->VlanTag;

            /* Setup the control flags and send buffer size. */
            Value32 = (Len << 16) | pPacket->Flags;

            Idx = (Idx + 1) & T3_SEND_RCB_ENTRY_COUNT_MASK;
                
            FragCount++;
            if (FragCount >= pPacket->u.Tx.FragCount)
            {
                pSendBd->u1.Len_Flags = Value32 | SND_BD_FLAG_END;
                break;
            }
            else
            {
                pSendBd->u1.Len_Flags = Value32;
            }
            pSendBd++;
            if ((Idx == 0) && !pDevice->NicSendBd)
            {
                pSendBd = &pDevice->pSendBdVirt[0];
            }
        } /* for */
        if (T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5700)
        {
            if (LM_Test4GBoundary(pDevice, pPacket, pTmpSendBd) ==
                LM_STATUS_SUCCESS)
            {
                if (MM_CoalesceTxBuffer(pDevice, pPacket) != LM_STATUS_SUCCESS)
                {
                    QQ_PushHead(&pDevice->TxPacketFreeQ.Container, pPacket);
                    return LM_STATUS_FAILURE;
                }
                continue;
            }
        }
        break;
    }
    /* Put the packet descriptor in the ActiveQ. */
    QQ_PushTail(&pDevice->TxPacketActiveQ.Container, pPacket);

#ifdef BCM_NIC_SEND_BD
    if (pDevice->NicSendBd)
    {
        pSendBd = &pDevice->pSendBdVirt[StartIdx];
        pShadowSendBd = &pDevice->ShadowSendBd[StartIdx];

        while (StartIdx != Idx)
        {
            LM_UINT32 Value32;

            if ((Value32 = pTmpSendBd->HostAddr.High) !=
                pShadowSendBd->HostAddr.High)
            {
                __raw_writel(Value32, &(pSendBd->HostAddr.High));
                pShadowSendBd->HostAddr.High = Value32;
            }

            __raw_writel(pTmpSendBd->HostAddr.Low, &(pSendBd->HostAddr.Low));

            if ((Value32 = pTmpSendBd->u1.Len_Flags) !=
                pShadowSendBd->u1.Len_Flags)
            {
                __raw_writel(Value32, &(pSendBd->u1.Len_Flags));
                pShadowSendBd->u1.Len_Flags = Value32;
            }

            if (pPacket->Flags & SND_BD_FLAG_VLAN_TAG)
            {
                __raw_writel(pTmpSendBd->u2.VlanTag, &(pSendBd->u2.VlanTag));
            }

            StartIdx = (StartIdx + 1) & T3_SEND_RCB_ENTRY_COUNT_MASK;
            if (StartIdx == 0)
            {
                pSendBd = &pDevice->pSendBdVirt[0];
                pShadowSendBd = &pDevice->ShadowSendBd[0];
            }
            else
            {
                pSendBd++;
                pShadowSendBd++;
            }
            pTmpSendBd++;
        }
        wmb();
        MB_REG_WR(pDevice, Mailbox.SendNicProdIdx[0].Low, Idx);

        if(T3_CHIP_REV(pDevice->ChipRevId) == T3_CHIP_REV_5700_BX)
        {
            MB_REG_WR(pDevice, Mailbox.SendNicProdIdx[0].Low, Idx);
        }
        if (pDevice->FlushPostedWrites)
        {
            MB_REG_RD(pDevice, Mailbox.SendNicProdIdx[0].Low);
        }
    }
    else
#endif
    {
        wmb();
        MB_REG_WR(pDevice, Mailbox.SendHostProdIdx[0].Low, Idx);

        if(T3_CHIP_REV(pDevice->ChipRevId) == T3_CHIP_REV_5700_BX)
        {
            MB_REG_WR(pDevice, Mailbox.SendHostProdIdx[0].Low, Idx);
        }
        if (pDevice->FlushPostedWrites)
        {
            MB_REG_RD(pDevice, Mailbox.SendHostProdIdx[0].Low);
        }
    }

    /* Update the SendBdLeft count. */
    atomic_sub(pPacket->u.Tx.FragCount, &pDevice->SendBdLeft);

    /* Update the producer index. */
    pDevice->SendProdIdx = Idx;

    return LM_STATUS_SUCCESS;
}

STATIC LM_STATUS
LM_Test4GBoundary(PLM_DEVICE_BLOCK pDevice, PLM_PACKET pPacket,
    PT3_SND_BD pSendBd)
{
    int FragCount;
    LM_UINT32 Idx, Base, Len;

    Idx = pDevice->SendProdIdx;
    for(FragCount = 0; ; )
    {
        Len = pSendBd->u1.Len_Flags >> 16;
        if (((Base = pSendBd->HostAddr.Low) > 0xffffdcc0) &&
            (pSendBd->HostAddr.High == 0) &&
            ((Base + 8 + Len) < Base))
        {
            return LM_STATUS_SUCCESS;
        }
        FragCount++;
        if (FragCount >= pPacket->u.Tx.FragCount)
        {
            break;
        }
        pSendBd++;
        if (!pDevice->NicSendBd)
        {
            Idx = (Idx + 1) & T3_SEND_RCB_ENTRY_COUNT_MASK;
            if (Idx == 0)
            {
                pSendBd = &pDevice->pSendBdVirt[0];
            }
        }
    }
    return LM_STATUS_FAILURE;
}

/******************************************************************************/
/* Description:                                                               */
/*                                                                            */
/* Return:                                                                    */
/******************************************************************************/
__inline static unsigned long
ComputeCrc32(
unsigned char *pBuffer,
unsigned long BufferSize) {
    unsigned long Reg;
    unsigned long Tmp;
    unsigned long j, k;

    Reg = 0xffffffff;

    for(j = 0; j < BufferSize; j++)
    {
        Reg ^= pBuffer[j];

        for(k = 0; k < 8; k++)
        {
            Tmp = Reg & 0x01;

            Reg >>= 1;

            if(Tmp)
            {
                Reg ^= 0xedb88320;
            }
        }
    }

    return ~Reg;
} /* ComputeCrc32 */



/******************************************************************************/
/* Description:                                                               */
/*    This routine sets the receive control register according to ReceiveMask */
/*                                                                            */
/* Return:                                                                    */
/*    LM_STATUS_SUCCESS                                                       */
/******************************************************************************/
LM_STATUS
LM_SetReceiveMask(PLM_DEVICE_BLOCK pDevice, LM_UINT32 Mask)
{
    LM_UINT32 ReceiveMask;
    LM_UINT32 RxMode;
    LM_UINT32 j, k;

    ReceiveMask = Mask;

    RxMode = pDevice->RxMode;

    if(Mask & LM_ACCEPT_UNICAST)
    {
        Mask &= ~LM_ACCEPT_UNICAST;
    }

    if(Mask & LM_ACCEPT_MULTICAST)
    {
        Mask &= ~LM_ACCEPT_MULTICAST;
    }

    if(Mask & LM_ACCEPT_ALL_MULTICAST)
    {
        Mask &= ~LM_ACCEPT_ALL_MULTICAST;
    }

    if(Mask & LM_ACCEPT_BROADCAST)
    {
        Mask &= ~LM_ACCEPT_BROADCAST;
    }

    RxMode &= ~RX_MODE_KEEP_VLAN_TAG;
    if (Mask & LM_KEEP_VLAN_TAG)
    {
        RxMode |= RX_MODE_KEEP_VLAN_TAG;
        Mask &= ~LM_KEEP_VLAN_TAG;
    }

    RxMode &= ~RX_MODE_PROMISCUOUS_MODE;
    if(Mask & LM_PROMISCUOUS_MODE)
    {
        RxMode |= RX_MODE_PROMISCUOUS_MODE;
        Mask &= ~LM_PROMISCUOUS_MODE;
    }

    RxMode &= ~(RX_MODE_ACCEPT_RUNTS | RX_MODE_ACCEPT_OVERSIZED);
    if(Mask & LM_ACCEPT_ERROR_PACKET)
    {
        RxMode |= RX_MODE_ACCEPT_RUNTS | RX_MODE_ACCEPT_OVERSIZED;
        Mask &= ~LM_ACCEPT_ERROR_PACKET;
    }

    /* Make sure all the bits are valid before committing changes. */
    if(Mask)
    {
        return LM_STATUS_FAILURE;
    }

    /* Commit the new filter. */
    pDevice->RxMode = RxMode;
    REG_WR(pDevice, MacCtrl.RxMode, RxMode);

    pDevice->ReceiveMask = ReceiveMask;

    /* Set up the MC hash table. */
    if(ReceiveMask & LM_ACCEPT_ALL_MULTICAST)
    {
        for(k = 0; k < 4; k++)
        {
            REG_WR(pDevice, MacCtrl.HashReg[k], 0xffffffff);
        }
    }
    else if(ReceiveMask & LM_ACCEPT_MULTICAST)
    {
        LM_UINT32 HashReg[4];

        HashReg[0] = 0; HashReg[1] = 0; HashReg[2] = 0; HashReg[3] = 0;
        for(j = 0; j < pDevice->McEntryCount; j++)
        {
            LM_UINT32 RegIndex;
            LM_UINT32 Bitpos;
            LM_UINT32 Crc32;

            Crc32 = ComputeCrc32(pDevice->McTable[j], ETHERNET_ADDRESS_SIZE);

            /* The most significant 7 bits of the CRC32 (no inversion), */
            /* are used to index into one of the possible 128 bit positions. */
            Bitpos = ~Crc32 & 0x7f;

            /* Hash register index. */
            RegIndex = (Bitpos & 0x60) >> 5;

            /* Bit to turn on within a hash register. */
            Bitpos &= 0x1f;

            /* Enable the multicast bit. */
            HashReg[RegIndex] |= (1 << Bitpos);
        }

        /* REV_AX has problem with multicast filtering where it uses both */
        /* DA and SA to perform hashing. */
        for(k = 0; k < 4; k++)
        {
            REG_WR(pDevice, MacCtrl.HashReg[k], HashReg[k]);
        }
    }
    else
    {
        /* Reject all multicast frames. */
        for(j = 0; j < 4; j++)
        {
            REG_WR(pDevice, MacCtrl.HashReg[j], 0);
        }
    }

    /* By default, Tigon3 will accept broadcast frames.  We need to setup */
    if(ReceiveMask & LM_ACCEPT_BROADCAST)
    {
        REG_WR(pDevice, MacCtrl.RcvRules[RCV_RULE1_REJECT_BROADCAST_IDX].Rule,
            REJECT_BROADCAST_RULE1_RULE & RCV_DISABLE_RULE_MASK);
        REG_WR(pDevice, MacCtrl.RcvRules[RCV_RULE1_REJECT_BROADCAST_IDX].Value,
            REJECT_BROADCAST_RULE1_VALUE & RCV_DISABLE_RULE_MASK);
        REG_WR(pDevice, MacCtrl.RcvRules[RCV_RULE2_REJECT_BROADCAST_IDX].Rule,
            REJECT_BROADCAST_RULE1_RULE & RCV_DISABLE_RULE_MASK);
        REG_WR(pDevice, MacCtrl.RcvRules[RCV_RULE2_REJECT_BROADCAST_IDX].Value,
            REJECT_BROADCAST_RULE1_VALUE & RCV_DISABLE_RULE_MASK);
    }
    else
    {
        REG_WR(pDevice, MacCtrl.RcvRules[RCV_RULE1_REJECT_BROADCAST_IDX].Rule, 
            REJECT_BROADCAST_RULE1_RULE);
        REG_WR(pDevice, MacCtrl.RcvRules[RCV_RULE1_REJECT_BROADCAST_IDX].Value, 
            REJECT_BROADCAST_RULE1_VALUE);
        REG_WR(pDevice, MacCtrl.RcvRules[RCV_RULE2_REJECT_BROADCAST_IDX].Rule, 
            REJECT_BROADCAST_RULE2_RULE);
        REG_WR(pDevice, MacCtrl.RcvRules[RCV_RULE2_REJECT_BROADCAST_IDX].Value, 
            REJECT_BROADCAST_RULE2_VALUE);
    }

    k = 16;
#ifdef BCM_ASF
    if (pDevice->EnableAsf)
    {
        k -= 4;
    }
#endif

    /* disable the rest of the rules. */
    for(j = RCV_LAST_RULE_IDX; j < k; j++)
    {
        REG_WR(pDevice, MacCtrl.RcvRules[j].Rule, 0);
        REG_WR(pDevice, MacCtrl.RcvRules[j].Value, 0);
    }

    return LM_STATUS_SUCCESS;
} /* LM_SetReceiveMask */



/******************************************************************************/
/* Description:                                                               */
/*    Disable the interrupt and put the transmitter and receiver engines in   */
/*    an idle state.  Also aborts all pending send requests and receive       */
/*    buffers.                                                                */
/*                                                                            */
/* Return:                                                                    */
/*    LM_STATUS_SUCCESS                                                       */
/******************************************************************************/
LM_STATUS
LM_Abort(
PLM_DEVICE_BLOCK pDevice)
{
    PLM_PACKET pPacket;
    LM_UINT Idx;

    LM_DisableInterrupt(pDevice);

    LM_DisableChip(pDevice);

    /* Abort packets that have already queued to go out. */
    pPacket = (PLM_PACKET) QQ_PopHead(&pDevice->TxPacketActiveQ.Container);
    while(pPacket)
    {

        pPacket->PacketStatus = LM_STATUS_TRANSMIT_ABORTED;
        pDevice->TxCounters.TxPacketAbortedCnt++;

        atomic_add(pPacket->u.Tx.FragCount, &pDevice->SendBdLeft);

        QQ_PushTail(&pDevice->TxPacketXmittedQ.Container, pPacket);

        pPacket = (PLM_PACKET) 
            QQ_PopHead(&pDevice->TxPacketActiveQ.Container);
    }

    /* Cleanup the receive return rings. */
    LM_ServiceRxInterrupt(pDevice);

    /* Don't want to indicate rx packets in Ndis miniport shutdown context. */
    /* Doing so may cause system crash. */
    if(!pDevice->ShuttingDown)
    {
        /* Indicate packets to the protocol. */
        MM_IndicateTxPackets(pDevice);

        /* Indicate received packets to the protocols. */
        MM_IndicateRxPackets(pDevice);
    }
    else
    {
        /* Move the receive packet descriptors in the ReceivedQ to the */
        /* free queue. */
        for(; ;)
        {
            pPacket = (PLM_PACKET) QQ_PopHead(
                &pDevice->RxPacketReceivedQ.Container);
            if(pPacket == NULL)
            {
                break;
            }
            QQ_PushTail(&pDevice->RxPacketFreeQ.Container, pPacket);
        }
    }

    /* Clean up the Std Receive Producer ring. */
    Idx = pDevice->pStatusBlkVirt->RcvStdConIdx;

    while(Idx != pDevice->RxStdProdIdx) {
        pPacket = (PLM_PACKET) (MM_UINT_PTR(pDevice->pPacketDescBase) +
            MM_UINT_PTR(pDevice->pRxStdBdVirt[Idx].Opaque));

        QQ_PushTail(&pDevice->RxPacketFreeQ.Container, pPacket);

        Idx = (Idx + 1) & T3_STD_RCV_RCB_ENTRY_COUNT_MASK;
    } /* while */

    /* Reinitialize our copy of the indices. */
    pDevice->RxStdProdIdx = 0;

#if T3_JUMBO_RCV_RCB_ENTRY_COUNT
    /* Clean up the Jumbo Receive Producer ring. */
    Idx = pDevice->pStatusBlkVirt->RcvJumboConIdx;

    while(Idx != pDevice->RxJumboProdIdx) {
        pPacket = (PLM_PACKET) (MM_UINT_PTR(pDevice->pPacketDescBase) +
            MM_UINT_PTR(pDevice->pRxJumboBdVirt[Idx].Opaque));

        QQ_PushTail(&pDevice->RxPacketFreeQ.Container, pPacket);

        Idx = (Idx + 1) & T3_JUMBO_RCV_RCB_ENTRY_COUNT_MASK;
    } /* while */

    /* Reinitialize our copy of the indices. */
    pDevice->RxJumboProdIdx = 0;
#endif /* T3_JUMBO_RCV_RCB_ENTRY_COUNT */

    /* Initialize the statistis Block */
    pDevice->pStatusBlkVirt->Status = 0;
    pDevice->pStatusBlkVirt->RcvStdConIdx = 0;
    pDevice->pStatusBlkVirt->RcvJumboConIdx = 0;
    pDevice->pStatusBlkVirt->RcvMiniConIdx = 0;

    return LM_STATUS_SUCCESS;
} /* LM_Abort */



/******************************************************************************/
/* Description:                                                               */
/*    Disable the interrupt and put the transmitter and receiver engines in   */
/*    an idle state.  Aborts all pending send requests and receive buffers.   */
/*    Also free all the receive buffers.                                      */
/*                                                                            */
/* Return:                                                                    */
/*    LM_STATUS_SUCCESS                                                       */
/******************************************************************************/
LM_STATUS
LM_Halt(
PLM_DEVICE_BLOCK pDevice) {
    PLM_PACKET pPacket;
    LM_UINT32 EntryCnt;

    LM_DisableFW(pDevice);

    LM_Abort(pDevice);

    /* Get the number of entries in the queue. */
    EntryCnt = QQ_GetEntryCnt(&pDevice->RxPacketFreeQ.Container);

    /* Make sure all the packets have been accounted for. */
    for(EntryCnt = 0; EntryCnt < pDevice->RxPacketDescCnt; EntryCnt++)
    {
        pPacket = (PLM_PACKET) QQ_PopHead(&pDevice->RxPacketFreeQ.Container);
        if (pPacket == 0)
            break;

        MM_FreeRxBuffer(pDevice, pPacket);

        QQ_PushTail(&pDevice->RxPacketFreeQ.Container, pPacket);
    }

    LM_ResetChip(pDevice);

#ifdef BCM_ASF
    if (pDevice->EnableAsf)
    {
        if (pDevice->WakeUpModeCap == LM_WAKE_UP_MODE_NONE)
        {
            MEM_WR_OFFSET(pDevice, T3_DRV_STATE_MAILBOX, T3_DRV_STATE_UNLOAD);
        }
        else
        {
            MEM_WR_OFFSET(pDevice, T3_DRV_STATE_MAILBOX, T3_DRV_STATE_WOL);
        }
    }
    else
#endif
    {
        MEM_WR_OFFSET(pDevice, T3_DRV_STATE_MAILBOX, T3_DRV_STATE_SUSPEND);
    }
    /* Restore PCI configuration registers. */
    MM_WriteConfig32(pDevice, PCI_CACHE_LINE_SIZE_REG,
        pDevice->SavedCacheLineReg);
    LM_RegWrInd(pDevice, PCI_SUBSYSTEM_VENDOR_ID_REG, 
        (pDevice->SubsystemId << 16) | pDevice->SubsystemVendorId);

    /* Reprogram the MAC address. */
    LM_SetMacAddress(pDevice, pDevice->NodeAddress);

    return LM_STATUS_SUCCESS;
} /* LM_Halt */


STATIC LM_STATUS
LM_ResetChip(PLM_DEVICE_BLOCK pDevice)
{
    LM_UINT32 Value32;
    LM_UINT32 j;

    /* Wait for access to the nvram interface before resetting.  This is */
    /* a workaround to prevent EEPROM corruption. */
    if(T3_ASIC_REV(pDevice->ChipRevId) != T3_ASIC_REV_5700 &&
        T3_ASIC_REV(pDevice->ChipRevId) != T3_ASIC_REV_5701)
    {
        /* Request access to the flash interface. */
        REG_WR(pDevice, Nvram.SwArb, SW_ARB_REQ_SET1);

        for(j = 0; j < 100000; j++)
        {
            Value32 = REG_RD(pDevice, Nvram.SwArb);
            if(Value32 & SW_ARB_GNT1)
            {
                break;
            }
            MM_Wait(10);
        }
    }

    /* Global reset. */
    REG_WR(pDevice, Grc.MiscCfg, GRC_MISC_CFG_CORE_CLOCK_RESET);
    MM_Wait(40); MM_Wait(40); MM_Wait(40);

    /* make sure we re-enable indirect accesses */
    MM_WriteConfig32(pDevice, T3_PCI_MISC_HOST_CTRL_REG,
        pDevice->MiscHostCtrl);

    /* Set MAX PCI retry to zero. */
    Value32 = T3_PCI_STATE_PCI_ROM_ENABLE | T3_PCI_STATE_PCI_ROM_RETRY_ENABLE;
    if (pDevice->ChipRevId == T3_CHIP_ID_5704_A0)
    {
        if (!(pDevice->PciState & T3_PCI_STATE_CONVENTIONAL_PCI_MODE))
        {
            Value32 |= T3_PCI_STATE_RETRY_SAME_DMA;
        }
    }
    MM_WriteConfig32(pDevice, T3_PCI_STATE_REG, Value32);

    /* Restore PCI command register. */
    MM_WriteConfig32(pDevice, PCI_COMMAND_REG,
        pDevice->PciCommandStatusWords);

    /* Disable PCI-X relaxed ordering bit. */
    MM_ReadConfig32(pDevice, PCIX_CAP_REG, &Value32);
    Value32 &= ~PCIX_ENABLE_RELAXED_ORDERING;
    MM_WriteConfig32(pDevice, PCIX_CAP_REG, Value32);

    /* Enable memory arbiter. */
    REG_WR(pDevice, MemArbiter.Mode, T3_MEM_ARBITER_MODE_ENABLE);

#ifdef BIG_ENDIAN_HOST
    /* Reconfigure the mode register. */
    Value32 = GRC_MODE_BYTE_SWAP_NON_FRAME_DATA | 
              GRC_MODE_WORD_SWAP_NON_FRAME_DATA |
              GRC_MODE_BYTE_SWAP_DATA |
              GRC_MODE_WORD_SWAP_DATA;
#else
    /* Reconfigure the mode register. */
    Value32 = GRC_MODE_BYTE_SWAP_NON_FRAME_DATA | GRC_MODE_BYTE_SWAP_DATA;
#endif
    REG_WR(pDevice, Grc.Mode, Value32);

    /* Prevent PXE from restarting. */
    MEM_WR_OFFSET(pDevice, 0x0b50, T3_MAGIC_NUM);

    if(pDevice->EnableTbi) {
        pDevice->MacMode = MAC_MODE_PORT_MODE_TBI;
        REG_WR(pDevice, MacCtrl.Mode, MAC_MODE_PORT_MODE_TBI);
    }
    else {
        REG_WR(pDevice, MacCtrl.Mode, 0);
    }

    /* Wait for the firmware to finish initialization. */
    for(j = 0; j < 100000; j++)
    {
        MM_Wait(10);

        Value32 = MEM_RD_OFFSET(pDevice, 0x0b50);
        if(Value32 == ~T3_MAGIC_NUM)
        {
            break;
        }
    }

#ifdef BCM_ASF
    Value32 = MEM_RD_OFFSET(pDevice, T3_NIC_DATA_SIG_ADDR);
    if (Value32 == T3_NIC_DATA_SIG)
    {
        Value32 = MEM_RD_OFFSET(pDevice, T3_NIC_DATA_NIC_CFG_ADDR);
        if (Value32 & T3_NIC_CFG_ENABLE_ASF)
        {
            pDevice->EnableAsf = TRUE;
        }
    }
#endif

    return LM_STATUS_SUCCESS;
}

/******************************************************************************/
/* Description:                                                               */
/*                                                                            */
/* Return:                                                                    */
/******************************************************************************/
__inline static void
LM_ServiceTxInterrupt(
PLM_DEVICE_BLOCK pDevice) {
    PLM_PACKET pPacket;
    LM_UINT32 HwConIdx;
    LM_UINT32 SwConIdx;

    HwConIdx = pDevice->pStatusBlkVirt->Idx[0].SendConIdx;

    /* Get our copy of the consumer index.  The buffer descriptors */
    /* that are in between the consumer indices are freed. */
    SwConIdx = pDevice->SendConIdx;

    /* Move the packets from the TxPacketActiveQ that are sent out to */
    /* the TxPacketXmittedQ.  Packets that are sent use the */
    /* descriptors that are between SwConIdx and HwConIdx. */
    while(SwConIdx != HwConIdx)
    {
        /* Get the packet that was sent from the TxPacketActiveQ. */
        pPacket = (PLM_PACKET) QQ_PopHead(
            &pDevice->TxPacketActiveQ.Container);

        /* Set the return status. */
        pPacket->PacketStatus = LM_STATUS_SUCCESS;

        /* Put the packet in the TxPacketXmittedQ for indication later. */
        QQ_PushTail(&pDevice->TxPacketXmittedQ.Container, pPacket);

        /* Move to the next packet's BD. */
        SwConIdx = (SwConIdx + pPacket->u.Tx.FragCount) & 
            T3_SEND_RCB_ENTRY_COUNT_MASK;

        /* Update the number of unused BDs. */
        atomic_add(pPacket->u.Tx.FragCount, &pDevice->SendBdLeft);

        /* Get the new updated HwConIdx. */
        HwConIdx = pDevice->pStatusBlkVirt->Idx[0].SendConIdx;
    } /* while */

    /* Save the new SwConIdx. */
    pDevice->SendConIdx = SwConIdx;

} /* LM_ServiceTxInterrupt */



/******************************************************************************/
/* Description:                                                               */
/*                                                                            */
/* Return:                                                                    */
/******************************************************************************/
__inline static void
LM_ServiceRxInterrupt(
PLM_DEVICE_BLOCK pDevice) {
    PLM_PACKET pPacket;
    PT3_RCV_BD pRcvBd;
    LM_UINT32 HwRcvRetProdIdx;
    LM_UINT32 SwRcvRetConIdx;

    /* Loop thru the receive return rings for received packets. */
    HwRcvRetProdIdx = pDevice->pStatusBlkVirt->Idx[0].RcvProdIdx;

    SwRcvRetConIdx = pDevice->RcvRetConIdx;
    while(SwRcvRetConIdx != HwRcvRetProdIdx)
    {
        pRcvBd = &pDevice->pRcvRetBdVirt[SwRcvRetConIdx];

        /* Get the received packet descriptor. */
        pPacket = (PLM_PACKET) (MM_UINT_PTR(pDevice->pPacketDescBase) +
            MM_UINT_PTR(pRcvBd->Opaque));

        /* Check the error flag. */
        if(pRcvBd->ErrorFlag &&
            pRcvBd->ErrorFlag != RCV_BD_ERR_ODD_NIBBLED_RCVD_MII)
        {
            pPacket->PacketStatus = LM_STATUS_FAILURE;

            pDevice->RxCounters.RxPacketErrCnt++;

            if(pRcvBd->ErrorFlag & RCV_BD_ERR_BAD_CRC)
            {
                pDevice->RxCounters.RxErrCrcCnt++;
            }

            if(pRcvBd->ErrorFlag & RCV_BD_ERR_COLL_DETECT)
            {
                pDevice->RxCounters.RxErrCollCnt++;
            }

            if(pRcvBd->ErrorFlag & RCV_BD_ERR_LINK_LOST_DURING_PKT)
            {
                pDevice->RxCounters.RxErrLinkLostCnt++;
            }

            if(pRcvBd->ErrorFlag & RCV_BD_ERR_PHY_DECODE_ERR)
            {
                pDevice->RxCounters.RxErrPhyDecodeCnt++;
            }

            if(pRcvBd->ErrorFlag & RCV_BD_ERR_ODD_NIBBLED_RCVD_MII)
            {
                pDevice->RxCounters.RxErrOddNibbleCnt++;
            }

            if(pRcvBd->ErrorFlag & RCV_BD_ERR_MAC_ABORT)
            {
                pDevice->RxCounters.RxErrMacAbortCnt++;
            }

            if(pRcvBd->ErrorFlag & RCV_BD_ERR_LEN_LT_64)
            {
                pDevice->RxCounters.RxErrShortPacketCnt++;
            }

            if(pRcvBd->ErrorFlag & RCV_BD_ERR_TRUNC_NO_RESOURCES)
            {
                pDevice->RxCounters.RxErrNoResourceCnt++;
            }

            if(pRcvBd->ErrorFlag & RCV_BD_ERR_GIANT_FRAME_RCVD)
            {
                pDevice->RxCounters.RxErrLargePacketCnt++;
            }
        }
        else
        {
            pPacket->PacketStatus = LM_STATUS_SUCCESS;
            pPacket->PacketSize = pRcvBd->Len - 4;

            pPacket->Flags = pRcvBd->Flags;
            if(pRcvBd->Flags & RCV_BD_FLAG_VLAN_TAG)
            {
                pPacket->VlanTag = pRcvBd->VlanTag;
            }

            pPacket->u.Rx.TcpUdpChecksum = pRcvBd->TcpUdpCksum;
        }

        /* Put the packet descriptor containing the received packet */
        /* buffer in the RxPacketReceivedQ for indication later. */
        QQ_PushTail(&pDevice->RxPacketReceivedQ.Container, pPacket);

        /* Go to the next buffer descriptor. */
        SwRcvRetConIdx = (SwRcvRetConIdx + 1) &
            pDevice->RcvRetRcbEntryCountMask;

        /* Get the updated HwRcvRetProdIdx. */
        HwRcvRetProdIdx = pDevice->pStatusBlkVirt->Idx[0].RcvProdIdx;
    } /* while */

    pDevice->RcvRetConIdx = SwRcvRetConIdx;

    /* Update the receive return ring consumer index. */
    MB_REG_WR(pDevice, Mailbox.RcvRetConIdx[0].Low, SwRcvRetConIdx);
    if (pDevice->FlushPostedWrites)
    {
        MB_REG_RD(pDevice, Mailbox.RcvRetConIdx[0].Low);
    }
} /* LM_ServiceRxInterrupt */



/******************************************************************************/
/* Description:                                                               */
/*    This is the interrupt event handler routine. It acknowledges all        */
/*    pending interrupts and process all pending events.                      */
/*                                                                            */
/* Return:                                                                    */
/*    LM_STATUS_SUCCESS                                                       */
/******************************************************************************/
LM_STATUS
LM_ServiceInterrupts(
    PLM_DEVICE_BLOCK pDevice)
{
    LM_UINT32 Value32;
    int ServicePhyInt = FALSE;

    /* Setup the phy chip whenever the link status changes. */
    if(pDevice->LinkChngMode == T3_LINK_CHNG_MODE_USE_STATUS_REG)
    {
        Value32 = REG_RD(pDevice, MacCtrl.Status);
        if(pDevice->PhyIntMode == T3_PHY_INT_MODE_MI_INTERRUPT)
        {
            if (Value32 & MAC_STATUS_MI_INTERRUPT)
            {
                ServicePhyInt = TRUE;
            }
        }
        else if(Value32 & MAC_STATUS_LINK_STATE_CHANGED)
        {
            ServicePhyInt = TRUE;
        }
    }
    else
    {
        if(pDevice->pStatusBlkVirt->Status & STATUS_BLOCK_LINK_CHANGED_STATUS)
        {
            pDevice->pStatusBlkVirt->Status = STATUS_BLOCK_UPDATED |
                (pDevice->pStatusBlkVirt->Status & ~STATUS_BLOCK_LINK_CHANGED_STATUS);
            ServicePhyInt = TRUE;
        }
    }
#if INCLUDE_TBI_SUPPORT
    if (pDevice->IgnoreTbiLinkChange == TRUE)
    {
        ServicePhyInt = FALSE;
    }
#endif
    if (ServicePhyInt == TRUE)
    {
        MM_ACQUIRE_PHY_LOCK_IN_IRQ(pDevice);
        LM_SetupPhy(pDevice);
        MM_RELEASE_PHY_LOCK_IN_IRQ(pDevice);
    }

    /* Service receive and transmit interrupts. */
    LM_ServiceRxInterrupt(pDevice);
    LM_ServiceTxInterrupt(pDevice);
        
    /* No spinlock for this queue since this routine is serialized. */
    if(!QQ_Empty(&pDevice->RxPacketReceivedQ.Container))
    {
        /* Indicate receive packets. */
        MM_IndicateRxPackets(pDevice);
//        LM_QueueRxPackets(pDevice);
    }

    /* No spinlock for this queue since this routine is serialized. */
    if(!QQ_Empty(&pDevice->TxPacketXmittedQ.Container))
    {
        MM_IndicateTxPackets(pDevice);
    }

    return LM_STATUS_SUCCESS;
} /* LM_ServiceInterrupts */



/******************************************************************************/
/* Description:                                                               */
/*                                                                            */
/* Return:                                                                    */
/******************************************************************************/
LM_STATUS
LM_MulticastAdd(
PLM_DEVICE_BLOCK pDevice,
PLM_UINT8 pMcAddress) {
    PLM_UINT8 pEntry;
    LM_UINT32 j;

    pEntry = pDevice->McTable[0];
    for(j = 0; j < pDevice->McEntryCount; j++)
    {
        if(IS_ETH_ADDRESS_EQUAL(pEntry, pMcAddress))
        {
            /* Found a match, increment the instance count. */
            pEntry[LM_MC_INSTANCE_COUNT_INDEX] += 1;

            return LM_STATUS_SUCCESS;
        }

        pEntry += LM_MC_ENTRY_SIZE;
    }
    
    if(pDevice->McEntryCount >= LM_MAX_MC_TABLE_SIZE)
    {
        return LM_STATUS_FAILURE;
    }

    pEntry = pDevice->McTable[pDevice->McEntryCount];

    COPY_ETH_ADDRESS(pMcAddress, pEntry);
    pEntry[LM_MC_INSTANCE_COUNT_INDEX] = 1;

    pDevice->McEntryCount++;

    LM_SetReceiveMask(pDevice, pDevice->ReceiveMask | LM_ACCEPT_MULTICAST);

    return LM_STATUS_SUCCESS;
} /* LM_MulticastAdd */



/******************************************************************************/
/* Description:                                                               */
/*                                                                            */
/* Return:                                                                    */
/******************************************************************************/
LM_STATUS
LM_MulticastDel(
PLM_DEVICE_BLOCK pDevice,
PLM_UINT8 pMcAddress) {
    PLM_UINT8 pEntry;
    LM_UINT32 j;

    pEntry = pDevice->McTable[0];
    for(j = 0; j < pDevice->McEntryCount; j++)
    {
        if(IS_ETH_ADDRESS_EQUAL(pEntry, pMcAddress))
        {
            /* Found a match, decrement the instance count. */
            pEntry[LM_MC_INSTANCE_COUNT_INDEX] -= 1;

            /* No more instance left, remove the address from the table. */
            /* Move the last entry in the table to the delete slot. */
            if(pEntry[LM_MC_INSTANCE_COUNT_INDEX] == 0 &&
                pDevice->McEntryCount > 1)
            {

                COPY_ETH_ADDRESS(
                    pDevice->McTable[pDevice->McEntryCount-1], pEntry);
                pEntry[LM_MC_INSTANCE_COUNT_INDEX] =
                    pDevice->McTable[pDevice->McEntryCount-1]
                    [LM_MC_INSTANCE_COUNT_INDEX];
            }
            pDevice->McEntryCount--;

            /* Update the receive mask if the table is empty. */
            if(pDevice->McEntryCount == 0)
            {
                LM_SetReceiveMask(pDevice, 
                    pDevice->ReceiveMask & ~LM_ACCEPT_MULTICAST);
            }

            return LM_STATUS_SUCCESS;
        }

        pEntry += LM_MC_ENTRY_SIZE;
    }

    return LM_STATUS_FAILURE;
} /* LM_MulticastDel */



/******************************************************************************/
/* Description:                                                               */
/*                                                                            */
/* Return:                                                                    */
/******************************************************************************/
LM_STATUS
LM_MulticastClear(
PLM_DEVICE_BLOCK pDevice) {
    pDevice->McEntryCount = 0;

    LM_SetReceiveMask(pDevice, pDevice->ReceiveMask & ~LM_ACCEPT_MULTICAST);

    return LM_STATUS_SUCCESS;
} /* LM_MulticastClear */



/******************************************************************************/
/* Description:                                                               */
/*                                                                            */
/* Return:                                                                    */
/******************************************************************************/
LM_STATUS
LM_SetMacAddress(
    PLM_DEVICE_BLOCK pDevice,
    PLM_UINT8 pMacAddress)
{
    LM_UINT32 j;

    for(j = 0; j < 4; j++)
    {
        REG_WR(pDevice, MacCtrl.MacAddr[j].High,
            (pMacAddress[0] << 8) | pMacAddress[1]);
        REG_WR(pDevice, MacCtrl.MacAddr[j].Low,
            (pMacAddress[2] << 24) | (pMacAddress[3] << 16) |
            (pMacAddress[4] << 8) | pMacAddress[5]);
    }

    return LM_STATUS_SUCCESS;
}


/******************************************************************************/
/* Description:                                                               */
/*                                                                            */
/* Return:                                                                    */
/*    LM_STATUS_LINK_ACTIVE                                                   */
/*    LM_STATUS_LINK_DOWN                                                     */
/******************************************************************************/
static LM_STATUS
LM_InitBcm540xPhy(
PLM_DEVICE_BLOCK pDevice)
{
    LM_LINE_SPEED CurrentLineSpeed;
    LM_DUPLEX_MODE CurrentDuplexMode;
    LM_STATUS CurrentLinkStatus;
    LM_UINT32 Value32;
    LM_UINT32 j;

    LM_WritePhy(pDevice, BCM5401_AUX_CTRL, 0x02);

    if (pDevice->ResetPhyOnLinkDown)
    {
        LM_ReadPhy(pDevice, PHY_STATUS_REG, &Value32);
        LM_ReadPhy(pDevice, PHY_STATUS_REG, &Value32);
        if(!(Value32 & PHY_STATUS_LINK_PASS))
        {
            LM_ResetPhy(pDevice);
        }
    }
    if((pDevice->PhyId & PHY_ID_MASK) == PHY_BCM5401_PHY_ID)
    {
        LM_ReadPhy(pDevice, PHY_STATUS_REG, &Value32);
        LM_ReadPhy(pDevice, PHY_STATUS_REG, &Value32);

        if(!pDevice->InitDone)
        {
            Value32 = 0;
        }

        if(!(Value32 & PHY_STATUS_LINK_PASS))
        {
            LM_WritePhy(pDevice, BCM5401_AUX_CTRL,  0x0c20);

            LM_WritePhy(pDevice, BCM540X_DSP_ADDRESS_REG, 0x0012);
            LM_WritePhy(pDevice, BCM540X_DSP_RW_PORT, 0x1804);
                
            LM_WritePhy(pDevice, BCM540X_DSP_ADDRESS_REG, 0x0013);
            LM_WritePhy(pDevice, BCM540X_DSP_RW_PORT, 0x1204);
                
            LM_WritePhy(pDevice, BCM540X_DSP_ADDRESS_REG, 0x8006);
            LM_WritePhy(pDevice, BCM540X_DSP_RW_PORT, 0x0132);

            LM_WritePhy(pDevice, BCM540X_DSP_ADDRESS_REG, 0x8006);
            LM_WritePhy(pDevice, BCM540X_DSP_RW_PORT, 0x0232);
                
            LM_WritePhy(pDevice, BCM540X_DSP_ADDRESS_REG, 0x201f);
            LM_WritePhy(pDevice, BCM540X_DSP_RW_PORT, 0x0a20);

            LM_ReadPhy(pDevice, PHY_STATUS_REG, &Value32);
            for(j = 0; j < 1000; j++)
            {
                MM_Wait(10);

                LM_ReadPhy(pDevice, PHY_STATUS_REG, &Value32);
                if(Value32 & PHY_STATUS_LINK_PASS)
                {
                    MM_Wait(40);
                    break;
                }
            }

            if((pDevice->PhyId & PHY_ID_REV_MASK) == PHY_BCM5401_B0_REV)
            {
                if(!(Value32 & PHY_STATUS_LINK_PASS) &&
                    (pDevice->OldLineSpeed == LM_LINE_SPEED_1000MBPS))
                {
                    LM_WritePhy(pDevice, PHY_CTRL_REG, PHY_CTRL_PHY_RESET);
                    for(j = 0; j < 100; j++)
                    {
                        MM_Wait(10);

                        LM_ReadPhy(pDevice, PHY_CTRL_REG, &Value32);
                        if(!(Value32 & PHY_CTRL_PHY_RESET))
                        {
                            MM_Wait(40);
                            break;
                        }
                    }

                    LM_WritePhy(pDevice, BCM5401_AUX_CTRL,  0x0c20);

                    LM_WritePhy(pDevice, BCM540X_DSP_ADDRESS_REG, 0x0012);
                    LM_WritePhy(pDevice, BCM540X_DSP_RW_PORT, 0x1804);
                        
                    LM_WritePhy(pDevice, BCM540X_DSP_ADDRESS_REG, 0x0013);
                    LM_WritePhy(pDevice, BCM540X_DSP_RW_PORT, 0x1204);
                        
                    LM_WritePhy(pDevice, BCM540X_DSP_ADDRESS_REG, 0x8006);
                    LM_WritePhy(pDevice, BCM540X_DSP_RW_PORT, 0x0132);

                    LM_WritePhy(pDevice, BCM540X_DSP_ADDRESS_REG, 0x8006);
                    LM_WritePhy(pDevice, BCM540X_DSP_RW_PORT, 0x0232);
                        
                    LM_WritePhy(pDevice, BCM540X_DSP_ADDRESS_REG, 0x201f);
                    LM_WritePhy(pDevice, BCM540X_DSP_RW_PORT, 0x0a20);
                }
            }
        }
    }
    else if(pDevice->ChipRevId == T3_CHIP_ID_5701_A0 ||
        pDevice->ChipRevId == T3_CHIP_ID_5701_B0)
    {
        /* Bug: 5701 A0, B0 TX CRC workaround. */
        LM_WritePhy(pDevice, 0x15, 0x0a75);
        LM_WritePhy(pDevice, 0x1c, 0x8c68);
        LM_WritePhy(pDevice, 0x1c, 0x8d68);
        LM_WritePhy(pDevice, 0x1c, 0x8c68);
    }

    /* Acknowledge interrupts. */
    LM_ReadPhy(pDevice, BCM540X_INT_STATUS_REG, &Value32);
    LM_ReadPhy(pDevice, BCM540X_INT_STATUS_REG, &Value32);

    /* Configure the interrupt mask. */
    if(pDevice->PhyIntMode == T3_PHY_INT_MODE_MI_INTERRUPT)
    {
        LM_WritePhy(pDevice, BCM540X_INT_MASK_REG, ~BCM540X_INT_LINK_CHANGE);
    }

    /* Configure PHY led mode. */
    if(T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5701 ||
        (T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5700))
    {
        if(pDevice->LedMode == LED_MODE_THREE_LINK)
        {
            LM_WritePhy(pDevice, BCM540X_EXT_CTRL_REG, 
                BCM540X_EXT_CTRL_LINK3_LED_MODE);
        }
        else
        {
            LM_WritePhy(pDevice, BCM540X_EXT_CTRL_REG, 0);
        }
    }

    CurrentLinkStatus = LM_STATUS_LINK_DOWN;

    /* Get current link and duplex mode. */
    for(j = 0; j < 100; j++)
    {
        LM_ReadPhy(pDevice, PHY_STATUS_REG, &Value32);
        LM_ReadPhy(pDevice, PHY_STATUS_REG, &Value32);

        if(Value32 & PHY_STATUS_LINK_PASS)
        {
            break;
        }
        MM_Wait(40);
    }

    if(Value32 & PHY_STATUS_LINK_PASS)
    {

        /* Determine the current line and duplex settings. */
        LM_ReadPhy(pDevice, BCM540X_AUX_STATUS_REG, &Value32);
        for(j = 0; j < 2000; j++)
        {
            MM_Wait(10);

            LM_ReadPhy(pDevice, BCM540X_AUX_STATUS_REG, &Value32);
            if(Value32)
            {
                break;
            }
        }

        switch(Value32 & BCM540X_AUX_SPEED_MASK)
        {
            case BCM540X_AUX_10BASET_HD:
                CurrentLineSpeed = LM_LINE_SPEED_10MBPS;
                CurrentDuplexMode = LM_DUPLEX_MODE_HALF;
                break;

            case BCM540X_AUX_10BASET_FD:
                CurrentLineSpeed = LM_LINE_SPEED_10MBPS;
                CurrentDuplexMode = LM_DUPLEX_MODE_FULL;
                break;

            case BCM540X_AUX_100BASETX_HD:
                CurrentLineSpeed = LM_LINE_SPEED_100MBPS;
                CurrentDuplexMode = LM_DUPLEX_MODE_HALF;
                break;

            case BCM540X_AUX_100BASETX_FD:
                CurrentLineSpeed = LM_LINE_SPEED_100MBPS;
                CurrentDuplexMode = LM_DUPLEX_MODE_FULL;
                break;

            case BCM540X_AUX_100BASET_HD:
                CurrentLineSpeed = LM_LINE_SPEED_1000MBPS;
                CurrentDuplexMode = LM_DUPLEX_MODE_HALF;
                break;

            case BCM540X_AUX_100BASET_FD:
                CurrentLineSpeed = LM_LINE_SPEED_1000MBPS;
                CurrentDuplexMode = LM_DUPLEX_MODE_FULL;
                break;

            default:

                CurrentLineSpeed = LM_LINE_SPEED_UNKNOWN;
                CurrentDuplexMode = LM_DUPLEX_MODE_UNKNOWN;
                break;
        }

        /* Make sure we are in auto-neg mode. */
        for (j = 0; j < 200; j++)
        {
            LM_ReadPhy(pDevice, PHY_CTRL_REG, &Value32);
            if(Value32 && Value32 != 0x7fff)
            {
                break;
            }

            if(Value32 == 0 &&
                pDevice->RequestedLineSpeed == LM_LINE_SPEED_10MBPS &&
                pDevice->RequestedDuplexMode == LM_DUPLEX_MODE_HALF)
            {
                break;
            }

            MM_Wait(10);
        }

        /* Use the current line settings for "auto" mode. */
        if(pDevice->RequestedLineSpeed == LM_LINE_SPEED_AUTO)
        {
            if(Value32 & PHY_CTRL_AUTO_NEG_ENABLE)
            {
                CurrentLinkStatus = LM_STATUS_LINK_ACTIVE;

                /* We may be exiting low power mode and the link is in */
                /* 10mb.  In this case, we need to restart autoneg. */

                if (LM_PhyAdvertiseAll(pDevice) != LM_STATUS_SUCCESS)
                {
                    CurrentLinkStatus = LM_STATUS_LINK_SETTING_MISMATCH;
                }
            }
            else
            {
                CurrentLinkStatus = LM_STATUS_LINK_SETTING_MISMATCH;
            }
        }
        else
        {
            /* Force line settings. */
            /* Use the current setting if it matches the user's requested */
            /* setting. */
            LM_ReadPhy(pDevice, PHY_CTRL_REG, &Value32);
            if((pDevice->LineSpeed == CurrentLineSpeed) &&
                (pDevice->DuplexMode == CurrentDuplexMode))
            {
                if ((pDevice->DisableAutoNeg &&
                    !(Value32 & PHY_CTRL_AUTO_NEG_ENABLE)) ||
                    (!pDevice->DisableAutoNeg &&
                    (Value32 & PHY_CTRL_AUTO_NEG_ENABLE)))
                {
                    CurrentLinkStatus = LM_STATUS_LINK_ACTIVE;
                }
                else
                {
                    CurrentLinkStatus = LM_STATUS_LINK_SETTING_MISMATCH;
                } 
            }
            else
            {
                CurrentLinkStatus = LM_STATUS_LINK_SETTING_MISMATCH;
            } 
        }

        /* Save line settings. */
        pDevice->LineSpeed = CurrentLineSpeed;
        pDevice->DuplexMode = CurrentDuplexMode;
    }

    return CurrentLinkStatus;
} /* LM_InitBcm540xPhy */

/******************************************************************************/
/* Description:                                                               */
/*                                                                            */
/* Return:                                                                    */
/******************************************************************************/
LM_STATUS
LM_SetFlowControl(
    PLM_DEVICE_BLOCK pDevice,
    LM_UINT32 LocalPhyAd,
    LM_UINT32 RemotePhyAd)
{
    LM_FLOW_CONTROL FlowCap;

    /* Resolve flow control. */
    FlowCap = LM_FLOW_CONTROL_NONE;

    /* See Table 28B-3 of 802.3ab-1999 spec. */
    if(pDevice->FlowControlCap & LM_FLOW_CONTROL_AUTO_PAUSE)
    {
        if(LocalPhyAd & PHY_AN_AD_PAUSE_CAPABLE)
        {
            if(LocalPhyAd & PHY_AN_AD_ASYM_PAUSE)
            {
                if(RemotePhyAd & PHY_LINK_PARTNER_PAUSE_CAPABLE)
                {
                    FlowCap = LM_FLOW_CONTROL_TRANSMIT_PAUSE |
                        LM_FLOW_CONTROL_RECEIVE_PAUSE;
                }
                else if(RemotePhyAd & PHY_LINK_PARTNER_ASYM_PAUSE)
                {
                    FlowCap = LM_FLOW_CONTROL_RECEIVE_PAUSE;
                }
            }
            else
            {
                if(RemotePhyAd & PHY_LINK_PARTNER_PAUSE_CAPABLE)
                {
                    FlowCap = LM_FLOW_CONTROL_TRANSMIT_PAUSE |
                        LM_FLOW_CONTROL_RECEIVE_PAUSE;
                }
            }
        }
        else if(LocalPhyAd & PHY_AN_AD_ASYM_PAUSE)
        {
            if((RemotePhyAd & PHY_LINK_PARTNER_PAUSE_CAPABLE) &&
                (RemotePhyAd & PHY_LINK_PARTNER_ASYM_PAUSE))
            {
                FlowCap = LM_FLOW_CONTROL_TRANSMIT_PAUSE;
            }
        }
    }
    else
    {
        FlowCap = pDevice->FlowControlCap;
    }

    /* Enable/disable rx PAUSE. */
    pDevice->RxMode &= ~RX_MODE_ENABLE_FLOW_CONTROL;
    if(FlowCap & LM_FLOW_CONTROL_RECEIVE_PAUSE &&
        (pDevice->FlowControlCap == LM_FLOW_CONTROL_AUTO_PAUSE ||
        pDevice->FlowControlCap & LM_FLOW_CONTROL_RECEIVE_PAUSE))
    {
        pDevice->FlowControl |= LM_FLOW_CONTROL_RECEIVE_PAUSE;
        pDevice->RxMode |= RX_MODE_ENABLE_FLOW_CONTROL;

    }
    REG_WR(pDevice, MacCtrl.RxMode, pDevice->RxMode);

    /* Enable/disable tx PAUSE. */
    pDevice->TxMode &= ~TX_MODE_ENABLE_FLOW_CONTROL;
    if(FlowCap & LM_FLOW_CONTROL_TRANSMIT_PAUSE &&
        (pDevice->FlowControlCap == LM_FLOW_CONTROL_AUTO_PAUSE ||
        pDevice->FlowControlCap & LM_FLOW_CONTROL_TRANSMIT_PAUSE))
    {
        pDevice->FlowControl |= LM_FLOW_CONTROL_TRANSMIT_PAUSE;
        pDevice->TxMode |= TX_MODE_ENABLE_FLOW_CONTROL;

    }
    REG_WR(pDevice, MacCtrl.TxMode, pDevice->TxMode);

    return LM_STATUS_SUCCESS;
}


#if INCLUDE_TBI_SUPPORT
/******************************************************************************/
/* Description:                                                               */
/*                                                                            */
/* Return:                                                                    */
/******************************************************************************/
STATIC LM_STATUS
LM_InitBcm800xPhy(
    PLM_DEVICE_BLOCK pDevice)
{
    LM_UINT32 Value32;
    LM_UINT32 j;

    Value32 = REG_RD(pDevice, MacCtrl.Status);

    /* Reset the SERDES during init and when we have link. */
    if(!pDevice->InitDone || Value32 & MAC_STATUS_PCS_SYNCED)
    {
        /* Set PLL lock range. */
        LM_WritePhy(pDevice, 0x16, 0x8007);

        /* Software reset. */
        LM_WritePhy(pDevice, 0x00, 0x8000);

        /* Wait for reset to complete. */
        for(j = 0; j < 500; j++)
        {
            MM_Wait(10);
        }

        /* Config mode; seletct PMA/Ch 1 regs. */
        LM_WritePhy(pDevice, 0x10, 0x8411);

        /* Enable auto-lock and comdet, select txclk for tx. */
        LM_WritePhy(pDevice, 0x11, 0x0a10);

        LM_WritePhy(pDevice, 0x18, 0x00a0);
        LM_WritePhy(pDevice, 0x16, 0x41ff);

        /* Assert and deassert POR. */
        LM_WritePhy(pDevice, 0x13, 0x0400);
        MM_Wait(40);
        LM_WritePhy(pDevice, 0x13, 0x0000);

        LM_WritePhy(pDevice, 0x11, 0x0a50);
        MM_Wait(40);
        LM_WritePhy(pDevice, 0x11, 0x0a10);

        /* Delay for signal to stabilize. */
        for(j = 0; j < 15000; j++)
        {
            MM_Wait(10);
        }

        /* Deselect the channel register so we can read the PHY id later. */
        LM_WritePhy(pDevice, 0x10, 0x8011);
    }

    return LM_STATUS_SUCCESS;
}



/******************************************************************************/
/* Description:                                                               */
/*                                                                            */
/* Return:                                                                    */
/******************************************************************************/
STATIC LM_STATUS
LM_SetupFiberPhy(
    PLM_DEVICE_BLOCK pDevice)
{
    LM_STATUS CurrentLinkStatus;
    AUTONEG_STATUS AnStatus = 0;
    LM_UINT32 Value32;
    LM_UINT32 Cnt;
    LM_UINT32 j, k;

    pDevice->MacMode &= ~(MAC_MODE_HALF_DUPLEX | MAC_MODE_PORT_MODE_MASK);

    /* Initialize the send_config register. */
    REG_WR(pDevice, MacCtrl.TxAutoNeg, 0);

    /* Enable TBI and full duplex mode. */
    pDevice->MacMode |= MAC_MODE_PORT_MODE_TBI;
    REG_WR(pDevice, MacCtrl.Mode, pDevice->MacMode);

    /* Initialize the BCM8002 SERDES PHY. */
    switch(pDevice->PhyId & PHY_ID_MASK)
    {
        case PHY_BCM8002_PHY_ID:
            LM_InitBcm800xPhy(pDevice);
            break;

        default:
            break;
    }

    /* Enable link change interrupt. */
    REG_WR(pDevice, MacCtrl.MacEvent, MAC_EVENT_ENABLE_LINK_STATE_CHANGED_ATTN);

    /* Default to link down. */
    CurrentLinkStatus = LM_STATUS_LINK_DOWN;

    /* Get the link status. */
    Value32 = REG_RD(pDevice, MacCtrl.Status);
    if(Value32 & MAC_STATUS_PCS_SYNCED)
    {
        if((pDevice->RequestedLineSpeed == LM_LINE_SPEED_AUTO) ||
            (pDevice->DisableAutoNeg == FALSE))
        {
            /* auto-negotiation mode. */
            /* Initialize the autoneg default capaiblities. */
            AutonegInit(&pDevice->AnInfo);

            /* Set the context pointer to point to the main device structure. */
            pDevice->AnInfo.pContext = pDevice;

            /* Setup flow control advertisement register. */
            Value32 = GetPhyAdFlowCntrlSettings(pDevice);
            if(Value32 & PHY_AN_AD_PAUSE_CAPABLE)
            {
                pDevice->AnInfo.mr_adv_sym_pause = 1;
            }
            else
            {
                pDevice->AnInfo.mr_adv_sym_pause = 0;
            }

            if(Value32 & PHY_AN_AD_ASYM_PAUSE)
            {
                pDevice->AnInfo.mr_adv_asym_pause = 1;
            }
            else
            {
                pDevice->AnInfo.mr_adv_asym_pause = 0;
            }

            /* Try to autoneg up to six times. */
            if (pDevice->IgnoreTbiLinkChange)
            {
                Cnt = 1;
            }
            else
            {
                Cnt = 6;
            }
            for (j = 0; j < Cnt; j++)
            {
                REG_WR(pDevice, MacCtrl.TxAutoNeg, 0);

                Value32 = pDevice->MacMode & ~MAC_MODE_PORT_MODE_MASK;
                REG_WR(pDevice, MacCtrl.Mode, Value32);
                MM_Wait(20);

                REG_WR(pDevice, MacCtrl.Mode, pDevice->MacMode |
                    MAC_MODE_SEND_CONFIGS);

                MM_Wait(20);

                pDevice->AnInfo.State = AN_STATE_UNKNOWN;
                pDevice->AnInfo.CurrentTime_us = 0;

                REG_WR(pDevice, Grc.Timer, 0);
                for(k = 0; (pDevice->AnInfo.CurrentTime_us < 75000) &&
                    (k < 75000); k++)
                {
                    AnStatus = Autoneg8023z(&pDevice->AnInfo);

                    if((AnStatus == AUTONEG_STATUS_DONE) || 
                        (AnStatus == AUTONEG_STATUS_FAILED))
                    {
                        break;
                    }

                    pDevice->AnInfo.CurrentTime_us = REG_RD(pDevice, Grc.Timer);
                
                }
                if((AnStatus == AUTONEG_STATUS_DONE) || 
                    (AnStatus == AUTONEG_STATUS_FAILED))
                {
                    break;
                }
                if (j >= 1)
                {
                    if (!(REG_RD(pDevice, MacCtrl.Status) &
                        MAC_STATUS_PCS_SYNCED)) {
                        break;
                    }
                }
            }

            /* Stop sending configs. */
            MM_AnTxIdle(&pDevice->AnInfo);

            /* Resolve flow control settings. */
            if((AnStatus == AUTONEG_STATUS_DONE) &&
                pDevice->AnInfo.mr_an_complete && pDevice->AnInfo.mr_link_ok &&
                pDevice->AnInfo.mr_lp_adv_full_duplex)
                {
                LM_UINT32 RemotePhyAd;
                LM_UINT32 LocalPhyAd;

                LocalPhyAd = 0;
                if(pDevice->AnInfo.mr_adv_sym_pause)
                {
                    LocalPhyAd |= PHY_AN_AD_PAUSE_CAPABLE;
                }

                if(pDevice->AnInfo.mr_adv_asym_pause)
                {
                    LocalPhyAd |= PHY_AN_AD_ASYM_PAUSE;
                }

                RemotePhyAd = 0;
                if(pDevice->AnInfo.mr_lp_adv_sym_pause)
                {
                    RemotePhyAd |= PHY_LINK_PARTNER_PAUSE_CAPABLE;
                }

                if(pDevice->AnInfo.mr_lp_adv_asym_pause)
                {
                    RemotePhyAd |= PHY_LINK_PARTNER_ASYM_PAUSE;
                }

                LM_SetFlowControl(pDevice, LocalPhyAd, RemotePhyAd);

                CurrentLinkStatus = LM_STATUS_LINK_ACTIVE;
            }
            else
            {
                LM_SetFlowControl(pDevice, 0, 0);
            }
            for (j = 0; j < 30; j++)
            {
                MM_Wait(20);
                REG_WR(pDevice, MacCtrl.Status, MAC_STATUS_SYNC_CHANGED |
                    MAC_STATUS_CFG_CHANGED);
                MM_Wait(20);
                if ((REG_RD(pDevice, MacCtrl.Status) &
                    (MAC_STATUS_SYNC_CHANGED | MAC_STATUS_CFG_CHANGED)) == 0)
                    break;
            }
            if (pDevice->PollTbiLink)
            {
                Value32 = REG_RD(pDevice, MacCtrl.Status);
                if (Value32 & MAC_STATUS_RECEIVING_CFG)
                {
                    pDevice->IgnoreTbiLinkChange = TRUE;
                }
                else
                {
                    pDevice->IgnoreTbiLinkChange = FALSE;
                }
            }
            Value32 = REG_RD(pDevice, MacCtrl.Status);
            if (CurrentLinkStatus == LM_STATUS_LINK_DOWN &&
                 (Value32 & MAC_STATUS_PCS_SYNCED) &&
                 ((Value32 & MAC_STATUS_RECEIVING_CFG) == 0))
            {
                CurrentLinkStatus = LM_STATUS_LINK_ACTIVE;
            }
        }
        else
        {
            /* We are forcing line speed. */
            pDevice->FlowControlCap &= ~LM_FLOW_CONTROL_AUTO_PAUSE;
            LM_SetFlowControl(pDevice, 0, 0);

            CurrentLinkStatus = LM_STATUS_LINK_ACTIVE;
            REG_WR(pDevice, MacCtrl.Mode, pDevice->MacMode |
                MAC_MODE_SEND_CONFIGS);
        }
    }
    /* Set the link polarity bit. */
    pDevice->MacMode &= ~MAC_MODE_LINK_POLARITY;
    REG_WR(pDevice, MacCtrl.Mode, pDevice->MacMode);

    pDevice->pStatusBlkVirt->Status = STATUS_BLOCK_UPDATED |
        (pDevice->pStatusBlkVirt->Status & ~STATUS_BLOCK_LINK_CHANGED_STATUS);

    for (j = 0; j < 100; j++)
    {
        REG_WR(pDevice, MacCtrl.Status, MAC_STATUS_SYNC_CHANGED |
            MAC_STATUS_CFG_CHANGED);
        MM_Wait(5);
        if ((REG_RD(pDevice, MacCtrl.Status) &
            (MAC_STATUS_SYNC_CHANGED | MAC_STATUS_CFG_CHANGED)) == 0)
            break;
    }

    Value32 = REG_RD(pDevice, MacCtrl.Status);
    if((Value32 & MAC_STATUS_PCS_SYNCED) == 0)
    {
        CurrentLinkStatus = LM_STATUS_LINK_DOWN;
        if (pDevice->DisableAutoNeg == FALSE)
        {
            REG_WR(pDevice, MacCtrl.Mode, pDevice->MacMode |
                MAC_MODE_SEND_CONFIGS);
            MM_Wait(1);
            REG_WR(pDevice, MacCtrl.Mode, pDevice->MacMode);
        }
    }

    /* Initialize the current link status. */
    if(CurrentLinkStatus == LM_STATUS_LINK_ACTIVE)
    {
        pDevice->LineSpeed = LM_LINE_SPEED_1000MBPS;
        pDevice->DuplexMode = LM_DUPLEX_MODE_FULL;
        REG_WR(pDevice, MacCtrl.LedCtrl, LED_CTRL_OVERRIDE_LINK_LED |
            LED_CTRL_1000MBPS_LED_ON);
    }
    else
    {
        pDevice->LineSpeed = LM_LINE_SPEED_UNKNOWN;
        pDevice->DuplexMode = LM_DUPLEX_MODE_UNKNOWN;
        REG_WR(pDevice, MacCtrl.LedCtrl, LED_CTRL_OVERRIDE_LINK_LED |
            LED_CTRL_OVERRIDE_TRAFFIC_LED);
    }

    /* Indicate link status. */
    if (pDevice->LinkStatus != CurrentLinkStatus) {
        pDevice->LinkStatus = CurrentLinkStatus;
        MM_IndicateStatus(pDevice, CurrentLinkStatus);
    }

    return LM_STATUS_SUCCESS;
}
#endif /* INCLUDE_TBI_SUPPORT */


/******************************************************************************/
/* Description:                                                               */
/*                                                                            */
/* Return:                                                                    */
/******************************************************************************/
LM_STATUS
LM_SetupCopperPhy(
    PLM_DEVICE_BLOCK pDevice)
{
    LM_STATUS CurrentLinkStatus;
    LM_UINT32 Value32;

    /* Assume there is not link first. */
    CurrentLinkStatus = LM_STATUS_LINK_DOWN;

    /* Disable phy link change attention. */
    REG_WR(pDevice, MacCtrl.MacEvent, 0);

    /* Clear link change attention. */
    REG_WR(pDevice, MacCtrl.Status, MAC_STATUS_SYNC_CHANGED |
        MAC_STATUS_CFG_CHANGED | MAC_STATUS_MI_COMPLETION |
        MAC_STATUS_LINK_STATE_CHANGED);

    /* Disable auto-polling for the moment. */
    pDevice->MiMode = 0xc0000;
    REG_WR(pDevice, MacCtrl.MiMode, pDevice->MiMode);
    MM_Wait(40);

    /* Determine the requested line speed and duplex. */
    pDevice->OldLineSpeed = pDevice->LineSpeed;
    pDevice->LineSpeed = pDevice->RequestedLineSpeed;
    pDevice->DuplexMode = pDevice->RequestedDuplexMode;

    /* Initialize the phy chip. */
    CurrentLinkStatus = LM_InitBcm540xPhy(pDevice);

    if(CurrentLinkStatus == LM_STATUS_LINK_SETTING_MISMATCH)
    {
        CurrentLinkStatus = LM_STATUS_LINK_DOWN;
    }
    
    /* Setup flow control. */
    pDevice->FlowControl = LM_FLOW_CONTROL_NONE;
    if(CurrentLinkStatus == LM_STATUS_LINK_ACTIVE)
    {
        LM_FLOW_CONTROL FlowCap;     /* Flow control capability. */

        FlowCap = LM_FLOW_CONTROL_NONE;

        if(pDevice->DuplexMode == LM_DUPLEX_MODE_FULL)
        {
            if(pDevice->DisableAutoNeg == FALSE ||
                pDevice->RequestedLineSpeed == LM_LINE_SPEED_AUTO)
            {
                LM_UINT32 ExpectedPhyAd;
                LM_UINT32 LocalPhyAd;
                LM_UINT32 RemotePhyAd;

                LM_ReadPhy(pDevice, PHY_AN_AD_REG, &LocalPhyAd);
		pDevice->advertising = LocalPhyAd;
                LocalPhyAd &= (PHY_AN_AD_ASYM_PAUSE | PHY_AN_AD_PAUSE_CAPABLE);

                ExpectedPhyAd = GetPhyAdFlowCntrlSettings(pDevice);

                if(LocalPhyAd != ExpectedPhyAd)
                {
                    CurrentLinkStatus = LM_STATUS_LINK_DOWN;
                }
                else
                {
                    LM_ReadPhy(pDevice, PHY_LINK_PARTNER_ABILITY_REG,
                        &RemotePhyAd);

                    LM_SetFlowControl(pDevice, LocalPhyAd, RemotePhyAd);
                }
            }
            else
            {
                pDevice->FlowControlCap &= ~LM_FLOW_CONTROL_AUTO_PAUSE;
                LM_SetFlowControl(pDevice, 0, 0);
            }
        }
    }

    if(CurrentLinkStatus == LM_STATUS_LINK_DOWN)
    {
        LM_ForceAutoNeg(pDevice);

        /* If we force line speed, we make get link right away. */
        LM_ReadPhy(pDevice, PHY_STATUS_REG, &Value32);
        LM_ReadPhy(pDevice, PHY_STATUS_REG, &Value32);
        if(Value32 & PHY_STATUS_LINK_PASS)
        {
            CurrentLinkStatus = LM_STATUS_LINK_ACTIVE;
        }
    }

    /* GMII interface. */
    pDevice->MacMode &= ~MAC_MODE_PORT_MODE_MASK;
    if(CurrentLinkStatus == LM_STATUS_LINK_ACTIVE)
    {
        if(pDevice->LineSpeed == LM_LINE_SPEED_100MBPS ||
            pDevice->LineSpeed == LM_LINE_SPEED_10MBPS)
        {
            pDevice->MacMode |= MAC_MODE_PORT_MODE_MII;
        }
        else
        {
            pDevice->MacMode |= MAC_MODE_PORT_MODE_GMII;
        }
    }
    else {
        pDevice->MacMode |= MAC_MODE_PORT_MODE_GMII;
    }

    /* Set the MAC to operate in the appropriate duplex mode. */
    pDevice->MacMode &= ~MAC_MODE_HALF_DUPLEX;
    if(pDevice->DuplexMode == LM_DUPLEX_MODE_HALF)
    {
        pDevice->MacMode |= MAC_MODE_HALF_DUPLEX;
    }

    /* Set the link polarity bit. */
    pDevice->MacMode &= ~MAC_MODE_LINK_POLARITY;
    if(T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5700)
    {
        if((pDevice->LedMode == LED_MODE_LINK10) ||
             (CurrentLinkStatus == LM_STATUS_LINK_ACTIVE &&
             pDevice->LineSpeed == LM_LINE_SPEED_10MBPS))
        {
            pDevice->MacMode |= MAC_MODE_LINK_POLARITY;
        }
    }
    else
    {
        if (CurrentLinkStatus == LM_STATUS_LINK_ACTIVE)
        {
            pDevice->MacMode |= MAC_MODE_LINK_POLARITY;
        }

        /* Set LED mode. */
        if(T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5700 ||
            T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5701)
        {
            Value32 = LED_CTRL_PHY_MODE_1;
        }
        else
        {
            if(pDevice->LedMode == LED_MODE_OUTPUT)
            {
                Value32 = LED_CTRL_PHY_MODE_2;
            }
            else
            {
                Value32 = LED_CTRL_PHY_MODE_1;
            }
        }
        REG_WR(pDevice, MacCtrl.LedCtrl, Value32);
    }

    REG_WR(pDevice, MacCtrl.Mode, pDevice->MacMode);

    /* Enable auto polling. */
    if(pDevice->PhyIntMode == T3_PHY_INT_MODE_AUTO_POLLING)
    {
        pDevice->MiMode |= MI_MODE_AUTO_POLLING_ENABLE;
        REG_WR(pDevice, MacCtrl.MiMode, pDevice->MiMode);
    }

    /* Enable phy link change attention. */
    if(pDevice->PhyIntMode == T3_PHY_INT_MODE_MI_INTERRUPT)
    {
        REG_WR(pDevice, MacCtrl.MacEvent, MAC_EVENT_ENABLE_MI_INTERRUPT);
    }
    else
    {
        REG_WR(pDevice, MacCtrl.MacEvent, 
            MAC_EVENT_ENABLE_LINK_STATE_CHANGED_ATTN);
    }
    if ((T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5700) &&
        (CurrentLinkStatus == LM_STATUS_LINK_ACTIVE) &&
        (pDevice->LineSpeed == LM_LINE_SPEED_1000MBPS) &&
        (((pDevice->PciState & T3_PCI_STATE_CONVENTIONAL_PCI_MODE) &&
          (pDevice->PciState & T3_PCI_STATE_BUS_SPEED_HIGH)) ||
         !(pDevice->PciState & T3_PCI_STATE_CONVENTIONAL_PCI_MODE)))
    {
        MM_Wait(120);
        REG_WR(pDevice, MacCtrl.Status, MAC_STATUS_SYNC_CHANGED |
            MAC_STATUS_CFG_CHANGED);
        MEM_WR_OFFSET(pDevice, T3_FIRMWARE_MAILBOX,
            T3_MAGIC_NUM_DISABLE_DMAW_ON_LINK_CHANGE);
    }

    /* Indicate link status. */
    if (pDevice->LinkStatus != CurrentLinkStatus) {
        pDevice->LinkStatus = CurrentLinkStatus;
        MM_IndicateStatus(pDevice, CurrentLinkStatus);
    }

    return LM_STATUS_SUCCESS;
} /* LM_SetupCopperPhy */

/******************************************************************************/
/* Description:                                                               */
/*                                                                            */
/* Return:                                                                    */
/******************************************************************************/
LM_STATUS
LM_SetupPhy(
    PLM_DEVICE_BLOCK pDevice)
{
    LM_STATUS LmStatus;
    LM_UINT32 Value32;

#if INCLUDE_TBI_SUPPORT
    if(pDevice->EnableTbi)
    {
        LmStatus = LM_SetupFiberPhy(pDevice);
    }
    else
#endif /* INCLUDE_TBI_SUPPORT */
    {
        LmStatus = LM_SetupCopperPhy(pDevice);
    }
    if (pDevice->ChipRevId == T3_CHIP_ID_5704_A0)
    {
        if (!(pDevice->PciState & T3_PCI_STATE_CONVENTIONAL_PCI_MODE))
        {
            Value32 = REG_RD(pDevice, PciCfg.PciState);
            REG_WR(pDevice, PciCfg.PciState,
                Value32 | T3_PCI_STATE_RETRY_SAME_DMA);
        }
    }
    if ((pDevice->LineSpeed == LM_LINE_SPEED_1000MBPS) &&
        (pDevice->DuplexMode == LM_DUPLEX_MODE_HALF))
    {
        REG_WR(pDevice, MacCtrl.TxLengths, 0x26ff);
    }
    else
    {
        REG_WR(pDevice, MacCtrl.TxLengths, 0x2620);
    }
    if (pDevice->LinkStatus == LM_STATUS_LINK_DOWN)
    {
        REG_WR(pDevice, HostCoalesce.StatsCoalescingTicks, 0);
    }
    else
    {
        REG_WR(pDevice, HostCoalesce.StatsCoalescingTicks,
            pDevice->StatsCoalescingTicks);
    }

    return LmStatus;
}

STATIC LM_VOID
LM_ResetPhy(LM_DEVICE_BLOCK *pDevice)
{
    int j;
    LM_UINT32 miireg;

    LM_WritePhy(pDevice, PHY_CTRL_REG, PHY_CTRL_PHY_RESET);

    for(j = 0; j < 100; j++)
    {
        MM_Wait(10);

        LM_ReadPhy(pDevice, PHY_CTRL_REG, &miireg);
        if(miireg && !(miireg & PHY_CTRL_PHY_RESET))
        {
            MM_Wait(40);
            break;
        }
    }
}

STATIC LM_STATUS
LM_PhyAdvertiseAll(LM_DEVICE_BLOCK *pDevice)
{
    LM_UINT32 miireg;

    LM_ReadPhy(pDevice, PHY_AN_AD_REG, &miireg);
    pDevice->advertising = miireg;
    if ((miireg & PHY_AN_AD_ALL_SPEEDS) != PHY_AN_AD_ALL_SPEEDS)
    {
        return LM_STATUS_FAILURE;
    }
    if(!pDevice->No1000)
    {
        LM_ReadPhy(pDevice, BCM540X_1000BASET_CTRL_REG, &miireg);
        pDevice->advertising1000 = miireg;
        if ((miireg & BCM540X_AN_AD_ALL_1G_SPEEDS) !=
            BCM540X_AN_AD_ALL_1G_SPEEDS)
        {
            return LM_STATUS_FAILURE;
        }
    }
    return LM_STATUS_SUCCESS;
}

/******************************************************************************/
/* Description:                                                               */
/*                                                                            */
/* Return:                                                                    */
/******************************************************************************/
LM_VOID
LM_ReadPhy(
PLM_DEVICE_BLOCK pDevice,
LM_UINT32 PhyReg,
PLM_UINT32 pData32) {
    LM_UINT32 Value32;
    LM_UINT32 j;

    if(pDevice->PhyIntMode == T3_PHY_INT_MODE_AUTO_POLLING)
    {
        REG_WR(pDevice, MacCtrl.MiMode, pDevice->MiMode &
            ~MI_MODE_AUTO_POLLING_ENABLE);
        MM_Wait(40);
    }

    Value32 = (pDevice->PhyAddr << MI_COM_FIRST_PHY_ADDR_BIT) |
        ((PhyReg & MI_COM_PHY_REG_ADDR_MASK) << MI_COM_FIRST_PHY_REG_ADDR_BIT) |
        MI_COM_CMD_READ | MI_COM_START;

    REG_WR(pDevice, MacCtrl.MiCom, Value32);
    
    for(j = 0; j < 20; j++)
    {
        MM_Wait(25);

        Value32 = REG_RD(pDevice, MacCtrl.MiCom);

        if(!(Value32 & MI_COM_BUSY))
        {
            MM_Wait(5);
            Value32 = REG_RD(pDevice, MacCtrl.MiCom);
            Value32 &= MI_COM_PHY_DATA_MASK;
            break;
        }
    }

    if(Value32 & MI_COM_BUSY)
    {
        Value32 = 0;
    }

    *pData32 = Value32;

    if(pDevice->PhyIntMode == T3_PHY_INT_MODE_AUTO_POLLING)
    {
        REG_WR(pDevice, MacCtrl.MiMode, pDevice->MiMode);
        MM_Wait(40);
    }
} /* LM_ReadPhy */



/******************************************************************************/
/* Description:                                                               */
/*                                                                            */
/* Return:                                                                    */
/******************************************************************************/
LM_VOID
LM_WritePhy(
PLM_DEVICE_BLOCK pDevice,
LM_UINT32 PhyReg,
LM_UINT32 Data32) {
    LM_UINT32 Value32;
    LM_UINT32 j;

    if(pDevice->PhyIntMode == T3_PHY_INT_MODE_AUTO_POLLING)
    {
        REG_WR(pDevice, MacCtrl.MiMode, pDevice->MiMode &
            ~MI_MODE_AUTO_POLLING_ENABLE);
        MM_Wait(40);
    }

    Value32 = (pDevice->PhyAddr << MI_COM_FIRST_PHY_ADDR_BIT) |
        ((PhyReg & MI_COM_PHY_REG_ADDR_MASK) << MI_COM_FIRST_PHY_REG_ADDR_BIT) |
        (Data32 & MI_COM_PHY_DATA_MASK) | MI_COM_CMD_WRITE | MI_COM_START;

    REG_WR(pDevice, MacCtrl.MiCom, Value32);
    
    for(j = 0; j < 20; j++)
    {
        MM_Wait(25);

        Value32 = REG_RD(pDevice, MacCtrl.MiCom);

        if(!(Value32 & MI_COM_BUSY))
        {
            MM_Wait(5);
            break;
        }
    }

    if(pDevice->PhyIntMode == T3_PHY_INT_MODE_AUTO_POLLING)
    {
        REG_WR(pDevice, MacCtrl.MiMode, pDevice->MiMode);
        MM_Wait(40);
    }
} /* LM_WritePhy */


/******************************************************************************/
/* Description:                                                               */
/*                                                                            */
/* Return:                                                                    */
/******************************************************************************/
LM_STATUS
LM_SetPowerState(
PLM_DEVICE_BLOCK pDevice,
LM_POWER_STATE PowerLevel)
{
#ifdef BCM_WOL
    LM_UINT32 PmeSupport;
    LM_UINT32 Value32;
    PLM_DEVICE_BLOCK pDevice2 = 0;
    int SwitchVaux;
#endif
    LM_UINT32 PmCtrl;

    /* make sureindirect accesses are enabled*/
    MM_WriteConfig32(pDevice, T3_PCI_MISC_HOST_CTRL_REG, pDevice->MiscHostCtrl);

    /* Clear the PME_ASSERT bit and the power state bits.  Also enable */
    /* the PME bit. */
    MM_ReadConfig32(pDevice, T3_PCI_PM_STATUS_CTRL_REG, &PmCtrl);

    PmCtrl |= T3_PM_PME_ASSERTED;
    PmCtrl &= ~T3_PM_POWER_STATE_MASK;

    /* Set the appropriate power state. */
    if(PowerLevel == LM_POWER_STATE_D0)
    {

        /* Bring the card out of low power mode. */
        PmCtrl |= T3_PM_POWER_STATE_D0;
        MM_WriteConfig32(pDevice, T3_PCI_PM_STATUS_CTRL_REG, PmCtrl);

        REG_WR(pDevice, Grc.LocalCtrl, pDevice->GrcLocalCtrl);
	MM_Wait(40);	/* Required delay is about 20us. */

        return LM_STATUS_SUCCESS;
    }
#ifdef BCM_WOL
    else if(PowerLevel == LM_POWER_STATE_D1)
    {
        PmCtrl |= T3_PM_POWER_STATE_D1;
    }
    else if(PowerLevel == LM_POWER_STATE_D2)
    {
        PmCtrl |= T3_PM_POWER_STATE_D2;
    }
    else if(PowerLevel == LM_POWER_STATE_D3)
    {
        PmCtrl |= T3_PM_POWER_STATE_D3;
    }
    else
    {
        return LM_STATUS_FAILURE;
    }
    PmCtrl |= T3_PM_PME_ENABLE;

    /* Mask out all interrupts so LM_SetupPhy won't be called while we are */
    /* setting new line speed. */
    Value32 = REG_RD(pDevice, PciCfg.MiscHostCtrl);
    REG_WR(pDevice, PciCfg.MiscHostCtrl, Value32 | MISC_HOST_CTRL_MASK_PCI_INT);

    if(!pDevice->RestoreOnWakeUp)
    {
        pDevice->RestoreOnWakeUp = TRUE;
        pDevice->WakeUpDisableAutoNeg = pDevice->DisableAutoNeg;
        pDevice->WakeUpRequestedLineSpeed = pDevice->RequestedLineSpeed;
        pDevice->WakeUpRequestedDuplexMode = pDevice->RequestedDuplexMode;
    }

    /* Force auto-negotiation to 10 line speed. */
    pDevice->DisableAutoNeg = FALSE;

    if (!pDevice->EnableTbi)
    {
        pDevice->RequestedLineSpeed = LM_LINE_SPEED_10MBPS;
        LM_SetupPhy(pDevice);
    }

    /* Put the driver in the initial state, and go through the power down */
    /* sequence. */
    LM_Halt(pDevice);

    MM_ReadConfig32(pDevice, T3_PCI_PM_CAP_REG, &PmeSupport);

    if (pDevice->WakeUpModeCap != LM_WAKE_UP_MODE_NONE)
    {
        MEM_WR_OFFSET(pDevice, DRV_WOL_MAILBOX, DRV_WOL_SIGNATURE |
            DRV_DOWN_STATE_SHUTDOWN | 0x2 | DRV_WOL_SET_MAGIC_PKT);

        /* Enable WOL. */
        if (!pDevice->EnableTbi)
        {
            LM_WritePhy(pDevice, BCM5401_AUX_CTRL, 0x5a);
            MM_Wait(40);
        }

        /* Set LED mode. */
        if(T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5700 ||
            T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5701)
        {
            Value32 = LED_CTRL_PHY_MODE_1;
        }
        else
        {
            if(pDevice->LedMode == LED_MODE_OUTPUT)
            {
                Value32 = LED_CTRL_PHY_MODE_2;
            }
            else
            {
                Value32 = LED_CTRL_PHY_MODE_1;
            }
        }

        if (pDevice->EnableTbi)
        {
            Value32 = MAC_MODE_PORT_MODE_TBI;
        }
        else
        {
            Value32 = MAC_MODE_PORT_MODE_MII;
            if(T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5700)
            {
                if(pDevice->LedMode == LED_MODE_LINK10 ||
                    pDevice->WolSpeed == WOL_SPEED_10MB)
                {
                    Value32 |= MAC_MODE_LINK_POLARITY;
                }
            }
            else
            {
                Value32 |= MAC_MODE_LINK_POLARITY;
            }
        }
        REG_WR(pDevice, MacCtrl.Mode, Value32);
        MM_Wait(40); MM_Wait(40); MM_Wait(40);

        /* Always enable magic packet wake-up if we have vaux. */
        if((PmeSupport & T3_PCI_PM_CAP_PME_D3COLD) && 
            (pDevice->WakeUpModeCap & LM_WAKE_UP_MODE_MAGIC_PACKET))
        {
            Value32 |= MAC_MODE_DETECT_MAGIC_PACKET_ENABLE;
        }

#ifdef BCM_ASF
        if (pDevice->EnableAsf)
        {
            Value32 &= ~MAC_MODE_ACPI_POWER_ON_ENABLE;
        }
#endif

        REG_WR(pDevice, MacCtrl.Mode, Value32);

        /* Enable the receiver. */
        REG_WR(pDevice, MacCtrl.RxMode, RX_MODE_ENABLE);
    }

    /* Disable tx/rx clocks, and seletect an alternate clock. */
    if(pDevice->WolSpeed == WOL_SPEED_100MB)
    {
        if(T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5700 ||
            T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5701)
        {
            Value32 = T3_PCI_DISABLE_RX_CLOCK | T3_PCI_DISABLE_TX_CLOCK |
                T3_PCI_SELECT_ALTERNATE_CLOCK;
        }
        else
        {
            Value32 = T3_PCI_SELECT_ALTERNATE_CLOCK;
        }
        REG_WR(pDevice, PciCfg.ClockCtrl, pDevice->ClockCtrl | Value32);

        MM_Wait(40);

        if(T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5700 ||
            T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5701)
        {
            Value32 = T3_PCI_DISABLE_RX_CLOCK | T3_PCI_DISABLE_TX_CLOCK |
                T3_PCI_SELECT_ALTERNATE_CLOCK | T3_PCI_44MHZ_CORE_CLOCK;
        }
        else
        {
            Value32 = T3_PCI_SELECT_ALTERNATE_CLOCK |
                T3_PCI_44MHZ_CORE_CLOCK;
        }

        REG_WR(pDevice, PciCfg.ClockCtrl, pDevice->ClockCtrl | Value32);

        MM_Wait(40);

        if(T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5700 ||
            T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5701)
        {
            Value32 = T3_PCI_DISABLE_RX_CLOCK | T3_PCI_DISABLE_TX_CLOCK |
                T3_PCI_44MHZ_CORE_CLOCK;
        }
        else
        {
            Value32 = T3_PCI_44MHZ_CORE_CLOCK;
        }

        REG_WR(pDevice, PciCfg.ClockCtrl, pDevice->ClockCtrl | Value32);
    }
    else
    {
        if(T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5700 ||
            T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5701)
        {
            Value32 = T3_PCI_DISABLE_RX_CLOCK | T3_PCI_DISABLE_TX_CLOCK |
                T3_PCI_SELECT_ALTERNATE_CLOCK |
                T3_PCI_POWER_DOWN_PCI_PLL133;
        }
        else
        {
            Value32 = T3_PCI_SELECT_ALTERNATE_CLOCK |
                T3_PCI_POWER_DOWN_PCI_PLL133;
        }

        REG_WR(pDevice, PciCfg.ClockCtrl, pDevice->ClockCtrl | Value32);
    }

    MM_Wait(40);

    if (T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5704)
    {
        pDevice2 = MM_FindPeerDev(pDevice);
    }
    if(!pDevice->EepromWp &&
        ((pDevice->WakeUpModeCap != LM_WAKE_UP_MODE_NONE) ||
        (pDevice2 && pDevice2->WakeUpModeCap != LM_WAKE_UP_MODE_NONE)))
    {
        SwitchVaux = 1;
        if (pDevice2 && pDevice2->InitDone)
        {
            SwitchVaux = 0;
        }
        if (SwitchVaux)
        {
            LM_SwitchVaux(pDevice);
        }
    }

    /* Set the phy to low power mode. */
    /* Put the the hardware in low power mode. */
    MM_WriteConfig32(pDevice, T3_PCI_PM_STATUS_CTRL_REG, PmCtrl);
#endif
    return LM_STATUS_SUCCESS;
} /* LM_SetPowerState */


LM_VOID
LM_SwitchVaux(PLM_DEVICE_BLOCK pDevice)
{
    /* Switch adapter to auxilliary power. */
    if(T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5700 ||
        T3_ASIC_REV(pDevice->ChipRevId) == T3_ASIC_REV_5701)
    {
        /* GPIO0 = 1, GPIO1 = 1, GPIO2 = 0. */
        REG_WR(pDevice, Grc.LocalCtrl, pDevice->GrcLocalCtrl |
            GRC_MISC_LOCAL_CTRL_GPIO_OE0 |
            GRC_MISC_LOCAL_CTRL_GPIO_OE1 |
            GRC_MISC_LOCAL_CTRL_GPIO_OE2 | 
            GRC_MISC_LOCAL_CTRL_GPIO_OUTPUT0 |
            GRC_MISC_LOCAL_CTRL_GPIO_OUTPUT1);
        MM_Wait(40);
    }
    else
    {
        /* GPIO0 = 0, GPIO1 = 1, GPIO2 = 1. */
        REG_WR(pDevice, Grc.LocalCtrl, pDevice->GrcLocalCtrl |
            GRC_MISC_LOCAL_CTRL_GPIO_OE0 |
            GRC_MISC_LOCAL_CTRL_GPIO_OE1 |
            GRC_MISC_LOCAL_CTRL_GPIO_OE2 | 
            GRC_MISC_LOCAL_CTRL_GPIO_OUTPUT1 | 
            GRC_MISC_LOCAL_CTRL_GPIO_OUTPUT2);
        MM_Wait(40);

        /* GPIO0 = 1, GPIO1 = 1, GPIO2 = 1. */
        REG_WR(pDevice, Grc.LocalCtrl, pDevice->GrcLocalCtrl |
            GRC_MISC_LOCAL_CTRL_GPIO_OE0 |
            GRC_MISC_LOCAL_CTRL_GPIO_OE1 |
            GRC_MISC_LOCAL_CTRL_GPIO_OE2 | 
            GRC_MISC_LOCAL_CTRL_GPIO_OUTPUT0 |
            GRC_MISC_LOCAL_CTRL_GPIO_OUTPUT1 |
            GRC_MISC_LOCAL_CTRL_GPIO_OUTPUT2);
        MM_Wait(40);

        /* GPIO0 = 1, GPIO1 = 1, GPIO2 = 0. */
        REG_WR(pDevice, Grc.LocalCtrl, pDevice->GrcLocalCtrl |
            GRC_MISC_LOCAL_CTRL_GPIO_OE0 | 
            GRC_MISC_LOCAL_CTRL_GPIO_OE1 |
            GRC_MISC_LOCAL_CTRL_GPIO_OE2 | 
            GRC_MISC_LOCAL_CTRL_GPIO_OUTPUT0 |
            GRC_MISC_LOCAL_CTRL_GPIO_OUTPUT1);
        MM_Wait(40);
    }
}

/******************************************************************************/
/* Description:                                                               */
/*                                                                            */
/* Return:                                                                    */
/******************************************************************************/
static LM_UINT32
GetPhyAdFlowCntrlSettings(
    PLM_DEVICE_BLOCK pDevice)
{
    LM_UINT32 Value32;

    Value32 = 0;

    /* Auto negotiation flow control only when autonegotiation is enabled. */
    if(pDevice->DisableAutoNeg == FALSE ||
        pDevice->RequestedLineSpeed == LM_LINE_SPEED_AUTO)
    {
        /* Please refer to Table 28B-3 of the 802.3ab-1999 spec. */
        if((pDevice->FlowControlCap == LM_FLOW_CONTROL_AUTO_PAUSE) ||
            ((pDevice->FlowControlCap & LM_FLOW_CONTROL_RECEIVE_PAUSE) &&
            (pDevice->FlowControlCap & LM_FLOW_CONTROL_TRANSMIT_PAUSE)))
        {
            Value32 |= PHY_AN_AD_PAUSE_CAPABLE;
        }
        else if(pDevice->FlowControlCap & LM_FLOW_CONTROL_TRANSMIT_PAUSE)
        {
            Value32 |= PHY_AN_AD_ASYM_PAUSE;
        }
        else if(pDevice->FlowControlCap & LM_FLOW_CONTROL_RECEIVE_PAUSE)
        {
            Value32 |= PHY_AN_AD_PAUSE_CAPABLE | PHY_AN_AD_ASYM_PAUSE;
        }
    }

    return Value32;
}



/******************************************************************************/
/* Description:                                                               */
/*                                                                            */
/* Return:                                                                    */
/*    LM_STATUS_FAILURE                                                       */
/*    LM_STATUS_SUCCESS                                                       */
/*                                                                            */
/******************************************************************************/
static LM_STATUS
LM_ForceAutoNeg(PLM_DEVICE_BLOCK pDevice) 
{
    LM_LINE_SPEED LineSpeed;
    LM_DUPLEX_MODE DuplexMode;
    LM_UINT32 NewPhyCtrl;
    LM_UINT32 Value32;
    LM_UINT32 Cnt;

    /* Get the interface type, line speed, and duplex mode. */
    LineSpeed = pDevice->RequestedLineSpeed;
    DuplexMode = pDevice->RequestedDuplexMode;

#ifdef BCM_WOL
    if (pDevice->RestoreOnWakeUp)
    {
        LM_WritePhy(pDevice, BCM540X_1000BASET_CTRL_REG, 0);
        pDevice->advertising1000 = 0;
        Value32 = PHY_AN_AD_10BASET_FULL | PHY_AN_AD_10BASET_HALF;
        if (pDevice->WolSpeed == WOL_SPEED_100MB)
        {
            Value32 |= PHY_AN_AD_100BASETX_FULL | PHY_AN_AD_100BASETX_HALF;
        }
        Value32 |= PHY_AN_AD_PROTOCOL_802_3_CSMA_CD;
        Value32 |= GetPhyAdFlowCntrlSettings(pDevice);
        LM_WritePhy(pDevice, PHY_AN_AD_REG, Value32);
        pDevice->advertising = Value32;
    }
    /* Setup the auto-negotiation advertisement register. */
    else if(LineSpeed == LM_LINE_SPEED_UNKNOWN)
#else
    /* Setup the auto-negotiation advertisement register. */
    if(LineSpeed == LM_LINE_SPEED_UNKNOWN)
#endif
    {
        /* Setup the 10/100 Mbps auto-negotiation advertisement register. */
        Value32 = PHY_AN_AD_PROTOCOL_802_3_CSMA_CD | PHY_AN_AD_ALL_SPEEDS;
        Value32 |= GetPhyAdFlowCntrlSettings(pDevice);

        LM_WritePhy(pDevice, PHY_AN_AD_REG, Value32);
        pDevice->advertising = Value32;

        /* Advertise 1000Mbps */
        if (!pDevice->No1000)
        {
            Value32 = BCM540X_AN_AD_ALL_1G_SPEEDS;

#if INCLUDE_5701_AX_FIX
            /* Bug: workaround for CRC error in gigabit mode when we are in */
            /* slave mode.  This will force the PHY to operate in */
            /* master mode. */
            if(pDevice->ChipRevId == T3_CHIP_ID_5701_A0 ||
                pDevice->ChipRevId == T3_CHIP_ID_5701_B0)
            {
                Value32 |= BCM540X_CONFIG_AS_MASTER |
                    BCM540X_ENABLE_CONFIG_AS_MASTER;
            }
#endif

            LM_WritePhy(pDevice, BCM540X_1000BASET_CTRL_REG, Value32);
            pDevice->advertising1000 = Value32;
        }
    }
    else
    {
        if(pDevice->No1000 && (LineSpeed == LM_LINE_SPEED_1000MBPS))
        {
            LineSpeed = LM_LINE_SPEED_100MBPS;
        }
        if(LineSpeed == LM_LINE_SPEED_1000MBPS)
        {
            Value32 = PHY_AN_AD_PROTOCOL_802_3_CSMA_CD;
            Value32 |= GetPhyAdFlowCntrlSettings(pDevice);

            LM_WritePhy(pDevice, PHY_AN_AD_REG, Value32);
            pDevice->advertising = Value32;

            if(DuplexMode != LM_DUPLEX_MODE_FULL)
            {
                Value32 = BCM540X_AN_AD_1000BASET_HALF;
            }
            else
            {
                Value32 = BCM540X_AN_AD_1000BASET_FULL;
            }

            LM_WritePhy(pDevice, BCM540X_1000BASET_CTRL_REG, Value32);
            pDevice->advertising1000 = Value32;
        }
        else if(LineSpeed == LM_LINE_SPEED_100MBPS)
        {
            LM_WritePhy(pDevice, BCM540X_1000BASET_CTRL_REG, 0);
            pDevice->advertising1000 = 0;

            if(DuplexMode != LM_DUPLEX_MODE_FULL)
            {
                Value32 = PHY_AN_AD_100BASETX_HALF;
            }
            else
            {
                Value32 = PHY_AN_AD_100BASETX_FULL;
            }

            Value32 |= PHY_AN_AD_PROTOCOL_802_3_CSMA_CD;
            Value32 |= GetPhyAdFlowCntrlSettings(pDevice);

            LM_WritePhy(pDevice, PHY_AN_AD_REG, Value32);
            pDevice->advertising = Value32;
        }
        else if(LineSpeed == LM_LINE_SPEED_10MBPS)
        {
            LM_WritePhy(pDevice, BCM540X_1000BASET_CTRL_REG, 0);
            pDevice->advertising1000 = 0;

            if(DuplexMode != LM_DUPLEX_MODE_FULL)
            {
                Value32 = PHY_AN_AD_10BASET_HALF;
            }
            else
            {
                Value32 = PHY_AN_AD_10BASET_FULL;
            }

            Value32 |= PHY_AN_AD_PROTOCOL_802_3_CSMA_CD;
            Value32 |= GetPhyAdFlowCntrlSettings(pDevice);

            LM_WritePhy(pDevice, PHY_AN_AD_REG, Value32);
            pDevice->advertising = Value32;
        }
    }

    /* Force line speed if auto-negotiation is disabled. */
    if(pDevice->DisableAutoNeg && LineSpeed != LM_LINE_SPEED_UNKNOWN)
    {
        /* This code path is executed only when there is link. */
        pDevice->LineSpeed = LineSpeed;
        pDevice->DuplexMode = DuplexMode;

        /* Force line seepd. */
        NewPhyCtrl = 0;
        switch(LineSpeed)
        {
            case LM_LINE_SPEED_10MBPS:
                NewPhyCtrl |= PHY_CTRL_SPEED_SELECT_10MBPS;
                break;
            case LM_LINE_SPEED_100MBPS:
                NewPhyCtrl |= PHY_CTRL_SPEED_SELECT_100MBPS;
                break;
            case LM_LINE_SPEED_1000MBPS:
                NewPhyCtrl |= PHY_CTRL_SPEED_SELECT_1000MBPS;
                break;
            default:
                NewPhyCtrl |= PHY_CTRL_SPEED_SELECT_1000MBPS;
                break;
        }

        if(DuplexMode == LM_DUPLEX_MODE_FULL)
        {
            NewPhyCtrl |= PHY_CTRL_FULL_DUPLEX_MODE;
        }

        /* Don't do anything if the PHY_CTRL is already what we wanted. */
        LM_ReadPhy(pDevice, PHY_CTRL_REG, &Value32);
        if(Value32 != NewPhyCtrl)
        {
            /* Temporary bring the link down before forcing line speed. */
            LM_WritePhy(pDevice, PHY_CTRL_REG, PHY_CTRL_LOOPBACK_MODE);
            
            /* Wait for link to go down. */
            for(Cnt = 0; Cnt < 15000; Cnt++)
            {
                MM_Wait(10);

                LM_ReadPhy(pDevice, PHY_STATUS_REG, &Value32);
                LM_ReadPhy(pDevice, PHY_STATUS_REG, &Value32);

                if(!(Value32 & PHY_STATUS_LINK_PASS))
                {
                    MM_Wait(40);
                    break;
                }
            }

            LM_WritePhy(pDevice, PHY_CTRL_REG, NewPhyCtrl);
            MM_Wait(40);
        }
    }
    else
    {
        LM_WritePhy(pDevice, PHY_CTRL_REG, PHY_CTRL_AUTO_NEG_ENABLE |
            PHY_CTRL_RESTART_AUTO_NEG);
    }

    return LM_STATUS_SUCCESS;
} /* LM_ForceAutoNegBcm540xPhy */

/******************************************************************************/
/* Description:                                                               */
/*                                                                            */
/* Return:                                                                    */
/******************************************************************************/
LM_STATUS LM_LoadFirmware(PLM_DEVICE_BLOCK pDevice,
                          PT3_FWIMG_INFO pFwImg,
                          LM_UINT32 LoadCpu,
                          LM_UINT32 StartCpu)
{
    LM_UINT32 i;
    LM_UINT32 address;

    if (LoadCpu & T3_RX_CPU_ID)
    {
        if (LM_HaltCpu(pDevice,T3_RX_CPU_ID) != LM_STATUS_SUCCESS)
        {
            return LM_STATUS_FAILURE;
        }

        /* First of all clear scrach pad memory */
        for (i = 0; i < T3_RX_CPU_SPAD_SIZE; i+=4)
        { 
            LM_RegWrInd(pDevice,T3_RX_CPU_SPAD_ADDR+i,0);
        }

        /* Copy code first */
        address = T3_RX_CPU_SPAD_ADDR + (pFwImg->Text.Offset & 0xffff);
        for (i = 0; i <= pFwImg->Text.Length; i+=4)
        {
            LM_RegWrInd(pDevice,address+i,
                        ((LM_UINT32 *)pFwImg->Text.Buffer)[i/4]);
        }

        address = T3_RX_CPU_SPAD_ADDR + (pFwImg->ROnlyData.Offset & 0xffff);
        for (i = 0; i <= pFwImg->ROnlyData.Length; i+=4)
        {
            LM_RegWrInd(pDevice,address+i,
                        ((LM_UINT32 *)pFwImg->ROnlyData.Buffer)[i/4]);
        }

        address = T3_RX_CPU_SPAD_ADDR + (pFwImg->Data.Offset & 0xffff);
        for (i= 0; i <= pFwImg->Data.Length; i+=4)
        {
            LM_RegWrInd(pDevice,address+i,
                        ((LM_UINT32 *)pFwImg->Data.Buffer)[i/4]);
        }
    }

    if (LoadCpu & T3_TX_CPU_ID)
    {
        if (LM_HaltCpu(pDevice,T3_TX_CPU_ID) != LM_STATUS_SUCCESS)
        {
            return LM_STATUS_FAILURE;
        }

        /* First of all clear scrach pad memory */
        for (i = 0; i < T3_TX_CPU_SPAD_SIZE; i+=4)
        { 
            LM_RegWrInd(pDevice,T3_TX_CPU_SPAD_ADDR+i,0);
        }

        /* Copy code first */
        address = T3_TX_CPU_SPAD_ADDR + (pFwImg->Text.Offset & 0xffff);
        for (i= 0; i <= pFwImg->Text.Length; i+=4)
        {
            LM_RegWrInd(pDevice,address+i,
                        ((LM_UINT32 *)pFwImg->Text.Buffer)[i/4]);
        }

        address = T3_TX_CPU_SPAD_ADDR + (pFwImg->ROnlyData.Offset & 0xffff);
        for (i= 0; i <= pFwImg->ROnlyData.Length; i+=4)
        {
            LM_RegWrInd(pDevice,address+i,
                        ((LM_UINT32 *)pFwImg->ROnlyData.Buffer)[i/4]);
        }

        address = T3_TX_CPU_SPAD_ADDR + (pFwImg->Data.Offset & 0xffff);
        for (i= 0; i <= pFwImg->Data.Length; i+=4)
        {
            LM_RegWrInd(pDevice,address+i,
                        ((LM_UINT32 *)pFwImg->Data.Buffer)[i/4]);
        }
    }

    if (StartCpu & T3_RX_CPU_ID)
    {
        /* Start Rx CPU */
        REG_WR(pDevice,rxCpu.reg.state, 0xffffffff);
        REG_WR(pDevice,rxCpu.reg.PC,pFwImg->StartAddress);
        for (i = 0 ; i < 5; i++)
        {
          if (pFwImg->StartAddress == REG_RD(pDevice,rxCpu.reg.PC))
             break;

          REG_WR(pDevice,rxCpu.reg.state, 0xffffffff);
          REG_WR(pDevice,rxCpu.reg.mode,CPU_MODE_HALT);
          REG_WR(pDevice,rxCpu.reg.PC,pFwImg->StartAddress);
          MM_Wait(1000);
        }

        REG_WR(pDevice,rxCpu.reg.state, 0xffffffff);
        REG_WR(pDevice,rxCpu.reg.mode, 0);
    }

    if (StartCpu & T3_TX_CPU_ID)
    {
        /* Start Tx CPU */
        REG_WR(pDevice,txCpu.reg.state, 0xffffffff);
        REG_WR(pDevice,txCpu.reg.PC,pFwImg->StartAddress);
        for (i = 0 ; i < 5; i++)
        {
          if (pFwImg->StartAddress == REG_RD(pDevice,txCpu.reg.PC))
             break;

          REG_WR(pDevice,txCpu.reg.state, 0xffffffff);
          REG_WR(pDevice,txCpu.reg.mode,CPU_MODE_HALT);
          REG_WR(pDevice,txCpu.reg.PC,pFwImg->StartAddress);
          MM_Wait(1000);
        }
        
        REG_WR(pDevice,txCpu.reg.state, 0xffffffff);
        REG_WR(pDevice,txCpu.reg.mode, 0);
    }
    
    return LM_STATUS_SUCCESS;
}

STATIC LM_STATUS LM_HaltCpu(PLM_DEVICE_BLOCK pDevice,LM_UINT32 cpu_number)
{
    LM_UINT32 i;

    if (cpu_number == T3_RX_CPU_ID)
    {
        for (i = 0 ; i < 10000; i++)
        {
            REG_WR(pDevice,rxCpu.reg.state, 0xffffffff);
            REG_WR(pDevice,rxCpu.reg.mode,CPU_MODE_HALT);

            if (REG_RD(pDevice,rxCpu.reg.mode) & CPU_MODE_HALT)
              break;
        }

        REG_WR(pDevice,rxCpu.reg.state, 0xffffffff);
        REG_WR(pDevice,rxCpu.reg.mode,CPU_MODE_HALT);
        MM_Wait(10);
    }
    else
    {
        for (i = 0 ; i < 10000; i++)
        {
            REG_WR(pDevice,txCpu.reg.state, 0xffffffff);
            REG_WR(pDevice,txCpu.reg.mode,CPU_MODE_HALT);

            if (REG_RD(pDevice,txCpu.reg.mode) & CPU_MODE_HALT)
               break;
        }
    }

  return (( i == 10000) ? LM_STATUS_FAILURE : LM_STATUS_SUCCESS);
}


int
LM_BlinkLED(PLM_DEVICE_BLOCK pDevice, LM_UINT32 BlinkDurationSec)
{
	LM_UINT32 Oldcfg;
	int j;
	int ret = 0;

	if(BlinkDurationSec == 0)
	{
		return 0;
        }
	if(BlinkDurationSec > 120)
        {
	        BlinkDurationSec = 120;
	}

	Oldcfg = REG_RD(pDevice, MacCtrl.LedCtrl);
	for(j = 0; j < BlinkDurationSec * 2; j++)
	{
		if(j % 2)
		{
			// Turn on the LEDs.
			REG_WR(pDevice, MacCtrl.LedCtrl,
				LED_CTRL_OVERRIDE_LINK_LED |
				LED_CTRL_1000MBPS_LED_ON |
				LED_CTRL_100MBPS_LED_ON |
				LED_CTRL_10MBPS_LED_ON |
				LED_CTRL_OVERRIDE_TRAFFIC_LED |
				LED_CTRL_BLINK_TRAFFIC_LED |
				LED_CTRL_TRAFFIC_LED);
		}
		else
		{
			// Turn off the LEDs.
			REG_WR(pDevice, MacCtrl.LedCtrl,
				LED_CTRL_OVERRIDE_LINK_LED |
				LED_CTRL_OVERRIDE_TRAFFIC_LED);
		}
		current->state = TASK_INTERRUPTIBLE;
		if (schedule_timeout(HZ/2) != 0) {
			ret = -EINTR;
			break;
		}
	}
	REG_WR(pDevice, MacCtrl.LedCtrl, Oldcfg);
	return ret;
}

STATIC LM_STATUS
LM_SwitchClocks(PLM_DEVICE_BLOCK pDevice)
{
    LM_UINT32 ClockCtrl;

    ClockCtrl = REG_RD(pDevice, PciCfg.ClockCtrl);
    pDevice->ClockCtrl = ClockCtrl & (BIT_21 | BIT_22 | 0x1f);
    if (ClockCtrl & T3_PCI_44MHZ_CORE_CLOCK)
    {
        REG_WR(pDevice, PciCfg.ClockCtrl, pDevice->ClockCtrl |
                T3_PCI_44MHZ_CORE_CLOCK | T3_PCI_SELECT_ALTERNATE_CLOCK);
        MM_Wait(40);  /* required delay is 27usec */
        REG_WR(pDevice, PciCfg.ClockCtrl, pDevice->ClockCtrl |
            T3_PCI_SELECT_ALTERNATE_CLOCK);
        MM_Wait(40);  /* required delay is 27usec */
    }
    REG_WR(pDevice, PciCfg.ClockCtrl, pDevice->ClockCtrl);
    MM_Wait(40);  /* required delay is 27usec */
    return LM_STATUS_SUCCESS;
}

int t3_do_dma(PLM_DEVICE_BLOCK pDevice, 
                   LM_PHYSICAL_ADDRESS host_addr_phy, int length,
                   int dma_read)
{
    T3_DMA_DESC dma_desc;
    int i;
    LM_UINT32 dma_desc_addr;
    LM_UINT32 value32;

    REG_WR(pDevice, BufMgr.Mode, 0);
    REG_WR(pDevice, Ftq.Reset, 0);

    dma_desc.host_addr.High = host_addr_phy.High;
    dma_desc.host_addr.Low = host_addr_phy.Low;
    dma_desc.nic_mbuf = 0x2100;
    dma_desc.len = length;
    dma_desc.flags = 0x00000004; /* Generate Rx-CPU event */

    if (dma_read)
    {
        dma_desc.cqid_sqid = (T3_QID_RX_BD_COMP << 8) |
            T3_QID_DMA_HIGH_PRI_READ;
        REG_WR(pDevice, DmaRead.Mode, DMA_READ_MODE_ENABLE);
    }
    else
    {
        dma_desc.cqid_sqid = (T3_QID_RX_DATA_COMP << 8) |
            T3_QID_DMA_HIGH_PRI_WRITE;
        REG_WR(pDevice, DmaWrite.Mode, DMA_WRITE_MODE_ENABLE);
    }

    dma_desc_addr = T3_NIC_DMA_DESC_POOL_ADDR;

    /* Writing this DMA descriptor to DMA memory */
    for (i = 0; i < sizeof(T3_DMA_DESC); i += 4)
    {
        value32 = *((PLM_UINT32) (((PLM_UINT8) &dma_desc) + i));
        MM_WriteConfig32(pDevice, T3_PCI_MEM_WIN_ADDR_REG, dma_desc_addr+i);
        MM_WriteConfig32(pDevice, T3_PCI_MEM_WIN_DATA_REG, cpu_to_le32(value32));
    }
    MM_WriteConfig32(pDevice, T3_PCI_MEM_WIN_ADDR_REG, 0);

    if (dma_read)
        REG_WR(pDevice, Ftq.DmaHighReadFtqFifoEnqueueDequeue, dma_desc_addr);
    else
        REG_WR(pDevice, Ftq.DmaHighWriteFtqFifoEnqueueDequeue, dma_desc_addr);

    for (i = 0; i < 40; i++)
    {
        if (dma_read)
            value32 = REG_RD(pDevice, Ftq.RcvBdCompFtqFifoEnqueueDequeue);
        else
            value32 = REG_RD(pDevice, Ftq.RcvDataCompFtqFifoEnqueueDequeue);

        if ((value32 & 0xffff) == dma_desc_addr)
            break;

        MM_Wait(10);
    }

    return LM_STATUS_SUCCESS;
}

STATIC LM_STATUS
LM_DmaTest(PLM_DEVICE_BLOCK pDevice, PLM_UINT8 pBufferVirt,
           LM_PHYSICAL_ADDRESS BufferPhy, LM_UINT32 BufferSize)
{
    int j;
    LM_UINT32 *ptr;
    int dma_success = 0;
    LM_STATUS ret = LM_STATUS_FAILURE;

    if(T3_ASIC_REV(pDevice->ChipRevId) != T3_ASIC_REV_5700 &&
        T3_ASIC_REV(pDevice->ChipRevId) != T3_ASIC_REV_5701)
    {
        return LM_STATUS_SUCCESS;
    }
    while (!dma_success)
    {
        /* Fill data with incremental patterns */
        ptr = (LM_UINT32 *)pBufferVirt;
        for (j = 0; j < BufferSize/4; j++)
            *ptr++ = j;

        if (t3_do_dma(pDevice,BufferPhy,BufferSize, 1) == LM_STATUS_FAILURE)
        {
            goto LM_DmaTestDone;
        }

        MM_Wait(40);
        ptr = (LM_UINT32 *)pBufferVirt;
        /* Fill data with zero */
        for (j = 0; j < BufferSize/4; j++)
            *ptr++ = 0;

        if (t3_do_dma(pDevice,BufferPhy,BufferSize, 0) == LM_STATUS_FAILURE)
        {
            goto LM_DmaTestDone;
        }

        MM_Wait(40);
        /* Check for data */
        ptr = (LM_UINT32 *)pBufferVirt;
        for (j = 0; j < BufferSize/4; j++)
        {
            if (*ptr++ != j)
            {
                if ((pDevice->DmaReadWriteCtrl & DMA_CTRL_WRITE_BOUNDARY_MASK)
                    == DMA_CTRL_WRITE_BOUNDARY_DISABLE)
                {
                    pDevice->DmaReadWriteCtrl = (pDevice->DmaReadWriteCtrl &
                         ~DMA_CTRL_WRITE_BOUNDARY_MASK) |
                          DMA_CTRL_WRITE_BOUNDARY_16;
                    REG_WR(pDevice, PciCfg.DmaReadWriteCtrl,
                           pDevice->DmaReadWriteCtrl);
                    break;
                 }
                 else
                 {
                     goto LM_DmaTestDone;
                 }
            }
        }
        if (j == (BufferSize/4))
            dma_success = 1;
    }
    ret = LM_STATUS_SUCCESS;
LM_DmaTestDone:
    memset(pBufferVirt, 0, BufferSize);
    return ret;
}

