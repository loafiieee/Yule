#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Installs game-function detours (OPTIONS menu injection, etc.)
void hooks_init(void);


// Input/text capture hooks used by SDL event wrapper
int hooks_text_capture_active(void);
int hooks_text_capture_keydown(int sym, int scancode, int mod);

// Mods menu input handling
int hooks_mods_menu_keydown(int sym, int scancode, int mod);
int hooks_mods_menu_control_action(int action);
int hooks_mods_menu_active(void);

// Input override API (Lua)
void hooks_set_input_override(int player_index, uint32_t cmd_mask, int frames, int replace);
void hooks_clear_input_override(int player_index);
int hooks_get_input_override(int player_index, uint32_t* out_mask, int* out_frames, int* out_replace);
