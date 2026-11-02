/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020 Realtek Technologies Co., Ltd
 */

#include "../r8125_ethdev.h"
#include "../r8125_hw.h"
#include "../r8125_phy.h"
#include "rtl8125bp_mcu.h"

/* For RTL8125BP, CFG_METHOD_8,9 */

static void
hw_init_rxcfg_8125bp(struct rtl8125_hw *hw)
{
	RTL_W32(hw, RxConfig, Rx_Fetch_Number_8 | Rx_Close_Multiple |
	        RxCfg_pause_slot_en | (RX_DMA_BURST_256 << RxCfgDMAShift));
}

static void
hw_ephy_config_8125bp(struct rtl8125_hw *hw)
{
	switch (hw->mcfg) {
	case CFG_METHOD_8:
	case CFG_METHOD_9:
		/* Nothing to do */
		break;
	}
}

static void
rtl8125_hw_phy_config_8125bp_1(struct rtl8125_hw *hw)
{
	rtl8125_set_eth_phy_ocp_bit(hw, 0xA442, BIT_11);

	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA80C, BIT_14,
	                                      (BIT_15 | BIT_11 | BIT_10));

	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x8010);
	rtl8125_clear_eth_phy_ocp_bit(hw, 0xA438, BIT_11);

	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8088);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xB87E, 0xFF00, 0x9000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x808F);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xB87E, 0xFF00, 0x9000);

	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x8174);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, BIT_13, (BIT_12 | BIT_11));
}

static void
rtl8125_hw_phy_config_8125bp_2(struct rtl8125_hw *hw)
{
	rtl8125_set_eth_phy_ocp_bit(hw, 0xA442, BIT_11);

	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x8010);
	rtl8125_clear_eth_phy_ocp_bit(hw, 0xA438, BIT_11);

	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8088);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xB87E, 0xFF00, 0x9000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x808F);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xB87E, 0xFF00, 0x9000);

	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x8174);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, BIT_13, (BIT_12 | BIT_11));
}

static void
hw_phy_config_8125bp(struct rtl8125_hw *hw)
{
	switch (hw->mcfg) {
	case CFG_METHOD_8:
		rtl8125_hw_phy_config_8125bp_1(hw);
		break;
	case CFG_METHOD_9:
		rtl8125_hw_phy_config_8125bp_2(hw);
		break;
	}
}

static void
hw_mac_mcu_config_8125bp(struct rtl8125_hw *hw)
{
	if (hw->NotWrMcuPatchCode == TRUE)
		return;

	switch (hw->mcfg) {
	case CFG_METHOD_8:
		rtl8125_set_mac_mcu_8125bp_1(hw);
		break;
	case CFG_METHOD_9:
		rtl8125_set_mac_mcu_8125bp_2(hw);
		break;
	}
}

static void
hw_phy_mcu_config_8125bp(struct rtl8125_hw *hw)
{
	switch (hw->mcfg) {
	case CFG_METHOD_8:
		rtl8125_set_phy_mcu_8125bp_1(hw);
		break;
	case CFG_METHOD_9:
		/* Nothing to do */
		break;
	}
}

const struct rtl8125_hw_ops rtl8125bp_ops = {
	.hw_init_rxcfg     = hw_init_rxcfg_8125bp,
	.hw_ephy_config    = hw_ephy_config_8125bp,
	.hw_phy_config     = hw_phy_config_8125bp,
	.hw_mac_mcu_config = hw_mac_mcu_config_8125bp,
	.hw_phy_mcu_config = hw_phy_mcu_config_8125bp,
};