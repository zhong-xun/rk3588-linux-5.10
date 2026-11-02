/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020 Realtek Technologies Co., Ltd
 */

#include <sys/queue.h>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <stdarg.h>

#include <rte_ether.h>
#include <ethdev_driver.h>

#include "r8125_hw.h"
#include "r8125_logs.h"
#include "r8125_dash.h"

void
rtl8125_mac_ocp_write(struct rtl8125_hw *hw, u16 addr, u16 value)
{
	u32 data32;

	data32 = addr / 2;
	data32 <<= OCPR_Addr_Reg_shift;
	data32 += value;
	data32 |= OCPR_Write;

	RTL_W32(hw, MACOCP, data32);
}

u16
rtl8125_mac_ocp_read(struct rtl8125_hw *hw, u16 addr)
{
	u32 data32;
	u16 data16 = 0;

	data32 = addr / 2;
	data32 <<= OCPR_Addr_Reg_shift;

	RTL_W32(hw, MACOCP, data32);
	data16 = (u16)RTL_R32(hw, MACOCP);

	return data16;
}

u32
rtl8125_csi_read(struct rtl8125_hw *hw, u32 addr)
{
	u32 cmd;
	int i;
	u32 value = 0;

	cmd = CSIAR_Read | CSIAR_ByteEn << CSIAR_ByteEn_shift |
	      (addr & CSIAR_Addr_Mask);

	RTL_W32(hw, CSIAR, cmd);

	for (i = 0; i < 10; i++) {
		udelay(100);

		/* Check if the RTL8125 has completed CSI read */
		if (RTL_R32(hw, CSIAR) & CSIAR_Flag) {
			value = RTL_R32(hw, CSIDR);
			break;
		}
	}

	udelay(20);

	return value;
}

void
rtl8125_get_mac_version(struct rtl8125_hw *hw, struct rte_pci_device *pci_dev)
{
	u32 reg, val32;
	u32 ic_version_id;

	val32 = RTL_R32(hw, TxConfig);
	reg = val32 & 0x7c800000;
	ic_version_id = val32 & 0x00700000;

	switch (reg) {
	case 0x60800000:
		if (ic_version_id == 0x00000000)
			hw->mcfg = CFG_METHOD_2;

		else if (ic_version_id == 0x100000)
			hw->mcfg = CFG_METHOD_3;

		else {
			hw->mcfg = CFG_METHOD_3;
			hw->HwIcVerUnknown = TRUE;
		}

		hw->efuse_ver = EFUSE_SUPPORT_V4;
		break;
	case 0x64000000:
		if (ic_version_id == 0x00000000)
			hw->mcfg = CFG_METHOD_4;

		else if (ic_version_id == 0x100000)
			hw->mcfg = CFG_METHOD_5;

		else {
			hw->mcfg = CFG_METHOD_5;
			hw->HwIcVerUnknown = TRUE;
		}

		hw->efuse_ver = EFUSE_SUPPORT_V4;
		break;
	case 0x68000000:
		if (ic_version_id == 0x00000000)
			hw->mcfg = CFG_METHOD_8;
		else if (ic_version_id == 0x100000)
			hw->mcfg = CFG_METHOD_9;
		else {
			hw->mcfg = CFG_METHOD_9;
			hw->HwIcVerUnknown = TRUE;
		}

		hw->efuse_ver = EFUSE_SUPPORT_V4;
		break;
	case 0x68800000:
		if (ic_version_id == 0x00000000)
			hw->mcfg = CFG_METHOD_10;
		else if (ic_version_id == 0x100000)
			hw->mcfg = CFG_METHOD_11;
		else {
			hw->mcfg = CFG_METHOD_11;
			hw->HwIcVerUnknown = TRUE;
		}

		hw->efuse_ver = EFUSE_SUPPORT_V4;
		break;
	default:
		PMD_INIT_LOG(NOTICE, "unknown chip version (%x)", reg);
		hw->mcfg = CFG_METHOD_DEFAULT;
		hw->HwIcVerUnknown = TRUE;
		hw->efuse_ver = EFUSE_NOT_SUPPORT;
		break;
	}

	if (pci_dev->id.device_id == 0x8162) {
		if (hw->mcfg == CFG_METHOD_3)
			hw->mcfg = CFG_METHOD_6;
		else if (hw->mcfg == CFG_METHOD_5)
			hw->mcfg = CFG_METHOD_7;
	}
}

void
rtl8125_enable_cfg9346_write(struct rtl8125_hw *hw)
{
	RTL_W8(hw, Cfg9346, RTL_R8(hw, Cfg9346) | Cfg9346_Unlock);
}

void
rtl8125_disable_cfg9346_write(struct rtl8125_hw *hw)
{
	RTL_W8(hw, Cfg9346, RTL_R8(hw, Cfg9346) & ~Cfg9346_Unlock);
}

void
rtl8125_rar_set(struct rtl8125_hw *hw, uint8_t *addr)
{
	uint32_t rar_low = 0;
	uint32_t rar_high = 0;

	rar_low = ((uint32_t) addr[0] |
	           ((uint32_t) addr[1] << 8) |
	           ((uint32_t) addr[2] << 16) |
	           ((uint32_t) addr[3] << 24));

	rar_high = ((uint32_t) addr[4] |
	            ((uint32_t) addr[5] << 8));

	rtl8125_enable_cfg9346_write(hw);
	RTL_W32(hw, MAC0, rar_low);
	RTL_W32(hw, MAC4, rar_high);

	rtl8125_disable_cfg9346_write(hw);
}

int
rtl8125_get_mac_address(struct rtl8125_hw *hw, struct rte_ether_addr *ea)
{
	u8 mac_addr[MAC_ADDR_LEN];

	switch (hw->mcfg) {
	case CFG_METHOD_2 ... CFG_METHOD_11:
		*(u32 *)&mac_addr[0] = RTL_R32(hw, BACKUP_ADDR0_8125);
		*(u16 *)&mac_addr[4] = RTL_R16(hw, BACKUP_ADDR1_8125);
		break;
	default:
		break;
	}

	rte_ether_addr_copy((struct rte_ether_addr *)mac_addr, ea);

	return 0;
}

int
rtl8125_set_hw_ops(struct rtl8125_hw *hw)
{
	switch (hw->mcfg) {
	/* 8125A */
	case CFG_METHOD_2:
	case CFG_METHOD_3:
		hw->hw_ops = rtl8125a_ops;
		return 0;
	/* 8125B */
	case CFG_METHOD_4:
	case CFG_METHOD_5:
		hw->hw_ops = rtl8125b_ops;
		return 0;
	/* 8125BP */
	case CFG_METHOD_8:
	case CFG_METHOD_9:
		hw->hw_ops = rtl8125bp_ops;
		return 0;
	/* 8125D */
	case CFG_METHOD_10:
	case CFG_METHOD_11:
		hw->hw_ops = rtl8125d_ops;
		return 0;
	default:
		return -ENOTSUP;
	}
}

static bool
rtl8125_stop_all_request(struct rtl8125_hw *hw)
{
	int i;

	RTL_W8(hw, ChipCmd, RTL_R8(hw, ChipCmd) | StopReq);

	switch (hw->mcfg) {
	case CFG_METHOD_2:
	case CFG_METHOD_3:
	case CFG_METHOD_6:
		for (i = 0; i < 20; i++) {
			udelay(10);
			if (!(RTL_R8(hw, ChipCmd) & StopReq)) break;
		}

		if (i == 20)
			return 0;
		break;
	}

	return 1;
}

static void
rtl8125_wait_txrx_fifo_empty(struct rtl8125_hw *hw)
{
	int i;
	switch (hw->mcfg) {
	case CFG_METHOD_2 ... CFG_METHOD_11:
		for (i = 0; i < 3000; i++) {
			udelay(50);
			if ((RTL_R8(hw, MCUCmd_reg) & (Txfifo_empty | Rxfifo_empty)) ==
			    (Txfifo_empty | Rxfifo_empty))
				break;
		}
		break;
	}

	switch (hw->mcfg) {
	case CFG_METHOD_4:
	case CFG_METHOD_5:
	case CFG_METHOD_7:
	case CFG_METHOD_8:
	case CFG_METHOD_9:
		for (i = 0; i < 3000; i++) {
			udelay(50);
			if ((RTL_R16(hw, IntrMitigate) & (BIT_0 | BIT_1 | BIT_8)) ==
			    (BIT_0 | BIT_1 | BIT_8))
				break;
		}
		break;
	}
}

static void
rtl8125_wait_ll_share_fifo_ready(struct rtl8125_hw *hw)
{
	int i;

	for (i = 0; i < 10; i++) {
		udelay(100);
		if (RTL_R16(hw, 0xD2) & BIT_9)
			break;
	}
}

static void
rtl8125_disable_now_is_oob(struct rtl8125_hw *hw)
{
	if (hw->HwSuppNowIsOobVer == 1)
		RTL_W8(hw, MCUCmd_reg, RTL_R8(hw, MCUCmd_reg) & ~Now_is_oob);
}

void
rtl8125_enable_force_clkreq(struct rtl8125_hw *hw, bool enable)
{
	if (enable)
		RTL_W8(hw, 0xF1, RTL_R8(hw, 0xF1) | BIT_7);
	else
		RTL_W8(hw, 0xF1, RTL_R8(hw, 0xF1) & ~BIT_7);
}

void
rtl8125_enable_aspm_clkreq_lock(struct rtl8125_hw *hw, bool enable)
{
	switch (hw->mcfg) {
	case CFG_METHOD_2 ... CFG_METHOD_11:
		rtl8125_enable_cfg9346_write(hw);
		if (enable) {
			RTL_W8(hw, Config2, RTL_R8(hw, Config2) | BIT_7);
			RTL_W8(hw, Config5, RTL_R8(hw, Config5) | BIT_0);
		} else {
			RTL_W8(hw, Config2, RTL_R8(hw, Config2) & ~BIT_7);
			RTL_W8(hw, Config5, RTL_R8(hw, Config5) & ~BIT_0);
		}
		rtl8125_disable_cfg9346_write(hw);
		break;
	}
}

void
rtl8125_hw_disable_mac_mcu_bps(struct rtl8125_hw *hw)
{
	u16 reg_addr;

	rtl8125_enable_aspm_clkreq_lock(hw, 0);

	switch (hw->mcfg) {
	case CFG_METHOD_2 ... CFG_METHOD_11:
		rtl8125_mac_ocp_write(hw, 0xFC48, 0x0000);
		break;
	}

	switch (hw->mcfg) {
	case CFG_METHOD_2 ... CFG_METHOD_11:
		for (reg_addr = 0xFC28; reg_addr < 0xFC48; reg_addr += 2)
			rtl8125_mac_ocp_write(hw, reg_addr, 0x0000);

		mdelay(3);

		rtl8125_mac_ocp_write(hw, 0xFC26, 0x0000);
		break;
	}
}

static void
rtl8125_enable_rxdvgate(struct rtl8125_hw *hw)
{
	switch (hw->mcfg) {
	case CFG_METHOD_2 ... CFG_METHOD_11:
		RTL_W8(hw, 0xF2, RTL_R8(hw, 0xF2) | BIT_3);
		mdelay(2);
	}
}

void
rtl8125_disable_rxdvgate(struct rtl8125_hw *hw)
{
	switch (hw->mcfg) {
	case CFG_METHOD_2 ... CFG_METHOD_11:
		RTL_W8(hw, 0xF2, RTL_R8(hw, 0xF2) & ~BIT_3);
		mdelay(2);
	}
}

void
rtl8125_nic_reset(struct rtl8125_hw *hw)
{
	int i;

	RTL_W32(hw, RxConfig, (RX_DMA_BURST_unlimited << RxCfgDMAShift));

	rtl8125_enable_rxdvgate(hw);

	rtl8125_stop_all_request(hw);

	rtl8125_wait_txrx_fifo_empty(hw);

	mdelay(2);

	/* Soft reset the chip. */
	RTL_W8(hw, ChipCmd, CmdReset);

	/* Check that the chip has finished the reset. */
	for (i = 100; i > 0; i--) {
		udelay(100);
		if ((RTL_R8(hw, ChipCmd) & CmdReset) == 0)
			break;
	}
}

static void
rtl8125_disable_rx_packet_filter(struct rtl8125_hw *hw)
{
	RTL_W32(hw, RxConfig, RTL_R32(hw, RxConfig) &
	        ~(AcceptErr | AcceptRunt | AcceptBroadcast | AcceptMulticast |
	          AcceptMyPhys | AcceptAllPhys));
}

static void
rtl8125_exit_realwow(struct rtl8125_hw *hw)
{
	/* Disable realwow function */
	switch (hw->mcfg) {
	case CFG_METHOD_2 ... CFG_METHOD_11:
		rtl8125_mac_ocp_write(hw, 0xC0BC, 0x00FF);
		break;
	}
}

void
rtl8125_exit_oob(struct rtl8125_hw *hw)
{
	u16 data16;

	rtl8125_disable_rx_packet_filter(hw);

	if (HW_DASH_SUPPORT_DASH(hw)) {
		rtl8125_driver_start(hw);
		rtl8125_dash2_disable_txrx(hw);
	}

	rtl8125_exit_realwow(hw);

	rtl8125_nic_reset(hw);

	switch (hw->mcfg) {
	case CFG_METHOD_2 ... CFG_METHOD_11:
		rtl8125_disable_now_is_oob(hw);

		data16 = rtl8125_mac_ocp_read(hw, 0xE8DE) & ~BIT_14;
		rtl8125_mac_ocp_write(hw, 0xE8DE, data16);
		rtl8125_wait_ll_share_fifo_ready(hw);

		rtl8125_mac_ocp_write(hw, 0xC0AA, 0x07D0);

		rtl8125_mac_ocp_write(hw, 0xC0A6, 0x01B5);

		rtl8125_mac_ocp_write(hw, 0xC01E, 0x5555);

		rtl8125_wait_ll_share_fifo_ready(hw);
		break;
	}
}

void
rtl8125_disable_ups(struct rtl8125_hw *hw)
{
	switch (hw->mcfg) {
	case CFG_METHOD_2 ... CFG_METHOD_11:
		rtl8125_mac_ocp_write(hw, 0xD40A, rtl8125_mac_ocp_read(hw, 0xD40A) & ~(BIT_4));
	}
}

static bool
rtl8125_is_autoneg_mode_valid(u32 autoneg)
{
	switch (autoneg) {
	case AUTONEG_ENABLE:
	case AUTONEG_DISABLE:
		return true;
	default:
		return false;
	}
}

bool
rtl8125_is_speed_mode_valid(u32 speed)
{
	switch (speed) {
	case SPEED_2500:
	case SPEED_1000:
	case SPEED_100:
	case SPEED_10:
		return true;
	default:
		return false;
	}
}

static bool
rtl8125_is_duplex_mode_valid(u8 duplex)
{
	switch (duplex) {
	case DUPLEX_FULL:
	case DUPLEX_HALF:
		return true;
	default:
		return false;
	}
}

static void
rtl8125_set_link_option(struct rtl8125_hw *hw,
                        u8 autoneg,
                        u32 speed,
                        u8 duplex,
                        enum rtl8125_fc_mode fc)
{
	u64 adv;

	if (!rtl8125_is_speed_mode_valid(speed))
		speed = SPEED_2500;

	if (!rtl8125_is_duplex_mode_valid(duplex))
		duplex = DUPLEX_FULL;

	if (!rtl8125_is_autoneg_mode_valid(autoneg))
		autoneg = AUTONEG_ENABLE;

	speed = RTE_MIN(speed, hw->HwSuppMaxPhyLinkSpeed);

	adv = 0;
	switch (speed) {
	case SPEED_2500:
		adv |= ADVERTISE_2500_FULL;
	/* Fall through */
	default:
		adv |= (ADVERTISE_10_HALF | ADVERTISE_10_FULL |
		        ADVERTISE_100_HALF | ADVERTISE_100_FULL |
		        ADVERTISE_1000_HALF | ADVERTISE_1000_FULL);
		break;
	}

	hw->autoneg = autoneg;
	hw->speed = speed;
	hw->duplex = duplex;
	hw->advertising = adv;
	hw->fcpause = fc;
}

void
rtl8125_init_software_variable(struct rtl8125_hw *hw)
{
	int tx_no_close_enable = 1;
	unsigned int speed_mode = SPEED_2500;
	unsigned int duplex_mode = DUPLEX_FULL;
	unsigned int autoneg_mode = AUTONEG_ENABLE;
	u8 tmp;

	switch (hw->mcfg) {
	case CFG_METHOD_2:
	case CFG_METHOD_3:
		tmp = (u8)rtl8125_mac_ocp_read(hw, 0xD006);
		if (tmp == 0x02 || tmp == 0x04)
			hw->HwSuppDashVer = 2;
		break;
	case CFG_METHOD_8:
	case CFG_METHOD_9:
		hw->HwSuppDashVer = 4;
		break;
	default:
		hw->HwSuppDashVer = 0;
		break;
	}

	switch (hw->mcfg) {
	case CFG_METHOD_2:
	case CFG_METHOD_3:
		if (HW_DASH_SUPPORT_DASH(hw))
			hw->HwSuppOcpChannelVer = 2;
		break;
	case CFG_METHOD_8:
	case CFG_METHOD_9:
		hw->HwSuppOcpChannelVer = 2;
		break;
	}

	hw->AllowAccessDashOcp = rtl8125_is_allow_access_dash_ocp(hw);

	if (HW_DASH_SUPPORT_DASH(hw) && rtl8125_check_dash(hw))
		hw->DASH = 1;
	else
		hw->DASH = 0;

	if (HW_DASH_SUPPORT_TYPE_2(hw))
		hw->cmac_ioaddr = hw->mmio_addr;

	switch (hw->mcfg) {
	case CFG_METHOD_2:
	case CFG_METHOD_3:
		hw->chipset_name = RTL8125A;
		break;
	case CFG_METHOD_4:
	case CFG_METHOD_5:
		hw->chipset_name = RTL8125B;
		break;
	case CFG_METHOD_6:
	case CFG_METHOD_7:
		hw->chipset_name = RTL8168KB;
		break;
	case CFG_METHOD_8:
	case CFG_METHOD_9:
		hw->chipset_name = RTL8125BP;
		break;
	case CFG_METHOD_10:
	case CFG_METHOD_11:
		hw->chipset_name = RTL8125D;
		break;
	default:
		hw->chipset_name = UNKNOWN;
		break;
	}

	switch (hw->mcfg) {
	case CFG_METHOD_2 ... CFG_METHOD_11:
		hw->HwSuppNowIsOobVer = 1;
		break;
	}

	switch (hw->mcfg) {
	case CFG_METHOD_2 ... CFG_METHOD_11:
		hw->HwSuppCheckPhyDisableModeVer = 3;
		break;
	}

	switch (hw->mcfg) {
	case CFG_METHOD_2:
	case CFG_METHOD_3:
	case CFG_METHOD_4:
	case CFG_METHOD_5:
	case CFG_METHOD_8:
	case CFG_METHOD_9:
	case CFG_METHOD_10:
	case CFG_METHOD_11:
		hw->HwSuppMaxPhyLinkSpeed = 2500;
		break;
	default:
		hw->HwSuppMaxPhyLinkSpeed = 1000;
		break;
	}

	switch (hw->mcfg) {
	case CFG_METHOD_2 ... CFG_METHOD_7:
		hw->HwSuppTxNoCloseVer = 3;
		break;
	case CFG_METHOD_8 ... CFG_METHOD_11:
		hw->HwSuppTxNoCloseVer = 6;
		break;
	}

	switch (hw->HwSuppTxNoCloseVer) {
	case 3:
		hw->MaxTxDescPtrMask = MAX_TX_NO_CLOSE_DESC_PTR_MASK_V2;
		hw->hw_clo_ptr_reg = HW_CLO_PTR0_8125;
		hw->sw_tail_ptr_reg = SW_TAIL_PTR0_8125;
		break;
	case 6:
		hw->MaxTxDescPtrMask = MAX_TX_NO_CLOSE_DESC_PTR_MASK_V4;
		hw->hw_clo_ptr_reg = HW_CLO_PTR0_8125BP;
		hw->sw_tail_ptr_reg = SW_TAIL_PTR0_8125BP;
		break;
	default:
		tx_no_close_enable = 0;
		hw->hw_clo_ptr_reg = HW_CLO_PTR0_8125;
		hw->sw_tail_ptr_reg = SW_TAIL_PTR0_8125;
		break;
	}

	if (hw->HwSuppTxNoCloseVer > 0 && tx_no_close_enable == 1)
		hw->EnableTxNoClose = TRUE;

	switch (hw->mcfg) {
	case CFG_METHOD_2:
		hw->sw_ram_code_ver = NIC_RAMCODE_VERSION_CFG_METHOD_2;
		break;
	case CFG_METHOD_3:
	case CFG_METHOD_6:
		hw->sw_ram_code_ver = NIC_RAMCODE_VERSION_CFG_METHOD_3;
		break;
	case CFG_METHOD_4:
		hw->sw_ram_code_ver = NIC_RAMCODE_VERSION_CFG_METHOD_4;
		break;
	case CFG_METHOD_5:
	case CFG_METHOD_7:
		hw->sw_ram_code_ver = NIC_RAMCODE_VERSION_CFG_METHOD_5;
		break;
	case CFG_METHOD_8:
		hw->sw_ram_code_ver = NIC_RAMCODE_VERSION_CFG_METHOD_8;
		break;
	case CFG_METHOD_9:
		hw->sw_ram_code_ver = NIC_RAMCODE_VERSION_CFG_METHOD_9;
		break;
	case CFG_METHOD_10:
		hw->sw_ram_code_ver = NIC_RAMCODE_VERSION_CFG_METHOD_10;
		break;
	case CFG_METHOD_11:
		hw->sw_ram_code_ver = NIC_RAMCODE_VERSION_CFG_METHOD_11;
		break;
	}

	if (hw->HwIcVerUnknown) {
		hw->NotWrRamCodeToMicroP = TRUE;
		hw->NotWrMcuPatchCode = TRUE;
	}

	switch (hw->mcfg) {
	case CFG_METHOD_3:
	case CFG_METHOD_6:
		if ((rtl8125_mac_ocp_read(hw, 0xD442) & BIT_5) &&
		    (rtl8125_mdio_direct_read_phy_ocp(hw, 0xD068) & BIT_1))
			hw->RequirePhyMdiSwapPatch = TRUE;
		break;
	}

	switch (hw->mcfg) {
	case CFG_METHOD_2 ... CFG_METHOD_11:
		hw->HwSuppMacMcuVer = 2;
		break;
	}

	switch (hw->mcfg) {
	case CFG_METHOD_2 ... CFG_METHOD_11:
		hw->MacMcuPageSize = RTL8125_MAC_MCU_PAGE_SIZE;
		break;
	}

	switch (hw->mcfg) {
	case CFG_METHOD_2:
	case CFG_METHOD_3:
	case CFG_METHOD_6:
		hw->HwSuppIntMitiVer = 3;
		break;
	case CFG_METHOD_4:
	case CFG_METHOD_5:
	case CFG_METHOD_7:
		hw->HwSuppIntMitiVer = 4;
		break;
	case CFG_METHOD_8:
	case CFG_METHOD_9:
	case CFG_METHOD_10:
	case CFG_METHOD_11:
		hw->HwSuppIntMitiVer = 6;
		break;
	}

	rtl8125_set_link_option(hw, autoneg_mode, speed_mode, duplex_mode,
	                        rtl8125_fc_full);

	switch (hw->mcfg) {
	case CFG_METHOD_2 ... CFG_METHOD_11:
		hw->mcu_pme_setting = rtl8125_mac_ocp_read(hw, 0xE00A);
		break;
	}
}

void
rtl8125_get_tally_stats(struct rtl8125_hw *hw, struct rte_eth_stats *rte_stats)
{
	struct rtl8125_counters *counters;
	uint64_t paddr;
	u32 cmd;
	u32 wait_cnt;

	counters = hw->tally_vaddr;
	paddr = hw->tally_paddr;
	if (!counters)
		return;

	RTL_W32(hw, CounterAddrHigh, (u64)paddr >> 32);
	cmd = (u64)paddr & DMA_BIT_MASK(32);
	RTL_W32(hw, CounterAddrLow, cmd);
	RTL_W32(hw, CounterAddrLow, cmd | CounterDump);

	wait_cnt = 0;
	while (RTL_R32(hw, CounterAddrLow) & CounterDump) {
		udelay(10);

		wait_cnt++;
		if (wait_cnt > 20)
			break;
	}

	/* RX errors */
	rte_stats->imissed = rte_le_to_cpu_64(counters->rx_missed);
	rte_stats->ierrors = rte_le_to_cpu_64(counters->rx_errors);

	/* TX errors */
	rte_stats->oerrors = rte_le_to_cpu_64(counters->tx_errors);

	rte_stats->ipackets = rte_le_to_cpu_64(counters->rx_packets);
	rte_stats->opackets = rte_le_to_cpu_64(counters->tx_packets);
}

void
rtl8125_clear_tally_stats(struct rtl8125_hw *hw)
{
	if (!hw->tally_paddr)
		return;

	RTL_W32(hw, CounterAddrHigh, (u64)hw->tally_paddr >> 32);
	RTL_W32(hw, CounterAddrLow,
	        ((u64)hw->tally_paddr & (DMA_BIT_MASK(32))) | CounterReset);
}

static void
rtl8125_switch_mac_mcu_ram_code_page(struct rtl8125_hw *hw, u16 page)
{
	u16 tmp_ushort;

	page &= (BIT_1 | BIT_0);
	tmp_ushort = rtl8125_mac_ocp_read(hw, 0xE446);
	tmp_ushort &= ~(BIT_1 | BIT_0);
	tmp_ushort |= page;
	rtl8125_mac_ocp_write(hw, 0xE446, tmp_ushort);
}

static void
_rtl8125_write_mac_mcu_ram_code(struct rtl8125_hw *hw, const u16 *entry,
                                u16 entry_cnt)
{
	u16 i;

	for (i = 0; i < entry_cnt; i++)
		rtl8125_mac_ocp_write(hw, 0xF800 + i * 2, entry[i]);
}

static void
_rtl8125_write_mac_mcu_ram_code_with_page(struct rtl8125_hw *hw,
                const u16 *entry, u16 entry_cnt, u16 page_size)
{
	u16 i;
	u16 offset;

	if (page_size == 0) return;

	for (i = 0; i < entry_cnt; i++) {
		offset = i % page_size;
		if (offset == 0) {
			u16 page = (i / page_size);
			rtl8125_switch_mac_mcu_ram_code_page(hw, page);
		}
		rtl8125_mac_ocp_write(hw, 0xF800 + offset * 2, entry[i]);
	}
}

void
rtl8125_write_mac_mcu_ram_code(struct rtl8125_hw *hw, const u16 *entry,
                               u16 entry_cnt)
{
	if (FALSE == HW_SUPPORT_MAC_MCU(hw))
		return;
	if (entry == NULL || entry_cnt == 0)
		return;

	if (hw->MacMcuPageSize > 0)
		_rtl8125_write_mac_mcu_ram_code_with_page(hw, entry, entry_cnt,
		                hw->MacMcuPageSize);
	else
		_rtl8125_write_mac_mcu_ram_code(hw, entry, entry_cnt);
}

static u32
rtl8125_eri_read_with_oob_base_address(struct rtl8125_hw *hw, int addr, int len,
                                       int type, const u32 base_address)
{
	int i, val_shift, shift = 0;
	u32 value1 = 0;
	u32 value2 = 0;
	u32 eri_cmd, tmp, mask;
	const u32 transformed_base_address = ((base_address & 0x00FFF000) << 6) |
	                                     (base_address & 0x000FFF);

	if (len > 4 || len <= 0)
		return -1;

	while (len > 0) {
		val_shift = addr % ERIAR_Addr_Align;
		addr = addr & ~0x3;

		eri_cmd = ERIAR_Read | transformed_base_address |
		          type << ERIAR_Type_shift |
		          ERIAR_ByteEn << ERIAR_ByteEn_shift |
		          (addr & 0x0FFF);
		if (addr & 0xF000) {
			tmp = addr & 0xF000;
			tmp >>= 12;
			eri_cmd |= (tmp << 20) & 0x00F00000;
		}

		RTL_W32(hw, ERIAR, eri_cmd);

		for (i = 0; i < R8125_CHANNEL_WAIT_COUNT; i++) {
			udelay(R8125_CHANNEL_WAIT_TIME);

			/* Check if the RTL8125 has completed ERI read */
			if (RTL_R32(hw, ERIAR) & ERIAR_Flag)
				break;
		}

		if (len == 1)       mask = (0xFF << (val_shift * 8)) & 0xFFFFFFFF;
		else if (len == 2)  mask = (0xFFFF << (val_shift * 8)) & 0xFFFFFFFF;
		else if (len == 3)  mask = (0xFFFFFF << (val_shift * 8)) & 0xFFFFFFFF;
		else            mask = (0xFFFFFFFF << (val_shift * 8)) & 0xFFFFFFFF;

		value1 = RTL_R32(hw, ERIDR) & mask;
		value2 |= (value1 >> val_shift * 8) << shift * 8;

		if (len <= 4 - val_shift)
			len = 0;
		else {
			len -= (4 - val_shift);
			shift = 4 - val_shift;
			addr += 4;
		}
	}

	udelay(R8125_CHANNEL_EXIT_DELAY_TIME);

	return value2;
}

static int
rtl8125_eri_write_with_oob_base_address(struct rtl8125_hw *hw, int addr,
                                        int len, u32 value, int type, const u32 base_address)
{
	int i, val_shift, shift = 0;
	u32 value1 = 0;
	u32 eri_cmd, mask, tmp;
	const u32 transformed_base_address = ((base_address & 0x00FFF000) << 6) |
	                                     (base_address & 0x000FFF);

	if (len > 4 || len <= 0)
		return -1;

	while (len > 0) {
		val_shift = addr % ERIAR_Addr_Align;
		addr = addr & ~0x3;

		if (len == 1)       mask = (0xFF << (val_shift * 8)) & 0xFFFFFFFF;
		else if (len == 2)  mask = (0xFFFF << (val_shift * 8)) & 0xFFFFFFFF;
		else if (len == 3)  mask = (0xFFFFFF << (val_shift * 8)) & 0xFFFFFFFF;
		else            mask = (0xFFFFFFFF << (val_shift * 8)) & 0xFFFFFFFF;

		value1 = rtl8125_eri_read_with_oob_base_address(hw, addr, 4, type,
		                base_address) & ~mask;
		value1 |= ((value << val_shift * 8) >> shift * 8);

		RTL_W32(hw, ERIDR, value1);

		eri_cmd = ERIAR_Write | transformed_base_address |
		          type << ERIAR_Type_shift |
		          ERIAR_ByteEn << ERIAR_ByteEn_shift |
		          (addr & 0x0FFF);
		if (addr & 0xF000) {
			tmp = addr & 0xF000;
			tmp >>= 12;
			eri_cmd |= (tmp << 20) & 0x00F00000;
		}

		RTL_W32(hw, ERIAR, eri_cmd);

		for (i = 0; i < R8125_CHANNEL_WAIT_COUNT; i++) {
			udelay(R8125_CHANNEL_WAIT_TIME);

			/* Check if the RTL8125 has completed ERI write */
			if (!(RTL_R32(hw, ERIAR) & ERIAR_Flag))
				break;
		}

		if (len <= 4 - val_shift)
			len = 0;
		else {
			len -= (4 - val_shift);
			shift = 4 - val_shift;
			addr += 4;
		}
	}

	udelay(R8125_CHANNEL_EXIT_DELAY_TIME);

	return 0;
}

static u32
rtl8125_ocp_read_with_oob_base_address(struct rtl8125_hw *hw, u16 addr, u8 len,
                                       const u32 base_address)
{
	return rtl8125_eri_read_with_oob_base_address(hw, addr, len, ERIAR_OOB,
	                base_address);
}

u32
rtl8125_ocp_read(struct rtl8125_hw *hw, u16 addr, u8 len)
{
	u32 value = 0;

	if (!hw->AllowAccessDashOcp)
		return 0xffffffff;

	if (hw->HwSuppOcpChannelVer == 2)
		value = rtl8125_ocp_read_with_oob_base_address(hw, addr, len, NO_BASE_ADDRESS);

	return value;
}

static u32
rtl8125_ocp_write_with_oob_base_address(struct rtl8125_hw *hw, u16 addr, u8 len,
                                        u32 value, const u32 base_address)
{
	return rtl8125_eri_write_with_oob_base_address(hw, addr, len, value, ERIAR_OOB,
	                base_address);
}

void
rtl8125_ocp_write(struct rtl8125_hw *hw, u16 addr, u8 len, u32 value)
{
	if (!hw->AllowAccessDashOcp)
		return;

	if (hw->HwSuppOcpChannelVer == 2)
		rtl8125_ocp_write_with_oob_base_address(hw, addr, len, value, NO_BASE_ADDRESS);
}

void
rtl8125_oob_mutex_lock(struct rtl8125_hw *hw)
{
	u8 reg_16, reg_a0;
	u32 wait_cnt_0, wait_cnt_1;
	u16 ocp_reg_mutex_ib;
	u16 ocp_reg_mutex_oob;
	u16 ocp_reg_mutex_prio;

	if (!hw->DASH)
		return;

	switch (hw->mcfg) {
	case CFG_METHOD_2:
	case CFG_METHOD_3:
	case CFG_METHOD_6:
	case CFG_METHOD_8:
	case CFG_METHOD_9:
		ocp_reg_mutex_oob = 0x110;
		ocp_reg_mutex_ib = 0x114;
		ocp_reg_mutex_prio = 0x11C;
		break;
	default:
		return;
	}

	rtl8125_ocp_write(hw, ocp_reg_mutex_ib, 1, BIT_0);
	reg_16 = rtl8125_ocp_read(hw, ocp_reg_mutex_oob, 1);
	wait_cnt_0 = 0;
	while (reg_16) {
		reg_a0 = rtl8125_ocp_read(hw, ocp_reg_mutex_prio, 1);
		if (reg_a0) {
			rtl8125_ocp_write(hw, ocp_reg_mutex_ib, 1, 0x00);
			reg_a0 = rtl8125_ocp_read(hw, ocp_reg_mutex_prio, 1);
			wait_cnt_1 = 0;
			while (reg_a0) {
				reg_a0 = rtl8125_ocp_read(hw, ocp_reg_mutex_prio, 1);

				wait_cnt_1++;

				if (wait_cnt_1 > 2000)
					break;
			};
			rtl8125_ocp_write(hw, ocp_reg_mutex_ib, 1, BIT_0);

		}
		reg_16 = rtl8125_ocp_read(hw, ocp_reg_mutex_oob, 1);

		wait_cnt_0++;

		if (wait_cnt_0 > 2000)
			break;
	};
}

void
rtl8125_oob_mutex_unlock(struct rtl8125_hw *hw)
{
	u16 ocp_reg_mutex_ib;
	u16 ocp_reg_mutex_oob;
	u16 ocp_reg_mutex_prio;

	if (!hw->DASH)
		return;

	switch (hw->mcfg) {
	case CFG_METHOD_2:
	case CFG_METHOD_3:
	case CFG_METHOD_6:
	case CFG_METHOD_8:
	case CFG_METHOD_9:
		ocp_reg_mutex_oob = 0x110;
		ocp_reg_mutex_ib = 0x114;
		ocp_reg_mutex_prio = 0x11C;
		break;
	default:
		return;
	}

	rtl8125_ocp_write(hw, ocp_reg_mutex_prio, 1, BIT_0);
	rtl8125_ocp_write(hw, ocp_reg_mutex_ib, 1, 0x00);
}