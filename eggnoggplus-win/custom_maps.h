#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void custom_maps_init(void);
void custom_maps_shutdown(void);
void custom_maps_handle_mapgen_init(void (*orig_mapgen_init)(void));

/* Build a JSON array suitable for the online control server:
   [{key,selector,label,kind}, ...]. Returns bytes that would have been written,
   excluding the trailing NUL. */
int custom_maps_build_manifest_json(char* out, size_t out_sz);

/* Resolve an online map key such as "vanilla:2" or "custom:<id>:<sig>" to the
   local map selector. Returns 1 on success. */
int custom_maps_selector_for_key(const char* key, int* out_selector);

/* Total number of valid map selector values (vanilla + registered custom). */
int custom_maps_total_selectors(void);

#ifdef __cplusplus
}
#endif
