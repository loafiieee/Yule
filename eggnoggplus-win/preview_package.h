#pragma once
#include <stddef.h>
#include <stdint.h>
#define PREVIEW_PACKAGE_MAX_BYTES (128u*1024u*1024u)
#define PREVIEW_PACKAGE_MAX_FILES 64u
#define PREVIEW_PACKAGE_NAME_BYTES 128u
typedef struct PreviewFile {char name[PREVIEW_PACKAGE_NAME_BYTES];const unsigned char* bytes;uint32_t size;} PreviewFile;
typedef struct PreviewPackage {uint32_t count;PreviewFile files[PREVIEW_PACKAGE_MAX_FILES];} PreviewPackage;
/* Borrowed views into input. No allocation, file writes or script evaluation.
 * Exact framing and direct runtime filenames only; output untouched on failure. */
int preview_package_decode(const void* bytes,size_t size,PreviewPackage* out,char* error,size_t error_size);
int preview_package_filename_valid(const char* name);
