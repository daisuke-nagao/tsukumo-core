/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef UUID_01A17C88_AD56_7D30_83D6_DA537927A543
#define UUID_01A17C88_AD56_7D30_83D6_DA537927A543

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/// Calls tkmc_memset() for every fill value (0..255), every length 0..70 and
/// every start offset 0..3, and fails the running test at the first call that
/// does not write exactly the requested range with exactly the value, or that
/// does not return its destination. The same function runs on the PC and on
/// QEMU, so a defect cannot hide in one of them.
extern void memset_matrix_check(void);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_01A17C88_AD56_7D30_83D6_DA537927A543 */
