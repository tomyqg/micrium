/***************************************************************************//**
 * @file
 * @brief File System - WINBOND W25Q DRIVER
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

/****************************************************************************************************//**
 * @note    (1) This driver supports Winbond W25Q80BL serial flash devices. Support for:
 *             - (a) Single I/O, Quad I/O data communication
 *
 * @note    (2) This driver does NOT support:
 *             - (a) Dual I/O data communication, that is commands 2READ and DREAD
 *******************************************************************************************************/

/********************************************************************************************************
 ********************************************************************************************************
 *                                     DEPENDENCIES & AVAIL CHECK(S)
 ********************************************************************************************************
 *******************************************************************************************************/

#include <rtos_description.h>

#if (defined(RTOS_MODULE_FS_STORAGE_NOR_AVAIL))

#if (!defined(RTOS_MODULE_FS_AVAIL))

#error NOR module requires File System Storage module. Make sure it is part of your project and that \
  RTOS_MODULE_FS_AVAIL is defined in rtos_description.h.

#endif

/********************************************************************************************************
 ********************************************************************************************************
 *                                            INCLUDE FILES
 ********************************************************************************************************
 *******************************************************************************************************/

//                                                                 ------------------------ FS ------------------------
#include  <fs/include/fs_nor_quad_spi.h>
#include  <fs/source/core/fs_core_priv.h>
#include  <fs/source/shared/fs_utils_priv.h>
#include  <fs/source/storage/nor/fs_nor_priv.h>

//                                                                 ----------------------- EXT ------------------------
#include  <cpu/include/cpu.h>
#include  <common/include/rtos_err.h>
#include  <common/include/lib_mem.h>
#include  <common/source/kal/kal_priv.h>

/********************************************************************************************************
 ********************************************************************************************************
 *                                            LOCAL DEFINES
 *
 * Note(s) : (1) The W25Q80BL devices clock frequency requirement is the following depending of the
 *               commands:
 *
 *               (a) Maximum clock frequency: 80 MHz for all commands except Read Data instruction(0x03u)
 *                   2.7V-3.6V VCC % Industrial Temperature.
 *
 *               (b) Maximum clock frequency: 50 MHz for all commands except Read Data instruction(0x03u)
 *                   2.3V-2.7V VCC % Industrial Temperature.
 *
 *               (c) Maximum clock frequency: 25 MHz for Read Data instruction(0x03u)
 *
 *           (2) Fast Read(0Bh), Fast Read Dual Output(3Bh) and Fast Read Quadouput(6Bh) require to add
 *               eight "Dummy" clocks after the 24-bit address is sent. The dummy clocks allow the
 *               device's internal circuits additional timer for setting up the initial address. The input
 *               data during the dummy clocks is "don't care". However, the IO pins should be high-impedance
 *               prior to the falling edge of the first data out clock.
 ********************************************************************************************************
 *******************************************************************************************************/

#define  LOG_DFLT_CH                            (FS, DRV, NOR)
#define  RTOS_MODULE_CUR                        RTOS_CFG_MODULE_FS

//                                                                 ------------------ FLASH DEV INFO ------------------
#define  FS_NOR_W25Q_PAGE_SIZE               256u               // Page size in bytes.
                                                                // Block size. W25Q family supports 32K or 64K.
#define  FS_NOR_W25Q_BLK_SIZE_LOG_2_32K       15u
#define  FS_NOR_W25Q_BLK_SIZE_LOG_2_64K       16u
#define  FS_NOR_W25Q_BLK_SIZE_LOG_2_SEL       FS_NOR_W25Q_BLK_SIZE_LOG_2_64K

#define  FS_NOR_W25Q_MAN_ID                  0xEFu              // JEDEC Manufacturer ID for Winbond.
#define  FS_NOR_W25Q_MEM_TYPE_A              0x40u

#define  FS_NOR_MEM_CAP_256Mb                0x19u
#define  FS_NOR_MEM_CAP_128Mb                0x18u
#define  FS_NOR_MEM_CAP_64Mb                 0x17u
#define  FS_NOR_MEM_CAP_32Mb                 0x16u
#define  FS_NOR_MEM_CAP_16Mb                 0x15u
#define  FS_NOR_MEM_CAP_8Mb                  0x14u

#define  FS_NOR_W25Q_DFLT_DUMMY_CYCLES          8u
#define  FS_NOR_W25Q_DUMMY_BYTE              0xA5u              // See Note #2

//                                                                 ----------------- FREQUENCY SETUP ------------------
//                                                                 See Note #1
#define  FS_NOR_W25Q80BL_2_7V_3_6V_CLK_MAX            80000000u
#define  FS_NOR_W25Q80BL_2_3V_2_7V_CLK_MAX            50000000u
#define  FS_NOR_W25Q80BL_RD_CMD_CLK_MAX               25000000u

#define  FS_NOR_NO_PHY_MODULE_CLK_MAX                 12000000u

#define  FS_NOR_W25Q80BL_CLK_SEL_MHz                    FS_NOR_NO_PHY_MODULE_CLK_MAX

//                                                                 ---------------------- OTHERS ----------------------
#define  FS_NOR_W25Q_REG_RD_RETRY_MAX                   0xFFFFu

/********************************************************************************************************
 *                                        WRITE OPERATIONS DELAY
 *
 * Note(s) : (1) These delays come from the Winbond W25Q80BL datasheet. They represent typical and maximum
 *               delays for the commands program/erase/write status register to complete.
 *
 *******************************************************************************************************/

#define  FS_NOR_W25Q_CMD_WRSR_DUR_TYPICAL_uS             10000u
#define  FS_NOR_W25Q_CMD_WRSR_DUR_MAX_uS                 15000u
#define  FS_NOR_W25Q_CMD_BLK_ERASE_TYPICAL_DUR_uS       180000u
#define  FS_NOR_W25Q_CMD_BLK_ERASE_MAX_DUR_uS           800000u
#define  FS_NOR_W25Q_CMD_CHIP_ERASE_TYPICAL_DUR_uS     3000000u
#define  FS_NOR_W25Q_CMD_CHIP_ERASE_MAX_DUR_uS         6000000u
#define  FS_NOR_W25Q_CMD_PP_TYPICAL_DUR_uS                 400u
#define  FS_NOR_W25Q_CMD_PP_MAX_DUR_uS                     800u

/********************************************************************************************************
 *                                        REGISTER BIT DEFINES
 *******************************************************************************************************/

//                                                                 -------------------- STATUS REG --------------------
#define  FS_NOR_W25Q_STATUS_REG2_SUS        DEF_BIT_15          // Suspend status bit.
#define  FS_NOR_W25Q_STATUS_REG2_CMP        DEF_BIT_14          // Complement protect bit.
#define  FS_NOR_W25Q_STATUS_REG2_LB3        DEF_BIT_13          // Security registerd lock bit.
#define  FS_NOR_W25Q_STATUS_REG2_LB2        DEF_BIT_12          // Security registerd lock bit.
#define  FS_NOR_W25Q_STATUS_REG2_LB1        DEF_BIT_11          // Security registerd lock bit.
#define  FS_NOR_W25Q_STATUS_REG2_RSVD       DEF_BIT_10          // Reserved bit.
#define  FS_NOR_W25Q_STATUS_REG2_QE         DEF_BIT_09          // Quad Enable bit.
#define  FS_NOR_W25Q_STATUS_REG2_SRP1       DEF_BIT_08          // Status register 2 write protect bit.

#define  FS_NOR_W25Q_STATUS_REG1_SRP0       DEF_BIT_07          // Status register 1 write protect bit.
#define  FS_NOR_W25Q_STATUS_REG1_SEC        DEF_BIT_06          // Sector/Block Protect bit.
#define  FS_NOR_W25Q_STATUS_REG1_TB         DEF_BIT_05          // Top/Bottom Block Protect.
#define  FS_NOR_W25Q_STATUS_REG1_BP2        DEF_BIT_04          // Level of protected block.
#define  FS_NOR_W25Q_STATUS_REG1_BP1        DEF_BIT_03          // Level of protected block.
#define  FS_NOR_W25Q_STATUS_REG1_BP0        DEF_BIT_02          // Level of protected block.
#define  FS_NOR_W25Q_STATUS_REG1_WEL        DEF_BIT_01          // Write enable latch bit.
#define  FS_NOR_W25Q_STATUS_REG1_BUSY       DEF_BIT_00          // Busy progress bit.
#define  FS_NOR_W25Q_STATUS_REG_BP_MASK     DEF_BIT_FIELD(3u, 2u)

/********************************************************************************************************
 *                                         NOR COMMAND DEFINES
 *******************************************************************************************************/

//                                                                 ------------------ REGISTERS CMDS ------------------
//                                                                 READ IDENTIFICATION cmd.
#define  FS_NOR_W25Q_CMD_RDID_OPCODE               0x9Fu
#define  FS_NOR_W25Q_CMD_RDID_HAS_OPCODE           DEF_YES
#define  FS_NOR_W25Q_CMD_RDID_HAS_ADDR             DEF_NO
#define  FS_NOR_W25Q_CMD_RDID_MULTI_IO_OPCODE      DEF_NO
#define  FS_NOR_W25Q_CMD_RDID_MULTI_IO_ADDR        DEF_NO
#define  FS_NOR_W25Q_CMD_RDID_MULTI_IO_DATA        DEF_NO
#define  FS_NOR_W25Q_CMD_RDID_ADDR_LEN_4_BYTES     DEF_NO
#define  FS_NOR_W25Q_CMD_RDID_IS_WR                DEF_NO
#define  FS_NOR_W25Q_CMD_RDID_MULTI_IO_QUAD        DEF_NO
//                                                                 READ STATUS REGISTER 1 cmd.
#define  FS_NOR_W25Q_CMD_RDSR_OPCODE               0x05u
#define  FS_NOR_W25Q_CMD_RDSR_HAS_OPCODE           DEF_YES
#define  FS_NOR_W25Q_CMD_RDSR_HAS_ADDR             DEF_NO
#define  FS_NOR_W25Q_CMD_RDSR_MULTI_IO_OPCODE      DEF_NO
#define  FS_NOR_W25Q_CMD_RDSR_MULTI_IO_ADDR        DEF_NO
#define  FS_NOR_W25Q_CMD_RDSR_MULTI_IO_DATA        DEF_NO
#define  FS_NOR_W25Q_CMD_RDSR_ADDR_LEN_4_BYTES     DEF_NO
#define  FS_NOR_W25Q_CMD_RDSR_IS_WR                DEF_NO
#define  FS_NOR_W25Q_CMD_RDSR_MULTI_IO_QUAD        DEF_NO

//                                                                 READ STATUS REGISTER 2 cmd.
#define  FS_NOR_W25Q_CMD_RDSR2_OPCODE              0x35u
#define  FS_NOR_W25Q_CMD_RDSR2_HAS_OPCODE          DEF_YES
#define  FS_NOR_W25Q_CMD_RDSR2_HAS_ADDR            DEF_NO
#define  FS_NOR_W25Q_CMD_RDSR2_MULTI_IO_OPCODE     DEF_NO
#define  FS_NOR_W25Q_CMD_RDSR2_MULTI_IO_ADDR       DEF_NO
#define  FS_NOR_W25Q_CMD_RDSR2_MULTI_IO_DATA       DEF_NO
#define  FS_NOR_W25Q_CMD_RDSR2_ADDR_LEN_4_BYTES    DEF_NO
#define  FS_NOR_W25Q_CMD_RDSR2_IS_WR               DEF_NO
#define  FS_NOR_W25Q_CMD_RDSR2_MULTI_IO_QUAD       DEF_NO

//                                                                 WRITE STATUS REGISTER cmd.
#define  FS_NOR_W25Q_CMD_WRSR_OPCODE               0x01u
#define  FS_NOR_W25Q_CMD_WRSR_HAS_OPCODE           DEF_YES
#define  FS_NOR_W25Q_CMD_WRSR_HAS_ADDR             DEF_NO
#define  FS_NOR_W25Q_CMD_WRSR_MULTI_IO_OPCODE      DEF_NO
#define  FS_NOR_W25Q_CMD_WRSR_MULTI_IO_ADDR        DEF_NO
#define  FS_NOR_W25Q_CMD_WRSR_MULTI_IO_DATA        DEF_NO
#define  FS_NOR_W25Q_CMD_WRSR_ADDR_LEN_4_BYTES     DEF_NO
#define  FS_NOR_W25Q_CMD_WRSR_IS_WR                DEF_YES
#define  FS_NOR_W25Q_CMD_WRSR_MULTI_IO_QUAD        DEF_NO

//                                                                 WRITE ENABLE cmd.
#define  FS_NOR_W25Q_CMD_WREN_OPCODE               0x06u
#define  FS_NOR_W25Q_CMD_WREN_HAS_OPCODE           DEF_YES
#define  FS_NOR_W25Q_CMD_WREN_HAS_ADDR             DEF_NO
#define  FS_NOR_W25Q_CMD_WREN_MULTI_IO_OPCODE      DEF_NO
#define  FS_NOR_W25Q_CMD_WREN_MULTI_IO_ADDR        DEF_NO
#define  FS_NOR_W25Q_CMD_WREN_MULTI_IO_DATA        DEF_NO
#define  FS_NOR_W25Q_CMD_WREN_ADDR_LEN_4_BYTES     DEF_NO
#define  FS_NOR_W25Q_CMD_WREN_IS_WR                DEF_NO
#define  FS_NOR_W25Q_CMD_WREN_MULTI_IO_QUAD        DEF_NO

//                                                                 WRITE DISABLE cmd.
#define  FS_NOR_W25Q_CMD_WRDI_OPCODE               0x04u
#define  FS_NOR_W25Q_CMD_WRDI_HAS_OPCODE           DEF_YES
#define  FS_NOR_W25Q_CMD_WRDI_HAS_ADDR             DEF_NO
#define  FS_NOR_W25Q_CMD_WRDI_MULTI_IO_OPCODE      DEF_NO
#define  FS_NOR_W25Q_CMD_WRDI_MULTI_IO_ADDR        DEF_NO
#define  FS_NOR_W25Q_CMD_WRDI_MULTI_IO_DATA        DEF_NO
#define  FS_NOR_W25Q_CMD_WRDI_ADDR_LEN_4_BYTES     DEF_NO
#define  FS_NOR_W25Q_CMD_WRDI_IS_WR                DEF_NO
#define  FS_NOR_W25Q_CMD_WRDI_MULTI_IO_QUAD        DEF_NO

//                                                                 FAST READ cmd.
#define  FS_NOR_W25Q_CMD_FREAD_OPCODE              0x0Bu
#define  FS_NOR_W25Q_CMD_FREAD_HAS_OPCODE          DEF_YES
#define  FS_NOR_W25Q_CMD_FREAD_HAS_ADDR            DEF_YES
#define  FS_NOR_W25Q_CMD_FREAD_MULTI_IO_OPCODE     DEF_NO
#define  FS_NOR_W25Q_CMD_FREAD_MULTI_IO_ADDR       DEF_NO
#define  FS_NOR_W25Q_CMD_FREAD_MULTI_IO_DATA       DEF_NO
#define  FS_NOR_W25Q_CMD_FREAD_ADDR_LEN_4_BYTES    DEF_NO
#define  FS_NOR_W25Q_CMD_FREAD_IS_WR               DEF_NO
#define  FS_NOR_W25Q_CMD_FREAD_MULTI_IO_QUAD       DEF_NO

//                                                                 QUAD READ cmd.
#define  FS_NOR_W25Q_CMD_QREAD_OPCODE              0x6Bu
#define  FS_NOR_W25Q_CMD_QREAD_HAS_OPCODE          DEF_YES
#define  FS_NOR_W25Q_CMD_QREAD_HAS_ADDR            DEF_YES
#define  FS_NOR_W25Q_CMD_QREAD_MULTI_IO_OPCODE     DEF_NO
#define  FS_NOR_W25Q_CMD_QREAD_MULTI_IO_ADDR       DEF_NO
#define  FS_NOR_W25Q_CMD_QREAD_MULTI_IO_DATA       DEF_YES
#define  FS_NOR_W25Q_CMD_QREAD_ADDR_LEN_4_BYTES    DEF_NO
#define  FS_NOR_W25Q_CMD_QREAD_IS_WR               DEF_NO
#define  FS_NOR_W25Q_CMD_QREAD_MULTI_IO_QUAD       DEF_YES

//                                                                 BLOCK ERASE 32K cmd.
#define  FS_NOR_W25Q_CMD_BE32_OPCODE               0x52u
#define  FS_NOR_W25Q_CMD_BE32_HAS_OPCODE           DEF_YES
#define  FS_NOR_W25Q_CMD_BE32_HAS_ADDR             DEF_YES
#define  FS_NOR_W25Q_CMD_BE32_MULTI_IO_OPCODE      DEF_NO
#define  FS_NOR_W25Q_CMD_BE32_MULTI_IO_ADDR        DEF_NO
#define  FS_NOR_W25Q_CMD_BE32_MULTI_IO_DATA        DEF_NO
#define  FS_NOR_W25Q_CMD_BE32_ADDR_LEN_4_BYTES     DEF_NO
#define  FS_NOR_W25Q_CMD_BE32_IS_WR                DEF_YES
#define  FS_NOR_W25Q_CMD_BE32_MULTI_IO_QUAD        DEF_NO

//                                                                 BLOCK ERASE 64K cmd.
#define  FS_NOR_W25Q_CMD_BE64_OPCODE               0xD8u
#define  FS_NOR_W25Q_CMD_BE64_HAS_OPCODE           DEF_YES
#define  FS_NOR_W25Q_CMD_BE64_HAS_ADDR             DEF_YES
#define  FS_NOR_W25Q_CMD_BE64_MULTI_IO_OPCODE      DEF_NO
#define  FS_NOR_W25Q_CMD_BE64_MULTI_IO_ADDR        DEF_NO
#define  FS_NOR_W25Q_CMD_BE64_MULTI_IO_DATA        DEF_NO
#define  FS_NOR_W25Q_CMD_BE64_ADDR_LEN_4_BYTES     DEF_NO
#define  FS_NOR_W25Q_CMD_BE64_IS_WR                DEF_YES
#define  FS_NOR_W25Q_CMD_BE64_MULTI_IO_QUAD        DEF_NO

//                                                                 CHIP ERASE cmd.
#define  FS_NOR_W25Q_CMD_CE_OPCODE                 0xC7u
#define  FS_NOR_W25Q_CMD_CE_HAS_OPCODE             DEF_YES
#define  FS_NOR_W25Q_CMD_CE_HAS_ADDR               DEF_NO
#define  FS_NOR_W25Q_CMD_CE_MULTI_IO_OPCODE        DEF_NO
#define  FS_NOR_W25Q_CMD_CE_MULTI_IO_ADDR          DEF_NO
#define  FS_NOR_W25Q_CMD_CE_MULTI_IO_DATA          DEF_NO
#define  FS_NOR_W25Q_CMD_CE_ADDR_LEN_4_BYTES       DEF_NO
#define  FS_NOR_W25Q_CMD_CE_IS_WR                  DEF_YES
#define  FS_NOR_W25Q_CMD_CE_MULTI_IO_QUAD          DEF_NO

//                                                                 PAGE PROGRAM cmd.
#define  FS_NOR_W25Q_CMD_PP_OPCODE                 0x02u
#define  FS_NOR_W25Q_CMD_PP_HAS_OPCODE             DEF_YES
#define  FS_NOR_W25Q_CMD_PP_HAS_ADDR               DEF_YES
#define  FS_NOR_W25Q_CMD_PP_MULTI_IO_OPCODE        DEF_NO
#define  FS_NOR_W25Q_CMD_PP_MULTI_IO_ADDR          DEF_NO
#define  FS_NOR_W25Q_CMD_PP_MULTI_IO_DATA          DEF_NO
#define  FS_NOR_W25Q_CMD_PP_ADDR_LEN_4_BYTES       DEF_NO
#define  FS_NOR_W25Q_CMD_PP_IS_WR                  DEF_YES
#define  FS_NOR_W25Q_CMD_PP_MULTI_IO_QUAD          DEF_NO

//                                                                 QUAD PAGE PROGRAM cmd.
#define  FS_NOR_W25Q_CMD_4PP_OPCODE                 0x32u
#define  FS_NOR_W25Q_CMD_4PP_HAS_OPCODE             DEF_YES
#define  FS_NOR_W25Q_CMD_4PP_HAS_ADDR               DEF_YES
#define  FS_NOR_W25Q_CMD_4PP_MULTI_IO_OPCODE        DEF_NO
#define  FS_NOR_W25Q_CMD_4PP_MULTI_IO_ADDR          DEF_NO
#define  FS_NOR_W25Q_CMD_4PP_MULTI_IO_DATA          DEF_YES
#define  FS_NOR_W25Q_CMD_4PP_ADDR_LEN_4_BYTES       DEF_NO
#define  FS_NOR_W25Q_CMD_4PP_IS_WR                  DEF_YES
#define  FS_NOR_W25Q_CMD_4PP_MULTI_IO_QUAD          DEF_YES

/********************************************************************************************************
 ********************************************************************************************************
 *                                          LOCAL DATA TYPES
 ********************************************************************************************************
 *******************************************************************************************************/

typedef  struct  fs_nor_phy_w25q {
  FS_NOR_PHY                    Phy;                            // NOR flash info.
  const FS_NOR_QUAD_SPI_DRV_API *QuadSpiDrvApiPtr;              // Pointer to Quad SPI driver API.
  void                          *QuadSpiDrvDataPtr;             // Pointer to Quad SPI driver private data.
  CPU_BOOLEAN                   QuadIOModeEn;                   // Flag indicating if Quad IO enabled by user.
  CPU_INT08U                    DummyCycleCnt;                  // Dummy cycles used by some READ commands.
} FS_NOR_PHY_W25Q;

/********************************************************************************************************
 ********************************************************************************************************
 *                                            LOCAL MACROS
 ********************************************************************************************************
 *******************************************************************************************************/

#define  FS_NOR_CMD_INIT(family_name, cmd_mnemonic) {            \
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
 *                                       LOCAL GLOBAL VARIABLES
 ********************************************************************************************************
 *******************************************************************************************************/

static FS_NOR_QUAD_SPI_SLAVE_INFO FS_NOR_PHY_W25Q_SlaveInfo = {
  .Mode = FS_NOR_SPI_MODE_0,                                    // W25Q supports serial mode 0 or 3.
  .LSbFirst = DEF_NO,                                           // MSB outputted first.
  .IdleByte = 0u,
  .HasDTR = DEF_NO,                                             // W25Q80BL does not support Double Transfer Rate.
  .ClkFreqMax = FS_NOR_W25Q80BL_CLK_SEL_MHz,
  .PageSize = FS_NOR_W25Q_PAGE_SIZE,
  .BlkSizeLog2 = FS_NOR_W25Q_BLK_SIZE_LOG_2_SEL,
};

/********************************************************************************************************
 *                                   COMMAND DESCRIPTOR DEFINITIONS
 *
 * Note(s) : (1) The WRITE ENABLE operation (FS_NOR_W25Q_CMD_WREN) sets the write enable latch bit in
 *               the Status register. The write enable latch bit must be set before every PROGRAM, ERASE
 *               and WRITE STATUS REGISTER commands.
 *
 *******************************************************************************************************/

static const FS_NOR_QUAD_SPI_CMD_DESC FS_NOR_W25Q_CMD_RDID = FS_NOR_CMD_INIT(FS_NOR_W25Q, RDID);
static const FS_NOR_QUAD_SPI_CMD_DESC FS_NOR_W25Q_CMD_WREN = FS_NOR_CMD_INIT(FS_NOR_W25Q, WREN);
static const FS_NOR_QUAD_SPI_CMD_DESC FS_NOR_W25Q_CMD_WRDI = FS_NOR_CMD_INIT(FS_NOR_W25Q, WRDI);
static const FS_NOR_QUAD_SPI_CMD_DESC FS_NOR_W25Q_CMD_RDSR = FS_NOR_CMD_INIT(FS_NOR_W25Q, RDSR);
static const FS_NOR_QUAD_SPI_CMD_DESC FS_NOR_W25Q_CMD_RDSR2 = FS_NOR_CMD_INIT(FS_NOR_W25Q, RDSR2);
static const FS_NOR_QUAD_SPI_CMD_DESC FS_NOR_W25Q_CMD_WRSR = FS_NOR_CMD_INIT(FS_NOR_W25Q, WRSR);

static const FS_NOR_QUAD_SPI_CMD_DESC FS_NOR_W25Q_CMD_FREAD = FS_NOR_CMD_INIT(FS_NOR_W25Q, FREAD);
static const FS_NOR_QUAD_SPI_CMD_DESC FS_NOR_W25Q_CMD_QREAD = FS_NOR_CMD_INIT(FS_NOR_W25Q, QREAD);
static const FS_NOR_QUAD_SPI_CMD_DESC FS_NOR_W25Q_CMD_BE32 = FS_NOR_CMD_INIT(FS_NOR_W25Q, BE32);
static const FS_NOR_QUAD_SPI_CMD_DESC FS_NOR_W25Q_CMD_BE64 = FS_NOR_CMD_INIT(FS_NOR_W25Q, BE64);
static const FS_NOR_QUAD_SPI_CMD_DESC FS_NOR_W25Q_CMD_CE = FS_NOR_CMD_INIT(FS_NOR_W25Q, CE);
static const FS_NOR_QUAD_SPI_CMD_DESC FS_NOR_W25Q_CMD_PP = FS_NOR_CMD_INIT(FS_NOR_W25Q, PP);
static const FS_NOR_QUAD_SPI_CMD_DESC FS_NOR_W25Q_CMD_4PP = FS_NOR_CMD_INIT(FS_NOR_W25Q, 4PP);

/********************************************************************************************************
 ********************************************************************************************************
 *                                      LOCAL FUNCTION PROTOTYPES
 ********************************************************************************************************
 *******************************************************************************************************/

/********************************************************************************************************
 *                                  PHY INTERFACE FUNCTION PROTOTYPES
 *******************************************************************************************************/

static FS_NOR_PHY *FS_NOR_W25Q_Add(const FS_NOR_PM_ITEM *p_nor_pm_item,
                                   MEM_SEG              *p_seg,
                                   RTOS_ERR             *p_err);

static void FS_NOR_W25Q_Open(FS_NOR_PHY *p_phy,
                             RTOS_ERR   *p_err);

static void FS_NOR_W25Q_Close(FS_NOR_PHY *p_phy,
                              RTOS_ERR   *p_err);

static void FS_NOR_W25Q_Rd(FS_NOR_PHY *p_phy,
                           void       *p_dest,
                           CPU_INT32U start_addr,
                           CPU_INT32U cnt,
                           RTOS_ERR   *p_err);

static void FS_NOR_W25Q_Wr(FS_NOR_PHY *p_phy,
                           void       *p_src,
                           CPU_INT32U start_addr,
                           CPU_INT32U cnt,
                           RTOS_ERR   *p_err);

static void FS_NOR_W25Q_BlkErase(FS_NOR_PHY *p_phy,
                                 CPU_INT32U start_addr,
                                 CPU_INT32U size,
                                 RTOS_ERR   *p_err);

static void FS_NOR_W25Q_ChipErase(FS_NOR_PHY *p_phy,
                                  RTOS_ERR   *p_err);

static void FS_NOR_W25Q_XipCfg(FS_NOR_PHY  *p_phy,
                               CPU_BOOLEAN xip_en,
                               RTOS_ERR    *p_err);

static CPU_SIZE_T FS_NOR_W25Q_AlignReqGet(FS_NOR_PHY *p_phy,
                                          RTOS_ERR   *p_err);

/********************************************************************************************************
 *                                      LOCAL FUNCTION PROTOTYPES
 *******************************************************************************************************/

static void FS_NOR_W25Q_WrEn(FS_NOR_PHY_W25Q *p_phy_w25q,
                             RTOS_ERR        *p_err);

static void FS_NOR_W25Q_WrCmplWait(FS_NOR_PHY_W25Q *p_phy_w25q,
                                   CPU_INT32U      typical_dur,
                                   CPU_INT32U      max_dur,
                                   RTOS_ERR        *p_err);

/********************************************************************************************************
 ********************************************************************************************************
 *                                         INTERFACE STRUCTURE
 ********************************************************************************************************
 *******************************************************************************************************/

const FS_NOR_PHY_API FS_NOR_PHY_W25Q_API = {
  .Add = FS_NOR_W25Q_Add,
  .Open = FS_NOR_W25Q_Open,
  .Close = FS_NOR_W25Q_Close,
  .Rd = FS_NOR_W25Q_Rd,
  .Wr = FS_NOR_W25Q_Wr,
  .BlkErase = FS_NOR_W25Q_BlkErase,
  .ChipErase = FS_NOR_W25Q_ChipErase,
  .XipCfg = FS_NOR_W25Q_XipCfg,
  .AlignReqGet = FS_NOR_W25Q_AlignReqGet
};

/********************************************************************************************************
 ********************************************************************************************************
 *                                           LOCAL FUNCTIONS
 ********************************************************************************************************
 *******************************************************************************************************/

/********************************************************************************************************
 *                               NOR PHYSICAL DRIVER INTERFACE FUNCTIONS
 *******************************************************************************************************/

/****************************************************************************************************//**
 *                                         FS_NOR_W25Q_Add()
 *
 * @brief   Add a NOR PHY instance.
 *
 * @param   p_pm_item   Pointer to a NOR platform manager item.
 *
 * @param   p_seg       Pointer to a memory segment where to allocate the internal data structures.
 *
 * @param   p_err       Error pointer.
 *
 * @return  Pointer to the added NOR PHY instance.
 *******************************************************************************************************/
static FS_NOR_PHY *FS_NOR_W25Q_Add(const FS_NOR_PM_ITEM *p_pm_item,
                                   MEM_SEG              *p_seg,
                                   RTOS_ERR             *p_err)
{
  FS_NOR_PHY_W25Q               *p_phy_w25q;
  FS_NOR_QUAD_SPI_PM_ITEM       *p_quad_spi_nor_pm_item = (FS_NOR_QUAD_SPI_PM_ITEM *)p_pm_item;
  const FS_NOR_QUAD_SPI_DRV_API *p_drv_api;
  void                          *p_drv_data;
  CPU_INT08U                    read_id[3];
  CPU_INT32U                    mem_capa;

  //                                                               Octal IO not supported by W25Q dev & Dual IO mode...
  //                                                               ...not implemented in this drv.
  RTOS_ASSERT_DBG_ERR_SET(((p_quad_spi_nor_pm_item->HwInfoPtr->CtrlrHwInfoPtr->BusWidth == FS_NOR_SERIAL_BUS_WIDTH_SINGLE_IO)
                           || (p_quad_spi_nor_pm_item->HwInfoPtr->CtrlrHwInfoPtr->BusWidth == FS_NOR_SERIAL_BUS_WIDTH_QUAD_IO)), *p_err, RTOS_ERR_NOT_SUPPORTED,; );

  //                                                               Alloc NOR PHY internal data.
  p_phy_w25q = (FS_NOR_PHY_W25Q *)Mem_SegAlloc("FS - NOR PHY W25Q instance",
                                               p_seg,
                                               sizeof(FS_NOR_PHY_W25Q),
                                               p_err);
  if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
    RTOS_ERR_CONTEXT_REFRESH(*p_err);
    LOG_ERR(("Couldn't allocate PHY data w/ err = ", RTOS_ERR_LOG_ARG_GET(*p_err), "."));
    return (DEF_NULL);
  }
  Mem_Clr(p_phy_w25q, sizeof(FS_NOR_PHY_W25Q));
  //                                                               Add QSPI controller.
  p_drv_api = p_quad_spi_nor_pm_item->HwInfoPtr->CtrlrHwInfoPtr->DrvApiPtr;
  FS_NOR_PHY_W25Q_SlaveInfo.ChipSelID = p_quad_spi_nor_pm_item->HwInfoPtr->PartHwInfo.ChipSelID;

  p_drv_data = p_drv_api->Add(p_quad_spi_nor_pm_item->HwInfoPtr->CtrlrHwInfoPtr,
                              &FS_NOR_PHY_W25Q_SlaveInfo,
                              p_seg,
                              p_err);
  if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
    LOG_ERR(("Couldn't add cmd interface w/ err = ", RTOS_ERR_LOG_ARG_GET(*p_err), "."));
    return (DEF_NULL);
  }

  if (p_drv_api->DTR_Set != DEF_NULL) {                         // Set Double Transfer Rate support.
    p_drv_api->DTR_Set(p_drv_data,
                       DEF_DISABLED,                            // W25Q dev does not support DTR.
                       p_err);
    if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
      return (DEF_NULL);
    }
  }
  //                                                               Set clk for setup phase.
  p_drv_api->ClkSet(p_drv_data,
                    FS_NOR_W25Q80BL_CLK_SEL_MHz,
                    p_err);
  if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
    return (DEF_NULL);
  }

  p_drv_api->Start(p_drv_data, p_err);                          // Start QSPI operations for initial dev info.
  if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
    return (DEF_NULL);
  }

  //                                                               ------------------- GET DEV INFO -------------------
  //                                                               Get device ID and capacity.
  p_drv_api->CmdSend(p_drv_data,
                     &FS_NOR_W25Q_CMD_RDID,
                     FS_NOR_PHY_CMD_ADDR_PHASE_NONE_PTR,
                     FS_NOR_PHY_CMD_DUMMY_CYCLES_NONE_PTR,
                     FS_NOR_PHY_CMD_DUMMY_CYCLES_NONE,
                     &read_id[0],
                     3u,
                     p_err);
  if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
    LOG_ERR(("Couldn't send RDID (Read ID) cmd to NOR device w/ err = ", RTOS_ERR_LOG_ARG_GET(*p_err), "."));
    return (DEF_NULL);
  }

  if (read_id[0] != FS_NOR_W25Q_MAN_ID) {                       // Validate manuf and device ID.
    RTOS_ERR_SET(*p_err, RTOS_ERR_IO);
    LOG_ERR(("Invalid manufacturer ID (read ", (X)read_id[0], ", expected ", (X)FS_NOR_W25Q_MAN_ID, ")."));
    return (DEF_NULL);
  }
  if (read_id[1] != FS_NOR_W25Q_MEM_TYPE_A) {
    RTOS_ERR_SET(*p_err, RTOS_ERR_IO);
    LOG_ERR(("Invalid memory type in device ID (read ", (X)read_id[1], ", expected ", (X)FS_NOR_W25Q_MEM_TYPE_A, ")."));
    return (DEF_NULL);
  }
  //                                                               Compute the memory capacity.
  switch (read_id[2]) {
    case FS_NOR_MEM_CAP_256Mb:
    case FS_NOR_MEM_CAP_128Mb:
    case FS_NOR_MEM_CAP_64Mb:
    case FS_NOR_MEM_CAP_32Mb:
    case FS_NOR_MEM_CAP_16Mb:
    case FS_NOR_MEM_CAP_8Mb:
      mem_capa = 1u << read_id[2];
      break;

    default:
      RTOS_ERR_SET(*p_err, RTOS_ERR_NOT_SUPPORTED);
      return (DEF_NULL);
  }

  if (mem_capa == 0) {                                          // Validate memory capacity.
    LOG_ERR(("Invalid memory capacity (mem_capa ", (u)mem_capa, ", power ", (u)read_id[2], ")."));
    RTOS_ERR_SET(*p_err, RTOS_ERR_IO);
    return (DEF_NULL);
  }
  //                                                               Set flash size in QSPI controller.
  if (p_drv_api->FlashSizeSet != DEF_NULL) {
    p_drv_api->FlashSizeSet(p_drv_data,
                            read_id[2],
                            FS_NOR_PHY_W25Q_SlaveInfo.ChipSelID,
                            p_err);
    if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
      return (DEF_NULL);
    }
  }

#if LOG_VRB_IS_EN()
  //                                                               ---------- PRINT SOME ADDITIONAL DEV INFO ----------
  {
    CPU_INT16U status_reg;

    //                                                             Rd Status reg and extract a few info.
    p_drv_api->CmdSend(p_drv_data,
                       &FS_NOR_W25Q_CMD_RDSR2,
                       FS_NOR_PHY_CMD_ADDR_PHASE_NONE_PTR,
                       FS_NOR_PHY_CMD_DUMMY_CYCLES_NONE_PTR,
                       FS_NOR_PHY_CMD_DUMMY_CYCLES_NONE,
                       &status_reg,
                       1u,
                       p_err);
    if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
      LOG_ERR(("Couldn't send RDSR (rd Status reg) cmd to NOR device w/ err = ", RTOS_ERR_LOG_ARG_GET(*p_err), "."));
      return (DEF_NULL);
    }

    status_reg = (status_reg << 8u);

    p_drv_api->CmdSend(p_drv_data,
                       &FS_NOR_W25Q_CMD_RDSR,
                       FS_NOR_PHY_CMD_ADDR_PHASE_NONE_PTR,
                       FS_NOR_PHY_CMD_DUMMY_CYCLES_NONE_PTR,
                       FS_NOR_PHY_CMD_DUMMY_CYCLES_NONE,
                       &status_reg,
                       1u,
                       p_err);
    if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
      LOG_ERR(("Couldn't send RDSR (rd Status reg) cmd to NOR device w/ err = ", RTOS_ERR_LOG_ARG_GET(*p_err), "."));
      return (DEF_NULL);
    }

    LOG_VRB(("Read Status Register:"));
    LOG_VRB(("- Status register write protect (SRP0, SRP1): ",
             (s)(DEF_BIT_IS_SET(status_reg, FS_NOR_W25Q_STATUS_REG1_SRP0) ? "1" : "0"),
             (s)(DEF_BIT_IS_SET(status_reg, FS_NOR_W25Q_STATUS_REG2_SRP1) ? "1" : "0")));
    LOG_VRB(("- Quad I/O protocol:             ", (s)(DEF_BIT_IS_SET(status_reg, FS_NOR_W25Q_STATUS_REG2_QE) ? "enabled" : "disabled")));
    LOG_VRB(("- Level of protected block (BP2 BP1 BP0): ",
             (s)(DEF_BIT_IS_SET(status_reg, FS_NOR_W25Q_STATUS_REG1_BP2) ? "1" : "0"),
             (s)(DEF_BIT_IS_SET(status_reg, FS_NOR_W25Q_STATUS_REG1_BP1) ? "1" : "0"),
             (s)(DEF_BIT_IS_SET(status_reg, FS_NOR_W25Q_STATUS_REG1_BP0) ? "1" : "0")));
  }
#endif

  p_drv_api->Stop(p_drv_data, p_err);                           // Stop QSPI operations.
  if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
    return (DEF_NULL);
  }

  //                                                               Get user PHY cfg.
  p_phy_w25q->QuadIOModeEn = (p_quad_spi_nor_pm_item->HwInfoPtr->CtrlrHwInfoPtr->BusWidth == FS_NOR_SERIAL_BUS_WIDTH_QUAD_IO);

  //                                                               Set dummy cycles needed by RD cmd to dflt val.
  p_phy_w25q->DummyCycleCnt = FS_NOR_W25Q_DFLT_DUMMY_CYCLES;
  p_phy_w25q->QuadSpiDrvApiPtr = p_drv_api;
  p_phy_w25q->QuadSpiDrvDataPtr = p_drv_data;

  //                                                               Save NOR params.
  p_phy_w25q->Phy.BlkCnt = FS_UTIL_DIV_PWR2(mem_capa, FS_NOR_W25Q_BLK_SIZE_LOG_2_SEL);
  p_phy_w25q->Phy.BlkSizeLog2 = FS_NOR_W25Q_BLK_SIZE_LOG_2_SEL;
  p_phy_w25q->Phy.PhyApiPtr = &FS_NOR_PHY_W25Q_API;
  p_phy_w25q->Phy.FourByteAddrSupport = DEF_NO;                 // W25Q below 128Mb: no need of 4B addressing.
  p_phy_w25q->Phy.FourByteAddrSet = DEF_NO;
  p_phy_w25q->Phy.FlashXipSupport = DEF_NO;                     // W25Q does not support XIP.

  return (&p_phy_w25q->Phy);
}

/****************************************************************************************************//**
 *                                          FS_NOR_W25Q_Open()
 *
 * @brief   Open (initialize) a NOR device instance & get NOR device information.
 *
 * @param   p_phy    Pointer to NOR PHY instance.
 *
 * @param   p_err    Error pointer.
 *******************************************************************************************************/
static void FS_NOR_W25Q_Open(FS_NOR_PHY *p_phy,
                             RTOS_ERR   *p_err)
{
  FS_NOR_PHY_W25Q *p_phy_w25q = (FS_NOR_PHY_W25Q *)p_phy;
  CPU_INT16U      status_reg;

  //                                                               Start QSPI operations.
  p_phy_w25q->QuadSpiDrvApiPtr->Start(p_phy_w25q->QuadSpiDrvDataPtr, p_err);
  if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
    return;
  }

  //                                                               ------------------ CFG STATUS REG ------------------
  //                                                               Get current Status reg content.
  p_phy_w25q->QuadSpiDrvApiPtr->CmdSend(p_phy_w25q->QuadSpiDrvDataPtr,
                                        &FS_NOR_W25Q_CMD_RDSR2,
                                        FS_NOR_PHY_CMD_ADDR_PHASE_NONE_PTR,
                                        FS_NOR_PHY_CMD_DUMMY_CYCLES_NONE_PTR,
                                        FS_NOR_PHY_CMD_DUMMY_CYCLES_NONE,
                                        &status_reg,
                                        1u,
                                        p_err);
  if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
    LOG_ERR(("Couldn't send RDSR (rd Status reg) cmd to NOR device w/ err = ", RTOS_ERR_LOG_ARG_GET(*p_err), "."));
    return;
  }

  status_reg = (status_reg << 8u);

  p_phy_w25q->QuadSpiDrvApiPtr->CmdSend(p_phy_w25q->QuadSpiDrvDataPtr,
                                        &FS_NOR_W25Q_CMD_RDSR,
                                        FS_NOR_PHY_CMD_ADDR_PHASE_NONE_PTR,
                                        FS_NOR_PHY_CMD_DUMMY_CYCLES_NONE_PTR,
                                        FS_NOR_PHY_CMD_DUMMY_CYCLES_NONE,
                                        &status_reg,
                                        1u,
                                        p_err);
  if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
    LOG_ERR(("Couldn't send RDSR (rd Status reg) cmd to NOR device w/ err = ", RTOS_ERR_LOG_ARG_GET(*p_err), "."));
    return;
  }

  //                                                               Set Wr En Latch bit to permit writing Status reg.
  FS_NOR_W25Q_WrEn(p_phy_w25q,
                   p_err);
  if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
    return;
  }
  //                                                               Update cfg.
  //                                                               Allow wr to Status reg regardless hw protection mode.
  DEF_BIT_CLR(status_reg, (CPU_INT16U)(FS_NOR_W25Q_STATUS_REG1_SRP0
                                       | FS_NOR_W25Q_STATUS_REG2_SRP1));
  //                                                               All memory area is accessible to PGM & ERASE cmds.
  DEF_BIT_FIELD_WR(status_reg, 0u, (CPU_INT08U)FS_NOR_W25Q_STATUS_REG_BP_MASK);
  //                                                               Set W25Q Quad I/O mode according to PHY cfg.
  if (p_phy_w25q->QuadIOModeEn) {
    DEF_BIT_SET(status_reg, FS_NOR_W25Q_STATUS_REG2_QE);
  } else {
    DEF_BIT_CLR(status_reg, (CPU_INT16U)FS_NOR_W25Q_STATUS_REG2_QE);
  }
  //                                                               Wr Status reg with updated cfg.
  p_phy_w25q->QuadSpiDrvApiPtr->CmdSend(p_phy_w25q->QuadSpiDrvDataPtr,
                                        &FS_NOR_W25Q_CMD_WRSR,
                                        FS_NOR_PHY_CMD_ADDR_PHASE_NONE_PTR,
                                        FS_NOR_PHY_CMD_DUMMY_CYCLES_NONE_PTR,
                                        FS_NOR_PHY_CMD_DUMMY_CYCLES_NONE,
                                        &status_reg,
                                        2u,
                                        p_err);
  if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
    LOG_ERR(("Couldn't send WRSR (Write status register) cmd to NOR device w/ err = ", RTOS_ERR_LOG_ARG_GET(*p_err), "."));
    return;
  }
  //                                                               Wait for Status reg wr to complete.
  FS_NOR_W25Q_WrCmplWait(p_phy_w25q,
                         FS_NOR_W25Q_CMD_WRSR_DUR_TYPICAL_uS,
                         FS_NOR_W25Q_CMD_WRSR_DUR_MAX_uS,
                         p_err);
  if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
    LOG_ERR(("Couldn't write status register w/ err = ", RTOS_ERR_LOG_ARG_GET(*p_err), "."));
    return;
  }

  //                                                               Chk Status reg to confirm protections are disabled.
  p_phy_w25q->QuadSpiDrvApiPtr->CmdSend(p_phy_w25q->QuadSpiDrvDataPtr,
                                        &FS_NOR_W25Q_CMD_RDSR2,
                                        FS_NOR_PHY_CMD_ADDR_PHASE_NONE_PTR,
                                        FS_NOR_PHY_CMD_DUMMY_CYCLES_NONE_PTR,
                                        FS_NOR_PHY_CMD_DUMMY_CYCLES_NONE,
                                        &status_reg,
                                        1u,
                                        p_err);
  if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
    LOG_ERR(("Couldn't send RDSR (rd Status reg) cmd to NOR device w/ err = ", RTOS_ERR_LOG_ARG_GET(*p_err), "."));
    return;
  }

  status_reg = (status_reg << 8u);

  p_phy_w25q->QuadSpiDrvApiPtr->CmdSend(p_phy_w25q->QuadSpiDrvDataPtr,
                                        &FS_NOR_W25Q_CMD_RDSR,
                                        FS_NOR_PHY_CMD_ADDR_PHASE_NONE_PTR,
                                        FS_NOR_PHY_CMD_DUMMY_CYCLES_NONE_PTR,
                                        FS_NOR_PHY_CMD_DUMMY_CYCLES_NONE,
                                        &status_reg,
                                        1u,
                                        p_err);
  if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
    LOG_ERR(("Couldn't send RDSR (rd Status reg) cmd to NOR device w/ err = ", RTOS_ERR_LOG_ARG_GET(*p_err), "."));
    return;
  }
  if (DEF_BIT_IS_SET(status_reg, FS_NOR_W25Q_STATUS_REG1_SRP0 | FS_NOR_W25Q_STATUS_REG2_SRP1)
      || DEF_BIT_IS_SET_ANY(status_reg, FS_NOR_W25Q_STATUS_REG_BP_MASK)) {
    RTOS_ERR_SET(*p_err, RTOS_ERR_IO);
    LOG_ERR(("Couldn't disable write protections w/ err = ", RTOS_ERR_LOG_ARG_GET(*p_err), "."));
  }
}

/****************************************************************************************************//**
 *                                          FS_NOR_W25Q_Close()
 *
 * @brief   Close (uninitialize) a NOR device instance.
 *
 * @param   p_phy    Pointer to a NOR PHY instance.
 *
 * @param   p_err    Error pointer.
 *
 * @note    (1) When WEL bit sets to 0, which means no internal write enable latch; the flash device
 *              will not accept program/erase/write status register instructions.
 *******************************************************************************************************/
static void FS_NOR_W25Q_Close(FS_NOR_PHY *p_phy,
                              RTOS_ERR   *p_err)
{
  FS_NOR_PHY_W25Q *p_phy_w25q = (FS_NOR_PHY_W25Q *)p_phy;
  CPU_INT08U      status_reg;

  //                                                               Reset Write Enable Latch (WEL) bit (see Note #1).
  p_phy_w25q->QuadSpiDrvApiPtr->CmdSend(p_phy_w25q->QuadSpiDrvDataPtr,
                                        &FS_NOR_W25Q_CMD_WRDI,
                                        FS_NOR_PHY_CMD_ADDR_PHASE_NONE_PTR,
                                        FS_NOR_PHY_CMD_DUMMY_CYCLES_NONE_PTR,
                                        FS_NOR_PHY_CMD_DUMMY_CYCLES_NONE,
                                        FS_NOR_PHY_CMD_DATA_PHASE_NONE_PTR,
                                        FS_NOR_PHY_CMD_DATA_PHASE_NONE,
                                        p_err);
  if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
    LOG_ERR(("Couldn't send WRDI (Write disable) cmd to NOR device w/ err = ", RTOS_ERR_LOG_ARG_GET(*p_err), "."));
    return;
  }

  //                                                               Chk Status reg to confirm WEL disabled.
  p_phy_w25q->QuadSpiDrvApiPtr->CmdSend(p_phy_w25q->QuadSpiDrvDataPtr,
                                        &FS_NOR_W25Q_CMD_RDSR,
                                        FS_NOR_PHY_CMD_ADDR_PHASE_NONE_PTR,
                                        FS_NOR_PHY_CMD_DUMMY_CYCLES_NONE_PTR,
                                        FS_NOR_PHY_CMD_DUMMY_CYCLES_NONE,
                                        &status_reg,
                                        1u,
                                        p_err);
  if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
    LOG_ERR(("Couldn't send RDSR (rd Status reg) cmd to NOR device w/ err = ", RTOS_ERR_LOG_ARG_GET(*p_err), "."));
    return;
  }
  if (DEF_BIT_IS_SET(status_reg, FS_NOR_W25Q_STATUS_REG1_WEL)) {
    RTOS_ERR_SET(*p_err, RTOS_ERR_IO);
    LOG_ERR(("Couldn't disable Write Enable Latch w/ err = ", RTOS_ERR_LOG_ARG_GET(*p_err), "."));
    return;
  }

  //                                                               Stop QSPI operations.
  p_phy_w25q->QuadSpiDrvApiPtr->Stop(p_phy_w25q->QuadSpiDrvDataPtr, p_err);
}

/****************************************************************************************************//**
 *                                           FS_NOR_W25Q_Rd()
 *
 * @brief   Read from a NOR device & store data in buffer.
 *
 * @param   p_phy       Pointer to a NOR PHY instance.
 *
 * @param   p_dest      Pointer to destination buffer.
 *
 * @param   start_addr  Start address of read (relative to start of device).
 *
 * @param   cnt         Number of octets to read.
 *
 * @param   p_err       Error pointer.
 *******************************************************************************************************/
static void FS_NOR_W25Q_Rd(FS_NOR_PHY *p_phy,
                           void       *p_dest,
                           CPU_INT32U start_addr,
                           CPU_INT32U cnt,
                           RTOS_ERR   *p_err)
{
  FS_NOR_PHY_W25Q                *p_phy_w25q = (FS_NOR_PHY_W25Q *)p_phy;
  const FS_NOR_QUAD_SPI_CMD_DESC *p_rd_cmd;
  CPU_INT32U                     dummy_data = FS_NOR_W25Q_DUMMY_BYTE;
  CPU_INT08U                     addr_tbl[FS_NOR_PHY_SIZE_128Mb_AND_LESS_3B_ADDR];

  //                                                               Check start addr.
  if (start_addr > (FS_NOR_PHY_SIZE_128Mb - 1u)) {
    LOG_ERR(("W25Q NOR flash series has a maximum density of 64Mb. Read start address cannot be greater than 128Mb: ", (X)start_addr, "."));
    RTOS_ERR_SET(*p_err, RTOS_ERR_INVALID_ARG);
    return;
  }
  //                                                               Get rd cmd associated with cfg'd IO mode.
  if (p_phy_w25q->QuadIOModeEn == DEF_ENABLED) {
    p_rd_cmd = &FS_NOR_W25Q_CMD_QREAD;                          // Rd cmd in Quad IO mode.
  } else {
    p_rd_cmd = &FS_NOR_W25Q_CMD_FREAD;                          // Rd cmd in Single IO mode.
  }
  //                                                               Cfg 3-byte address.
  MEM_VAL_COPY_GET_INTU(&addr_tbl[0u], &start_addr, FS_NOR_PHY_SIZE_128Mb_AND_LESS_3B_ADDR);

  //                                                               Rd flash dev.
  p_phy_w25q->QuadSpiDrvApiPtr->CmdSend(p_phy_w25q->QuadSpiDrvDataPtr,
                                        p_rd_cmd,
                                        addr_tbl,
                                        (CPU_INT08U *)&dummy_data,
                                        p_phy_w25q->DummyCycleCnt,
                                        p_dest,
                                        cnt,
                                        p_err);
  if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
    LOG_ERR(("Couldn't send FREAD (Fast read) cmd to NOR device w/ err = ", RTOS_ERR_LOG_ARG_GET(*p_err), "."));
    return;
  }
}

/****************************************************************************************************//**
 *                                           FS_NOR_W25Q_Wr()
 *
 * @brief   Write data to a NOR device from a buffer.
 *
 * @param   p_phy       Pointer to a NOR PHY instance.
 *
 * @param   p_src       Pointer to source buffer.
 *
 * @param   start_addr  Start address of write (relative to start of device).
 *
 * @param   cnt         Number of octets to write.
 *
 * @param   p_err       Error pointer.
 *
 * @note    (1) To align the bulk of the write on page boundaries, the first page program is limited
 *              to the remaining size of the page. This way, subsequent page programs are page-aligned.
 *******************************************************************************************************/
static void FS_NOR_W25Q_Wr(FS_NOR_PHY *p_phy,
                           void       *p_src,
                           CPU_INT32U start_addr,
                           CPU_INT32U cnt,
                           RTOS_ERR   *p_err)
{
  FS_NOR_PHY_W25Q                *p_phy_w25q = (FS_NOR_PHY_W25Q *)p_phy;
  CPU_INT08U                     *p_src_08 = (CPU_INT08U *)p_src;
  const FS_NOR_QUAD_SPI_CMD_DESC *p_wr_cmd;
  CPU_INT32U                     addr = start_addr;
  CPU_INT32U                     bytes_rem = cnt;
  CPU_INT16U                     pg_cnt;
  CPU_INT16U                     first_pg_cnt;
  CPU_INT08U                     addr_tbl[FS_NOR_PHY_SIZE_128Mb_AND_LESS_3B_ADDR];

  //                                                               Check start addr.
  if (start_addr > (FS_NOR_PHY_SIZE_128Mb - 1u)) {
    LOG_ERR(("W25Q NOR flash series has a maximum density of 64Mb. Write start address cannot be greater than 128Mb: ", (X)start_addr, "."));
    RTOS_ERR_SET(*p_err, RTOS_ERR_INVALID_ARG);
    return;
  }
  //                                                               Get wr cmd associated with cfg'd IO mode.
  if (p_phy_w25q->QuadIOModeEn == DEF_ENABLED) {
    p_wr_cmd = &FS_NOR_W25Q_CMD_4PP;                            // Wr cmd in Quad IO mode.
  } else {
    p_wr_cmd = &FS_NOR_W25Q_CMD_PP;                             // Wr cmd in Single IO mode.
  }
  //                                                               See Note #1.
  first_pg_cnt = FS_NOR_W25Q_PAGE_SIZE - (start_addr & 0xFFu);
  pg_cnt = DEF_MIN(bytes_rem, first_pg_cnt);

  while (bytes_rem > 0) {                                       // Wr xfer split into several page program cmds.
                                                                // Set Wr En Latch bit to permit page program.
    FS_NOR_W25Q_WrEn(p_phy_w25q,
                     p_err);
    if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
      return;
    }
    //                                                             Cfg 3-byte address.
    MEM_VAL_COPY_GET_INTU(&addr_tbl[0u], &addr, FS_NOR_PHY_SIZE_128Mb_AND_LESS_3B_ADDR);

    //                                                             Perform a page program.
    p_phy_w25q->QuadSpiDrvApiPtr->CmdSend(p_phy_w25q->QuadSpiDrvDataPtr,
                                          p_wr_cmd,
                                          addr_tbl,
                                          FS_NOR_PHY_CMD_DUMMY_CYCLES_NONE_PTR,
                                          FS_NOR_PHY_CMD_DUMMY_CYCLES_NONE,
                                          p_src_08,
                                          pg_cnt,
                                          p_err);
    if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
      LOG_ERR(("Couldn't send PP (Page Program) cmd to NOR device w/ err = ", RTOS_ERR_LOG_ARG_GET(*p_err), "."));
      return;
    }
    //                                                             Wait for page program to complete.
    FS_NOR_W25Q_WrCmplWait(p_phy_w25q,
                           FS_NOR_W25Q_CMD_PP_TYPICAL_DUR_uS,
                           FS_NOR_W25Q_CMD_PP_MAX_DUR_uS,
                           p_err);
    if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
      LOG_ERR(("Program operation timed out or failed w/ err = ", RTOS_ERR_LOG_ARG_GET(*p_err), "."));
      return;
    }

    p_src_08 += pg_cnt;
    addr += pg_cnt;
    bytes_rem -= pg_cnt;
    //                                                             Wr full pages until, if needed, partial last page.
    pg_cnt = DEF_MIN(FS_NOR_W25Q_PAGE_SIZE, bytes_rem);
  }
}

/****************************************************************************************************//**
 *                                        FS_NOR_W25Q_BlkErase()
 *
 * @brief   Erase block of NOR device.
 *
 * @param   p_phy       Pointer to NOR PHY instance.
 *
 * @param   start_addr  Start address of block (relative to start of device).
 *
 * @param   size        Size of block, in octets.
 *
 * @param   p_err       Error pointer.
 *******************************************************************************************************/
static void FS_NOR_W25Q_BlkErase(FS_NOR_PHY *p_phy,
                                 CPU_INT32U start_addr,
                                 CPU_INT32U size,
                                 RTOS_ERR   *p_err)
{
  FS_NOR_PHY_W25Q                *p_phy_w25q = (FS_NOR_PHY_W25Q *)p_phy;
  const FS_NOR_QUAD_SPI_CMD_DESC *p_erase_cmd;
  CPU_INT08U                     addr_tbl[FS_NOR_PHY_SIZE_128Mb_AND_LESS_3B_ADDR];

  //                                                               Check start addr.
  if (start_addr > (FS_NOR_PHY_SIZE_128Mb - 1u)) {
    LOG_ERR(("W25Q NOR flash series has a maximum density of 64Mb. Read start address cannot be greater than 128Mb: ", (X)start_addr, "."));
    RTOS_ERR_SET(*p_err, RTOS_ERR_INVALID_ARG);
    return;
  }

  //                                                               Cfg right blk cmd.
  switch (size) {
    case FS_NOR_PHY_BLK_SIZE_32K:
      p_erase_cmd = &FS_NOR_W25Q_CMD_BE32;
      break;

    case FS_NOR_PHY_BLK_SIZE_64K:
      p_erase_cmd = &FS_NOR_W25Q_CMD_BE64;
      break;

    default:
      LOG_ERR(("Wrong block size (", (u)size, " bytes) for erase operation."));
      RTOS_DBG_FAIL_EXEC_ERR(*p_err, RTOS_ERR_INVALID_CFG,; );
  }
  //                                                               Set Wr En Latch bit to permit erasing blk.
  FS_NOR_W25Q_WrEn(p_phy_w25q,
                   p_err);
  if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
    return;
  }
  //                                                               Cfg 3-byte address.
  MEM_VAL_COPY_GET_INTU(&addr_tbl[0u], &start_addr, FS_NOR_PHY_SIZE_128Mb_AND_LESS_3B_ADDR);

  //                                                               Erase flash dev blk.
  p_phy_w25q->QuadSpiDrvApiPtr->CmdSend(p_phy_w25q->QuadSpiDrvDataPtr,
                                        p_erase_cmd,
                                        addr_tbl,
                                        FS_NOR_PHY_CMD_DUMMY_CYCLES_NONE_PTR,
                                        FS_NOR_PHY_CMD_DUMMY_CYCLES_NONE,
                                        FS_NOR_PHY_CMD_DATA_PHASE_NONE_PTR,
                                        FS_NOR_PHY_CMD_DATA_PHASE_NONE,
                                        p_err);
  if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
    LOG_ERR(("Couldn't send BE (block erase) cmd to NOR device w/ err = ", RTOS_ERR_LOG_ARG_GET(*p_err), "."));
    return;
  }
  //                                                               Wait for erase operation to complete.
  FS_NOR_W25Q_WrCmplWait(p_phy_w25q,
                         FS_NOR_W25Q_CMD_BLK_ERASE_TYPICAL_DUR_uS,
                         FS_NOR_W25Q_CMD_BLK_ERASE_MAX_DUR_uS,
                         p_err);
  if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
    LOG_ERR(("Program operation timed out or failed w/ err = ", RTOS_ERR_LOG_ARG_GET(*p_err), "."));
    return;
  }
}

/****************************************************************************************************//**
 *                                       FS_NOR_W25Q_ChipErase()
 *
 * @brief   Erase NOR device.
 *
 * @param   p_phy     Pointer to NOR PHY instance.
 *
 * @param   p_err     Error pointer.
 *******************************************************************************************************/
static void FS_NOR_W25Q_ChipErase(FS_NOR_PHY *p_phy,
                                  RTOS_ERR   *p_err)
{
  FS_NOR_PHY_W25Q *p_phy_w25q = (FS_NOR_PHY_W25Q *)p_phy;

  //                                                               Set Wr En Latch bit to permit erasing flash dev.
  FS_NOR_W25Q_WrEn(p_phy_w25q,
                   p_err);
  if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
    return;
  }

  //                                                               Send erase cmd.
  p_phy_w25q->QuadSpiDrvApiPtr->CmdSend(p_phy_w25q->QuadSpiDrvDataPtr,
                                        &FS_NOR_W25Q_CMD_CE,
                                        FS_NOR_PHY_CMD_ADDR_PHASE_NONE_PTR,
                                        FS_NOR_PHY_CMD_DUMMY_CYCLES_NONE_PTR,
                                        FS_NOR_PHY_CMD_DUMMY_CYCLES_NONE,
                                        FS_NOR_PHY_CMD_DATA_PHASE_NONE_PTR,
                                        FS_NOR_PHY_CMD_DATA_PHASE_NONE,
                                        p_err);
  if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
    LOG_ERR(("Couldn't send CE (chip erase) cmd to NOR device w/ err = ", RTOS_ERR_LOG_ARG_GET(*p_err), "."));
    return;
  }
  //                                                               Wait for erase operation to complete.
  FS_NOR_W25Q_WrCmplWait(p_phy_w25q,
                         FS_NOR_W25Q_CMD_CHIP_ERASE_TYPICAL_DUR_uS,
                         FS_NOR_W25Q_CMD_CHIP_ERASE_MAX_DUR_uS,
                         p_err);
  if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
    LOG_ERR(("Chip erase operation timed out or failed w/ err = ", RTOS_ERR_LOG_ARG_GET(*p_err), "."));
    return;
  }
}

/****************************************************************************************************//**
 *                                         FS_NOR_W25Q_XipCfg()
 *
 * @brief   Configure XIP (eXecute-In-Place) mode in Quad SPI controller (see Note #1).
 *
 * @param   p_phy   Pointer to a NOR PHY instance.
 *
 * @param   xip_en  XIP enable flag.
 *
 *                  DEF_ENABLE   Enable  XIP in NOR flash device and Quad SPI controller.
 *                  DEF_DISABLE  Disable XIP in NOR flash device and Quad SPI controller.
 *
 * @param   p_err   Error pointer.
 *******************************************************************************************************/
static void FS_NOR_W25Q_XipCfg(FS_NOR_PHY  *p_phy,
                               CPU_BOOLEAN xip_en,
                               RTOS_ERR    *p_err)
{
  FS_NOR_PHY_W25Q *p_phy_w25q = (FS_NOR_PHY_W25Q *)p_phy;

  if (p_phy_w25q->QuadSpiDrvApiPtr->XipCfg != DEF_NULL) {
    p_phy_w25q->QuadSpiDrvApiPtr->XipCfg(p_phy_w25q->QuadSpiDrvDataPtr,
                                         xip_en,
                                         p_phy->FlashXipSupport,
                                         p_err);
  }
}

/****************************************************************************************************//**
 *                                       FS_NOR_W25Q_AlignReqGet()
 *
 * @brief   Get buffer alignment requirement from Quad SPI controller.
 *
 * @param   p_phy   Pointer to a NOR PHY instance.
 *
 * @param   p_err   Error pointer.
 *
 * @return  Alignment requirement.
 *******************************************************************************************************/
static CPU_SIZE_T FS_NOR_W25Q_AlignReqGet(FS_NOR_PHY *p_phy,
                                          RTOS_ERR   *p_err)
{
  FS_NOR_PHY_W25Q *p_phy_w25q = (FS_NOR_PHY_W25Q *)p_phy;
  CPU_SIZE_T      align_req;

  align_req = p_phy_w25q->QuadSpiDrvApiPtr->AlignReqGet(p_phy_w25q->QuadSpiDrvDataPtr, p_err);

  return (align_req);
}

/********************************************************************************************************
 ********************************************************************************************************
 *                                           INTERNAL FUNCTIONS
 ********************************************************************************************************
 *******************************************************************************************************/

/****************************************************************************************************//**
 *                                          FS_NOR_W25Q_WrEn()
 *
 * @brief   Set Write Enable Latch on flash device to perform commands changing the device content
 *               (see Note #1).
 *
 * @param   p_phy_w25q      Pointer to W25Q PHY data.
 *
 * @param   p_err           Error pointer.
 *
 * @note    (1) Write Enable Latch must be set every time prior to executing those W25Q flash
 *              commands: PP, 4PP, SE, BE32K, BE, CE, and WRSR.
 *******************************************************************************************************/
static void FS_NOR_W25Q_WrEn(FS_NOR_PHY_W25Q *p_phy_w25q,
                             RTOS_ERR        *p_err)
{
  CPU_INT08U status_reg;
  CPU_INT32U retry;

  //                                                               Send WR EN cmd to flash dev.
  p_phy_w25q->QuadSpiDrvApiPtr->CmdSend(p_phy_w25q->QuadSpiDrvDataPtr,
                                        &FS_NOR_W25Q_CMD_WREN,
                                        FS_NOR_PHY_CMD_ADDR_PHASE_NONE_PTR,
                                        FS_NOR_PHY_CMD_DUMMY_CYCLES_NONE_PTR,
                                        FS_NOR_PHY_CMD_DUMMY_CYCLES_NONE,
                                        FS_NOR_PHY_CMD_DATA_PHASE_NONE_PTR,
                                        FS_NOR_PHY_CMD_DATA_PHASE_NONE,
                                        p_err);
  if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
    LOG_ERR(("Couldn't send WREN (Write enable) cmd to NOR device w/ err = ", RTOS_ERR_LOG_ARG_GET(*p_err), "."));
    return;
  }
  //                                                               Test WEL bit to ensure write enable latch en in dev.
  p_phy_w25q->QuadSpiDrvApiPtr->CmdSend(p_phy_w25q->QuadSpiDrvDataPtr,
                                        &FS_NOR_W25Q_CMD_RDSR,
                                        FS_NOR_PHY_CMD_ADDR_PHASE_NONE_PTR,
                                        FS_NOR_PHY_CMD_DUMMY_CYCLES_NONE_PTR,
                                        FS_NOR_PHY_CMD_DUMMY_CYCLES_NONE,
                                        &status_reg,
                                        1u,
                                        p_err);
  if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
    LOG_ERR(("Couldn't send RDSR (rd Status reg) cmd to NOR device w/ err = ", RTOS_ERR_LOG_ARG_GET(*p_err), "."));
    return;
  }

  retry = 0u;
  while (DEF_BIT_IS_CLR(status_reg, FS_NOR_W25Q_STATUS_REG1_WEL)
         && (retry < FS_NOR_W25Q_REG_RD_RETRY_MAX)) {
    p_phy_w25q->QuadSpiDrvApiPtr->CmdSend(p_phy_w25q->QuadSpiDrvDataPtr,
                                          &FS_NOR_W25Q_CMD_RDSR,
                                          FS_NOR_PHY_CMD_ADDR_PHASE_NONE_PTR,
                                          FS_NOR_PHY_CMD_DUMMY_CYCLES_NONE_PTR,
                                          FS_NOR_PHY_CMD_DUMMY_CYCLES_NONE,
                                          &status_reg,
                                          1u,
                                          p_err);
    if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
      LOG_ERR(("Couldn't send RDSR (rd Status reg) cmd to NOR device w/ err = ", RTOS_ERR_LOG_ARG_GET(*p_err), "."));
      return;
    }

    retry++;
  }
  if (retry == FS_NOR_W25Q_REG_RD_RETRY_MAX) {
    RTOS_ERR_SET(*p_err, RTOS_ERR_IO);
  }
}

/****************************************************************************************************//**
 *                                      FS_NOR_W25Q_WrCmplWait()
 *
 * @brief   Wait for write operation (program, erase or write status register) to complete.
 *
 * @param   p_phy_w25q      Pointer to W25Q PHY data.
 *
 * @param   typical_dur     Typical duration of operation in us.
 *
 * @param   max_dur         Maximum duration of operation in us.
 *
 * @param   p_err           Error pointer.
 *******************************************************************************************************/
static void FS_NOR_W25Q_WrCmplWait(FS_NOR_PHY_W25Q *p_phy_w25q,
                                   CPU_INT32U      dur_typical,
                                   CPU_INT32U      dur_max,
                                   RTOS_ERR        *p_err)
{
  CPU_INT32U retry = 0u;
  CPU_INT32U timeout_cnt;
  CPU_INT32U dly;
  CPU_INT08U status_reg;

  dly = dur_typical / 1000u;                                // Wait for typical_dur before polling.
  if (dly != 0u) {
    KAL_Dly(dly);
  }

  timeout_cnt = (dur_max - dly * 1000u) * 20u;              // Compute a timeout taken into account typical...
                                                            //     ...duration already elapsed.

  //                                                               Test WIP bit to ensure write completed.
  p_phy_w25q->QuadSpiDrvApiPtr->CmdSend(p_phy_w25q->QuadSpiDrvDataPtr,
                                        &FS_NOR_W25Q_CMD_RDSR,
                                        FS_NOR_PHY_CMD_ADDR_PHASE_NONE_PTR,
                                        FS_NOR_PHY_CMD_DUMMY_CYCLES_NONE_PTR,
                                        FS_NOR_PHY_CMD_DUMMY_CYCLES_NONE,
                                        &status_reg,
                                        1u,
                                        p_err);
  if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
    LOG_ERR(("Couldn't send RDSR (rd Status reg) cmd to NOR device w/ err = ", RTOS_ERR_LOG_ARG_GET(*p_err), "."));
    return;
  }

  while (DEF_BIT_IS_SET(status_reg, FS_NOR_W25Q_STATUS_REG1_BUSY)
         && (retry < timeout_cnt)) {
    p_phy_w25q->QuadSpiDrvApiPtr->CmdSend(p_phy_w25q->QuadSpiDrvDataPtr,
                                          &FS_NOR_W25Q_CMD_RDSR,
                                          FS_NOR_PHY_CMD_ADDR_PHASE_NONE_PTR,
                                          FS_NOR_PHY_CMD_DUMMY_CYCLES_NONE_PTR,
                                          FS_NOR_PHY_CMD_DUMMY_CYCLES_NONE,
                                          &status_reg,
                                          1u,
                                          p_err);
    if (RTOS_ERR_CODE_GET(*p_err) != RTOS_ERR_NONE) {
      LOG_ERR(("Couldn't send RDSR (rd Status reg) cmd to NOR device w/ err = ", RTOS_ERR_LOG_ARG_GET(*p_err), "."));
      return;
    }

    retry++;
  }
  if (retry == timeout_cnt) {
    RTOS_ERR_SET(*p_err, RTOS_ERR_IO);
  }
}

/********************************************************************************************************
 ********************************************************************************************************
 *                                   DEPENDENCIES & AVAIL CHECK(S) END
 ********************************************************************************************************
 *******************************************************************************************************/

#endif // RTOS_MODULE_FS_STORAGE_NOR_AVAIL
