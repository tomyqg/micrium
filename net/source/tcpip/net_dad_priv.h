/***************************************************************************//**
 * @file
 * @brief Network Dad Layer - (Duplication Address Detection)
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

#ifndef  _NET_DAD_PRIV_H_
#define  _NET_DAD_PRIV_H_

#include  "../../include/net_cfg_net.h"

#ifdef   NET_DAD_MODULE_EN

/********************************************************************************************************
 ********************************************************************************************************
 *                                               INCLUDE FILES
 ********************************************************************************************************
 *******************************************************************************************************/

#include  "net_ipv6_priv.h"
#include  "net_priv.h"

/********************************************************************************************************
 ********************************************************************************************************
 *                                               DATA TYPES
 ********************************************************************************************************
 *******************************************************************************************************/

/********************************************************************************************************
 *                                           DAD DATA TYPE
 *******************************************************************************************************/

typedef  struct  net_dad_obj NET_DAD_OBJ;

typedef  void (*NET_DAD_FNCT)(NET_IF_NBR               if_nbr,
                              NET_DAD_OBJ              *p_dad_obj,
                              NET_IPv6_ADDR_CFG_STATUS status);

struct net_dad_obj {
  NET_DAD_OBJ    *NextPtr;
  NET_IPv6_ADDR  Addr;
  KAL_SEM_HANDLE SignalErr;
  KAL_SEM_HANDLE SignalCompl;
  CPU_BOOLEAN    NotifyComplEn;
  NET_DAD_FNCT   Fnct;
};

/********************************************************************************************************
 *                                           DAD SIGNAL DATA TYPE
 *******************************************************************************************************/

typedef enum net_dad_signal_type {
  NET_DAD_SIGNAL_TYPE_ERR,
  NET_DAD_SIGNAL_TYPE_COMPL,
} NET_DAD_SIGNAL_TYPE;

typedef  enum net_dad_status {
  NET_DAD_STATUS_NONE,
  NET_DAD_STATUS_SUCCEED,
  NET_DAD_STATUS_IN_PROGRESS,
  NET_DAD_STATUS_FAIL
} NET_DAD_STATUS;

/********************************************************************************************************
 ********************************************************************************************************
 *                                           GLOBAL VARIABLES
 ********************************************************************************************************
 *******************************************************************************************************/

/********************************************************************************************************
 ********************************************************************************************************
 *                                               MACRO'S
 ********************************************************************************************************
 *******************************************************************************************************/

/********************************************************************************************************
 ********************************************************************************************************
 *                                           FUNCTION PROTOTYPES
 ********************************************************************************************************
 *******************************************************************************************************/

void NetDAD_Init(RTOS_ERR *p_err);

NET_DAD_STATUS NetDAD_Start(NET_IF_NBR             if_nbr,
                            NET_IPv6_ADDR          *p_addr,
                            NET_IPv6_ADDR_CFG_TYPE addr_cfg_type,
                            NET_DAD_FNCT           dad_hook_fnct,
                            RTOS_ERR               *p_err);

void NetDAD_Stop(NET_IF_NBR  if_nbr,
                 NET_DAD_OBJ *p_dad_obj);

NET_DAD_OBJ *NetDAD_ObjGet(RTOS_ERR *p_err);

void NetDAD_ObjRelease(NET_DAD_OBJ *p_dad_obj);

NET_DAD_OBJ *NetDAD_ObjSrch(NET_IPv6_ADDR *p_addr);

void NetDAD_SignalWait(NET_DAD_SIGNAL_TYPE signal_type,
                       NET_DAD_OBJ         *p_dad_obj,
                       RTOS_ERR            *p_err);

void NetDAD_Signal(NET_DAD_SIGNAL_TYPE signal_type,
                   NET_DAD_OBJ         *p_dad_obj);

/********************************************************************************************************
 ********************************************************************************************************
 *                                               MODULE END
 ********************************************************************************************************
 *******************************************************************************************************/

#endif  // NET_DAD_MODULE_EN
#endif  // _NET_DAD_PRIV_H_
