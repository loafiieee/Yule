#include "../text_util.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    char buffer[32];
    char overlap[16] = "abcdef";

    text_copy(buffer, sizeof(buffer), NULL);
    assert(strcmp(buffer, "") == 0);

    text_copy(buffer, 5u, "abcdef");
    assert(strcmp(buffer, "abcd") == 0);

    text_copy(overlap, sizeof(overlap), overlap + 2);
    assert(strcmp(overlap, "cdef") == 0);

    text_copy_ellipsized(buffer, sizeof(buffer), "short", 8u);
    assert(strcmp(buffer, "short") == 0);

    text_copy_ellipsized(buffer, sizeof(buffer), "abcdefghij", 7u);
    assert(strcmp(buffer, "abcd...") == 0);

    strcpy(overlap, "abcdefghij");
    text_copy_ellipsized(overlap, sizeof(overlap), overlap, 6u);
    assert(strcmp(overlap, "abc...") == 0);

    text_copy_ellipsized(buffer, sizeof(buffer), "abcdef", 3u);
    assert(strcmp(buffer, "abc") == 0);

    text_format_bytes(999u, buffer, sizeof(buffer));
    assert(strcmp(buffer, "999B") == 0);
    text_format_bytes(1536u, buffer, sizeof(buffer));
    assert(strcmp(buffer, "1.5KB") == 0);
    text_format_bytes(1024u * 1024u, buffer, sizeof(buffer));
    assert(strcmp(buffer, "1.00MB") == 0);

    puts("text utility tests: OK");
    return 0;
}
