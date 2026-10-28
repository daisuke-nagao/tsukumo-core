/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef UUID_01A181E4_EB43_7002_85F2_320A83B5B7DF
#define UUID_01A181E4_EB43_7002_85F2_320A83B5B7DF

#include <tk/tkernel.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @brief Tell whether id is a valid ID of a kind whose largest ID is max.
 *
 * @return TRUE if 1 <= id <= max, FALSE otherwise. A call that gets FALSE must
 *         return E_ID before it forms the address of a table entry from id.
 */
static inline BOOL tkmc_id_in_range(ID id, ID max) {
  return (id >= 1 && id <= max) ? TRUE : FALSE;
}

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_01A181E4_EB43_7002_85F2_320A83B5B7DF */
