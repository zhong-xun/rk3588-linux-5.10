/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020 Realtek Technologies Co., Ltd */

#include <sys/queue.h>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <stdarg.h>

#include <rte_ether.h>
#include <ethdev_driver.h>

#include "r8125_ethdev.h"
#include "r8125_hw.h"
#include "r8125_phy.h"
#include "r8125_logs.h"

static u16
rtl8125_map_phy_ocp_addr(u16 PageNum, u8 RegNum)
{
	u16 ocp_page_num = 0;
	u16 ocp_phy_address = 0;
	u8 ocp_reg_num = 0;


	if (PageNum == 0) {
		ocp_page_num = OCP_STD_PHY_BASE_PAGE + (RegNum / 8);
		ocp_reg_num = 0x10 + (RegNum % 8);
	} else {
		ocp_page_num = PageNum;
		ocp_reg_num = RegNum;
	}

	ocp_page_num <<= 4;

	if (ocp_reg_num < 16)
		ocp_phy_address = 0;

	else {
		ocp_reg_num -= 16;
		ocp_reg_num <<= 1;

		ocp_phy_address = ocp_page_num + ocp_reg_num;
	}


	return ocp_phy_address;
}

static u32
rtl8125_mdio_real_read_phy_ocp(struct rtl8125_hw *hw, u32 RegAddr)
{
	u32 data32;
	int i, value = 0;

	data32 = RegAddr / 2;
	data32 <<= OCPR_Addr_Reg_shift;

	RTL_W32(hw, PHYOCP, data32);
	for (i = 0; i < 100; i++) {
		udelay(1);

		if (RTL_R32(hw, PHYOCP) & OCPR_Flag)
			break;
	}
	value = RTL_R32(hw, PHYOCP) & OCPDR_Data_Mask;

	return value;
}

u32
rtl8125_mdio_direct_read_phy_ocp(struct rtl8125_hw *hw, u32 RegAddr)
{
	return rtl8125_mdio_real_read_phy_ocp(hw, RegAddr);
}

static u32
rtl8125_mdio_read_phy_ocp(struct rtl8125_hw *hw, u16 PageNum, u32 RegAddr)
{
	u16 ocp_addr;

	ocp_addr = rtl8125_map_phy_ocp_addr(PageNum, RegAddr);

	return rtl8125_mdio_direct_read_phy_ocp(hw, ocp_addr);
}

static u32
rtl8125_mdio_real_read(struct rtl8125_hw *hw, u32 RegAddr)
{
	return rtl8125_mdio_read_phy_ocp(hw, hw->cur_page, RegAddr);
}

static void
rtl8125_mdio_real_write_phy_ocp(struct rtl8125_hw *hw, u32 RegAddr, u32 value)
{
	u32 data32;
	int i;

	data32 = RegAddr / 2;
	data32 <<= OCPR_Addr_Reg_shift;
	data32 |= OCPR_Write | value;

	RTL_W32(hw, PHYOCP, data32);
	for (i = 0; i < 100; i++) {
		udelay(1);

		if (!(RTL_R32(hw, PHYOCP) & OCPR_Flag))
			break;
	}
}

void
rtl8125_mdio_direct_write_phy_ocp(struct rtl8125_hw *hw, u32 RegAddr, u32 value)
{
	rtl8125_mdio_real_write_phy_ocp(hw, RegAddr, value);
}

static void
rtl8125_mdio_write_phy_ocp(struct rtl8125_hw *hw, u16 PageNum, u32 RegAddr,
                           u32 value)
{
	u16 ocp_addr;

	ocp_addr = rtl8125_map_phy_ocp_addr(PageNum, RegAddr);

	rtl8125_mdio_direct_write_phy_ocp(hw, ocp_addr, value);
}

static void
rtl8125_mdio_real_write(struct rtl8125_hw *hw, u32 RegAddr, u32 value)
{
	if (RegAddr == 0x1F)
		hw->cur_page = value;
	rtl8125_mdio_write_phy_ocp(hw, hw->cur_page, RegAddr, value);
}

u32
rtl8125_mdio_read(struct rtl8125_hw *hw, u32 RegAddr)
{
	return rtl8125_mdio_real_read(hw, RegAddr);
}

void
rtl8125_mdio_write(struct rtl8125_hw *hw, u32 RegAddr, u32 value)
{
	rtl8125_mdio_real_write(hw, RegAddr, value);
}

void
rtl8125_clear_and_set_eth_phy_ocp_bit(struct rtl8125_hw *hw, u16 addr,
                                      u16 clearmask, u16 setmask)
{
	u16 phy_reg_value;

	phy_reg_value = rtl8125_mdio_direct_read_phy_ocp(hw, addr);
	phy_reg_value &= ~clearmask;
	phy_reg_value |= setmask;
	rtl8125_mdio_direct_write_phy_ocp(hw, addr, phy_reg_value);
}

void
rtl8125_clear_eth_phy_ocp_bit(struct rtl8125_hw *hw, u16 addr, u16 mask)
{
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, addr, mask, 0);
}

void
rtl8125_set_eth_phy_ocp_bit(struct rtl8125_hw *hw, u16 addr, u16 mask)
{
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, addr, 0, mask);
}

void
rtl8125_ephy_write(struct rtl8125_hw *hw, int addr, int value)
{
	int i;

	RTL_W32(hw, EPHYAR, EPHYAR_Write |
	        (addr & EPHYAR_Reg_Mask_v2) << EPHYAR_Reg_shift |
	        (value & EPHYAR_Data_Mask));

	for (i = 0; i < 10; i++) {
		udelay(100);

		/* Check if the RTL8125 has completed EPHY write */
		if (!(RTL_R32(hw, EPHYAR) & EPHYAR_Flag))
			break;
	}

	udelay(20);
}

static u16
rtl8125_ephy_read(struct rtl8125_hw *hw, int addr)
{
	int i;
	u16 value = 0xffff;

	RTL_W32(hw, EPHYAR, EPHYAR_Read |
	        (addr & EPHYAR_Reg_Mask_v2) << EPHYAR_Reg_shift);

	for (i = 0; i < 10; i++) {
		udelay(100);

		/* Check if the RTL8125 has completed EPHY read */
		if (RTL_R32(hw, EPHYAR) & EPHYAR_Flag) {
			value = (u16)(RTL_R32(hw, EPHYAR) & EPHYAR_Data_Mask);
			break;
		}
	}

	udelay(20);

	return value;
}

void
rtl8125_clear_and_set_pcie_phy_bit(struct rtl8125_hw *hw, u8 addr,
                                   u16 clearmask, u16 setmask)
{
	u16 ephy_value;

	ephy_value = rtl8125_ephy_read(hw, addr);
	ephy_value &= ~clearmask;
	ephy_value |= setmask;
	rtl8125_ephy_write(hw, addr, ephy_value);
}

void
rtl8125_clear_pcie_phy_bit(struct rtl8125_hw *hw, u8 addr, u16 mask)
{
	rtl8125_clear_and_set_pcie_phy_bit(hw, addr, mask, 0);
}

void
rtl8125_set_pcie_phy_bit(struct rtl8125_hw *hw, u8 addr, u16 mask)
{
	rtl8125_clear_and_set_pcie_phy_bit(hw, addr, 0, mask);
}

static void
rtl8125_clear_set_mac_ocp_bit(struct rtl8125_hw *hw, u16 addr,
                              u16 clearmask, u16 setmask)
{
	u16 phy_reg_value;

	phy_reg_value = rtl8125_mac_ocp_read(hw, addr);
	phy_reg_value &= ~clearmask;
	phy_reg_value |= setmask;
	rtl8125_mac_ocp_write(hw, addr, phy_reg_value);
}

void
rtl8125_clear_mac_ocp_bit(struct rtl8125_hw *hw, u16 addr, u16 mask)
{
	rtl8125_clear_set_mac_ocp_bit(hw, addr, mask, 0);
}

void
rtl8125_set_mac_ocp_bit(struct rtl8125_hw *hw, u16 addr, u16 mask)
{
	rtl8125_clear_set_mac_ocp_bit(hw, addr, 0, mask);
}

static u16
rtl8125_get_hw_phy_mcu_code_ver(struct rtl8125_hw *hw)
{
	u16 hw_ram_code_ver = ~0;

	switch (hw->mcfg) {
	case CFG_METHOD_2 ... CFG_METHOD_11:
		rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x801E);
		hw_ram_code_ver = rtl8125_mdio_direct_read_phy_ocp(hw, 0xA438);
		break;
	}

	return hw_ram_code_ver;
}

static int
rtl8125_check_hw_phy_mcu_code_ver(struct rtl8125_hw *hw)
{
	int ram_code_ver_match = 0;

	hw->hw_ram_code_ver = rtl8125_get_hw_phy_mcu_code_ver(hw);

	if (hw->hw_ram_code_ver == hw->sw_ram_code_ver) {
		ram_code_ver_match = 1;
		hw->HwHasWrRamCodeToMicroP = TRUE;
	} else
		hw->HwHasWrRamCodeToMicroP = FALSE;

	return ram_code_ver_match;
}

static void
rtl8125_write_hw_phy_mcu_code_ver(struct rtl8125_hw *hw)
{
	switch (hw->mcfg) {
	case CFG_METHOD_2 ... CFG_METHOD_11:
		rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x801E);
		rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, hw->sw_ram_code_ver);
		hw->hw_ram_code_ver = hw->sw_ram_code_ver;
		break;
	}
}

bool
rtl8125_set_phy_mcu_patch_request(struct rtl8125_hw *hw)
{
	u16 gphy_val;
	u16 wait_cnt;
	bool bool_success = TRUE;

	rtl8125_set_eth_phy_ocp_bit(hw, 0xB820, BIT_4);

	wait_cnt = 0;
	do {
		gphy_val = rtl8125_mdio_direct_read_phy_ocp(hw, 0xB800);
		udelay(100);
		wait_cnt++;
	} while (!(gphy_val & BIT_6) && (wait_cnt < 1000));

	if (!(gphy_val & BIT_6) && (wait_cnt == 1000))
		bool_success = FALSE;

	if (!bool_success)
		PMD_INIT_LOG(NOTICE, "rtl8125_set_phy_mcu_patch_request fail.");

	return bool_success;
}

bool
rtl8125_clear_phy_mcu_patch_request(struct rtl8125_hw *hw)
{
	u16 gphy_val;
	u16 wait_cnt;
	bool bool_success = TRUE;

	rtl8125_clear_eth_phy_ocp_bit(hw, 0xB820, BIT_4);

	wait_cnt = 0;
	do {
		gphy_val = rtl8125_mdio_direct_read_phy_ocp(hw, 0xB800);
		udelay(100);
		wait_cnt++;
	} while ((gphy_val & BIT_6) && (wait_cnt < 1000));

	if ((gphy_val & BIT_6) && (wait_cnt == 1000)) bool_success = FALSE;

	if (!bool_success)
		PMD_INIT_LOG(NOTICE, "rtl8125_clear_phy_mcu_patch_request fail.");

	return bool_success;
}

void
rtl8125_disable_ocp_phy_power_saving(struct rtl8125_hw *hw)
{
	u16 val;

	if (hw->mcfg == CFG_METHOD_2 || hw->mcfg == CFG_METHOD_3 ||
	    hw->mcfg == CFG_METHOD_6) {
		val = rtl8125_mdio_direct_read_phy_ocp(hw, 0xC416);
		if (val != 0x0050) {
			rtl8125_set_phy_mcu_patch_request(hw);
			rtl8125_mdio_direct_write_phy_ocp(hw, 0xC416, 0x0000);
			rtl8125_mdio_direct_write_phy_ocp(hw, 0xC416, 0x0500);
			rtl8125_clear_phy_mcu_patch_request(hw);
		}
	}
}

static void
rtl8125_enable_phy_disable_mode(struct rtl8125_hw *hw)
{
	switch (hw->HwSuppCheckPhyDisableModeVer) {
	case 3:
		RTL_W8(hw, 0xF2, RTL_R8(hw, 0xF2) | BIT_5);
		break;
	}
}

static void
rtl8125_disable_phy_disable_mode(struct rtl8125_hw *hw)
{
	switch (hw->HwSuppCheckPhyDisableModeVer) {
	case 3:
		RTL_W8(hw, 0xF2, RTL_R8(hw, 0xF2) & ~BIT_5);
		break;
	}

	mdelay(1);
}

static u8
rtl8125_is_phy_disable_mode_enabled(struct rtl8125_hw *hw)
{
	u8 phy_disable_mode_enabled = FALSE;

	switch (hw->HwSuppCheckPhyDisableModeVer) {
	case 3:
		if (RTL_R8(hw, 0xF2) & BIT_5)
			phy_disable_mode_enabled = TRUE;
		break;
	}

	return phy_disable_mode_enabled;
}

static u8
rtl8125_is_gpio_low(struct rtl8125_hw *hw)
{
	u8 gpio_low = FALSE;

	switch (hw->HwSuppCheckPhyDisableModeVer) {
	case 3:
		if (!(rtl8125_mac_ocp_read(hw, 0xDC04) & BIT_13))
			gpio_low = TRUE;
		break;
	}

	return gpio_low;
}

static u8
rtl8125_is_in_phy_disable_mode(struct rtl8125_hw *hw)
{
	u8 in_phy_disable_mode = FALSE;

	if (rtl8125_is_phy_disable_mode_enabled(hw) && rtl8125_is_gpio_low(hw))
		in_phy_disable_mode = TRUE;

	return in_phy_disable_mode;
}

static void
rtl8125_init_hw_phy_mcu(struct rtl8125_hw *hw)
{
	u8 require_disable_phy_disable_mode = FALSE;

	if (hw->NotWrRamCodeToMicroP == TRUE)
		return;

	if (rtl8125_check_hw_phy_mcu_code_ver(hw))
		return;

	if (HW_SUPPORT_CHECK_PHY_DISABLE_MODE(hw) && rtl8125_is_in_phy_disable_mode(hw))
		require_disable_phy_disable_mode = TRUE;

	if (require_disable_phy_disable_mode)
		rtl8125_disable_phy_disable_mode(hw);

	hw->hw_ops.hw_phy_mcu_config(hw);

	if (require_disable_phy_disable_mode)
		rtl8125_enable_phy_disable_mode(hw);

	rtl8125_write_hw_phy_mcu_code_ver(hw);

	rtl8125_mdio_write(hw, 0x1F, 0x0000);

	hw->HwHasWrRamCodeToMicroP = TRUE;
}

static int
rtl8125_wait_phy_reset_complete(struct rtl8125_hw *hw)
{
	int i, val;

	for (i = 0; i < 2500; i++) {
		val = rtl8125_mdio_read(hw, MII_BMCR) & BMCR_RESET;
		if (!val)
			return 0;

		mdelay(1);
	}

	return -1;
}

static void
rtl8125_xmii_reset_enable(struct rtl8125_hw *hw)
{
	if (rtl8125_is_in_phy_disable_mode(hw))
		return;

	rtl8125_mdio_write(hw, 0x1f, 0x0000);
	rtl8125_mdio_write(hw, MII_ADVERTISE, rtl8125_mdio_read(hw, MII_ADVERTISE) &
	                   ~(ADVERTISE_10HALF | ADVERTISE_10FULL |
	                     ADVERTISE_100HALF | ADVERTISE_100FULL));
	rtl8125_mdio_write(hw, MII_CTRL1000, rtl8125_mdio_read(hw, MII_CTRL1000) &
	                   ~(ADVERTISE_1000HALF | ADVERTISE_1000FULL));
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA5D4,
	                                  rtl8125_mdio_direct_read_phy_ocp(hw, 0xA5D4) & ~(RTK_ADVERTISE_2500FULL));
	rtl8125_mdio_write(hw, MII_BMCR, BMCR_RESET | BMCR_ANENABLE);

	if (rtl8125_wait_phy_reset_complete(hw) == 0)
		return;
}

static void
rtl8125_phy_restart_nway(struct rtl8125_hw *hw)
{
	if (rtl8125_is_in_phy_disable_mode(hw))
		return;

	rtl8125_mdio_write(hw, 0x1F, 0x0000);
	rtl8125_mdio_write(hw, MII_BMCR, BMCR_ANENABLE | BMCR_ANRESTART);
}

static void
rtl8125_phy_setup_force_mode(struct rtl8125_hw *hw, u32 speed, u8 duplex)
{
	u16 bmcr_true_force = 0;

	if (rtl8125_is_in_phy_disable_mode(hw)) return;

	if ((speed == SPEED_10) && (duplex == DUPLEX_HALF))
		bmcr_true_force = BMCR_SPEED10;

	else if ((speed == SPEED_10) && (duplex == DUPLEX_FULL))
		bmcr_true_force = BMCR_SPEED10 | BMCR_FULLDPLX;

	else if ((speed == SPEED_100) && (duplex == DUPLEX_HALF))
		bmcr_true_force = BMCR_SPEED100;

	else if ((speed == SPEED_100) && (duplex == DUPLEX_FULL))
		bmcr_true_force = BMCR_SPEED100 | BMCR_FULLDPLX;

	else
		return;

	rtl8125_mdio_write(hw, 0x1F, 0x0000);
	rtl8125_mdio_write(hw, MII_BMCR, bmcr_true_force);
}

static int
rtl8125_set_speed_xmii(struct rtl8125_hw *hw, u8 autoneg, u32 speed, u8 duplex,
                       u32 adv)
{
	int auto_nego = 0;
	int giga_ctrl = 0;
	int ctrl_2500 = 0;
	int rc = -EINVAL;

	/* Disable giga lite */
	rtl8125_clear_eth_phy_ocp_bit(hw, 0xA428, BIT_9);
	rtl8125_clear_eth_phy_ocp_bit(hw, 0xA5EA, BIT_0);

	if (!rtl8125_is_speed_mode_valid(speed)) {
		speed = SPEED_2500;
		duplex = DUPLEX_FULL;
		adv |= hw->advertising;
	}

	giga_ctrl = rtl8125_mdio_read(hw, MII_CTRL1000);
	giga_ctrl &= ~(ADVERTISE_1000HALF | ADVERTISE_1000FULL);
	ctrl_2500 = rtl8125_mdio_direct_read_phy_ocp(hw, 0xA5D4);
	ctrl_2500 &= ~RTK_ADVERTISE_2500FULL;

	if (autoneg == AUTONEG_ENABLE) {
		/* N-way force */
		auto_nego = rtl8125_mdio_read(hw, MII_ADVERTISE);
		auto_nego &= ~(ADVERTISE_10HALF | ADVERTISE_10FULL |
		               ADVERTISE_100HALF | ADVERTISE_100FULL |
		               ADVERTISE_PAUSE_CAP | ADVERTISE_PAUSE_ASYM);

		if (adv & ADVERTISE_10_HALF)
			auto_nego |= ADVERTISE_10HALF;
		if (adv & ADVERTISE_10_FULL)
			auto_nego |= ADVERTISE_10FULL;
		if (adv & ADVERTISE_100_HALF)
			auto_nego |= ADVERTISE_100HALF;
		if (adv & ADVERTISE_100_FULL)
			auto_nego |= ADVERTISE_100FULL;
		if (adv & ADVERTISE_1000_HALF)
			giga_ctrl |= ADVERTISE_1000HALF;
		if (adv & ADVERTISE_1000_FULL)
			giga_ctrl |= ADVERTISE_1000FULL;
		if (adv & ADVERTISE_2500_FULL)
			ctrl_2500 |= RTK_ADVERTISE_2500FULL;

		/* Flow control */
		if (hw->fcpause == rtl8125_fc_full)
			auto_nego |= ADVERTISE_PAUSE_CAP | ADVERTISE_PAUSE_ASYM;

		hw->phy_auto_nego_reg = auto_nego;
		hw->phy_1000_ctrl_reg = giga_ctrl;

		hw->phy_2500_ctrl_reg = ctrl_2500;

		rtl8125_mdio_write(hw, 0x1f, 0x0000);
		rtl8125_mdio_write(hw, MII_ADVERTISE, auto_nego);
		rtl8125_mdio_write(hw, MII_CTRL1000, giga_ctrl);
		rtl8125_mdio_direct_write_phy_ocp(hw, 0xA5D4, ctrl_2500);
		rtl8125_phy_restart_nway(hw);
		mdelay(20);
	} else {
		/* True force */
		if (speed == SPEED_10 || speed == SPEED_100)
			rtl8125_phy_setup_force_mode(hw, speed, duplex);
		else
			goto out;
	}
	hw->autoneg = autoneg;
	hw->speed = speed;
	hw->duplex = duplex;
	hw->advertising = adv;

	rc = 0;
out:
	return rc;
}

static void
rtl8125_wait_phy_ups_resume(struct rtl8125_hw *hw, u16 PhyState)
{
	u16 tmp_phy_state;
	int i = 0;

	switch (hw->mcfg) {
	case CFG_METHOD_2 ... CFG_METHOD_11:
		do {
			tmp_phy_state = rtl8125_mdio_direct_read_phy_ocp(hw, 0xA420);
			tmp_phy_state &= 0x7;
			mdelay(1);
			i++;
		} while ((i < 100) && (tmp_phy_state != PhyState));
	}
}

static void
rtl8125_phy_power_up(struct rtl8125_hw *hw)
{
	if (rtl8125_is_in_phy_disable_mode(hw))
		return;

	rtl8125_mdio_write(hw, 0x1F, 0x0000);
	rtl8125_mdio_write(hw, MII_BMCR, BMCR_ANENABLE);

	/* Wait ups resume (phy state 3) */
	switch (hw->mcfg) {
	case CFG_METHOD_2 ... CFG_METHOD_11:
		rtl8125_wait_phy_ups_resume(hw, 3);
	}
}

void
rtl8125_powerup_pll(struct rtl8125_hw *hw)
{
	switch (hw->mcfg) {
	case CFG_METHOD_2 ... CFG_METHOD_11:
		RTL_W8(hw, PMCH, RTL_R8(hw, PMCH) | BIT_7 | BIT_6);
	}

	rtl8125_phy_power_up(hw);
}

static void
rtl8125_phy_power_down(struct rtl8125_hw *hw)
{
	rtl8125_mdio_write(hw, 0x1F, 0x0000);
	rtl8125_mdio_write(hw, MII_BMCR, BMCR_ANENABLE | BMCR_PDOWN);
}

void
rtl8125_powerdown_pll(struct rtl8125_hw *hw)
{
	if (hw->DASH)
		return;

	rtl8125_phy_power_down(hw);

	switch (hw->mcfg) {
	case CFG_METHOD_2 ... CFG_METHOD_11:
		RTL_W8(hw, PMCH, RTL_R8(hw, PMCH) & ~BIT_7);
		break;
	}
}

int
rtl8125_set_speed(struct rtl8125_hw *hw)
{
	int ret;

	ret = rtl8125_set_speed_xmii(hw, hw->autoneg, hw->speed, hw->duplex,
	                             hw->advertising);

	return ret;
}

static void
rtl8125_disable_aldps(struct rtl8125_hw *hw)
{
	u16 tmp_ushort;
	u32 timeout, wait_cnt;

	tmp_ushort = rtl8125_mdio_real_read_phy_ocp(hw, 0xA430);
	if (tmp_ushort & BIT_2) {
		timeout = 0;
		wait_cnt = 200;
		rtl8125_clear_eth_phy_ocp_bit(hw, 0xA430, BIT_2);

		do {
			udelay(100);

			tmp_ushort = rtl8125_mac_ocp_read(hw, 0xE908);

			timeout++;
		} while (!(tmp_ushort & BIT_7) && timeout < wait_cnt);
	}
}

static bool
rtl8125_is_adv_eee_enabled(struct rtl8125_hw *hw)
{
	switch (hw->mcfg) {
	case CFG_METHOD_2 ... CFG_METHOD_9:
		if (rtl8125_mdio_direct_read_phy_ocp(hw, 0xA430) & BIT_15)
			return true;
		break;
	default:
		break;
	}

	return false;
}

static void
_rtl8125_disable_adv_eee(struct rtl8125_hw *hw)
{
	bool lock;

	if (rtl8125_is_adv_eee_enabled(hw))
		lock = true;
	else
		lock = false;

	if (lock)
		rtl8125_set_phy_mcu_patch_request(hw);

	rtl8125_clear_mac_ocp_bit(hw, 0xE052, BIT_0);
	rtl8125_clear_eth_phy_ocp_bit(hw, 0xA442, (BIT_12 | BIT_13));
	rtl8125_clear_eth_phy_ocp_bit(hw, 0xA430, BIT_15);

	if (lock)
		rtl8125_clear_phy_mcu_patch_request(hw);
}

static void
rtl8125_disable_adv_eee(struct rtl8125_hw *hw)
{
	switch (hw->mcfg) {
	case CFG_METHOD_2:
	case CFG_METHOD_3:
	case CFG_METHOD_6:
	case CFG_METHOD_8:
	case CFG_METHOD_9:
		rtl8125_oob_mutex_lock(hw);
		break;
	}

	_rtl8125_disable_adv_eee(hw);

	switch (hw->mcfg) {
	case CFG_METHOD_2:
	case CFG_METHOD_3:
	case CFG_METHOD_6:
	case CFG_METHOD_8:
	case CFG_METHOD_9:
		rtl8125_oob_mutex_unlock(hw);
		break;
	}
}

static void
rtl8125_disable_eee(struct rtl8125_hw *hw)
{
	switch (hw->mcfg) {
	case CFG_METHOD_2:
	case CFG_METHOD_3:
	case CFG_METHOD_6:
		rtl8125_clear_mac_ocp_bit(hw, 0xE040, (BIT_1 | BIT_0));
		rtl8125_clear_mac_ocp_bit(hw, 0xEB62, (BIT_2 | BIT_1));

		rtl8125_clear_eth_phy_ocp_bit(hw, 0xA432, BIT_4);
		rtl8125_clear_eth_phy_ocp_bit(hw, 0xA5D0, (BIT_2 | BIT_1));
		rtl8125_clear_eth_phy_ocp_bit(hw, 0xA6D4, BIT_0);

		rtl8125_clear_eth_phy_ocp_bit(hw, 0xA6D8, BIT_4);
		rtl8125_clear_eth_phy_ocp_bit(hw, 0xA428, BIT_7);
		rtl8125_clear_eth_phy_ocp_bit(hw, 0xA4A2, BIT_9);
		break;
	case CFG_METHOD_4:
	case CFG_METHOD_5:
	case CFG_METHOD_7:
	case CFG_METHOD_8:
	case CFG_METHOD_9:
	case CFG_METHOD_10:
	case CFG_METHOD_11:
		rtl8125_clear_mac_ocp_bit(hw, 0xE040, (BIT_1 | BIT_0));

		rtl8125_set_eth_phy_ocp_bit(hw, 0xA432, BIT_4);
		rtl8125_clear_eth_phy_ocp_bit(hw, 0xA5D0, (BIT_2 | BIT_1));
		rtl8125_clear_eth_phy_ocp_bit(hw, 0xA6D4, BIT_0);

		rtl8125_clear_eth_phy_ocp_bit(hw, 0xA6D8, BIT_4);
		rtl8125_clear_eth_phy_ocp_bit(hw, 0xA428, BIT_7);
		rtl8125_clear_eth_phy_ocp_bit(hw, 0xA4A2, BIT_9);
		break;
	default:
		/* Not support EEE */
		break;
	}

	/* Advanced EEE */
	rtl8125_disable_adv_eee(hw);
}

static void
rtl8125_set_hw_phy_before_init_phy_mcu(struct rtl8125_hw *hw)
{
	u16 phy_reg_value;

	switch (hw->mcfg) {
	case CFG_METHOD_4:
		rtl8125_mdio_direct_write_phy_ocp(hw, 0xBF86, 0x9000);

		rtl8125_set_eth_phy_ocp_bit(hw, 0xC402, BIT_10);
		rtl8125_clear_eth_phy_ocp_bit(hw, 0xC402, BIT_10);

		phy_reg_value = rtl8125_mdio_direct_read_phy_ocp(hw, 0xBF86);
		phy_reg_value &= (BIT_1 | BIT_0);
		if (phy_reg_value != 0)
			PMD_INIT_LOG(NOTICE, "PHY watch dog not clear, value = 0x%x", phy_reg_value);

		rtl8125_mdio_direct_write_phy_ocp(hw, 0xBD86, 0x1010);
		rtl8125_mdio_direct_write_phy_ocp(hw, 0xBD88, 0x1010);

		rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xBD4E, (BIT_11 | BIT_10), BIT_11);
		rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xBF46,
		                                      (BIT_11 | BIT_10 | BIT_9 | BIT_8), (BIT_10 | BIT_9 | BIT_8));
		break;
	}
}

void
rtl8125_hw_phy_config(struct rtl8125_hw *hw)
{
	rtl8125_xmii_reset_enable(hw);

	rtl8125_set_hw_phy_before_init_phy_mcu(hw);

	rtl8125_init_hw_phy_mcu(hw);

	hw->hw_ops.hw_phy_config(hw);

	switch (hw->mcfg) {
	case CFG_METHOD_2 ... CFG_METHOD_11:
		rtl8125_disable_aldps(hw);
		break;
	}

	/* Legacy force mode (chap 22) */
	switch (hw->mcfg) {
	case CFG_METHOD_2 ... CFG_METHOD_11:
	default:
		rtl8125_clear_eth_phy_ocp_bit(hw, 0xA5B4, BIT_15);
		break;
	}

	rtl8125_mdio_write(hw, 0x1F, 0x0000);

	if (HW_HAS_WRITE_PHY_MCU_RAM_CODE(hw))
		rtl8125_disable_eee(hw);
}

void
rtl8125_set_phy_mcu_ram_code(struct rtl8125_hw *hw, const u16 *ramcode,
                             u16 codesize)
{
	u16 i;
	u16 addr;
	u16 val;

	if (ramcode == NULL || codesize % 2)
		goto out;

	for (i = 0; i < codesize; i += 2) {
		addr = ramcode[i];
		val = ramcode[i + 1];
		if (addr == 0xFFFF && val == 0xFFFF)
			break;
		rtl8125_mdio_direct_write_phy_ocp(hw, addr, val);
	}

out:
	return;
}