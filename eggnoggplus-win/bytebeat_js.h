#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BYTEBEAT_JS_MAX_SOURCE (8u * 1024u * 1024u)
#define BYTEBEAT_JS_MEMORY_LIMIT (64u * 1024u * 1024u)
#define BYTEBEAT_JS_STACK_LIMIT (256u * 1024u)
#define BYTEBEAT_JS_COMPILE_MIN_BUDGET_MS 100u
#define BYTEBEAT_JS_COMPILE_MAX_BUDGET_MS 5000u
#define BYTEBEAT_JS_CALLBACK_BUDGET_MS 50u
#define BYTEBEAT_JS_CALLBACK_MAX_BUDGET_MS 250u
#define BYTEBEAT_JS_BATCH_FRAMES 4096u

typedef enum BytebeatJsMode {
    BYTEBEAT_JS_MODE_U8 = 0,
    BYTEBEAT_JS_MODE_S8 = 1,
    BYTEBEAT_JS_MODE_FLOAT = 2,
    BYTEBEAT_JS_MODE_FUNC = 3
} BytebeatJsMode;

typedef struct BytebeatJsRuntime BytebeatJsRuntime;

/*
 * Creates an isolated QuickJS runtime with no std/os modules. The source is
 * compiled with the same function shape and shortened Math names used by the
 * Dollchan Bytebeat Composer. A bounded t=0 validation call is performed so
 * initialization and diagnostics match the reference player.
 */
BytebeatJsRuntime* bytebeat_js_create(const char* source, size_t source_length,
                                      BytebeatJsMode mode,
                                      uint32_t sample_rate, double volume,
                                      char* error, size_t error_size);

void bytebeat_js_destroy(BytebeatJsRuntime* runtime);

/*
 * Bounds one native audio callback's JavaScript work. The interrupt handler
 * terminates runaway loops after budget_ms; zero selects the documented
 * callback budget.
 */
void bytebeat_js_begin_callback(BytebeatJsRuntime* runtime,
                                uint32_t budget_ms);
void bytebeat_js_end_callback(BytebeatJsRuntime* runtime);

/* Budgets scale with declared work but retain strict upper bounds. */
uint32_t bytebeat_js_compile_budget_ms(size_t source_length);
uint32_t bytebeat_js_render_budget_ms(uint32_t frame_count,
                                      uint32_t sample_rate);

/*
 * Produces one stereo PCM16 source frame. Mono return values are duplicated;
 * two-element JavaScript arrays are stereo. Non-numeric/NaN channel values
 * hold the previous channel value, matching Dollchan's AudioWorklet.
 */
int bytebeat_js_sample(BytebeatJsRuntime* runtime, uint64_t sample_index,
                       int16_t* out_left, int16_t* out_right);

/*
 * Evaluates a contiguous source block in one JavaScript call. This mirrors
 * Dollchan's AudioWorklet loop and avoids one native/VM transition per sample.
 * frame_count must be 1..BYTEBEAT_JS_BATCH_FRAMES and output contains
 * interleaved stereo PCM16.
 */
int bytebeat_js_render(BytebeatJsRuntime* runtime, uint64_t first_sample,
                       uint32_t frame_count, int16_t* output);

int bytebeat_js_failed(const BytebeatJsRuntime* runtime);
void bytebeat_js_error(const BytebeatJsRuntime* runtime,
                       char* out, size_t out_size);

#ifdef __cplusplus
}
#endif
