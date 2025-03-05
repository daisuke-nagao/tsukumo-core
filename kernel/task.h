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

#define TA_ASM 0x00000000UL     /* Program by assembler */
#define TA_HLNG 0x00000001UL    /* Program by high level programming language */
#define TA_USERBUF 0x00000020UL /* Specify user buffer */
#define TA_DSNAME 0x00000040UL  /* Use object name */

#define TA_RNG0 0x00000000UL /* Execute by protection level 0 */
#define TA_RNG1 0x00000100UL /* Execute by protection level 1 */
#define TA_RNG2 0x00000200UL /* Execute by protection level 2 */
#define TA_RNG3 0x00000300UL /* Execute by protection level 3 */

#define TA_COP0 0x00001000U /* Use coprocessor (ID=0) */
#define TA_COP1 0x00002000U /* Use coprocessor (ID=1) */
#define TA_COP2 0x00004000U /* Use coprocessor (ID=2) */
#define TA_COP3 0x00008000U /* Use coprocessor (ID=3) */

enum TaskState {
  NON_EXISTENT = 0,
  DORMANT,
  READY,
  RUNNING,
  WAIT,
};

/* Task Control Block */
typedef struct TCB {
  tkmc_list_head head;
  ID tskid;
  PRI itskpri;
  enum TaskState state;
  void *sp;
  FP task;
  void *exinf;
  UINT tick_count;
} TCB;

extern TCB *current;

extern void tkmc_init_tcb(void);

typedef struct T_CTSK {
  void *exinf;  /* Extended Information */
  ATR tskatr;   /* Task Attribute  */
  FP task;      /* Task Start Address */
  PRI itskpri;  /* Initial Task Priority */
  SZ stksz;     /* Stack Size */
  void *bufptr; /* Buffer Pointer */
} T_CTSK;

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_01948452_C50C_7B25_A131_C2733EF5E067 */