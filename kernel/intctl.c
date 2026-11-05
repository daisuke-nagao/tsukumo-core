/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include <tk/tkernel.h>

#include "asm/rv32/address.h"
#include "context.h"
#include "intctl.h"

#define PLIC_PRIORITY(src)                                                     \
  (PLIC_BASE_ADDRESS + PLIC_PRIORITY_OFFSET + 4 * (src))
#define PLIC_ENABLE(src)                                                       \
  (PLIC_BASE_ADDRESS + PLIC_ENABLE_OFFSET + 4 * ((src) / 32))
#define PLIC_THRESHOLD (PLIC_BASE_ADDRESS + PLIC_THRESHOLD_OFFSET)
#define PLIC_CLAIM (PLIC_BASE_ADDRESS + PLIC_CLAIM_OFFSET)

// The handler of each interrupt number, NULL where none is defined.
static FP s_handlers[CFN_MAX_INTNO];

static BOOL is_intno(UINT intno) { return intno >= 1 && intno < CFN_MAX_INTNO; }

static BOOL is_enabled(UINT src) {
  return (in_w(PLIC_ENABLE(src)) >> (src % 32)) & 1u;
}

// Sets or clears the enable bit of one source. The caller keeps interrupts off
// across this read, modify and write: the bits of 32 sources share a word.
static void set_enabled(UINT src, BOOL on) {
  UW word = in_w(PLIC_ENABLE(src));
  UW bit = 1u << (src % 32);
  out_w(PLIC_ENABLE(src), on ? word | bit : word & ~bit);
}

void tkmc_init_intctl(void) {
  for (UINT intno = 0; intno < CFN_MAX_INTNO; ++intno) {
    s_handlers[intno] = NULL;
  }
  for (UINT src = 1; src < PLIC_SOURCE_COUNT; ++src) {
    out_w(PLIC_PRIORITY(src), 0);
  }
  for (UINT word = 0; word < (PLIC_SOURCE_COUNT + 31) / 32; ++word) {
    out_w(PLIC_ENABLE(32 * word), 0);
  }
  out_w(PLIC_THRESHOLD, 0);
}

// Not done: E_NOMEM. The table is static, so no definition can run out of
// memory.
//
// E_CTX with interrupts disabled is this kernel's choice, not the
// specification's, which lists only the call from the task-independent portion.
// It keeps the call to the context the wait calls need too, and a caller in a
// critical section is not surprised by a handler that changes under it.
ER tk_def_int(UINT intno, CONST T_DINT *pk_dint) {
  UINT intsts;
  DI(intsts);
  ER ercd = E_OK;
  if (isDI(intsts) || tkmc_in_isr()) {
    ercd = E_CTX;
  } else if (!is_intno(intno)) {
    ercd = E_PAR;
  } else if (pk_dint == NULL) {
    s_handlers[intno] = NULL;
  } else if (pk_dint->intatr != TA_HLNG) {
    // TA_ASM (0) is refused as well: the handler is called as a C function,
    // and one that expects the registers of the trap would be called wrongly.
    ercd = E_RSATR;
  } else if (pk_dint->inthdr == NULL) {
    ercd = E_PAR;
  } else {
    s_handlers[intno] = pk_dint->inthdr;
  }
  EI(intsts);
  return ercd;
}

// The specification has no error code for a bad number or level and does not
// say what happens, so nothing is written: the registers of a source the table
// does not hold, or a priority that is none, are not this call's to set.
void EnableInt(UINT intno, INT level) {
  if (!is_intno(intno) || level < 1 || level > PLIC_MAX_PRIORITY) {
    return;
  }
  UINT intsts;
  DI(intsts);
  out_w(PLIC_PRIORITY(intno), (UW)level);
  set_enabled(intno, TRUE);
  // QEMU 11.1.0 does not raise a source that was already pending when its
  // enable bit is set, which the specification requires of EnableInt() ("an
  // interrupt is generated"): the PLIC looks for a source to raise only when a
  // line or the threshold changes. Writing the threshold makes it look. The
  // value it has is written, not 0: a handler that has interrupts on runs with
  // the threshold at the priority of its source (see below), and a call from
  // it must not let the sources of a lower priority in.
  out_w(PLIC_THRESHOLD, in_w(PLIC_THRESHOLD));
  EI(intsts);
}

void DisableInt(UINT intno) {
  if (!is_intno(intno)) {
    return;
  }
  UINT intsts;
  DI(intsts);
  set_enabled(intno, FALSE);
  EI(intsts);
}

// The system's default handler, for a source with no handler: it is switched
// off. Left on, a device that keeps asserting it would be served again and
// again, and the system would do nothing else.
void tkmc_external_interrupt(void) {
  UINT intno = in_w(PLIC_CLAIM);
  if (intno == 0) {
    return;
  }
  FP handler = is_intno(intno) ? s_handlers[intno] : NULL;
  if (handler != NULL) {
    // The PLIC raises only a source whose priority is above the threshold, so
    // a handler that turns interrupts on is interrupted by a source of a higher
    // priority only, never by the same or a lower one. The threshold is put
    // back before the completion, which is what the next claim looks at.
    UW threshold = in_w(PLIC_THRESHOLD);
    out_w(PLIC_THRESHOLD, in_w(PLIC_PRIORITY(intno)));
    ((void (*)(UINT))handler)(intno);
    out_w(PLIC_THRESHOLD, threshold);
  }
  // A source stays off if the default handler runs for it or if the handler
  // switched it off (DisableInt()). The completion is written with the source
  // enabled all the same: the PLIC specification has it ignore the completion
  // of a source that is not enabled, and such a source would stay claimed and
  // never be raised again. Interrupts are off, so nothing is taken in between.
  BOOL stays_on = handler != NULL && is_enabled(intno);
  set_enabled(intno, TRUE);
  out_w(PLIC_CLAIM, intno);
  if (!stays_on) {
    set_enabled(intno, FALSE);
  }
}
