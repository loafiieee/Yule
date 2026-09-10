#ifndef YULE_INPUT_BINDING_H
#define YULE_INPUT_BINDING_H

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

/* Pure keyboard binding names; parsing must never partially accept numeric text. */
typedef struct BindNameMap {
    int sym;
    const char* name;
} BindNameMap;

static const BindNameMap k_bind_name_map[] = {
    { 8, "Backspace" },
    { 9, "Tab" },
    { 13, "Enter" },
    { 27, "Escape" },
    { 32, "Space" },
    { 127, "Delete" },
    { 1073741882, "F1" }, { 1073741883, "F2" }, { 1073741884, "F3" },
    { 1073741885, "F4" }, { 1073741886, "F5" }, { 1073741887, "F6" },
    { 1073741888, "F7" }, { 1073741889, "F8" }, { 1073741890, "F9" },
    { 1073741891, "F10" }, { 1073741892, "F11" }, { 1073741893, "F12" },
    { 1073741898, "Home" },
    { 1073741899, "PageUp" },
    { 1073741901, "End" },
    { 1073741902, "PageDown" },
    { 1073741903, "Right" },
    { 1073741904, "Left" },
    { 1073741905, "Down" },
    { 1073741906, "Up" },
    { 1073741912, "KeypadEnter" },
    { '`', "Backtick" },
};

static int bind_sym_to_name(int sym, char* out, size_t outsz) {
    int length;
    if (!out || outsz == 0) return 0;
    out[0] = '\0';
    if (sym < 0) return 0;
    if (sym == 0) {
        length = snprintf(out, outsz, "Unbound");
        return length >= 0 && (size_t)length < outsz;
    }
    for (size_t i = 0; i < sizeof(k_bind_name_map) / sizeof(k_bind_name_map[0]); i++) {
        if (k_bind_name_map[i].sym == sym) {
            length = snprintf(out, outsz, "%s", k_bind_name_map[i].name);
            return length >= 0 && (size_t)length < outsz;
        }
    }
    if (sym >= 33 && sym <= 126) {
        if (sym >= 'a' && sym <= 'z') sym = toupper(sym);
        length = snprintf(out, outsz, "%c", (char)sym);
        return length >= 0 && (size_t)length < outsz;
    }
    length = snprintf(out, outsz, "Key%d", sym);
    return length >= 0 && (size_t)length < outsz;
}

static int bind_ascii_equal(const char* name, size_t len, const char* expected) {
    if (len != strlen(expected)) return 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char a = (unsigned char)name[i];
        unsigned char b = (unsigned char)expected[i];
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + ('a' - 'A'));
        if (a != b) return 0;
    }
    return 1;
}

static int bind_name_to_sym(const char* name, int* out_sym) {
    size_t len, offset = 0;
    unsigned int value = 0;
    if (!name) return 0;
    while (*name && isspace((unsigned char)*name)) name++;
    len = strlen(name);
    while (len && isspace((unsigned char)name[len - 1])) len--;
    if (!len) return 0;
    if (bind_ascii_equal(name, len, "none") ||
        bind_ascii_equal(name, len, "unbound") ||
        bind_ascii_equal(name, len, "clear")) {
        if (out_sym) *out_sym = 0;
        return 1;
    }
    for (size_t i = 0; i < sizeof(k_bind_name_map) / sizeof(k_bind_name_map[0]); i++) {
        if (bind_ascii_equal(name, len, k_bind_name_map[i].name)) {
            if (out_sym) *out_sym = k_bind_name_map[i].sym;
            return 1;
        }
    }
    if (len > 3 && bind_ascii_equal(name, 3, "Key")) offset = 3;
    else if (len > 5 && bind_ascii_equal(name, 5, "SDLK_")) offset = 5;
    if (offset) {
        for (size_t i = offset; i < len; i++) {
            unsigned int digit;
            if (name[i] < '0' || name[i] > '9') return 0;
            digit = (unsigned int)(name[i] - '0');
            if (value > ((unsigned int)INT_MAX - digit) / 10u) return 0;
            value = value * 10u + digit;
        }
        if (out_sym) *out_sym = (int)value;
        return 1;
    }
    if (len == 1) {
        int sym = (unsigned char)name[0];
        if (sym >= 'A' && sym <= 'Z') sym += 'a' - 'A';
        if (out_sym) *out_sym = sym;
        return 1;
    }
    return 0;
}

#endif
