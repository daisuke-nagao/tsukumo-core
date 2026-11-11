/* SPDX-FileCopyrightText: 2026 Daisuke Nagao
 *
 * SPDX-License-Identifier: MIT
 */

#if defined(PUBLIC_DECLARATIONS_UMBRELLA)
#include <tk/tkernel.h>
#else
#include <tk/typedef.h>
#endif

LOCAL int local_variable = 5;
LOCAL int local_function(void) { return local_variable; }

EXPORT int public_variable = 21;

EXPORT int public_function(void) { return public_variable + local_function(); }
