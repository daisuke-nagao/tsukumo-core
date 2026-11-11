/* SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#include <stddef.h>

#if defined(PUBLIC_DECLARATIONS_UMBRELLA)
#include <tk/tkernel.h>

_Static_assert(TK_SUPPORT_USEC == FALSE, "microsecond profile changed");
_Static_assert(TK_SUPPORT_UTC == TRUE, "UNIX time profile changed");
_Static_assert(TK_SUPPORT_TRONTIME == FALSE, "TRON time profile changed");
#if defined(TKERNEL_CHECK_CONST)
_Static_assert(_Generic(&tk_cre_tsk, ID (*)(const T_CTSK *): 1, default: 0),
               "CONST must make the public task definition parameter const");
_Static_assert(_Generic(&tk_set_utc, ER (*)(const SYSTIM *): 1, default: 0),
               "CONST must make the tk_set_utc() parameter const");
#else
_Static_assert(_Generic(&tk_cre_tsk, ID (*)(T_CTSK *): 1, default: 0),
               "disabled CONST must leave the public parameter mutable");
_Static_assert(_Generic(&tk_set_utc, ER (*)(SYSTIM *): 1, default: 0),
               "disabled CONST must leave the tk_set_utc() parameter mutable");
#endif
_Static_assert(_Generic(&tk_get_utc, ER (*)(SYSTIM *): 1, default: 0),
               "tk_get_utc() must take a mutable SYSTIM pointer");
#else
#include <tk/typedef.h>
#endif

_Static_assert(sizeof(W) == 4 && (W)-1 < 0, "W must be signed 32-bit");
_Static_assert(sizeof(UW) == 4 && (UW)-1 > 0, "UW must be unsigned 32-bit");
_Static_assert(sizeof(struct systim) == 8, "systim must be eight bytes");
_Static_assert(offsetof(struct systim, hi) == 0, "systim.hi offset changed");
_Static_assert(offsetof(struct systim, lo) == 4, "systim.lo offset changed");
_Static_assert(_Generic(((struct systim *)0)->hi, W: 1, default: 0),
               "systim.hi must have type W");
_Static_assert(_Generic(((struct systim *)0)->lo, UW: 1, default: 0),
               "systim.lo must have type UW");
_Static_assert(_Generic((SYSTIM){0}, struct systim: 1, default: 0),
               "SYSTIM must alias struct systim");

LOCAL int local_variable = 7;
LOCAL int local_function(void) { return local_variable; }

IMPORT int public_variable;
IMPORT int public_function(void);

#if defined(__riscv)
void _start(void) { (void)public_function(); }
#else
int main(void) {
  return public_variable == 21 && public_function() == 26 &&
                 local_function() == 7
             ? 0
             : 1;
}
#endif
