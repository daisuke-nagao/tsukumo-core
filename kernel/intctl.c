/*
 * SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include <tk/tkernel.h>

#include "asm/rv32/address.h"
#include "context.h"
#include "intctl.h"
#include "panic.h"

#define PLIC_PRIORITY(src)                                                     \
  (PLIC_BASE_ADDRESS + PLIC_PRIORITY_OFFSET + 4 * (src))
#define PLIC_ENABLE(src)                                                       \
  (PLIC_BASE_ADDRESS + PLIC_ENABLE_OFFSET + 4 * ((src) / 32))
#define PLIC_PENDING(src)                                                      \
  (PLIC_BASE_ADDRESS + PLIC_PENDING_OFFSET + 4 * ((src) / 32))
#define PLIC_THRESHOLD (PLIC_BASE_ADDRESS + PLIC_THRESHOLD_OFFSET)
#define PLIC_CLAIM (PLIC_BASE_ADDRESS + PLIC_CLAIM_OFFSET)

extern void **schedule(void *sp);

typedef struct {
  FP handler;
  UINT kind;
} interrupt_handler;

// Handler table entry, default-initialized to no handler.
static interrupt_handler s_handlers[CFN_MAX_INTNO];

// Active external interrupt contexts are linked in nesting order.
tkmc_interrupt_context *volatile tkmc_active_interrupt_context;

#define CLINT_MSIP_ADDRESS (CLINT_BASE_ADDRESS + CLINT_MSIP_OFFSET)

_Static_assert(TKMC_PLIC_BASE_ADDRESS == PLIC_BASE_ADDRESS,
               "public ASM PLIC base must match the RV32 port");
_Static_assert(TKMC_PLIC_ENABLE_OFFSET == PLIC_ENABLE_OFFSET,
               "public ASM PLIC enable offset must match the RV32 port");
_Static_assert(TKMC_PLIC_THRESHOLD_OFFSET == PLIC_THRESHOLD_OFFSET,
               "public ASM PLIC threshold offset must match the RV32 port");
_Static_assert(TKMC_PLIC_CLAIM_OFFSET == PLIC_CLAIM_OFFSET,
               "public ASM PLIC claim offset must match the RV32 port");
_Static_assert((TKMC_INTERRUPT_KIND_NONE & ~TKMC_INTERRUPT_KIND_MASK) == 0 &&
                   (TKMC_INTERRUPT_KIND_HLNG & ~TKMC_INTERRUPT_KIND_MASK) ==
                       0 &&
                   (TKMC_INTERRUPT_KIND_ASM & ~TKMC_INTERRUPT_KIND_MASK) == 0,
               "interrupt kinds must fit the shared mask");
_Static_assert((TKMC_INTERRUPT_KIND_MASK & TKMC_INTERRUPT_COMPLETED) == 0 &&
                   (TKMC_INTERRUPT_COMPLETED &
                    (TKMC_INTERRUPT_COMPLETED - 1)) == 0,
               "interrupt completion flag must be a distinct single bit");

#if defined(__riscv) && __riscv_xlen == 32
_Static_assert(sizeof(tkmc_interrupt_context) == TKMC_IRQCTX_SIZE,
               "RV32 interrupt context size must match its assembly ABI");
_Static_assert(__builtin_offsetof(tkmc_interrupt_context, previous) ==
                   TKMC_IRQCTX_PREVIOUS,
               "previous offset must match the assembly ABI");
_Static_assert(__builtin_offsetof(tkmc_interrupt_context, frame) ==
                   TKMC_IRQCTX_FRAME,
               "frame offset must match the assembly ABI");
_Static_assert(__builtin_offsetof(tkmc_interrupt_context, entry_sp) ==
                   TKMC_IRQCTX_ENTRY_SP,
               "entry SP offset must match the assembly ABI");
_Static_assert(__builtin_offsetof(tkmc_interrupt_context, handler) ==
                   TKMC_IRQCTX_HANDLER,
               "handler offset must match the assembly ABI");
_Static_assert(__builtin_offsetof(tkmc_interrupt_context, depth) ==
                   TKMC_IRQCTX_DEPTH,
               "depth offset must match the assembly ABI");
_Static_assert(__builtin_offsetof(tkmc_interrupt_context, intno) ==
                   TKMC_IRQCTX_INTNO,
               "interrupt number offset must match the assembly ABI");
_Static_assert(__builtin_offsetof(tkmc_interrupt_context, saved_threshold) ==
                   TKMC_IRQCTX_SAVED_THRESHOLD,
               "threshold offset must match the assembly ABI");
_Static_assert(__builtin_offsetof(tkmc_interrupt_context, kind) ==
                   TKMC_IRQCTX_KIND,
               "kind offset must match the assembly ABI");
_Static_assert(TKMC_IRQCTX_KIND + sizeof(((tkmc_interrupt_context *)0)->kind) ==
                   TKMC_IRQCTX_SIZE,
               "kind field must end at the context ABI size");
#endif

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

// Claim one external source and capture all per-nesting state before dispatch.
// ctx is allocated by the trap entry and remains live until finish/return.
void tkmc_prepare_external_interrupt(tkmc_interrupt_context *ctx, void *frame) {
  UINT intsts;
  DI(intsts);
  ctx->previous = tkmc_active_interrupt_context;
  ctx->frame = frame;
  ctx->entry_sp = ctx;
  ctx->handler = NULL;
  ctx->depth = tkmc_isr_depth;
  ctx->intno = in_w(PLIC_CLAIM);
  ctx->saved_threshold = 0;
  ctx->kind = TKMC_INTERRUPT_KIND_NONE;
  if (ctx->intno != 0) {
    if (is_intno(ctx->intno)) {
      ctx->handler = s_handlers[ctx->intno].handler;
      ctx->kind = s_handlers[ctx->intno].kind & TKMC_INTERRUPT_KIND_MASK;
    }
    if (ctx->handler != NULL) {
      ctx->saved_threshold = in_w(PLIC_THRESHOLD);
      out_w(PLIC_THRESHOLD, in_w(PLIC_PRIORITY(ctx->intno)));
    }
    tkmc_active_interrupt_context = ctx;
  }
  EI(intsts);
}

// The caller keeps interrupts off while completion and the per-claim flag are
// updated, so nested handlers cannot observe or complete the same claim.
static void tkmc_complete_external_interrupt(tkmc_interrupt_context *ctx) {
  if (ctx->kind & TKMC_INTERRUPT_COMPLETED) {
    return;
  }

  BOOL stays_on = is_enabled(ctx->intno);
  set_enabled(ctx->intno, TRUE);
  out_w(PLIC_CLAIM, ctx->intno);
  ctx->kind |= TKMC_INTERRUPT_COMPLETED;
  if (!stays_on) {
    set_enabled(ctx->intno, FALSE);
  }
}

// The threshold precedes completion so the next PLIC claim sees the priority
// of the enclosing handler. A default unhandled source remains disabled.
void tkmc_finish_external_interrupt(tkmc_interrupt_context *ctx) {
  if (ctx->intno == 0) {
    return;
  }

  UINT intsts;
  DI(intsts);
  if (tkmc_active_interrupt_context != ctx || ctx->depth != tkmc_isr_depth) {
    tkmc_invalid_ret_int();
  }
  if (ctx->handler != NULL) {
    out_w(PLIC_THRESHOLD, ctx->saved_threshold);
  }
  tkmc_complete_external_interrupt(ctx);
  if (ctx->handler == NULL) {
    set_enabled(ctx->intno, FALSE);
  }
  tkmc_active_interrupt_context = ctx->previous;
  EI(intsts);
}

void EndOfInt(UINT intno) {
  if (!is_intno(intno)) {
    return;
  }

  UINT intsts;
  DI(intsts);
  for (tkmc_interrupt_context *ctx = tkmc_active_interrupt_context; ctx != NULL;
       ctx = ctx->previous) {
    if (ctx->intno == intno) {
      tkmc_complete_external_interrupt(ctx);
      break;
    }
  }
  EI(intsts);
}

void tkmc_init_intctl(void) {
  tkmc_active_interrupt_context = NULL;
  for (UINT intno = 0; intno < CFN_MAX_INTNO; ++intno) {
    s_handlers[intno] =
        (interrupt_handler){.handler = NULL, .kind = TKMC_INTERRUPT_KIND_NONE};
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
    s_handlers[intno] =
        (interrupt_handler){.handler = NULL, .kind = TKMC_INTERRUPT_KIND_NONE};
  } else if (pk_dint->intatr != TA_HLNG && pk_dint->intatr != TA_ASM) {
    ercd = E_RSATR;
  } else if (pk_dint->inthdr == NULL) {
    ercd = E_PAR;
  } else {
    s_handlers[intno].handler = pk_dint->inthdr;
    s_handlers[intno].kind = pk_dint->intatr == TA_ASM
                                 ? TKMC_INTERRUPT_KIND_ASM
                                 : TKMC_INTERRUPT_KIND_HLNG;
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
  // The tested QEMU 9.2.50 custom build does not raise a source that was
  // already pending when its enable bit is set, which the specification
  // requires of EnableInt() ("an interrupt is generated"): the PLIC looks for
  // a source to raise only when a line or the threshold changes. Writing the
  // threshold makes it look. The value it has is written, not 0: a handler
  // that has interrupts on runs with the threshold at its source priority (see
  // below), and a call from it must not let lower-priority sources in.
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

BOOL CheckInt(UINT intno) {
  return is_intno(intno) &&
         (in_w(PLIC_PENDING(intno)) & (1u << (intno % 32))) != 0;
}

void ClearInt(UINT intno) {
  if (!is_intno(intno)) {
    return;
  }

  UINT intsts;
  DI(intsts);

  UW saved_enable[(PLIC_SOURCE_COUNT + 31) / 32];
  for (UINT word = 0; word < (PLIC_SOURCE_COUNT + 31) / 32; ++word) {
    saved_enable[word] = in_w(PLIC_ENABLE(32 * word));
  }
  UW priority = in_w(PLIC_PRIORITY(intno));
  UW threshold = in_w(PLIC_THRESHOLD);

  // Isolate every source so the claim cannot consume another pending IRQ.
  for (UINT word = 0; word < (PLIC_SOURCE_COUNT + 31) / 32; ++word) {
    out_w(PLIC_ENABLE(32 * word), 0);
  }
  if (priority == 0) {
    out_w(PLIC_PRIORITY(intno), 1);
  }
  out_w(PLIC_ENABLE(intno), 1u << (intno % 32));

  // The tested QEMU build filters claims by threshold, so expose the target.
  out_w(PLIC_THRESHOLD, 0);
  UINT claimed = in_w(PLIC_CLAIM);
  if (claimed == intno) {
    out_w(PLIC_CLAIM, claimed);
  }

  if (priority == 0) {
    out_w(PLIC_PRIORITY(intno), priority);
  }
  for (UINT word = 0; word < (PLIC_SOURCE_COUNT + 31) / 32; ++word) {
    out_w(PLIC_ENABLE(32 * word), saved_enable[word]);
  }
  // Restoring the threshold also refreshes QEMU's pending-source delivery.
  out_w(PLIC_THRESHOLD, threshold);
  EI(intsts);
}

// The system's default handler, for a source with no handler: it is switched
// off. Left on, a device that keeps asserting it would be served again and
// again, and the system would do nothing else.
void tkmc_external_interrupt(void) {
  tkmc_interrupt_context ctx;
  tkmc_prepare_external_interrupt(&ctx, NULL);
  if (ctx.intno == 0) {
    return;
  }
  if ((ctx.kind & TKMC_INTERRUPT_KIND_MASK) == TKMC_INTERRUPT_KIND_ASM) {
    tkmc_invalid_ret_int();
  }
  if ((ctx.kind & TKMC_INTERRUPT_KIND_MASK) == TKMC_INTERRUPT_KIND_HLNG) {
    ((void (*)(UINT))ctx.handler)(ctx.intno);
  }
  tkmc_finish_external_interrupt(&ctx);
}

__attribute__((__noreturn__)) void tkmc_invalid_ret_int(void) {
  tkmc_fatal_context(TKMC_CONTEXT_RET_INT);
}

__attribute__((__noreturn__)) void tkmc_ret_int_from_sp(void *entry_sp) {
  UINT intsts;
  DI(intsts);
  tkmc_interrupt_context *ctx = tkmc_active_interrupt_context;
  if (!isDI(intsts) || ctx == NULL ||
      (ctx->kind & TKMC_INTERRUPT_KIND_MASK) != TKMC_INTERRUPT_KIND_ASM ||
      ctx->depth != tkmc_isr_depth || ctx->entry_sp != entry_sp ||
      ctx->intno == 0 || ctx->frame == NULL || current == NULL) {
    tkmc_invalid_ret_int();
  }

  void *resume_frame = ctx->frame;
  UW saved_mie_bits = 0;
  if (ctx->depth == 1) {
    saved_mie_bits = ((UW *)ctx->frame)[29] & ((1u << 3) | (1u << 7));
  }
  UINT depth = ctx->depth;
  tkmc_finish_external_interrupt(ctx);
  if (depth == 1 && in_w(CLINT_MSIP_ADDRESS) != 0) {
    out_w(CLINT_MSIP_ADDRESS, 0);
    resume_frame = (void *)schedule(resume_frame);
  }
  tkmc_isr_depth = tkmc_isr_depth - 1;
  tkmc_restore_external_interrupt_frame(resume_frame, saved_mie_bits);
}
