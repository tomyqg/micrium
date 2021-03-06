/***************************************************************************//**
 * @file
 * @brief USB Host Hub Declarations
 *******************************************************************************
 * # License
 * <b>Copyright 2018 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inc.  Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement.
 * The software is governed by the sections of the MSLA applicable to Micrium
 * Software.
 *
 ******************************************************************************/

/********************************************************************************************************
 ********************************************************************************************************
 *                                               MODULE
 ********************************************************************************************************
 *******************************************************************************************************/

#ifndef  _USBH_CORE_HUB_PRIV_H_
#define  _USBH_CORE_HUB_PRIV_H_

/********************************************************************************************************
 ********************************************************************************************************
 *                                               INCLUDE FILES
 ********************************************************************************************************
 *******************************************************************************************************/

#include  <cpu/include/cpu.h>

#include  <common/include/rtos_err.h>
#include  <common/include/rtos_path.h>
#include  <usbh_cfg.h>

#include  <usb/include/host/usbh_core.h>
#include  <usb/include/host/usbh_core_handle.h>

#include  <usb/source/host/core/usbh_core_types_priv.h>

/********************************************************************************************************
 ********************************************************************************************************
 *                                               EXTERNS
 ********************************************************************************************************
 *******************************************************************************************************/

#ifdef   HUB_MODULE
#define  HUB_EXT
#else
#define  HUB_EXT  extern
#endif

/********************************************************************************************************
 ********************************************************************************************************
 *                                               DATA TYPES
 ********************************************************************************************************
 *******************************************************************************************************/

/********************************************************************************************************
 ********************************************************************************************************
 *                                           GLOBAL VARIABLES
 ********************************************************************************************************
 *******************************************************************************************************/

#if (USBH_HUB_CFG_EXT_HUB_EN == DEF_ENABLED)
extern USBH_CLASS_DRV USBH_HUB_Drv;                             // Hub class driver.
#endif

/********************************************************************************************************
 ********************************************************************************************************
 *                                           FUNCTION PROTOTYPES
 ********************************************************************************************************
 *******************************************************************************************************/

void USBH_HUB_Init(CPU_INT08U hub_fnct_qty,
                   CPU_INT08U hub_event_qty,
                   RTOS_ERR   *p_err);

#if (USBH_CFG_UNINIT_EN == DEF_ENABLED)
void USBH_HUB_UnInit(RTOS_ERR *p_err);
#endif

void USBH_HUB_RootInit(USBH_HUB_FNCT *p_hub_fnct,
                       USBH_HOST     *p_host,
                       USBH_HC       *p_hc,
                       RTOS_ERR      *p_err);

void USBH_HUB_RootStart(USBH_HC  *p_hc,
                        RTOS_ERR *p_err);

void USBH_HUB_RootStop(USBH_HC  *p_hc,
                       RTOS_ERR *p_err);

void USBH_HUB_RootResume(USBH_HC  *p_hc,
                         RTOS_ERR *p_err);

void USBH_HUB_RootSuspend(USBH_HC  *p_hc,
                          RTOS_ERR *p_err);

void USBH_HUB_PortResetProcess(USBH_HUB_FNCT *p_hub_fnct,
                               CPU_INT16U    port_nbr,
                               RTOS_ERR      *p_err);

USBH_HUB_EVENT *USBH_HUB_EventAlloc(void);

void USBH_HUB_EventQPost(USBH_HUB_EVENT *p_hub_event,
                         RTOS_ERR       *p_err);

USBH_DEV *USBH_HUB_DevAtPortGet(USBH_HUB_FNCT *p_hub_fnct,
                                CPU_INT08U    port_nbr);

/********************************************************************************************************
 ********************************************************************************************************
 *                                               MODULE END
 ********************************************************************************************************
 *******************************************************************************************************/

#endif
