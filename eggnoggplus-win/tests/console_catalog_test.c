#include "../console_catalog.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int ascii_case_equal(const char* left, const char* right) {
    while (*left || *right) {
        if (tolower((unsigned char)*left) !=
            tolower((unsigned char)*right)) {
            return 0;
        }
        ++left;
        ++right;
    }
    return 1;
}

int main(void) {
    size_t count = console_catalog_count();
    size_t index;
    size_t other;

    assert(count >= 60u);
    assert(console_catalog_at(count) == NULL);
    assert(!console_catalog_is_known(NULL));
    assert(!console_catalog_is_known(""));
    assert(!console_catalog_is_known("definitely.not.a.command"));

    for (index = 0; index < count; ++index) {
        const char* command = console_catalog_at(index);
        assert(command != NULL);
        assert(command[0] != '\0');
        assert(console_catalog_is_known(command));
        for (other = index + 1u; other < count; ++other) {
            assert(!ascii_case_equal(command, console_catalog_at(other)));
        }
    }

    /* These used to execute but were absent from autocomplete. */
    assert(console_catalog_is_known("ggpo.roundtrip"));
    assert(console_catalog_is_known("ggpo.selftest"));
    assert(console_catalog_is_known("ggpo.local"));
    assert(console_catalog_is_known("mods.cfg.get"));
    assert(console_catalog_is_known("online"));
    assert(console_catalog_is_known("QUIT"));

    assert(console_catalog_is_developer("GGPO.NET"));
    assert(console_catalog_is_developer("lua.mod"));
    assert(!console_catalog_is_developer("help"));
    assert(!console_catalog_is_developer("dev"));

    /* Every developer-only command must also be a known command. */
    for (index = 0; index < count; ++index) {
        const char* command = console_catalog_at(index);
        if (console_catalog_is_developer(command)) {
            assert(console_catalog_is_known(command));
        }
    }

    puts("console command catalog tests: OK");
    return 0;
}
