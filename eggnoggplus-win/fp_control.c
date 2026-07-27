#include "fp_control.h"

#if !defined(__i386__) && !defined(_M_IX86)
#error "EGGNOGG+ native simulation requires the 32-bit x86 floating-point ABI"
#endif

static void fp_control_install_canonical(void) {
    const uint16_t x87 = FP_CONTROL_CANONICAL_X87;
    const uint32_t mxcsr = FP_CONTROL_CANONICAL_MXCSR;

    /* Do not use FNINIT here: it would destroy a caller's x87 stack and sticky
     * status.  Only the deterministic control modes belong to this guard. */
    __asm__ volatile("fldcw %0" : : "m"(x87) : "memory");
    __asm__ volatile("ldmxcsr %0" : : "m"(mxcsr) : "memory");
}

uint16_t fp_control_get_x87_control(void) {
    uint16_t value;
    __asm__ volatile("fnstcw %0" : "=m"(value) : : "memory");
    return value;
}

uint32_t fp_control_get_mxcsr(void) {
    uint32_t value;
    __asm__ volatile("stmxcsr %0" : "=m"(value) : : "memory");
    return value;
}

int fp_control_run_canonical_tick(FpControlTickFn callback, void* user) {
    uint16_t caller_x87;
    uint32_t caller_mxcsr;
    int result;

    if (!callback) return 0;

    caller_x87 = fp_control_get_x87_control();
    caller_mxcsr = fp_control_get_mxcsr();
    fp_control_install_canonical();
    result = callback(user);
    __asm__ volatile("ldmxcsr %0" : : "m"(caller_mxcsr) : "memory");
    __asm__ volatile("fldcw %0" : : "m"(caller_x87) : "memory");
    return result;
}
