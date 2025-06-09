#pragma once

/* The functions exported through this header are intended for the arch-specific
 * backends to call back into the generic FPSpy core.
 *
 * For example, the architecture-specific backends need access to the common way
 * FPSpy aborts operation when some inconsistent state is encountered.
 */

#include <signal.h>

void fp_trap_handler(siginfo_t *si, ucontext_t *uc);
void brk_trap_handler(siginfo_t *si, ucontext_t *uc);
void abort_operation(char *reason);
