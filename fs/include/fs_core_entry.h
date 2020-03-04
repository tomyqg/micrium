/***************************************************************************//**
 * @file
 * @brief File System - Core Entry Operations
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
 * @addtogroup FS_CORE
 * @{
 *******************************************************************************************************/

/********************************************************************************************************
 ********************************************************************************************************
 *                                               MODULE
 ********************************************************************************************************
 *******************************************************************************************************/

#ifndef  FS_CORE_ENTRY_H_
#define  FS_CORE_ENTRY_H_

/********************************************************************************************************
 ********************************************************************************************************
 *                                           INCLUDE FILES
 ********************************************************************************************************
 *******************************************************************************************************/

//                                                                 ------------------------ FS ------------------------
#include  <fs/include/fs_core_working_dir.h>
#include  <fs/include/fs_core.h>
#include  <fs/include/fs_core_vol.h>
//                                                                 ----------------------- EXT ------------------------
#include  <common/include/clk.h>
#include  <common/include/rtos_err.h>
#include  <cpu/include/cpu.h>

#include  "sl_sleeptimer.h"

/*********************************************************************************************************
 *********************************************************************************************************
 *                                               DEFINES
 ********************************************************************************************************
 *******************************************************************************************************/

//                                                                 ------------------- ENTRY TYPES --------------------
#define  FS_ENTRY_TYPE_FILE                         DEF_BIT_00
#define  FS_ENTRY_TYPE_DIR                          DEF_BIT_01
#define  FS_ENTRY_TYPE_WRK_DIR                      DEF_BIT_02
#define  FS_ENTRY_TYPE_ANY                         (FS_ENTRY_TYPE_FILE | FS_ENTRY_TYPE_DIR)

//                                                                 ------------- ENTRY ATTRIBUTE DEFINES --------------
#define  FS_ENTRY_ATTRIB_NONE                       DEF_BIT_NONE
#define  FS_ENTRY_ATTRIB_RD                         DEF_BIT_00
#define  FS_ENTRY_ATTRIB_WR                         DEF_BIT_01
#define  FS_ENTRY_ATTRIB_HIDDEN                     DEF_BIT_02

//                                                                 ----------------- DATE TIME TYPES ------------------
#define  FS_DATE_TIME_NONE                          DEF_BIT_NONE
#define  FS_DATE_TIME_CREATE                        DEF_BIT_00
#define  FS_DATE_TIME_MODIFY                        DEF_BIT_01
#define  FS_DATE_TIME_ACCESS                        DEF_BIT_02
#define  FS_DATE_TIME_ALL                           DEF_BIT_03

/********************************************************************************************************
 *                                           ENTRY INFO DATA TYPE
 *******************************************************************************************************/

typedef struct fs_entry_attrib {
  CPU_INT32U Wr : 1;                                            ///< Bit indicating if entry has write access.
  CPU_INT32U Rd : 1;                                            ///< Bit indicating if entry has read access.
  CPU_INT32U Hidden : 1;                                        ///< Bit indicating if entry is hidden.
  CPU_INT32U IsDir : 1;                                         ///< Bit indicating if entry is a directory or a file.
  CPU_INT32U IsRootDir : 1;                                     ///< Bit indicating if entry is root directory.
} FS_ENTRY_ATTRIB;

typedef struct fs_entry_info {
  FS_ENTRY_ATTRIB            Attrib;                            ///< Entry attributes.
  FS_ID                      NodeId;                            ///< Entry node id.
  FS_ID                      DevId;                             ///< Entry device id.
  CPU_SIZE_T                 Size;                              ///< File size in octets.
  sl_sleeptimer_timestamp_t  DateAccess;                        ///< Date of last access.
  FS_LB_QTY                  BlkCnt;                            ///< Number of blocks allocated for file.
  FS_LB_SIZE                 BlkSize;                           ///< Block size in octets.
  sl_sleeptimer_timestamp_t  DateTimeWr;                        ///< Date/time of last write.
  sl_sleeptimer_timestamp_t  DateTimeCreate;                    ///< Date/time of creation.
} FS_ENTRY_INFO;

/********************************************************************************************************
 ********************************************************************************************************
 *                                           FUNCTION PROTOTYPES
 ********************************************************************************************************
 *******************************************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

#if (FS_CORE_CFG_RD_ONLY_EN == DEF_DISABLED)

void sl_fs_entry_time_set(FS_WRK_DIR_HANDLE    wrk_dir_handle,
                          const CPU_CHAR       *p_path,
                          sl_sleeptimer_date_t *p_time,
                          CPU_INT08U           time_type,
                          RTOS_ERR             *p_err);


void FSEntry_Create(FS_WRK_DIR_HANDLE wrk_dir_handle,
                    const CPU_CHAR    *p_path,
                    FS_FLAGS          entry_type,
                    CPU_BOOLEAN       excl,
                    RTOS_ERR          *p_err);

void FSEntry_AttribSet(FS_WRK_DIR_HANDLE wrk_dir_handle,
                       const CPU_CHAR    *path,
                       FS_FLAGS          attrib,
                       RTOS_ERR          *p_err);

void FSEntry_Del(FS_WRK_DIR_HANDLE wrk_dir_handle,
                 const CPU_CHAR    *p_path,
                 FS_FLAGS          entry_type,
                 RTOS_ERR          *p_err);
#endif

void FSEntry_Query(FS_WRK_DIR_HANDLE wrk_dir_handle,
                   const CPU_CHAR    *p_path,
                   FS_ENTRY_INFO     *p_entry_info,
                   RTOS_ERR          *p_err);

#if (FS_CORE_CFG_RD_ONLY_EN == DEF_DISABLED)
void FSEntry_Rename(FS_WRK_DIR_HANDLE src_wrk_dir_handle,
                    const CPU_CHAR    *p_src_path,
                    FS_WRK_DIR_HANDLE dest_wrk_dir_handle,
                    const CPU_CHAR    *p_dest_path,
                    CPU_BOOLEAN       excl,
                    RTOS_ERR          *p_err);

/*****************************************************************************************************//**
 *                                               FSEntry_TimeSet()
 *
 * @brief    Set a file or directory's date/time.
 *
 * @deprecated
 *           Deprecated function. New code should call @ref sl_fs_entry_time_set().
 *
 * @param    wrk_dir_handle  Handle to a working directory.
 *
 * @param    p_path          Entry path relative to the given working directory.
 *
 * @param    p_time          Pointer to date/time.
 *
 * @param    time_type       Flag to indicate which Date/Time should be set
 *                           FS_DATE_TIME_CREATE
 *                           FS_DATE_TIME_MODIFY
 *                           FS_DATE_TIME_ACCESS
 *                           FS_DATE_TIME_ALL
 *
 * @param    p_err           Pointer to variable that will receive the return error code(s) from this
 *                           function:
 *                               - RTOS_ERR_NONE
 *                               - RTOS_ERR_NOT_FOUND
 *                               - RTOS_ERR_ENTRY_OPENED
 *                               - RTOS_ERR_ENTRY_ROOT_DIR
 *                               - RTOS_ERR_WRK_DIR_CLOSED
 *                               - RTOS_ERR_VOL_CLOSED
 *                               - RTOS_ERR_VOL_CORRUPTED
 *                               - RTOS_ERR_BLK_DEV_CLOSED
 *                               - RTOS_ERR_BLK_DEV_CORRUPTED
 *                               - RTOS_ERR_IO
 *
 * @note     (1) The date/time of the root directory may NOT be set.
 *
 * @note     (2) The date should be generated using Clk_DateTimeMake() or it must be validated using
 *               Clk_IsDateValid() prior calling this function.
 ********************************************************************************************************/
__STATIC_INLINE void FSEntry_TimeSet(FS_WRK_DIR_HANDLE src_wrk_dir_handle,
                                     const CPU_CHAR    *path,
                                     CLK_DATE_TIME     *p_time,
                                     CPU_INT08U        time_type,
                                     RTOS_ERR          *p_err)
{
  sl_sleeptimer_date_t date;

  date.day_of_week = p_time->DayOfWk;
  date.day_of_year = p_time->DayOfYr;
  date.hour = p_time->Hr;
  date.min = p_time->Min;
  date.month = p_time->Month;
  date.month_day = p_time->Day;
  date.sec = p_time->Sec;
  date.time_zone = p_time->TZ_sec;
  date.year = p_time->Yr;

  sl_fs_entry_time_set(src_wrk_dir_handle, path, &date, time_type, p_err);
}
#endif

#ifdef __cplusplus
}
#endif

/****************************************************************************************************//**
 ********************************************************************************************************
 * @}                                            MODULE END
 ********************************************************************************************************
 *******************************************************************************************************/

#endif
