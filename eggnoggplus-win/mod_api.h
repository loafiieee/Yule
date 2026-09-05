#ifndef EGGNOGGPLUS_MOD_API_H
#define EGGNOGGPLUS_MOD_API_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Major changes may remove or change public contracts. Revisions are additive
 * within one major and therefore remain backward compatible.
 */
#define MOD_API_MAJOR 1
#define MOD_API_REVISION 6
#define MOD_API_VERSION MOD_API_MAJOR

#define MOD_API_CAPABILITY_NAME_MAX 64
#define MOD_API_REQUIRED_CAPABILITIES_MAX 32

int mod_api_capability_name_valid(const char* name);
int mod_api_has_capability(const char* name);
int mod_api_capability_count(void);
const char* mod_api_capability_at(int index);

/*
 * Returns nonzero when a manifest may load on this API. required_capabilities
 * points to required_capability_count consecutive fixed-width
 * MOD_API_CAPABILITY_NAME_MAX-byte slots, so callers never hand this routine
 * owner-unsafe pointers into parsed manifest storage.
 */
int mod_api_check_compatibility(
    int required_major,
    int minimum_revision,
    const char* required_capabilities,
    int required_capability_count,
    char* error,
    size_t error_capacity
);

#ifdef __cplusplus
}
#endif

#endif
