/***************************************************************************//**
 * @file
 * @brief File System - Quad SPI Management Layer
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

#ifndef  FS_NOR_QUAD_SPI_PRIV_H_
#define  FS_NOR_QUAD_SPI_PRIV_H_

/********************************************************************************************************
 ********************************************************************************************************
 *                                               INCLUDE FILES
 ********************************************************************************************************
 *******************************************************************************************************/

#include  <cpu/include/cpu.h>
#include  <common/include/lib_def.h>
#include  <common/include/lib_mem.h>
#include  <common/include/rtos_err.h>

#include  <fs/include/fs_nor_quad_spi.h>

/********************************************************************************************************
 ********************************************************************************************************
 *                                               DATA TYPES
 ********************************************************************************************************
 *******************************************************************************************************/

typedef struct quad_spi_ctrlr_info QUAD_SPI_CTRLR_INFO;

typedef struct quad_spi_slave_info QUAD_SPI_SLAVE_INFO;

/********************************************************************************************************
 *                                           NARROW TYPE BITFIELDS DEFINES
 *******************************************************************************************************/

#if (RTOS_TOOLCHAIN == RTOS_TOOLCHAIN_GNU && !defined(__STRICT_ANSI__))
typedef CPU_INT08U BITFIELD08U;
typedef CPU_INT16U BITFIELD16U;
typedef CPU_INT32U BITFIELD32U;
typedef CPU_INT64U BITFIELD64U;
#else
typedef unsigned int BITFIELD08U;
typedef unsigned int BITFIELD16U;
typedef unsigned int BITFIELD32U;
typedef unsigned int BITFIELD64U;
#endif

typedef struct quad_spi_cmd_form_flags {                        // ------------ QUAD SPI CMD FORMAT FLAGS -------------
  BITFIELD08U HasOpcode     : 1;                                // Cmd reqs transmission of an opcode.
  BITFIELD08U HasAddr       : 1;                                // Cmd reqs transmission of an address.
  BITFIELD08U OpcodeMultiIO : 1;                                // Opcode xfer in dual/quad mode.
  BITFIELD08U AddrMultiIO   : 1;                                // Addr/inter data xfer in dual/quad mode.
  BITFIELD08U DataMultiIO   : 1;                                // Data xfer in dual/quad mode.
  BITFIELD08U AddrLen4Bytes : 1;                                // DEF_YES: addr is 4 bytes, DEF_NO: addr is 3 bytes.
  BITFIELD08U IsWr          : 1;                                // DEF_YES: data xfer direction from host to slave.
  BITFIELD08U MultiIO_Quad  : 1;                                // Multi-IO is (DEF_YES) quad or (DEF_NO) dual mode.
} QUAD_SPI_CMD_FORM_FLAGS;

typedef struct quad_spi_cmd_desc {                              // ------------- QUAD SPI CMD DESCRIPTOR --------------
  CPU_INT08U              Opcode;                               // Value of cmd's opcode.
  QUAD_SPI_CMD_FORM_FLAGS Form;                                 // Flags to define form of cmd.
} QUAD_SPI_CMD_DESC;

//                                                                 ----------------- QUAD SPI DRV API -----------------
//                                                                 *INDENT-OFF*
typedef struct quad_spi_drv_api {
  void *(*Add)(const QUAD_SPI_CTRLR_INFO *p_hw_info,
               const QUAD_SPI_SLAVE_INFO *p_slave_info,
               MEM_SEG                   *p_seg,
               RTOS_ERR                  *p_err);

  void (*Start)(void     *p_drv_data,
                RTOS_ERR *p_err);

  void (*Stop)(void     *p_drv_data,
               RTOS_ERR *p_err);

  void (*ClkSet)(void       *p_drv_data,
                 CPU_INT32U clk,
                 RTOS_ERR   *p_err);

  void (*DTRSet)(void        *p_drv_data,
                 CPU_BOOLEAN en,
                 RTOS_ERR    *p_err);

  void (*FlashSizeSet)(void       *p_drv_data,
                       CPU_INT08U flash_size_log2,
                       RTOS_ERR   *p_err);

  void (*CmdSend)(void                    *p_drv_data,
                  const QUAD_SPI_CMD_DESC *p_cmd,
                  CPU_CHAR                addr_tbl[],
                  CPU_CHAR                inter_data[],
                  CPU_INT08U              inter_cycles,
                  void                    *p_xfer_data,
                  CPU_INT32U              xfer_size,
                  RTOS_ERR                *p_err);

  void (*WaitWhileBusy)(void                    *p_drv_data,
                        const QUAD_SPI_CMD_DESC *p_cmd,
                        CPU_INT32U              typical_dur,
                        CPU_INT32U              max_dur,
                        CPU_INT32U              status_reg_mask,
                        RTOS_ERR                *p_err);

  CPU_SIZE_T (*AlignReqGet)(void     *p_drv_data,
                            RTOS_ERR *p_err);

  void (*XipBitSet)(void       *p_drv_data,
                    CPU_INT08U dummy_byte,
                    RTOS_ERR   *p_err);

  void (*XipCfg)(void        *p_drv_data,
                 CPU_BOOLEAN enter,
                 RTOS_ERR    *p_err);
} QUAD_SPI_DRV_API;
//                                                                 *INDENT-ON*
/********************************************************************************************************
 ********************************************************************************************************
 *                                               MACRO'S
 ********************************************************************************************************
 *******************************************************************************************************/

#define  QUAD_SPI_CMD_INIT(family_name, cmd_mnemonic) {          \
    family_name ## _CMD_ ## cmd_mnemonic ## _OPCODE,             \
    {                                                            \
      family_name ## _CMD_ ## cmd_mnemonic ## _HAS_OPCODE,       \
      family_name ## _CMD_ ## cmd_mnemonic ## _HAS_ADDR,         \
      family_name ## _CMD_ ## cmd_mnemonic ## _MULTI_IO_OPCODE,  \
      family_name ## _CMD_ ## cmd_mnemonic ## _MULTI_IO_ADDR,    \
      family_name ## _CMD_ ## cmd_mnemonic ## _MULTI_IO_DATA,    \
      family_name ## _CMD_ ## cmd_mnemonic ## _ADDR_LEN_4_BYTES, \
      family_name ## _CMD_ ## cmd_mnemonic ## _IS_WR,            \
      family_name ## _CMD_ ## cmd_mnemonic ## _MULTI_IO_QUAD     \
    }                                                            \
}

/********************************************************************************************************
 ********************************************************************************************************
 *                                               MODULE END
 ********************************************************************************************************
 *******************************************************************************************************/

#endif
