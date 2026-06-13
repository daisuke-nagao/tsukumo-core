/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef UUID_0194865E_691A_7E72_BAEC_34AA8041878E
#define UUID_0194865E_691A_7E72_BAEC_34AA8041878E

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Maximum task priority (also reflected in service profile item TK_MAX_TSKPRI)*/
#define CFN_MAX_PRI 16
_Static_assert(CFN_MAX_PRI >= 16, "CFN_MAX_PRI must be greater than or equal to 16, as required by TK_MAX_TSKPRI.");

/* Lowest address of the area dynamically managed by uT-Kernel memory management function */
// #define CFN_SYSTEMAREA_TOP 0

/* Highest address of the area dynamically managed by uT-Kernel memory management function */
// #define CFN_SYSTEMAREA_END 0

/* System timer interrupt period (milliseconds) */
// #define CFN_TIMER_PERIOD 10

/* Maximum number of tasks */
#define CFN_MAX_TSKID 32

/* Maximum number of semaphores */
#define CFN_MAX_SEMID 16

/* Maximum number of event flags */
#define CFN_MAX_FLGID 16
_Static_assert(CFN_MAX_FLGID < 0x80000000, "CFN_MAX_FLGID must be less than 2147483648 (0x80000000).");

/* Maximum number of mailboxes */
#define CFN_MAX_MBXID 8

/* Maximum number of mutexes */
// #define CFN_MAX_MTXID 4

/* Maximum number of message buffers */
// #define CFN_MAX_MBFID 8

/* Maximum number of fixed-size memory pools */
// #define CFN_MAX_MPFID 8

/* Maximum number of variable-size memory pools */
// #define CFN_MAX_MPLID 4

/* Maximum number of cyclic handlers */
// #define CFN_MAX_CYCID 4

/* Maximum number of alarm handlers */
// #define CFN_MAX_ALMID 8

/* Maximum number of subsystems */
// #define CFN_MAX_SSYID      // Undefined

/* Maximum number of subsystem priorities */
// #define CFN_MAX_SSYPRI     // Undefined

/* Maximum number of registered devices */
// #define CFN_MAX_REGDEV 8

/* Maximum number of open devices */
// #define CFN_MAX_OPNDEV 16

/* Maximum number of device requests */
// #define CFN_MAX_REQDEV 16

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_0194865E_691A_7E72_BAEC_34AA8041878E */
