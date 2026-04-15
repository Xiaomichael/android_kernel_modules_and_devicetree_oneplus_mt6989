/******************************************************************************
** Copyright (C), 2019-2029, Oplus Mobile Comm Corp., Ltd
** File: oplus_sap_accelerate.h
** Description: add for accelerating real-time data packet in SAP mode
** Version: 1.0
** Date : 2025/09/20
** CONNECTIVITY.WIFI.BASIC.SOFTAP.10084878
** TAG: OPLUS_FEATURE_WIFI_SAP_ACCELERATE
** ------------------------------- Revision History: ----------------------------
** <author>     <data>   <version>
** ------------------------------------------------------------------------------
** Deng Jia   2025/09/20    1.0
*******************************************************************************/

#ifndef _OPLUS_SAP_ACCELERATE_H
#define _OPLUS_SAP_ACCELERATE_H

void oplus_sap_accelerate_module_init(void);
void oplus_sap_accelerate_module_deinit(void);
void oplus_enable_sap_accelerate_module(void);
void oplus_disable_sap_accelerate_module(void);
bool oplus_get_sap_acce_func_status(void);
void oplus_get_sap_acc_statistic_data(long*);
void oplus_nic_rx_mark_prior_pkt(struct SW_RFB*);
void oplus_kal_rx_save_prior_pkt_info(struct sk_buff*);
void oplus_kal_tx_accelerate_prior_pkt(struct sk_buff*);

#endif /* _OPLUS_SAP_ACCELERATE_H */
