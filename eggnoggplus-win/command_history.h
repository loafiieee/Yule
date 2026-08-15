#ifndef EGGNOGGPLUS_COMMAND_HISTORY_H
#define EGGNOGGPLUS_COMMAND_HISTORY_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COMMAND_HISTORY_CAPACITY 64
#define COMMAND_HISTORY_ENTRY_SIZE 512

typedef struct CommandHistory {
    char entries[COMMAND_HISTORY_CAPACITY][COMMAND_HISTORY_ENTRY_SIZE];
    size_t count;
    int loaded;
} CommandHistory;

void command_history_reset(CommandHistory* history);

/* Loads at most once until reset. Missing files leave an empty, loaded store. */
int command_history_load_once(CommandHistory* history, const char* path);
int command_history_save(const CommandHistory* history, const char* path);

/* Returns nonzero only when a nonempty, non-duplicate entry was appended. */
int command_history_add(CommandHistory* history, const char* command);
size_t command_history_count(const CommandHistory* history);
const char* command_history_at(const CommandHistory* history, size_t index);

#ifdef __cplusplus
}
#endif

#endif
