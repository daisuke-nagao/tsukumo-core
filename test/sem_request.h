/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

// A child task that asks a semaphore for units and records what tk_wai_sem()
// returned, for the semaphore suites. The child only records: a case that
// asserted in the child could leave another waiter blocked for good.

#ifndef UUID_01A1EE03_144E_73C0_883A_CCE5F177634E
#define UUID_01A1EE03_144E_73C0_883A_CCE5F177634E

#include <tk/tkernel.h>

#include "runner.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

// The result of a request that has not returned yet.
enum { REQUEST_UNSET = 1 };
_Static_assert(REQUEST_UNSET > E_OK, "REQUEST_UNSET cannot be a wait result");

typedef struct {
  ID semid;
  INT units;
  TMO timeout;
  volatile ER result;
} request_t;

static void request_units(void *arg) {
  request_t *request = arg;
  request->result =
      tk_wai_sem(request->semid, request->units, request->timeout);
  test_checkpoint();
}

// Spawns a child at prio that asks semid for units, waiting up to timeout, and
// returns its ID. A child of higher priority than the case makes the request,
// or waits, before this returns.
static inline ID start_request(request_t *request, ID semid, INT units,
                               TMO timeout, PRI prio) {
  *request = (request_t){semid, units, timeout, REQUEST_UNSET};
  return test_spawn(request_units, prio, request);
}

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_01A1EE03_144E_73C0_883A_CCE5F177634E */
