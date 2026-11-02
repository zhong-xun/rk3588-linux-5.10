/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020 Realtek Technologies Co., Ltd
 */

#ifndef _r8125_LOGS_H_
#define _r8125_LOGS_H_

extern int r8125_logtype_init;
#define PMD_INIT_LOG(level, fmt, args...) \
	rte_log(RTE_LOG_ ## level, r8125_logtype_init, \
		"%s(): " fmt "\n", __func__, ##args)

#define PMD_INIT_FUNC_TRACE() PMD_INIT_LOG(DEBUG, " >>")

#ifdef RTE_LIBRTE_R8125_DEBUG_RX
extern int r8125_logtype_rx;
#define PMD_RX_LOG(level, fmt, args...)			\
	rte_log(RTE_LOG_ ## level, r8125_logtype_rx,	\
		"%s(): " fmt "\n", __func__, ## args)
#else
#define PMD_RX_LOG(level, fmt, args...) do { } while (0)
#endif

#ifdef RTE_LIBRTE_R8125_DEBUG_TX
extern int r8125_logtype_tx;
#define PMD_TX_LOG(level, fmt, args...)			\
	rte_log(RTE_LOG_ ## level, r8125_logtype_tx,	\
		"%s(): " fmt "\n", __func__, ## args)
#else
#define PMD_TX_LOG(level, fmt, args...) do { } while (0)
#endif

#ifdef RTE_LIBRTE_R8125_DEBUG_TX_FREE
extern int r8125_logtype_tx_free;
#define PMD_TX_FREE_LOG(level, fmt, args...)			\
	rte_log(RTE_LOG_ ## level, r8125_logtype_tx_free,	\
		"%s(): " fmt "\n", __func__, ## args)
#else
#define PMD_TX_FREE_LOG(level, fmt, args...) do { } while (0)
#endif

extern int r8125_logtype_driver;
#define PMD_DRV_LOG_RAW(level, fmt, args...) \
	rte_log(RTE_LOG_ ## level, r8125_logtype_driver, "%s(): " fmt, \
		__func__, ## args)

#define PMD_DRV_LOG(level, fmt, args...) \
	PMD_DRV_LOG_RAW(level, fmt "\n", ## args)

#endif /* _r8125_LOGS_H_ */