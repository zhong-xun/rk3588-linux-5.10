# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2020 Realtek Technologies Co., Ltd

include $(RTE_SDK)/mk/rte.vars.mk

#
# library name
#
LIB = librte_pmd_r8125.a

CFLAGS += -O3
CFLAGS += $(WERROR_FLAGS)
LDLIBS += -lrte_eal -lrte_mbuf -lrte_mempool -lrte_ring
LDLIBS += -lrte_ethdev -lrte_net -lrte_kvargs
LDLIBS += -lrte_bus_vdev

EXPORT_MAP := rte_pmd_r8125_version.map

VPATH += $(SRCDIR)/base

#
# all source are stored in SRCS-y
#
SRCS-$(CONFIG_RTE_LIBRTE_R8125_PMD) += r8125_rxtx.c
SRCS-$(CONFIG_RTE_LIBRTE_R8125_PMD) += r8125_ethdev.c
SRCS-$(CONFIG_RTE_LIBRTE_R8125_PMD) += r8125_hw.c
SRCS-$(CONFIG_RTE_LIBRTE_R8125_PMD) += r8125_phy.c
SRCS-$(CONFIG_RTE_LIBRTE_R8125_PMD) += r8125_dash.c
SRCS-$(CONFIG_RTE_LIBRTE_R8125_PMD) += rtl8125a.c
SRCS-$(CONFIG_RTE_LIBRTE_R8125_PMD) += rtl8125b.c
SRCS-$(CONFIG_RTE_LIBRTE_R8125_PMD) += rtl8125bp.c
SRCS-$(CONFIG_RTE_LIBRTE_R8125_PMD) += rtl8125d.c
SRCS-$(CONFIG_RTE_LIBRTE_R8125_PMD) += rtl8125a_mcu.c
SRCS-$(CONFIG_RTE_LIBRTE_R8125_PMD) += rtl8125b_mcu.c
SRCS-$(CONFIG_RTE_LIBRTE_R8125_PMD) += rtl8125bp_mcu.c
SRCS-$(CONFIG_RTE_LIBRTE_R8125_PMD) += rtl8125d_mcu.c

include $(RTE_SDK)/mk/rte.lib.mk