/***************************************************************************//**
 * @file
 * @brief File System - NAND Device Generic Controller Micron On-Chip ECC
 *        Extension
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
 *                                       DEPENDENCIES & AVAIL CHECK(S)
 ********************************************************************************************************
 *******************************************************************************************************/

#include <rtos_description.h>

#if (defined(RTOS_MODULE_FS_STORAGE_NAND_AVAIL))

#if (!defined(RTOS_MODULE_FS_AVAIL))

#error NAND module requires File System Storage module. Make sure it is part of your project and that \
  RTOS_MODULE_FS_AVAIL is defined in rtos_description.h.

#endif

/********************************************************************************************************
 ********************************************************************************************************
 *                                               INCLUDE FILES
 ********************************************************************************************************
 *******************************************************************************************************/

//                                                                 ------------------------ FS ------------------------
#include  <fs/include/fs_nand_ctrlr_gen_ext_micron_ecc.h>
#include  <fs/source/storage/nand/fs_nand_ctrlr_gen_priv.h>
#include  <fs/source/shared/fs_utils_priv.h>

//                                                                 ----------------------- EXT ------------------------
#include  <cpu/include/cpu.h>
#include  <common/source/rtos/rtos_utils_priv.h>
#include  <common/source/logging/logging_priv.h>

/********************************************************************************************************
 ********************************************************************************************************
 *                                               LOCAL DEFINES
 ********************************************************************************************************
 *******************************************************************************************************/

#define  LOG_DFLT_CH                      (FS, DRV, NAND)
#define  RTOS_MODULE_CUR                   RTOS_CFG_MODULE_FS

#define  FS_NAND_CMD_RDSTATUS              0x70u
#define  FS_NAND_CMD_SET_FEATURES          0xEFu
#define  FS_NAND_CMD_GET_FEATURES          0xEEu
#define  FS_NAND_CMD_RDMODE                0x00u

/********************************************************************************************************
 *                                       STATUS REGISTER BIT DEFINES
 *******************************************************************************************************/

#define  FS_NAND_SR_WRPROTECT              DEF_BIT_07
#define  FS_NAND_SR_BUSY                   DEF_BIT_06
#define  FS_NAND_SR_REWRITE                DEF_BIT_03
#define  FS_NAND_SR_CACHEPGMFAIL           DEF_BIT_01
#define  FS_NAND_SR_FAIL                   DEF_BIT_00

/********************************************************************************************************
 ********************************************************************************************************
 *                                           LOCAL DATA TYPES
 ********************************************************************************************************
 *******************************************************************************************************/

typedef struct micron_ecc_data {
  const FS_NAND_CTRLR_DRV *DrvPtr;
} MICRON_ECC_DATA;

/********************************************************************************************************
 ********************************************************************************************************
 *                                       LOCAL FUNCTION PROTOTYPES
 ********************************************************************************************************
 *******************************************************************************************************/

static void *FS_NAND_CtrlrGen_MicronECC_Open(FS_NAND_CTRLR_GEN                   *p_ctrlr_gen,
                                             const FS_NAND_CTRLR_GEN_EXT_HW_INFO *p_gen_ext_hw_info,
                                             MEM_SEG                             *p_seg,
                                             RTOS_ERR                            *p_err);

static void FS_NAND_CtrlrGen_MicronECC_Close(void *p_ext_data);

static void FS_NAND_CtrlrGen_MicronECC_RdStatusChk(void     *p_ext_data,
                                                   RTOS_ERR *p_err);

/********************************************************************************************************
 ********************************************************************************************************
 *                               NAND GENERIC CTRLR MICRON HARDWARE ECC EXTENSION
 ********************************************************************************************************
 *******************************************************************************************************/

static const FS_NAND_CTRLR_GEN_EXT_API FS_NAND_CtrlrGen_MicronECC = {
  .Open = FS_NAND_CtrlrGen_MicronECC_Open,
  .Close = FS_NAND_CtrlrGen_MicronECC_Close,
  .Setup = DEF_NULL,
  .RdStatusChk = FS_NAND_CtrlrGen_MicronECC_RdStatusChk,
  .ECC_Calc = DEF_NULL,
  .ECC_Verify = DEF_NULL,
};

const FS_NAND_CTRLR_GEN_EXT_MICRON_ECC_HW_INFO FS_NAND_CtrlrGen_MicronECC_HwInfo = {
  .CtrlrGenExtHwInfo.CtrlrGenExtApiPtr = &FS_NAND_CtrlrGen_MicronECC
};

/********************************************************************************************************
 ********************************************************************************************************
 *                                               LOCAL FUNCTIONS
 ********************************************************************************************************
 *******************************************************************************************************/

/****************************************************************************************************//**
 *                                       FS_NAND_CtrlrGen_MicronECC_Open()
 *
 * @brief    Open extension module instance and enables the on-chip ECC hardware module.
 *
 * @param    p_ctrlr_gen         Pointer to a NAND generic controller instance.
 *
 * @param    p_gen_ext_hw_info   Pointer to a NAND generic controller extension hardware description
 *                               structure.
 *
 * @param    p_seg               Pointer to a memory segment where to allocate controller extension
 *                               internal data structures.
 *
 * @param    p_err               Error pointer.
 *
 * @return   Pointer to Micron ECC extension data.
 *******************************************************************************************************/
static void *FS_NAND_CtrlrGen_MicronECC_Open(FS_NAND_CTRLR_GEN                   *p_ctrlr_gen,
                                             const FS_NAND_CTRLR_GEN_EXT_HW_INFO *p_gen_ext_hw_info,
                                             MEM_SEG                             *p_seg,
                                             RTOS_ERR                            *p_err)
{
  MICRON_ECC_DATA             *p_micron_ecc_data;
  const FS_NAND_CTRLR_DRV_API *p_drv_api;
  CPU_INT08U                  cmd;
  CPU_INT08U                  addr;
  CPU_INT08U                  internal_ecc_data[4];

  PP_UNUSED_PARAM(p_gen_ext_hw_info);

  //                                                               --------------- ALLOC AND INIT DATA ----------------
  p_micron_ecc_data = (MICRON_ECC_DATA *)Mem_SegAlloc("FS - NAND Micron ECC data",
                                                      p_seg,
                                                      sizeof(MICRON_ECC_DATA),
                                                      p_err);
  if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
    return (DEF_NULL);
  }

  p_micron_ecc_data->DrvPtr = &p_ctrlr_gen->Drv;

  p_drv_api = p_ctrlr_gen->Drv.HW_InfoPtr->DrvApiPtr;

  //                                                               ------------------ ENABLE HW ECC -------------------
  cmd = FS_NAND_CMD_SET_FEATURES;
  addr = 0x90u;
  internal_ecc_data[0] = 0x08u;
  internal_ecc_data[1] = 0x00u;
  internal_ecc_data[2] = 0x00u;
  internal_ecc_data[3] = 0x00u;

  p_drv_api->ChipSelEn(&p_ctrlr_gen->Drv);

  FS_ERR_CHK_RTN(p_drv_api->CmdWr(&p_ctrlr_gen->Drv, &cmd, 1u, p_err),
                 p_drv_api->ChipSelDis(&p_ctrlr_gen->Drv),
                 p_micron_ecc_data);
  FS_ERR_CHK_RTN(p_drv_api->AddrWr(&p_ctrlr_gen->Drv, &addr, 1u, p_err),
                 p_drv_api->ChipSelDis(&p_ctrlr_gen->Drv),
                 p_micron_ecc_data);
  p_drv_api->DataWr(&p_ctrlr_gen->Drv, &internal_ecc_data[0], 4u, 8u, p_err);

  p_drv_api->ChipSelDis(&p_ctrlr_gen->Drv);

  //                                                               ------------------ CHECK HW ECC -------------------
  cmd = FS_NAND_CMD_GET_FEATURES;

  p_drv_api->ChipSelEn(&p_ctrlr_gen->Drv);

  FS_ERR_CHK_RTN(p_drv_api->CmdWr(&p_ctrlr_gen->Drv, &cmd, 1u, p_err),
                 p_drv_api->ChipSelDis(&p_ctrlr_gen->Drv),
                 p_micron_ecc_data);
  FS_ERR_CHK_RTN(p_drv_api->AddrWr(&p_ctrlr_gen->Drv, &addr, 1u, p_err),
                 p_drv_api->ChipSelDis(&p_ctrlr_gen->Drv),
                 p_micron_ecc_data);
  //                                                               Wait until rdy.
  p_drv_api->WaitWhileBusy(&p_ctrlr_gen->Drv,
                           DEF_NULL,
                           DEF_NULL,
                           5000u,
                           p_err);
  if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
    p_drv_api->ChipSelDis(&p_ctrlr_gen->Drv);
    LOG_DBG(("Timeout occurred when sending command."));
    RTOS_ERR_SET(*p_err, RTOS_ERR_TIMEOUT);
    return (p_micron_ecc_data);
  }

  p_drv_api->DataRd(&p_ctrlr_gen->Drv, &internal_ecc_data[0], 4u, 8u, p_err);

  p_drv_api->ChipSelDis(&p_ctrlr_gen->Drv);

  if (internal_ecc_data[0] != 0x08) {
    LOG_ERR(("Failed to enable on-chip ECC."));
    RTOS_ERR_SET(*p_err, RTOS_ERR_IO);
    return (p_micron_ecc_data);
  }

  return (p_micron_ecc_data);
}

/****************************************************************************************************//**
 *                                   FS_NAND_CtrlrGen_MicronECC_Close()
 *
 * @brief    Close extension module instance.
 *
 * @param    p_ext_data  Pointer to Micron controller extension data.
 *******************************************************************************************************/
static void FS_NAND_CtrlrGen_MicronECC_Close(void *p_ext_data)
{
  PP_UNUSED_PARAM(p_ext_data);
}

/****************************************************************************************************//**
 *                                   FS_NAND_CtrlrGen_MicronECC_RdStatusChk()
 *
 * @brief    Check NAND page read operation status for ECC errors.
 *
 * @param    p_ext_data  Pointer to Micron controller extension data.
 *
 * @param    p_err       Error pointer.
 *******************************************************************************************************/
static void FS_NAND_CtrlrGen_MicronECC_RdStatusChk(void     *p_ext_data,
                                                   RTOS_ERR *p_err)
{
  MICRON_ECC_DATA             *p_micron_ecc_data;
  const FS_NAND_CTRLR_DRV_API *p_drv_api;
  CPU_INT08U                  sr;
  CPU_INT08U                  cmd;

  p_micron_ecc_data = (MICRON_ECC_DATA *)p_ext_data;
  p_drv_api = p_micron_ecc_data->DrvPtr->HW_InfoPtr->DrvApiPtr;

  cmd = FS_NAND_CMD_RDSTATUS;
  FS_ERR_CHK(p_drv_api->CmdWr((FS_NAND_CTRLR_DRV *)p_micron_ecc_data->DrvPtr, &cmd, 1u, p_err),; );
  FS_ERR_CHK(p_drv_api->DataRd((FS_NAND_CTRLR_DRV *)p_micron_ecc_data->DrvPtr, &sr, 1u, 8u, p_err),; );

  if (DEF_BIT_IS_SET(sr, FS_NAND_SR_FAIL) == DEF_YES) {
    RTOS_ERR_SET(*p_err, RTOS_ERR_ECC_UNCORR);
  } else {
    if (DEF_BIT_IS_SET(sr, FS_NAND_SR_REWRITE) == DEF_YES) {
      RTOS_ERR_SET(*p_err, RTOS_ERR_ECC_CRITICAL_CORR);
    }
  }
}

/********************************************************************************************************
 ********************************************************************************************************
 *                                   DEPENDENCIES & AVAIL CHECK(S) END
 ********************************************************************************************************
 *******************************************************************************************************/

#endif // RTOS_MODULE_FS_STORAGE_NAND_AVAIL
