#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../fp_control.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "fp_control_test:%d: CHECK failed: %s\n", \
                __LINE__, #expr); \
        return 0; \
    } \
} while (0)

typedef struct TickProbe {
    int calls;
    int return_value;
    int mutate_controls;
    uint16_t seen_x87;
    uint32_t seen_mxcsr;
} TickProbe;

static void install_hostile(uint16_t x87, uint32_t mxcsr) {
    __asm__ volatile("fldcw %0" : : "m"(x87));
    __asm__ volatile("ldmxcsr %0" : : "m"(mxcsr));
}

static int probe_tick(void* user) {
    TickProbe* probe = (TickProbe*)user;
    probe->calls++;
    probe->seen_x87 = fp_control_get_x87_control();
    probe->seen_mxcsr = fp_control_get_mxcsr();
    if (probe->mutate_controls) {
        const uint16_t changed_x87 = UINT16_C(0x0f7f);
        const uint32_t changed_mxcsr = UINT32_C(0x0000ffc0);
        __asm__ volatile("fldcw %0" : : "m"(changed_x87));
        __asm__ volatile("ldmxcsr %0" : : "m"(changed_mxcsr));
    }
    return probe->return_value;
}

static int nested_tick(void* user) {
    TickProbe* outer = (TickProbe*)user;
    TickProbe inner;
    int result;

    memset(&inner, 0, sizeof(inner));
    inner.return_value = 41;
    outer->calls++;
    outer->seen_x87 = fp_control_get_x87_control();
    outer->seen_mxcsr = fp_control_get_mxcsr();
    result = fp_control_run_canonical_tick(probe_tick, &inner);
    CHECK(result == 41);
    CHECK(inner.calls == 1);
    CHECK(inner.seen_x87 == FP_CONTROL_CANONICAL_X87);
    CHECK(inner.seen_mxcsr == FP_CONTROL_CANONICAL_MXCSR);
    CHECK(fp_control_get_x87_control() == FP_CONTROL_CANONICAL_X87);
    CHECK(fp_control_get_mxcsr() == FP_CONTROL_CANONICAL_MXCSR);
    return outer->return_value;
}

static int run_hostile_case(uint16_t hostile_x87,
                            uint32_t hostile_mxcsr,
                            int callback_result,
                            int mutate_controls) {
    uint16_t original_x87;
    uint16_t before_x87;
    uint32_t original_mxcsr;
    uint32_t before_mxcsr;
    TickProbe probe;
    int result;

    original_x87 = fp_control_get_x87_control();
    original_mxcsr = fp_control_get_mxcsr();
    install_hostile(hostile_x87, hostile_mxcsr);
    before_x87 = fp_control_get_x87_control();
    before_mxcsr = fp_control_get_mxcsr();

    memset(&probe, 0, sizeof(probe));
    probe.return_value = callback_result;
    probe.mutate_controls = mutate_controls;
    result = fp_control_run_canonical_tick(probe_tick, &probe);

    CHECK(result == callback_result);
    CHECK(probe.calls == 1);
    CHECK(probe.seen_x87 == FP_CONTROL_CANONICAL_X87);
    CHECK(probe.seen_mxcsr == FP_CONTROL_CANONICAL_MXCSR);
    CHECK(fp_control_get_x87_control() == before_x87);
    CHECK(fp_control_get_mxcsr() == before_mxcsr);

    install_hostile(original_x87, original_mxcsr);
    return 1;
}

static int test_hostile_modes_and_restoration(void) {
    /* 24-bit/down, 53-bit/up, and 64-bit/chop; all enable FTZ+DAZ in the
     * hostile SSE state.  The callback must never observe any of them. */
    CHECK(run_hostile_case(UINT16_C(0x047f), UINT32_C(0x0000bfc0), 17, 0));
    CHECK(run_hostile_case(UINT16_C(0x0a7f), UINT32_C(0x0000dfc0), 0, 1));
    CHECK(run_hostile_case(UINT16_C(0x0f7f), UINT32_C(0x0000ffc0), -9, 1));
    return 1;
}

static int test_nested_scope(void) {
    uint16_t original_x87;
    uint16_t before_x87;
    uint32_t original_mxcsr;
    uint32_t before_mxcsr;
    TickProbe outer;
    int result;

    original_x87 = fp_control_get_x87_control();
    original_mxcsr = fp_control_get_mxcsr();
    install_hostile(UINT16_C(0x047f), UINT32_C(0x0000bfc0));
    before_x87 = fp_control_get_x87_control();
    before_mxcsr = fp_control_get_mxcsr();
    memset(&outer, 0, sizeof(outer));
    outer.return_value = 73;

    result = fp_control_run_canonical_tick(nested_tick, &outer);
    CHECK(result == 73);
    CHECK(outer.calls == 1);
    CHECK(outer.seen_x87 == FP_CONTROL_CANONICAL_X87);
    CHECK(outer.seen_mxcsr == FP_CONTROL_CANONICAL_MXCSR);
    CHECK(fp_control_get_x87_control() == before_x87);
    CHECK(fp_control_get_mxcsr() == before_mxcsr);
    install_hostile(original_x87, original_mxcsr);
    return 1;
}

static int subnormal_tick(void* user) {
    volatile float smallest_normal = 0x1p-126f;
    volatile float half = 0.5f;
    volatile float zero = 0.0f;
    float identity;
    int* ok = (int*)user;
    /* Force the underflow/denormal probes through SSE rather than this
     * 32-bit target's default x87 scalar code generation. */
    __asm__ volatile(
        "movss %1, %%xmm0\n\t"
        "mulss %2, %%xmm0\n\t"
        "addss %3, %%xmm0\n\t"
        "movss %%xmm0, %0"
        : "=m"(identity)
        : "m"(smallest_normal), "m"(half), "m"(zero)
        : "xmm0");
    *ok = identity != 0.0f ? 1 : 0;
    return *ok;
}

static int test_gradual_underflow(void) {
    uint16_t original_x87;
    uint32_t original_mxcsr;
    int ok = 0;
    int result;
    original_x87 = fp_control_get_x87_control();
    original_mxcsr = fp_control_get_mxcsr();
    install_hostile(UINT16_C(0x0f7f), UINT32_C(0x0000ffc0));
    result = fp_control_run_canonical_tick(subnormal_tick, &ok);
    install_hostile(original_x87, original_mxcsr);
    CHECK(result == 1);
    CHECK(ok == 1);
    return 1;
}

int main(void) {
    if (!test_hostile_modes_and_restoration() ||
        !test_nested_scope() ||
        !test_gradual_underflow()) {
        return 1;
    }
    puts("fp control tests: ALL OK");
    return 0;
}
