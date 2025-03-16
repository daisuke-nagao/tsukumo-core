/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef UUID_01948452_C50C_7B25_A131_C2733EF5E067
#define UUID_01948452_C50C_7B25_A131_C2733EF5E067

#include <tk/tkernel.h>

#include "list.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

enum TaskState {
  TTS_NOEXS = 0x0000,
  TTS_RUN = 0x0001,
  TTS_RDY = 0x0002,
  TTS_WAI = 0x0004,
  TTS_SUS = 0x0008,
  TTS_WAS = 0x000c,
  TTS_DMT = 0x0010,
  TTS_NODISWAI = 0x0080,
};

enum TaskWait {
  TTW_SLP = 0x00000001,  /**< Waiting due to `tk_slp_tsk` */
  TTW_DLY = 0x00000002,  /**< Waiting due to `tk_dly_tsk` */
  TTW_SEM = 0x00000004,  /**< Waiting due to `tk_wai_sem` */
  TTW_FLG = 0x00000008,  /**< Waiting due to `tk_wai_flg` */
  TTW_MBX = 0x00000040,  /**< Waiting due to `tk_rcv_mbx` */
  TTW_MTX = 0x00000080,  /**< Waiting due to `tk_loc_mtx` */
  TTW_SMBF = 0x00000100, /**< Waiting due to `tk_snd_mbf` */
  TTW_RMBF = 0x00000200, /**< Waiting due to `tk_rcv_mbf` */
  // TTW_CAL  = 0x00000400, /**< Reserved  */
  // TTW_ACP  = 0x00000800, /**< Reserved  */
  // TTW_RDV  = 0x00001000, /**< Reserved  */
  // TTW_CAL_RDV = TTW_CAL|TTW_RDV, /**< Reserved */
  TTW_MPF = 0x00002000, /**< Waiting due to `tk_get_mpf` */
  TTW_MPL = 0x00004000, /**< Waiting due to `tk_get_mpl` */
  TTW_EV1 = 0x00010000, /**< Waiting for Task Event #1 */
  TTW_EV2 = 0x00020000, /**< Waiting for Task Event #2 */
  TTW_EV3 = 0x00040000, /**< Waiting for Task Event #3 */
  TTW_EV4 = 0x00080000, /**< Waiting for Task Event #4 */
  TTW_EV5 = 0x00100000, /**< Waiting for Task Event #5 */
  TTW_EV6 = 0x00200000, /**< Waiting for Task Event #6 */
  TTW_EV7 = 0x00400000, /**< Waiting for Task Event #7 */
  TTW_EV8 = 0x00800000  /**< Waiting for Task Event #8 */
};

/* Task Control Block */
typedef struct TCB {
  tkmc_list_head head;
  ID tskid;
  PRI itskpri;
  UINT tskstat;
  UW tskwait;
  void *sp;
  void *initial_sp;
  FP task;
  void *exinf;
  UINT delay_ticks;
  ER wupcause;
  UINT wupcnt;
} TCB;

extern TCB *current;

extern void tkmc_init_tcb(void);

extern void __launch_task(void **sp_end);
extern TCB *tkmc_get_highest_priority_task(void);
extern void tkmc_yield(void);

extern TCB *current;
extern TCB *next;
extern tkmc_list_head tkmc_ready_queue[CFN_MAX_PRI];
extern TCB *current;
extern TCB *next;
extern TCB tkmc_tcbs[CFN_MAX_TSKID];

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_01948452_C50C_7B25_A131_C2733EF5E067 */
