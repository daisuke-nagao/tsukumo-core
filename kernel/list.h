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

static inline void tkmc_list_del(tkmc_list_head *head) {
  tkmc_list_head *prev = head->prev;
  tkmc_list_head *next = head->next;

  prev->next = next;
  next->prev = prev;

  head->next = head->prev = (tkmc_list_head *)0xdeadbeef;
}

static inline void tkmc_list_add(tkmc_list_head *new, tkmc_list_head *head) {
  new->next = head->next;
  new->prev = head;
  head->next->prev = new;
  head->next = new;
}

static inline void tkmc_list_add_tail(tkmc_list_head *new,
                                      tkmc_list_head *head) {
  new->next = head;
  new->prev = head->prev;
  new->prev->next = new;
  head->prev = new;
}

/* Macro to get the next element */
#define tkmc_list_next_entry(pos, member)                                      \
  tkmc_list_entry((pos)->member.next, typeof(*(pos)), member)

/* Macro for normal traversal */
#define tkmc_list_for_each_entry(pos, head, member)                            \
  for (pos = tkmc_list_first_entry(head, typeof(*pos), member);                \
       &pos->member != (head); pos = tkmc_list_next_entry(pos, member))

/*
 * Macro for safe traversal:
 * Since the next element `n` is pre-fetched even if `pos` is deleted within
 * the loop, the entire list can be safely traversed.
 */
#define tkmc_list_for_each_entry_safe(pos, n, head, member)                    \
  for (pos = tkmc_list_first_entry(head, typeof(*pos), member),                \
      n = tkmc_list_next_entry(pos, member);                                   \
       &pos->member != (head); pos = n, n = tkmc_list_next_entry(n, member))

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_0194C23B_B68F_7643_AA69_13C344A3D04F */
