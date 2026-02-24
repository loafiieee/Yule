#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Installs game-function detours (OPTIONS menu injection, etc.)
void hooks_init(void);

// Returns 1 if the framework is currently capturing text input for a string config field.
int hooks_text_capture_active(void);

// Returns 1 while the custom MODS menu state is active.
int hooks_mods_menu_active(void);

// Handle a keydown while text capture is active.
// Returns 1 if consumed (framework handled it), else 0.
int hooks_text_capture_keydown(int sym, int scancode, int mod);

// Handle keydown navigation for the custom MODS menu.
// Returns 1 if consumed, else 0.
int hooks_mods_menu_keydown(int sym, int scancode, int mod);

// Handle controller-style menu actions for the custom MODS menu.
// action: 1=up 2=down 3=left 4=right 5=activate 6=back
// Returns 1 if consumed, else 0.
int hooks_mods_menu_control_action(int action);

// Configure synthetic command-bit overrides for main_player_poll_cmds.
// player_index: 0 or 1
// cmd_mask: command bits to apply
// frames:
//   >0  apply for that many polls, then auto-clear
//    0  clear override
//   <0  hold indefinitely until cleared
// replace:
//   0 => OR cmd_mask with real commands
//   1 => replace real commands with cmd_mask
void hooks_set_input_override(int player_index, uint32_t cmd_mask, int frames, int replace);
void hooks_clear_input_override(int player_index);
int  hooks_get_input_override(int player_index, uint32_t* out_mask, int* out_frames, int* out_replace);

#ifdef __cplusplus
}
#endif
