#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "../bytebeat_js.h"
#include "../bytebeat_stream.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void require(int condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static int16_t expected_u8(uint32_t t, double volume) {
    double normalized = (double)(t & 255u) / 127.5 - 1.0;
    double sample = normalized * volume * 32767.0;
    if (sample < -32768.0) sample = -32768.0;
    if (sample > 32767.0) sample = 32767.0;
    return (int16_t)(sample < 0.0
        ? ceil(sample - 0.5) : floor(sample + 0.5));
}

int main(void) {
    static const char source[] = "t";
    BytebeatJsRuntime* runtime;
    BytebeatStream* stream;
    char error[256];
    DWORD deadline;
    uint32_t i;

    runtime = bytebeat_js_create(
        source, sizeof(source) - 1u, BYTEBEAT_JS_MODE_U8,
        32000u, 0.5, error, sizeof(error));
    if (!runtime) {
        fprintf(stderr, "runtime creation failed: %s\n", error);
        return 1;
    }
    stream = bytebeat_stream_create(
        runtime, 32000u, error, sizeof(error));
    if (!stream) {
        bytebeat_js_destroy(runtime);
        fprintf(stderr, "stream creation failed: %s\n", error);
        return 1;
    }

    deadline = GetTickCount() + 5000u;
    while (!bytebeat_stream_ready(stream) &&
           !bytebeat_stream_failed(stream) &&
           (LONG)(deadline - GetTickCount()) > 0) {
        Sleep(5u);
    }
    if (bytebeat_stream_failed(stream)) {
        bytebeat_stream_error(stream, error, sizeof(error));
        fprintf(stderr, "producer failed: %s\n", error);
        bytebeat_stream_destroy(stream);
        return 1;
    }
    require(bytebeat_stream_ready(stream),
            "producer should prefill without blocking the consumer");
    require(bytebeat_stream_buffered_frames(stream) ==
                BYTEBEAT_STREAM_CAPACITY_FRAMES,
            "ready stream should expose a complete safety buffer");

    for (i = 0u; i < 8192u; i++) {
        int16_t left = 0;
        int16_t right = 0;
        int16_t expected = expected_u8(i, 0.5);
        require(bytebeat_stream_read(stream, &left, &right),
                "prefilled stream read should not underrun");
        require(left == expected && right == expected,
                "stream PCM must stay ordered and match Dollchan conversion");
    }
    require(bytebeat_stream_underruns(stream) == 0u,
            "normal prefilled consumption should not report an underrun");
    bytebeat_stream_destroy(stream);
    puts("bytebeat producer stream tests: OK");
    return 0;
}
