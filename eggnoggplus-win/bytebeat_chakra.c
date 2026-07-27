#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "bytebeat_chakra.h"
#include "bytebeat_js.h"

#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef int JsErrorCode;
typedef void* JsRuntimeHandle;
typedef void* JsContextRef;
typedef void* JsValueRef;
typedef uintptr_t JsSourceContext;

enum {
    JS_NO_ERROR = 0,
    JS_RUNTIME_ALLOW_SCRIPT_INTERRUPT = 0x00000002,
    JS_RUNTIME_DISABLE_FATAL_ON_OOM = 0x00000080,
    JS_ARRAY_TYPE_FLOAT64 = 8
};

typedef JsErrorCode (WINAPI *JsCreateRuntimeFn)(
    unsigned int, void*, JsRuntimeHandle*);
typedef JsErrorCode (WINAPI *JsDisposeRuntimeFn)(JsRuntimeHandle);
typedef JsErrorCode (WINAPI *JsCreateContextFn)(
    JsRuntimeHandle, JsContextRef*);
typedef JsErrorCode (WINAPI *JsSetCurrentContextFn)(JsContextRef);
typedef JsErrorCode (WINAPI *JsSetRuntimeMemoryLimitFn)(
    JsRuntimeHandle, size_t);
typedef JsErrorCode (WINAPI *JsRunScriptFn)(
    const wchar_t*, JsSourceContext, const wchar_t*, JsValueRef*);
typedef JsErrorCode (WINAPI *JsGetGlobalObjectFn)(JsValueRef*);
typedef JsErrorCode (WINAPI *JsCallFunctionFn)(
    JsValueRef, JsValueRef*, unsigned short, JsValueRef*);
typedef JsErrorCode (WINAPI *JsCreateTypedArrayFn)(
    int, JsValueRef, unsigned int, unsigned int, JsValueRef*);
typedef JsErrorCode (WINAPI *JsGetTypedArrayStorageFn)(
    JsValueRef, unsigned char**, unsigned int*, int*, int*);
typedef JsErrorCode (WINAPI *JsDoubleToNumberFn)(double, JsValueRef*);
typedef JsErrorCode (WINAPI *JsIntToNumberFn)(int, JsValueRef*);
typedef JsErrorCode (WINAPI *JsBoolToBooleanFn)(unsigned char, JsValueRef*);
typedef JsErrorCode (WINAPI *JsGetAndClearExceptionFn)(JsValueRef*);
typedef JsErrorCode (WINAPI *JsConvertValueToStringFn)(
    JsValueRef, JsValueRef*);
typedef JsErrorCode (WINAPI *JsStringToPointerFn)(
    JsValueRef, const wchar_t**, size_t*);
typedef JsErrorCode (WINAPI *JsAddRefFn)(void*, unsigned int*);
typedef JsErrorCode (WINAPI *JsReleaseFn)(void*, unsigned int*);
typedef JsErrorCode (WINAPI *JsDisableRuntimeExecutionFn)(JsRuntimeHandle);
typedef JsErrorCode (WINAPI *JsEnableRuntimeExecutionFn)(JsRuntimeHandle);

typedef struct BytebeatChakraApi {
    HMODULE module;
    JsCreateRuntimeFn create_runtime;
    JsDisposeRuntimeFn dispose_runtime;
    JsCreateContextFn create_context;
    JsSetCurrentContextFn set_current_context;
    JsSetRuntimeMemoryLimitFn set_memory_limit;
    JsRunScriptFn run_script;
    JsGetGlobalObjectFn get_global;
    JsCallFunctionFn call_function;
    JsCreateTypedArrayFn create_typed_array;
    JsGetTypedArrayStorageFn get_typed_array_storage;
    JsDoubleToNumberFn double_to_number;
    JsIntToNumberFn int_to_number;
    JsBoolToBooleanFn bool_to_boolean;
    JsGetAndClearExceptionFn get_and_clear_exception;
    JsConvertValueToStringFn convert_to_string;
    JsStringToPointerFn string_to_pointer;
    JsAddRefFn add_ref;
    JsReleaseFn release;
    JsDisableRuntimeExecutionFn disable_execution;
    JsEnableRuntimeExecutionFn enable_execution;
} BytebeatChakraApi;

struct BytebeatChakraRuntime {
    JsRuntimeHandle runtime;
    JsContextRef context;
    JsValueRef global;
    JsValueRef function;
    JsValueRef batch_function;
    JsValueRef batch_output;
    double* batch_values;
    unsigned int batch_bytes;
    uint32_t source_context;
    int mode;
    uint32_t sample_rate;
    double volume;
    double held[2];
    HANDLE timer;
    ULONGLONG deadline_ms;
    volatile LONG interrupt_enabled;
    volatile LONG script_running;
    volatile LONG timed_out;
    volatile LONG failed;
    char error[256];
};

static INIT_ONCE g_chakra_once = INIT_ONCE_STATIC_INIT;
static BytebeatChakraApi g_chakra;
static int g_chakra_available = 0;

static void bbch_copy_error(char* out, size_t out_size,
                            const char* message) {
    if (!out || out_size == 0u) return;
    snprintf(out, out_size, "%s", message && message[0]
        ? message : "unknown JavaScript error");
}

static void bbch_set_error(BytebeatChakraRuntime* runtime,
                           const char* message) {
    if (!runtime) return;
    bbch_copy_error(runtime->error, sizeof(runtime->error), message);
    InterlockedExchange(&runtime->failed, 1);
}

static BOOL CALLBACK bbch_load_api(PINIT_ONCE once, PVOID parameter,
                                   PVOID* context) {
    wchar_t system_path[MAX_PATH];
    size_t length;
    HMODULE module;
#define BBCH_RESOLVE(field, name) \
    do { \
        g_chakra.field = (name##Fn)(void*)GetProcAddress(module, #name); \
        if (!g_chakra.field) return TRUE; \
    } while (0)
    (void)once;
    (void)parameter;
    (void)context;
    memset(&g_chakra, 0, sizeof(g_chakra));
    length = (size_t)GetSystemDirectoryW(
        system_path, (UINT)(sizeof(system_path) / sizeof(system_path[0])));
    if (length == 0u ||
        length + wcslen(L"\\Chakra.dll") + 1u >
            sizeof(system_path) / sizeof(system_path[0])) {
        return TRUE;
    }
    wcscat(system_path, L"\\Chakra.dll");
    module = LoadLibraryExW(system_path, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) module = LoadLibraryW(system_path);
    if (!module) return TRUE;
    g_chakra.module = module;

    BBCH_RESOLVE(create_runtime, JsCreateRuntime);
    BBCH_RESOLVE(dispose_runtime, JsDisposeRuntime);
    BBCH_RESOLVE(create_context, JsCreateContext);
    BBCH_RESOLVE(set_current_context, JsSetCurrentContext);
    BBCH_RESOLVE(set_memory_limit, JsSetRuntimeMemoryLimit);
    BBCH_RESOLVE(run_script, JsRunScript);
    BBCH_RESOLVE(get_global, JsGetGlobalObject);
    BBCH_RESOLVE(call_function, JsCallFunction);
    BBCH_RESOLVE(create_typed_array, JsCreateTypedArray);
    BBCH_RESOLVE(get_typed_array_storage, JsGetTypedArrayStorage);
    BBCH_RESOLVE(double_to_number, JsDoubleToNumber);
    BBCH_RESOLVE(int_to_number, JsIntToNumber);
    BBCH_RESOLVE(bool_to_boolean, JsBoolToBoolean);
    BBCH_RESOLVE(get_and_clear_exception, JsGetAndClearException);
    BBCH_RESOLVE(convert_to_string, JsConvertValueToString);
    BBCH_RESOLVE(string_to_pointer, JsStringToPointer);
    BBCH_RESOLVE(add_ref, JsAddRef);
    BBCH_RESOLVE(release, JsRelease);
    BBCH_RESOLVE(disable_execution, JsDisableRuntimeExecution);
    BBCH_RESOLVE(enable_execution, JsEnableRuntimeExecution);
    g_chakra_available = 1;
    return TRUE;
#undef BBCH_RESOLVE
}

int bytebeat_chakra_available(void) {
    InitOnceExecuteOnce(&g_chakra_once, bbch_load_api, NULL, NULL);
    return g_chakra_available;
}

static void bbch_timer_disarm(BytebeatChakraRuntime* runtime) {
    if (!runtime || !runtime->timer) return;
    (void)ChangeTimerQueueTimer(NULL, runtime->timer, INFINITE, 0u);
}

static VOID CALLBACK bbch_timeout(PVOID parameter, BOOLEAN fired) {
    BytebeatChakraRuntime* runtime =
        (BytebeatChakraRuntime*)parameter;
    (void)fired;
    if (!runtime ||
        InterlockedCompareExchange(&runtime->interrupt_enabled, 0, 0) == 0 ||
        InterlockedCompareExchange(&runtime->script_running, 0, 0) == 0 ||
        GetTickCount64() < runtime->deadline_ms) {
        return;
    }
    InterlockedExchange(&runtime->timed_out, 1);
    (void)g_chakra.disable_execution(runtime->runtime);
}

static int bbch_timer_arm(BytebeatChakraRuntime* runtime) {
    ULONGLONG now;
    ULONGLONG remaining;
    DWORD due;
    if (!runtime || !runtime->timer ||
        InterlockedCompareExchange(&runtime->interrupt_enabled, 0, 0) == 0) {
        return 0;
    }
    now = GetTickCount64();
    if (now >= runtime->deadline_ms) {
        InterlockedExchange(&runtime->timed_out, 1);
        bbch_set_error(runtime, "runtime error: InternalError: interrupted");
        return 0;
    }
    remaining = runtime->deadline_ms - now;
    due = remaining > (ULONGLONG)MAXDWORD
        ? MAXDWORD : (DWORD)remaining;
    if (due == 0u) due = 1u;
    InterlockedExchange(&runtime->script_running, 1);
    if (!ChangeTimerQueueTimer(NULL, runtime->timer, due, 0u)) {
        InterlockedExchange(&runtime->script_running, 0);
        bbch_set_error(runtime,
                       "could not arm the JavaScript interrupt watchdog");
        return 0;
    }
    return 1;
}

static void bbch_timer_after_script(BytebeatChakraRuntime* runtime) {
    if (!runtime) return;
    InterlockedExchange(&runtime->script_running, 0);
    bbch_timer_disarm(runtime);
}

static void bbch_exception(BytebeatChakraRuntime* runtime,
                           const char* prefix, JsErrorCode code) {
    JsValueRef exception = NULL;
    JsValueRef text = NULL;
    const wchar_t* wide = NULL;
    size_t wide_length = 0u;
    char utf8[180];
    char combined[256];
    int converted;
    if (!runtime) return;
    if (InterlockedCompareExchange(&runtime->timed_out, 0, 0) != 0) {
        bbch_set_error(runtime, "runtime error: InternalError: interrupted");
        return;
    }
    utf8[0] = '\0';
    if (g_chakra.get_and_clear_exception(&exception) == JS_NO_ERROR &&
        exception &&
        g_chakra.convert_to_string(exception, &text) == JS_NO_ERROR &&
        text &&
        g_chakra.string_to_pointer(text, &wide, &wide_length) == JS_NO_ERROR &&
        wide && wide_length > 0u) {
        int count = wide_length > 80u ? 80 : (int)wide_length;
        converted = WideCharToMultiByte(
            CP_UTF8, 0, wide, count, utf8, (int)sizeof(utf8) - 1,
            NULL, NULL);
        if (converted > 0) utf8[converted] = '\0';
    }
    if (utf8[0]) {
        snprintf(combined, sizeof(combined), "%s: %s",
                 prefix ? prefix : "JavaScript error", utf8);
    } else {
        snprintf(combined, sizeof(combined), "%s: Chakra error code %d",
                 prefix ? prefix : "JavaScript error", code);
    }
    bbch_set_error(runtime, combined);
}

static wchar_t* bbch_utf8_to_wide(const char* source, size_t length) {
    int required;
    wchar_t* wide;
    if (!source || length == 0u || length > (size_t)INT_MAX) return NULL;
    required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                   source, (int)length, NULL, 0);
    if (required <= 0) return NULL;
    wide = (wchar_t*)malloc(((size_t)required + 1u) * sizeof(wchar_t));
    if (!wide) return NULL;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                            source, (int)length, wide, required) != required) {
        free(wide);
        return NULL;
    }
    wide[required] = L'\0';
    return wide;
}

static int bbch_run(BytebeatChakraRuntime* runtime,
                    const char* source, size_t source_length,
                    const wchar_t* source_name,
                    const char* error_prefix,
                    JsValueRef* out) {
    wchar_t* wide;
    JsValueRef result = NULL;
    JsErrorCode code;
    if (!runtime || !source || source_length == 0u) return 0;
    wide = bbch_utf8_to_wide(source, source_length);
    if (!wide) {
        bbch_set_error(runtime,
                       "JavaScript source is not valid UTF-8 or is too large");
        return 0;
    }
    if (!bbch_timer_arm(runtime)) {
        free(wide);
        return 0;
    }
    code = g_chakra.run_script(
        wide, (JsSourceContext)runtime->source_context++,
        source_name ? source_name : L"playlist-bytebeat.js", &result);
    bbch_timer_after_script(runtime);
    free(wide);
    if (code != JS_NO_ERROR) {
        bbch_exception(runtime, error_prefix, code);
        return 0;
    }
    if (out) *out = result;
    return 1;
}

static char* bbch_wrapper(const char* source, size_t source_length,
                          int mode, size_t* out_length) {
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

static int bbch_keep(JsValueRef value) {
    return value &&
        g_chakra.add_ref(value, NULL) == JS_NO_ERROR;
}

static int bbch_call_raw(BytebeatChakraRuntime* runtime,
                         uint64_t sample_index, JsValueRef* out) {
    JsValueRef arguments[3];
    JsValueRef result = NULL;
    unsigned short count;
    JsErrorCode code;
    arguments[0] = runtime->global;
    if (runtime->mode == BYTEBEAT_JS_MODE_FUNC) {
        if (g_chakra.double_to_number(
                (double)sample_index / (double)runtime->sample_rate,
                &arguments[1]) != JS_NO_ERROR ||
            g_chakra.int_to_number(
                (int)runtime->sample_rate, &arguments[2]) != JS_NO_ERROR) {
            bbch_set_error(runtime,
                           "could not create funcbeat call arguments");
            return 0;
        }
        count = 3u;
    } else {
        if (g_chakra.double_to_number(
                (double)sample_index, &arguments[1]) != JS_NO_ERROR) {
            bbch_set_error(runtime,
                           "could not create bytebeat call argument");
            return 0;
        }
        count = 2u;
    }
    if (!bbch_timer_arm(runtime)) return 0;
    code = g_chakra.call_function(
        runtime->function, arguments, count, &result);
    bbch_timer_after_script(runtime);
    if (code != JS_NO_ERROR) {
        bbch_exception(runtime, "runtime error", code);
        return 0;
    }
    if (out) *out = result;
    return 1;
}

static uint32_t bbch_to_uint32(double value) {
    double truncated;
    double wrapped;
    if (!isfinite(value) || value == 0.0) return 0u;
    truncated = value < 0.0 ? ceil(value) : floor(value);
    wrapped = fmod(truncated, 4294967296.0);
    if (wrapped < 0.0) wrapped += 4294967296.0;
    return (uint32_t)wrapped;
}

static double bbch_normalize(BytebeatChakraRuntime* runtime, double value) {
    if (runtime->mode == BYTEBEAT_JS_MODE_FLOAT ||
        runtime->mode == BYTEBEAT_JS_MODE_FUNC) {
        if (value < -1.0) return -1.0;
        if (value > 1.0) return 1.0;
        return value;
    }
    if (runtime->mode == BYTEBEAT_JS_MODE_S8) value += 128.0;
    return (double)(bbch_to_uint32(value) & 0xffu) / 127.5 - 1.0;
}

static int16_t bbch_pcm16(double normalized, double volume) {
    double sample = normalized * volume * 32767.0;
    if (sample < -32768.0) sample = -32768.0;
    if (sample > 32767.0) sample = 32767.0;
    return (int16_t)(sample < 0.0
        ? ceil(sample - 0.5) : floor(sample + 0.5));
}

BytebeatChakraRuntime* bytebeat_chakra_create(
    const char* source, size_t source_length, int mode,
    uint32_t sample_rate, double volume,
    char* error, size_t error_size) {
    static const char setup[] =
        "(function(g){"
        "for(const k of Object.getOwnPropertyNames(Math)){"
        "Object.defineProperty(g,k,{value:Math[k],"
        "writable:false,configurable:false});}"
        "Object.defineProperty(g,'int',{value:Math.floor,"
        "writable:false,configurable:false});"
        "Object.defineProperty(g,'window',{value:g,"
        "writable:false,configurable:false});"
        "})(this)";
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
    BytebeatChakraRuntime* runtime;
    char* wrapper = NULL;
    size_t wrapper_length = 0u;
    JsValueRef ignored = NULL;
    JsValueRef validation = NULL;
    int array_type = -1;
    int element_size = 0;

    if (!bytebeat_chakra_available()) return NULL;
    runtime = (BytebeatChakraRuntime*)calloc(1u, sizeof(*runtime));
    if (!runtime) {
        bbch_copy_error(error, error_size,
                        "out of memory creating Chakra runtime");
        return NULL;
    }
    runtime->mode = mode;
    runtime->sample_rate = sample_rate;
    runtime->volume = volume;
    if (g_chakra.create_runtime(
            JS_RUNTIME_ALLOW_SCRIPT_INTERRUPT |
                JS_RUNTIME_DISABLE_FATAL_ON_OOM,
            NULL, &runtime->runtime) != JS_NO_ERROR ||
        !runtime->runtime) {
        bbch_copy_error(error, error_size,
                        "could not create system Chakra runtime");
        free(runtime);
        return NULL;
    }
    if (g_chakra.set_memory_limit(
            runtime->runtime, BYTEBEAT_JS_MEMORY_LIMIT) != JS_NO_ERROR ||
        g_chakra.create_context(
            runtime->runtime, &runtime->context) != JS_NO_ERROR ||
        !runtime->context ||
        g_chakra.set_current_context(runtime->context) != JS_NO_ERROR) {
        bbch_copy_error(error, error_size,
                        "could not initialize system Chakra context");
        bytebeat_chakra_destroy(runtime);
        return NULL;
    }
    (void)bbch_keep(runtime->context);
    if (!CreateTimerQueueTimer(
            &runtime->timer, NULL, bbch_timeout, runtime,
            INFINITE, 0u, WT_EXECUTEDEFAULT)) {
        bbch_copy_error(error, error_size,
                        "could not create JavaScript interrupt watchdog");
        bytebeat_chakra_destroy(runtime);
        return NULL;
    }
    bytebeat_chakra_begin(
        runtime, bytebeat_js_compile_budget_ms(source_length));
    if (g_chakra.get_global(&runtime->global) != JS_NO_ERROR ||
        !bbch_keep(runtime->global) ||
        !bbch_run(runtime, setup, sizeof(setup) - 1u,
                  L"dollchan-prelude.js", "Dollchan prelude failed",
                  &ignored) ||
        !bbch_run(runtime, batch_source, sizeof(batch_source) - 1u,
                  L"dollchan-block-renderer.js",
                  "Dollchan block renderer failed",
                  &runtime->batch_function) ||
        !bbch_keep(runtime->batch_function)) {
        bbch_copy_error(error, error_size, runtime->error);
        bytebeat_chakra_end(runtime);
        bytebeat_chakra_destroy(runtime);
        return NULL;
    }
    if (g_chakra.create_typed_array(
            JS_ARRAY_TYPE_FLOAT64, NULL, 0u,
            BYTEBEAT_JS_BATCH_FRAMES * 2u,
            &runtime->batch_output) != JS_NO_ERROR ||
        !bbch_keep(runtime->batch_output) ||
        g_chakra.get_typed_array_storage(
            runtime->batch_output,
            (unsigned char**)&runtime->batch_values,
            &runtime->batch_bytes, &array_type,
            &element_size) != JS_NO_ERROR ||
        !runtime->batch_values ||
        array_type != JS_ARRAY_TYPE_FLOAT64 ||
        element_size != (int)sizeof(double) ||
        runtime->batch_bytes <
            BYTEBEAT_JS_BATCH_FRAMES * 2u * sizeof(double)) {
        bbch_set_error(runtime,
                       "Dollchan output buffer has an invalid layout");
        bbch_copy_error(error, error_size, runtime->error);
        bytebeat_chakra_end(runtime);
        bytebeat_chakra_destroy(runtime);
        return NULL;
    }
    wrapper = bbch_wrapper(source, source_length, mode, &wrapper_length);
    if (!wrapper ||
        !bbch_run(runtime, wrapper, wrapper_length,
                  L"playlist-bytebeat.js", "compile error",
                  &runtime->function) ||
        !bbch_keep(runtime->function) ||
        !bbch_call_raw(runtime, 0u, &validation)) {
        if (!runtime->error[0]) {
            bbch_set_error(runtime,
                           "could not compile or validate JavaScript source");
        }
        bbch_copy_error(error, error_size, runtime->error);
        free(wrapper);
        bytebeat_chakra_end(runtime);
        bytebeat_chakra_destroy(runtime);
        return NULL;
    }
    free(wrapper);
    bytebeat_chakra_end(runtime);
    runtime->held[0] = 0.0;
    runtime->held[1] = 0.0;
    runtime->failed = 0;
    runtime->error[0] = '\0';
    (void)g_chakra.set_current_context(NULL);
    return runtime;
}

void bytebeat_chakra_destroy(BytebeatChakraRuntime* runtime) {
    if (!runtime) return;
    InterlockedExchange(&runtime->interrupt_enabled, 0);
    InterlockedExchange(&runtime->script_running, 0);
    if (runtime->timer) {
        (void)DeleteTimerQueueTimer(
            NULL, runtime->timer, INVALID_HANDLE_VALUE);
        runtime->timer = NULL;
    }
    if (runtime->runtime) {
        (void)g_chakra.enable_execution(runtime->runtime);
    }
    if (runtime->context) {
        (void)g_chakra.set_current_context(runtime->context);
        if (runtime->batch_output) {
            (void)g_chakra.release(runtime->batch_output, NULL);
        }
        if (runtime->batch_function) {
            (void)g_chakra.release(runtime->batch_function, NULL);
        }
        if (runtime->function) {
            (void)g_chakra.release(runtime->function, NULL);
        }
        if (runtime->global) {
            (void)g_chakra.release(runtime->global, NULL);
        }
        (void)g_chakra.release(runtime->context, NULL);
        (void)g_chakra.set_current_context(NULL);
    }
    if (runtime->runtime) {
        (void)g_chakra.dispose_runtime(runtime->runtime);
    }
    free(runtime);
}

void bytebeat_chakra_begin(BytebeatChakraRuntime* runtime,
                           uint32_t budget_ms) {
    if (!runtime) return;
    if (budget_ms == 0u) budget_ms = BYTEBEAT_JS_CALLBACK_BUDGET_MS;
    runtime->deadline_ms = GetTickCount64() + (ULONGLONG)budget_ms;
    InterlockedExchange(&runtime->timed_out, 0);
    InterlockedExchange(&runtime->interrupt_enabled, 1);
}

void bytebeat_chakra_end(BytebeatChakraRuntime* runtime) {
    if (!runtime) return;
    InterlockedExchange(&runtime->interrupt_enabled, 0);
    InterlockedExchange(&runtime->script_running, 0);
    bbch_timer_disarm(runtime);
    runtime->deadline_ms = 0u;
}

int bytebeat_chakra_render(BytebeatChakraRuntime* runtime,
                           uint64_t first_sample, uint32_t frame_count,
                           int16_t* output) {
    JsValueRef arguments[7];
    JsValueRef result = NULL;
    JsErrorCode code;
    uint32_t frame;
    if (!runtime || !output || frame_count == 0u ||
        frame_count > BYTEBEAT_JS_BATCH_FRAMES ||
        !runtime->batch_values ||
        runtime->batch_bytes <
            (size_t)frame_count * 2u * sizeof(double) ||
        InterlockedCompareExchange(&runtime->failed, 0, 0) != 0 ||
        g_chakra.set_current_context(runtime->context) != JS_NO_ERROR) {
        return 0;
    }
    arguments[0] = runtime->global;
    arguments[1] = runtime->function;
    arguments[2] = runtime->batch_output;
    if (g_chakra.double_to_number(
            (double)first_sample, &arguments[3]) != JS_NO_ERROR ||
        g_chakra.int_to_number(
            (int)frame_count, &arguments[4]) != JS_NO_ERROR ||
        g_chakra.int_to_number(
            (int)runtime->sample_rate, &arguments[5]) != JS_NO_ERROR ||
        g_chakra.bool_to_boolean(
            runtime->mode == BYTEBEAT_JS_MODE_FUNC,
            &arguments[6]) != JS_NO_ERROR ||
        !bbch_timer_arm(runtime)) {
        (void)g_chakra.set_current_context(NULL);
        return 0;
    }
    code = g_chakra.call_function(
        runtime->batch_function, arguments, 7u, &result);
    bbch_timer_after_script(runtime);
    if (code != JS_NO_ERROR) {
        bbch_exception(runtime, "runtime error", code);
        (void)g_chakra.set_current_context(NULL);
        return 0;
    }
    for (frame = 0u; frame < frame_count; frame++) {
        double left = runtime->batch_values[(size_t)frame * 2u];
        double right = runtime->batch_values[(size_t)frame * 2u + 1u];
        if (!isnan(left)) {
            runtime->held[0] = bbch_normalize(runtime, left);
        }
        if (!isnan(right)) {
            runtime->held[1] = bbch_normalize(runtime, right);
        }
        output[(size_t)frame * 2u] =
            bbch_pcm16(runtime->held[0], runtime->volume);
        output[(size_t)frame * 2u + 1u] =
            bbch_pcm16(runtime->held[1], runtime->volume);
    }
    (void)g_chakra.set_current_context(NULL);
    return 1;
}

int bytebeat_chakra_sample(BytebeatChakraRuntime* runtime,
                           uint64_t sample_index,
                           int16_t* out_left, int16_t* out_right) {
    int16_t output[2];
    if (!out_left || !out_right ||
        !bytebeat_chakra_render(runtime, sample_index, 1u, output)) {
        return 0;
    }
    *out_left = output[0];
    *out_right = output[1];
    return 1;
}

int bytebeat_chakra_failed(const BytebeatChakraRuntime* runtime) {
    if (!runtime) return 1;
    return InterlockedCompareExchange(
        (volatile LONG*)&runtime->failed, 0, 0) != 0;
}

void bytebeat_chakra_error(const BytebeatChakraRuntime* runtime,
                           char* out, size_t out_size) {
    if (!runtime) {
        bbch_copy_error(out, out_size, "missing Chakra runtime");
        return;
    }
    bbch_copy_error(out, out_size, runtime->error);
}
