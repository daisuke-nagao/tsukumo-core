/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef UUID_01946FAC_8E45_7658_B009_C10ED747A05C
#define UUID_01946FAC_8E45_7658_B009_C10ED747A05C

#include <sys/config.h>

// clang-format off
#include <tk/typedef.h>
// clang-format on
#include <tk/errno.h>

#include <sys/profile.h>

#include <sys/interrupt.h>
#include <sys/ioport.h>

#ifdef TKERNEL_CHECK_CONST
#define CONST const
#else
#define CONST
#endif /* TKERNEL_CHECK_CONST */

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

typedef struct T_CTSK {
  void *exinf;  /* Extended Information */
  ATR tskatr;   /* Task Attribute  */
  FP task;      /* Task Start Address */
  PRI itskpri;  /* Initial Task Priority */
  SZ stksz;     /* Stack Size */
  void *bufptr; /* Buffer Pointer */
} T_CTSK;

extern void tk_ext_tsk(void);
extern ID tk_cre_tsk(const T_CTSK *pk_ctsk);
extern ER tk_sta_tsk(ID tskid, INT stacd);
extern ER tk_dly_tsk(TMO dlytm);
extern ER tk_slp_tsk(TMO tmout);
extern ER tk_rel_wai(ID tskid);
extern ER tk_wup_tsk(ID tskid);

#endif /* UUID_01946FAC_8E45_7658_B009_C10ED747A05C */