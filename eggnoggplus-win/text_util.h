#ifndef EGGNOGGPLUS_TEXT_UTIL_H
#define EGGNOGGPLUS_TEXT_UTIL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Always NUL-terminates when destination_size is nonzero. NULL source text is
 * treated as an empty string. Source and destination may overlap. */
void text_copy(char* destination, size_t destination_size,
               const char* source);

/* Copies at most max_characters and uses a three-dot suffix when it fits. */
void text_copy_ellipsized(char* destination, size_t destination_size,
                          const char* source, size_t max_characters);

/* Formats a byte count for compact diagnostics and menus. */
void text_format_bytes(unsigned int bytes, char* destination,
                       size_t destination_size);

#ifdef __cplusplus
}
#endif

#endif
