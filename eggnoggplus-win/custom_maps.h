#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void custom_maps_init(void);
void custom_maps_shutdown(void);
void custom_maps_handle_mapgen_init(void (*orig_mapgen_init)(void));

#ifdef __cplusplus
}
#endif
