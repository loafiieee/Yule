#include "text_util.h"

#include <stdio.h>
#include <string.h>

void text_copy(char* destination, size_t destination_size,
               const char* source) {
    size_t length;

    if (!destination || destination_size == 0) return;
    if (!source) source = "";
    length = strlen(source);
    if (length >= destination_size) length = destination_size - 1u;
    if (length != 0) memmove(destination, source, length);
    destination[length] = '\0';
}

void text_copy_ellipsized(char* destination, size_t destination_size,
                          const char* source, size_t max_characters) {
    size_t length;
    size_t keep;

    if (!destination || destination_size == 0) return;
    if (!source) source = "";
    if (max_characters >= destination_size) {
        max_characters = destination_size - 1u;
    }
    length = strlen(source);
    if (length <= max_characters) {
        text_copy(destination, destination_size, source);
        return;
    }
    if (max_characters < 4u) {
        if (max_characters != 0) {
            memmove(destination, source, max_characters);
        }
        destination[max_characters] = '\0';
        return;
    }

    keep = max_characters - 3u;
    if (keep != 0) memmove(destination, source, keep);
    destination[keep] = '.';
    destination[keep + 1u] = '.';
    destination[keep + 2u] = '.';
    destination[keep + 3u] = '\0';
}

void text_format_bytes(unsigned int bytes, char* destination,
                       size_t destination_size) {
    if (!destination || destination_size == 0) return;
    if (bytes >= 1024u * 1024u) {
        snprintf(destination, destination_size, "%.2fMB",
                 (double)bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024u) {
        snprintf(destination, destination_size, "%.1fKB",
                 (double)bytes / 1024.0);
    } else {
        snprintf(destination, destination_size, "%uB", bytes);
    }
}
