/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef UUID_01955F69_C5A7_741B_B0BA_A9BC5CD7A819
#define UUID_01955F69_C5A7_741B_B0BA_A9BC5CD7A819

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define CLINT_BASE_ADDRESS 0x2000000

#define CLINT_MSIP_OFFSET 0x0
#define CLINT_MTIMECMP_OFFSET 0x4000
#define CLINT_MTIME_OFFSET 0xBFF8

/*
 * The PLIC of the QEMU virt board. Context 0 is the machine mode external
 * interrupt of hart 0 (the device tree lists it first, then the supervisor
 * mode one). Source 0 does not exist; the board has sources 1 to 95.
 */
#define PLIC_BASE_ADDRESS 0xC000000

#define PLIC_PRIORITY_OFFSET 0x0   /* A word for each source. */
#define PLIC_PENDING_OFFSET 0x1000 /* A bit for each source, 32 to a word. */
#define PLIC_ENABLE_OFFSET 0x2000  /* A bit for each source, 32 to a word. */
#define PLIC_THRESHOLD_OFFSET 0x200000
#define PLIC_CLAIM_OFFSET 0x200004 /* Read to claim, write to complete. */
#define PLIC_SOURCE_COUNT 96       /* Sources 0 to 95. */
#define PLIC_MAX_PRIORITY 7        /* The priorities are 1 to 7. */

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_01955F69_C5A7_741B_B0BA_A9BC5CD7A819 */
