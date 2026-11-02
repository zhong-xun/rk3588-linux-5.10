/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020 Realtek Technologies Co., Ltd
 */

#include "../r8125_ethdev.h"
#include "../r8125_hw.h"
#include "../r8125_phy.h"
#include "rtl8125d_mcu.h"

/* For RTL8125D, CFG_METHOD_10,11 */

static void
hw_init_rxcfg_8125d(struct rtl8125_hw *hw)
{
	RTL_W32(hw, RxConfig, Rx_Fetch_Number_8 | Rx_Close_Multiple |
	        RxCfg_pause_slot_en | (RX_DMA_BURST_256 << RxCfgDMAShift));
}

static void
hw_ephy_config_8125d(struct rtl8125_hw *hw)
{
	switch (hw->mcfg) {
	case CFG_METHOD_10:
	case CFG_METHOD_11:
		/* Nothing to do */
		break;
	}
}

static void
rtl8125_hw_phy_config_8125d_1(struct rtl8125_hw *hw)
{
	rtl8125_set_eth_phy_ocp_bit(hw, 0xA442, BIT_11);

	rtl8125_set_phy_mcu_patch_request(hw);

	rtl8125_set_eth_phy_ocp_bit(hw, 0xBF96, BIT_15);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xBF94, 0x0007, 0x0005);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xBF8E, 0x3C00, 0x2800);

	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xBCD8, 0xC000, 0x4000);
	rtl8125_set_eth_phy_ocp_bit(hw, 0xBCD8, (BIT_15 | BIT_14));
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xBCD8, 0xC000, 0x4000);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xBC80, 0x001F, 0x0004);
	rtl8125_set_eth_phy_ocp_bit(hw, 0xBC82, (BIT_15 | BIT_14 | BIT_13));
	rtl8125_set_eth_phy_ocp_bit(hw, 0xBC82, (BIT_12 | BIT_11 | BIT_10));
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xBC80, 0x001F, 0x0005);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xBC82, 0x00E0, 0x0040);
	rtl8125_set_eth_phy_ocp_bit(hw, 0xBC82, (BIT_4 | BIT_3 | BIT_2));
	rtl8125_clear_eth_phy_ocp_bit(hw, 0xBCD8, (BIT_15 | BIT_14));
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xBCD8, 0xC000, 0x8000);
	rtl8125_clear_eth_phy_ocp_bit(hw, 0xBCD8, (BIT_15 | BIT_14));

	rtl8125_clear_phy_mcu_patch_request(hw);

	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x832C);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xB87E, 0xFF00, 0x0500);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xB106, 0x0700, 0x0100);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xB206, 0x0700, 0x0200);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xB306, 0x0700, 0x0300);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x80CB);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xB87E, 0xFF00, 0x0300);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xBCF4, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xBCF6, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xBC12, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x844d);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xB87E, 0xFF00, 0x0200);
	if (HW_HAS_WRITE_PHY_MCU_RAM_CODE(hw)) {
		rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8feb);
		rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xB87E, 0xFF00, 0x0100);
		rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8fe9);
		rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xB87E, 0xFF00, 0x0600);
	}


	rtl8125_clear_eth_phy_ocp_bit(hw, 0xAD40, (BIT_5 | BIT_4));
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xAD66, 0x000F, 0x0007);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xAD68, 0xF000, 0x8000);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xAD68, 0x0F00, 0x0500);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xAD68, 0x000F, 0x0002);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xAD6A, 0xF000, 0x7000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xAC50, 0x01E8);


	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x81FA);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, 0xFF00, 0x5400);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA864, 0x00F0, 0x00C0);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA42C, 0x00FF, 0x0002);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x80E1);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, 0xFF00, 0x0F00);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x80DE);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, 0xF000, 0x0700);
	rtl8125_set_eth_phy_ocp_bit(hw, 0xA846, BIT_7);

	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x80BA);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8A04);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x80BD);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, 0xFF00, 0xCA00);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x80B7);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, 0xFF00, 0xB300);

	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x80CE);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8A04);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x80D1);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, 0xFF00, 0xCA00);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x80CB);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, 0xFF00, 0xBB00);

	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x80A6);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x4909);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x80A8);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x05B8);

	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x8200);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, 0xFF00, 0x5800);

	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x8FF1);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x7078);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x8FF3);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5D78);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x8FF5);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x7862);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x8FF7);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, 0xFF00, 0x1400);

	if (HW_HAS_WRITE_PHY_MCU_RAM_CODE(hw)) {
		rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x814C);
		rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8455);
		rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x814E);
		rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x84A6);
		rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x8163);
		rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, 0xFF00, 0x0600);
		rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x816A);
		rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, 0xFF00, 0x0500);
		rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x8171);
		rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, 0xFF00, 0x1f00);
	}

	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xBC3A, 0x000F, 0x0006);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x8064);
	rtl8125_clear_eth_phy_ocp_bit(hw, 0xA438, (BIT_10 | BIT_9 | BIT_8));
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x8067);
	rtl8125_clear_eth_phy_ocp_bit(hw, 0xA438, (BIT_10 | BIT_9 | BIT_8));
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x806A);
	rtl8125_clear_eth_phy_ocp_bit(hw, 0xA438, (BIT_10 | BIT_9 | BIT_8));
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x806D);
	rtl8125_clear_eth_phy_ocp_bit(hw, 0xA438, (BIT_10 | BIT_9 | BIT_8));
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x8070);
	rtl8125_clear_eth_phy_ocp_bit(hw, 0xA438, (BIT_10 | BIT_9 | BIT_8));
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x8073);
	rtl8125_clear_eth_phy_ocp_bit(hw, 0xA438, (BIT_10 | BIT_9 | BIT_8));
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x8076);
	rtl8125_clear_eth_phy_ocp_bit(hw, 0xA438, (BIT_10 | BIT_9 | BIT_8));
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x8079);
	rtl8125_clear_eth_phy_ocp_bit(hw, 0xA438, (BIT_10 | BIT_9 | BIT_8));
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x807C);
	rtl8125_clear_eth_phy_ocp_bit(hw, 0xA438, (BIT_10 | BIT_9 | BIT_8));
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x807F);
	rtl8125_clear_eth_phy_ocp_bit(hw, 0xA438, (BIT_10 | BIT_9 | BIT_8));

	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xBFA0, 0xFF70, 0x5500);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xBFA2, 0x9D00);

	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x8165);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, 0x0700, 0x0200);

	if (HW_HAS_WRITE_PHY_MCU_RAM_CODE(hw)) {
		rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x8019);
		rtl8125_set_eth_phy_ocp_bit(hw, 0xA438, BIT_8);
		rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x8FE3);
		rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0005);
		rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
		rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x00ED);
		rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0502);
		rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0B00);
		rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xD401);
		rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, 0xFF00, 0x2900);
	}

	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x8018);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, 0xFF00, 0x1700);

	if (HW_HAS_WRITE_PHY_MCU_RAM_CODE(hw)) {
		rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x815B);
		rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, 0xFF00, 0x1700);
	}

	rtl8125_set_eth_phy_ocp_bit(hw, 0xA430, BIT_12 | BIT_0);
	rtl8125_set_eth_phy_ocp_bit(hw, 0xA442, BIT_7);
}

static void
rtl8125_hw_phy_config_8125d_2(struct rtl8125_hw *hw)
{
	rtl8125_set_eth_phy_ocp_bit(hw, 0xA442, BIT_11);
}

static void
hw_phy_config_8125d(struct rtl8125_hw *hw)
{
	switch (hw->mcfg) {
	case CFG_METHOD_10:
		rtl8125_hw_phy_config_8125d_1(hw);
		break;
	case CFG_METHOD_11:
		rtl8125_hw_phy_config_8125d_2(hw);
		break;
	}
}

static void
hw_mac_mcu_config_8125d(struct rtl8125_hw *hw)
{
	if (hw->NotWrMcuPatchCode == TRUE)
		return;

	switch (hw->mcfg) {
	case CFG_METHOD_10:
		rtl8125_set_mac_mcu_8125d_1(hw);
		break;
	case CFG_METHOD_11:
		rtl8125_set_mac_mcu_8125d_2(hw);
		break;
	}
}

static void
hw_phy_mcu_config_8125d(struct rtl8125_hw *hw)
{
	switch (hw->mcfg) {
	case CFG_METHOD_10:
		rtl8125_set_phy_mcu_8125d_1(hw);
		break;
	case CFG_METHOD_11:
		/* Nothing to do */
		break;
	}
}

const struct rtl8125_hw_ops rtl8125d_ops = {
	.hw_init_rxcfg     = hw_init_rxcfg_8125d,
	.hw_ephy_config    = hw_ephy_config_8125d,
	.hw_phy_config     = hw_phy_config_8125d,
	.hw_mac_mcu_config = hw_mac_mcu_config_8125d,
	.hw_phy_mcu_config = hw_phy_mcu_config_8125d,
};