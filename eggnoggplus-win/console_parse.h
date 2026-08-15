#ifndef EGGNOGGPLUS_CONSOLE_PARSE_H
#define EGGNOGGPLUS_CONSOLE_PARSE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Destructively returns the next whitespace-delimited token and advances the
 * cursor. Single- and double-quoted tokens may contain spaces. */
char* console_parse_token(char** inout_cursor);

/* Parse a complete value, allowing surrounding whitespace but no suffix. */
int console_try_parse_long(const char* text, long* out_value);
int console_try_parse_double(const char* text, double* out_value);

#ifdef __cplusplus
}
#endif

#endif
