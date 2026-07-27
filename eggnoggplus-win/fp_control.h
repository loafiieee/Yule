#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Canonical floating-point controls for one deterministic native game tick.
 *
 * eggnoggplus.exe is a 32-bit MinGW/x87 binary whose CRT __fpreset executes
 * FNINIT, giving it the 0x037f x87 control word.  Preserve that native
 * extended-precision behavior while removing process/thread startup mode as a
 * rollback input.  SSE uses the architectural reset mode: masked exceptions,
 * round-to-nearest/even, gradual underflow, and DAZ disabled.
 */
#define FP_CONTROL_CANONICAL_X87 UINT16_C(0x037f)
#define FP_CONTROL_CANONICAL_MXCSR UINT32_C(0x00001f80)

typedef int (*FpControlTickFn)(void* user);

/*
 * Runs callback exactly once under the canonical controls and returns its
 * result.  The caller's exact x87 control word and MXCSR are restored even when
 * callback returns failure.  Callbacks must return normally; a non-local exit
 * would bypass ordinary C cleanup and is forbidden for simulation ticks.
 */
int fp_control_run_canonical_tick(FpControlTickFn callback, void* user);

uint16_t fp_control_get_x87_control(void);
uint32_t fp_control_get_mxcsr(void);

#ifdef __cplusplus
}
#endif
