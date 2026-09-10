#include <assert.h>
#include <stdio.h>
#include "../input_binding.h"

int main(void) {
    int value = 777;
    char name[64];
    const char* bad[] = {"Key", "SDLK_", "Keyfoo5", "Key12junk3", "Key-1",
        "Key+1", "Key1.5", "Key2147483648", "Key999999999999999999999999",
        "SDLK_4e2", "Key1 2", "", " ", NULL};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        value = 777;
        assert(!bind_name_to_sym(bad[i], &value));
        assert(value == 777);
    }
    assert(bind_name_to_sym("  LEFT ", &value) && value == 1073741904);
    assert(bind_name_to_sym("A", &value) && value == 'a');
    assert(bind_name_to_sym(" Key2147483647 ", &value) && value == INT_MAX);
    assert(bind_name_to_sym("sdlk_1073741903", &value) && value == 1073741903);
    assert(bind_name_to_sym("Key00042", &value) && value == 42);
    assert(bind_name_to_sym("clear", &value) && value == 0);
    for (size_t i = 0; i < sizeof(k_bind_name_map) / sizeof(k_bind_name_map[0]); i++) {
        assert(bind_sym_to_name(k_bind_name_map[i].sym, name, sizeof(name)));
        assert(bind_name_to_sym(name, &value) && value == k_bind_name_map[i].sym);
    }
    assert(bind_sym_to_name(INT_MAX, name, sizeof(name)));
    assert(bind_name_to_sym(name, &value) && value == INT_MAX);
    assert(!bind_sym_to_name(-1, name, sizeof(name)));
    assert(!bind_sym_to_name(0, name, 1));
    assert(name[0] == 0);
    puts("strict keyboard binding parser and round-trip tests: OK");
    return 0;
}
