#include "command_history.h"

#include "text_util.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int ascii_case_equal(const char* left, const char* right) {
    unsigned char a;
    unsigned char b;

    if (!left || !right) return 0;
    do {
        a = (unsigned char)*left++;
        b = (unsigned char)*right++;
        if (tolower(a) != tolower(b)) return 0;
    } while (a != '\0');
    return 1;
}

static void strip_line_ending(char* text) {
    size_t length;

    if (!text) return;
    length = strlen(text);
    while (length > 0u &&
           (text[length - 1u] == '\r' || text[length - 1u] == '\n')) {
        text[--length] = '\0';
    }
}

void command_history_reset(CommandHistory* history) {
    if (!history) return;
    memset(history, 0, sizeof(*history));
}

int command_history_load_once(CommandHistory* history, const char* path) {
    FILE* file;
    char line[COMMAND_HISTORY_ENTRY_SIZE];

    if (!history || !path || !path[0]) return 0;
    if (history->loaded) return 1;
    history->loaded = 1;
    history->count = 0u;
    file = fopen(path, "r");
    if (!file) return 0;

    while (history->count < COMMAND_HISTORY_CAPACITY &&
           fgets(line, sizeof(line), file)) {
        size_t length = strlen(line);
        if (length > 0u && line[length - 1u] != '\n' && !feof(file)) {
            int ch;
            while ((ch = fgetc(file)) != '\n' && ch != EOF) {
                /* Discard the remainder of an externally enlarged entry. */
            }
        }
        strip_line_ending(line);
        if (!line[0]) continue;
        text_copy(history->entries[history->count],
                  sizeof(history->entries[history->count]), line);
        ++history->count;
    }
    fclose(file);
    return 1;
}

int command_history_save(const CommandHistory* history, const char* path) {
    FILE* file;
    size_t index;

    if (!history || !path || !path[0]) return 0;
    file = fopen(path, "w");
    if (!file) return 0;
    for (index = 0; index < history->count; ++index) {
        if (fprintf(file, "%s\n", history->entries[index]) < 0) {
            fclose(file);
            return 0;
        }
    }
    if (fclose(file) != 0) return 0;
    return 1;
}

int command_history_add(CommandHistory* history, const char* command) {
    if (!history || !command || !command[0]) return 0;
    if (history->count > 0u &&
        ascii_case_equal(history->entries[history->count - 1u], command)) {
        return 0;
    }
    if (history->count >= COMMAND_HISTORY_CAPACITY) {
        memmove(history->entries[0], history->entries[1],
                (COMMAND_HISTORY_CAPACITY - 1u) *
                    sizeof(history->entries[0]));
        history->count = COMMAND_HISTORY_CAPACITY - 1u;
    }
    text_copy(history->entries[history->count],
              sizeof(history->entries[history->count]), command);
    ++history->count;
    return 1;
}

size_t command_history_count(const CommandHistory* history) {
    return history ? history->count : 0u;
}

const char* command_history_at(const CommandHistory* history, size_t index) {
    if (!history || index >= history->count) return NULL;
    return history->entries[index];
}
