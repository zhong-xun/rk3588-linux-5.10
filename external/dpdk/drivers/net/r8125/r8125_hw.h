/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020 Realtek Technologies Co., Ltd
 */

#ifndef _R8125_HW_H_
#define _R8125_HW_H_

#include <stdint.h>

#include <rte_bus_pci.h>
#include <rte_ethdev.h>
#include <rte_ethdev_core.h>

#include "r8125_base.h"
#include "r8125_ethdev.h"
#include "r8125_phy.h"
#include "r8125_dash.h"

u16 rtl8125_mac_ocp_read(struct rtl8125_hw *hw, u16 addr);
void rtl8125_mac_ocp_write(struct rtl8125_hw *hw, u16 addr, u16 value);

u32 rtl8125_csi_read(struct rtl8125_hw *hw, u32 addr);

void rtl8125_get_mac_version(struct rtl8125_hw *hw,
                             struct rte_pci_device *pci_dev);
int rtl8125_get_mac_address(struct rtl8125_hw *hw, struct rte_ether_addr *ea);

void rtl8125_enable_cfg9346_write(struct rtl8125_hw *hw);
void rtl8125_disable_cfg9346_write(struct rtl8125_hw *hw);

void rtl8125_rar_set(struct rtl8125_hw *hw, uint8_t *addr);

void rtl8125_hw_disable_mac_mcu_bps(struct rtl8125_hw *hw);

void rtl8125_disable_ups(struct rtl8125_hw *hw);

void rtl8125_disable_rxdvgate(struct rtl8125_hw *hw);

void rtl8125_init_software_variable(struct rtl8125_hw *hw);

void rtl8125_get_tally_stats(struct rtl8125_hw *hw,
                             struct rte_eth_stats *stats);
void rtl8125_clear_tally_stats(struct rtl8125_hw *hw);

void rtl8125_exit_oob(struct rtl8125_hw *hw);
void rtl8125_nic_reset(struct rtl8125_hw *hw);

int rtl8125_set_hw_ops(struct rtl8125_hw *hw);

void rtl8125_write_mac_mcu_ram_code(struct rtl8125_hw *hw, const u16 *entry,
                                    u16 entry_cnt);

void rtl8125_enable_force_clkreq(struct rtl8125_hw *hw, bool enable);
void rtl8125_enable_aspm_clkreq_lock(struct rtl8125_hw *hw, bool enable);

bool rtl8125_is_speed_mode_valid(u32 speed);

void rtl8125_ocp_write(struct rtl8125_hw *hw, u16 addr, u8 len, u32 value);
u32 rtl8125_ocp_read(struct rtl8125_hw *hw, u16 addr, u8 len);

void rtl8125_oob_mutex_lock(struct rtl8125_hw *hw);
void rtl8125_oob_mutex_unlock(struct rtl8125_hw *hw);

extern const struct rtl8125_hw_ops rtl8125a_ops;
extern const struct rtl8125_hw_ops rtl8125b_ops;
extern const struct rtl8125_hw_ops rtl8125bp_ops;
extern const struct rtl8125_hw_ops rtl8125d_ops;

#define RTL8125_MAC_MCU_PAGE_SIZE 256

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#define HW_SUPPORT_CHECK_PHY_DISABLE_MODE(_M) ((_M)->HwSuppCheckPhyDisableModeVer > 0 )
#define HW_HAS_WRITE_PHY_MCU_RAM_CODE(_M)     (((_M)->HwHasWrRamCodeToMicroP == TRUE) ? 1 : 0)
#define HW_SUPPORT_D0_SPEED_UP(_M)            ((_M)->HwSuppD0SpeedUpVer > 0)
#define HW_SUPPORT_MAC_MCU(_M)                ((_M)->HwSuppMacMcuVer > 0)

#define HW_SUPP_PHY_LINK_SPEED_GIGA(_M)	      ((_M)->HwSuppMaxPhyLinkSpeed >= 1000)
#define HW_SUPP_PHY_LINK_SPEED_2500M(_M)      ((_M)->HwSuppMaxPhyLinkSpeed >= 2500)

#define NO_BASE_ADDRESS       0x00000000

/* Tx NO CLOSE */
#define MAX_TX_NO_CLOSE_DESC_PTR_V2            0x10000
#define MAX_TX_NO_CLOSE_DESC_PTR_MASK_V2       0xFFFF
#define MAX_TX_NO_CLOSE_DESC_PTR_V3            0x100000000
#define MAX_TX_NO_CLOSE_DESC_PTR_MASK_V3       0xFFFFFFFF
#define MAX_TX_NO_CLOSE_DESC_PTR_V4            0x80000000
#define MAX_TX_NO_CLOSE_DESC_PTR_MASK_V4       0x7FFFFFFF
#define TX_NO_CLOSE_SW_PTR_MASK_V2             0x1FFFF

/* Channel wait count */
#define R8125_CHANNEL_WAIT_COUNT       20000
#define R8125_CHANNEL_WAIT_TIME        1   /* 1us */
#define R8125_CHANNEL_EXIT_DELAY_TIME  20  /* 20us */

#endif