#pragma once

#include <stddef.h>
#include <stdint.h>

#include "bytebeat_js.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Dollchan programs can be substantially more expensive than one real-time
 * audio callback allows.  The stream owns a JavaScript runtime and renders it
 * on a dedicated producer thread into a bounded PCM ring.  The audio thread
 * never enters JavaScript, allocates memory, or waits for the producer.
 */
typedef struct BytebeatStream BytebeatStream;

#define BYTEBEAT_STREAM_BLOCK_FRAMES BYTEBEAT_JS_BATCH_FRAMES
#define BYTEBEAT_STREAM_BLOCK_COUNT 8u
#define BYTEBEAT_STREAM_CAPACITY_FRAMES \
    (BYTEBEAT_STREAM_BLOCK_FRAMES * BYTEBEAT_STREAM_BLOCK_COUNT)

/*
 * Ownership of runtime transfers to the returned stream on success.  On
 * failure, the caller still owns runtime.
 */
BytebeatStream* bytebeat_stream_create(BytebeatJsRuntime* runtime,
                                       uint32_t sample_rate,
                                       char* error, size_t error_size);

/* Stops and joins the producer before destroying its JavaScript runtime. */
void bytebeat_stream_destroy(BytebeatStream* stream);

/*
 * Playback remains silent until a complete safety buffer is ready.  This
 * avoids exposing partially filled buffers from expensive tracks.
 */
int bytebeat_stream_ready(const BytebeatStream* stream);

/*
 * Non-blocking, single-consumer read for the audio callback.  Returns zero
 * while prebuffering, after failure, or on an unexpected underrun.
 */
int bytebeat_stream_read(BytebeatStream* stream,
                         int16_t* out_left, int16_t* out_right);

int bytebeat_stream_failed(const BytebeatStream* stream);
void bytebeat_stream_error(const BytebeatStream* stream,
                           char* out, size_t out_size);
uint32_t bytebeat_stream_buffered_frames(const BytebeatStream* stream);
uint32_t bytebeat_stream_underruns(const BytebeatStream* stream);

#ifdef __cplusplus
}
#endif
