/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020 Realtek Technologies Co., Ltd
 */

#ifndef _R8125_ETHDEV_H_
#define _R8125_ETHDEV_H_

#include <stdint.h>
#include <stdbool.h>

#include <rte_ethdev.h>
#include <rte_ethdev_core.h>

#include "r8125_base.h"

#define R8125_LINK_CHECK_WAIT     4000 /* 1s */
#define R8125_LINK_CHECK_TIMEOUT  50   /* 10s */
#define R8125_LINK_CHECK_INTERVAL 200  /* ms */

struct rtl8125_hw;

struct rtl8125_hw_ops {
	void (*hw_config)(struct rtl8125_hw *hw);
	void (*hw_init_rxcfg)(struct rtl8125_hw *hw);
	void (*hw_ephy_config)(struct rtl8125_hw *hw);
	void (*hw_phy_config)(struct rtl8125_hw *hw);
	void (*hw_mac_mcu_config)(struct rtl8125_hw *hw);
	void (*hw_phy_mcu_config)(struct rtl8125_hw *hw);
};

struct rtl8125_hw {
	struct rtl8125_hw_ops hw_ops;
	struct rtl8125_counters *tally_vaddr;
	u8  *mmio_addr;
	u8  *cmac_ioaddr; /* cmac memory map physical address */
	u64 tally_paddr;
	u8  chipset_name;
	u32 mcfg;
	u32 mtu;
	u32 rx_buf_sz;
	u8  mac_addr[MAC_ADDR_LEN];
	u8  adapter_stopped;
	u16 cur_page;

	u16 sw_ram_code_ver;
	u16 hw_ram_code_ver;

	u16 phy_reg_anlpar;
	u8  efuse_ver;

	u8  autoneg;
	u8  duplex;
	u32 speed;
	u32 advertising;
	enum rtl8125_fc_mode fcpause;

	u8  HwIcVerUnknown;
	u8  NotWrRamCodeToMicroP;
	u8  NotWrMcuPatchCode;
	u8  HwHasWrRamCodeToMicroP;

	u8  HwSuppNowIsOobVer;

	u8  HwSuppCheckPhyDisableModeVer;

	/* EnableTxNoClose */
	u8  HwSuppTxNoCloseVer;
	u8  EnableTxNoClose;
	u32 NextHwDesCloPtr0;
	u32 BeginHwDesCloPtr0;
	u32 MaxTxDescPtrMask;
	u16 hw_clo_ptr_reg;
	u16 sw_tail_ptr_reg;

	int phy_auto_nego_reg;
	int phy_1000_ctrl_reg;

	int phy_2500_ctrl_reg;
	u8  RequirePhyMdiSwapPatch;
	u16 MacMcuPageSize;
	u8  HwSuppMacMcuVer;
	u32 HwSuppMaxPhyLinkSpeed;

	u16 mcu_pme_setting;

	u8 HwSuppIntMitiVer;

	/* Dash */
	u8 HwSuppDashVer;
	u8 DASH;
	u8 HwSuppOcpChannelVer;
	u8 AllowAccessDashOcp;
};

struct rtl8125_sw_stats {
	u64 tx_packets;
	u64 tx_bytes;
	u64 tx_errors;
	u64 rx_packets;
	u64 rx_bytes;
	u64 rx_errors;
};

struct rtl8125_adapter {
	struct rtl8125_hw       hw;
	struct rtl8125_sw_stats	sw_stats;
};

#define RTL8125_DEV_PRIVATE(eth_dev) \
	((struct rtl8125_adapter *)((eth_dev)->data->dev_private))

int rtl8125_rx_init(struct rte_eth_dev *dev);
int rtl8125_tx_init(struct rte_eth_dev *dev);

uint16_t rtl8125_xmit_pkts(void *txq, struct rte_mbuf **tx_pkts,
                           uint16_t nb_pkts);
uint16_t rtl8125_recv_pkts(void *rxq, struct rte_mbuf **rx_pkts,
                           uint16_t nb_pkts);
uint16_t rtl8125_recv_scattered_pkts(void *rxq, struct rte_mbuf **rx_pkts,
                                     uint16_t nb_pkts);

int rtl8125_stop_queues(struct rte_eth_dev *dev);
void rtl8125_free_queues(struct rte_eth_dev *dev);

void rtl8125_tx_queue_release(void *txq);
void rtl8125_rx_queue_release(void *rxq);

int rtl8125_tx_done_cleanup(void *tx_queue, uint32_t free_cnt);

int rtl8125_tx_queue_setup(struct rte_eth_dev *dev, uint16_t queue_idx,
                           uint16_t nb_tx_desc, unsigned int socket_id,
                           const struct rte_eth_txconf *tx_conf);

int rtl8125_rx_queue_setup(struct rte_eth_dev *dev, uint16_t queue_idx,
                           uint16_t nb_rx_desc, unsigned int socket_id,
                           const struct rte_eth_rxconf *rx_conf,
                           struct rte_mempool *mb_pool);

void rtl8125_rxq_info_get(struct rte_eth_dev *dev, uint16_t queue_idx,
                          struct rte_eth_rxq_info *qinfo);

void rtl8125_txq_info_get(struct rte_eth_dev *dev, uint16_t queue_idx,
                          struct rte_eth_txq_info *qinfo);

uint64_t rtl8125_get_tx_port_offloads(struct rte_eth_dev *dev __rte_unused);
uint64_t rtl8125_get_rx_port_offloads(struct rte_eth_dev *dev __rte_unused);

#endif /* _R8125_ETHDEV_H_ */