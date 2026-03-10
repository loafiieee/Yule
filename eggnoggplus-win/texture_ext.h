#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RgbaImage RgbaImage;

void texture_ext_init(void);
void texture_ext_shutdown(void);

// Returns 1 after we've seen a target texture load once.
// target_path accepts:
// - "data/sprites.png" style paths
// - "sprites.png" shorthand (maps to data/sprites.png)
int texture_ext_path_loaded(const char* target_path);

// Generic registration for data/*.png replacements.
// target_path accepts the same formats as texture_ext_path_loaded.
// rel_path is relative to the mod folder.
// If override_other is 0 and another mod owns this target, this fails.
int texture_ext_register_png(
    const char* owner_mod_id,
    const char* owner_mod_folder,
    const char* target_path,
    const char* rel_path,
    int override_other,
    char* err,
    int err_sz
);

// Backward-compatible helpers for data/sprites.png.
int texture_ext_sprites_loaded(void);

int texture_ext_register_spritesheet(
    const char* owner_mod_id,
    const char* owner_mod_folder,
    const char* rel_path,
    int override_other,
    char* err,
    int err_sz
);

// Returns 1 if full_path matches any registered replacement source PNG path.
// Used by lua_manager hot-reload scanning to avoid full runtime reloads when
// only tracked texture pack PNGs change.
int texture_ext_is_tracked_path(const char* full_path);

// Polls source PNG mtimes and reloads changed replacement buffers.
// Safe to call frequently; internally rate-limited.
// Returns number of replacement PNGs reloaded in this poll.
int texture_ext_poll_hot_reload(void);

// Force-reloads all registered replacement PNGs from disk.
// out_reloaded: successfully reloaded buffers.
// out_failed: reload failures (missing file/invalid image/etc).
// out_restart_required: reloaded buffers that cannot apply live because the
// target atlas already loaded this session.
void texture_ext_reload_all(int* out_reloaded, int* out_failed, int* out_restart_required);

// Called from the rgba_load detour. Applies registered replacements matching
// the loaded path.
void texture_ext_on_rgba_load(const char* path, RgbaImage* img);

#ifdef __cplusplus
}
#endif
