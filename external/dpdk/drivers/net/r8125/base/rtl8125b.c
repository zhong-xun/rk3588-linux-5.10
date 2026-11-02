/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020 Realtek Technologies Co., Ltd
 */

#include "../r8125_ethdev.h"
#include "../r8125_hw.h"
#include "../r8125_phy.h"
#include "rtl8125b_mcu.h"

/* For RTL8125B, CFG_METHOD_4,5 */

static void
hw_init_rxcfg_8125b(struct rtl8125_hw *hw)
{
	RTL_W32(hw, RxConfig, Rx_Fetch_Number_8 | RxCfg_pause_slot_en |
	        (RX_DMA_BURST_256 << RxCfgDMAShift));
}

static void
hw_ephy_config_8125b(struct rtl8125_hw *hw)
{
	switch (hw->mcfg) {
	case CFG_METHOD_4:
		rtl8125_ephy_write(hw, 0x06, 0x001F);
		rtl8125_ephy_write(hw, 0x0A, 0xB66B);
		rtl8125_ephy_write(hw, 0x01, 0xA852);
		rtl8125_ephy_write(hw, 0x24, 0x0008);
		rtl8125_ephy_write(hw, 0x2F, 0x6052);
		rtl8125_ephy_write(hw, 0x0D, 0xF716);
		rtl8125_ephy_write(hw, 0x20, 0xD477);
		rtl8125_ephy_write(hw, 0x21, 0x4477);
		rtl8125_ephy_write(hw, 0x22, 0x0013);
		rtl8125_ephy_write(hw, 0x23, 0xBB66);
		rtl8125_ephy_write(hw, 0x0B, 0xA909);
		rtl8125_ephy_write(hw, 0x29, 0xFF04);
		rtl8125_ephy_write(hw, 0x1B, 0x1EA0);

		rtl8125_ephy_write(hw, 0x46, 0x001F);
		rtl8125_ephy_write(hw, 0x4A, 0xB66B);
		rtl8125_ephy_write(hw, 0x41, 0xA84A);
		rtl8125_ephy_write(hw, 0x64, 0x000C);
		rtl8125_ephy_write(hw, 0x6F, 0x604A);
		rtl8125_ephy_write(hw, 0x4D, 0xF716);
		rtl8125_ephy_write(hw, 0x60, 0xD477);
		rtl8125_ephy_write(hw, 0x61, 0x4477);
		rtl8125_ephy_write(hw, 0x62, 0x0013);
		rtl8125_ephy_write(hw, 0x63, 0xBB66);
		rtl8125_ephy_write(hw, 0x4B, 0xA909);
		rtl8125_ephy_write(hw, 0x69, 0xFF04);
		rtl8125_ephy_write(hw, 0x5B, 0x1EA0);
		break;
	case CFG_METHOD_5:
		rtl8125_ephy_write(hw, 0x0B, 0xA908);
		rtl8125_ephy_write(hw, 0x1E, 0x20EB);
		rtl8125_ephy_write(hw, 0x22, 0x0023);
		rtl8125_ephy_write(hw, 0x02, 0x60C2);
		rtl8125_ephy_write(hw, 0x29, 0xFF00);

		rtl8125_ephy_write(hw, 0x4B, 0xA908);
		rtl8125_ephy_write(hw, 0x5E, 0x28EB);
		rtl8125_ephy_write(hw, 0x62, 0x0023);
		rtl8125_ephy_write(hw, 0x42, 0x60C2);
		rtl8125_ephy_write(hw, 0x69, 0xFF00);
		break;
	}
}

static void
rtl8125_hw_phy_config_8125b_1(struct rtl8125_hw *hw)
{
	rtl8125_set_eth_phy_ocp_bit(hw, 0xA442, BIT_11);

	rtl8125_set_eth_phy_ocp_bit(hw, 0xBC08, (BIT_3 | BIT_2));

	if (HW_HAS_WRITE_PHY_MCU_RAM_CODE(hw)) {
		rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x8FFF);
		rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, 0xFF00, 0x0400);
	}
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8560);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0x19CC);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8562);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0x19CC);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8564);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0x19CC);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8566);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0x147D);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8568);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0x147D);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x856A);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0x147D);
	if (HW_HAS_WRITE_PHY_MCU_RAM_CODE(hw)) {
		rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8FFE);
		rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0x0907);
	}
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xACDA, 0xFF00, 0xFF00);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xACDE, 0xF000, 0xF000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x80D6);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0x2801);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x80F2);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0x2801);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x80F4);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0x6077);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB506, 0x01E7);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xAC8C, 0x0FFC);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xAC46, 0xB7B4);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xAC50, 0x0FBC);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xAC3C, 0x9240);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xAC4E, 0x0DB4);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xACC6, 0x0707);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xACC8, 0xA0D3);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xAD08, 0x0007);

	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8013);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0x0700);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8FB9);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0x2801);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8FBA);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0x0100);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8FBC);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0x1900);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8FBE);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0xE100);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8FC0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0x0800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8FC2);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0xE500);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8FC4);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0x0F00);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8FC6);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0xF100);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8FC8);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0x0400);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8FCa);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0xF300);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8FCc);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0xFD00);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8FCe);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0xFF00);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8FD0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0xFB00);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8FD2);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0x0100);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8FD4);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0xF400);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8FD6);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0xFF00);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8FD8);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0xF600);


	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x813D);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0x390E);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x814F);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0x790E);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x80B0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0x0F31);
	rtl8125_set_eth_phy_ocp_bit(hw, 0xBF4C, BIT_1);
	rtl8125_set_eth_phy_ocp_bit(hw, 0xBCCA, (BIT_9 | BIT_8));
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8141);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0x320E);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8153);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0x720E);
	rtl8125_clear_eth_phy_ocp_bit(hw, 0xA432, BIT_6);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8529);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0x050E);

	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x816C);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xC4A0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x8170);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xC4A0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x8174);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x04A0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x8178);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x04A0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x817C);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0719);
	if (HW_HAS_WRITE_PHY_MCU_RAM_CODE(hw)) {
		rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x8FF4);
		rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0400);
		rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x8FF1);
		rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0404);
	}
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xBF4A, 0x001B);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8033);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0x7C13);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8037);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0x7C13);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x803B);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0xFC32);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x803F);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0x7C13);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8043);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0x7C13);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8047);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0x7C13);


	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8145);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0x370E);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8157);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0x770E);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8169);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0x0D0A);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x817B);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0x1D0A);


	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x8217);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, 0xFF00, 0x5000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x821A);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, 0xFF00, 0x5000);

	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x80DA);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0403);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x80DC);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, 0xFF00, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x80B3);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0384);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x80B7);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x2007);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x80BA);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, 0xFF00, 0x6C00);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x80B5);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xF009);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x80BD);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, 0xFF00, 0x9F00);

	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x80C7);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xf083);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x80DD);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x03f0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x80DF);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, 0xFF00, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x80CB);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x2007);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x80CE);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, 0xFF00, 0x6C00);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x80C9);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8009);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x80D1);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, 0xFF00, 0x8000);

	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x80A3);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x200A);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x80A5);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xF0AD);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x809F);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x6073);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x80A1);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x000B);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x80A9);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, 0xFF00, 0xC000);

	rtl8125_set_phy_mcu_patch_request(hw);

	rtl8125_clear_eth_phy_ocp_bit(hw, 0xB896, BIT_0);
	rtl8125_clear_eth_phy_ocp_bit(hw, 0xB892, 0xFF00);

	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB88E, 0xC23E);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB890, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB88E, 0xC240);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB890, 0x0103);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB88E, 0xC242);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB890, 0x0507);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB88E, 0xC244);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB890, 0x090B);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB88E, 0xC246);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB890, 0x0C0E);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB88E, 0xC248);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB890, 0x1012);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB88E, 0xC24A);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB890, 0x1416);

	rtl8125_set_eth_phy_ocp_bit(hw, 0xB896, BIT_0);

	rtl8125_clear_phy_mcu_patch_request(hw);


	rtl8125_set_eth_phy_ocp_bit(hw, 0xA86A, BIT_0);
	rtl8125_set_eth_phy_ocp_bit(hw, 0xA6F0, BIT_0);


	rtl8125_mdio_direct_write_phy_ocp(hw, 0xBFA0, 0xD70D);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xBFA2, 0x4100);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xBFA4, 0xE868);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xBFA6, 0xDC59);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB54C, 0x3C18);
	rtl8125_clear_eth_phy_ocp_bit(hw, 0xBFA4, BIT_5);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x817D);
	rtl8125_set_eth_phy_ocp_bit(hw, 0xA438, BIT_12);
}

static void
rtl8125_hw_phy_config_8125b_2(struct rtl8125_hw *hw)
{
	rtl8125_set_eth_phy_ocp_bit(hw, 0xA442, BIT_11);


	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xAC46, 0x00F0, 0x0090);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xAD30, 0x0003, 0x0001);

	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x80F5);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0x760E);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8107);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0x360E);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8551);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xB87E,
	                                      BIT_15 | BIT_14 | BIT_13 | BIT_12 | BIT_11 | BIT_10 | BIT_9 | BIT_8,
	                                      BIT_11);

	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xbf00, 0xE000, 0xA000);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xbf46, 0x0F00, 0x0300);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xa436, 0x8044);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xa438, 0x2417);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xa436, 0x804A);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xa438, 0x2417);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xa436, 0x8050);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xa438, 0x2417);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xa436, 0x8056);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xa438, 0x2417);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xa436, 0x805C);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xa438, 0x2417);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xa436, 0x8062);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xa438, 0x2417);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xa436, 0x8068);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xa438, 0x2417);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xa436, 0x806E);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xa438, 0x2417);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xa436, 0x8074);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xa438, 0x2417);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xa436, 0x807A);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xa438, 0x2417);


	rtl8125_set_eth_phy_ocp_bit(hw, 0xA4CA, BIT_6);


	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xBF84, (BIT_15 | BIT_14 | BIT_13),
	                                      (BIT_15 | BIT_13));


	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x8170);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438,
	                                      (BIT_13 | BIT_10 | BIT_9 | BIT_8),
	                                      (BIT_15 | BIT_14 | BIT_12 | BIT_11));

	rtl8125_set_eth_phy_ocp_bit(hw, 0xA424, BIT_3);
}

static void
hw_phy_config_8125b(struct rtl8125_hw *hw)
{
	switch (hw->mcfg) {
	case CFG_METHOD_4:
		rtl8125_hw_phy_config_8125b_1(hw);
		break;
	case CFG_METHOD_5:
		rtl8125_hw_phy_config_8125b_2(hw);
		break;
	}
}

static void
hw_mac_mcu_config_8125b(struct rtl8125_hw *hw)
{
	if (hw->NotWrMcuPatchCode == TRUE)
		return;

	switch (hw->mcfg) {
	case CFG_METHOD_4:
		rtl8125_set_mac_mcu_8125b_1(hw);
		break;
	case CFG_METHOD_5:
		rtl8125_set_mac_mcu_8125b_2(hw);
		break;
	}
}

static void
hw_phy_mcu_config_8125b(struct rtl8125_hw *hw)
{
	switch (hw->mcfg) {
	case CFG_METHOD_4:
		rtl8125_set_phy_mcu_8125b_1(hw);
		break;
	case CFG_METHOD_5:
		rtl8125_set_phy_mcu_8125b_2(hw);
		break;
	}
}

const struct rtl8125_hw_ops rtl8125b_ops = {
	.hw_init_rxcfg     = hw_init_rxcfg_8125b,
	.hw_ephy_config    = hw_ephy_config_8125b,
	.hw_phy_config     = hw_phy_config_8125b,
	.hw_mac_mcu_config = hw_mac_mcu_config_8125b,
	.hw_phy_mcu_config = hw_phy_mcu_config_8125b,
};