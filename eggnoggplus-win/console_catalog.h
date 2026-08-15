#ifndef EGGNOGGPLUS_CONSOLE_CATALOG_H
#define EGGNOGGPLUS_CONSOLE_CATALOG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns the complete public and developer console command catalog used by
 * autocomplete. Returned strings have static lifetime. */
size_t console_catalog_count(void);
const char* console_catalog_at(size_t index);

/* Command comparisons are ASCII case-insensitive. */
int console_catalog_is_known(const char* command);
int console_catalog_is_developer(const char* command);

#ifdef __cplusplus
}
#endif

#endif
