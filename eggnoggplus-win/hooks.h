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

// Developer console input + render hooks
int hooks_console_active(void);
int hooks_console_keydown(int sym, int scancode, int mod);
int hooks_console_textinput(const char* text);
int hooks_console_control_action(int action);
int hooks_console_mousewheel(int y);
void hooks_console_pump(void);
void hooks_console_on_pre_swap(void);

// Mods menu input handling
int hooks_mods_menu_keydown(int sym, int scancode, int mod);
int hooks_mods_menu_control_action(int action);
int hooks_mods_menu_active(void);
void hooks_mods_menu_notify_reload(void);


// Custom state API (Lua)
int         hooks_register_custom_state(const char* name);
int         hooks_enter_custom_state(const char* name);
int         hooks_leave_custom_state(void);
const char* hooks_custom_state_name_for_ptr(void* state_ptr);
const char* hooks_custom_state_active_name(void);

// Lightweight immediate-mode UI helpers for custom states.
void hooks_ui_fill_rect(float x, float y, float w, float h,
                        float r, float g, float b, float a);
void hooks_ui_stroke_rect(float x, float y, float w, float h, float line_w,
                          float r, float g, float b, float a);

// Legacy online hub helpers (kept as thin wrappers).
void hooks_enter_online_hub(void);
void hooks_leave_online_hub(void);
int  hooks_online_hub_active(void);

void hooks_set_input_override(int player_index, uint32_t cmd_mask, int frames, int replace);
void hooks_clear_input_override(int player_index);
int hooks_get_input_override(int player_index, uint32_t* out_mask, int* out_frames, int* out_replace);

// Tick-synchronous gameplay input API.
void hooks_set_tick_input(int player_index, uint32_t cmd_mask, int ticks, int replace);
void hooks_clear_tick_input(int player_index);
int hooks_get_tick_input(int player_index, uint32_t* out_mask, int* out_ticks, int* out_replace);
void hooks_set_raw_input_blocked(int player_index, int blocked);
int hooks_get_raw_input_blocked(int player_index);

// Command inspection helpers.
uint32_t hooks_peek_player_cmds_raw(int player_index, int mode);
uint32_t hooks_peek_player_cmds_effective(int player_index, int mode);
void hooks_block_next_game_tick(int block);
int hooks_simulate_game_ticks(int count, int arg0);

#ifdef __cplusplus
}
#endif
