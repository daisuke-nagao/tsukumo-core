/*
 * SPDX-FileCopyrightText: 2025 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef UUID_0194C23B_B68F_7643_AA69_13C344A3D04F
#define UUID_0194C23B_B68F_7643_AA69_13C344A3D04F

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct tkmc_list_head {
  struct tkmc_list_head *next;
  struct tkmc_list_head *prev;
} tkmc_list_head;

static inline void tkmc_init_list_head(tkmc_list_head *head) {
  head->next = head;
  head->prev = head;
}

static inline BOOL tkmc_list_empty(const tkmc_list_head *head) {
  return head == head->next ? TRUE : FALSE;
}

#define tkmc_offsetof(type, member) ((unsigned long)&(((type *)0)->member))

#define tkmc_container_of(ptr, type, member)                                   \
  ((type *)((char *)(ptr) - tkmc_offsetof(type, member)))

#define tkmc_list_entry(ptr, type, member) tkmc_container_of(ptr, type, member)

#define tkmc_list_first_entry(ptr, type, member)                               \
  tkmc_list_entry((ptr)->next, type, member)

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_0194C23B_B68F_7643_AA69_13C344A3D04F */
