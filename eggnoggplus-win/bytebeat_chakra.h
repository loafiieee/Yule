#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BytebeatChakraRuntime BytebeatChakraRuntime;

/* Returns non-zero only when the system 32-bit Chakra hosting API is present. */
int bytebeat_chakra_available(void);

BytebeatChakraRuntime* bytebeat_chakra_create(
    const char* source, size_t source_length, int mode,
    uint32_t sample_rate, double volume,
    char* error, size_t error_size);
void bytebeat_chakra_destroy(BytebeatChakraRuntime* runtime);

void bytebeat_chakra_begin(BytebeatChakraRuntime* runtime,
                           uint32_t budget_ms);
void bytebeat_chakra_end(BytebeatChakraRuntime* runtime);

int bytebeat_chakra_sample(BytebeatChakraRuntime* runtime,
                           uint64_t sample_index,
                           int16_t* out_left, int16_t* out_right);
int bytebeat_chakra_render(BytebeatChakraRuntime* runtime,
                           uint64_t first_sample, uint32_t frame_count,
                           int16_t* output);

int bytebeat_chakra_failed(const BytebeatChakraRuntime* runtime);
void bytebeat_chakra_error(const BytebeatChakraRuntime* runtime,
                           char* out, size_t out_size);

#ifdef __cplusplus
}
#endif
