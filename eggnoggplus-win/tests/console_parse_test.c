#include "../console_parse.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    char command[] = "  mods.config.set  'my mod' \"speed value\"  12  ";
    char unterminated[] = "\"two words";
    char spaces[] = "   \t  ";
    char* cursor = command;
    char* token;
    long integer = 0;
    double number = 0.0;

    token = console_parse_token(&cursor);
    assert(token && strcmp(token, "mods.config.set") == 0);
    token = console_parse_token(&cursor);
    assert(token && strcmp(token, "my mod") == 0);
    token = console_parse_token(&cursor);
    assert(token && strcmp(token, "speed value") == 0);
    token = console_parse_token(&cursor);
    assert(token && strcmp(token, "12") == 0);
    assert(console_parse_token(&cursor) == NULL);

    cursor = unterminated;
    token = console_parse_token(&cursor);
    assert(token && strcmp(token, "two words") == 0);
    assert(console_parse_token(&cursor) == NULL);
    cursor = spaces;
    assert(console_parse_token(&cursor) == NULL);
    assert(console_parse_token(NULL) == NULL);

    assert(console_try_parse_long(" 0x10 ", &integer));
    assert(integer == 16);
    assert(console_try_parse_long("-42", &integer));
    assert(integer == -42);
    assert(!console_try_parse_long("12px", &integer));
    assert(!console_try_parse_long("", &integer));
    assert(!console_try_parse_long("999999999999999999999999999999", &integer));

    assert(console_try_parse_double(" 1.25e2 ", &number));
    assert(fabs(number - 125.0) < 0.000001);
    assert(!console_try_parse_double("1.5x", &number));
    assert(!console_try_parse_double("nan", &number));
    assert(!console_try_parse_double("inf", &number));
    assert(!console_try_parse_double("1e9999", &number));

    puts("console parser tests: OK");
    return 0;
}
