/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef UUID_01946E67_323D_7643_BC6D_0CBFBCC20897
#define UUID_01946E67_323D_7643_BC6D_0CBFBCC20897

/* Device Driver Functions */
#define TK_SUPPORT_TASKEVENT FALSE /* Support of task event */
#define TK_SUPPORT_DISWAI FALSE    /* Support of disabling wait */
#define TK_SUPPORT_IOPORT FALSE    /* Support of I/O port access */
#define TK_SUPPORT_MICROWAIT FALSE /* Support of micro wait */

/* Power Management Functions */
#define TK_SUPPORT_LOWPOWER FALSE /* Support of power management functions */

/* Static/dynamic Memory Management Functions */
#define TK_SUPPORT_USERBUF TRUE  /* Support of user-specified buffer (TA_USERBUF) */
#define TK_SUPPORT_AUTOBUF FALSE /* Support of automatic buffer allocation (No TA_USERBUF specification) */
#define TK_SUPPORT_MEMLIB FALSE  /* Support of memory allocation library */

/* Task Exception Handling Functions */
#define TK_SUPPORT_TASKEXCEPTION FALSE /* Support of task exception handling functions */

/* Subsystem Management Functions */
#define TK_SUPPORT_SUBSYSTEM FALSE /* Support of subsystem management functions */
#define TK_SUPPORT_SSYEVENT FALSE  /* Support of event processing of subsystems */

/*System Configuration Information Acquisition Functions */
#define TK_SUPPORT_SYSCONF FALSE /* Support of system configuration information management functions */

/* Supporting 64-bit and 16-bit CPUs */
#define TK_HAS_DOUBLEWORD TRUE    /* Support of 64-bit data types (D, UD, VD) */
#define TK_SUPPORT_USEC FALSE     /* Support of microsecond */
#define TK_SUPPORT_LARGEDEV FALSE /* Support of large mass-storage device (64-bit) */
#define TK_SUPPORT_SERCD TRUE     /* Support of sub error code */

/* Interrupt Management Functions */
#define TK_SUPPORT_INTCTRL FALSE      /* Support of interrupt controller management */
#define TK_HAS_ENAINTLEVEL FALSE      /* Can specify interrupt priority level */
#define TK_SUPPORT_CPUINTLEVEL FALSE  /* Support of CPU interrupt mask level */
#define TK_SUPPORT_CTRLINTLEVEL FALSE /* Support of interrupt controller mask level */
#define TK_SUPPORT_INTMODE FALSE      /* Support of setting interrupt mode */

/* Memory Cache Control Functions */
#define TK_SUPPORT_CACHECTRL FALSE    /* Support of memory cache control functions */
#define TK_SUPPORT_SETCACHEMODE FALSE /* Support of set cache mode function */
#define TK_SUPPORT_WBCACHE FALSE      /* Support of write-back cache */
#define TK_SUPPORT_WTCACHE FALSE      /* Support of write-through cache */

/* FPU(COP) Support Functions */
#define TK_SUPPORT_FPU FALSE  /* Support of FPU */
#define TK_SUPPORT_COP0 FALSE /* Support of co-processor number 0 */
#define TK_SUPPORT_COP1 FALSE /* Support of co-processor number 1 */
#define TK_SUPPORT_COP2 FALSE /* Support of co-processor number 2 */
#define TK_SUPPORT_COP3 FALSE /* Support of co-processor number 3 */

/* Miscellaneous Functions */
#define TK_SUPPORT_ASM FALSE      /* Support of assembly language function entry/exit */
#define TK_SUPPORT_REGOPS FALSE   /* Support for task-register manipulation functions */
#define TK_ALLOW_MISALIGN FALSE   /* Memory misalign access is permitted */
#define TK_BIGENDIAN FALSE        /* Is big endian (Must be defined) */
#define TK_TRAP_SVC FALSE         /* Use CPU Trap instruction for system call entry */
#define TK_HAS_SYSSTACK FALSE     /* Task has a separate system stack */
#define TK_SUPPORT_PTIMER FALSE   /* Support of physical timer function */
#define TK_SUPPORT_UTC FALSE      /* Support of UNIX time */
#define TK_SUPPORT_TRONTIME FALSE /* Support of TRON time */

/* Debugger Support Functions */
#define TK_SUPPORT_DSNAME FALSE /* Support of DS object name */
#define TK_SUPPORT_DBGSPT FALSE /* Support of μT-Kernel/DS */

/* Service Profile Items that Represent Values */
#define TK_SPECVER_MAGIC 6
#define TK_SPECVER_MAJOR 3
#define TK_SPECVER_MINOR 0
#define TK_SPECVER ((TK_SPECVER_MAJOR * 256) | TK_SPECVER_MINOR)
#define TK_MAX_TSKPRI CFN_MAX_PRI
#define TK_WAKEUP_MAXCNT 4095
#define TK_SEMAPHORE_MAXCNT 4095
#define TK_SUSPEND_MAXCNT 4095
#define TK_MEM_RNG0 0
#define TK_MEM_RNG1 0
#define TK_MEM_RNG2 0
#define TK_MEM_RNG3 0
#define TK_MAX_PTIMER 0

#endif /* UUID_01946E67_323D_7643_BC6D_0CBFBCC20897 */
