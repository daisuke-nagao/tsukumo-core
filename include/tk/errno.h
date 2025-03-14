/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef UUID_01946CA7_7025_7EE0_9CD5_537052C2504E
#define UUID_01946CA7_7025_7EE0_9CD5_537052C2504E

#include <tk/typedef.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* 65536 == 0x10000 */
#define MERCD(er) ((ER)((W)(er) / 65536))
#define SERCD(er) ((H)((UW)(er) % 65536))
#define ERCD(mer, ser) ((ER)((W)(mer) * 65536) + (UH)(ser))

/* Error code definitions */

/* Normal completion error class (0) */
#define E_OK ERCD(0, 0) /* Normal completion */

/* Internal error class (5-8) */
#define E_SYS ERCD(-5, 0)   /* System error */
#define E_NOCOP ERCD(-6, 0) /* Coprocessor unavailable */

/* Unsupported error class (9-16) */
#define E_NOSPT ERCD(-9, 0)  /* Unsupported function */
#define E_RSFN ERCD(-10, 0)  /* Reserved function code */
#define E_RSATR ERCD(-11, 0) /* Reserved attribute */

/* Parameter error class (17-24) */
#define E_PAR ERCD(-17, 0) /* Parameter error */
#define E_ID ERCD(-18, 0)  /* Invalid ID number */

/* Call context error class (25-32) */
#define E_CTX ERCD(-25, 0)   /* Context error */
#define E_MACV ERCD(-26, 0)  /* Memory access violation */
#define E_OACV ERCD(-27, 0)  /* Object access violation */
#define E_ILUSE ERCD(-28, 0) /* Illegal system call use */

/* Resource shortage error class (33-40) */
#define E_NOMEM ERCD(-33, 0) /* Insufficient memory */
#define E_LIMIT ERCD(-34, 0) /* System limit exceeded */

/* Object state error class (41-48) */
#define E_OBJ ERCD(-41, 0)   /* Invalid object state */
#define E_NOEXS ERCD(-42, 0) /* Non-existent object */
#define E_QOVR ERCD(-43, 0)  /* Queue overflow */

/* Wait release error class (49-56) */
#define E_RLWAI ERCD(-49, 0)  /* Forced release from waiting */
#define E_TMOUT ERCD(-50, 0)  /* Polling failure or timeout */
#define E_DLT ERCD(-51, 0)    /* Waiting object deleted */
#define E_DISWAI ERCD(-52, 0) /* Release from waiting due to wait disable */

/* Device error class (57-64) (uT-Kernel/SM) */
#define E_IO ERCD(-57, 0)    /* I/O error */
#define E_NOMDA ERCD(-58, 0) /* No media */

/* Miscellaneous state error class (65-72) (uT-Kernel/SM) */
#define E_BUSY ERCD(-65, 0)  /* Busy state */
#define E_ABORT ERCD(-66, 0) /* Aborted */
#define E_RONLY ERCD(-67, 0) /* Write protected */

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_01946CA7_7025_7EE0_9CD5_537052C2504E */
