#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BYTEBEAT_MAX_EXPRESSION 1024u
#define BYTEBEAT_MAX_NODES 512u
#define BYTEBEAT_MAX_DEPTH 48u
#define BYTEBEAT_MAX_SAMPLES 262144u
#define BYTEBEAT_MAX_RENDER_OPERATIONS 8388608u
#define BYTEBEAT_MAX_STREAM_OPERATIONS_PER_SECOND 25165824u
#define BYTEBEAT_DEFAULT_PLAYLIST_SAMPLE_RATE 8000u
#define BYTEBEAT_DEFAULT_PLAYLIST_VOLUME 0.25
#define BYTEBEAT_DEFAULT_PLAYLIST_OUTPUT_RATE 0u

typedef enum BytebeatMode {
    BYTEBEAT_MODE_U8 = 0,
    BYTEBEAT_MODE_FLOAT = 1
} BytebeatMode;

typedef enum BytebeatPlaylistEngine {
    BYTEBEAT_PLAYLIST_BOUNDED = 0,
    BYTEBEAT_PLAYLIST_DOLLCHAN = 1
} BytebeatPlaylistEngine;

typedef enum BytebeatPlaylistMode {
    BYTEBEAT_PLAYLIST_U8 = 0,
    BYTEBEAT_PLAYLIST_S8 = 1,
    BYTEBEAT_PLAYLIST_FLOAT = 2,
    BYTEBEAT_PLAYLIST_FUNC = 3
} BytebeatPlaylistMode;

typedef struct BytebeatPlaylistOptions {
    uint32_t sample_rate;
    uint32_t output_rate;
    double volume;
    BytebeatPlaylistEngine engine;
    BytebeatPlaylistMode mode;
} BytebeatPlaylistOptions;

typedef struct BytebeatDiagnostic {
    size_t offset;
    char message[160];
} BytebeatDiagnostic;

typedef struct BytebeatNode {
    uint8_t kind;
    uint8_t op;
    int16_t a;
    int16_t b;
    int16_t c;
    double value;
} BytebeatNode;

typedef struct BytebeatProgram {
    BytebeatNode nodes[BYTEBEAT_MAX_NODES];
    uint16_t node_count;
    uint16_t max_depth;
    int16_t root;
    uint8_t mode;
    uint8_t reserved;
} BytebeatProgram;

typedef struct BytebeatRenderOptions {
    uint32_t sample_rate;
    uint32_t sample_count;
    uint32_t fade_samples;
    double gain;
} BytebeatRenderOptions;

int bytebeat_compile(const char* expression, BytebeatMode mode,
                     BytebeatProgram* out_program,
                     BytebeatDiagnostic* diagnostic);

/*
 * Parses `( yule:bytebeat ... )` metadata. sample_rate and volume are
 * independently optional and receive the documented defaults when omitted.
 */
int bytebeat_parse_playlist_marker(const char* marker,
                                   uint32_t* out_sample_rate,
                                   double* out_volume,
                                   BytebeatDiagnostic* diagnostic);

int bytebeat_parse_playlist_options(const char* marker,
                                    BytebeatPlaylistOptions* out_options,
                                    BytebeatDiagnostic* diagnostic);

int bytebeat_evaluate(const BytebeatProgram* program, uint32_t t,
                      uint32_t sample_rate, double* out_value);

int bytebeat_render_pcm16(const BytebeatProgram* program,
                          const BytebeatRenderOptions* options,
                          int16_t* out_samples, size_t out_sample_capacity,
                          BytebeatDiagnostic* diagnostic);

size_t bytebeat_wav_size(uint32_t sample_count);
int bytebeat_render_wav(const BytebeatProgram* program,
                        const BytebeatRenderOptions* options,
                        unsigned char* out_wav, size_t out_capacity,
                        size_t* out_size, BytebeatDiagnostic* diagnostic);

#ifdef __cplusplus
}
#endif
