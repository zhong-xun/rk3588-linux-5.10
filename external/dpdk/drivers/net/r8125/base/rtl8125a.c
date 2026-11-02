/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020 Realtek Technologies Co., Ltd
 */

#include "../r8125_ethdev.h"
#include "../r8125_hw.h"
#include "../r8125_phy.h"
#include "rtl8125a_mcu.h"

/* For RTL8125A, CFG_METHOD_2,3 */

static void
hw_init_rxcfg_8125a(struct rtl8125_hw *hw)
{
	RTL_W32(hw, RxConfig, Rx_Fetch_Number_8 | (RX_DMA_BURST_256 <<
	                RxCfgDMAShift));
}

static void
hw_ephy_config_8125a(struct rtl8125_hw *hw)
{
	switch (hw->mcfg) {
	case CFG_METHOD_2:
		rtl8125_ephy_write(hw, 0x01, 0xA812);
		rtl8125_ephy_write(hw, 0x09, 0x520C);
		rtl8125_ephy_write(hw, 0x04, 0xD000);
		rtl8125_ephy_write(hw, 0x0D, 0xF702);
		rtl8125_ephy_write(hw, 0x0A, 0x8653);
		rtl8125_ephy_write(hw, 0x06, 0x001E);
		rtl8125_ephy_write(hw, 0x08, 0x3595);
		rtl8125_ephy_write(hw, 0x20, 0x9455);
		rtl8125_ephy_write(hw, 0x21, 0x99FF);
		rtl8125_ephy_write(hw, 0x02, 0x6046);
		rtl8125_ephy_write(hw, 0x29, 0xFE00);
		rtl8125_ephy_write(hw, 0x23, 0xAB62);

		rtl8125_ephy_write(hw, 0x41, 0xA80C);
		rtl8125_ephy_write(hw, 0x49, 0x520C);
		rtl8125_ephy_write(hw, 0x44, 0xD000);
		rtl8125_ephy_write(hw, 0x4D, 0xF702);
		rtl8125_ephy_write(hw, 0x4A, 0x8653);
		rtl8125_ephy_write(hw, 0x46, 0x001E);
		rtl8125_ephy_write(hw, 0x48, 0x3595);
		rtl8125_ephy_write(hw, 0x60, 0x9455);
		rtl8125_ephy_write(hw, 0x61, 0x99FF);
		rtl8125_ephy_write(hw, 0x42, 0x6046);
		rtl8125_ephy_write(hw, 0x69, 0xFE00);
		rtl8125_ephy_write(hw, 0x63, 0xAB62);
		break;
	case CFG_METHOD_3:
		rtl8125_ephy_write(hw, 0x04, 0xD000);
		rtl8125_ephy_write(hw, 0x0A, 0x8653);
		rtl8125_ephy_write(hw, 0x23, 0xAB66);
		rtl8125_ephy_write(hw, 0x20, 0x9455);
		rtl8125_ephy_write(hw, 0x21, 0x99FF);
		rtl8125_ephy_write(hw, 0x29, 0xFE04);

		rtl8125_ephy_write(hw, 0x44, 0xD000);
		rtl8125_ephy_write(hw, 0x4A, 0x8653);
		rtl8125_ephy_write(hw, 0x63, 0xAB66);
		rtl8125_ephy_write(hw, 0x60, 0x9455);
		rtl8125_ephy_write(hw, 0x61, 0x99FF);
		rtl8125_ephy_write(hw, 0x69, 0xFE04);

		rtl8125_clear_and_set_pcie_phy_bit(hw, 0x2A, (BIT_14 | BIT_13 | BIT_12),
		                                   (BIT_13 | BIT_12));
		rtl8125_clear_pcie_phy_bit(hw, 0x19, BIT_6);
		rtl8125_set_pcie_phy_bit(hw, 0x1B, (BIT_11 | BIT_10 | BIT_9));
		rtl8125_clear_pcie_phy_bit(hw, 0x1B, (BIT_14 | BIT_13 | BIT_12));
		rtl8125_ephy_write(hw, 0x02, 0x6042);
		rtl8125_ephy_write(hw, 0x06, 0x0014);

		rtl8125_clear_and_set_pcie_phy_bit(hw, 0x6A, (BIT_14 | BIT_13 | BIT_12),
		                                   (BIT_13 | BIT_12));
		rtl8125_clear_pcie_phy_bit(hw, 0x59, BIT_6);
		rtl8125_set_pcie_phy_bit(hw, 0x5B, (BIT_11 | BIT_10 | BIT_9));
		rtl8125_clear_pcie_phy_bit(hw, 0x5B, (BIT_14 | BIT_13 | BIT_12));
		rtl8125_ephy_write(hw, 0x42, 0x6042);
		rtl8125_ephy_write(hw, 0x46, 0x0014);
		break;
	}
}

static void
rtl8125_hw_phy_config_8125a_1(struct rtl8125_hw *hw)
{
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xAD40, 0x03FF, 0x84);

	rtl8125_set_eth_phy_ocp_bit(hw, 0xAD4E, BIT_4);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xAD16, 0x03FF, 0x0006);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xAD32, 0x003F, 0x0006);
	rtl8125_clear_eth_phy_ocp_bit(hw, 0xAC08, BIT_12);
	rtl8125_clear_eth_phy_ocp_bit(hw, 0xAC08, BIT_8);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xAC8A,
	                                      (BIT_15 | BIT_14 | BIT_13 | BIT_12), (BIT_14 | BIT_13 | BIT_12));
	rtl8125_set_eth_phy_ocp_bit(hw, 0xAD18, BIT_10);
	rtl8125_set_eth_phy_ocp_bit(hw, 0xAD1A, 0x3FF);
	rtl8125_set_eth_phy_ocp_bit(hw, 0xAD1C, 0x3FF);

	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x80EA);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, 0xFF00, 0xC400);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x80EB);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, 0x0700, 0x0300);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x80F8);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, 0xFF00, 0x1C00);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x80F1);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, 0xFF00, 0x3000);

	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x80FE);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, 0xFF00, 0xA500);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x8102);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, 0xFF00, 0x5000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x8105);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, 0xFF00, 0x3300);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x8100);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, 0xFF00, 0x7000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x8104);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, 0xFF00, 0xF000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x8106);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, 0xFF00, 0x6500);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x80DC);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xA438, 0xFF00, 0xED00);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x80DF);
	rtl8125_set_eth_phy_ocp_bit(hw, 0xA438, BIT_8);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x80E1);
	rtl8125_clear_eth_phy_ocp_bit(hw, 0xA438, BIT_8);

	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xBF06, 0x003F, 0x38);

	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x819F);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xD0B6);

	rtl8125_mdio_direct_write_phy_ocp(hw, 0xBC34, 0x5555);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xBF0A, (BIT_11 | BIT_10 | BIT_9),
	                                      (BIT_11 | BIT_9));

	rtl8125_clear_eth_phy_ocp_bit(hw, 0xA5C0, BIT_10);

	rtl8125_set_eth_phy_ocp_bit(hw, 0xA442, BIT_11);
}

static void
rtl8125_hw_phy_config_8125a_2(struct rtl8125_hw *hw)
{
	u16 adccal_offset_p0;
	u16 adccal_offset_p1;
	u16 adccal_offset_p2;
	u16 adccal_offset_p3;
	u16 rg_lpf_cap_xg_p0;
	u16 rg_lpf_cap_xg_p1;
	u16 rg_lpf_cap_xg_p2;
	u16 rg_lpf_cap_xg_p3;
	u16 rg_lpf_cap_p0;
	u16 rg_lpf_cap_p1;
	u16 rg_lpf_cap_p2;
	u16 rg_lpf_cap_p3;

	rtl8125_set_eth_phy_ocp_bit(hw, 0xAD4E, BIT_4);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xAD16, 0x03FF, 0x03FF);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xAD32, 0x003F, 0x0006);
	rtl8125_clear_eth_phy_ocp_bit(hw, 0xAC08, BIT_12);
	rtl8125_clear_eth_phy_ocp_bit(hw, 0xAC08, BIT_8);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xACC0, (BIT_1 | BIT_0), BIT_1);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xAD40, (BIT_7 | BIT_6 | BIT_5),
	                                      BIT_6);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xAD40, (BIT_2 | BIT_1 | BIT_0),
	                                      BIT_2);
	rtl8125_clear_eth_phy_ocp_bit(hw, 0xAC14, BIT_7);
	rtl8125_clear_eth_phy_ocp_bit(hw, 0xAC80, BIT_9 | BIT_8);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xAC5E, (BIT_2 | BIT_1 | BIT_0),
	                                      BIT_1);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xAD4C, 0x00A8);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xAC5C, 0x01FF);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xAC8A,
	                                      (BIT_7 | BIT_6 | BIT_5 | BIT_4), (BIT_5 | BIT_4));
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8157);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xB87E, 0xFF00, 0x0500);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x8159);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xB87E, 0xFF00, 0x0700);

	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x80A2);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0x0153);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87C, 0x809C);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB87E, 0x0153);

	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x81B3);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0043);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x00A7);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x00D6);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x00EC);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x00F6);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x00FB);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x00FD);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x00FF);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x00BB);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0058);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0029);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0013);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0009);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0004);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0002);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);

	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x8257);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x020F);

	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x80EA);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x7843);

	rtl8125_set_phy_mcu_patch_request(hw);

	rtl8125_clear_eth_phy_ocp_bit(hw, 0xB896, BIT_0);
	rtl8125_clear_eth_phy_ocp_bit(hw, 0xB892, 0xFF00);

	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB88E, 0xC091);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB890, 0x6E12);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB88E, 0xC092);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB890, 0x1214);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB88E, 0xC094);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB890, 0x1516);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB88E, 0xC096);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB890, 0x171B);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB88E, 0xC098);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB890, 0x1B1C);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB88E, 0xC09A);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB890, 0x1F1F);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB88E, 0xC09C);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB890, 0x2021);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB88E, 0xC09E);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB890, 0x2224);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB88E, 0xC0A0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB890, 0x2424);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB88E, 0xC0A2);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB890, 0x2424);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB88E, 0xC0A4);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB890, 0x2424);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB88E, 0xC018);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB890, 0x0AF2);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB88E, 0xC01A);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB890, 0x0D4A);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB88E, 0xC01C);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB890, 0x0F26);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB88E, 0xC01E);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB890, 0x118D);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB88E, 0xC020);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB890, 0x14F3);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB88E, 0xC022);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB890, 0x175A);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB88E, 0xC024);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB890, 0x19C0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB88E, 0xC026);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB890, 0x1C26);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB88E, 0xC089);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB890, 0x6050);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB88E, 0xC08A);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB890, 0x5F6E);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB88E, 0xC08C);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB890, 0x6E6E);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB88E, 0xC08E);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB890, 0x6E6E);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB88E, 0xC090);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xB890, 0x6E12);

	rtl8125_set_eth_phy_ocp_bit(hw, 0xB896, BIT_0);

	rtl8125_clear_phy_mcu_patch_request(hw);

	rtl8125_set_eth_phy_ocp_bit(hw, 0xD068, BIT_13);

	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x81A2);
	rtl8125_set_eth_phy_ocp_bit(hw, 0xA438, BIT_8);
	rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xB54C, 0xFF00, 0xDB00);

	rtl8125_clear_eth_phy_ocp_bit(hw, 0xA454, BIT_0);

	rtl8125_set_eth_phy_ocp_bit(hw, 0xA5D4, BIT_5);
	rtl8125_clear_eth_phy_ocp_bit(hw, 0xAD4E, BIT_4);
	rtl8125_clear_eth_phy_ocp_bit(hw, 0xA86A, BIT_0);

	rtl8125_set_eth_phy_ocp_bit(hw, 0xA442, BIT_11);

	if (hw->RequirePhyMdiSwapPatch) {
		rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xD068, 0x0007, 0x0001);
		rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xD068, 0x0018, 0x0000);
		adccal_offset_p0 = rtl8125_mdio_direct_read_phy_ocp(hw, 0xD06A);
		adccal_offset_p0 &= 0x07FF;
		rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xD068, 0x0018, 0x0008);
		adccal_offset_p1 = rtl8125_mdio_direct_read_phy_ocp(hw, 0xD06A);
		adccal_offset_p1 &= 0x07FF;
		rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xD068, 0x0018, 0x0010);
		adccal_offset_p2 = rtl8125_mdio_direct_read_phy_ocp(hw, 0xD06A);
		adccal_offset_p2 &= 0x07FF;
		rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xD068, 0x0018, 0x0018);
		adccal_offset_p3 = rtl8125_mdio_direct_read_phy_ocp(hw, 0xD06A);
		adccal_offset_p3 &= 0x07FF;


		rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xD068, 0x0018, 0x0000);
		rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xD06A, 0x07FF, adccal_offset_p3);
		rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xD068, 0x0018, 0x0008);
		rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xD06A, 0x07FF, adccal_offset_p2);
		rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xD068, 0x0018, 0x0010);
		rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xD06A, 0x07FF, adccal_offset_p1);
		rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xD068, 0x0018, 0x0018);
		rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xD06A, 0x07FF, adccal_offset_p0);


		rg_lpf_cap_xg_p0 = rtl8125_mdio_direct_read_phy_ocp(hw, 0xBD5A);
		rg_lpf_cap_xg_p0 &= 0x001F;
		rg_lpf_cap_xg_p1 = rtl8125_mdio_direct_read_phy_ocp(hw, 0xBD5A);
		rg_lpf_cap_xg_p1 &= 0x1F00;
		rg_lpf_cap_xg_p2 = rtl8125_mdio_direct_read_phy_ocp(hw, 0xBD5C);
		rg_lpf_cap_xg_p2 &= 0x001F;
		rg_lpf_cap_xg_p3 = rtl8125_mdio_direct_read_phy_ocp(hw, 0xBD5C);
		rg_lpf_cap_xg_p3 &= 0x1F00;
		rg_lpf_cap_p0 = rtl8125_mdio_direct_read_phy_ocp(hw, 0xBC18);
		rg_lpf_cap_p0 &= 0x001F;
		rg_lpf_cap_p1 = rtl8125_mdio_direct_read_phy_ocp(hw, 0xBC18);
		rg_lpf_cap_p1 &= 0x1F00;
		rg_lpf_cap_p2 = rtl8125_mdio_direct_read_phy_ocp(hw, 0xBC1A);
		rg_lpf_cap_p2 &= 0x001F;
		rg_lpf_cap_p3 = rtl8125_mdio_direct_read_phy_ocp(hw, 0xBC1A);
		rg_lpf_cap_p3 &= 0x1F00;


		rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xBD5A, 0x001F,
		                                      rg_lpf_cap_xg_p3 >> 8);
		rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xBD5A, 0x1F00,
		                                      rg_lpf_cap_xg_p2 << 8);
		rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xBD5C, 0x001F,
		                                      rg_lpf_cap_xg_p1 >> 8);
		rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xBD5C, 0x1F00,
		                                      rg_lpf_cap_xg_p0 << 8);
		rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xBC18, 0x001F, rg_lpf_cap_p3 >> 8);
		rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xBC18, 0x1F00, rg_lpf_cap_p2 << 8);
		rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xBC1A, 0x001F, rg_lpf_cap_p1 >> 8);
		rtl8125_clear_and_set_eth_phy_ocp_bit(hw, 0xBC1A, 0x1F00, rg_lpf_cap_p0 << 8);
	}

	rtl8125_set_eth_phy_ocp_bit(hw, 0xA424, BIT_3);
}

static void
hw_phy_config_8125a(struct rtl8125_hw *hw)
{
	switch (hw->mcfg) {
	case CFG_METHOD_2:
		rtl8125_hw_phy_config_8125a_1(hw);
		break;
	case CFG_METHOD_3:
		rtl8125_hw_phy_config_8125a_2(hw);
		break;
	}
}

static void
hw_mac_mcu_config_8125a(struct rtl8125_hw *hw)
{
	if (hw->NotWrMcuPatchCode == TRUE)
		return;

	switch (hw->mcfg) {
	case CFG_METHOD_2:
		rtl8125_set_mac_mcu_8125a_1(hw);
		break;
	case CFG_METHOD_3:
		rtl8125_set_mac_mcu_8125a_2(hw);
		break;
	}
}

static void
hw_phy_mcu_config_8125a(struct rtl8125_hw *hw)
{
	switch (hw->mcfg) {
	case CFG_METHOD_2:
		rtl8125_set_phy_mcu_8125a_1(hw);
		break;
	case CFG_METHOD_3:
		rtl8125_set_phy_mcu_8125a_2(hw);
		break;
	}
}


const struct rtl8125_hw_ops rtl8125a_ops = {
	.hw_init_rxcfg     = hw_init_rxcfg_8125a,
	.hw_ephy_config    = hw_ephy_config_8125a,
	.hw_phy_config     = hw_phy_config_8125a,
	.hw_mac_mcu_config = hw_mac_mcu_config_8125a,
	.hw_phy_mcu_config = hw_phy_mcu_config_8125a,
};