#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "bytebeat_js.h"
#include "bytebeat_chakra.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "third_party/quickjs-ng/quickjs.h"

struct BytebeatJsRuntime {
    BytebeatChakraRuntime* chakra;
    JSRuntime* runtime;
    JSContext* context;
    JSValue function;
    JSValue batch_function;
    JSValue batch_output;
    JSValue global;
    double* batch_values;
    size_t batch_value_count;
    BytebeatJsMode mode;
    uint32_t sample_rate;
    double volume;
    double held[2];
    ULONGLONG deadline_ms;
    volatile LONG interrupt_enabled;
    volatile LONG failed;
    char error[256];
};

static void bbjs_copy_error(char* out, size_t out_size, const char* message) {
    if (!out || out_size == 0u) return;
    snprintf(out, out_size, "%s", message && message[0]
        ? message : "unknown JavaScript error");
}

static void bbjs_set_error(BytebeatJsRuntime* runtime, const char* message) {
    if (!runtime) return;
    bbjs_copy_error(runtime->error, sizeof(runtime->error), message);
    InterlockedExchange(&runtime->failed, 1);
}

static int bbjs_interrupt(JSRuntime* js_runtime, void* opaque) {
    BytebeatJsRuntime* runtime = (BytebeatJsRuntime*)opaque;
    (void)js_runtime;
    if (!runtime) return 1;
    if (InterlockedCompareExchange(&runtime->failed, 0, 0) != 0) return 1;
    if (InterlockedCompareExchange(&runtime->interrupt_enabled, 0, 0) == 0) {
        return 0;
    }
    return GetTickCount64() > runtime->deadline_ms;
}

static void bbjs_exception(BytebeatJsRuntime* runtime, const char* prefix) {
    JSValue exception;
    JSValue stack;
    const char* message = NULL;
    const char* stack_text = NULL;
    char combined[256];
    if (!runtime || !runtime->context) return;
    exception = JS_GetException(runtime->context);
    message = JS_ToCString(runtime->context, exception);
    stack = JS_GetPropertyStr(runtime->context, exception, "stack");
    if (!JS_IsException(stack) && !JS_IsUndefined(stack)) {
        stack_text = JS_ToCString(runtime->context, stack);
    }
    if (stack_text && stack_text[0]) {
        snprintf(combined, sizeof(combined), "%s: %.180s (%.48s)",
                 prefix ? prefix : "JavaScript error",
                 message ? message : "exception", stack_text);
    } else {
        snprintf(combined, sizeof(combined), "%s: %.210s",
                 prefix ? prefix : "JavaScript error",
                 message ? message : "exception");
    }
    bbjs_set_error(runtime, combined);
    if (stack_text) JS_FreeCString(runtime->context, stack_text);
    JS_FreeValue(runtime->context, stack);
    if (message) JS_FreeCString(runtime->context, message);
    JS_FreeValue(runtime->context, exception);
}

static int bbjs_eval_setup(BytebeatJsRuntime* runtime) {
    static const char setup[] =
        "for(const k of Object.getOwnPropertyNames(Math)){"
        "Object.defineProperty(globalThis,k,{value:Math[k],"
        "writable:false,configurable:false});}"
        "Object.defineProperty(globalThis,'int',{value:Math.floor,"
        "writable:false,configurable:false});"
        "Object.defineProperty(globalThis,'window',{value:globalThis,"
        "writable:false,configurable:false});";
    JSValue result = JS_Eval(runtime->context, setup, sizeof(setup) - 1u,
                             "dollchan-prelude.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result)) {
        bbjs_exception(runtime, "Dollchan prelude failed");
        return 0;
    }
    JS_FreeValue(runtime->context, result);
    return 1;
}

static int bbjs_create_batch_renderer(BytebeatJsRuntime* runtime) {
    static const char batch_source[] =
        "(function(fn,out,start,count,rate,isFunc){"
        "let j=0;"
        "for(let i=0;i<count;i++){"
        "let v=isFunc?fn((start+i)/rate,rate):fn(start+i),l,r;"
        "if(Array.isArray(v)){l=v[0];r=v[1]}else{l=v;r=v}"
        "try{l=Number(l)}catch(_){l=NaN}"
        "try{r=Number(r)}catch(_){r=NaN}"
        "out[j++]=l;out[j++]=r"
        "}"
        "return count"
        "})";
    JSValue length_argument;
    JSValue array_buffer;
    uint8_t* bytes;
    size_t byte_offset = 0u;
    size_t byte_length = 0u;
    size_t bytes_per_element = 0u;
    size_t array_buffer_size = 0u;

    runtime->batch_function = JS_Eval(
        runtime->context, batch_source, sizeof(batch_source) - 1u,
        "dollchan-block-renderer.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(runtime->batch_function)) {
        bbjs_exception(runtime, "Dollchan block renderer failed");
        runtime->batch_function = JS_UNDEFINED;
        return 0;
    }
    if (!JS_IsFunction(runtime->context, runtime->batch_function)) {
        JS_FreeValue(runtime->context, runtime->batch_function);
        runtime->batch_function = JS_UNDEFINED;
        bbjs_set_error(runtime,
                       "Dollchan block renderer did not compile to a function");
        return 0;
    }

    length_argument = JS_NewUint32(
        runtime->context, BYTEBEAT_JS_BATCH_FRAMES * 2u);
    runtime->batch_output = JS_NewTypedArray(
        runtime->context, 1, &length_argument, JS_TYPED_ARRAY_FLOAT64);
    JS_FreeValue(runtime->context, length_argument);
    if (JS_IsException(runtime->batch_output)) {
        bbjs_exception(runtime, "Dollchan output buffer allocation failed");
        runtime->batch_output = JS_UNDEFINED;
        return 0;
    }
    array_buffer = JS_GetTypedArrayBuffer(
        runtime->context, runtime->batch_output, &byte_offset,
        &byte_length, &bytes_per_element);
    if (JS_IsException(array_buffer)) {
        bbjs_exception(runtime, "Dollchan output buffer lookup failed");
        return 0;
    }
    bytes = JS_GetArrayBuffer(runtime->context, &array_buffer_size,
                              array_buffer);
    if (!bytes || bytes_per_element != sizeof(double) ||
        byte_offset > array_buffer_size ||
        byte_length > array_buffer_size - byte_offset ||
        byte_length < BYTEBEAT_JS_BATCH_FRAMES * 2u * sizeof(double)) {
        JS_FreeValue(runtime->context, array_buffer);
        bbjs_set_error(runtime, "Dollchan output buffer has an invalid layout");
        return 0;
    }
    runtime->batch_values = (double*)(void*)(bytes + byte_offset);
    runtime->batch_value_count = byte_length / sizeof(double);
    JS_FreeValue(runtime->context, array_buffer);
    return 1;
}

static char* bbjs_build_wrapper(const char* source, size_t source_length,
                                BytebeatJsMode mode, size_t* out_length) {
    const char* prefix = mode == BYTEBEAT_JS_MODE_FUNC
        ? "(function(){\n" : "(function(t){return 0,\n";
    const char* suffix = mode == BYTEBEAT_JS_MODE_FUNC ? "\n})()" : "\n})";
    size_t prefix_length = strlen(prefix);
    size_t suffix_length = strlen(suffix);
    size_t total;
    char* wrapper;
    if (source_length > BYTEBEAT_JS_MAX_SOURCE ||
        prefix_length > SIZE_MAX - source_length ||
        prefix_length + source_length > SIZE_MAX - suffix_length) {
        return NULL;
    }
    total = prefix_length + source_length + suffix_length;
    wrapper = (char*)malloc(total + 1u);
    if (!wrapper) return NULL;
    memcpy(wrapper, prefix, prefix_length);
    memcpy(wrapper + prefix_length, source, source_length);
    memcpy(wrapper + prefix_length + source_length, suffix, suffix_length);
    wrapper[total] = '\0';
    if (out_length) *out_length = total;
    return wrapper;
}

static int bbjs_call_raw(BytebeatJsRuntime* runtime, uint64_t sample_index,
                         JSValue* out_value) {
    JSValue arguments[2];
    int argument_count;
    if (!runtime || !out_value || !runtime->context ||
        JS_IsUndefined(runtime->function)) {
        return 0;
    }
    if (runtime->mode == BYTEBEAT_JS_MODE_FUNC) {
        arguments[0] = JS_NewFloat64(
            runtime->context,
            (double)sample_index / (double)runtime->sample_rate);
        arguments[1] = JS_NewFloat64(runtime->context,
                                     (double)runtime->sample_rate);
        argument_count = 2;
    } else {
        arguments[0] = JS_NewFloat64(runtime->context,
                                     (double)sample_index);
        arguments[1] = JS_UNDEFINED;
        argument_count = 1;
    }
    *out_value = JS_Call(runtime->context, runtime->function,
                         runtime->global, argument_count, arguments);
    JS_FreeValue(runtime->context, arguments[0]);
    if (argument_count == 2) JS_FreeValue(runtime->context, arguments[1]);
    if (JS_IsException(*out_value)) {
        bbjs_exception(runtime, "runtime error");
        return 0;
    }
    return 1;
}

static uint32_t bbjs_to_uint32(double value) {
    double truncated;
    double wrapped;
    if (!isfinite(value) || value == 0.0) return 0u;
    truncated = value < 0.0 ? ceil(value) : floor(value);
    wrapped = fmod(truncated, 4294967296.0);
    if (wrapped < 0.0) wrapped += 4294967296.0;
    return (uint32_t)wrapped;
}

static double bbjs_normalize(BytebeatJsRuntime* runtime, double value) {
    if (runtime->mode == BYTEBEAT_JS_MODE_FLOAT ||
        runtime->mode == BYTEBEAT_JS_MODE_FUNC) {
        if (value < -1.0) return -1.0;
        if (value > 1.0) return 1.0;
        return value;
    }
    if (runtime->mode == BYTEBEAT_JS_MODE_S8) {
        value += 128.0;
    }
    return (double)(bbjs_to_uint32(value) & 0xffu) / 127.5 - 1.0;
}

static int16_t bbjs_pcm16(double normalized, double volume) {
    double sample = normalized * volume * 32767.0;
    if (sample < -32768.0) sample = -32768.0;
    if (sample > 32767.0) sample = 32767.0;
    return (int16_t)(sample < 0.0
        ? ceil(sample - 0.5) : floor(sample + 0.5));
}

static void bbjs_process_channel(BytebeatJsRuntime* runtime,
                                 JSValueConst value, int channel) {
    double number;
    if (JS_ToFloat64(runtime->context, &number, value) == 0 &&
        !isnan(number)) {
        runtime->held[channel] = bbjs_normalize(runtime, number);
    } else {
        /*
         * JS_ToFloat64 may leave a coercion exception. Dollchan catches that
         * conversion and treats it like NaN, so clear the exception here.
         */
        if (JS_HasException(runtime->context)) {
            JSValue exception = JS_GetException(runtime->context);
            JS_FreeValue(runtime->context, exception);
        }
    }
}

uint32_t bytebeat_js_compile_budget_ms(size_t source_length) {
    uint64_t chunks =
        ((uint64_t)source_length + UINT64_C(4095)) / UINT64_C(4096);
    uint64_t budget =
        (uint64_t)BYTEBEAT_JS_COMPILE_MIN_BUDGET_MS + chunks * 25u;
    if (budget > BYTEBEAT_JS_COMPILE_MAX_BUDGET_MS) {
        budget = BYTEBEAT_JS_COMPILE_MAX_BUDGET_MS;
    }
    return (uint32_t)budget;
}

uint32_t bytebeat_js_render_budget_ms(uint32_t frame_count,
                                      uint32_t sample_rate) {
    uint64_t playback_ms;
    uint64_t budget;
    if (frame_count == 0u || sample_rate == 0u) {
        return BYTEBEAT_JS_CALLBACK_BUDGET_MS;
    }
    playback_ms =
        ((uint64_t)frame_count * UINT64_C(1000) + sample_rate - 1u) /
        sample_rate;
    budget = playback_ms * 2u + 25u;
    if (budget < BYTEBEAT_JS_CALLBACK_BUDGET_MS) {
        budget = BYTEBEAT_JS_CALLBACK_BUDGET_MS;
    }
    if (budget > BYTEBEAT_JS_CALLBACK_MAX_BUDGET_MS) {
        budget = BYTEBEAT_JS_CALLBACK_MAX_BUDGET_MS;
    }
    return (uint32_t)budget;
}

BytebeatJsRuntime* bytebeat_js_create(const char* source, size_t source_length,
                                      BytebeatJsMode mode,
                                      uint32_t sample_rate, double volume,
                                      char* error, size_t error_size) {
    BytebeatJsRuntime* runtime;
    char* wrapper;
    size_t wrapper_length = 0u;
    JSValue compiled;
    JSValue validation;
    if (!source || source_length == 0u ||
        source_length > BYTEBEAT_JS_MAX_SOURCE) {
        bbjs_copy_error(
            error, error_size,
            "JavaScript source must be between 1 byte and 8 MiB");
        return NULL;
    }
    if (mode < BYTEBEAT_JS_MODE_U8 || mode > BYTEBEAT_JS_MODE_FUNC) {
        bbjs_copy_error(error, error_size, "invalid Dollchan playback mode");
        return NULL;
    }
    if (sample_rate < 4000u || sample_rate > 48000u) {
        bbjs_copy_error(error, error_size,
                        "sample_rate must be between 4000 and 48000");
        return NULL;
    }
    if (!isfinite(volume) || volume < 0.0 || volume > 1.0) {
        bbjs_copy_error(error, error_size,
                        "volume must be finite and between 0 and 1");
        return NULL;
    }
    if (memchr(source, '\0', source_length) != NULL) {
        bbjs_copy_error(error, error_size,
                        "JavaScript source contains a NUL byte");
        return NULL;
    }
    {
        BytebeatChakraRuntime* chakra = bytebeat_chakra_create(
            source, source_length, (int)mode, sample_rate, volume,
            error, error_size);
        if (chakra) {
            runtime = (BytebeatJsRuntime*)calloc(1u, sizeof(*runtime));
            if (!runtime) {
                bytebeat_chakra_destroy(chakra);
                bbjs_copy_error(
                    error, error_size,
                    "out of memory retaining system JavaScript runtime");
                return NULL;
            }
            runtime->chakra = chakra;
            return runtime;
        }
    }
    runtime = (BytebeatJsRuntime*)calloc(1u, sizeof(*runtime));
    if (!runtime) {
        bbjs_copy_error(error, error_size,
                        "out of memory creating JavaScript runtime");
        return NULL;
    }
    runtime->function = JS_UNDEFINED;
    runtime->batch_function = JS_UNDEFINED;
    runtime->batch_output = JS_UNDEFINED;
    runtime->global = JS_UNDEFINED;
    runtime->mode = mode;
    runtime->sample_rate = sample_rate;
    runtime->volume = volume;
    runtime->runtime = JS_NewRuntime();
    if (!runtime->runtime) {
        bbjs_copy_error(error, error_size,
                        "could not create JavaScript runtime");
        free(runtime);
        return NULL;
    }
    JS_SetRuntimeOpaque(runtime->runtime, runtime);
    JS_SetMemoryLimit(runtime->runtime, BYTEBEAT_JS_MEMORY_LIMIT);
    JS_SetMaxStackSize(runtime->runtime, BYTEBEAT_JS_STACK_LIMIT);
    JS_SetInterruptHandler(runtime->runtime, bbjs_interrupt, runtime);
    runtime->context = JS_NewContext(runtime->runtime);
    if (!runtime->context) {
        bbjs_copy_error(error, error_size,
                        "could not create JavaScript context");
        bytebeat_js_destroy(runtime);
        return NULL;
    }
    runtime->global = JS_GetGlobalObject(runtime->context);
    bytebeat_js_begin_callback(
        runtime, bytebeat_js_compile_budget_ms(source_length));
    if (!bbjs_eval_setup(runtime)) {
        bbjs_copy_error(error, error_size, runtime->error);
        bytebeat_js_end_callback(runtime);
        bytebeat_js_destroy(runtime);
        return NULL;
    }
    if (!bbjs_create_batch_renderer(runtime)) {
        bbjs_copy_error(error, error_size, runtime->error);
        bytebeat_js_end_callback(runtime);
        bytebeat_js_destroy(runtime);
        return NULL;
    }
    wrapper = bbjs_build_wrapper(source, source_length, mode,
                                 &wrapper_length);
    if (!wrapper) {
        bbjs_copy_error(error, error_size,
                        "out of memory wrapping JavaScript source");
        bytebeat_js_end_callback(runtime);
        bytebeat_js_destroy(runtime);
        return NULL;
    }
    compiled = JS_Eval(runtime->context, wrapper, wrapper_length,
                       "playlist-bytebeat.js", JS_EVAL_TYPE_GLOBAL);
    free(wrapper);
    if (JS_IsException(compiled)) {
        bbjs_exception(runtime, "compile error");
        bbjs_copy_error(error, error_size, runtime->error);
        bytebeat_js_end_callback(runtime);
        bytebeat_js_destroy(runtime);
        return NULL;
    }
    if (!JS_IsFunction(runtime->context, compiled)) {
        JS_FreeValue(runtime->context, compiled);
        bbjs_copy_error(error, error_size,
                        mode == BYTEBEAT_JS_MODE_FUNC
                            ? "funcbeat source did not return a function"
                            : "JavaScript source did not compile to a function");
        bytebeat_js_end_callback(runtime);
        bytebeat_js_destroy(runtime);
        return NULL;
    }
    runtime->function = compiled;
    if (!bbjs_call_raw(runtime, 0u, &validation)) {
        bbjs_copy_error(error, error_size, runtime->error);
        bytebeat_js_end_callback(runtime);
        bytebeat_js_destroy(runtime);
        return NULL;
    }
    JS_FreeValue(runtime->context, validation);
    bytebeat_js_end_callback(runtime);
    runtime->held[0] = 0.0;
    runtime->held[1] = 0.0;
    runtime->failed = 0;
    runtime->error[0] = '\0';
    return runtime;
}

void bytebeat_js_destroy(BytebeatJsRuntime* runtime) {
    if (!runtime) return;
    if (runtime->chakra) {
        bytebeat_chakra_destroy(runtime->chakra);
        free(runtime);
        return;
    }
    InterlockedExchange(&runtime->failed, 1);
    if (runtime->context) {
        JS_FreeValue(runtime->context, runtime->function);
        JS_FreeValue(runtime->context, runtime->batch_function);
        JS_FreeValue(runtime->context, runtime->batch_output);
        JS_FreeValue(runtime->context, runtime->global);
        JS_FreeContext(runtime->context);
    }
    if (runtime->runtime) JS_FreeRuntime(runtime->runtime);
    free(runtime);
}

void bytebeat_js_begin_callback(BytebeatJsRuntime* runtime,
                                uint32_t budget_ms) {
    if (!runtime) return;
    if (runtime->chakra) {
        bytebeat_chakra_begin(runtime->chakra, budget_ms);
        return;
    }
    if (budget_ms == 0u) budget_ms = BYTEBEAT_JS_CALLBACK_BUDGET_MS;
    runtime->deadline_ms = GetTickCount64() + (ULONGLONG)budget_ms;
    InterlockedExchange(&runtime->interrupt_enabled, 1);
}

void bytebeat_js_end_callback(BytebeatJsRuntime* runtime) {
    if (!runtime) return;
    if (runtime->chakra) {
        bytebeat_chakra_end(runtime->chakra);
        return;
    }
    InterlockedExchange(&runtime->interrupt_enabled, 0);
    runtime->deadline_ms = 0u;
}

int bytebeat_js_sample(BytebeatJsRuntime* runtime, uint64_t sample_index,
                       int16_t* out_left, int16_t* out_right) {
    JSValue result;
    if (runtime && runtime->chakra) {
        return bytebeat_chakra_sample(
            runtime->chakra, sample_index, out_left, out_right);
    }
    if (!runtime || !out_left || !out_right ||
        InterlockedCompareExchange(&runtime->failed, 0, 0) != 0) {
        return 0;
    }
    if (!bbjs_call_raw(runtime, sample_index, &result)) return 0;
    if (JS_IsArray(result)) {
        JSValue left = JS_GetPropertyUint32(runtime->context, result, 0u);
        JSValue right = JS_GetPropertyUint32(runtime->context, result, 1u);
        if (!JS_IsException(left)) bbjs_process_channel(runtime, left, 0);
        if (!JS_IsException(right)) bbjs_process_channel(runtime, right, 1);
        JS_FreeValue(runtime->context, left);
        JS_FreeValue(runtime->context, right);
    } else {
        bbjs_process_channel(runtime, result, 0);
        runtime->held[1] = runtime->held[0];
    }
    JS_FreeValue(runtime->context, result);
    *out_left = bbjs_pcm16(runtime->held[0], runtime->volume);
    *out_right = bbjs_pcm16(runtime->held[1], runtime->volume);
    return 1;
}

int bytebeat_js_render(BytebeatJsRuntime* runtime, uint64_t first_sample,
                       uint32_t frame_count, int16_t* output) {
    JSValue arguments[6];
    JSValue result;
    uint32_t frame;
    if (runtime && runtime->chakra) {
        return bytebeat_chakra_render(
            runtime->chakra, first_sample, frame_count, output);
    }
    if (!runtime || !output || frame_count == 0u ||
        frame_count > BYTEBEAT_JS_BATCH_FRAMES ||
        !runtime->context || !runtime->batch_values ||
        runtime->batch_value_count < (size_t)frame_count * 2u ||
        JS_IsUndefined(runtime->batch_function) ||
        InterlockedCompareExchange(&runtime->failed, 0, 0) != 0) {
        return 0;
    }

    arguments[0] = JS_DupValue(runtime->context, runtime->function);
    arguments[1] = JS_DupValue(runtime->context, runtime->batch_output);
    arguments[2] = JS_NewFloat64(runtime->context, (double)first_sample);
    arguments[3] = JS_NewUint32(runtime->context, frame_count);
    arguments[4] = JS_NewUint32(runtime->context, runtime->sample_rate);
    arguments[5] = JS_NewBool(runtime->context,
                              runtime->mode == BYTEBEAT_JS_MODE_FUNC);
    result = JS_Call(runtime->context, runtime->batch_function,
                     runtime->global, 6, arguments);
    for (frame = 0u; frame < 6u; frame++) {
        JS_FreeValue(runtime->context, arguments[frame]);
    }
    if (JS_IsException(result)) {
        bbjs_exception(runtime, "runtime error");
        return 0;
    }
    JS_FreeValue(runtime->context, result);

    for (frame = 0u; frame < frame_count; frame++) {
        double left = runtime->batch_values[(size_t)frame * 2u];
        double right = runtime->batch_values[(size_t)frame * 2u + 1u];
        if (!isnan(left)) {
            runtime->held[0] = bbjs_normalize(runtime, left);
        }
        if (!isnan(right)) {
            runtime->held[1] = bbjs_normalize(runtime, right);
        }
        output[(size_t)frame * 2u] =
            bbjs_pcm16(runtime->held[0], runtime->volume);
        output[(size_t)frame * 2u + 1u] =
            bbjs_pcm16(runtime->held[1], runtime->volume);
    }
    return 1;
}

int bytebeat_js_failed(const BytebeatJsRuntime* runtime) {
    if (!runtime) return 1;
    if (runtime->chakra) {
        return bytebeat_chakra_failed(runtime->chakra);
    }
    return InterlockedCompareExchange(
        (volatile LONG*)&runtime->failed, 0, 0) != 0;
}

void bytebeat_js_error(const BytebeatJsRuntime* runtime,
                       char* out, size_t out_size) {
    if (!runtime) {
        bbjs_copy_error(out, out_size, "missing JavaScript runtime");
        return;
    }
    if (runtime->chakra) {
        bytebeat_chakra_error(runtime->chakra, out, out_size);
        return;
    }
    bbjs_copy_error(out, out_size, runtime->error);
}
