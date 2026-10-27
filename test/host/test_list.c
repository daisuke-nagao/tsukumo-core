/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// Property test of kernel/list.h, the doubly linked list every kernel queue is
// built on: random sequences of add, add_tail, del and a safe traversal that
// deletes some of the nodes it passes are applied to a list and to an array
// that models it, and after every step the list must match the model, forwards
// and backwards.

#include <stdio.h>

#include <tk/tkernel.h>
#include <unity.h>

#include "list.h"

enum { NODES = 8, STEPS_PER_SEED = 300, SEEDS = 20 };

// The link is not the first member, so the container offset is not zero and a
// missing offset calculation shows.
typedef struct {
  INT id;
  tkmc_list_head link;
} node_t;

static tkmc_list_head g_head;
static node_t g_nodes[NODES];
static INT g_model[NODES]; // Node IDs in list order.
static UINT g_length;

static UINT g_adds, g_add_tails, g_dels, g_sweeps, g_empties, g_singles;

// A fixed generator, so a failing run can be reproduced from the seed.
static UINT next_random(UINT *state) {
  *state = *state * 1664525u + 1013904223u;
  return *state >> 8;
}

static BOOL in_model(INT id) {
  for (UINT i = 0; i < g_length; ++i) {
    if (g_model[i] == id) {
      return TRUE;
    }
  }
  return FALSE;
}

static void check_matches_model(UINT seed, UINT step) {
  char where[40];
  snprintf(where, sizeof(where), "seed %u, step %u", seed, step);

  TEST_ASSERT_EQUAL_INT_MESSAGE(g_length == 0, tkmc_list_empty(&g_head), where);

  UINT index = 0;
  node_t *pos;
  tkmc_list_for_each_entry(pos, &g_head, link) {
    TEST_ASSERT_LESS_THAN_UINT_MESSAGE(g_length, index, where);
    TEST_ASSERT_EQUAL_INT_MESSAGE(g_model[index], pos->id, where);
    ++index;
  }
  TEST_ASSERT_EQUAL_UINT_MESSAGE(g_length, index, where);

  // Backwards, through the prev links.
  index = g_length;
  for (tkmc_list_head *link = g_head.prev; link != &g_head; link = link->prev) {
    TEST_ASSERT_GREATER_THAN_UINT_MESSAGE(0, index, where);
    --index;
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        g_model[index], tkmc_list_entry(link, node_t, link)->id, where);
  }
  TEST_ASSERT_EQUAL_UINT_MESSAGE(0, index, where);
}

static void insert_model(UINT position, INT id) {
  for (UINT i = g_length; i > position; --i) {
    g_model[i] = g_model[i - 1];
  }
  g_model[position] = id;
  ++g_length;
}

static void remove_model(UINT position) {
  for (UINT i = position; i + 1 < g_length; ++i) {
    g_model[i] = g_model[i + 1];
  }
  --g_length;
}

// A removed node is poisoned, so a stale use is caught.
static void check_poisoned(const char *where, INT id) {
  TEST_ASSERT_EQUAL_PTR_MESSAGE((tkmc_list_head *)0xdeadbeef,
                                g_nodes[id].link.next, where);
  TEST_ASSERT_EQUAL_PTR_MESSAGE((tkmc_list_head *)0xdeadbeef,
                                g_nodes[id].link.prev, where);
}

// Walks the list with the safe traversal, which is how the kernel wakes
// waiters, and deletes a random subset of the nodes on the way.
static void sweep(UINT *state, const char *where) {
  UINT kept = 0;
  UINT index = 0;
  node_t *pos, *n;
  tkmc_list_for_each_entry_safe(pos, n, &g_head, link) {
    TEST_ASSERT_LESS_THAN_UINT_MESSAGE(g_length, index, where);
    TEST_ASSERT_EQUAL_INT_MESSAGE(g_model[index], pos->id, where);
    if (next_random(state) % 2 != 0) {
      tkmc_list_del(&pos->link);
      check_poisoned(where, pos->id);
    } else {
      g_model[kept++] = pos->id; // kept <= index: the model is compacted.
    }
    ++index;
  }
  TEST_ASSERT_EQUAL_UINT_MESSAGE(g_length, index, where);
  g_length = kept;
}

static void run_seed(UINT seed) {
  UINT state = seed;
  tkmc_init_list_head(&g_head);
  g_length = 0;
  for (INT id = 0; id < NODES; ++id) {
    g_nodes[id].id = id;
  }

  for (UINT step = 0; step < STEPS_PER_SEED; ++step) {
    char where[40];
    snprintf(where, sizeof(where), "seed %u, step %u", seed, step);

    UINT choice = next_random(&state) % 4;
    // A full list can only lose nodes and an empty one can only gain a node.
    if (g_length == NODES) {
      choice = 2 + next_random(&state) % 2;
    } else if (g_length == 0) {
      choice = next_random(&state) % 2;
    }

    if (choice == 2) {
      INT id = g_model[next_random(&state) % g_length];
      UINT position = 0;
      while (g_model[position] != id) {
        ++position;
      }
      tkmc_list_del(&g_nodes[id].link);
      check_poisoned(where, id);
      remove_model(position);
      ++g_dels;
      if (g_length == 0) {
        ++g_empties;
      }
    } else if (choice == 3) {
      sweep(&state, where);
      ++g_sweeps;
      if (g_length == 0) {
        ++g_empties;
      }
    } else {
      // Pick a node that is not in the list.
      INT id = (INT)(next_random(&state) % NODES);
      while (in_model(id)) {
        id = (id + 1) % NODES;
      }
      if (choice == 0) {
        tkmc_list_add(&g_nodes[id].link, &g_head);
        insert_model(0, id);
        ++g_adds;
      } else {
        tkmc_list_add_tail(&g_nodes[id].link, &g_head);
        insert_model(g_length, id);
        ++g_add_tails;
      }
      if (g_length == 1) {
        ++g_singles;
      }
    }
    check_matches_model(seed, step);
  }
}

void test_list_matches_a_model_under_random_operations(void) {
  g_adds = g_add_tails = g_dels = g_sweeps = g_empties = g_singles = 0;
  for (UINT seed = 1; seed <= SEEDS; ++seed) {
    run_seed(seed);
  }
  // The run only means something if it reached every kind of step and the
  // empty and one element states (they cover all seeds, so there is no seed to
  // report).
  TEST_ASSERT_GREATER_THAN_UINT(0, g_adds);
  TEST_ASSERT_GREATER_THAN_UINT(0, g_add_tails);
  TEST_ASSERT_GREATER_THAN_UINT(0, g_dels);
  TEST_ASSERT_GREATER_THAN_UINT(0, g_sweeps);
  TEST_ASSERT_GREATER_THAN_UINT(0, g_empties);
  TEST_ASSERT_GREATER_THAN_UINT(0, g_singles);
}
