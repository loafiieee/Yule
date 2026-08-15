#include "../command_history.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    static const char* const path = "build\\command_history_test.tmp";
    CommandHistory history;
    CommandHistory loaded;
    FILE* fixture;
    char command[32];
    int index;

    command_history_reset(&history);
    assert(command_history_count(&history) == 0u);
    assert(command_history_at(&history, 0u) == NULL);
    assert(!command_history_add(&history, ""));
    assert(command_history_add(&history, "help"));
    assert(!command_history_add(&history, "HELP"));
    assert(command_history_add(&history, "mods.list"));
    assert(command_history_count(&history) == 2u);
    assert(strcmp(command_history_at(&history, 1u), "mods.list") == 0);

    for (index = 0; index < COMMAND_HISTORY_CAPACITY + 3; ++index) {
        snprintf(command, sizeof(command), "echo %d", index);
        assert(command_history_add(&history, command));
    }
    assert(command_history_count(&history) == COMMAND_HISTORY_CAPACITY);
    assert(strcmp(command_history_at(&history, 0u), "echo 3") == 0);
    assert(command_history_save(&history, path));

    command_history_reset(&loaded);
    assert(command_history_load_once(&loaded, path));
    assert(command_history_count(&loaded) == COMMAND_HISTORY_CAPACITY);
    assert(strcmp(command_history_at(&loaded, 0u), "echo 3") == 0);
    assert(strcmp(command_history_at(&loaded,
                                     COMMAND_HISTORY_CAPACITY - 1u),
                  "echo 66") == 0);
    assert(command_history_load_once(&loaded, "missing-file-is-not-read"));

    fixture = fopen(path, "w");
    assert(fixture != NULL);
    fputs("first\r\n\r\nsecond\n", fixture);
    assert(fclose(fixture) == 0);
    command_history_reset(&loaded);
    assert(command_history_load_once(&loaded, path));
    assert(command_history_count(&loaded) == 2u);
    assert(strcmp(command_history_at(&loaded, 0u), "first") == 0);
    assert(strcmp(command_history_at(&loaded, 1u), "second") == 0);

    assert(remove(path) == 0);
    puts("command history tests: OK");
    return 0;
}
