#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>

#include "bytebeat_stream.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct BytebeatStream {
    BytebeatJsRuntime* runtime;
    HANDLE thread;
    HANDLE wake_event;
    volatile LONG stop_requested;
    volatile LONG ready;
    volatile LONG failed;
    volatile LONG read_sequence;
    volatile LONG write_sequence;
    volatile LONG underruns;
    uint32_t sample_rate;
    uint64_t next_sample;
    int16_t pcm[BYTEBEAT_STREAM_CAPACITY_FRAMES * 2u];
    char error[256];
};

static void bytebeat_stream_copy_error(char* out, size_t out_size,
                                       const char* message) {
    if (!out || out_size == 0u) return;
    snprintf(out, out_size, "%s",
             message && message[0] ? message : "unknown stream error");
}

static uint32_t bytebeat_stream_load_sequence(const volatile LONG* value) {
    return (uint32_t)InterlockedCompareExchange(
        (volatile LONG*)value, 0, 0);
}

static uint32_t bytebeat_stream_used(const BytebeatStream* stream) {
    uint32_t read;
    uint32_t write;
    if (!stream) return 0u;
    read = bytebeat_stream_load_sequence(&stream->read_sequence);
    write = bytebeat_stream_load_sequence(&stream->write_sequence);
    return write - read;
}

static void bytebeat_stream_fail(BytebeatStream* stream,
                                 const char* fallback) {
    char runtime_error[256];
    if (!stream) return;
    runtime_error[0] = '\0';
    if (stream->runtime) {
        bytebeat_js_error(stream->runtime, runtime_error,
                          sizeof(runtime_error));
    }
    bytebeat_stream_copy_error(
        stream->error, sizeof(stream->error),
        runtime_error[0] ? runtime_error : fallback);
    InterlockedExchange(&stream->failed, 1);
    InterlockedExchange(&stream->ready, 1);
}

static unsigned __stdcall bytebeat_stream_thread(void* parameter) {
    BytebeatStream* stream = (BytebeatStream*)parameter;
    if (!stream) return 0u;
    (void)SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

    while (InterlockedCompareExchange(
               &stream->stop_requested, 0, 0) == 0) {
        uint32_t read =
            bytebeat_stream_load_sequence(&stream->read_sequence);
        uint32_t write =
            bytebeat_stream_load_sequence(&stream->write_sequence);
        uint32_t used = write - read;
        uint32_t slot;
        uint32_t budget;

        if (used > BYTEBEAT_STREAM_CAPACITY_FRAMES) {
            bytebeat_stream_fail(
                stream, "internal PCM stream sequence overflow");
            break;
        }
        if (BYTEBEAT_STREAM_CAPACITY_FRAMES - used <
            BYTEBEAT_STREAM_BLOCK_FRAMES) {
            if (used == BYTEBEAT_STREAM_CAPACITY_FRAMES) {
                InterlockedExchange(&stream->ready, 1);
            }
            (void)WaitForSingleObject(stream->wake_event, 25u);
            continue;
        }

        slot = write & (BYTEBEAT_STREAM_CAPACITY_FRAMES - 1u);
        budget = bytebeat_js_render_budget_ms(
            BYTEBEAT_STREAM_BLOCK_FRAMES, stream->sample_rate);
        bytebeat_js_begin_callback(stream->runtime, budget);
        if (!bytebeat_js_render(
                stream->runtime, stream->next_sample,
                BYTEBEAT_STREAM_BLOCK_FRAMES,
                &stream->pcm[(size_t)slot * 2u])) {
            bytebeat_js_end_callback(stream->runtime);
            bytebeat_stream_fail(
                stream, "Dollchan producer could not render PCM");
            break;
        }
        bytebeat_js_end_callback(stream->runtime);
        stream->next_sample += BYTEBEAT_STREAM_BLOCK_FRAMES;

        /*
         * InterlockedExchange is the publish barrier: the consumer cannot
         * observe this block until all interleaved PCM writes are complete.
         */
        InterlockedExchange(
            &stream->write_sequence,
            (LONG)(write + BYTEBEAT_STREAM_BLOCK_FRAMES));
        if (used + BYTEBEAT_STREAM_BLOCK_FRAMES ==
            BYTEBEAT_STREAM_CAPACITY_FRAMES) {
            InterlockedExchange(&stream->ready, 1);
        }
    }
    return 0u;
}

BytebeatStream* bytebeat_stream_create(BytebeatJsRuntime* runtime,
                                       uint32_t sample_rate,
                                       char* error, size_t error_size) {
    BytebeatStream* stream;
    uintptr_t thread;
    if (!runtime || sample_rate < 4000u || sample_rate > 48000u) {
        bytebeat_stream_copy_error(
            error, error_size, "invalid Dollchan stream parameters");
        return NULL;
    }
    stream = (BytebeatStream*)calloc(1u, sizeof(*stream));
    if (!stream) {
        bytebeat_stream_copy_error(
            error, error_size, "out of memory creating PCM stream");
        return NULL;
    }
    stream->runtime = runtime;
    stream->sample_rate = sample_rate;
    stream->wake_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!stream->wake_event) {
        bytebeat_stream_copy_error(
            error, error_size, "could not create PCM producer event");
        free(stream);
        return NULL;
    }
    thread = _beginthreadex(NULL, 0u, bytebeat_stream_thread,
                            stream, 0u, NULL);
    if (thread == 0u) {
        CloseHandle(stream->wake_event);
        stream->wake_event = NULL;
        bytebeat_stream_copy_error(
            error, error_size, "could not start PCM producer thread");
        free(stream);
        return NULL;
    }
    stream->thread = (HANDLE)thread;
    return stream;
}

void bytebeat_stream_destroy(BytebeatStream* stream) {
    if (!stream) return;
    InterlockedExchange(&stream->stop_requested, 1);
    if (stream->wake_event) SetEvent(stream->wake_event);
    if (stream->thread) {
        (void)WaitForSingleObject(stream->thread, INFINITE);
        CloseHandle(stream->thread);
    }
    if (stream->wake_event) CloseHandle(stream->wake_event);
    bytebeat_js_destroy(stream->runtime);
    free(stream);
}

int bytebeat_stream_ready(const BytebeatStream* stream) {
    return stream &&
        InterlockedCompareExchange(
            (volatile LONG*)&stream->ready, 0, 0) != 0 &&
        !bytebeat_stream_failed(stream);
}

int bytebeat_stream_read(BytebeatStream* stream,
                         int16_t* out_left, int16_t* out_right) {
    uint32_t read;
    uint32_t write;
    uint32_t slot;
    if (!stream || !out_left || !out_right ||
        !bytebeat_stream_ready(stream)) {
        return 0;
    }
    read = bytebeat_stream_load_sequence(&stream->read_sequence);
    write = bytebeat_stream_load_sequence(&stream->write_sequence);
    if (read == write) {
        InterlockedIncrement(&stream->underruns);
        return 0;
    }
    slot = read & (BYTEBEAT_STREAM_CAPACITY_FRAMES - 1u);
    *out_left = stream->pcm[(size_t)slot * 2u];
    *out_right = stream->pcm[(size_t)slot * 2u + 1u];
    InterlockedExchange(&stream->read_sequence, (LONG)(read + 1u));
    if (((read + 1u) & (BYTEBEAT_STREAM_BLOCK_FRAMES - 1u)) == 0u &&
        stream->wake_event) {
        SetEvent(stream->wake_event);
    }
    return 1;
}

int bytebeat_stream_failed(const BytebeatStream* stream) {
    return !stream ||
        InterlockedCompareExchange(
            (volatile LONG*)&stream->failed, 0, 0) != 0;
}

void bytebeat_stream_error(const BytebeatStream* stream,
                           char* out, size_t out_size) {
    if (!stream) {
        bytebeat_stream_copy_error(out, out_size, "missing PCM stream");
        return;
    }
    bytebeat_stream_copy_error(out, out_size, stream->error);
}

uint32_t bytebeat_stream_buffered_frames(const BytebeatStream* stream) {
    uint32_t used = bytebeat_stream_used(stream);
    return used > BYTEBEAT_STREAM_CAPACITY_FRAMES
        ? BYTEBEAT_STREAM_CAPACITY_FRAMES : used;
}

uint32_t bytebeat_stream_underruns(const BytebeatStream* stream) {
    if (!stream) return 0u;
    return (uint32_t)InterlockedCompareExchange(
        (volatile LONG*)&stream->underruns, 0, 0);
}
