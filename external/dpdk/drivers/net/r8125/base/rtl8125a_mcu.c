/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020 Realtek Technologies Co., Ltd
 */

#include "../r8125_ethdev.h"
#include "../r8125_hw.h"
#include "../r8125_phy.h"
#include "rtl8125a_mcu.h"

/* For RTL8125A, CFG_METHOD_2,3 */

/* ------------------------------------MAC 8125A------------------------------------- */

void
rtl8125_set_mac_mcu_8125a_1(struct rtl8125_hw *hw)
{
	rtl8125_hw_disable_mac_mcu_bps(hw);
}

void
rtl8125_set_mac_mcu_8125a_2(struct rtl8125_hw *hw)
{
	static const u16 mcu_patch_code_8125a_2[] = {
		0xE010, 0xE012, 0xE022, 0xE024, 0xE029, 0xE02B, 0xE094, 0xE09D, 0xE09F,
		0xE0AA, 0xE0B5, 0xE0C6, 0xE0CC, 0xE0D1, 0xE0D6, 0xE0D8, 0xC602, 0xBE00,
		0x0000, 0xC60F, 0x73C4, 0x49B3, 0xF106, 0x73C2, 0xC608, 0xB406, 0xC609,
		0xFF80, 0xC605, 0xB406, 0xC605, 0xFF80, 0x0544, 0x0568, 0xE906, 0xCDE8,
		0xC602, 0xBE00, 0x0000, 0x48C1, 0x48C2, 0x9C46, 0xC402, 0xBC00, 0x0A12,
		0xC602, 0xBE00, 0x0EBA, 0x1501, 0xF02A, 0x1500, 0xF15D, 0xC661, 0x75C8,
		0x49D5, 0xF00A, 0x49D6, 0xF008, 0x49D7, 0xF006, 0x49D8, 0xF004, 0x75D2,
		0x49D9, 0xF150, 0xC553, 0x77A0, 0x75C8, 0x4855, 0x4856, 0x4857, 0x4858,
		0x48DA, 0x48DB, 0x49FE, 0xF002, 0x485A, 0x49FF, 0xF002, 0x485B, 0x9DC8,
		0x75D2, 0x4859, 0x9DD2, 0xC643, 0x75C0, 0x49D4, 0xF033, 0x49D0, 0xF137,
		0xE030, 0xC63A, 0x75C8, 0x49D5, 0xF00E, 0x49D6, 0xF00C, 0x49D7, 0xF00A,
		0x49D8, 0xF008, 0x75D2, 0x49D9, 0xF005, 0xC62E, 0x75C0, 0x49D7, 0xF125,
		0xC528, 0x77A0, 0xC627, 0x75C8, 0x4855, 0x4856, 0x4857, 0x4858, 0x48DA,
		0x48DB, 0x49FE, 0xF002, 0x485A, 0x49FF, 0xF002, 0x485B, 0x9DC8, 0x75D2,
		0x4859, 0x9DD2, 0xC616, 0x75C0, 0x4857, 0x9DC0, 0xC613, 0x75C0, 0x49DA,
		0xF003, 0x49D0, 0xF107, 0xC60B, 0xC50E, 0x48D9, 0x9DC0, 0x4859, 0x9DC0,
		0xC608, 0xC702, 0xBF00, 0x3AE0, 0xE860, 0xB400, 0xB5D4, 0xE908, 0xE86C,
		0x1200, 0xC409, 0x6780, 0x48F1, 0x8F80, 0xC404, 0xC602, 0xBE00, 0x10AA,
		0xC010, 0xEA7C, 0xC602, 0xBE00, 0x0000, 0x740A, 0x4846, 0x4847, 0x9C0A,
		0xC607, 0x74C0, 0x48C6, 0x9CC0, 0xC602, 0xBE00, 0x13FE, 0xE054, 0x72CA,
		0x4826, 0x4827, 0x9ACA, 0xC607, 0x72C0, 0x48A6, 0x9AC0, 0xC602, 0xBE00,
		0x07DC, 0xE054, 0xC60F, 0x74C4, 0x49CC, 0xF109, 0xC60C, 0x74CA, 0x48C7,
		0x9CCA, 0xC609, 0x74C0, 0x4846, 0x9CC0, 0xC602, 0xBE00, 0x2480, 0xE092,
		0xE0C0, 0xE054, 0x7420, 0x48C0, 0x9C20, 0x7444, 0xC602, 0xBE00, 0x12F8,
		0x1BFF, 0x46EB, 0x1BFF, 0xC102, 0xB900, 0x0D5A, 0x1BFF, 0x46EB, 0x1BFF,
		0xC102, 0xB900, 0x0E2A, 0xC602, 0xBE00, 0x0000, 0xC602, 0xBE00, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x6486,
		0x0B15, 0x090E, 0x1139
	};

	rtl8125_hw_disable_mac_mcu_bps(hw);

	rtl8125_write_mac_mcu_ram_code(hw, mcu_patch_code_8125a_2,
	                               ARRAY_SIZE(mcu_patch_code_8125a_2));

	rtl8125_mac_ocp_write(hw, 0xFC26, 0x8000);

	rtl8125_mac_ocp_write(hw, 0xFC2A, 0x0540);
	rtl8125_mac_ocp_write(hw, 0xFC2E, 0x0A06);
	rtl8125_mac_ocp_write(hw, 0xFC30, 0x0EB8);
	rtl8125_mac_ocp_write(hw, 0xFC32, 0x3A5C);
	rtl8125_mac_ocp_write(hw, 0xFC34, 0x10A8);
	rtl8125_mac_ocp_write(hw, 0xFC40, 0x0D54);
	rtl8125_mac_ocp_write(hw, 0xFC42, 0x0E24);

	rtl8125_mac_ocp_write(hw, 0xFC48, 0x307A);
}

/* ------------------------------------PHY 8125A--------------------------------------- */

static void
rtl8125_acquire_phy_mcu_patch_key_lock(struct rtl8125_hw *hw)
{
	u16 patch_key;

	switch (hw->mcfg) {
	case CFG_METHOD_2:
		patch_key = 0x8600;
		break;
	case CFG_METHOD_3:
	case CFG_METHOD_6:
		patch_key = 0x8601;
		break;
	case CFG_METHOD_4:
		patch_key = 0x3700;
		break;
	case CFG_METHOD_5:
	case CFG_METHOD_7:
		patch_key = 0x3701;
		break;
	default:
		return;
	}
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x8024);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, patch_key);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xB82E);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0001);
}

static void
rtl8125_release_phy_mcu_patch_key_lock(struct rtl8125_hw *hw)
{
	switch (hw->mcfg) {
	case CFG_METHOD_2:
	case CFG_METHOD_3:
	case CFG_METHOD_4:
	case CFG_METHOD_5:
	case CFG_METHOD_6:
	case CFG_METHOD_7:
		rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x0000);
		rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
		rtl8125_clear_eth_phy_ocp_bit(hw, 0xB82E, BIT_0);
		rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0x8024);
		rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
		break;
	default:
		break;
	}
}

static void
rtl8125_real_set_phy_mcu_8125a_1(struct rtl8125_hw *hw)
{
	rtl8125_acquire_phy_mcu_patch_key_lock(hw);


	rtl8125_set_eth_phy_ocp_bit(hw, 0xB820, BIT_7);


	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA016);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA012);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA014);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8010);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8013);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8021);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x802f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x803d);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8042);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8051);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8051);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa088);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0a50);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8008);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd014);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd1a3);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd700);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x401a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd707);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x40c2);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x60a6);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd700);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5f8b);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0a86);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0a6c);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8080);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd019);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd1a2);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd700);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x401a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd707);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x40c4);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x60a6);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd700);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5f8b);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0a86);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0a84);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd503);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8970);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0c07);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0901);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd500);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xce01);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xcf09);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd705);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x4000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xceff);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xaf0a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd504);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1213);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8401);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd500);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8580);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1253);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd064);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd181);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd704);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x4018);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd504);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xc50f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd706);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x2c59);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x804d);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xc60f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xf002);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xc605);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xae02);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x10fd);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA026);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xffff);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA024);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xffff);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA022);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x10f4);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA020);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1252);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA006);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1206);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA004);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0a78);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA002);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0a60);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0a4f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA008);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x3f00);


	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA016);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0010);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA012);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA014);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8010);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8066);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x807c);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8089);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x808e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x80a0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x80b2);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x80c2);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd501);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xce01);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd700);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x62db);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x655c);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd73e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x60e9);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x614a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x61ab);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0c0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0501);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0304);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0c0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0503);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0304);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0c0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0505);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0304);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0c0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0509);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0304);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x653c);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd73e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x60e9);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x614a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x61ab);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0c0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0503);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0304);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0c0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0502);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0304);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0c0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0506);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0304);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0c0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x050a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0304);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd73e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x60e9);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x614a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x61ab);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0c0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0505);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0304);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0c0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0506);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0304);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0c0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0504);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0304);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0c0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x050c);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0304);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd73e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x60e9);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x614a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x61ab);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0c0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0509);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0304);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0c0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x050a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0304);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0c0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x050c);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0304);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0c0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0508);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0304);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd501);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xce01);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd73e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x60e9);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x614a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x61ab);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0c0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0501);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0321);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0c0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0502);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0321);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0c0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0504);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0321);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0c0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0508);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0321);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0346);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd501);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xce01);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8208);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x609d);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa50f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x001a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0c0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0503);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x001a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x607d);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x00ab);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x00ab);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd501);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xce01);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd700);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x60fd);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa50f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xce00);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd500);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xaa0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x017b);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0c0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0503);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xce00);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd500);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0c0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0a05);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x017b);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd501);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xce01);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd700);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x60fd);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa50f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xce00);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd500);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xaa0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x01e0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0c0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0503);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xce00);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd500);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0c0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0a05);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x01e0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd700);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x60fd);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa50f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xce00);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd500);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xaa0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0231);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0c0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0503);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xce00);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd500);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0c0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0a05);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0231);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA08E);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xffff);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA08C);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0221);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA08A);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x01ce);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA088);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0169);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA086);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x00a6);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA084);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x000d);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA082);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0308);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA080);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x029f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA090);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x007f);


	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA016);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0020);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA012);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA014);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8010);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8017);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x801b);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8029);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8054);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x805a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8064);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x80a7);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x9430);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x9480);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb408);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd120);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd057);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x064b);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xcb80);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x9906);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0567);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xcb94);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8190);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x82a0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x800a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8406);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8010);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa740);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8dff);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x07e4);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa840);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0773);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xcb91);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd700);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x4063);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd139);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xf002);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd140);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd040);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb404);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0c0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0d00);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x07dc);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa610);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa110);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa2a0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa404);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd704);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x4045);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa180);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd704);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x405d);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa720);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0742);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x07ec);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd700);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5f74);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0742);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd702);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x7fb6);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8190);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x82a0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8404);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8610);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0c0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0d01);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x07dc);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x064b);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x07c0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd700);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5fa7);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0481);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x94bc);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x870c);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa190);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa00a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa280);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa404);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8220);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x078e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xcb92);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa840);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd700);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x4063);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd140);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xf002);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd150);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd040);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd703);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x60a0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x6121);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x61a2);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x6223);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xf02f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0cf0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0d10);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8010);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa740);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xf00f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0cf0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0d20);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8010);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa740);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xf00a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0cf0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0d30);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8010);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa740);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xf005);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0cf0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0d40);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8010);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa740);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x07e4);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa610);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa008);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd704);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x4046);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa002);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd704);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x405d);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa720);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0742);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x07f7);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd700);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5f74);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0742);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd702);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x7fb5);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x800a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0cf0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0d00);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x07e4);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8010);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa740);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd701);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x3ad4);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0537);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8610);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8840);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x064b);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8301);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x800a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8190);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x82a0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8404);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa70c);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x9402);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x890c);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8840);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x064b);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA10E);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0642);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA10C);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0686);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA10A);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0788);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA108);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x047b);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA106);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x065c);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA104);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0769);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA102);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0565);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA100);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x06f9);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA110);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x00ff);


	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xb87c);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8530);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xb87e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xaf85);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x3caf);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8593);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xaf85);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x9caf);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x85a5);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xbf86);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd702);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5afb);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xe083);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xfb0c);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x020d);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x021b);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x10bf);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x86d7);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x025a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb7bf);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x86da);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x025a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xfbe0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x83fc);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0c02);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0d02);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1b10);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xbf86);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xda02);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5ab7);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xbf86);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xdd02);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5afb);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xe083);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xfd0c);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x020d);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x021b);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x10bf);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x86dd);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x025a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb7bf);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x86e0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x025a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xfbe0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x83fe);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0c02);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0d02);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1b10);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xbf86);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xe002);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5ab7);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xaf2f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xbd02);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x2cac);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0286);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x65af);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x212b);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x022c);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x6002);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x86b6);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xaf21);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0cd1);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x03bf);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8710);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x025a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb7bf);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x870d);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x025a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb7bf);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8719);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x025a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb7bf);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8716);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x025a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb7bf);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x871f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x025a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb7bf);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x871c);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x025a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb7bf);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8728);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x025a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb7bf);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8725);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x025a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb7bf);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8707);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x025a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xfbad);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x281c);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd100);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xbf87);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0a02);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5ab7);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xbf87);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1302);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5ab7);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xbf87);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x2202);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5ab7);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xbf87);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x2b02);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5ab7);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xae1a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd101);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xbf87);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0a02);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5ab7);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xbf87);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1302);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5ab7);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xbf87);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x2202);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5ab7);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xbf87);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x2b02);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5ab7);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd101);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xbf87);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x3402);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5ab7);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xbf87);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x3102);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5ab7);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xbf87);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x3d02);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5ab7);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xbf87);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x3a02);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5ab7);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xbf87);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x4302);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5ab7);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xbf87);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x4002);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5ab7);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xbf87);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x4c02);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5ab7);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xbf87);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x4902);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5ab7);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd100);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xbf87);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x2e02);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5ab7);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xbf87);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x3702);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5ab7);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xbf87);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x4602);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5ab7);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xbf87);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x4f02);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5ab7);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xaf35);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x7ff8);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xfaef);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x69bf);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x86e3);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x025a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xfbbf);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x86fb);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x025a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb7bf);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x86e6);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x025a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xfbbf);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x86fe);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x025a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb7bf);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x86e9);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x025a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xfbbf);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8701);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x025a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb7bf);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x86ec);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x025a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xfbbf);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8704);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x025a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb7bf);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x86ef);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0262);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x7cbf);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x86f2);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0262);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x7cbf);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x86f5);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0262);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x7cbf);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x86f8);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0262);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x7cef);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x96fe);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xfc04);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xf8fa);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xef69);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xbf86);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xef02);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x6273);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xbf86);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xf202);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x6273);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xbf86);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xf502);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x6273);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xbf86);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xf802);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x6273);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xef96);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xfefc);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0420);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb540);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x53b5);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x4086);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb540);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb9b5);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x40c8);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb03a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xc8b0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xbac8);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb13a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xc8b1);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xba77);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xbd26);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xffbd);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x2677);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xbd28);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xffbd);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x2840);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xbd26);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xc8bd);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x2640);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xbd28);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xc8bd);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x28bb);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa430);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x98b0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1eba);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb01e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xdcb0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1e98);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb09e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xbab0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x9edc);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb09e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x98b1);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1eba);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb11e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xdcb1);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1e98);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb19e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xbab1);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x9edc);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb19e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x11b0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1e22);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb01e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x33b0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1e11);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb09e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x22b0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x9e33);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb09e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x11b1);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1e22);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb11e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x33b1);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1e11);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb19e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x22b1);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x9e33);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb19e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xb85e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x2f71);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xb860);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x20d9);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xb862);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x2109);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xb864);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x34e7);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xb878);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x000f);


	rtl8125_clear_eth_phy_ocp_bit(hw, 0xB820, BIT_7);


	rtl8125_release_phy_mcu_patch_key_lock(hw);
}

static void
rtl8125_real_set_phy_mcu_8125a_2(struct rtl8125_hw *hw)
{
	rtl8125_acquire_phy_mcu_patch_key_lock(hw);

	rtl8125_set_eth_phy_ocp_bit(hw, 0xB820, BIT_7);

	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA016);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA012);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA014);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8010);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x808b);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x808f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8093);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8097);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x809d);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x80a1);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x80aa);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd718);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x607b);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x40da);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xf00e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x42da);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xf01e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd718);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x615b);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1456);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x14a4);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x14bc);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd718);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5f2e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xf01c);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1456);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x14a4);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x14bc);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd718);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5f2e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xf024);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1456);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x14a4);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x14bc);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd718);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5f2e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xf02c);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1456);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x14a4);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x14bc);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd718);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5f2e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xf034);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd719);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x4118);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd504);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xac11);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd501);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xce01);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa410);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xce00);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd500);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x4779);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd504);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xac0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xae01);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd500);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1444);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xf034);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd719);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x4118);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd504);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xac22);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd501);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xce01);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa420);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xce00);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd500);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x4559);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd504);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xac0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xae01);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd500);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1444);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xf023);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd719);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x4118);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd504);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xac44);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd501);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xce01);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa440);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xce00);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd500);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x4339);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd504);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xac0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xae01);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd500);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1444);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xf012);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd719);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x4118);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd504);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xac88);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd501);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xce01);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa480);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xce00);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd500);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x4119);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd504);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xac0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xae01);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd500);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1444);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xf001);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1456);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd718);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5fac);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xc48f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x141b);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd504);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8010);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x121a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd0b4);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd1bb);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0898);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd0b4);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd1bb);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0a0e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd064);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd18a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0b7e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x401c);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd501);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa804);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8804);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x053b);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd500);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa301);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0648);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xc520);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa201);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd701);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x252d);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1646);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd708);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x4006);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1646);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0308);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA026);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0307);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA024);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1645);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA022);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0647);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA020);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x053a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA006);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0b7c);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA004);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0a0c);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA002);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0896);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x11a1);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA008);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xff00);


	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA016);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0010);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA012);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA014);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8010);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8015);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x801a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x801a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x801a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x801a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x801a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x801a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xad02);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x02d7);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x00ed);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0c0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0509);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xc100);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x008f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA08E);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xffff);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA08C);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xffff);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA08A);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xffff);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA088);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xffff);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA086);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xffff);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA084);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xffff);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA082);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x008d);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA080);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x00eb);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA090);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0103);


	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA016);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0020);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA012);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA014);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8010);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8014);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8018);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8024);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8051);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8055);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8072);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x80dc);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xfffd);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xfffd);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8301);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x800a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8190);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x82a0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8404);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa70c);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x9402);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x890c);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8840);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa380);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x066e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xcb91);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd700);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x4063);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd139);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xf002);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd140);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd040);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb404);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0c0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0d00);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x07e0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa610);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa110);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa2a0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa404);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd704);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x4085);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa180);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa404);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8280);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd704);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x405d);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa720);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0743);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x07f0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd700);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5f74);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0743);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd702);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x7fb6);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8190);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x82a0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8404);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8610);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0c0f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0d01);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x07e0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x066e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd158);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd04d);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x03d4);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x94bc);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x870c);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8380);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd10d);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd040);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x07c4);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd700);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5fb4);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa190);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa00a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa280);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa404);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa220);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd130);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd040);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x07c4);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd700);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5fb4);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xbb80);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd1c4);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd074);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa301);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd704);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x604b);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa90c);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0556);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xcb92);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd700);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x4063);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd116);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xf002);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd119);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd040);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd703);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x60a0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x6241);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x63e2);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x6583);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xf054);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd701);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x611e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd701);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x40da);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0cf0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0d10);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa010);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8740);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xf02f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0cf0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0d50);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8010);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa740);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xf02a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd701);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x611e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd701);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x40da);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0cf0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0d20);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa010);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8740);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xf021);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0cf0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0d60);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8010);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa740);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xf01c);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd701);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x611e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd701);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x40da);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0cf0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0d30);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa010);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8740);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xf013);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0cf0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0d70);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8010);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa740);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xf00e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd701);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x611e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd701);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x40da);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0cf0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0d40);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa010);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8740);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xf005);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0cf0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0d80);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8010);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa740);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x07e8);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa610);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd704);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x405d);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa720);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd700);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x5ff4);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa008);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd704);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x4046);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa002);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0743);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x07fb);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd703);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x7f6f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x7f4e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x7f2d);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x7f0c);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x800a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0cf0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0d00);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x07e8);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8010);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa740);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0743);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd702);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x7fb5);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd701);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x3ad4);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0556);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8610);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x066e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd1f5);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xd049);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x1800);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x01ec);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA10E);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x01ea);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA10C);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x06a9);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA10A);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x078a);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA108);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x03d2);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA106);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x067f);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA104);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0665);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA102);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA100);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0000);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xA110);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x00fc);


	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xb87c);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8530);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xb87e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xaf85);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x3caf);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8545);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xaf85);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x45af);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8545);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xee82);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xf900);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0103);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xaf03);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb7f8);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xe0a6);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x00e1);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa601);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xef01);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x58f0);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa080);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x37a1);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8402);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xae16);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa185);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x02ae);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x11a1);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8702);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xae0c);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xa188);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x02ae);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x07a1);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x8902);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xae02);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xae1c);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xe0b4);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x62e1);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb463);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x6901);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xe4b4);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x62e5);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb463);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xe0b4);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x62e1);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb463);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x6901);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xe4b4);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x62e5);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xb463);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xfc04);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xb85e);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x03b3);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xb860);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xffff);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xb862);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xffff);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xb864);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0xffff);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA436, 0xb878);
	rtl8125_mdio_direct_write_phy_ocp(hw, 0xA438, 0x0001);


	rtl8125_clear_eth_phy_ocp_bit(hw, 0xB820, BIT_7);


	rtl8125_release_phy_mcu_patch_key_lock(hw);
}

void
rtl8125_set_phy_mcu_8125a_1(struct rtl8125_hw *hw)
{
	rtl8125_set_phy_mcu_patch_request(hw);

	rtl8125_real_set_phy_mcu_8125a_1(hw);

	rtl8125_clear_phy_mcu_patch_request(hw);
}

void
rtl8125_set_phy_mcu_8125a_2(struct rtl8125_hw *hw)
{
	rtl8125_set_phy_mcu_patch_request(hw);

	rtl8125_real_set_phy_mcu_8125a_2(hw);

	rtl8125_clear_phy_mcu_patch_request(hw);
}