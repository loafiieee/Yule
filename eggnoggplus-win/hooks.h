#pragma once

#include <stddef.h>
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
int hooks_console_mousebutton(int x, int y, int button, int down);
void hooks_console_pump(void);
void hooks_console_on_pre_swap(void);
void hooks_online_on_pre_swap(void);

// Mods menu input handling
int hooks_mods_menu_keydown(int sym, int scancode, int mod);
int hooks_mods_menu_control_action(int action);
int hooks_mods_menu_active(void);
void hooks_mods_menu_notify_reload(void);

// Built-in online hub input handling
int hooks_online_hub_keydown(int sym, int scancode, int mod);
int hooks_online_hub_textinput(const char* text);
int hooks_online_hub_mousebutton(int x, int y, int button, int down);
int hooks_online_hub_mousemotion(int x, int y);
int hooks_online_hub_mousewheel(int y);
int hooks_online_hub_control_action(int action);
int hooks_online_hub_active(void);


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
void hooks_ui_draw_line(float x1, float y1, float x2, float y2, float line_w,
                        float r, float g, float b, float a);

void hooks_set_input_override(int player_index, uint32_t cmd_mask, int frames, int replace);
void hooks_clear_input_override(int player_index);
int hooks_get_input_override(int player_index, uint32_t* out_mask, int* out_frames, int* out_replace);

// Tick-synchronous gameplay input API.
void hooks_set_tick_input(int player_index, uint32_t cmd_mask, int ticks, int replace);
void hooks_clear_tick_input(int player_index);
int hooks_get_tick_input(int player_index, uint32_t* out_mask, int* out_ticks, int* out_replace);
void hooks_set_raw_input_blocked(int player_index, int blocked);
int hooks_get_raw_input_blocked(int player_index);
void hooks_sync_mad_ticks_to_game_clock(void);

// Command inspection helpers.
uint32_t hooks_peek_player_cmds_raw(int player_index, int mode);
uint32_t hooks_peek_player_cmds_effective(int player_index, int mode);
void hooks_block_next_game_tick(int block);
int hooks_simulate_game_ticks(int count, int arg0);
int hooks_advance_game_tick(int arg0, int run_framework_tick);

int hooks_player_colour_index(int player_index, int clothing);
int hooks_set_player_colour_index(int player_index, int clothing, int colour_index);
void hooks_set_player_body_hidden(int player_index, int hidden);
int hooks_player_body_hidden(int player_index);
void hooks_set_player_sword_idle_offset(int player_index, float x, float y);

#define HOOKS_RNG_TRACE_CAPACITY 512u

typedef struct HooksRngTraceEvent {
    uint32_t kind;
    uintptr_t caller;
} HooksRngTraceEvent;

typedef struct HooksRngTrace {
    uint32_t frame;
    uint32_t phase;
    uint32_t count;
    uint32_t overflow;
    HooksRngTraceEvent events[HOOKS_RNG_TRACE_CAPACITY];
} HooksRngTrace;

void hooks_rng_trace_begin(uint32_t frame, uint32_t phase);
void hooks_rng_trace_end(void);
void hooks_rng_trace_copy(HooksRngTrace* out_trace);
void hooks_rng_trace_describe_diff(const HooksRngTrace* expected, const HooksRngTrace* got, char* out, size_t out_cap);

int hooks_get_native_synth_enabled(void);
int hooks_set_native_synth_enabled(int enabled);

#ifdef __cplusplus
}
#endif
