#include <windows.h>
#include <winhttp.h>
#include <commdlg.h>
#include <shlobj.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <luajit-2.1/lua.h>
#include <luajit-2.1/lauxlib.h>
#include <luajit-2.1/lualib.h>
#include "log.h"
#include "net_ext.h"
#include "lua_manager.h"
#include "hooks.h"
#include "font_ext.h"
#include "texture_ext.h"
#include "ggpo_net.h"

static lua_State *L = NULL;
static int g_safe_env_ref = -2; /* LUA_NOREF; sandbox allowlist table ref */

typedef struct SDL_RWops SDL_RWops;
extern SDL_RWops* SDL_RWFromFile(const char* file, const char* mode);
extern const char* SDL_GetError(void);

void luna_force_crash_report(unsigned int exit_code);

// Bump this when you make breaking changes to the Lua mod API.
#define MOD_API_VERSION 1

// Config entry file format version (internal; not exposed)
// The schema/value format is the simple line-based "key: type, value" described in MODDING.md.
// We keep this as v1 until we need a breaking change.
#define MOD_CONFIG_FORMAT_VERSION 1

// Engine symbols used by the lightweight Lua UI overlay.
#define ADDR_STATE_CURRENT         0x405DB0u
#define ADDR_STATE_SWITCH          0x405DC0u
#define ADDR_MAD_W                 0x404300u
#define ADDR_MAD_H                 0x404320u
#define ADDR_PLOT_TEXT             0x4304E0u
#define ADDR_TURTLE_SET_ANGLE      0x409000u
#define ADDR_TURTLE_SET_POS        0x409040u
#define ADDR_TURTLE_SET_POS_UNSCALED 0x409040u
#define ADDR_TURTLE_SET_SCALE      0x409080u
#define ADDR_TURTLE_SET_RGB        0x4091E0u
#define ADDR_TURTLE_SET_RGBA       0x4090C0u
#define ADDR_TURTLE_RESET          0x4092D0u
#define ADDR_BUTTON_GET            0x415D00u
#define ADDR_BUTTON_SET_LAYOUT     0x415F80u
#define ADDR_BUTTON_EX             0x416310u
#define ADDR_BUTTON_COUNT          0x416890u
#define ADDR_BTN_PLAYER_FILTER     0x4325C0u
#define ADDR_MAIN_SPRITE_BATCHES_DRAW 0x431890u
#define ADDR_MAIN_BTN_FRAMED      0x432390u
#define ADDR_MAIN_PLAYER_POLL_CMDS   0x433F90u
#define ADDR_IS_POS_SOLID            0x42B770u
#define ADDR_MAP_TILES_H             0x434950u
#define ADDR_MAP_TILE                0x434A50u
#define ADDR_GAME_PLAYER_COLOUR      0x4207C0u
#define ADDR_SPRITE_BATCH_PLOT       0x405890u
#define ADDR_SPRITE_GET              0x405D20u
#define ADDR_SPRITE_COUNT            0x405D60u
#define ADDR_ATLAS_GET               0x4013E0u
#define ADDR_ATLAS_UPLOAD            0x401480u
#define ADDR_ATLAS_EXIT              0x402560u
#define ADDR_ATLAS_LOAD_SPRITESHEET  0x4023A0u
#define ADDR_SPRITES_RESET           0x405D70u
#define ADDR_LOAD_GFX                0x42FB00u
#define ADDR_FREETYPE_ATLAS          0x45B4E0u
#define ADDR_MISC_ID                 0x547B7Cu
#define ADDR_TILES_ID                0x547B80u
#define ADDR_SPRITES_ID              0x547B94u
#define ADDR_GLYPHS_ID               0x547B98u
#define ADDR_STBI_LOAD               0x4140A0u
#define ADDR_STBI_IMAGE_FREE         0x411C80u

// Built-in synth SFX entry points (reverse engineered from ghidra symbols).
#define ADDR_SOUND_SWORD_CHING       0x425D70u
#define ADDR_SOUND_NOISE             0x43BEA0u
#define ADDR_SOUND_THUMP             0x43BF60u
#define ADDR_SOUND_SHRED             0x43BF80u
#define ADDR_SOUND_PIP               0x43C030u
#define ADDR_SOUND_FM                0x43C080u
#define ADDR_SOUND_RINGMOD           0x43C0E0u
#define ADDR_SOUND_WARBLE            0x43C150u
#define ADDR_SOUND_CREEPY            0x43C3A0u
#define ADDR_SOUND_PULSE             0x43C3F0u

// SDL_mixer runtime constants (we resolve functions dynamically at runtime).
#define AUDIO_MIX_FORMAT_S16SYS      0x8010u
#define AUDIO_MIX_MAX_VOLUME         128
#define AUDIO_DEFAULT_CHANNELS       32
#define AUDIO_CHANNELS_PER_MOD       8

// Globals used by click/state-transition logic.
#define ADDR_BTN_RESET_COUNTER     0x50E3A8u

// Reverse-engineered button struct field offsets (from button_ex in ghidra).
#define BTN_OFS_CENTER_X          0x10
#define BTN_OFS_CENTER_Y          0x14
#define BTN_OFS_TEXT_SCALE_X      0xA4
#define BTN_OFS_TEXT_SCALE_Y      0xA8
#define BTN_OFS_FLAGS             0xBC
#define BTN_OFS_NOLINK_FLAG       0xBD
#define BTN_OFS_LABEL_PTR         0xC8
#define BTN_OFS_LINK_PTR          0xE0
#define BTN_OFS_ACTION_PTR        0xE4

// Bits in the 0xBC flags field that affect focus/navigation in menu logic.
#define BTN_FLAG_NOCLICK          0x00000100u
#define BTN_FLAG_NOCLICKTHRU      0x00000200u
#define BTN_FLAG_NOEMPTYCLICK     0x00000400u

// Known game state addresses (base game).
#define ADDR_ERROR_STATE           0x448204u
#define ADDR_GAME_STATE            0x448220u
#define ADDR_MAIN_STATE_INITIAL    0x448340u
#define ADDR_MAIN_STATE            0x448350u
#define ADDR_OPTIONS_STATE_PAUSED  0x448388u
#define ADDR_OPTIONS_STATE         0x448398u
#define ADDR_PREGAME_STATE         0x4483A8u
#define ADDR_REMAP_STATE2          0x4483B8u
#define ADDR_REMAP_STATE1          0x4483C8u

// Gameplay globals (from bundled ghidra symbols).
#define ADDR_MAP_SELECTOR          0x55A2F4u
#define ADDR_MAP_MODE              0x55A2F8u
#define ADDR_ROUND_END_ANY         0x55A304u
#define ADDR_SCORE_TARGET          0x55A30Cu
#define ADDR_ARMED_RESPAWN_LIMIT   0x55A310u
#define ADDR_SCORE_P0              0x55A314u
#define ADDR_SCORE_P1              0x55A318u
#define ADDR_GAME_ACTIVE_ROOM      0x541E08u
#define ADDR_LEADER                0x541E0Cu
#define ADDR_CROWD_SOUND_LAST_TICK 0x541E40u
/*
 * Wall-clock (_mad_ticks) sound-effect debounce timers that the game packs into
 * the transient region. Each is set to _mad_ticks (real time, NOT synced between
 * peers) when its sfx plays, so they diverge between peers and shift whenever
 * sound is toggled - the "changed=transient" desync. CROWD_SOUND_LAST_TICK above
 * (_last_.14582 @0x541E40) was already canonicalized; these five were missed.
 * Grouped into two contiguous runs (the gaps are dead space / already-zeroed fx).
 *   A: 0x541E10 _last_.15333 (sword-block) + 0x541E14 _last_.14917 (creepy ambience)
 *   B: 0x541EF8 _last_.15487 (sword_ching per-tick) + 0x541EFC _last_.15039
 *      + 0x541F00 _last_.14573 (do_cheer)
 */
#define ADDR_SOUND_DEDUP_TIMERS_A     0x541E10u
#define SOUND_DEDUP_TIMERS_A_BYTES    0x8u
#define ADDR_SOUND_DEDUP_TIMERS_B     0x541EF8u
#define SOUND_DEDUP_TIMERS_B_BYTES    0xCu
#define ADDR_WATERFALL_FX          0x541E44u
#define ADDR_CHANT_STEP            0x541F60u
#define ADDR_CHANT_TIMER           0x541F64u
#define ADDR_CROWD_TIMER           0x541F68u
#define ADDR_THING_COUNT           0x54202Cu
#define ADDR_THINGS_ALLOCATED      0x542034u
#define ADDR_WATERFALL_COUNT       0x542038u
#define ADDR_GAME_STARTED          0x54203Cu
#define ADDR_LERP_TIME             0x542040u
#define ADDR_END_COUNTDOWN         0x542044u
#define ADDR_START_COUNTDOWN       0x542048u
#define ADDR_GAME_LEVEL            0x542054u
#define ADDR_PLAYER_ARRAY          0x542058u
#define ADDR_CONTROLLER            0x542060u
#define ADDR_LOSER                 0x542064u
#define ADDR_SCORE_SHUDDER         0x542068u
#define ADDR_SEED                  0x542074u
#define ADDR_THING_LATEST          0x54204cu
#define ADDR_GAME_DO_LERP_COLOURS  0x542050u
/*
 * Room colour-transition lerp value storage lives contiguously in the
 * transient block between crowd_timer (0x541F68+4) and thing_count (0x54202C).
 * These are the background/water/foreground colour floats interpolated during
 * room transitions (DAT_00541F70..DAT_00542028 in the decompile). Purely
 * cosmetic - excluded from the rollback checksum so render/timing drift does
 * not trigger false-positive desyncs.
 */
#define ADDR_COLOUR_LERP_BLOCK     0x541F70u
#define COLOUR_LERP_BLOCK_BYTES    (0x54202Cu - 0x541F70u)
#define ADDR_THINGS                0x542080u
#define ADDR_THING_INFO            0x543640u
#define ADDR_ROOM_INFO             0x543700u
#define ADDR_PARTICLE_STATE        0x526420u
#define ADDR_MRAND_SEED            0x496DA0u
#define ADDR_GAME_TICKS            0x547BA0u
#define ADDR_ROOM_W                0x55A3A4u
#define ADDR_TILEMAP_DATA_PTR      0x54A1E4u
#define ADDR_TILEMAP_W             0x54A1E8u
#define ADDR_TILEMAP_H             0x54A1ECu
#define ADDR_TILE_W                0x54A1F0u
#define ADDR_TILE_H                0x54A1F4u
#define ADDR_TILEMAP_PIXELS_W      0x54A200u
#define ADDR_TILEMAP_PIXELS_H      0x54A204u
#define ADDR_ROOMDEF_COUNT         0x54A360u
#define ADDR_ROOM_PIXEL_W          0x55AB34u
#define ADDR_CAMERA_X              0x55A360u
#define ADDR_CAMERA_Y              0x55A364u
#define ADDR_CAMERA_SHAKE          0x55A37Cu
#define ADDR_CAMERA_SHAKE_DECAY    0x55A380u
#define ADDR_GAME_W                0x55A394u
#define ADDR_GAME_H                0x55A324u
#define ADDR_MAP_H                 0x55A3A0u
#define ADDR_MAP_W                 0x55A3A8u
#define ADDR_GAME_OLD_ACTIVE_ROOM  0x448330u
#define ADDR_RESUMED               0x448334u
#define ADDR_FREEZE                0x542070u
#define ADDR_PLAYER_MODE0          0x55A180u
#define ADDR_PLAYER_MODE1          0x55A184u
#define ADDR_TRANSIENT_GAME_STATE  0x541E00u
#define TRANSIENT_GAME_STATE_SIZE  0x480u

typedef void* (__cdecl *fn_state_current_t)(void);
typedef void* (__cdecl *fn_state_switch_t)(void*);
typedef float (__cdecl *fn_mad_dim_t)(void);
typedef void  (__cdecl *fn_plot_text_t)(const char*, int);
typedef void  (__cdecl *fn_turtle_set_angle_t)(double);
typedef void  (__cdecl *fn_turtle_set_pos_t)(double, double);
typedef void  (__cdecl *fn_turtle_set_pos_unscaled_t)(double, double);
typedef void  (__cdecl *fn_turtle_set_scale_t)(double, double);
typedef void  (__cdecl *fn_turtle_set_rgb_t)(float, float, float);
typedef void  (__cdecl *fn_turtle_set_rgba_t)(float, float, float, float);
typedef void  (__cdecl *fn_turtle_reset_t)(void);
typedef void* (__cdecl *fn_button_get_t)(int);
typedef void  (__cdecl *fn_button_set_layout_t)(float, float);
typedef void* (__cdecl *fn_button_ex_t)(float, float, uint32_t, const char*, int);
typedef int   (__cdecl *fn_button_count_t)(void);
typedef int   (__cdecl *fn_btn_player_filter_t)(void* btn, int event_code);
typedef void  (__cdecl *fn_main_sprite_batches_draw_t)(void);
typedef int   (__cdecl *fn_main_btn_framed_t)(int btn_ptr, int event_code);
typedef void  (__cdecl *fn_button_set_w_ex_t)(int, float, float);
typedef void  (__cdecl *fn_button_set_h_ex_t)(int, float, float);
typedef uint32_t (__cdecl *fn_main_player_poll_cmds_t)(uint32_t, uint32_t);
typedef int   (__cdecl *fn_is_pos_solid_t)(float, float);
typedef int   (__cdecl *fn_map_tiles_h_t)(void);
typedef int   (__cdecl *fn_map_tile_t)(int, int);
typedef void  (__cdecl *fn_player_die_t)(int player_ptr);
typedef unsigned char* (__cdecl *fn_map_coord_tile_t)(float x, float y);
typedef void  (__cdecl *fn_game_player_colour_t)(float*, uint32_t, int);
typedef void  (__cdecl *fn_sprite_batch_plot_t)(int sprite_ptr, int flip, int layer);
typedef void* (__cdecl *fn_sprite_get_t)(uint32_t sprite_id);
typedef int   (__cdecl *fn_sprite_count_t)(void);
typedef int   (__cdecl *fn_atlas_get_t)(int);
typedef int   (__cdecl *fn_atlas_upload_t)(int, int, int);
typedef void  (__cdecl *fn_atlas_exit_t)(void);
typedef int   (__cdecl *fn_atlas_load_spritesheet_t)(int, int*, int, int, int, uint32_t, char*);
typedef void  (__cdecl *fn_sprites_reset_t)(void);
typedef int   (__cdecl *fn_load_gfx_t)(void);
typedef unsigned char* (__cdecl *fn_stbi_load_t)(const char*, int*, int*, int*, int);
typedef void  (__cdecl *fn_stbi_image_free_t)(void*);
typedef void* (__cdecl *fn_sound_sword_ching_t)(float, float);
typedef void  (__cdecl *fn_sound_noise_t)(float, int);
typedef void  (__cdecl *fn_sound_thump_t)(float);
typedef void  (__cdecl *fn_sound_shred_t)(float, int);
typedef void  (__cdecl *fn_sound_pip_t)(float, int);
typedef void  (__cdecl *fn_sound_fm_t)(float, float, float);
typedef void  (__cdecl *fn_sound_ringmod_t)(float, int);
typedef void  (__cdecl *fn_sound_warble_t)(float);
typedef void  (__cdecl *fn_sound_creepy_t)(float);
typedef void  (__cdecl *fn_sound_pulse_t)(float, int);

typedef struct Mix_Chunk Mix_Chunk;
typedef struct Mix_Music Mix_Music;

typedef int       (__cdecl *fn_mix_open_audio_t)(int, unsigned short, int, int);
typedef void      (__cdecl *fn_mix_close_audio_t)(void);
typedef int       (__cdecl *fn_mix_allocate_channels_t)(int);
typedef Mix_Chunk*(__cdecl *fn_mix_load_wav_rw_t)(SDL_RWops*, int);
typedef int       (__cdecl *fn_mix_play_channel_timed_t)(int, Mix_Chunk*, int, int);
typedef int       (__cdecl *fn_mix_playing_t)(int);
typedef int       (__cdecl *fn_mix_halt_channel_t)(int);
typedef int       (__cdecl *fn_mix_volume_channel_t)(int, int);
typedef void      (__cdecl *fn_mix_free_chunk_t)(Mix_Chunk*);
typedef Mix_Music*(__cdecl *fn_mix_load_mus_t)(const char*);
typedef int       (__cdecl *fn_mix_play_music_t)(Mix_Music*, int);
typedef int       (__cdecl *fn_mix_halt_music_t)(void);
typedef int       (__cdecl *fn_mix_volume_music_t)(int);
typedef void      (__cdecl *fn_mix_free_music_t)(Mix_Music*);
typedef const char* (__cdecl *fn_mix_get_error_t)(void);
typedef BOOL (WINAPI *fn_play_sound_a_t)(LPCSTR, HMODULE, DWORD);

static fn_state_current_t   p_state_current   = (fn_state_current_t)(uintptr_t)ADDR_STATE_CURRENT;
static fn_state_switch_t    p_state_switch    = (fn_state_switch_t)(uintptr_t)ADDR_STATE_SWITCH;
static fn_mad_dim_t         p_mad_w           = (fn_mad_dim_t)(uintptr_t)ADDR_MAD_W;
static fn_mad_dim_t         p_mad_h           = (fn_mad_dim_t)(uintptr_t)ADDR_MAD_H;
static fn_plot_text_t       p_plot_text       = (fn_plot_text_t)(uintptr_t)ADDR_PLOT_TEXT;
static fn_turtle_set_angle_t p_turtle_set_angle = (fn_turtle_set_angle_t)(uintptr_t)ADDR_TURTLE_SET_ANGLE;
static fn_turtle_set_pos_t  p_turtle_set_pos  = (fn_turtle_set_pos_t)(uintptr_t)ADDR_TURTLE_SET_POS;
static fn_turtle_set_pos_unscaled_t p_turtle_set_pos_unscaled = (fn_turtle_set_pos_unscaled_t)(uintptr_t)ADDR_TURTLE_SET_POS_UNSCALED;
static fn_turtle_set_scale_t p_turtle_set_scale = (fn_turtle_set_scale_t)(uintptr_t)ADDR_TURTLE_SET_SCALE;
static fn_turtle_set_rgb_t  p_turtle_set_rgb  = (fn_turtle_set_rgb_t)(uintptr_t)ADDR_TURTLE_SET_RGB;
static fn_turtle_set_rgba_t p_turtle_set_rgba = (fn_turtle_set_rgba_t)(uintptr_t)ADDR_TURTLE_SET_RGBA;
static fn_turtle_reset_t    p_turtle_reset    = (fn_turtle_reset_t)(uintptr_t)ADDR_TURTLE_RESET;
static fn_button_get_t      p_button_get      = (fn_button_get_t)(uintptr_t)ADDR_BUTTON_GET;
static fn_button_set_layout_t p_button_set_layout = (fn_button_set_layout_t)(uintptr_t)ADDR_BUTTON_SET_LAYOUT;
static fn_button_ex_t       p_button_ex       = (fn_button_ex_t)(uintptr_t)ADDR_BUTTON_EX;
static fn_button_count_t    p_button_count    = (fn_button_count_t)(uintptr_t)ADDR_BUTTON_COUNT;
static fn_btn_player_filter_t p_btn_player_filter = (fn_btn_player_filter_t)(uintptr_t)ADDR_BTN_PLAYER_FILTER;
static fn_main_sprite_batches_draw_t p_main_sprite_batches_draw = (fn_main_sprite_batches_draw_t)(uintptr_t)ADDR_MAIN_SPRITE_BATCHES_DRAW;
static fn_main_btn_framed_t   p_main_btn_framed   = (fn_main_btn_framed_t)(uintptr_t)ADDR_MAIN_BTN_FRAMED;
static fn_button_set_w_ex_t   p_button_set_w_ex   = (fn_button_set_w_ex_t)(uintptr_t)0x4161a0u;
static fn_button_set_h_ex_t   p_button_set_h_ex   = (fn_button_set_h_ex_t)(uintptr_t)0x416230u;
static fn_main_player_poll_cmds_t p_main_player_poll_cmds = (fn_main_player_poll_cmds_t)(uintptr_t)ADDR_MAIN_PLAYER_POLL_CMDS;
static fn_is_pos_solid_t     p_is_pos_solid     = (fn_is_pos_solid_t)(uintptr_t)ADDR_IS_POS_SOLID;
static fn_map_tiles_h_t      p_map_tiles_h       = (fn_map_tiles_h_t)(uintptr_t)ADDR_MAP_TILES_H;
static fn_map_tile_t         p_map_tile          = (fn_map_tile_t)(uintptr_t)ADDR_MAP_TILE;
/* Custom-content (World Control) native helpers. player_die: cdecl(player_ptr);
   map_coord_tile: cdecl(float x, float y) -> byte* to the tile id at a world pos. */
static fn_player_die_t       p_player_die        = (fn_player_die_t)(uintptr_t)0x00422830u;
static fn_map_coord_tile_t   p_map_coord_tile    = (fn_map_coord_tile_t)(uintptr_t)0x00434B30u;
static fn_game_player_colour_t p_game_player_colour = (fn_game_player_colour_t)(uintptr_t)ADDR_GAME_PLAYER_COLOUR;
static fn_sprite_batch_plot_t p_sprite_batch_plot = (fn_sprite_batch_plot_t)(uintptr_t)ADDR_SPRITE_BATCH_PLOT;
static fn_sprite_get_t        p_sprite_get        = (fn_sprite_get_t)(uintptr_t)ADDR_SPRITE_GET;
static fn_sprite_count_t      p_sprite_count      = (fn_sprite_count_t)(uintptr_t)ADDR_SPRITE_COUNT;
static fn_atlas_get_t         p_atlas_get         = (fn_atlas_get_t)(uintptr_t)ADDR_ATLAS_GET;
static fn_atlas_upload_t      p_atlas_upload      = (fn_atlas_upload_t)(uintptr_t)ADDR_ATLAS_UPLOAD;
static fn_atlas_exit_t        p_atlas_exit        = (fn_atlas_exit_t)(uintptr_t)ADDR_ATLAS_EXIT;
static fn_atlas_load_spritesheet_t p_atlas_load_spritesheet = (fn_atlas_load_spritesheet_t)(uintptr_t)ADDR_ATLAS_LOAD_SPRITESHEET;
static fn_sprites_reset_t     p_sprites_reset     = (fn_sprites_reset_t)(uintptr_t)ADDR_SPRITES_RESET;
static fn_load_gfx_t          p_load_gfx          = (fn_load_gfx_t)(uintptr_t)ADDR_LOAD_GFX;
static fn_stbi_load_t         p_stbi_load         = (fn_stbi_load_t)(uintptr_t)ADDR_STBI_LOAD;
static fn_stbi_image_free_t   p_stbi_image_free   = (fn_stbi_image_free_t)(uintptr_t)ADDR_STBI_IMAGE_FREE;
static fn_sound_sword_ching_t p_sound_sword_ching = (fn_sound_sword_ching_t)(uintptr_t)ADDR_SOUND_SWORD_CHING;
static fn_sound_noise_t      p_sound_noise      = (fn_sound_noise_t)(uintptr_t)ADDR_SOUND_NOISE;
static fn_sound_thump_t      p_sound_thump      = (fn_sound_thump_t)(uintptr_t)ADDR_SOUND_THUMP;
static fn_sound_shred_t      p_sound_shred      = (fn_sound_shred_t)(uintptr_t)ADDR_SOUND_SHRED;
static fn_sound_pip_t        p_sound_pip        = (fn_sound_pip_t)(uintptr_t)ADDR_SOUND_PIP;
static fn_sound_fm_t         p_sound_fm         = (fn_sound_fm_t)(uintptr_t)ADDR_SOUND_FM;
static fn_sound_ringmod_t    p_sound_ringmod    = (fn_sound_ringmod_t)(uintptr_t)ADDR_SOUND_RINGMOD;
static fn_sound_warble_t     p_sound_warble     = (fn_sound_warble_t)(uintptr_t)ADDR_SOUND_WARBLE;
static fn_sound_creepy_t     p_sound_creepy     = (fn_sound_creepy_t)(uintptr_t)ADDR_SOUND_CREEPY;
static fn_sound_pulse_t      p_sound_pulse      = (fn_sound_pulse_t)(uintptr_t)ADDR_SOUND_PULSE;

static fn_mix_open_audio_t        p_mix_open_audio = NULL;
static fn_mix_close_audio_t       p_mix_close_audio = NULL;
static fn_mix_allocate_channels_t p_mix_allocate_channels = NULL;
static fn_mix_load_wav_rw_t       p_mix_load_wav_rw = NULL;
static fn_mix_play_channel_timed_t p_mix_play_channel_timed = NULL;
static fn_mix_playing_t           p_mix_playing = NULL;
static fn_mix_halt_channel_t      p_mix_halt_channel = NULL;
static fn_mix_volume_channel_t    p_mix_volume_channel = NULL;
static fn_mix_free_chunk_t        p_mix_free_chunk = NULL;
static fn_mix_load_mus_t          p_mix_load_mus = NULL;
static fn_mix_play_music_t        p_mix_play_music = NULL;
static fn_mix_halt_music_t        p_mix_halt_music = NULL;
static fn_mix_volume_music_t      p_mix_volume_music = NULL;
static fn_mix_free_music_t        p_mix_free_music = NULL;
static fn_mix_get_error_t         p_mix_get_error = NULL;
static fn_play_sound_a_t          p_play_sound_a = NULL;

static volatile int* p_btn_reset_counter = (volatile int*)(uintptr_t)ADDR_BTN_RESET_COUNTER;
static volatile int* p_map_selector = (volatile int*)(uintptr_t)ADDR_MAP_SELECTOR;
static volatile int* p_map_mode = (volatile int*)(uintptr_t)ADDR_MAP_MODE;
static volatile int* p_round_end_any = (volatile int*)(uintptr_t)ADDR_ROUND_END_ANY;
static volatile int* p_score_target = (volatile int*)(uintptr_t)ADDR_SCORE_TARGET;
static volatile int* p_armed_respawn_limit = (volatile int*)(uintptr_t)ADDR_ARMED_RESPAWN_LIMIT;
static volatile int* p_score_p0 = (volatile int*)(uintptr_t)ADDR_SCORE_P0;
static volatile int* p_score_p1 = (volatile int*)(uintptr_t)ADDR_SCORE_P1;
static volatile int* p_game_active_room = (volatile int*)(uintptr_t)ADDR_GAME_ACTIVE_ROOM;
static volatile uintptr_t* p_game_leader = (volatile uintptr_t*)(uintptr_t)ADDR_LEADER;
static volatile uint32_t* p_crowd_sound_last_tick = (volatile uint32_t*)(uintptr_t)ADDR_CROWD_SOUND_LAST_TICK;
static volatile uintptr_t* p_waterfall_fx = (volatile uintptr_t*)(uintptr_t)ADDR_WATERFALL_FX;
static volatile int* p_chant_step = (volatile int*)(uintptr_t)ADDR_CHANT_STEP;
static volatile int* p_chant_timer = (volatile int*)(uintptr_t)ADDR_CHANT_TIMER;
static volatile int* p_crowd_timer = (volatile int*)(uintptr_t)ADDR_CROWD_TIMER;
static volatile int* p_native_thing_count = (volatile int*)(uintptr_t)ADDR_THING_COUNT;
static volatile int* p_things_allocated = (volatile int*)(uintptr_t)ADDR_THINGS_ALLOCATED;
static volatile int* p_waterfall_count = (volatile int*)(uintptr_t)ADDR_WATERFALL_COUNT;
static volatile int* p_game_started = (volatile int*)(uintptr_t)ADDR_GAME_STARTED;
static volatile int* p_lerp_time = (volatile int*)(uintptr_t)ADDR_LERP_TIME;
static volatile int* p_end_countdown = (volatile int*)(uintptr_t)ADDR_END_COUNTDOWN;
static volatile int* p_start_countdown = (volatile int*)(uintptr_t)ADDR_START_COUNTDOWN;
static volatile uint32_t* p_game_level = (volatile uint32_t*)(uintptr_t)ADDR_GAME_LEVEL;
static volatile uintptr_t* p_controller = (volatile uintptr_t*)(uintptr_t)ADDR_CONTROLLER;
static volatile uintptr_t* p_loser = (volatile uintptr_t*)(uintptr_t)ADDR_LOSER;
static volatile int* p_score_shudder = (volatile int*)(uintptr_t)ADDR_SCORE_SHUDDER;
static volatile uint32_t* p_seed = (volatile uint32_t*)(uintptr_t)ADDR_SEED;
static volatile int* p_room_w = (volatile int*)(uintptr_t)ADDR_ROOM_W;
static volatile uintptr_t* p_tilemap_data_ptr = (volatile uintptr_t*)(uintptr_t)ADDR_TILEMAP_DATA_PTR;
static volatile int* p_tilemap_w = (volatile int*)(uintptr_t)ADDR_TILEMAP_W;
static volatile int* p_tilemap_h = (volatile int*)(uintptr_t)ADDR_TILEMAP_H;
static volatile int* p_tile_w_native = (volatile int*)(uintptr_t)ADDR_TILE_W;
static volatile int* p_tile_h_native = (volatile int*)(uintptr_t)ADDR_TILE_H;
static volatile int* p_tilemap_pixels_w = (volatile int*)(uintptr_t)ADDR_TILEMAP_PIXELS_W;
static volatile int* p_tilemap_pixels_h = (volatile int*)(uintptr_t)ADDR_TILEMAP_PIXELS_H;
static volatile int* p_roomdef_count = (volatile int*)(uintptr_t)ADDR_ROOMDEF_COUNT;
static volatile int* p_room_pixel_w = (volatile int*)(uintptr_t)ADDR_ROOM_PIXEL_W;
static volatile uint32_t* p_mrand_seed = (volatile uint32_t*)(uintptr_t)ADDR_MRAND_SEED;
static volatile uint32_t* p_native_game_ticks = (volatile uint32_t*)(uintptr_t)ADDR_GAME_TICKS;
static volatile int* p_misc_id = (volatile int*)(uintptr_t)ADDR_MISC_ID;
static volatile int* p_tiles_id = (volatile int*)(uintptr_t)ADDR_TILES_ID;
static volatile int* p_sprites_id = (volatile int*)(uintptr_t)ADDR_SPRITES_ID;
static volatile int* p_glyphs_id = (volatile int*)(uintptr_t)ADDR_GLYPHS_ID;
static volatile uintptr_t* p_freetype_atlas = (volatile uintptr_t*)(uintptr_t)ADDR_FREETYPE_ATLAS;
static volatile float* p_camera_x = (volatile float*)(uintptr_t)ADDR_CAMERA_X;
static volatile float* p_camera_y = (volatile float*)(uintptr_t)ADDR_CAMERA_Y;
static volatile float* p_camera_shake = (volatile float*)(uintptr_t)ADDR_CAMERA_SHAKE;
static volatile float* p_camera_shake_decay = (volatile float*)(uintptr_t)ADDR_CAMERA_SHAKE_DECAY;
static volatile float* p_game_w = (volatile float*)(uintptr_t)ADDR_GAME_W;
static volatile float* p_game_h = (volatile float*)(uintptr_t)ADDR_GAME_H;
static volatile int* p_map_h = (volatile int*)(uintptr_t)ADDR_MAP_H;
static volatile int* p_map_w = (volatile int*)(uintptr_t)ADDR_MAP_W;
static volatile int* p_game_old_active_room = (volatile int*)(uintptr_t)ADDR_GAME_OLD_ACTIVE_ROOM;
static volatile int* p_resumed = (volatile int*)(uintptr_t)ADDR_RESUMED;
static volatile int* p_freeze = (volatile int*)(uintptr_t)ADDR_FREEZE;
static volatile int* p_player_mode0 = (volatile int*)(uintptr_t)ADDR_PLAYER_MODE0;
static volatile int* p_player_mode1 = (volatile int*)(uintptr_t)ADDR_PLAYER_MODE1;
static uint8_t* p_transient_game_state = (uint8_t*)(uintptr_t)ADDR_TRANSIENT_GAME_STATE;
static uintptr_t* p_player_slots = (uintptr_t*)(uintptr_t)ADDR_PLAYER_ARRAY;
static volatile int* p_thing_latest = (volatile int*)(uintptr_t)ADDR_THING_LATEST;
static volatile int* p_game_do_lerp_colours = (volatile int*)(uintptr_t)ADDR_GAME_DO_LERP_COLOURS;
static uint8_t* p_things = (uint8_t*)(uintptr_t)ADDR_THINGS;
static uint8_t* p_thing_info = (uint8_t*)(uintptr_t)ADDR_THING_INFO;
static uint8_t* p_room_info_state = (uint8_t*)(uintptr_t)ADDR_ROOM_INFO;
static uint8_t* p_particle_state = (uint8_t*)(uintptr_t)ADDR_PARTICLE_STATE;

typedef struct UiHitBox {
    float x;
    float y;
    float w;
    float h;
} UiHitBox;

typedef struct UiLayout {
    int active;
    float cursor_x;
    float cursor_y;
    float row_h;
    float gap;
    float width;
    float text_scale;
} UiLayout;

typedef struct UiNativeButton {
    char id[64];
    char state_name[32];
    char label[128];
    float grid_x;
    float grid_y;
    float layout_x;
    float layout_y;
    void* btn_ptr;
    int clicked;
    int warned_non_menu;
} UiNativeButton;

// =============================
// Data model
// =============================

typedef struct LayoutHandler {
    char state_name[32];
    int  ref;
} LayoutHandler;

typedef struct LuaRefList {
    int* refs;
    int  count;
    int  cap;
} LuaRefList;

typedef struct AudioChunkCacheEntry {
    char path[MAX_PATH];
    Mix_Chunk* chunk;
} AudioChunkCacheEntry;

typedef struct FontRegistration {
    uint8_t byte_value;
    char relpath[128];
} FontRegistration;

typedef struct TextureRegistration {
    char target[128];
    char relpath[128];
} TextureRegistration;

typedef struct ModAssetSheet {
    char id[64];
    char relpath[MAX_PATH];
    char fullpath[MAX_PATH];
    int base_id;
    int count;
    int cell_w;
    int cell_h;
    int padding;
    uint32_t flags;
} ModAssetSheet;

#define MOD_COLOR_MASK_MAX_LAYERS 8
#define MOD_COLOR_MASK_MAX_COLORS 128
#define MOD_COLOR_MASK_MAX_PIXELS (2048u * 2048u)

typedef struct ModColorMaskLayer {
    char name[32];
    uint32_t colors[MOD_COLOR_MASK_MAX_COLORS];
    int color_count;
    unsigned int matched_pixels;
    char relpath[MAX_PATH];
    char fullpath[MAX_PATH];
} ModColorMaskLayer;

typedef struct ModPerfCounter {
    unsigned int call_count;
    double total_ms;
    double last_ms;
    double max_ms;
} ModPerfCounter;

// Forward declarations for helper functions used before definition
typedef struct LoadedMod LoadedMod;

static void reflist_clear(lua_State* Ls, LuaRefList* list);
static int ui_engine_button_exists(void* btn_ptr);
static void ui_button_apply_flags_hidden(void* btn_ptr, int hidden);
static int reload_engine_gfx_atlases(const char* reason);
static void ui_reset_render_state(void);
static int ptr_readable(const void* p, SIZE_T len);
static LoadedMod* get_mod_by_index(int mod_index);
static LoadedMod* get_mod_by_id_ci(const char* mod_id);


#define MOD_ID_LIST_MAX 32
#define MOD_DEP_RANGE_MAX 96
#define MOD_STORAGE_KEY_MAX 64
#define MOD_STORAGE_STR_MAX 256
#define MOD_INTEROP_NAMESPACE_MAX 96

typedef struct ModIdList {
    char ids[MOD_ID_LIST_MAX][64];
    int count;
} ModIdList;

typedef struct ModDepSpec {
    char id[64];
    char range[MOD_DEP_RANGE_MAX];
} ModDepSpec;

typedef struct ModDepList {
    ModDepSpec items[MOD_ID_LIST_MAX];
    int count;
} ModDepList;

struct LoadedMod {
    char id[64];
    char name[64];
    char version[32];
    char author[64];
    char description[256];
    char entry[128];
    char folder_path[MAX_PATH];
    int  api_version;

    int  enabled;
    int  error_count;
    int  trace_events;

    ModPerfCounter perf_frame;
    ModPerfCounter perf_tick;
    ModPerfCounter perf_event;
    ModPerfCounter perf_layout;

    ModDepList depends;
    ModDepList optional_deps;
    ModIdList conflicts;

    // Registry refs
    int  env_ref;       // mod environment table
    int  mod_ref;       // the per-mod 'mod' table
    int  on_load_ref;   // function or LUA_NOREF
    int  on_unload_ref; // function or LUA_NOREF

    LuaRefList on_frame;
    LuaRefList on_tick;
    LuaRefList on_tick_post;
    LuaRefList on_event;

    // Fired once per state entry, after the engine's button list is stable.
    LayoutHandler* on_layout;
    int on_layout_count;
    int on_layout_cap;

    // Immediate-mode UI runtime state.
    UiHitBox* ui_hitboxes;
    int ui_hitbox_count;
    int ui_hitbox_cap;
    void* ui_state_ptr;
    UiLayout ui_layout;
    UiNativeButton** ui_native_buttons;
    int ui_native_count;
    int ui_native_cap;


    // Strings allocated by the framework on behalf of this mod (e.g. button labels).
    char** ui_string_pool;
    int ui_string_count;
    int ui_string_cap;
    // =============================
    // Optional config (in-game editable)
    // =============================
    char config_rel[128];
    char config_path[MAX_PATH];

    struct ConfigEntry* cfg_entries;
    int cfg_count;
    int cfg_cap;

    struct ConfigAction* cfg_actions;
    int cfg_action_count;
    int cfg_action_cap;

    struct InputBinding* binds;
    int bind_count;
    int bind_cap;
    char binds_path[MAX_PATH];

    // Optional persistent key/value storage (saved across sessions).
    char storage_rel[128];
    char binds_rel[128];
    char storage_path[MAX_PATH];
    int storage_schema_version;
    int storage_suspend_save;
    struct StorageEntry* storage_entries;
    int storage_count;
    int storage_cap;

    // Per-mod audio state.
    float audio_sfx_volume;
    float audio_music_volume;
    int audio_channel_base;
    int audio_channel_count;
    int audio_next_channel;
    AudioChunkCacheEntry* audio_chunks;
    int audio_chunk_count;
    int audio_chunk_cap;

    FontRegistration* font_regs;
    int font_reg_count;
    int font_reg_cap;
    TextureRegistration* texture_regs;
    int texture_reg_count;
    int texture_reg_cap;

    ModAssetSheet* asset_sheets;
    int asset_sheet_count;
    int asset_sheet_cap;
    int asset_batch_depth;
    int asset_batch_dirty;
};


typedef struct ModManifest {
    char folder_name[MAX_PATH];
    char folder_path[MAX_PATH];
    char manifest_path[MAX_PATH];

    char id[64];
    char name[64];
    char version[32];
    char author[64];
    char description[256];
    char entry[128];
    char config_rel[128];
    char storage_rel[128];
    char binds_rel[128];

    int api_version;
    int allow_api_mismatch;
    int priority;

    ModDepList depends;
    ModDepList optional_deps;
    ModIdList conflicts;
    ModIdList load_before;
    ModIdList load_after;
} ModManifest;

typedef struct DiscoveredMod {
    ModManifest manifest;
    int active;
    int loaded;
} DiscoveredMod;

// =============================
// Config model
// =============================

#define CFG_MAX_OPTIONS 16
#define CFG_OPTION_LEN  64

typedef struct ConfigEntry {
    char key[64];
    char label[64];
    int  type;              // LUA_CFG_*
    char value[256];        // current value (empty for actions)
    int  has_min;
    int  has_max;
    double min_value;
    double max_value;
    // LUA_CFG_ENUM: the fixed list of allowed string options.
    char options[CFG_MAX_OPTIONS][CFG_OPTION_LEN];
    int  option_count;
} ConfigEntry;

typedef struct ConfigAction {
    char key[64];
    LuaRefList handlers;     // Lua functions
} ConfigAction;

typedef struct InputBinding {
    char key[64];
    char label[64];
    int  sym;
    int  default_sym;
    int  down;
    int  pressed;
    int  released;
    char value_name[64];
} InputBinding;

enum {
    MOD_STORAGE_BOOL = 1,
    MOD_STORAGE_NUMBER = 2,
    MOD_STORAGE_STRING = 3,
};

typedef struct StorageEntry {
    char key[MOD_STORAGE_KEY_MAX];
    int type;
    int bool_value;
    double num_value;
    char str_value[MOD_STORAGE_STR_MAX];
} StorageEntry;

typedef struct InteropProvider {
    char ns[MOD_INTEROP_NAMESPACE_MAX];
    char version[32];
    int table_ref;
    LoadedMod* owner;
} InteropProvider;

static LoadedMod* g_mods = NULL;
static int        g_mod_count = 0;
static int        g_mod_cap = 0;
static InteropProvider* g_interop_providers = NULL;
static int g_interop_provider_count = 0;
static int g_interop_provider_cap = 0;

// UI input state shared across mods.
static int g_ui_mouse_x = 0;
static int g_ui_mouse_y = 0;
static int g_ui_mouse_down_left = 0;
static int g_ui_mouse_pressed_left = 0;
static int g_ui_mouse_down_middle = 0;
static int g_ui_mouse_pressed_middle = 0;
static int g_ui_mouse_down_right = 0;
static int g_ui_mouse_pressed_right = 0;
static int g_ui_default_custom_cursor_suppressed = 0;
static int g_mod_asset_injection_active = 0;

// Runtime hot-reload state. Automatic polling is disabled by default because
// generated mod cache assets can otherwise trigger reload loops during play.
#define AUTO_HOT_RELOAD_ENABLED 0
#define HOT_RELOAD_INTERVAL_MS 1000ULL
static uint64_t g_hot_reload_signature = 0;
static ULONGLONG g_hot_reload_next_poll_ms = 0;
static uint64_t g_hot_reload_pending_signature = 0;
static uint64_t g_hot_reload_modset_signature = 0;
static int g_force_layout_refresh = 1;
static void* g_last_layout_state = (void*)-1;
static int g_last_btn_count = -1;
static int g_pending_menu_state_reset = 0;
static void* g_pending_menu_state_ptr = NULL;

// UI label strings can be referenced by engine button structs long after a mod
// unload. During hot reload, keep those allocations alive and free them only on
// final shutdown.
static int g_unloading_for_shutdown = 0;
static char** g_orphan_ui_strings = NULL;
static int g_orphan_ui_string_count = 0;
static int g_orphan_ui_string_cap = 0;
static const char g_orphan_empty_label[] = "";

// Optional SDL_mixer-backed audio runtime used by mod.audio.* APIs.
static HMODULE g_audio_mixer_module = NULL;
static int g_audio_mixer_ready = 0;
static int g_audio_mixer_disabled = 0;
static int g_audio_mixer_warned = 0;
static int g_audio_channels_reserved = 0;
static int g_audio_next_channel_base = 0;
static LoadedMod* g_audio_music_owner = NULL;
static Mix_Music* g_audio_music = NULL;
static HMODULE g_audio_winmm_module = NULL;
static int g_audio_winmm_ready = 0;
static int g_audio_winmm_warned = 0;
static LoadedMod* g_audio_fallback_music_owner = NULL;
static char g_audio_backend_error[256] = "";
static unsigned long long g_game_tick_count = 0;


#ifndef SND_SYNC
#define SND_SYNC        0x0000u
#endif
#ifndef SND_ASYNC
#define SND_ASYNC       0x0001u
#endif
#ifndef SND_NODEFAULT
#define SND_NODEFAULT   0x0002u
#endif
#ifndef SND_LOOP
#define SND_LOOP        0x0008u
#endif
#ifndef SND_NOSTOP
#define SND_NOSTOP      0x0010u
#endif
#ifndef SND_FILENAME
#define SND_FILENAME    0x00020000u
#endif

// =============================
// Mod manifest JSON parsing
// =============================

typedef struct JsonCursor {
    const char* start;
    const char* cur;
    const char* end;
    char err[256];
} JsonCursor;

static int g_allow_api_mismatch_global = -1;

static void mod_id_list_reset(ModIdList* list) {
    if (!list) return;
    list->count = 0;
}

static int mod_id_list_contains(const ModIdList* list, const char* id) {
    if (!list || !id || !id[0]) return 0;
    for (int i = 0; i < list->count; i++) {
        if (_stricmp(list->ids[i], id) == 0) return 1;
    }
    return 0;
}

static int mod_id_list_add(ModIdList* list, const char* id, const char* field, char* err, int err_sz) {
    if (!list || !id || !id[0]) {
        if (err && err_sz > 0) snprintf(err, err_sz, "manifest field \"%s\" contains an empty mod id", field ? field : "?");
        return 0;
    }
    if (mod_id_list_contains(list, id)) return 1;
    if (list->count >= MOD_ID_LIST_MAX) {
        if (err && err_sz > 0) {
            snprintf(err, err_sz, "manifest field \"%s\" has too many entries (max=%d)",
                     field ? field : "?", MOD_ID_LIST_MAX);
        }
        return 0;
    }
    snprintf(list->ids[list->count], sizeof(list->ids[list->count]), "%s", id);
    list->count++;
    return 1;
}

static void mod_dep_list_reset(ModDepList* list) {
    if (!list) return;
    list->count = 0;
}

static int mod_dep_list_find_by_id(const ModDepList* list, const char* id) {
    if (!list || !id || !id[0]) return -1;
    for (int i = 0; i < list->count; i++) {
        if (_stricmp(list->items[i].id, id) == 0) return i;
    }
    return -1;
}

static int dep_spec_split(const char* spec, char* out_id, int out_id_sz, char* out_range, int out_range_sz) {
    const char* p = spec;
    const char* at = NULL;
    const char* cmp = NULL;
    const char* id_end = NULL;
    const char* range_start = NULL;

    if (!out_id || out_id_sz <= 0 || !out_range || out_range_sz <= 0) return 0;
    out_id[0] = '\0';
    out_range[0] = '\0';
    if (!spec) return 0;

    while (*p && isspace((unsigned char)*p)) p++;
    if (!*p) return 0;
    spec = p;
    for (; *p; p++) {
        if (*p == '@') { at = p; break; }
    }

    if (at) {
        id_end = at;
        range_start = at + 1;
    } else {
        for (p = spec; *p; p++) {
            if (*p == '<' || *p == '>' || *p == '=' || *p == '!' || *p == '~' || *p == '^') {
                cmp = p;
                break;
            }
        }
        if (cmp) {
            id_end = cmp;
            range_start = cmp;
        } else {
            id_end = spec + strlen(spec);
            range_start = id_end;
        }
    }

    while (id_end > spec && isspace((unsigned char)id_end[-1])) id_end--;
    while (range_start && *range_start && isspace((unsigned char)*range_start)) range_start++;

    if (id_end <= spec) return 0;

    {
        size_t id_len = (size_t)(id_end - spec);
        if ((int)id_len >= out_id_sz) return 0;
        memcpy(out_id, spec, id_len);
        out_id[id_len] = '\0';
    }

    if (range_start && *range_start) {
        size_t range_len = strlen(range_start);
        while (range_len > 0 && isspace((unsigned char)range_start[range_len - 1])) range_len--;
        if ((int)range_len >= out_range_sz) return 0;
        memcpy(out_range, range_start, range_len);
        out_range[range_len] = '\0';
    } else {
        out_range[0] = '\0';
    }

    return 1;
}

static int mod_dep_list_add(ModDepList* list, const char* spec, const char* field, char* err, int err_sz) {
    char dep_id[64];
    char dep_range[MOD_DEP_RANGE_MAX];
    int existing = -1;

    if (!list || !spec || !spec[0]) {
        if (err && err_sz > 0) snprintf(err, err_sz, "manifest field \"%s\" contains an empty dependency spec", field ? field : "?");
        return 0;
    }
    if (!dep_spec_split(spec, dep_id, (int)sizeof(dep_id), dep_range, (int)sizeof(dep_range))) {
        if (err && err_sz > 0) snprintf(err, err_sz, "manifest field \"%s\" has invalid dependency spec \"%s\"", field ? field : "?", spec);
        return 0;
    }

    existing = mod_dep_list_find_by_id(list, dep_id);
    if (existing >= 0) {
        if (_stricmp(list->items[existing].range, dep_range) == 0) return 1;
        if (err && err_sz > 0) {
            snprintf(err, err_sz, "manifest field \"%s\" duplicates dependency \"%s\" with a different version range",
                     field ? field : "?", dep_id);
        }
        return 0;
    }

    if (list->count >= MOD_ID_LIST_MAX) {
        if (err && err_sz > 0) {
            snprintf(err, err_sz, "manifest field \"%s\" has too many entries (max=%d)",
                     field ? field : "?", MOD_ID_LIST_MAX);
        }
        return 0;
    }

    snprintf(list->items[list->count].id, sizeof(list->items[list->count].id), "%s", dep_id);
    snprintf(list->items[list->count].range, sizeof(list->items[list->count].range), "%s", dep_range);
    list->count++;
    return 1;
}

static void mod_manifest_set_defaults(ModManifest* manifest, const char* folder_name) {
    if (!manifest) return;
    memset(manifest, 0, sizeof(*manifest));
    if (!folder_name) folder_name = "unknown_mod";

    snprintf(manifest->folder_name, sizeof(manifest->folder_name), "%s", folder_name);
    snprintf(manifest->folder_path, sizeof(manifest->folder_path), "mods\\%s", folder_name);
    snprintf(manifest->manifest_path, sizeof(manifest->manifest_path), "%s\\mod.json", manifest->folder_path);

    snprintf(manifest->id, sizeof(manifest->id), "%s", folder_name);
    snprintf(manifest->name, sizeof(manifest->name), "%s", folder_name);
    snprintf(manifest->version, sizeof(manifest->version), "?.?.?");
    snprintf(manifest->author, sizeof(manifest->author), "Unknown");
    manifest->description[0] = '\0';
    manifest->entry[0] = '\0';
    manifest->config_rel[0] = '\0';
    snprintf(manifest->binds_rel, sizeof(manifest->binds_rel), "binds.cfg");
    snprintf(manifest->storage_rel, sizeof(manifest->storage_rel), "storage.cfg");
    manifest->api_version = MOD_API_VERSION;
    manifest->allow_api_mismatch = 0;
    manifest->priority = 0;
    mod_dep_list_reset(&manifest->depends);
    mod_dep_list_reset(&manifest->optional_deps);
    mod_id_list_reset(&manifest->conflicts);
    mod_id_list_reset(&manifest->load_before);
    mod_id_list_reset(&manifest->load_after);
}

static void json_cursor_set_error(JsonCursor* jc, const char* msg) {
    if (!jc || jc->err[0]) return;
    if (!msg) msg = "invalid json";
    size_t off = 0;
    if (jc->start && jc->cur && jc->cur >= jc->start) {
        off = (size_t)(jc->cur - jc->start);
    }
    snprintf(jc->err, sizeof(jc->err), "%s at byte %u", msg, (unsigned)off);
}

static void json_cursor_skip_ws(JsonCursor* jc) {
    if (!jc) return;
    while (jc->cur < jc->end) {
        char c = *jc->cur;
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        jc->cur++;
    }
}

static int json_cursor_expect_char(JsonCursor* jc, char expected, const char* what) {
    json_cursor_skip_ws(jc);
    if (!jc || jc->cur >= jc->end || *jc->cur != expected) {
        char buf[96];
        snprintf(buf, sizeof(buf), "expected %s", what ? what : "token");
        json_cursor_set_error(jc, buf);
        return 0;
    }
    jc->cur++;
    return 1;
}

static int json_hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static int json_cursor_parse_hex4(JsonCursor* jc, unsigned* out_codepoint) {
    unsigned v = 0;
    if (!jc || !out_codepoint) return 0;
    if ((jc->end - jc->cur) < 4) {
        json_cursor_set_error(jc, "invalid \\u escape");
        return 0;
    }
    for (int i = 0; i < 4; i++) {
        int hv = json_hex_value(jc->cur[i]);
        if (hv < 0) {
            json_cursor_set_error(jc, "invalid \\u escape");
            return 0;
        }
        v = (v << 4) | (unsigned)hv;
    }
    jc->cur += 4;
    *out_codepoint = v;
    return 1;
}

static int json_cursor_parse_string(JsonCursor* jc, char* out, int out_sz) {
    int capture = (out && out_sz > 0);
    int oi = 0;
    json_cursor_skip_ws(jc);

    if (capture) out[0] = '\0';
    if (!jc || jc->cur >= jc->end || *jc->cur != '"') {
        json_cursor_set_error(jc, "expected string");
        return 0;
    }

    jc->cur++; // consume opening quote
    while (jc->cur < jc->end) {
        unsigned ch = (unsigned char)*jc->cur++;
        if (ch == '"') {
            if (capture) out[oi] = '\0';
            return 1;
        }

        if (ch == '\\') {
            if (jc->cur >= jc->end) {
                json_cursor_set_error(jc, "unterminated escape sequence");
                return 0;
            }
            char esc = *jc->cur++;
            switch (esc) {
                case '"':  ch = '"';  break;
                case '\\': ch = '\\'; break;
                case '/':  ch = '/';  break;
                case 'b':  ch = '\b'; break;
                case 'f':  ch = '\f'; break;
                case 'n':  ch = '\n'; break;
                case 'r':  ch = '\r'; break;
                case 't':  ch = '\t'; break;
                case 'u': {
                    unsigned cp = 0;
                    if (!json_cursor_parse_hex4(jc, &cp)) return 0;
                    ch = (cp <= 0x7Fu) ? cp : '?';
                    break;
                }
                default:
                    json_cursor_set_error(jc, "invalid escape sequence");
                    return 0;
            }
        } else if (ch < 0x20u) {
            json_cursor_set_error(jc, "invalid control character in string");
            return 0;
        }

        if (capture) {
            if (oi + 1 >= out_sz) {
                json_cursor_set_error(jc, "string value is too long");
                return 0;
            }
            out[oi++] = (char)ch;
        }
    }

    json_cursor_set_error(jc, "unterminated string");
    return 0;
}

static int json_cursor_parse_bool(JsonCursor* jc, int* out_value) {
    json_cursor_skip_ws(jc);
    if (!jc || jc->cur >= jc->end || !out_value) return 0;
    if ((jc->end - jc->cur) >= 4 && strncmp(jc->cur, "true", 4) == 0) {
        jc->cur += 4;
        *out_value = 1;
        return 1;
    }
    if ((jc->end - jc->cur) >= 5 && strncmp(jc->cur, "false", 5) == 0) {
        jc->cur += 5;
        *out_value = 0;
        return 1;
    }
    json_cursor_set_error(jc, "expected boolean");
    return 0;
}

static int json_cursor_skip_number(JsonCursor* jc) {
    const char* p = NULL;
    if (!jc) return 0;
    p = jc->cur;
    if (p >= jc->end) {
        json_cursor_set_error(jc, "expected number");
        return 0;
    }

    if (*p == '-') p++;
    if (p >= jc->end || !isdigit((unsigned char)*p)) {
        json_cursor_set_error(jc, "expected number");
        return 0;
    }

    if (*p == '0') {
        p++;
    } else {
        while (p < jc->end && isdigit((unsigned char)*p)) p++;
    }

    if (p < jc->end && *p == '.') {
        p++;
        if (p >= jc->end || !isdigit((unsigned char)*p)) {
            json_cursor_set_error(jc, "invalid decimal number");
            return 0;
        }
        while (p < jc->end && isdigit((unsigned char)*p)) p++;
    }

    if (p < jc->end && (*p == 'e' || *p == 'E')) {
        p++;
        if (p < jc->end && (*p == '+' || *p == '-')) p++;
        if (p >= jc->end || !isdigit((unsigned char)*p)) {
            json_cursor_set_error(jc, "invalid exponent");
            return 0;
        }
        while (p < jc->end && isdigit((unsigned char)*p)) p++;
    }

    jc->cur = p;
    return 1;
}

static int json_cursor_parse_int(JsonCursor* jc, int* out_value) {
    const char* tok_start = NULL;
    char numbuf[64];
    char* endp = NULL;
    long v = 0;

    json_cursor_skip_ws(jc);
    if (!jc || !out_value) return 0;
    tok_start = jc->cur;
    if (!json_cursor_skip_number(jc)) return 0;

    for (const char* p = tok_start; p < jc->cur; p++) {
        if (*p == '.' || *p == 'e' || *p == 'E') {
            json_cursor_set_error(jc, "expected integer");
            return 0;
        }
    }

    size_t len = (size_t)(jc->cur - tok_start);
    if (len == 0 || len >= sizeof(numbuf)) {
        json_cursor_set_error(jc, "invalid integer");
        return 0;
    }
    memcpy(numbuf, tok_start, len);
    numbuf[len] = '\0';

    v = strtol(numbuf, &endp, 10);
    if (!endp || *endp != '\0') {
        json_cursor_set_error(jc, "invalid integer");
        return 0;
    }
    if (v < (long)INT_MIN || v > (long)INT_MAX) {
        json_cursor_set_error(jc, "integer out of range");
        return 0;
    }

    *out_value = (int)v;
    return 1;
}

static int json_cursor_skip_value(JsonCursor* jc);

static int json_cursor_skip_array(JsonCursor* jc) {
    if (!json_cursor_expect_char(jc, '[', "'['")) return 0;
    json_cursor_skip_ws(jc);
    if (jc->cur < jc->end && *jc->cur == ']') {
        jc->cur++;
        return 1;
    }

    while (jc->cur < jc->end) {
        if (!json_cursor_skip_value(jc)) return 0;
        json_cursor_skip_ws(jc);
        if (jc->cur < jc->end && *jc->cur == ',') {
            jc->cur++;
            continue;
        }
        if (jc->cur < jc->end && *jc->cur == ']') {
            jc->cur++;
            return 1;
        }
        json_cursor_set_error(jc, "expected ',' or ']'");
        return 0;
    }

    json_cursor_set_error(jc, "unterminated array");
    return 0;
}

static int json_cursor_skip_object(JsonCursor* jc) {
    if (!json_cursor_expect_char(jc, '{', "'{'")) return 0;
    json_cursor_skip_ws(jc);
    if (jc->cur < jc->end && *jc->cur == '}') {
        jc->cur++;
        return 1;
    }

    while (jc->cur < jc->end) {
        if (!json_cursor_parse_string(jc, NULL, 0)) return 0;
        if (!json_cursor_expect_char(jc, ':', "':'")) return 0;
        if (!json_cursor_skip_value(jc)) return 0;
        json_cursor_skip_ws(jc);
        if (jc->cur < jc->end && *jc->cur == ',') {
            jc->cur++;
            continue;
        }
        if (jc->cur < jc->end && *jc->cur == '}') {
            jc->cur++;
            return 1;
        }
        json_cursor_set_error(jc, "expected ',' or '}'");
        return 0;
    }

    json_cursor_set_error(jc, "unterminated object");
    return 0;
}

static int json_cursor_skip_value(JsonCursor* jc) {
    json_cursor_skip_ws(jc);
    if (!jc || jc->cur >= jc->end) {
        json_cursor_set_error(jc, "unexpected end of json");
        return 0;
    }

    switch (*jc->cur) {
        case '"':
            return json_cursor_parse_string(jc, NULL, 0);
        case '{':
            return json_cursor_skip_object(jc);
        case '[':
            return json_cursor_skip_array(jc);
        case 't':
            if ((jc->end - jc->cur) >= 4 && strncmp(jc->cur, "true", 4) == 0) {
                jc->cur += 4;
                return 1;
            }
            break;
        case 'f':
            if ((jc->end - jc->cur) >= 5 && strncmp(jc->cur, "false", 5) == 0) {
                jc->cur += 5;
                return 1;
            }
            break;
        case 'n':
            if ((jc->end - jc->cur) >= 4 && strncmp(jc->cur, "null", 4) == 0) {
                jc->cur += 4;
                return 1;
            }
            break;
        default:
            if (*jc->cur == '-' || isdigit((unsigned char)*jc->cur)) {
                return json_cursor_skip_number(jc);
            }
            break;
    }

    json_cursor_set_error(jc, "invalid json value");
    return 0;
}

static int json_cursor_parse_string_array(JsonCursor* jc, ModIdList* out_list, const char* field_name) {
    if (!out_list) return 0;
    mod_id_list_reset(out_list);
    if (!json_cursor_expect_char(jc, '[', "'['")) return 0;
    json_cursor_skip_ws(jc);

    if (jc->cur < jc->end && *jc->cur == ']') {
        jc->cur++;
        return 1;
    }

    while (jc->cur < jc->end) {
        char item[64];
        item[0] = '\0';
        if (!json_cursor_parse_string(jc, item, (int)sizeof(item))) return 0;
        if (!mod_id_list_add(out_list, item, field_name, jc->err, (int)sizeof(jc->err))) return 0;

        json_cursor_skip_ws(jc);
        if (jc->cur < jc->end && *jc->cur == ',') {
            jc->cur++;
            continue;
        }
        if (jc->cur < jc->end && *jc->cur == ']') {
            jc->cur++;
            return 1;
        }
        json_cursor_set_error(jc, "expected ',' or ']'");
        return 0;
    }

    json_cursor_set_error(jc, "unterminated string array");
    return 0;
}

static int json_cursor_parse_dep_array(JsonCursor* jc, ModDepList* out_list, const char* field_name) {
    if (!out_list) return 0;
    mod_dep_list_reset(out_list);
    if (!json_cursor_expect_char(jc, '[', "'['")) return 0;
    json_cursor_skip_ws(jc);

    if (jc->cur < jc->end && *jc->cur == ']') {
        jc->cur++;
        return 1;
    }

    while (jc->cur < jc->end) {
        char item[160];
        item[0] = '\0';
        if (!json_cursor_parse_string(jc, item, (int)sizeof(item))) return 0;
        if (!mod_dep_list_add(out_list, item, field_name, jc->err, (int)sizeof(jc->err))) return 0;

        json_cursor_skip_ws(jc);
        if (jc->cur < jc->end && *jc->cur == ',') {
            jc->cur++;
            continue;
        }
        if (jc->cur < jc->end && *jc->cur == ']') {
            jc->cur++;
            return 1;
        }
        json_cursor_set_error(jc, "expected ',' or ']'");
        return 0;
    }

    json_cursor_set_error(jc, "unterminated dependency array");
    return 0;
}

static int read_text_file_alloc(const char* path, char** out_text, size_t* out_len, char* err, int err_sz) {
    FILE* f = NULL;
    long sz = 0;
    size_t read_bytes = 0;
    char* buf = NULL;

    if (out_text) *out_text = NULL;
    if (out_len) *out_len = 0;
    if (!path || !out_text) {
        if (err && err_sz > 0) snprintf(err, err_sz, "invalid file read args");
        return 0;
    }

    f = fopen(path, "rb");
    if (!f) {
        if (err && err_sz > 0) snprintf(err, err_sz, "could not open %s", path);
        return 0;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        if (err && err_sz > 0) snprintf(err, err_sz, "could not seek %s", path);
        return 0;
    }

    sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        if (err && err_sz > 0) snprintf(err, err_sz, "could not size %s", path);
        return 0;
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        if (err && err_sz > 0) snprintf(err, err_sz, "could not rewind %s", path);
        return 0;
    }

    buf = (char*)malloc((size_t)sz + 1u);
    if (!buf) {
        fclose(f);
        if (err && err_sz > 0) snprintf(err, err_sz, "out of memory reading %s", path);
        return 0;
    }

    if (sz > 0) {
        read_bytes = fread(buf, 1, (size_t)sz, f);
        if (read_bytes != (size_t)sz) {
            free(buf);
            fclose(f);
            if (err && err_sz > 0) snprintf(err, err_sz, "failed to read %s", path);
            return 0;
        }
    }
    buf[(size_t)sz] = '\0';
    fclose(f);

    *out_text = buf;
    if (out_len) *out_len = (size_t)sz;
    return 1;
}

static int parse_mod_manifest_json(const char* json_text, size_t json_len, ModManifest* manifest, char* err, int err_sz) {
    JsonCursor jc;
    int saw_id = 0;
    int saw_name = 0;
    int saw_version = 0;
    int saw_author = 0;
    int saw_entry = 0;

    if (!json_text || !manifest) {
        if (err && err_sz > 0) snprintf(err, err_sz, "invalid manifest parse args");
        return 0;
    }

    memset(&jc, 0, sizeof(jc));
    jc.start = json_text;
    jc.cur = json_text;
    jc.end = json_text + json_len;
    jc.err[0] = '\0';

    if (!json_cursor_expect_char(&jc, '{', "'{'")) goto fail;
    json_cursor_skip_ws(&jc);
    if (jc.cur < jc.end && *jc.cur == '}') {
        json_cursor_set_error(&jc, "manifest must be a non-empty object");
        goto fail;
    }

    while (jc.cur < jc.end) {
        char key[64];
        key[0] = '\0';

        if (!json_cursor_parse_string(&jc, key, (int)sizeof(key))) goto fail;
        if (!json_cursor_expect_char(&jc, ':', "':'")) goto fail;

        if (_stricmp(key, "id") == 0) {
            if (!json_cursor_parse_string(&jc, manifest->id, (int)sizeof(manifest->id))) goto fail;
            saw_id = 1;
        } else if (_stricmp(key, "name") == 0) {
            if (!json_cursor_parse_string(&jc, manifest->name, (int)sizeof(manifest->name))) goto fail;
            saw_name = 1;
        } else if (_stricmp(key, "version") == 0) {
            if (!json_cursor_parse_string(&jc, manifest->version, (int)sizeof(manifest->version))) goto fail;
            saw_version = 1;
        } else if (_stricmp(key, "author") == 0) {
            if (!json_cursor_parse_string(&jc, manifest->author, (int)sizeof(manifest->author))) goto fail;
            saw_author = 1;
        } else if (_stricmp(key, "description") == 0) {
            if (!json_cursor_parse_string(&jc, manifest->description, (int)sizeof(manifest->description))) goto fail;
        } else if (_stricmp(key, "entry") == 0) {
            if (!json_cursor_parse_string(&jc, manifest->entry, (int)sizeof(manifest->entry))) goto fail;
            saw_entry = 1;
        } else if (_stricmp(key, "config") == 0) {
            if (!json_cursor_parse_string(&jc, manifest->config_rel, (int)sizeof(manifest->config_rel))) goto fail;
        } else if (_stricmp(key, "binds") == 0) {
            if (!json_cursor_parse_string(&jc, manifest->binds_rel, (int)sizeof(manifest->binds_rel))) goto fail;
        } else if (_stricmp(key, "storage") == 0) {
            if (!json_cursor_parse_string(&jc, manifest->storage_rel, (int)sizeof(manifest->storage_rel))) goto fail;
        } else if (_stricmp(key, "api_version") == 0) {
            if (!json_cursor_parse_int(&jc, &manifest->api_version)) goto fail;
        } else if (_stricmp(key, "allow_api_mismatch") == 0) {
            if (!json_cursor_parse_bool(&jc, &manifest->allow_api_mismatch)) goto fail;
        } else if (_stricmp(key, "priority") == 0) {
            if (!json_cursor_parse_int(&jc, &manifest->priority)) goto fail;
        } else if (_stricmp(key, "depends") == 0) {
            if (!json_cursor_parse_dep_array(&jc, &manifest->depends, "depends")) goto fail;
        } else if (_stricmp(key, "optional_deps") == 0) {
            if (!json_cursor_parse_dep_array(&jc, &manifest->optional_deps, "optional_deps")) goto fail;
        } else if (_stricmp(key, "conflicts") == 0) {
            if (!json_cursor_parse_string_array(&jc, &manifest->conflicts, "conflicts")) goto fail;
        } else if (_stricmp(key, "load_before") == 0) {
            if (!json_cursor_parse_string_array(&jc, &manifest->load_before, "load_before")) goto fail;
        } else if (_stricmp(key, "load_after") == 0) {
            if (!json_cursor_parse_string_array(&jc, &manifest->load_after, "load_after")) goto fail;
        } else {
            if (!json_cursor_skip_value(&jc)) goto fail;
        }

        json_cursor_skip_ws(&jc);
        if (jc.cur < jc.end && *jc.cur == ',') {
            jc.cur++;
            continue;
        }
        if (jc.cur < jc.end && *jc.cur == '}') {
            jc.cur++;
            break;
        }
        json_cursor_set_error(&jc, "expected ',' or '}'");
        goto fail;
    }

    json_cursor_skip_ws(&jc);
    if (jc.cur != jc.end) {
        json_cursor_set_error(&jc, "unexpected trailing data");
        goto fail;
    }

    if (!saw_id || !manifest->id[0]) {
        if (err && err_sz > 0) snprintf(err, err_sz, "missing required string field \"id\"");
        return 0;
    }
    if (!saw_name || !manifest->name[0]) {
        if (err && err_sz > 0) snprintf(err, err_sz, "missing required string field \"name\"");
        return 0;
    }
    if (!saw_version || !manifest->version[0]) {
        if (err && err_sz > 0) snprintf(err, err_sz, "missing required string field \"version\"");
        return 0;
    }
    if (!saw_author || !manifest->author[0]) {
        if (err && err_sz > 0) snprintf(err, err_sz, "missing required string field \"author\"");
        return 0;
    }
    if (!saw_entry || !manifest->entry[0]) {
        if (err && err_sz > 0) snprintf(err, err_sz, "missing required string field \"entry\"");
        return 0;
    }

    return 1;

fail:
    if (err && err_sz > 0) {
        snprintf(err, err_sz, "%s", jc.err[0] ? jc.err : "invalid mod.json");
    }
    return 0;
}

static int parse_mod_manifest_file(const char* folder_name, ModManifest* out_manifest, char* err, int err_sz) {
    char* json_text = NULL;
    size_t json_len = 0;

    if (!folder_name || !out_manifest) {
        if (err && err_sz > 0) snprintf(err, err_sz, "invalid manifest load args");
        return 0;
    }

    mod_manifest_set_defaults(out_manifest, folder_name);
    if (!read_text_file_alloc(out_manifest->manifest_path, &json_text, &json_len, err, err_sz)) {
        return 0;
    }

    int ok = parse_mod_manifest_json(json_text, json_len, out_manifest, err, err_sz);
    free(json_text);
    return ok;
}

static int global_allow_api_mismatch(void) {
    if (g_allow_api_mismatch_global >= 0) return g_allow_api_mismatch_global;
    g_allow_api_mismatch_global = 0;
    const char* env = getenv("LUNA_ALLOW_API_MISMATCH");
    if (!env || !env[0]) return g_allow_api_mismatch_global;
    if (_stricmp(env, "1") == 0 ||
        _stricmp(env, "true") == 0 ||
        _stricmp(env, "yes") == 0 ||
        _stricmp(env, "on") == 0) {
        g_allow_api_mismatch_global = 1;
    }
    return g_allow_api_mismatch_global;
}

// =============================
// Config parsing (line-based)
// Format:
//   key: bool, true
//   some_string: str, hello
//   do_thing: action
//   action_with_label: action, "Do Thing"
// =============================

static void str_trim(char* s) {
    if (!s) return;
    // left
    char* p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    // right
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[n - 1] = '\0';
        n--;
    }
}

static void str_lower(char* s) {
    for (; s && *s; s++) *s = (char)tolower((unsigned char)*s);
}

static void unquote_inplace(char* s) {
    str_trim(s);
    size_t n = strlen(s);
    if (n >= 2 && s[0] == '"' && s[n - 1] == '"') {
        // remove outer quotes
        memmove(s, s + 1, n - 2);
        s[n - 2] = '\0';
        // unescape very small set: \\ and \"
        char out[512];
        size_t oi = 0;
        for (size_t i = 0; s[i] && oi + 1 < sizeof(out); i++) {
            if (s[i] == '\\' && s[i + 1]) {
                i++;
                out[oi++] = s[i];
            } else {
                out[oi++] = s[i];
            }
        }
        out[oi] = '\0';
        strncpy(s, out, 511);
        s[511] = '\0';
    }
}

static int parse_bool(const char* s) {
    if (!s) return 0;
    if (_stricmp(s, "true") == 0) return 1;
    if (_stricmp(s, "yes") == 0) return 1;
    if (_stricmp(s, "on") == 0) return 1;
    if (strcmp(s, "1") == 0) return 1;
    return 0;
}

typedef struct SemVersion {
    int major;
    int minor;
    int patch;
    int has_prerelease;
    char prerelease[64];
} SemVersion;

static int semver_is_numeric_token(const char* s) {
    if (!s || !s[0]) return 0;
    for (const char* p = s; *p; p++) {
        if (!isdigit((unsigned char)*p)) return 0;
    }
    return 1;
}

static int semver_parse(const char* src, SemVersion* out) {
    char buf[128];
    char* p = NULL;
    char* dash = NULL;
    char* plus = NULL;
    int parts[3] = {0, 0, 0};
    int part_count = 0;

    if (!src || !out) return 0;
    memset(out, 0, sizeof(*out));

    snprintf(buf, sizeof(buf), "%s", src);
    str_trim(buf);
    if (!buf[0]) return 0;

    p = buf;
    if (p[0] == 'v' || p[0] == 'V') p++;

    plus = strchr(p, '+');
    if (plus) *plus = '\0';

    dash = strchr(p, '-');
    if (dash) {
        *dash = '\0';
        dash++;
        str_trim(dash);
        if (dash[0]) {
            out->has_prerelease = 1;
            snprintf(out->prerelease, sizeof(out->prerelease), "%s", dash);
        }
    }

    for (char* tok = strtok(p, "."); tok; tok = strtok(NULL, ".")) {
        long v;
        char* endp = NULL;
        if (part_count >= 3) return 0;
        if (!tok[0]) return 0;
        for (char* q = tok; *q; q++) {
            if (!isdigit((unsigned char)*q)) return 0;
        }
        v = strtol(tok, &endp, 10);
        if (!endp || *endp != '\0' || v < 0 || v > INT_MAX) return 0;
        parts[part_count++] = (int)v;
    }
    if (part_count <= 0) return 0;

    out->major = parts[0];
    out->minor = (part_count >= 2) ? parts[1] : 0;
    out->patch = (part_count >= 3) ? parts[2] : 0;
    return 1;
}

static int semver_compare_prerelease(const char* a, const char* b) {
    const char* pa = a ? a : "";
    const char* pb = b ? b : "";

    while (1) {
        char ta[32];
        char tb[32];
        int tai = 0;
        int tbi = 0;

        while (*pa && *pa != '.') {
            if (tai + 1 < (int)sizeof(ta)) ta[tai++] = *pa;
            pa++;
        }
        while (*pb && *pb != '.') {
            if (tbi + 1 < (int)sizeof(tb)) tb[tbi++] = *pb;
            pb++;
        }
        ta[tai] = '\0';
        tb[tbi] = '\0';

        if (!ta[0] && !tb[0]) return 0;
        if (!ta[0]) return -1;
        if (!tb[0]) return 1;

        {
            int a_num = semver_is_numeric_token(ta);
            int b_num = semver_is_numeric_token(tb);
            if (a_num && b_num) {
                long av = strtol(ta, NULL, 10);
                long bv = strtol(tb, NULL, 10);
                if (av < bv) return -1;
                if (av > bv) return 1;
            } else if (a_num && !b_num) {
                return -1;
            } else if (!a_num && b_num) {
                return 1;
            } else {
                int c = strcmp(ta, tb);
                if (c < 0) return -1;
                if (c > 0) return 1;
            }
        }

        if (*pa == '.') pa++;
        if (*pb == '.') pb++;
        if (!*pa && !*pb) return 0;
        if (!*pa && *pb) return -1;
        if (*pa && !*pb) return 1;
    }
}

static int semver_compare(const SemVersion* a, const SemVersion* b) {
    if (!a || !b) return 0;
    if (a->major != b->major) return (a->major < b->major) ? -1 : 1;
    if (a->minor != b->minor) return (a->minor < b->minor) ? -1 : 1;
    if (a->patch != b->patch) return (a->patch < b->patch) ? -1 : 1;
    if (!a->has_prerelease && !b->has_prerelease) return 0;
    if (a->has_prerelease && !b->has_prerelease) return -1;
    if (!a->has_prerelease && b->has_prerelease) return 1;
    return semver_compare_prerelease(a->prerelease, b->prerelease);
}

static int semver_eval_clause(const SemVersion* have, const char* clause) {
    char token[96];
    const char* p = clause;
    const char* ver_text = NULL;
    int op = 0; // 1==,2!=,3>,4>=,5<,6<=,7^,8~
    SemVersion want;
    SemVersion upper;
    int cmp = 0;

    if (!have || !clause) return 0;
    snprintf(token, sizeof(token), "%s", clause);
    str_trim(token);
    if (!token[0] || strcmp(token, "*") == 0 || _stricmp(token, "x") == 0) return 1;
    p = token;

    if (strncmp(p, ">=", 2) == 0) { op = 4; ver_text = p + 2; }
    else if (strncmp(p, "<=", 2) == 0) { op = 6; ver_text = p + 2; }
    else if (strncmp(p, "==", 2) == 0) { op = 1; ver_text = p + 2; }
    else if (strncmp(p, "!=", 2) == 0) { op = 2; ver_text = p + 2; }
    else if (*p == '>') { op = 3; ver_text = p + 1; }
    else if (*p == '<') { op = 5; ver_text = p + 1; }
    else if (*p == '=') { op = 1; ver_text = p + 1; }
    else if (*p == '^') { op = 7; ver_text = p + 1; }
    else if (*p == '~') { op = 8; ver_text = p + 1; }
    else { op = 1; ver_text = p; }

    if (!ver_text || !ver_text[0]) return 0;
    if (!semver_parse(ver_text, &want)) return 0;

    cmp = semver_compare(have, &want);
    if (op == 1) return cmp == 0;
    if (op == 2) return cmp != 0;
    if (op == 3) return cmp > 0;
    if (op == 4) return cmp >= 0;
    if (op == 5) return cmp < 0;
    if (op == 6) return cmp <= 0;

    if (op == 7 || op == 8) {
        upper = want;
        upper.has_prerelease = 0;
        upper.prerelease[0] = '\0';
        if (op == 7) {
            if (want.major > 0) {
                upper.major = want.major + 1;
                upper.minor = 0;
                upper.patch = 0;
            } else if (want.minor > 0) {
                upper.major = 0;
                upper.minor = want.minor + 1;
                upper.patch = 0;
            } else {
                upper.major = 0;
                upper.minor = 0;
                upper.patch = want.patch + 1;
            }
        } else {
            upper.major = want.major;
            upper.minor = want.minor + 1;
            upper.patch = 0;
        }
        return semver_compare(have, &want) >= 0 && semver_compare(have, &upper) < 0;
    }

    return 0;
}

static int semver_satisfies_range(const char* version, const char* range) {
    SemVersion have;
    char buf[192];
    char clause[96];
    int ci = 0;

    if (!range || !range[0] || strcmp(range, "*") == 0) return 1;
    if (!version || !version[0]) return 0;
    if (!semver_parse(version, &have)) return 0;

    snprintf(buf, sizeof(buf), "%s", range);
    for (size_t i = 0;; i++) {
        char c = buf[i];
        int at_end = (c == '\0');
        int is_sep = (!at_end && (c == ',' || isspace((unsigned char)c)));
        if (!at_end && !is_sep) {
            if (ci + 1 < (int)sizeof(clause)) clause[ci++] = c;
            continue;
        }

        if (ci > 0) {
            clause[ci] = '\0';
            if (!semver_eval_clause(&have, clause)) return 0;
            ci = 0;
        }
        if (at_end) break;
    }

    return 1;
}

static const char* type_to_string(int type) {
    switch (type) {
        case LUA_CFG_BOOL:   return "bool";
        case LUA_CFG_INT:    return "int";
        case LUA_CFG_FLOAT:  return "float";
        case LUA_CFG_STRING: return "str";
        case LUA_CFG_ACTION: return "action";
        case LUA_CFG_ENUM:   return "options";
        default: return "none";
    }
}

static int string_to_type(const char* t) {
    if (!t || !t[0]) return LUA_CFG_NONE;
    if (_stricmp(t, "bool") == 0 || _stricmp(t, "boolean") == 0) return LUA_CFG_BOOL;
    if (_stricmp(t, "int") == 0 || _stricmp(t, "integer") == 0) return LUA_CFG_INT;
    if (_stricmp(t, "float") == 0 || _stricmp(t, "number") == 0 || _stricmp(t, "double") == 0) return LUA_CFG_FLOAT;
    if (_stricmp(t, "str") == 0 || _stricmp(t, "string") == 0) return LUA_CFG_STRING;
    if (_stricmp(t, "action") == 0 || _stricmp(t, "button") == 0) return LUA_CFG_ACTION;
    if (_stricmp(t, "options") == 0 || _stricmp(t, "enum") == 0) return LUA_CFG_ENUM;
    return LUA_CFG_NONE;
}

// Find a value among an enum entry's options (case-insensitive). Returns the
// option index, or -1 if not present.
static int cfg_enum_find_option(const ConfigEntry* e, const char* val) {
    if (!e || !val) return -1;
    for (int i = 0; i < e->option_count; i++) {
        if (_stricmp(e->options[i], val) == 0) return i;
    }
    return -1;
}

// Force an enum entry's value to a declared option: snaps an unknown value to
// the first option and canonicalizes the spelling to match the declaration.
static void cfg_enum_snap_value(ConfigEntry* e) {
    if (!e || e->type != LUA_CFG_ENUM) return;
    if (e->option_count <= 0) { e->value[0] = '\0'; return; }
    int idx = cfg_enum_find_option(e, e->value);
    if (idx < 0) idx = 0;
    snprintf(e->value, sizeof(e->value), "%s", e->options[idx]);
}

// Split a raw "a, b, c" bracket body into an enum entry's option list.
static void cfg_parse_options(const char* inner, ConfigEntry* e) {
    e->option_count = 0;
    if (!inner || !inner[0]) return;

    char buf[256];
    strncpy(buf, inner, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* p = buf;
    while (*p && e->option_count < CFG_MAX_OPTIONS) {
        char* comma = strchr(p, ',');
        if (comma) *comma = '\0';

        char tmp[CFG_OPTION_LEN];
        strncpy(tmp, p, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        str_trim(tmp);
        if (tmp[0]) {
            strncpy(e->options[e->option_count], tmp, CFG_OPTION_LEN - 1);
            e->options[e->option_count][CFG_OPTION_LEN - 1] = '\0';
            e->option_count++;
        }

        if (!comma) break;
        p = comma + 1;
    }
}

static void clamp_cfg_value(ConfigEntry* e) {
    if (!e) return;
    if (e->type == LUA_CFG_ENUM) { cfg_enum_snap_value(e); return; }
    if (e->type != LUA_CFG_INT && e->type != LUA_CFG_FLOAT) return;

    double v = atof(e->value);
    if (e->has_min && v < e->min_value) v = e->min_value;
    if (e->has_max && v > e->max_value) v = e->max_value;

    if (e->type == LUA_CFG_INT) {
        int iv = (int)v;
        snprintf(e->value, sizeof(e->value), "%d", iv);
    } else {
        snprintf(e->value, sizeof(e->value), "%.6g", v);
    }
}

static int parse_type_and_bounds(const char* type_src,
                                 int* out_type,
                                 int* out_has_min,
                                 double* out_min,
                                 int* out_has_max,
                                 double* out_max,
                                 char* out_inner,
                                 size_t inner_sz) {
    char type_buf[256];
    char base[32];
    char bounds[256];
    char* lb = NULL;
    char* rb = NULL;

    if (!type_src || !out_type || !out_has_min || !out_min || !out_has_max || !out_max) return 0;
    if (out_inner && inner_sz) out_inner[0] = '\0';

    strncpy(type_buf, type_src, sizeof(type_buf) - 1);
    type_buf[sizeof(type_buf) - 1] = '\0';
    str_trim(type_buf);

    *out_has_min = 0;
    *out_has_max = 0;
    *out_min = 0.0;
    *out_max = 0.0;

    lb = strchr(type_buf, '[');
    rb = lb ? strchr(lb + 1, ']') : NULL;
    if (lb && rb) {
        size_t base_len = (size_t)(lb - type_buf);
        if (base_len >= sizeof(base)) base_len = sizeof(base) - 1;
        memcpy(base, type_buf, base_len);
        base[base_len] = '\0';
        str_trim(base);

        {
            size_t b_len = (size_t)(rb - (lb + 1));
            if (b_len >= sizeof(bounds)) b_len = sizeof(bounds) - 1;
            memcpy(bounds, lb + 1, b_len);
            bounds[b_len] = '\0';
            str_trim(bounds);
        }
    } else {
        strncpy(base, type_buf, sizeof(base) - 1);
        base[sizeof(base) - 1] = '\0';
        bounds[0] = '\0';
    }

    if (out_inner && inner_sz) {
        strncpy(out_inner, bounds, inner_sz - 1);
        out_inner[inner_sz - 1] = '\0';
    }

    *out_type = string_to_type(base);
    if (*out_type == LUA_CFG_NONE) return 0;

    if (bounds[0] && (*out_type == LUA_CFG_INT || *out_type == LUA_CFG_FLOAT)) {
        char bcopy[48];
        char* comma;
        strncpy(bcopy, bounds, sizeof(bcopy) - 1);
        bcopy[sizeof(bcopy) - 1] = '\0';

        comma = strchr(bcopy, ',');
        if (comma) {
            *comma = '\0';
            comma++;
            str_trim(bcopy);
            str_trim(comma);
            if (bcopy[0]) {
                *out_min = atof(bcopy);
                *out_has_min = 1;
            }
            if (comma[0]) {
                *out_max = atof(comma);
                *out_has_max = 1;
            }
            if (*out_has_min && *out_has_max && *out_min > *out_max) {
                double t = *out_min;
                *out_min = *out_max;
                *out_max = t;
            }
        }
    }

    return 1;
}

static void format_type_with_bounds(const ConfigEntry* e, char* out, size_t outsz) {
    if (!out || outsz == 0) return;
    out[0] = '\0';
    if (!e) return;

    const char* base = type_to_string(e->type);
    if (e->type == LUA_CFG_ENUM) {
        char list[256];
        size_t pos = 0;
        list[0] = '\0';
        for (int i = 0; i < e->option_count; i++) {
            int n = snprintf(list + pos, sizeof(list) - pos, "%s%s",
                             (i ? "," : ""), e->options[i]);
            if (n < 0 || (size_t)n >= sizeof(list) - pos) break;
            pos += (size_t)n;
        }
        snprintf(out, outsz, "options[%s]", list);
        return;
    }
    if ((e->type == LUA_CFG_INT || e->type == LUA_CFG_FLOAT) && (e->has_min || e->has_max)) {
        char minbuf[64] = {0};
        char maxbuf[64] = {0};
        if (e->has_min) snprintf(minbuf, sizeof(minbuf), "%.6g", e->min_value);
        if (e->has_max) snprintf(maxbuf, sizeof(maxbuf), "%.6g", e->max_value);
        snprintf(out, outsz, "%s[%s,%s]", base, minbuf, maxbuf);
    } else {
        snprintf(out, outsz, "%s", base);
    }
}

static void mod_config_clear(lua_State* Ls, LoadedMod* mod) {
    if (!mod) return;
    if (mod->cfg_entries) {
        free(mod->cfg_entries);
        mod->cfg_entries = NULL;
    }
    mod->cfg_count = 0;
    mod->cfg_cap = 0;

    if (mod->cfg_actions) {
        for (int i = 0; i < mod->cfg_action_count; i++) {
            reflist_clear(Ls, &mod->cfg_actions[i].handlers);
        }
        free(mod->cfg_actions);
        mod->cfg_actions = NULL;
    }
    mod->cfg_action_count = 0;
    mod->cfg_action_cap = 0;
}

static void mod_config_push_entry(LoadedMod* mod, const ConfigEntry* e) {
    if (!mod || !e) return;
    if (mod->cfg_count + 1 > mod->cfg_cap) {
        int newcap = (mod->cfg_cap == 0) ? 8 : (mod->cfg_cap * 2);
        ConfigEntry* ne = (ConfigEntry*)realloc(mod->cfg_entries, sizeof(ConfigEntry) * newcap);
        if (!ne) return;
        mod->cfg_entries = ne;
        mod->cfg_cap = newcap;
    }
    mod->cfg_entries[mod->cfg_count++] = *e;
}

static int mod_config_find_index(LoadedMod* mod, const char* key) {
    if (!mod || !key) return -1;
    for (int i = 0; i < mod->cfg_count; i++) {
        if (_stricmp(mod->cfg_entries[i].key, key) == 0) return i;
    }
    return -1;
}

static ConfigAction* mod_config_get_or_add_action(LoadedMod* mod, const char* key) {
    if (!mod || !key) return NULL;
    for (int i = 0; i < mod->cfg_action_count; i++) {
        if (_stricmp(mod->cfg_actions[i].key, key) == 0) return &mod->cfg_actions[i];
    }
    if (mod->cfg_action_count + 1 > mod->cfg_action_cap) {
        int newcap = (mod->cfg_action_cap == 0) ? 4 : (mod->cfg_action_cap * 2);
        ConfigAction* na = (ConfigAction*)realloc(mod->cfg_actions, sizeof(ConfigAction) * newcap);
        if (!na) return NULL;
        mod->cfg_actions = na;
        mod->cfg_action_cap = newcap;
    }
    ConfigAction* a = &mod->cfg_actions[mod->cfg_action_count++];
    memset(a, 0, sizeof(*a));
    strncpy(a->key, key, sizeof(a->key) - 1);
    a->key[sizeof(a->key) - 1] = '\0';
    a->handlers.refs = NULL;
    a->handlers.count = 0;
    a->handlers.cap = 0;
    return a;
}

static InputBinding* mod_bind_by_index(LoadedMod* mod, int bind_index) {
    if (!mod) return NULL;
    if (bind_index < 0 || bind_index >= mod->bind_count) return NULL;
    return &mod->binds[bind_index];
}

typedef struct BindNameMap {
    int sym;
    const char* name;
} BindNameMap;

static const BindNameMap k_bind_name_map[] = {
    { 8, "Backspace" },
    { 9, "Tab" },
    { 13, "Enter" },
    { 27, "Escape" },
    { 32, "Space" },
    { 127, "Delete" },
    { 1073741882, "F1" }, { 1073741883, "F2" }, { 1073741884, "F3" },
    { 1073741885, "F4" }, { 1073741886, "F5" }, { 1073741887, "F6" },
    { 1073741888, "F7" }, { 1073741889, "F8" }, { 1073741890, "F9" },
    { 1073741891, "F10" }, { 1073741892, "F11" }, { 1073741893, "F12" },
    { 1073741898, "Home" },
    { 1073741899, "PageUp" },
    { 1073741901, "End" },
    { 1073741902, "PageDown" },
    { 1073741903, "Right" },
    { 1073741904, "Left" },
    { 1073741905, "Down" },
    { 1073741906, "Up" },
    { 1073741912, "KeypadEnter" },
    { '`', "Backtick" },
};

static int bind_sym_to_name(int sym, char* out, size_t outsz) {
    if (!out || outsz == 0) return 0;
    out[0] = '\0';
    if (sym == 0) {
        snprintf(out, outsz, "Unbound");
        return 1;
    }
    for (size_t i = 0; i < sizeof(k_bind_name_map) / sizeof(k_bind_name_map[0]); i++) {
        if (k_bind_name_map[i].sym == sym) {
            snprintf(out, outsz, "%s", k_bind_name_map[i].name);
            return 1;
        }
    }
    if (sym >= 33 && sym <= 126) {
        if (sym >= 'a' && sym <= 'z') sym = toupper(sym);
        snprintf(out, outsz, "%c", (char)sym);
        return 1;
    }
    snprintf(out, outsz, "Key%d", sym);
    return 1;
}

static int bind_name_to_sym(const char* name, int* out_sym) {
    if (!name || !name[0]) return 0;
    while (*name && isspace((unsigned char)*name)) name++;
    if (!name[0]) return 0;
    if (_stricmp(name, "none") == 0 || _stricmp(name, "unbound") == 0 || _stricmp(name, "clear") == 0) {
        if (out_sym) *out_sym = 0;
        return 1;
    }
    for (size_t i = 0; i < sizeof(k_bind_name_map) / sizeof(k_bind_name_map[0]); i++) {
        if (_stricmp(name, k_bind_name_map[i].name) == 0) {
            if (out_sym) *out_sym = k_bind_name_map[i].sym;
            return 1;
        }
    }
    if ((_strnicmp(name, "Key", 3) == 0 || _strnicmp(name, "SDLK_", 5) == 0) && isdigit((unsigned char)name[strlen(name)-1])) {
        const char* n = (_strnicmp(name, "Key", 3) == 0) ? (name + 3) : (name + 5);
        int v = atoi(n);
        if (out_sym) *out_sym = v;
        return 1;
    }
    if (!name[1]) {
        int sym = (unsigned char)name[0];
        if (sym >= 'A' && sym <= 'Z') sym = tolower(sym);
        if (out_sym) *out_sym = sym;
        return 1;
    }
    return 0;
}

static void mod_bind_update_name(InputBinding* bind) {
    if (!bind) return;
    bind_sym_to_name(bind->sym, bind->value_name, sizeof(bind->value_name));
}

static void mod_bind_clear(LoadedMod* mod) {
    if (!mod) return;
    if (mod->binds) {
        free(mod->binds);
        mod->binds = NULL;
    }
    mod->bind_count = 0;
    mod->bind_cap = 0;
    mod->binds_path[0] = '\0';
}

static int mod_bind_find_index(LoadedMod* mod, const char* key) {
    if (!mod || !key || !key[0]) return -1;
    for (int i = 0; i < mod->bind_count; i++) {
        if (_stricmp(mod->binds[i].key, key) == 0) return i;
    }
    return -1;
}

static int mod_bind_ensure_capacity(LoadedMod* mod, int needed) {
    int newcap;
    InputBinding* nb;
    if (!mod) return 0;
    if (needed <= mod->bind_cap) return 1;
    newcap = (mod->bind_cap == 0) ? 4 : (mod->bind_cap * 2);
    while (newcap < needed) newcap *= 2;
    nb = (InputBinding*)realloc(mod->binds, sizeof(InputBinding) * newcap);
    if (!nb) return 0;
    mod->binds = nb;
    mod->bind_cap = newcap;
    return 1;
}

static int mod_bind_register(LoadedMod* mod, const char* key, const char* label, int default_sym) {
    int idx;
    if (!mod || !key || !key[0]) return -1;
    idx = mod_bind_find_index(mod, key);
    if (idx >= 0) {
        InputBinding* b = &mod->binds[idx];
        if (label && label[0]) snprintf(b->label, sizeof(b->label), "%s", label);
        if (default_sym != 0 && b->default_sym == 0) b->default_sym = default_sym;
        if (b->sym == 0 && default_sym != 0) b->sym = default_sym;
        mod_bind_update_name(b);
        return idx;
    }
    if (!mod_bind_ensure_capacity(mod, mod->bind_count + 1)) return -1;
    idx = mod->bind_count++;
    memset(&mod->binds[idx], 0, sizeof(mod->binds[idx]));
    snprintf(mod->binds[idx].key, sizeof(mod->binds[idx].key), "%s", key);
    snprintf(mod->binds[idx].label, sizeof(mod->binds[idx].label), "%s", (label && label[0]) ? label : key);
    mod->binds[idx].default_sym = default_sym;
    mod->binds[idx].sym = default_sym;
    mod_bind_update_name(&mod->binds[idx]);
    return idx;
}

static int mod_bind_save(LoadedMod* mod) {
    FILE* f;
    if (!mod || !mod->binds_path[0]) return 0;
    f = fopen(mod->binds_path, "w");
    if (!f) return 0;
    fprintf(f, "# Luna binds v1\n");
    for (int i = 0; i < mod->bind_count; i++) {
        char name[64];
        bind_sym_to_name(mod->binds[i].sym, name, sizeof(name));
        fprintf(f, "%s: %s\n", mod->binds[i].key, name);
    }
    fclose(f);
    return 1;
}

static int mod_bind_load(LoadedMod* mod) {
    FILE* f;
    if (!mod || !mod->binds_path[0]) return 0;
    f = fopen(mod->binds_path, "r");
    if (!f) return 1;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char* colon;
        char* key;
        char* value;
        int idx;
        int sym = 0;
        str_trim(line);
        if (!line[0] || line[0] == '#') continue;
        colon = strchr(line, ':');
        if (!colon) continue;
        *colon = '\0';
        key = line;
        value = colon + 1;
        str_trim(key);
        str_trim(value);
        if (!key[0]) continue;
        idx = mod_bind_find_index(mod, key);
        if (idx < 0) continue;
        if (!bind_name_to_sym(value, &sym)) continue;
        mod->binds[idx].sym = sym;
        mod_bind_update_name(&mod->binds[idx]);
    }
    fclose(f);
    return 1;
}

static int mod_bind_has_conflict(LoadedMod* mod, int bind_index) {
    InputBinding* bind = mod_bind_by_index(mod, bind_index);
    if (!bind || bind->sym == 0) return 0;
    for (int mi = 0; mi < g_mod_count; mi++) {
        LoadedMod* other = &g_mods[mi];
        for (int bi = 0; bi < other->bind_count; bi++) {
            InputBinding* other_bind = &other->binds[bi];
            if (other == mod && bi == bind_index) continue;
            if (other_bind->sym == 0) continue;
            if (other_bind->sym == bind->sym) return 1;
        }
    }
    return 0;
}

static void cfg_escape_and_quote(const char* in, char* out, size_t outsz) {
    if (!out || outsz == 0) return;
    out[0] = '\0';
    if (!in) in = "";

    int needs_quotes = 0;
    for (const char* p = in; *p; p++) {
        if (*p == ',' || *p == '"' || isspace((unsigned char)*p) || *p == '#') {
            needs_quotes = 1;
            break;
        }
    }

    if (!needs_quotes) {
        strncpy(out, in, outsz - 1);
        out[outsz - 1] = '\0';
        return;
    }

    size_t oi = 0;
    if (oi + 1 < outsz) out[oi++] = '"';
    for (const char* p = in; *p && oi + 2 < outsz; p++) {
        if (*p == '"' || *p == '\\') {
            out[oi++] = '\\';
        }
        out[oi++] = *p;
    }
    if (oi + 1 < outsz) out[oi++] = '"';
    out[oi] = '\0';
}

static int mod_config_save(LoadedMod* mod) {
    if (!mod || !mod->config_path[0]) return 0;
    FILE* f = fopen(mod->config_path, "w");
    if (!f) return 0;

    fprintf(f, "# Eggnogg+ mod config (v%d)\n", MOD_CONFIG_FORMAT_VERSION);
    fprintf(f, "# Format: key: type, value   (or: key: action[, \"Label\"])\n\n");

    for (int i = 0; i < mod->cfg_count; i++) {
        ConfigEntry* e = &mod->cfg_entries[i];
        char tbuf[80];
        format_type_with_bounds(e, tbuf, sizeof(tbuf));
        const char* t = tbuf;

        if (e->type == LUA_CFG_ACTION) {
            if (_stricmp(e->label, e->key) != 0 && e->label[0]) {
                char q[512];
                cfg_escape_and_quote(e->label, q, sizeof(q));
                fprintf(f, "%s: %s, %s\n", e->key, t, q);
            } else {
                fprintf(f, "%s: %s\n", e->key, t);
            }
            continue;
        }

        char q[512];
        cfg_escape_and_quote(e->value, q, sizeof(q));
        fprintf(f, "%s: %s, %s\n", e->key, t, q);
    }

    fclose(f);
    return 1;
}

static char* find_top_level_comma(char* s) {
    int bracket_depth = 0;
    int in_quotes = 0;
    int esc = 0;
    if (!s) return NULL;

    for (; *s; s++) {
        char c = *s;
        if (in_quotes) {
            if (esc) {
                esc = 0;
                continue;
            }
            if (c == '\\') {
                esc = 1;
                continue;
            }
            if (c == '"') {
                in_quotes = 0;
            }
            continue;
        }

        if (c == '"') {
            in_quotes = 1;
            continue;
        }
        if (c == '[') {
            bracket_depth++;
            continue;
        }
        if (c == ']') {
            if (bracket_depth > 0) bracket_depth--;
            continue;
        }
        if (c == ',' && bracket_depth == 0) {
            return s;
        }
    }

    return NULL;
}

static int mod_config_load(LoadedMod* mod) {
    if (!mod || !mod->config_path[0]) return 0;

    FILE* f = fopen(mod->config_path, "r");
    if (!f) {
        // If config file doesn't exist yet, create an empty stub.
        LOG_INFO("Creating config file for mod %s: %s", mod->id, mod->config_path);
        mod_config_save(mod);
        return 1;
    }

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        // Strip newline
        line[strcspn(line, "\r\n")] = '\0';
        str_trim(line);
        if (!line[0]) continue;
        if (line[0] == '#') continue;
        if (line[0] == ';') continue;
        if (line[0] == '/' && line[1] == '/') continue;

        char* colon = strchr(line, ':');
        if (!colon) continue;
        *colon = '\0';
        char key[64];
        strncpy(key, line, sizeof(key) - 1);
        key[sizeof(key) - 1] = '\0';
        str_trim(key);
        if (!key[0]) continue;

        char rest[448];
        strncpy(rest, colon + 1, sizeof(rest) - 1);
        rest[sizeof(rest) - 1] = '\0';
        str_trim(rest);
        if (!rest[0]) continue;

        // Split on first comma
        char type_str[256] = {0};
        char value_str[256] = {0};

        char* comma = find_top_level_comma(rest);
        if (comma) {
            *comma = '\0';
            strncpy(type_str, rest, sizeof(type_str) - 1);
            strncpy(value_str, comma + 1, sizeof(value_str) - 1);
        } else {
            strncpy(type_str, rest, sizeof(type_str) - 1);
            value_str[0] = '\0';
        }

        str_trim(type_str);
        str_trim(value_str);
        unquote_inplace(value_str);

        int type = LUA_CFG_NONE;
        int has_min = 0;
        int has_max = 0;
        double min_value = 0.0;
        double max_value = 0.0;
        char type_inner[256] = {0};
        if (!parse_type_and_bounds(type_str, &type, &has_min, &min_value, &has_max, &max_value,
                                   type_inner, sizeof(type_inner))) continue;

        ConfigEntry e;
        memset(&e, 0, sizeof(e));
        strncpy(e.key, key, sizeof(e.key) - 1);
        strncpy(e.label, key, sizeof(e.label) - 1);
        e.type = type;
        e.has_min = has_min;
        e.has_max = has_max;
        e.min_value = min_value;
        e.max_value = max_value;

        if (type == LUA_CFG_ENUM) {
            cfg_parse_options(type_inner, &e);
            if (e.option_count <= 0) {
                LOG_WARN("[mod:%s] config key '%s' is type 'options' but has no choices; skipping",
                         mod->id, key);
                continue;
            }
        }

        if (type == LUA_CFG_ACTION) {
            // optional label after comma
            if (value_str[0]) {
                strncpy(e.label, value_str, sizeof(e.label) - 1);
            }
            e.value[0] = '\0';
        } else if (type == LUA_CFG_BOOL) {
            int b = parse_bool(value_str);
            snprintf(e.value, sizeof(e.value), "%s", b ? "true" : "false");
        } else {
            strncpy(e.value, value_str, sizeof(e.value) - 1);
        }

        clamp_cfg_value(&e);

        mod_config_push_entry(mod, &e);
    }

    fclose(f);
    return 1;
}

static void mod_storage_clear(LoadedMod* mod) {
    if (!mod) return;
    free(mod->storage_entries);
    mod->storage_entries = NULL;
    mod->storage_count = 0;
    mod->storage_cap = 0;
    mod->storage_schema_version = 0;
    mod->storage_suspend_save = 0;
}

static int mod_storage_find_index(LoadedMod* mod, const char* key) {
    if (!mod || !key || !key[0]) return -1;
    for (int i = 0; i < mod->storage_count; i++) {
        if (_stricmp(mod->storage_entries[i].key, key) == 0) return i;
    }
    return -1;
}

static int mod_storage_key_valid(const char* key) {
    if (!key || !key[0]) return 0;
    if (strlen(key) >= MOD_STORAGE_KEY_MAX) return 0;
    if (_stricmp(key, "__schema") == 0) return 0;
    for (const char* p = key; *p; p++) {
        if (*p == ':' || *p == ',' || *p == '\r' || *p == '\n' || *p == '\t') return 0;
    }
    return 1;
}

static int mod_storage_ensure_capacity(LoadedMod* mod, int needed) {
    if (!mod || needed <= 0) return 0;
    if (needed <= mod->storage_cap) return 1;
    int newcap = (mod->storage_cap == 0) ? 8 : (mod->storage_cap * 2);
    while (newcap < needed) newcap *= 2;
    StorageEntry* ne = (StorageEntry*)realloc(mod->storage_entries, sizeof(StorageEntry) * newcap);
    if (!ne) return 0;
    mod->storage_entries = ne;
    mod->storage_cap = newcap;
    return 1;
}

static int mod_storage_set_value(LoadedMod* mod, const char* key, int type, int bool_value, double num_value, const char* str_value) {
    int idx;
    StorageEntry* e;
    if (!mod || !mod_storage_key_valid(key)) return 0;
    if (type != MOD_STORAGE_BOOL && type != MOD_STORAGE_NUMBER && type != MOD_STORAGE_STRING) return 0;

    idx = mod_storage_find_index(mod, key);
    if (idx < 0) {
        if (!mod_storage_ensure_capacity(mod, mod->storage_count + 1)) return 0;
        idx = mod->storage_count++;
        memset(&mod->storage_entries[idx], 0, sizeof(mod->storage_entries[idx]));
        snprintf(mod->storage_entries[idx].key, sizeof(mod->storage_entries[idx].key), "%s", key);
    }

    e = &mod->storage_entries[idx];
    e->type = type;
    if (type == MOD_STORAGE_BOOL) {
        e->bool_value = bool_value ? 1 : 0;
        e->num_value = e->bool_value ? 1.0 : 0.0;
        e->str_value[0] = '\0';
    } else if (type == MOD_STORAGE_NUMBER) {
        e->num_value = num_value;
        e->bool_value = (num_value != 0.0) ? 1 : 0;
        e->str_value[0] = '\0';
    } else {
        if (!str_value) str_value = "";
        snprintf(e->str_value, sizeof(e->str_value), "%s", str_value);
        e->bool_value = parse_bool(str_value);
        e->num_value = atof(str_value);
    }
    return 1;
}

static int mod_storage_remove_key(LoadedMod* mod, const char* key) {
    int idx = mod_storage_find_index(mod, key);
    if (!mod || idx < 0) return 0;
    if (idx < mod->storage_count - 1) {
        memmove(&mod->storage_entries[idx],
                &mod->storage_entries[idx + 1],
                sizeof(StorageEntry) * (size_t)(mod->storage_count - idx - 1));
    }
    mod->storage_count--;
    return 1;
}

static int mod_storage_save(LoadedMod* mod) {
    FILE* f = NULL;
    if (!mod || !mod->storage_path[0]) return 0;
    f = fopen(mod->storage_path, "w");
    if (!f) return 0;

    fprintf(f, "# Eggnogg+ mod storage (v1)\n");
    fprintf(f, "__schema: int, %d\n", mod->storage_schema_version);
    for (int i = 0; i < mod->storage_count; i++) {
        const StorageEntry* e = &mod->storage_entries[i];
        if (e->type == MOD_STORAGE_BOOL) {
            fprintf(f, "%s: bool, %s\n", e->key, e->bool_value ? "true" : "false");
        } else if (e->type == MOD_STORAGE_NUMBER) {
            fprintf(f, "%s: num, %.17g\n", e->key, e->num_value);
        } else if (e->type == MOD_STORAGE_STRING) {
            char q[MOD_STORAGE_STR_MAX * 2];
            cfg_escape_and_quote(e->str_value, q, sizeof(q));
            fprintf(f, "%s: str, %s\n", e->key, q);
        }
    }

    fclose(f);
    return 1;
}

static int mod_storage_load(LoadedMod* mod) {
    FILE* f = NULL;
    char line[640];
    if (!mod || !mod->storage_path[0]) return 0;

    mod_storage_clear(mod);

    f = fopen(mod->storage_path, "r");
    if (!f) return 1; // storage is optional; create lazily on first write.

    while (fgets(line, sizeof(line), f)) {
        char key[MOD_STORAGE_KEY_MAX];
        char rest[512];
        char type_str[64];
        char value_str[MOD_STORAGE_STR_MAX];
        char* colon = NULL;
        char* comma = NULL;

        line[strcspn(line, "\r\n")] = '\0';
        str_trim(line);
        if (!line[0] || line[0] == '#' || line[0] == ';') continue;
        if (line[0] == '/' && line[1] == '/') continue;

        colon = strchr(line, ':');
        if (!colon) continue;
        *colon = '\0';
        snprintf(key, sizeof(key), "%s", line);
        str_trim(key);
        if (!key[0]) continue;

        snprintf(rest, sizeof(rest), "%s", colon + 1);
        str_trim(rest);
        if (!rest[0]) continue;

        comma = find_top_level_comma(rest);
        if (comma) {
            *comma = '\0';
            snprintf(type_str, sizeof(type_str), "%s", rest);
            snprintf(value_str, sizeof(value_str), "%s", comma + 1);
        } else {
            snprintf(type_str, sizeof(type_str), "%s", rest);
            value_str[0] = '\0';
        }

        str_trim(type_str);
        str_trim(value_str);
        unquote_inplace(value_str);

        if (_stricmp(key, "__schema") == 0) {
            mod->storage_schema_version = atoi(value_str);
            if (mod->storage_schema_version < 0) mod->storage_schema_version = 0;
            continue;
        }

        if (!mod_storage_key_valid(key)) continue;
        if (_stricmp(type_str, "bool") == 0 || _stricmp(type_str, "boolean") == 0) {
            mod_storage_set_value(mod, key, MOD_STORAGE_BOOL, parse_bool(value_str), 0.0, NULL);
        } else if (_stricmp(type_str, "num") == 0 || _stricmp(type_str, "number") == 0 ||
                   _stricmp(type_str, "int") == 0 || _stricmp(type_str, "float") == 0) {
            mod_storage_set_value(mod, key, MOD_STORAGE_NUMBER, 0, atof(value_str), NULL);
        } else if (_stricmp(type_str, "str") == 0 || _stricmp(type_str, "string") == 0) {
            mod_storage_set_value(mod, key, MOD_STORAGE_STRING, 0, 0.0, value_str);
        }
    }

    fclose(f);
    return 1;
}

static void mod_resource_regs_clear(LoadedMod* mod) {
    if (!mod) return;
    free(mod->font_regs);
    mod->font_regs = NULL;
    mod->font_reg_count = 0;
    mod->font_reg_cap = 0;
    free(mod->texture_regs);
    mod->texture_regs = NULL;
    mod->texture_reg_count = 0;
    mod->texture_reg_cap = 0;
}

static int mod_font_reg_record(LoadedMod* mod, uint8_t byte_value, const char* relpath) {
    FontRegistration* nr = NULL;
    if (!mod || !relpath || !relpath[0]) return 0;
    for (int i = 0; i < mod->font_reg_count; i++) {
        if (mod->font_regs[i].byte_value != byte_value) continue;
        snprintf(mod->font_regs[i].relpath, sizeof(mod->font_regs[i].relpath), "%s", relpath);
        return 1;
    }
    if (mod->font_reg_count + 1 > mod->font_reg_cap) {
        int newcap = (mod->font_reg_cap == 0) ? 4 : (mod->font_reg_cap * 2);
        nr = (FontRegistration*)realloc(mod->font_regs, sizeof(FontRegistration) * newcap);
        if (!nr) return 0;
        mod->font_regs = nr;
        mod->font_reg_cap = newcap;
    }
    mod->font_regs[mod->font_reg_count].byte_value = byte_value;
    snprintf(mod->font_regs[mod->font_reg_count].relpath,
             sizeof(mod->font_regs[mod->font_reg_count].relpath),
             "%s", relpath);
    mod->font_reg_count++;
    return 1;
}

static int mod_texture_reg_record(LoadedMod* mod, const char* target, const char* relpath) {
    TextureRegistration* nr = NULL;
    if (!mod || !target || !target[0] || !relpath || !relpath[0]) return 0;
    for (int i = 0; i < mod->texture_reg_count; i++) {
        if (_stricmp(mod->texture_regs[i].target, target) != 0) continue;
        snprintf(mod->texture_regs[i].relpath, sizeof(mod->texture_regs[i].relpath), "%s", relpath);
        return 1;
    }
    if (mod->texture_reg_count + 1 > mod->texture_reg_cap) {
        int newcap = (mod->texture_reg_cap == 0) ? 4 : (mod->texture_reg_cap * 2);
        nr = (TextureRegistration*)realloc(mod->texture_regs, sizeof(TextureRegistration) * newcap);
        if (!nr) return 0;
        mod->texture_regs = nr;
        mod->texture_reg_cap = newcap;
    }
    snprintf(mod->texture_regs[mod->texture_reg_count].target,
             sizeof(mod->texture_regs[mod->texture_reg_count].target),
             "%s", target);
    snprintf(mod->texture_regs[mod->texture_reg_count].relpath,
             sizeof(mod->texture_regs[mod->texture_reg_count].relpath),
             "%s", relpath);
    mod->texture_reg_count++;
    return 1;
}

// =============================
// Small helpers
// =============================

static void reflist_push(LuaRefList* list, int ref) {
    if (list->count + 1 > list->cap) {
        int newcap = (list->cap == 0) ? 8 : (list->cap * 2);
        int* newrefs = (int*)realloc(list->refs, sizeof(int) * newcap);
        if (!newrefs) return;
        list->refs = newrefs;
        list->cap = newcap;
    }
    list->refs[list->count++] = ref;
}

static void reflist_clear(lua_State* Ls, LuaRefList* list) {
    if (!list || !list->refs) return;
    for (int i = 0; i < list->count; i++) {
        if (list->refs[i] != LUA_NOREF && list->refs[i] != LUA_REFNIL) {
            luaL_unref(Ls, LUA_REGISTRYINDEX, list->refs[i]);
        }
    }
    free(list->refs);
    list->refs = NULL;
    list->count = 0;
    list->cap = 0;
}

static int reflist_snapshot(const LuaRefList* list, int** out_refs, int* out_count) {
    if (out_refs) *out_refs = NULL;
    if (out_count) *out_count = 0;
    if (!list || list->count <= 0 || !list->refs) return 1;
    if (!out_refs || !out_count) return 0;

    int count = list->count;
    int* refs = (int*)malloc(sizeof(int) * count);
    if (!refs) return 0;

    memcpy(refs, list->refs, sizeof(int) * count);
    *out_refs = refs;
    *out_count = count;
    return 1;
}

static int interop_find_index_by_ns(const char* ns) {
    if (!ns || !ns[0]) return -1;
    for (int i = 0; i < g_interop_provider_count; i++) {
        if (_stricmp(g_interop_providers[i].ns, ns) == 0) return i;
    }
    return -1;
}

static int interop_ensure_capacity(int needed) {
    if (needed <= g_interop_provider_cap) return 1;
    int newcap = (g_interop_provider_cap == 0) ? 8 : (g_interop_provider_cap * 2);
    while (newcap < needed) newcap *= 2;
    InteropProvider* ne = (InteropProvider*)realloc(g_interop_providers, sizeof(InteropProvider) * newcap);
    if (!ne) return 0;
    g_interop_providers = ne;
    g_interop_provider_cap = newcap;
    return 1;
}

static void interop_remove_index(lua_State* Ls, int idx) {
    if (idx < 0 || idx >= g_interop_provider_count) return;
    if (Ls && g_interop_providers[idx].table_ref != LUA_NOREF && g_interop_providers[idx].table_ref != LUA_REFNIL) {
        luaL_unref(Ls, LUA_REGISTRYINDEX, g_interop_providers[idx].table_ref);
    }
    if (idx < g_interop_provider_count - 1) {
        memmove(&g_interop_providers[idx],
                &g_interop_providers[idx + 1],
                sizeof(InteropProvider) * (size_t)(g_interop_provider_count - idx - 1));
    }
    g_interop_provider_count--;
}

static void interop_remove_owner(lua_State* Ls, LoadedMod* owner) {
    if (!owner) return;
    for (int i = g_interop_provider_count - 1; i >= 0; i--) {
        if (g_interop_providers[i].owner == owner) {
            interop_remove_index(Ls, i);
        }
    }
}

static void interop_clear_all(lua_State* Ls) {
    if (Ls) {
        for (int i = 0; i < g_interop_provider_count; i++) {
            if (g_interop_providers[i].table_ref != LUA_NOREF && g_interop_providers[i].table_ref != LUA_REFNIL) {
                luaL_unref(Ls, LUA_REGISTRYINDEX, g_interop_providers[i].table_ref);
            }
        }
    }
    free(g_interop_providers);
    g_interop_providers = NULL;
    g_interop_provider_count = 0;
    g_interop_provider_cap = 0;
}

static LoadedMod* mods_add(void) {
    if (g_mod_count + 1 > g_mod_cap) {
        int newcap = (g_mod_cap == 0) ? 8 : (g_mod_cap * 2);
        LoadedMod* nm = (LoadedMod*)realloc(g_mods, sizeof(LoadedMod) * newcap);
        if (!nm) return NULL;
        g_mods = nm;
        g_mod_cap = newcap;
    }
    LoadedMod* m = &g_mods[g_mod_count++];
    memset(m, 0, sizeof(*m));
    m->enabled = 1;
    m->error_count = 0;
    m->env_ref = LUA_NOREF;
    m->mod_ref = LUA_NOREF;
    m->on_load_ref = LUA_NOREF;
    m->on_unload_ref = LUA_NOREF;
    m->api_version = MOD_API_VERSION;

    m->on_layout = NULL;
    m->on_layout_count = 0;
    m->on_layout_cap = 0;

    m->ui_hitboxes = NULL;
    m->ui_hitbox_count = 0;
    m->ui_hitbox_cap = 0;
    m->ui_state_ptr = NULL;
    memset(&m->ui_layout, 0, sizeof(m->ui_layout));
    m->ui_native_buttons = NULL;
    m->ui_native_count = 0;
    m->ui_native_cap = 0;

    m->depends.count = 0;
    m->optional_deps.count = 0;
    m->conflicts.count = 0;

    m->config_rel[0] = '\0';
    m->config_path[0] = '\0';
    m->cfg_entries = NULL;
    m->cfg_count = 0;
    m->cfg_cap = 0;
    m->cfg_actions = NULL;
    m->cfg_action_count = 0;
    m->cfg_action_cap = 0;
    m->binds = NULL;
    m->bind_count = 0;
    m->bind_cap = 0;
    m->binds_path[0] = '\0';
    m->storage_rel[0] = '\0';
    m->binds_rel[0] = '\0';
    m->storage_path[0] = '\0';
    m->storage_schema_version = 0;
    m->storage_suspend_save = 0;
    m->storage_entries = NULL;
    m->storage_count = 0;
    m->storage_cap = 0;

    m->audio_sfx_volume = 1.0f;
    m->audio_music_volume = 1.0f;
    m->audio_channel_base = -1;
    m->audio_channel_count = 0;
    m->audio_next_channel = 0;
    m->audio_chunks = NULL;
    m->audio_chunk_count = 0;
    m->audio_chunk_cap = 0;
    m->font_regs = NULL;
    m->font_reg_count = 0;
    m->font_reg_cap = 0;
    m->texture_regs = NULL;
    m->texture_reg_count = 0;
    m->texture_reg_cap = 0;
    m->asset_sheets = NULL;
    m->asset_sheet_count = 0;
    m->asset_sheet_cap = 0;
    m->asset_batch_depth = 0;
    m->asset_batch_dirty = 0;
    return m;
}

static void mod_set_single_ref(lua_State* Ls, int* slot, int newref) {
    if (!slot) return;
    if (*slot != LUA_NOREF && *slot != LUA_REFNIL) {
        luaL_unref(Ls, LUA_REGISTRYINDEX, *slot);
    }
    *slot = newref;
}

static LoadedMod* mod_from_upvalue(lua_State* Ls) {
    return (LoadedMod*)lua_touserdata(Ls, lua_upvalueindex(1));
}

static void log_mod(LoadedMod* mod, const char* level, const char* msg) {
    if (!mod) {
        log_write(level, "[mod:?] %s", msg);
        return;
    }
    log_write(level, "[mod:%s] %s", mod->id[0] ? mod->id : "?", msg);
}

static double perf_now_ms(void) {
    static LARGE_INTEGER freq = { 0 };
    static int freq_ready = 0;
    LARGE_INTEGER now;
    if (!freq_ready) {
        if (!QueryPerformanceFrequency(&freq)) {
            freq.QuadPart = 0;
        }
        freq_ready = 1;
    }
    if (freq.QuadPart > 0 && QueryPerformanceCounter(&now)) {
        return ((double)now.QuadPart * 1000.0) / (double)freq.QuadPart;
    }
    return (double)GetTickCount64();
}

static void mod_perf_counter_record(ModPerfCounter* counter, double elapsed_ms) {
    if (!counter) return;
    if (elapsed_ms < 0.0) elapsed_ms = 0.0;
    counter->call_count++;
    counter->total_ms += elapsed_ms;
    counter->last_ms = elapsed_ms;
    if (elapsed_ms > counter->max_ms) counter->max_ms = elapsed_ms;
}

static void mod_perf_reset(LoadedMod* mod) {
    if (!mod) return;
    memset(&mod->perf_frame, 0, sizeof(mod->perf_frame));
    memset(&mod->perf_event, 0, sizeof(mod->perf_event));
    memset(&mod->perf_layout, 0, sizeof(mod->perf_layout));
}

static unsigned int mod_diag_estimate_memory_bytes(const LoadedMod* mod) {
    unsigned long long bytes = 0;
    if (!mod) return 0;

    bytes += (unsigned long long)sizeof(*mod);
    bytes += (unsigned long long)mod->on_frame.cap * (unsigned long long)sizeof(int);
    bytes += (unsigned long long)mod->on_tick.cap * (unsigned long long)sizeof(int);
    bytes += (unsigned long long)mod->on_tick_post.cap * (unsigned long long)sizeof(int);
    bytes += (unsigned long long)mod->on_event.cap * (unsigned long long)sizeof(int);
    bytes += (unsigned long long)mod->on_layout_cap * (unsigned long long)sizeof(LayoutHandler);
    bytes += (unsigned long long)mod->ui_hitbox_cap * (unsigned long long)sizeof(UiHitBox);
    bytes += (unsigned long long)mod->ui_native_cap * (unsigned long long)sizeof(UiNativeButton*);
    bytes += (unsigned long long)mod->ui_string_cap * (unsigned long long)sizeof(char*);
    bytes += (unsigned long long)mod->cfg_cap * (unsigned long long)sizeof(ConfigEntry);
    bytes += (unsigned long long)mod->cfg_action_cap * (unsigned long long)sizeof(ConfigAction);
    bytes += (unsigned long long)mod->bind_cap * (unsigned long long)sizeof(InputBinding);
    bytes += (unsigned long long)mod->storage_cap * (unsigned long long)sizeof(StorageEntry);
    bytes += (unsigned long long)mod->audio_chunk_cap * (unsigned long long)sizeof(AudioChunkCacheEntry);
    bytes += (unsigned long long)mod->font_reg_cap * (unsigned long long)sizeof(FontRegistration);
    bytes += (unsigned long long)mod->texture_reg_cap * (unsigned long long)sizeof(TextureRegistration);
    bytes += (unsigned long long)mod->asset_sheet_cap * (unsigned long long)sizeof(ModAssetSheet);

    if (mod->cfg_actions) {
        for (int i = 0; i < mod->cfg_action_count; i++) {
            bytes += (unsigned long long)mod->cfg_actions[i].handlers.cap * (unsigned long long)sizeof(int);
        }
    }
    if (mod->ui_native_buttons) {
        for (int i = 0; i < mod->ui_native_count; i++) {
            if (mod->ui_native_buttons[i]) bytes += (unsigned long long)sizeof(UiNativeButton);
        }
    }
    if (mod->ui_string_pool) {
        for (int i = 0; i < mod->ui_string_count; i++) {
            if (mod->ui_string_pool[i]) bytes += (unsigned long long)(strlen(mod->ui_string_pool[i]) + 1);
        }
    }

    if (bytes > (unsigned long long)UINT_MAX) return UINT_MAX;
    return (unsigned int)bytes;
}

static void mod_trace_event_log(LoadedMod* mod, const char* type, int sym, int x, int y, int button, int handler_count, int consumed) {
    char buf[256];
    if (!mod || !mod->trace_events || handler_count <= 0) return;
    snprintf(buf, sizeof(buf),
             "trace event type=%s sym=%d button=%d x=%d y=%d handlers=%d consumed=%s",
             (type && type[0]) ? type : "?",
             sym,
             button,
             x,
             y,
             handler_count,
             consumed ? "true" : "false");
    log_mod(mod, "INFO", buf);
}

static float audio_clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int audio_file_exists(const char* path) {
    DWORD attrs;
    if (!path || !path[0]) return 0;
    attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) return 0;
    if (attrs & FILE_ATTRIBUTE_DIRECTORY) return 0;
    return 1;
}

static int audio_is_absolute_path(const char* path) {
    if (!path || !path[0]) return 0;
    if (((path[0] >= 'a' && path[0] <= 'z') || (path[0] >= 'A' && path[0] <= 'Z')) &&
        path[1] == ':' &&
        (path[2] == '\\' || path[2] == '/')) {
        return 1;
    }
    if ((path[0] == '\\' || path[0] == '/') &&
        (path[1] == '\\' || path[1] == '/')) {
        return 1;
    }
    return 0;
}

static void audio_normalize_slashes(char* path) {
    if (!path) return;
    for (int i = 0; path[i]; i++) {
        if (path[i] == '/') path[i] = '\\';
    }
}

static int audio_resolve_mod_path(LoadedMod* mod, const char* in_path, char* out, int out_sz) {
    if (!out || out_sz <= 0) return 0;
    out[0] = '\0';
    if (!mod || !in_path || !in_path[0]) return 0;

    if (audio_is_absolute_path(in_path)) {
        snprintf(out, out_sz, "%s", in_path);
    } else {
        snprintf(out, out_sz, "%s\\%s", mod->folder_path, in_path);
    }

    audio_normalize_slashes(out);
    return 1;
}

static int mod_asset_id_valid(const char* id) {
    int len = 0;
    if (!id || !id[0]) return 0;
    for (const char* p = id; *p; p++, len++) {
        unsigned char c = (unsigned char)*p;
        if (len >= 63) return 0;
        if (isalnum(c) || c == '_' || c == '-' || c == '.' || c == ':') continue;
        return 0;
    }
    return len > 0;
}

static int mod_asset_rel_path_safe(const char* path) {
    const char* p;
    if (!path || !path[0]) return 0;
    if (audio_is_absolute_path(path)) return 0;
    if (path[0] == '\\' || path[0] == '/') return 0;
    p = path;
    while (*p) {
        unsigned char c = (unsigned char)*p;
        if (c < 32) return 0;
        if ((p[0] == '.' && p[1] == '.') &&
            (p[2] == '\0' || p[2] == '\\' || p[2] == '/' || p == path || p[-1] == '\\' || p[-1] == '/')) {
            return 0;
        }
        p++;
    }
    return 1;
}

static int mod_asset_resolve_path(LoadedMod* mod, const char* rel_path, char* out, int out_sz, char* err, int err_sz) {
    if (!mod || !rel_path || !rel_path[0]) {
        if (err && err_sz > 0) snprintf(err, err_sz, "missing asset path");
        return 0;
    }
    if (!mod_asset_rel_path_safe(rel_path)) {
        if (err && err_sz > 0) snprintf(err, err_sz, "asset paths must be relative to the mod folder");
        return 0;
    }
    if (!audio_resolve_mod_path(mod, rel_path, out, out_sz)) {
        if (err && err_sz > 0) snprintf(err, err_sz, "failed to resolve asset path");
        return 0;
    }
    if (!audio_file_exists(out)) {
        if (err && err_sz > 0) snprintf(err, err_sz, "file not found: %s", rel_path);
        return 0;
    }
    return 1;
}

static int mod_asset_create_dir_recursive(const char* full_dir) {
    char tmp[MAX_PATH];
    size_t len;
    char* p;

    if (!full_dir || !full_dir[0]) return 0;
    snprintf(tmp, sizeof(tmp), "%s", full_dir);
    tmp[sizeof(tmp) - 1] = '\0';
    audio_normalize_slashes(tmp);

    len = strlen(tmp);
    while (len > 0 && (tmp[len - 1] == '\\' || tmp[len - 1] == '/')) {
        tmp[len - 1] = '\0';
        len--;
    }
    if (len == 0) return 0;

    for (p = tmp; *p; p++) {
        if (*p != '\\' && *p != '/') continue;
        if (p == tmp) continue;
        if (p > tmp && p[-1] == ':') continue;
        {
            char old = *p;
            *p = '\0';
            if (tmp[0] && !CreateDirectoryA(tmp, NULL)) {
                DWORD gle = GetLastError();
                if (gle != ERROR_ALREADY_EXISTS) {
                    *p = old;
                    return 0;
                }
            }
            *p = old;
        }
    }

    if (!CreateDirectoryA(tmp, NULL)) {
        DWORD gle = GetLastError();
        if (gle != ERROR_ALREADY_EXISTS) return 0;
    }
    return 1;
}

static int mod_asset_resolve_output_dir(LoadedMod* mod, const char* rel_dir, char* full_dir, int full_dir_sz, char* err, int err_sz) {
    if (!rel_dir || !rel_dir[0]) rel_dir = "cache/assets";
    if (!mod_asset_rel_path_safe(rel_dir)) {
        if (err && err_sz > 0) snprintf(err, err_sz, "output dir must be relative to the mod folder");
        return 0;
    }
    if (!audio_resolve_mod_path(mod, rel_dir, full_dir, full_dir_sz)) {
        if (err && err_sz > 0) snprintf(err, err_sz, "failed to resolve output dir");
        return 0;
    }
    if (!mod_asset_create_dir_recursive(full_dir)) {
        if (err && err_sz > 0) snprintf(err, err_sz, "failed to create output dir: %s", rel_dir);
        return 0;
    }
    return 1;
}

static void mod_asset_sanitize_key(const char* in, char* out, int out_sz) {
    int oi = 0;
    if (!out || out_sz <= 0) return;
    out[0] = '\0';
    if (!in || !in[0]) in = "asset";
    for (const char* p = in; *p && oi < out_sz - 1; p++) {
        unsigned char c = (unsigned char)*p;
        if (isalnum(c) || c == '_' || c == '-' || c == '.') {
            out[oi++] = (char)c;
        } else {
            out[oi++] = '_';
        }
    }
    while (oi > 0 && out[oi - 1] == '_') oi--;
    if (oi == 0) {
        snprintf(out, out_sz, "asset");
    } else {
        out[oi] = '\0';
    }
}

static int mod_asset_hex_value(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static int mod_asset_parse_hex_color(const char* value, uint32_t* out_rgb) {
    char hex[7];
    int n = 0;
    const char* p = value;

    if (!value || !out_rgb) return 0;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == '#') p++;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    while (*p && !isspace((unsigned char)*p)) {
        if (n >= 6) return 0;
        if (mod_asset_hex_value((unsigned char)*p) < 0) return 0;
        hex[n++] = *p++;
    }
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p) return 0;

    if (n == 3) {
        int r = mod_asset_hex_value((unsigned char)hex[0]);
        int g = mod_asset_hex_value((unsigned char)hex[1]);
        int b = mod_asset_hex_value((unsigned char)hex[2]);
        *out_rgb = (uint32_t)((r << 20) | (r << 16) | (g << 12) | (g << 8) | (b << 4) | b);
        return 1;
    }
    if (n == 6) {
        int r = (mod_asset_hex_value((unsigned char)hex[0]) << 4) | mod_asset_hex_value((unsigned char)hex[1]);
        int g = (mod_asset_hex_value((unsigned char)hex[2]) << 4) | mod_asset_hex_value((unsigned char)hex[3]);
        int b = (mod_asset_hex_value((unsigned char)hex[4]) << 4) | mod_asset_hex_value((unsigned char)hex[5]);
        *out_rgb = (uint32_t)((r << 16) | (g << 8) | b);
        return 1;
    }
    return 0;
}

static int mod_asset_color_component_from_lua(lua_State* Ls, int table_index, const char* field, int array_index, int* out) {
    double v = -1.0;
    lua_getfield(Ls, table_index, field);
    if (lua_isnumber(Ls, -1)) v = lua_tonumber(Ls, -1);
    lua_pop(Ls, 1);
    if (v < 0.0) {
        lua_rawgeti(Ls, table_index, array_index);
        if (lua_isnumber(Ls, -1)) v = lua_tonumber(Ls, -1);
        lua_pop(Ls, 1);
    }
    if (v < 0.0) return 0;
    if (v <= 1.0) v *= 255.0;
    if (v < 0.0) v = 0.0;
    if (v > 255.0) v = 255.0;
    *out = (int)(v + 0.5);
    return 1;
}

static int mod_asset_parse_lua_color(lua_State* Ls, int value_index, uint32_t* out_rgb) {
    if (lua_isnumber(Ls, value_index)) {
        int value = (int)lua_tointeger(Ls, value_index);
        if (value < 0) return 0;
        *out_rgb = (uint32_t)value & 0x00FFFFFFu;
        return 1;
    }
    if (lua_isstring(Ls, value_index)) {
        return mod_asset_parse_hex_color(lua_tostring(Ls, value_index), out_rgb);
    }
    if (lua_istable(Ls, value_index)) {
        int r = 0, g = 0, b = 0;
        if (!mod_asset_color_component_from_lua(Ls, value_index, "r", 1, &r)) return 0;
        if (!mod_asset_color_component_from_lua(Ls, value_index, "g", 2, &g)) return 0;
        if (!mod_asset_color_component_from_lua(Ls, value_index, "b", 3, &b)) return 0;
        *out_rgb = (uint32_t)((r << 16) | (g << 8) | b);
        return 1;
    }
    return 0;
}

static int mod_asset_layer_add_color(ModColorMaskLayer* layer, uint32_t rgb) {
    if (!layer) return 0;
    for (int i = 0; i < layer->color_count; i++) {
        if (layer->colors[i] == rgb) return 1;
    }
    if (layer->color_count >= MOD_COLOR_MASK_MAX_COLORS) return 0;
    layer->colors[layer->color_count++] = rgb;
    return 1;
}

static int mod_asset_parse_color_list(lua_State* Ls, int value_index, ModColorMaskLayer* layer, char* err, int err_sz) {
    uint32_t rgb = 0;

    if (!lua_istable(Ls, value_index)) {
        if (!mod_asset_parse_lua_color(Ls, value_index, &rgb) || !mod_asset_layer_add_color(layer, rgb)) {
            if (err && err_sz > 0) snprintf(err, err_sz, "invalid color mask value for %s", layer->name);
            return 0;
        }
        return 1;
    }

    {
        int colors_index = 0;
        lua_getfield(Ls, value_index, "colors");
        if (lua_istable(Ls, -1)) colors_index = lua_gettop(Ls);
        if (!colors_index) {
            lua_pop(Ls, 1);
            lua_getfield(Ls, value_index, "colours");
            if (lua_istable(Ls, -1)) colors_index = lua_gettop(Ls);
        }
        if (colors_index) {
            int ok = mod_asset_parse_color_list(Ls, colors_index, layer, err, err_sz);
            lua_pop(Ls, 1);
            return ok;
        }
        lua_pop(Ls, 1);
    }

    {
        size_t n = lua_objlen(Ls, value_index);
        if (n == 0) {
            if (mod_asset_parse_lua_color(Ls, value_index, &rgb) && mod_asset_layer_add_color(layer, rgb)) {
                return 1;
            }
            if (err && err_sz > 0) snprintf(err, err_sz, "empty color mask list for %s", layer->name);
            return 0;
        }
        for (size_t i = 1; i <= n; i++) {
            lua_rawgeti(Ls, value_index, (int)i);
            if (!mod_asset_parse_lua_color(Ls, -1, &rgb) || !mod_asset_layer_add_color(layer, rgb)) {
                lua_pop(Ls, 1);
                if (err && err_sz > 0) snprintf(err, err_sz, "invalid color mask entry for %s", layer->name);
                return 0;
            }
            lua_pop(Ls, 1);
        }
    }

    return 1;
}

static int mod_asset_layer_name_valid(const char* name) {
    int len = 0;
    if (!name || !name[0]) return 0;
    for (const char* p = name; *p; p++, len++) {
        unsigned char c = (unsigned char)*p;
        if (len >= 31) return 0;
        if (isalnum(c) || c == '_' || c == '-') continue;
        return 0;
    }
    return len > 0;
}

static int mod_asset_rgb_in_layer(const ModColorMaskLayer* layer, uint32_t rgb) {
    if (!layer) return 0;
    for (int i = 0; i < layer->color_count; i++) {
        if (layer->colors[i] == rgb) return 1;
    }
    return 0;
}

static int mod_assets_write_tga_rgba(const char* full_path, const uint8_t* rgba, int w, int h, char* err, int err_sz) {
    FILE* f;
    uint8_t header[18];
    uint8_t* row;

    if (!full_path || !rgba || w <= 0 || h <= 0) {
        if (err && err_sz > 0) snprintf(err, err_sz, "invalid generated image");
        return 0;
    }

    f = fopen(full_path, "wb");
    if (!f) {
        if (err && err_sz > 0) snprintf(err, err_sz, "failed to write %s", full_path);
        return 0;
    }

    memset(header, 0, sizeof(header));
    header[2] = 2; // uncompressed truecolor
    header[12] = (uint8_t)(w & 0xFF);
    header[13] = (uint8_t)((w >> 8) & 0xFF);
    header[14] = (uint8_t)(h & 0xFF);
    header[15] = (uint8_t)((h >> 8) & 0xFF);
    header[16] = 32;
    header[17] = 0x28; // 8 alpha bits, top-left origin
    if (fwrite(header, 1, sizeof(header), f) != sizeof(header)) {
        fclose(f);
        if (err && err_sz > 0) snprintf(err, err_sz, "failed to write TGA header");
        return 0;
    }

    row = (uint8_t*)malloc((size_t)w * 4u);
    if (!row) {
        fclose(f);
        if (err && err_sz > 0) snprintf(err, err_sz, "out of memory writing TGA");
        return 0;
    }

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int si = (y * w + x) * 4;
            int di = x * 4;
            row[di + 0] = rgba[si + 2];
            row[di + 1] = rgba[si + 1];
            row[di + 2] = rgba[si + 0];
            row[di + 3] = rgba[si + 3];
        }
        if (fwrite(row, 1, (size_t)w * 4u, f) != (size_t)w * 4u) {
            free(row);
            fclose(f);
            if (err && err_sz > 0) snprintf(err, err_sz, "failed to write TGA pixels");
            return 0;
        }
    }

    free(row);
    fclose(f);
    return 1;
}

static int lua_assets_build_color_masks(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* source_rel = luaL_checkstring(Ls, 1);
    const char* out_dir = "cache/assets";
    const char* key_in = "asset";
    char key[96];
    char source_full[MAX_PATH];
    char out_full_dir[MAX_PATH];
    char err[256];
    ModColorMaskLayer layers[MOD_COLOR_MASK_MAX_LAYERS];
    int layer_count = 0;
    int masks_index;
    int w = 0;
    int h = 0;
    int comp = 0;
    unsigned char* src = NULL;
    uint8_t* base = NULL;
    uint8_t* layer_pixels[MOD_COLOR_MASK_MAX_LAYERS];
    size_t pixel_count;
    size_t bytes;

    memset(layers, 0, sizeof(layers));
    memset(layer_pixels, 0, sizeof(layer_pixels));
    err[0] = '\0';

    if (!mod || !mod->enabled) {
        lua_pushnil(Ls);
        lua_pushstring(Ls, "mod is not active");
        return 2;
    }
    if (!lua_istable(Ls, 2)) {
        lua_pushnil(Ls);
        lua_pushstring(Ls, "build_color_masks expects an options table");
        return 2;
    }
    if (!p_stbi_load || !p_stbi_image_free ||
        IsBadCodePtr((FARPROC)(void*)p_stbi_load) ||
        IsBadCodePtr((FARPROC)(void*)p_stbi_image_free)) {
        lua_pushnil(Ls);
        lua_pushstring(Ls, "image decoder unavailable");
        return 2;
    }

    lua_getfield(Ls, 2, "out_dir");
    if (lua_isstring(Ls, -1)) out_dir = lua_tostring(Ls, -1);
    lua_pop(Ls, 1);
    lua_getfield(Ls, 2, "key");
    if (lua_isstring(Ls, -1)) key_in = lua_tostring(Ls, -1);
    lua_pop(Ls, 1);
    mod_asset_sanitize_key(key_in, key, (int)sizeof(key));

    lua_getfield(Ls, 2, "masks");
    if (!lua_istable(Ls, -1)) {
        lua_pop(Ls, 1);
        lua_getfield(Ls, 2, "layers");
    }
    if (!lua_istable(Ls, -1)) {
        lua_pop(Ls, 1);
        lua_pushnil(Ls);
        lua_pushstring(Ls, "missing masks table");
        return 2;
    }
    masks_index = lua_gettop(Ls);

    lua_pushnil(Ls);
    while (lua_next(Ls, masks_index) != 0) {
        const char* name = lua_type(Ls, -2) == LUA_TSTRING ? lua_tostring(Ls, -2) : NULL;
        if (name && name[0]) {
            ModColorMaskLayer* layer;
            if (layer_count >= MOD_COLOR_MASK_MAX_LAYERS) {
                lua_pop(Ls, 2);
                lua_pushnil(Ls);
                lua_pushfstring(Ls, "too many color mask layers (max=%d)", MOD_COLOR_MASK_MAX_LAYERS);
                return 2;
            }
            if (!mod_asset_layer_name_valid(name)) {
                lua_pop(Ls, 2);
                lua_pushnil(Ls);
                lua_pushstring(Ls, "color mask layer names may only use letters, numbers, _, or -");
                return 2;
            }
            layer = &layers[layer_count];
            snprintf(layer->name, sizeof(layer->name), "%s", name);
            if (!mod_asset_parse_color_list(Ls, lua_gettop(Ls), layer, err, (int)sizeof(err))) {
                lua_pop(Ls, 2);
                lua_pushnil(Ls);
                lua_pushstring(Ls, err[0] ? err : "invalid color mask");
                return 2;
            }
            if (layer->color_count > 0) layer_count++;
        }
        lua_pop(Ls, 1);
    }
    lua_pop(Ls, 1);

    if (layer_count <= 0) {
        lua_pushnil(Ls);
        lua_pushstring(Ls, "no color mask colors supplied");
        return 2;
    }

    if (!mod_asset_resolve_path(mod, source_rel, source_full, (int)sizeof(source_full), err, (int)sizeof(err)) ||
        !mod_asset_resolve_output_dir(mod, out_dir, out_full_dir, (int)sizeof(out_full_dir), err, (int)sizeof(err))) {
        lua_pushnil(Ls);
        lua_pushstring(Ls, err[0] ? err : "failed to resolve color mask paths");
        return 2;
    }

    src = p_stbi_load(source_full, &w, &h, &comp, 4);
    if (!src || w <= 0 || h <= 0) {
        if (src) p_stbi_image_free(src);
        lua_pushnil(Ls);
        lua_pushfstring(Ls, "failed to decode spritesheet '%s'", source_rel);
        return 2;
    }
    pixel_count = (size_t)w * (size_t)h;
    if (pixel_count == 0 || pixel_count > MOD_COLOR_MASK_MAX_PIXELS) {
        p_stbi_image_free(src);
        lua_pushnil(Ls);
        lua_pushstring(Ls, "spritesheet is too large for color mask generation");
        return 2;
    }
    bytes = pixel_count * 4u;

    base = (uint8_t*)malloc(bytes);
    if (!base) {
        p_stbi_image_free(src);
        lua_pushnil(Ls);
        lua_pushstring(Ls, "out of memory building base color mask");
        return 2;
    }
    memcpy(base, src, bytes);

    for (int li = 0; li < layer_count; li++) {
        layer_pixels[li] = (uint8_t*)calloc(1, bytes);
        if (!layer_pixels[li]) {
            for (int j = 0; j < li; j++) free(layer_pixels[j]);
            free(base);
            p_stbi_image_free(src);
            lua_pushnil(Ls);
            lua_pushstring(Ls, "out of memory building color mask layers");
            return 2;
        }
    }

    for (size_t pi = 0; pi < pixel_count; pi++) {
        size_t bi = pi * 4u;
        uint8_t r = src[bi + 0];
        uint8_t g = src[bi + 1];
        uint8_t b = src[bi + 2];
        uint8_t a = src[bi + 3];
        uint32_t rgb = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        if (a == 0) continue;
        for (int li = 0; li < layer_count; li++) {
            if (!mod_asset_rgb_in_layer(&layers[li], rgb)) continue;
            base[bi + 0] = 0;
            base[bi + 1] = 0;
            base[bi + 2] = 0;
            base[bi + 3] = 0;
            layer_pixels[li][bi + 0] = 255;
            layer_pixels[li][bi + 1] = 255;
            layer_pixels[li][bi + 2] = 255;
            layer_pixels[li][bi + 3] = a;
            layers[li].matched_pixels++;
            break;
        }
    }

    {
        char base_rel[MAX_PATH];
        char base_full[MAX_PATH];
        snprintf(base_rel, sizeof(base_rel), "%s/%s_base.tga", out_dir, key);
        snprintf(base_full, sizeof(base_full), "%s\\%s_base.tga", out_full_dir, key);
        audio_normalize_slashes(base_full);
        if (!mod_assets_write_tga_rgba(base_full, base, w, h, err, (int)sizeof(err))) {
            for (int li = 0; li < layer_count; li++) free(layer_pixels[li]);
            free(base);
            p_stbi_image_free(src);
            lua_pushnil(Ls);
            lua_pushstring(Ls, err[0] ? err : "failed to write base mask");
            return 2;
        }

        for (int li = 0; li < layer_count; li++) {
            snprintf(layers[li].relpath, sizeof(layers[li].relpath), "%s/%s_%s.tga", out_dir, key, layers[li].name);
            snprintf(layers[li].fullpath, sizeof(layers[li].fullpath), "%s\\%s_%s.tga", out_full_dir, key, layers[li].name);
            audio_normalize_slashes(layers[li].fullpath);
            if (layers[li].matched_pixels > 0 &&
                !mod_assets_write_tga_rgba(layers[li].fullpath, layer_pixels[li], w, h, err, (int)sizeof(err))) {
                for (int j = 0; j < layer_count; j++) free(layer_pixels[j]);
                free(base);
                p_stbi_image_free(src);
                lua_pushnil(Ls);
                lua_pushstring(Ls, err[0] ? err : "failed to write color mask layer");
                return 2;
            }
        }

        lua_newtable(Ls);
        lua_pushstring(Ls, base_rel); lua_setfield(Ls, -2, "base");
        lua_pushinteger(Ls, w); lua_setfield(Ls, -2, "w");
        lua_pushinteger(Ls, h); lua_setfield(Ls, -2, "h");
        lua_newtable(Ls);
        for (int li = 0; li < layer_count; li++) {
            if (layers[li].matched_pixels > 0) {
                lua_pushstring(Ls, layers[li].relpath);
                lua_setfield(Ls, -2, layers[li].name);
            }
        }
        lua_setfield(Ls, -2, "layers");
        lua_newtable(Ls);
        for (int li = 0; li < layer_count; li++) {
            lua_pushinteger(Ls, (lua_Integer)layers[li].matched_pixels);
            lua_setfield(Ls, -2, layers[li].name);
        }
        lua_setfield(Ls, -2, "counts");
    }

    for (int li = 0; li < layer_count; li++) free(layer_pixels[li]);
    free(base);
    p_stbi_image_free(src);
    return 1;
}

static int mod_asset_sheet_find(const LoadedMod* mod, const char* id) {
    if (!mod || !id || !id[0]) return -1;
    for (int i = 0; i < mod->asset_sheet_count; i++) {
        if (_stricmp(mod->asset_sheets[i].id, id) == 0) return i;
    }
    return -1;
}

static int mod_asset_sheet_reserve(LoadedMod* mod, int want_count) {
    if (!mod) return 0;
    if (want_count <= mod->asset_sheet_cap) return 1;
    int newcap = (mod->asset_sheet_cap == 0) ? 4 : mod->asset_sheet_cap * 2;
    while (newcap < want_count) newcap *= 2;
    ModAssetSheet* ns = (ModAssetSheet*)realloc(mod->asset_sheets, sizeof(ModAssetSheet) * newcap);
    if (!ns) return 0;
    mod->asset_sheets = ns;
    mod->asset_sheet_cap = newcap;
    return 1;
}

static void mod_asset_sheets_clear(LoadedMod* mod) {
    if (!mod) return;
    free(mod->asset_sheets);
    mod->asset_sheets = NULL;
    mod->asset_sheet_count = 0;
    mod->asset_sheet_cap = 0;
    mod->asset_batch_depth = 0;
    mod->asset_batch_dirty = 0;
}

static void lua_push_asset_sheet_info(lua_State* Ls, const ModAssetSheet* s) {
    lua_newtable(Ls);
    if (!s) return;
    lua_pushstring(Ls, s->id); lua_setfield(Ls, -2, "id");
    lua_pushstring(Ls, s->relpath); lua_setfield(Ls, -2, "path");
    lua_pushstring(Ls, s->fullpath); lua_setfield(Ls, -2, "full_path");
    lua_pushinteger(Ls, s->base_id); lua_setfield(Ls, -2, "base_id");
    lua_pushinteger(Ls, s->base_id); lua_setfield(Ls, -2, "base");
    lua_pushinteger(Ls, s->count); lua_setfield(Ls, -2, "count");
    lua_pushinteger(Ls, s->cell_w); lua_setfield(Ls, -2, "cell_w");
    lua_pushinteger(Ls, s->cell_h); lua_setfield(Ls, -2, "cell_h");
    lua_pushinteger(Ls, s->padding); lua_setfield(Ls, -2, "padding");
    lua_pushinteger(Ls, (lua_Integer)s->flags); lua_setfield(Ls, -2, "flags");
}

static int mod_assets_native_load_ready(void) {
    if (!p_atlas_load_spritesheet || !p_sprite_count) return 0;
    if (IsBadCodePtr((FARPROC)(void*)p_atlas_load_spritesheet) ||
        IsBadCodePtr((FARPROC)(void*)p_sprite_count)) {
        return 0;
    }
    return 1;
}

static int mod_assets_can_rebuild_now(void) {
    if (!mod_assets_native_load_ready()) return 0;
    if (!p_atlas_exit || !p_sprites_reset || !p_load_gfx) return 0;
    if (IsBadCodePtr((FARPROC)(void*)p_atlas_exit) ||
        IsBadCodePtr((FARPROC)(void*)p_sprites_reset) ||
        IsBadCodePtr((FARPROC)(void*)p_load_gfx)) {
        return 0;
    }
    return p_sprite_count() > 0;
}

void lua_manager_before_atlas_upload(int atlas_ptr) {
    if (g_mod_asset_injection_active) return;
    if (!atlas_ptr || !mod_assets_native_load_ready()) return;
    if (!p_freetype_atlas || !ptr_readable((const void*)p_freetype_atlas, sizeof(uintptr_t)) || *p_freetype_atlas == 0) {
        return;
    }

    g_mod_asset_injection_active = 1;
    for (int mi = 0; mi < g_mod_count; mi++) {
        LoadedMod* mod = &g_mods[mi];
        if (!mod->enabled || mod->asset_sheet_count <= 0) continue;
        for (int si = 0; si < mod->asset_sheet_count; si++) {
            ModAssetSheet* sheet = &mod->asset_sheets[si];
            int before;
            int after;
            int loaded;

            sheet->base_id = -1;
            sheet->count = 0;
            if (!sheet->fullpath[0] || !audio_file_exists(sheet->fullpath)) {
                LOG_WARN("mod.assets: missing spritesheet for %s:%s (%s)",
                         mod->id[0] ? mod->id : "?",
                         sheet->id,
                         sheet->relpath);
                continue;
            }

            before = p_sprite_count();
            loaded = p_atlas_load_spritesheet(atlas_ptr,
                                              NULL,
                                              sheet->cell_w,
                                              sheet->cell_h,
                                              sheet->padding,
                                              sheet->flags,
                                              sheet->fullpath);
            after = p_sprite_count();
            if (loaded >= 0 && after > before) {
                sheet->base_id = before;
                sheet->count = after - before;
                LOG_INFO("mod.assets: packed %s:%s base=%d count=%d",
                         mod->id[0] ? mod->id : "?",
                         sheet->id,
                         sheet->base_id,
                         sheet->count);
            } else {
                LOG_WARN("mod.assets: failed to pack %s:%s (%s), code=%d",
                         mod->id[0] ? mod->id : "?",
                         sheet->id,
                         sheet->relpath,
                         loaded);
            }
        }
        mod->asset_batch_dirty = 0;
    }
    g_mod_asset_injection_active = 0;
}

static int mod_assets_flush_batch(LoadedMod* mod, const char* reason, char* err, int err_sz) {
    if (!mod) {
        if (err && err_sz > 0) snprintf(err, err_sz, "mod is not active");
        return 0;
    }
    if (mod->asset_batch_depth > 0 || !mod->asset_batch_dirty) {
        return 1;
    }
    if (!mod_assets_can_rebuild_now()) {
        if (err && err_sz > 0) snprintf(err, err_sz, "assets registered; waiting for graphics atlas rebuild");
        return 0;
    }
    if (!reload_engine_gfx_atlases(reason && reason[0] ? reason : "mod asset spritesheet batch registered")) {
        if (err && err_sz > 0) snprintf(err, err_sz, "assets registered, but atlas rebuild failed");
        return 0;
    }
    mod->asset_batch_dirty = 0;
    return 1;
}

static int lua_assets_load_spritesheet(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* id = luaL_checkstring(Ls, 1);
    const char* rel_path = luaL_checkstring(Ls, 2);
    int cell_w = 16;
    int cell_h = 16;
    int padding = 0;
    uint32_t flags = 1u;
    int force = 0;
    int existing;
    char full_path[MAX_PATH];
    char err[256];
    ModAssetSheet* sheet;

    err[0] = '\0';
    if (!mod || !mod->enabled) {
        lua_pushnil(Ls);
        lua_pushstring(Ls, "mod is not active");
        return 2;
    }
    if (!mod_asset_id_valid(id)) {
        lua_pushnil(Ls);
        lua_pushstring(Ls, "asset id must be 1..63 chars using letters, numbers, _, -, ., or :");
        return 2;
    }

    if (lua_istable(Ls, 3)) {
        lua_getfield(Ls, 3, "cell_w");
        if (lua_isnumber(Ls, -1)) cell_w = (int)lua_tointeger(Ls, -1);
        lua_pop(Ls, 1);
        lua_getfield(Ls, 3, "cell_h");
        if (lua_isnumber(Ls, -1)) cell_h = (int)lua_tointeger(Ls, -1);
        lua_pop(Ls, 1);
        lua_getfield(Ls, 3, "padding");
        if (lua_isnumber(Ls, -1)) padding = (int)lua_tointeger(Ls, -1);
        lua_pop(Ls, 1);
        lua_getfield(Ls, 3, "flags");
        if (lua_isnumber(Ls, -1)) flags = (uint32_t)lua_tointeger(Ls, -1);
        lua_pop(Ls, 1);
        lua_getfield(Ls, 3, "force");
        if (lua_isboolean(Ls, -1)) force = lua_toboolean(Ls, -1) ? 1 : 0;
        lua_pop(Ls, 1);
    }

    if (cell_w <= 0 || cell_h <= 0 || cell_w > 512 || cell_h > 512 || padding < 0 || padding > 64) {
        lua_pushnil(Ls);
        lua_pushstring(Ls, "invalid spritesheet cell size or padding");
        return 2;
    }

    existing = mod_asset_sheet_find(mod, id);
    if (existing >= 0 && !force && mod->asset_sheets[existing].count > 0) {
        lua_push_asset_sheet_info(Ls, &mod->asset_sheets[existing]);
        return 1;
    }

    if (!mod_asset_resolve_path(mod, rel_path, full_path, (int)sizeof(full_path), err, (int)sizeof(err))) {
        lua_pushnil(Ls);
        lua_pushstring(Ls, err[0] ? err : "failed to resolve asset path");
        return 2;
    }

    if (existing >= 0) {
        sheet = &mod->asset_sheets[existing];
    } else {
        if (!mod_asset_sheet_reserve(mod, mod->asset_sheet_count + 1)) {
            lua_pushnil(Ls);
            lua_pushstring(Ls, "failed to store asset sheet metadata");
            return 2;
        }
        sheet = &mod->asset_sheets[mod->asset_sheet_count++];
        memset(sheet, 0, sizeof(*sheet));
    }

    snprintf(sheet->id, sizeof(sheet->id), "%s", id);
    snprintf(sheet->relpath, sizeof(sheet->relpath), "%s", rel_path);
    snprintf(sheet->fullpath, sizeof(sheet->fullpath), "%s", full_path);
    sheet->base_id = -1;
    sheet->count = 0;
    sheet->cell_w = cell_w;
    sheet->cell_h = cell_h;
    sheet->padding = padding;
    sheet->flags = flags;

    if (mod->asset_batch_depth > 0) {
        mod->asset_batch_dirty = 1;
        lua_push_asset_sheet_info(Ls, sheet);
        return 1;
    }

    if (!mod_assets_can_rebuild_now()) {
        lua_pushnil(Ls);
        lua_pushstring(Ls, "asset registered; waiting for graphics atlas rebuild");
        return 2;
    }

    if (!reload_engine_gfx_atlases("mod asset spritesheet registered")) {
        lua_pushnil(Ls);
        lua_pushstring(Ls, "asset registered, but atlas rebuild failed");
        return 2;
    }

    if (sheet->count <= 0 || sheet->base_id < 0) {
        lua_pushnil(Ls);
        lua_pushfstring(Ls, "failed to pack spritesheet '%s'", rel_path);
        return 2;
    }

    lua_push_asset_sheet_info(Ls, sheet);
    return 1;
}

static int lua_assets_begin_batch(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    if (!mod || !mod->enabled) {
        lua_pushnil(Ls);
        lua_pushstring(Ls, "mod is not active");
        return 2;
    }
    if (mod->asset_batch_depth < 32) {
        mod->asset_batch_depth++;
    }
    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_assets_end_batch(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    char err[256];
    err[0] = '\0';

    if (!mod || !mod->enabled) {
        lua_pushnil(Ls);
        lua_pushstring(Ls, "mod is not active");
        return 2;
    }
    if (mod->asset_batch_depth > 0) {
        mod->asset_batch_depth--;
    }
    if (!mod_assets_flush_batch(mod, "mod asset spritesheet batch registered", err, (int)sizeof(err))) {
        lua_pushnil(Ls);
        lua_pushstring(Ls, err[0] ? err : "asset batch flush failed");
        return 2;
    }
    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_assets_cancel_batch(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    if (!mod || !mod->enabled) {
        lua_pushnil(Ls);
        lua_pushstring(Ls, "mod is not active");
        return 2;
    }
    mod->asset_batch_depth = 0;
    mod->asset_batch_dirty = 0;
    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_assets_sprite_id(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* id = luaL_checkstring(Ls, 1);
    int index = (int)luaL_optinteger(Ls, 2, 0);
    int found = mod_asset_sheet_find(mod, id);
    if (found < 0) {
        lua_pushnil(Ls);
        lua_pushfstring(Ls, "unknown asset sheet '%s'", id);
        return 2;
    }
    if (index < 0 || index >= mod->asset_sheets[found].count) {
        lua_pushnil(Ls);
        lua_pushstring(Ls, "asset sprite index out of range");
        return 2;
    }
    lua_pushinteger(Ls, mod->asset_sheets[found].base_id + index);
    return 1;
}

static int lua_assets_info(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    if (lua_isnoneornil(Ls, 1)) {
        lua_newtable(Ls);
        if (mod) {
            for (int i = 0; i < mod->asset_sheet_count; i++) {
                lua_push_asset_sheet_info(Ls, &mod->asset_sheets[i]);
                lua_rawseti(Ls, -2, i + 1);
            }
        }
        return 1;
    }

    {
        const char* id = luaL_checkstring(Ls, 1);
        int found = mod_asset_sheet_find(mod, id);
        if (found < 0) {
            lua_pushnil(Ls);
            return 1;
        }
        lua_push_asset_sheet_info(Ls, &mod->asset_sheets[found]);
        return 1;
    }
}

static void push_assets_api_table(lua_State* Ls, LoadedMod* mod) {
    lua_newtable(Ls);
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_assets_load_spritesheet, 1); lua_setfield(Ls, -2, "load_spritesheet");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_assets_begin_batch, 1); lua_setfield(Ls, -2, "begin_batch");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_assets_end_batch, 1); lua_setfield(Ls, -2, "end_batch");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_assets_cancel_batch, 1); lua_setfield(Ls, -2, "cancel_batch");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_assets_build_color_masks, 1); lua_setfield(Ls, -2, "build_color_masks");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_assets_sprite_id, 1);        lua_setfield(Ls, -2, "sprite_id");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_assets_info, 1);             lua_setfield(Ls, -2, "info");
}

static int audio_opts_get_int(lua_State* Ls, int arg_index, const char* key, int fallback) {
    int out = fallback;
    if (!lua_istable(Ls, arg_index)) return out;
    lua_getfield(Ls, arg_index, key);
    if (lua_isnumber(Ls, -1)) out = (int)lua_tointeger(Ls, -1);
    lua_pop(Ls, 1);
    return out;
}

static float audio_opts_get_float(lua_State* Ls, int arg_index, const char* key, float fallback) {
    float out = fallback;
    if (!lua_istable(Ls, arg_index)) return out;
    lua_getfield(Ls, arg_index, key);
    if (lua_isnumber(Ls, -1)) out = (float)lua_tonumber(Ls, -1);
    lua_pop(Ls, 1);
    return out;
}

static const char* audio_mix_error(void) {
    if (p_mix_get_error) {
        const char* err = p_mix_get_error();
        if (err && err[0]) return err;
    }
    {
        const char* sdl_err = SDL_GetError();
        if (sdl_err && sdl_err[0]) return sdl_err;
    }
    return "unknown SDL_mixer error";
}

static void audio_set_backend_error(const char* reason) {
    if (!reason) reason = "audio backend unavailable";
    snprintf(g_audio_backend_error, sizeof(g_audio_backend_error), "%s", reason);
}

static const char* audio_backend_error_message(void) {
    if (g_audio_backend_error[0]) return g_audio_backend_error;
    return "audio mixer is not available";
}

static int audio_path_has_wav_extension(const char* path) {
    const char* dot;
    if (!path || !path[0]) return 0;
    dot = strrchr(path, '.');
    if (!dot) return 0;
    return (_stricmp(dot, ".wav") == 0);
}

static int audio_winmm_ensure_ready(void) {
    if (g_audio_winmm_ready && p_play_sound_a) return 1;

    if (!g_audio_winmm_module) {
        g_audio_winmm_module = LoadLibraryA("winmm.dll");
        if (!g_audio_winmm_module) return 0;
    }

    p_play_sound_a = (fn_play_sound_a_t)GetProcAddress(g_audio_winmm_module, "PlaySoundA");
    if (!p_play_sound_a) return 0;
    g_audio_winmm_ready = 1;
    return 1;
}

static int audio_fallback_play_wav(const char* full_path, int loop, int no_stop, char* err, int err_sz) {
    DWORD flags = SND_FILENAME | SND_ASYNC | SND_NODEFAULT;
    if (err && err_sz > 0) err[0] = '\0';

    if (!full_path || !full_path[0]) {
        if (err && err_sz > 0) snprintf(err, err_sz, "missing wav path");
        return 0;
    }
    if (!audio_file_exists(full_path)) {
        if (err && err_sz > 0) snprintf(err, err_sz, "file not found: %s", full_path);
        return 0;
    }
    if (!audio_path_has_wav_extension(full_path)) {
        if (err && err_sz > 0) snprintf(err, err_sz, "fallback backend only supports .wav files");
        return 0;
    }
    if (!audio_winmm_ensure_ready()) {
        if (err && err_sz > 0) snprintf(err, err_sz, "winmm fallback unavailable");
        return 0;
    }

    if (loop) flags |= SND_LOOP;
    if (no_stop) flags |= SND_NOSTOP;

    if (!p_play_sound_a(full_path, NULL, flags)) {
        if (err && err_sz > 0) snprintf(err, err_sz, "PlaySoundA failed");
        return 0;
    }

    if (!g_audio_winmm_warned) {
        LOG_WARN("Audio API: using WinMM fallback backend (.wav only; limited mixing/volume control)");
        g_audio_winmm_warned = 1;
    }

    return 1;
}

static void audio_fallback_stop_music_for_owner(LoadedMod* owner) {
    if (owner && g_audio_fallback_music_owner != owner) return;
    if (!g_audio_fallback_music_owner) return;
    if (!audio_winmm_ensure_ready()) return;

    p_play_sound_a(NULL, NULL, 0);
    g_audio_fallback_music_owner = NULL;
}

static int audio_runtime_disable(const char* reason) {
    audio_set_backend_error(reason && reason[0] ? reason : "audio backend unavailable");

    if (!g_audio_mixer_warned) {
        if (reason && reason[0]) LOG_WARN("%s", reason);
        else LOG_WARN("Audio API disabled");
        g_audio_mixer_warned = 1;
    }

    g_audio_mixer_disabled = 1;
    g_audio_mixer_ready = 0;
    g_audio_channels_reserved = 0;
    g_audio_next_channel_base = 0;
    g_audio_music_owner = NULL;
    g_audio_music = NULL;

    if (g_audio_mixer_module) {
        FreeLibrary(g_audio_mixer_module);
        g_audio_mixer_module = NULL;
    }

    p_mix_open_audio = NULL;
    p_mix_close_audio = NULL;
    p_mix_allocate_channels = NULL;
    p_mix_load_wav_rw = NULL;
    p_mix_play_channel_timed = NULL;
    p_mix_playing = NULL;
    p_mix_halt_channel = NULL;
    p_mix_volume_channel = NULL;
    p_mix_free_chunk = NULL;
    p_mix_load_mus = NULL;
    p_mix_play_music = NULL;
    p_mix_halt_music = NULL;
    p_mix_volume_music = NULL;
    p_mix_free_music = NULL;
    p_mix_get_error = NULL;
    return 0;
}

static int audio_runtime_ensure_ready(void) {
    int ch;
    if (g_audio_mixer_ready) return 1;
    if (g_audio_mixer_disabled) return 0;

    g_audio_mixer_module = LoadLibraryA("SDL2_mixer.dll");
    if (!g_audio_mixer_module) {
        return audio_runtime_disable("Audio API unavailable: could not load SDL2_mixer.dll");
    }

    p_mix_open_audio = (fn_mix_open_audio_t)GetProcAddress(g_audio_mixer_module, "Mix_OpenAudio");
    p_mix_close_audio = (fn_mix_close_audio_t)GetProcAddress(g_audio_mixer_module, "Mix_CloseAudio");
    p_mix_allocate_channels = (fn_mix_allocate_channels_t)GetProcAddress(g_audio_mixer_module, "Mix_AllocateChannels");
    p_mix_load_wav_rw = (fn_mix_load_wav_rw_t)GetProcAddress(g_audio_mixer_module, "Mix_LoadWAV_RW");
    p_mix_play_channel_timed = (fn_mix_play_channel_timed_t)GetProcAddress(g_audio_mixer_module, "Mix_PlayChannelTimed");
    p_mix_playing = (fn_mix_playing_t)GetProcAddress(g_audio_mixer_module, "Mix_Playing");
    p_mix_halt_channel = (fn_mix_halt_channel_t)GetProcAddress(g_audio_mixer_module, "Mix_HaltChannel");
    p_mix_volume_channel = (fn_mix_volume_channel_t)GetProcAddress(g_audio_mixer_module, "Mix_Volume");
    p_mix_free_chunk = (fn_mix_free_chunk_t)GetProcAddress(g_audio_mixer_module, "Mix_FreeChunk");
    p_mix_load_mus = (fn_mix_load_mus_t)GetProcAddress(g_audio_mixer_module, "Mix_LoadMUS");
    p_mix_play_music = (fn_mix_play_music_t)GetProcAddress(g_audio_mixer_module, "Mix_PlayMusic");
    p_mix_halt_music = (fn_mix_halt_music_t)GetProcAddress(g_audio_mixer_module, "Mix_HaltMusic");
    p_mix_volume_music = (fn_mix_volume_music_t)GetProcAddress(g_audio_mixer_module, "Mix_VolumeMusic");
    p_mix_free_music = (fn_mix_free_music_t)GetProcAddress(g_audio_mixer_module, "Mix_FreeMusic");
    p_mix_get_error = (fn_mix_get_error_t)GetProcAddress(g_audio_mixer_module, "Mix_GetError");

    if (!p_mix_open_audio ||
        !p_mix_close_audio ||
        !p_mix_allocate_channels ||
        !p_mix_load_wav_rw ||
        !p_mix_play_channel_timed ||
        !p_mix_playing ||
        !p_mix_halt_channel ||
        !p_mix_volume_channel ||
        !p_mix_free_chunk ||
        !p_mix_load_mus ||
        !p_mix_play_music ||
        !p_mix_halt_music ||
        !p_mix_volume_music ||
        !p_mix_free_music) {
        return audio_runtime_disable("Audio API unavailable: SDL2_mixer.dll is missing required exports");
    }

    if (p_mix_open_audio(22050, (unsigned short)AUDIO_MIX_FORMAT_S16SYS, 2, 1024) < 0) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Audio API unavailable: Mix_OpenAudio failed (%s)", audio_mix_error());
        return audio_runtime_disable(msg);
    }

    ch = p_mix_allocate_channels(AUDIO_DEFAULT_CHANNELS);
    if (ch < AUDIO_DEFAULT_CHANNELS) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Audio API warning: requested %d mixer channels, got %d", AUDIO_DEFAULT_CHANNELS, ch);
        LOG_WARN("%s", msg);
    }

    g_audio_channels_reserved = (ch > 0) ? ch : AUDIO_DEFAULT_CHANNELS;
    g_audio_next_channel_base = 0;
    g_audio_mixer_ready = 1;
    return 1;
}

static void audio_release_music_for_owner(LoadedMod* owner) {
    if (owner && g_audio_music_owner != owner) return;
    if (!g_audio_music && !g_audio_music_owner) return;

    if (g_audio_mixer_ready && p_mix_halt_music) {
        p_mix_halt_music();
    }
    if (g_audio_music && p_mix_free_music) {
        p_mix_free_music(g_audio_music);
    }

    g_audio_music = NULL;
    g_audio_music_owner = NULL;
}

static void audio_runtime_reset_channels(void) {
    g_audio_next_channel_base = 0;
    if (!g_audio_mixer_ready || !p_mix_allocate_channels) {
        g_audio_channels_reserved = 0;
        return;
    }
    {
        int ch = p_mix_allocate_channels(AUDIO_DEFAULT_CHANNELS);
        g_audio_channels_reserved = (ch > 0) ? ch : AUDIO_DEFAULT_CHANNELS;
    }
}

static int mod_audio_chunk_reserve(LoadedMod* mod, int want_count) {
    int newcap;
    AudioChunkCacheEntry* nc;
    if (!mod) return 0;
    if (want_count <= mod->audio_chunk_cap) return 1;

    newcap = (mod->audio_chunk_cap == 0) ? 8 : (mod->audio_chunk_cap * 2);
    while (newcap < want_count) newcap *= 2;

    nc = (AudioChunkCacheEntry*)realloc(mod->audio_chunks, sizeof(AudioChunkCacheEntry) * newcap);
    if (!nc) return 0;

    mod->audio_chunks = nc;
    mod->audio_chunk_cap = newcap;
    return 1;
}

static Mix_Chunk* mod_audio_find_chunk(LoadedMod* mod, const char* full_path) {
    if (!mod || !full_path || !full_path[0]) return NULL;
    for (int i = 0; i < mod->audio_chunk_count; i++) {
        AudioChunkCacheEntry* e = &mod->audio_chunks[i];
        if (_stricmp(e->path, full_path) == 0) return e->chunk;
    }
    return NULL;
}

static Mix_Chunk* mod_audio_get_or_load_chunk(LoadedMod* mod, const char* full_path, char* err, int err_sz) {
    Mix_Chunk* chunk;
    SDL_RWops* rw;

    if (err && err_sz > 0) err[0] = '\0';
    if (!mod || !full_path || !full_path[0]) {
        if (err && err_sz > 0) snprintf(err, err_sz, "missing sound path");
        return NULL;
    }

    chunk = mod_audio_find_chunk(mod, full_path);
    if (chunk) return chunk;

    if (!audio_file_exists(full_path)) {
        if (err && err_sz > 0) snprintf(err, err_sz, "file not found: %s", full_path);
        return NULL;
    }

    rw = SDL_RWFromFile(full_path, "rb");
    if (!rw) {
        if (err && err_sz > 0) snprintf(err, err_sz, "failed to open %s", full_path);
        return NULL;
    }

    chunk = p_mix_load_wav_rw(rw, 1);
    if (!chunk) {
        if (err && err_sz > 0) snprintf(err, err_sz, "Mix_LoadWAV_RW failed: %s", audio_mix_error());
        return NULL;
    }

    if (!mod_audio_chunk_reserve(mod, mod->audio_chunk_count + 1)) {
        if (p_mix_free_chunk) p_mix_free_chunk(chunk);
        if (err && err_sz > 0) snprintf(err, err_sz, "out of memory");
        return NULL;
    }

    {
        AudioChunkCacheEntry* e = &mod->audio_chunks[mod->audio_chunk_count++];
        memset(e, 0, sizeof(*e));
        strncpy(e->path, full_path, sizeof(e->path) - 1);
        e->chunk = chunk;
    }
    return chunk;
}

static int mod_audio_ensure_channel_range(LoadedMod* mod, char* err, int err_sz) {
    int needed;
    int got;
    if (!mod) {
        if (err && err_sz > 0) snprintf(err, err_sz, "no mod context");
        return 0;
    }
    if (mod->audio_channel_base >= 0 && mod->audio_channel_count > 0) return 1;

    mod->audio_channel_base = g_audio_next_channel_base;
    mod->audio_channel_count = AUDIO_CHANNELS_PER_MOD;
    mod->audio_next_channel = 0;
    g_audio_next_channel_base += AUDIO_CHANNELS_PER_MOD;

    needed = mod->audio_channel_base + mod->audio_channel_count;
    if (needed > g_audio_channels_reserved) {
        got = p_mix_allocate_channels(needed);
        if (got < needed) {
            if (err && err_sz > 0) snprintf(err, err_sz, "Mix_AllocateChannels failed (%d/%d)", got, needed);
            return 0;
        }
        g_audio_channels_reserved = got;
    }

    return 1;
}

static int mod_audio_pick_channel(LoadedMod* mod) {
    int start;
    int count;
    int ch;
    int i;
    if (!mod || mod->audio_channel_base < 0 || mod->audio_channel_count <= 0) return -1;

    start = mod->audio_channel_base;
    count = mod->audio_channel_count;

    for (i = 0; i < count; i++) {
        ch = start + ((mod->audio_next_channel + i) % count);
        if (!p_mix_playing(ch)) {
            mod->audio_next_channel = (ch - start + 1) % count;
            return ch;
        }
    }

    ch = start + (mod->audio_next_channel % count);
    mod->audio_next_channel = (mod->audio_next_channel + 1) % count;
    if (p_mix_halt_channel) p_mix_halt_channel(ch);
    return ch;
}

static int audio_play_builtin_sfx(LoadedMod* mod, lua_State* Ls, const char* id, int opts_index) {
    float volume = mod ? mod->audio_sfx_volume : 1.0f;
    if (!id || !id[0]) return 0;
    volume *= audio_opts_get_float(Ls, opts_index, "volume", 1.0f);
    volume = audio_clampf(volume, 0.0f, 1.0f);
    if (volume <= 0.0f) return 1;

    if (_stricmp(id, "pip") == 0 && p_sound_pip) {
        float pitch = audio_opts_get_float(Ls, opts_index, "pitch", 1.0f);
        int duration = audio_opts_get_int(Ls, opts_index, "duration", 100);
        if (duration < 1) duration = 1;
        p_sound_pip(pitch, duration);
        return 1;
    }
    if (_stricmp(id, "noise") == 0 && p_sound_noise) {
        float freq = audio_opts_get_float(Ls, opts_index, "freq", 250.0f);
        int duration = audio_opts_get_int(Ls, opts_index, "duration", 100);
        if (duration < 1) duration = 1;
        p_sound_noise(freq, duration);
        return 1;
    }
    if (_stricmp(id, "thump") == 0 && p_sound_thump) {
        float freq = audio_opts_get_float(Ls, opts_index, "freq", 250.0f);
        p_sound_thump(freq);
        return 1;
    }
    if (_stricmp(id, "shred") == 0 && p_sound_shred) {
        float amount = audio_opts_get_float(Ls, opts_index, "amount", 1.0f);
        int duration = audio_opts_get_int(Ls, opts_index, "duration", 250);
        if (duration < 1) duration = 1;
        p_sound_shred(amount, duration);
        return 1;
    }
    if (_stricmp(id, "fm") == 0 && p_sound_fm) {
        float carrier = audio_opts_get_float(Ls, opts_index, "carrier", 5.0f);
        float mod_freq = audio_opts_get_float(Ls, opts_index, "mod", 1000.0f);
        float index = audio_opts_get_float(Ls, opts_index, "index", 100.0f);
        p_sound_fm(carrier, mod_freq, index);
        return 1;
    }
    if (_stricmp(id, "ringmod") == 0 && p_sound_ringmod) {
        float freq = audio_opts_get_float(Ls, opts_index, "freq", 3.0f);
        int duration = audio_opts_get_int(Ls, opts_index, "duration", 50);
        if (duration < 1) duration = 1;
        p_sound_ringmod(freq, duration);
        return 1;
    }
    if (_stricmp(id, "warble") == 0 && p_sound_warble) {
        float amount = audio_opts_get_float(Ls, opts_index, "amount", 1.0f);
        p_sound_warble(amount);
        return 1;
    }
    if (_stricmp(id, "creepy") == 0 && p_sound_creepy) {
        float freq = audio_opts_get_float(Ls, opts_index, "freq", 50.0f);
        p_sound_creepy(freq);
        return 1;
    }
    if (_stricmp(id, "pulse") == 0 && p_sound_pulse) {
        float pitch = audio_opts_get_float(Ls, opts_index, "pitch", 1.0f);
        int duration = audio_opts_get_int(Ls, opts_index, "duration", 50);
        if (duration < 1) duration = 1;
        p_sound_pulse(pitch, duration);
        return 1;
    }
    if ((_stricmp(id, "sword_ching") == 0 || _stricmp(id, "ching") == 0) && p_sound_sword_ching) {
        float pitch = audio_opts_get_float(Ls, opts_index, "pitch", 1.0f);
        float tone = audio_opts_get_float(Ls, opts_index, "tone", 1.0f);
        p_sound_sword_ching(pitch, tone);
        return 1;
    }

    return 0;
}

static void mod_audio_clear(LoadedMod* mod) {
    if (!mod) return;

    audio_release_music_for_owner(mod);
    audio_fallback_stop_music_for_owner(mod);

    if (g_audio_mixer_ready && p_mix_halt_channel &&
        mod->audio_channel_base >= 0 && mod->audio_channel_count > 0) {
        int start = mod->audio_channel_base;
        int end = start + mod->audio_channel_count;
        for (int ch = start; ch < end; ch++) {
            p_mix_halt_channel(ch);
        }
    }

    if (mod->audio_chunks) {
        for (int i = 0; i < mod->audio_chunk_count; i++) {
            if (mod->audio_chunks[i].chunk && p_mix_free_chunk) {
                p_mix_free_chunk(mod->audio_chunks[i].chunk);
            }
        }
        free(mod->audio_chunks);
        mod->audio_chunks = NULL;
    }
    mod->audio_chunk_count = 0;
    mod->audio_chunk_cap = 0;
    mod->audio_channel_base = -1;
    mod->audio_channel_count = 0;
    mod->audio_next_channel = 0;
}

static void audio_runtime_shutdown(void) {
    audio_release_music_for_owner(NULL);
    audio_fallback_stop_music_for_owner(NULL);

    if (g_audio_mixer_ready && p_mix_close_audio) {
        p_mix_close_audio();
    }

    if (g_audio_mixer_module) {
        FreeLibrary(g_audio_mixer_module);
        g_audio_mixer_module = NULL;
    }
    if (g_audio_winmm_module) {
        FreeLibrary(g_audio_winmm_module);
        g_audio_winmm_module = NULL;
    }

    g_audio_mixer_ready = 0;
    g_audio_mixer_disabled = 0;
    g_audio_mixer_warned = 0;
    g_audio_winmm_ready = 0;
    g_audio_winmm_warned = 0;
    g_audio_channels_reserved = 0;
    g_audio_next_channel_base = 0;
    g_audio_music_owner = NULL;
    g_audio_music = NULL;
    g_audio_fallback_music_owner = NULL;
    g_audio_backend_error[0] = '\0';

    p_mix_open_audio = NULL;
    p_mix_close_audio = NULL;
    p_mix_allocate_channels = NULL;
    p_mix_load_wav_rw = NULL;
    p_mix_play_channel_timed = NULL;
    p_mix_playing = NULL;
    p_mix_halt_channel = NULL;
    p_mix_volume_channel = NULL;
    p_mix_free_chunk = NULL;
    p_mix_load_mus = NULL;
    p_mix_play_music = NULL;
    p_mix_halt_music = NULL;
    p_mix_volume_music = NULL;
    p_mix_free_music = NULL;
    p_mix_get_error = NULL;
    p_play_sound_a = NULL;
}

static int lua_os_exit_status(lua_State* Ls, int arg) {
    if (lua_isnoneornil(Ls, arg)) return 0;
    if (lua_isboolean(Ls, arg)) return lua_toboolean(Ls, arg) ? 0 : 1;
    return (int)luaL_checkinteger(Ls, arg);
}

static int lua_mod_os_exit(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    int exit_code = lua_os_exit_status(Ls, 1);
    char buf[256];

    snprintf(buf, sizeof(buf),
             "os.exit(%d) called; forcing the crash handler path",
             exit_code);
    log_mod(mod, "ERROR", buf);

    luna_force_crash_report((unsigned int)exit_code);
    return 0;
}

static void push_mod_os_table(lua_State* Ls, LoadedMod* mod) {
    /* Only time/date helpers are exposed. Notably absent: os.execute (shell),
       os.remove / os.rename (file deletion), os.getenv, os.tmpname. */
    static const char* k_safe_os[] = { "time", "clock", "date", "difftime", NULL };

    lua_newtable(Ls); /* safe os */
    lua_getglobal(Ls, "os");
    if (lua_istable(Ls, -1)) {
        for (int i = 0; k_safe_os[i]; i++) {
            lua_getfield(Ls, -1, k_safe_os[i]);
            if (lua_isnil(Ls, -1)) lua_pop(Ls, 1);
            else lua_setfield(Ls, -3, k_safe_os[i]);
        }
    }
    lua_pop(Ls, 1); /* drop real os */

    /* Deliberate exit routes through the crash-handler path, not a clean exit. */
    lua_pushlightuserdata(Ls, mod);
    lua_pushcclosure(Ls, lua_mod_os_exit, 1);
    lua_setfield(Ls, -2, "exit");
}

static void* ui_current_state_ptr(void) {
    return p_state_current ? p_state_current() : NULL;
}

static int ui_is_menu_state_name(const char* name) {
    const char* active_custom = hooks_custom_state_active_name();
    if (!name) return 0;
    if (active_custom && _stricmp(name, active_custom) == 0) return 1;
    return (_stricmp(name, "main") == 0 ||
            _stricmp(name, "main_initial") == 0 ||
            _stricmp(name, "options") == 0 ||
            _stricmp(name, "options_paused") == 0 ||
            _stricmp(name, "pregame") == 0 ||
            _stricmp(name, "remap1") == 0 ||
            _stricmp(name, "remap2") == 0 ||
            _stricmp(name, "mods") == 0);
}

static const char* ui_state_name_from_ptr(void* st) {
    const char* custom_name;
    uintptr_t p = (uintptr_t)st;
    if (!st) return "none";
    if (hooks_mods_menu_active()) return "mods";
    custom_name = hooks_custom_state_name_for_ptr(st);
    if (custom_name && custom_name[0]) return custom_name;
    if (p == (uintptr_t)ADDR_MAIN_STATE) return "main";
    if (p == (uintptr_t)ADDR_MAIN_STATE_INITIAL) return "main_initial";
    if (p == (uintptr_t)ADDR_OPTIONS_STATE) return "options";
    if (p == (uintptr_t)ADDR_OPTIONS_STATE_PAUSED) return "options_paused";
    if (p == (uintptr_t)ADDR_PREGAME_STATE) return "pregame";
    if (p == (uintptr_t)ADDR_REMAP_STATE1) return "remap1";
    if (p == (uintptr_t)ADDR_REMAP_STATE2) return "remap2";
    if (p == (uintptr_t)ADDR_GAME_STATE) return "game";
    if (p == (uintptr_t)ADDR_ERROR_STATE) return "error";
    return "unknown";
}

static int ui_state_matches_name(void* st, const char* name) {
    const char* current = ui_state_name_from_ptr(st);
    if (!name || !name[0]) return 1;
    if (_stricmp(name, current) == 0) return 1;

    // Title-screen scripts often guard UI with is_state("main"). During startup,
    // the game may still report main_initial for a short handoff window.
    if (_stricmp(name, "main") == 0 && _stricmp(current, "main_initial") == 0) return 1;

    if (_stricmp(name, "menu") == 0 && ui_is_menu_state_name(current)) return 1;
    return 0;
}

static float ui_screen_w(void) {
    return p_mad_w ? p_mad_w() : 1280.0f;
}

static float ui_screen_h(void) {
    return p_mad_h ? p_mad_h() : 720.0f;
}

static float ui_clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float ui_sqrt_approx(float v) {
    float x;
    int i;
    if (v <= 0.0f) return 0.0f;
    x = (v >= 1.0f) ? v : 1.0f;
    for (i = 0; i < 5; i++) {
        x = 0.5f * (x + (v / x));
    }
    return x;
}

static float ui_text_window_factor(void) {
    float w = ui_screen_w();
    float h = ui_screen_h();
    float sx = w / 1280.0f;
    float sy = h / 720.0f;
    float s = (sx < sy) ? sx : sy;

    if (s < 1.0f) s = 1.0f;
    return ui_clampf(1.35f * ui_sqrt_approx(s), 1.35f, 2.10f);
}

static float ui_readable_text_scale(float scale) {
    float effective;
    if (scale < 0.85f) scale = 0.85f;
    if (scale > 4.0f) scale = 4.0f;
    effective = scale * ui_text_window_factor();
    return ui_clampf(effective, 1.20f, 5.0f);
}

static float ui_approx_text_width(const char* text, float scale) {
    if (!text) return 0.0f;
    return (float)strlen(text) * 9.0f * ui_readable_text_scale(scale);
}

static float ui_text_line_height(float scale) {
    return 9.0f * ui_readable_text_scale(scale);
}

static void ui_measure_text_bounds(const char* text, float scale, float* out_w, float* out_h) {
    size_t line_len = 0;
    size_t max_line_len = 0;
    int lines = 1;
    const unsigned char* p;

    if (scale < 0.0f) scale = 0.0f;
    if (!text) text = "";
    scale = ui_readable_text_scale(scale);

    for (p = (const unsigned char*)text; *p; p++) {
        if (*p == '\r') continue;
        if (*p == '\n') {
            if (line_len > max_line_len) max_line_len = line_len;
            line_len = 0;
            lines++;
            continue;
        }
        line_len++;
    }
    if (line_len > max_line_len) max_line_len = line_len;

    if (out_w) *out_w = (float)max_line_len * 9.0f * scale;
    if (out_h) *out_h = (float)lines * 9.0f * scale;
}

static void ui_reset_render_state(void) {
    if (p_turtle_reset) {
        p_turtle_reset();
        return;
    }
    if (p_turtle_set_angle) p_turtle_set_angle(0.0);
    if (p_turtle_set_scale) p_turtle_set_scale(1.0, 1.0);
    if (p_turtle_set_rgba) p_turtle_set_rgba(1.0f, 1.0f, 1.0f, 1.0f);
    else if (p_turtle_set_rgb) p_turtle_set_rgb(1.0f, 1.0f, 1.0f);
}

static void ui_draw_default_custom_state_cursor(void) {
    int sprite_id;
    void* sprite_ptr;
    float scale;

    if (g_ui_default_custom_cursor_suppressed) return;
    if (!hooks_custom_state_active_name()) return;
    if (!p_misc_id || !p_sprite_get || !p_sprite_batch_plot || !p_turtle_set_pos ||
        !p_turtle_set_scale || !p_turtle_set_angle) {
        return;
    }
    if (!p_turtle_set_rgba && !p_turtle_set_rgb) return;

    sprite_id = *p_misc_id + 7;  // Vanilla menu cursor: top-right 16x16 cell in misc.png.
    if (sprite_id < 0) return;

    sprite_ptr = p_sprite_get((uint32_t)sprite_id);
    if (!sprite_ptr) return;

    scale = ui_readable_text_scale(1.0f);
    p_turtle_set_angle(0.0);
    p_turtle_set_scale((double)scale, (double)scale);
    if (p_turtle_set_rgba) p_turtle_set_rgba(1.0f, 1.0f, 1.0f, 1.0f);
    else p_turtle_set_rgb(1.0f, 1.0f, 1.0f);
    p_turtle_set_pos((double)((float)g_ui_mouse_x + 8.0f * scale),
                     (double)((float)g_ui_mouse_y + 8.0f * scale));
    p_sprite_batch_plot((int)(intptr_t)sprite_ptr, 0, 0);
}

static void ui_draw_text_mode_alpha(float x, float y, float scale, float r, float g, float b, float a, const char* text, int mode) {
    if (!text || !text[0] || !p_plot_text || !p_turtle_set_pos || !p_turtle_set_scale || !p_turtle_set_angle) return;
    if (!p_turtle_set_rgb && !p_turtle_set_rgba) return;
    scale = ui_readable_text_scale(scale);
    p_turtle_set_angle(0.0);
    p_turtle_set_scale((double)scale, (double)scale);
    if (p_turtle_set_rgba) p_turtle_set_rgba(r, g, b, a);
    else p_turtle_set_rgb(r, g, b);
    p_turtle_set_pos((double)x, (double)y);
    p_plot_text(text, mode);
}

static void ui_draw_text_mode(float x, float y, float scale, float r, float g, float b, const char* text, int mode) {
    ui_draw_text_mode_alpha(x, y, scale, r, g, b, 1.0f, text, mode);
}

static int reload_engine_gfx_atlases(const char* reason) {
    int loaded = 0;
    if (!p_atlas_exit || !p_sprites_reset || !p_load_gfx) return 0;
    if (IsBadCodePtr((FARPROC)(void*)p_atlas_exit)) return 0;
    if (IsBadCodePtr((FARPROC)(void*)p_sprites_reset)) return 0;
    if (IsBadCodePtr((FARPROC)(void*)p_load_gfx)) return 0;

    if (reason && reason[0]) LOG_INFO("Asset hot reload: rebuilding atlases (%s)", reason);
    else LOG_INFO("Asset hot reload: rebuilding atlases");

    p_atlas_exit();
    p_sprites_reset();
    loaded = p_load_gfx();
    ui_reset_render_state();

    if (loaded < 0) {
        LOG_WARN("Asset hot reload: load_gfx returned %d; restart may still be required", loaded);
        return 0;
    }

    LOG_INFO("Asset hot reload: atlas rebuild complete (load_gfx=%d)", loaded);
    return 1;
}

static int ui_sheet_base_from_name(const char* name, int* out_base) {
    int base = -1;
    if (!name || !name[0] || !out_base) return 0;
    if ((_stricmp(name, "sprites") == 0 || _stricmp(name, "sprite") == 0 ||
         _stricmp(name, "spritesheet") == 0 || _stricmp(name, "data/sprites.png") == 0 ||
         _stricmp(name, "data\\sprites.png") == 0 || _stricmp(name, "sprites.png") == 0) &&
        p_sprites_id) {
        base = *p_sprites_id;
    } else if ((_stricmp(name, "tiles") == 0 || _stricmp(name, "tile") == 0 ||
                _stricmp(name, "tilesheet") == 0 || _stricmp(name, "data/tiles.png") == 0 ||
                _stricmp(name, "data\\tiles.png") == 0 || _stricmp(name, "tiles.png") == 0) &&
               p_tiles_id) {
        base = *p_tiles_id;
    } else if ((_stricmp(name, "misc") == 0 || _stricmp(name, "miscsheet") == 0 ||
                _stricmp(name, "data/misc.png") == 0 || _stricmp(name, "data\\misc.png") == 0 ||
                _stricmp(name, "misc.png") == 0) &&
               p_misc_id) {
        base = *p_misc_id;
    } else if ((_stricmp(name, "glyphs") == 0 || _stricmp(name, "font") == 0 ||
                _stricmp(name, "data/font8x8.png") == 0 || _stricmp(name, "data\\font8x8.png") == 0 ||
                _stricmp(name, "font8x8.png") == 0) &&
               p_glyphs_id) {
        base = *p_glyphs_id;
    } else {
        return 0;
    }

    if (base < 0) return 0;
    *out_base = base;
    return 1;
}

static void ui_lua_read_tint(lua_State* Ls, int table_index,
                             float* out_r, float* out_g, float* out_b, float* out_a) {
    if (!lua_istable(Ls, table_index)) return;

    lua_getfield(Ls, table_index, "r");
    if (lua_isnumber(Ls, -1)) *out_r = (float)lua_tonumber(Ls, -1);
    lua_pop(Ls, 1);

    lua_getfield(Ls, table_index, "g");
    if (lua_isnumber(Ls, -1)) *out_g = (float)lua_tonumber(Ls, -1);
    lua_pop(Ls, 1);

    lua_getfield(Ls, table_index, "b");
    if (lua_isnumber(Ls, -1)) *out_b = (float)lua_tonumber(Ls, -1);
    lua_pop(Ls, 1);

    lua_getfield(Ls, table_index, "a");
    if (lua_isnumber(Ls, -1)) *out_a = (float)lua_tonumber(Ls, -1);
    lua_pop(Ls, 1);

    lua_rawgeti(Ls, table_index, 1);
    if (lua_isnumber(Ls, -1)) *out_r = (float)lua_tonumber(Ls, -1);
    lua_pop(Ls, 1);

    lua_rawgeti(Ls, table_index, 2);
    if (lua_isnumber(Ls, -1)) *out_g = (float)lua_tonumber(Ls, -1);
    lua_pop(Ls, 1);

    lua_rawgeti(Ls, table_index, 3);
    if (lua_isnumber(Ls, -1)) *out_b = (float)lua_tonumber(Ls, -1);
    lua_pop(Ls, 1);

    lua_rawgeti(Ls, table_index, 4);
    if (lua_isnumber(Ls, -1)) *out_a = (float)lua_tonumber(Ls, -1);
    lua_pop(Ls, 1);
}

static float ui_lua_read_number_field(lua_State* Ls, int table_index, const char* key, float fallback) {
    float value = fallback;
    if (!lua_istable(Ls, table_index) || !key) return fallback;
    lua_getfield(Ls, table_index, key);
    if (lua_isnumber(Ls, -1)) value = (float)lua_tonumber(Ls, -1);
    lua_pop(Ls, 1);
    return value;
}

static int ui_lua_read_color_field(lua_State* Ls, int table_index, const char* key,
                                   float* out_r, float* out_g, float* out_b, float* out_a) {
    int found = 0;
    if (!lua_istable(Ls, table_index) || !key) return 0;
    lua_getfield(Ls, table_index, key);
    if (lua_istable(Ls, -1)) {
        ui_lua_read_tint(Ls, lua_gettop(Ls), out_r, out_g, out_b, out_a);
        found = 1;
    }
    lua_pop(Ls, 1);
    return found;
}

static void ui_lua_read_color_opts(lua_State* Ls, int table_index, const char* preferred_key,
                                   float* out_r, float* out_g, float* out_b, float* out_a) {
    if (!lua_istable(Ls, table_index)) return;
    if (preferred_key && ui_lua_read_color_field(Ls, table_index, preferred_key, out_r, out_g, out_b, out_a)) {
        return;
    }
    if (ui_lua_read_color_field(Ls, table_index, "color", out_r, out_g, out_b, out_a)) return;
    if (ui_lua_read_color_field(Ls, table_index, "colour", out_r, out_g, out_b, out_a)) return;
    if (ui_lua_read_color_field(Ls, table_index, "tint", out_r, out_g, out_b, out_a)) return;
    if (ui_lua_read_color_field(Ls, table_index, "fg", out_r, out_g, out_b, out_a)) return;
    ui_lua_read_tint(Ls, table_index, out_r, out_g, out_b, out_a);
}

static float ui_lua_read_line_width_opts(lua_State* Ls, int table_index, float fallback) {
    float line_w = fallback;
    if (!lua_istable(Ls, table_index)) return fallback;
    line_w = ui_lua_read_number_field(Ls, table_index, "line_w", line_w);
    line_w = ui_lua_read_number_field(Ls, table_index, "line_width", line_w);
    line_w = ui_lua_read_number_field(Ls, table_index, "width", line_w);
    line_w = ui_lua_read_number_field(Ls, table_index, "thickness", line_w);
    if (line_w < 1.0f) line_w = 1.0f;
    return line_w;
}

static void ui_lua_apply_sprite_opts(lua_State* Ls,
                                     int table_index,
                                     int* inout_flip,
                                     int* inout_layer,
                                     float* inout_sx,
                                     float* inout_sy,
                                     float* inout_angle,
                                     float* inout_r,
                                     float* inout_g,
                                     float* inout_b,
                                     float* inout_a) {
    if (!lua_istable(Ls, table_index)) return;

    lua_getfield(Ls, table_index, "scale");
    if (lua_isnumber(Ls, -1)) {
        float s = (float)lua_tonumber(Ls, -1);
        *inout_sx = s;
        *inout_sy = s;
    }
    lua_pop(Ls, 1);

    lua_getfield(Ls, table_index, "scale_x");
    if (lua_isnumber(Ls, -1)) *inout_sx = (float)lua_tonumber(Ls, -1);
    lua_pop(Ls, 1);

    lua_getfield(Ls, table_index, "sx");
    if (lua_isnumber(Ls, -1)) *inout_sx = (float)lua_tonumber(Ls, -1);
    lua_pop(Ls, 1);

    lua_getfield(Ls, table_index, "scale_y");
    if (lua_isnumber(Ls, -1)) *inout_sy = (float)lua_tonumber(Ls, -1);
    lua_pop(Ls, 1);

    lua_getfield(Ls, table_index, "sy");
    if (lua_isnumber(Ls, -1)) *inout_sy = (float)lua_tonumber(Ls, -1);
    lua_pop(Ls, 1);

    lua_getfield(Ls, table_index, "angle");
    if (lua_isnumber(Ls, -1)) *inout_angle = (float)lua_tonumber(Ls, -1);
    lua_pop(Ls, 1);

    lua_getfield(Ls, table_index, "flip");
    if (lua_isboolean(Ls, -1)) {
        *inout_flip = lua_toboolean(Ls, -1) ? 1 : 0;
    } else if (lua_isnumber(Ls, -1)) {
        *inout_flip = ((int)lua_tointeger(Ls, -1) != 0) ? 1 : 0;
    }
    lua_pop(Ls, 1);

    lua_getfield(Ls, table_index, "flip_x");
    if (lua_isboolean(Ls, -1)) {
        *inout_flip = lua_toboolean(Ls, -1) ? 1 : 0;
    } else if (lua_isnumber(Ls, -1)) {
        *inout_flip = ((int)lua_tointeger(Ls, -1) != 0) ? 1 : 0;
    }
    lua_pop(Ls, 1);

    lua_getfield(Ls, table_index, "layer");
    if (lua_isnumber(Ls, -1)) *inout_layer = (int)lua_tointeger(Ls, -1);
    lua_pop(Ls, 1);

    lua_getfield(Ls, table_index, "r");
    if (lua_isnumber(Ls, -1)) *inout_r = (float)lua_tonumber(Ls, -1);
    lua_pop(Ls, 1);

    lua_getfield(Ls, table_index, "g");
    if (lua_isnumber(Ls, -1)) *inout_g = (float)lua_tonumber(Ls, -1);
    lua_pop(Ls, 1);

    lua_getfield(Ls, table_index, "b");
    if (lua_isnumber(Ls, -1)) *inout_b = (float)lua_tonumber(Ls, -1);
    lua_pop(Ls, 1);

    lua_getfield(Ls, table_index, "a");
    if (lua_isnumber(Ls, -1)) *inout_a = (float)lua_tonumber(Ls, -1);
    lua_pop(Ls, 1);

    lua_getfield(Ls, table_index, "tint");
    if (lua_istable(Ls, -1)) {
        ui_lua_read_tint(Ls, lua_gettop(Ls), inout_r, inout_g, inout_b, inout_a);
    }
    lua_pop(Ls, 1);
}

static void orphan_ui_string_take(char* s) {
    if (!s) return;
    if (g_orphan_ui_string_count + 1 > g_orphan_ui_string_cap) {
        int newcap = (g_orphan_ui_string_cap == 0) ? 64 : (g_orphan_ui_string_cap * 2);
        char** np = (char**)realloc(g_orphan_ui_strings, sizeof(char*) * newcap);
        if (!np) {
            // Keep the string alive (leak-safe fallback) so engine button
            // pointers never dangle during hot reload.
            return;
        }
        g_orphan_ui_strings = np;
        g_orphan_ui_string_cap = newcap;
    }
    g_orphan_ui_strings[g_orphan_ui_string_count++] = s;
}

static void orphan_ui_strings_free_all(void) {
    if (!g_orphan_ui_strings) return;
    for (int i = 0; i < g_orphan_ui_string_count; i++) {
        if (g_orphan_ui_strings[i]) free(g_orphan_ui_strings[i]);
    }
    free(g_orphan_ui_strings);
    g_orphan_ui_strings = NULL;
    g_orphan_ui_string_count = 0;
    g_orphan_ui_string_cap = 0;
}

static void ui_detach_button_for_unload(void* btn_ptr) {
    if (!btn_ptr) return;
    if (!ui_engine_button_exists(btn_ptr)) return;

    ui_button_apply_flags_hidden(btn_ptr, 1);
    if (p_button_set_w_ex) p_button_set_w_ex((int)(intptr_t)btn_ptr, 1.0f, 0.0f);
    if (p_button_set_h_ex) p_button_set_h_ex((int)(intptr_t)btn_ptr, 1.0f, 0.0f);
    *(float*)((uint8_t*)btn_ptr + BTN_OFS_CENTER_X) = -10000.0f;
    *(float*)((uint8_t*)btn_ptr + BTN_OFS_CENTER_Y) = -10000.0f;

    if (!IsBadWritePtr((uint8_t*)btn_ptr + BTN_OFS_LABEL_PTR, (SIZE_T)sizeof(void*))) {
        *(const char**)((uint8_t*)btn_ptr + BTN_OFS_LABEL_PTR) = g_orphan_empty_label;
    }
}

static void mod_ui_reset_frame(LoadedMod* mod, void* state_ptr) {
    if (!mod) return;
    mod->ui_hitbox_count = 0;
    mod->ui_state_ptr = state_ptr;
}

static void mod_ui_free(LoadedMod* mod) {
    if (!mod) return;

    if (mod->ui_hitboxes) {
        free(mod->ui_hitboxes);
        mod->ui_hitboxes = NULL;
    }
    mod->ui_hitbox_count = 0;
    mod->ui_hitbox_cap = 0;
    mod->ui_state_ptr = NULL;
    memset(&mod->ui_layout, 0, sizeof(mod->ui_layout));

    if (mod->ui_native_buttons) {
        for (int i = 0; i < mod->ui_native_count; i++) {
            UiNativeButton* b = mod->ui_native_buttons[i];
            if (!b) continue;
            if (b->btn_ptr) {
                ui_detach_button_for_unload(b->btn_ptr);
                b->btn_ptr = NULL;
            }
            free(b);
        }
        free(mod->ui_native_buttons);
        mod->ui_native_buttons = NULL;
    }
    mod->ui_native_count = 0;
    mod->ui_native_cap = 0;

    if (mod->ui_string_pool) {
        for (int i = 0; i < mod->ui_string_count; i++) {
            char* s = mod->ui_string_pool[i];
            if (!s) continue;
            if (g_unloading_for_shutdown) {
                free(s);
            } else {
                orphan_ui_string_take(s);
            }
        }
        free(mod->ui_string_pool);
        mod->ui_string_pool = NULL;
    }
    mod->ui_string_count = 0;
    mod->ui_string_cap = 0;
}

static void mod_ui_push_hitbox(LoadedMod* mod, float x, float y, float w, float h) {
    if (!mod) return;
    if (w <= 0.0f || h <= 0.0f) return;
    if (mod->ui_hitbox_count + 1 > mod->ui_hitbox_cap) {
        int newcap = (mod->ui_hitbox_cap == 0) ? 8 : (mod->ui_hitbox_cap * 2);
        UiHitBox* nb = (UiHitBox*)realloc(mod->ui_hitboxes, sizeof(UiHitBox) * newcap);
        if (!nb) return;
        mod->ui_hitboxes = nb;
        mod->ui_hitbox_cap = newcap;
    }
    mod->ui_hitboxes[mod->ui_hitbox_count].x = x;
    mod->ui_hitboxes[mod->ui_hitbox_count].y = y;
    mod->ui_hitboxes[mod->ui_hitbox_count].w = w;
    mod->ui_hitboxes[mod->ui_hitbox_count].h = h;
    mod->ui_hitbox_count++;
}

static int ui_point_in_hitbox(int x, int y, const UiHitBox* hb) {
    float fx = (float)x;
    float fy = (float)y;
    if (!hb) return 0;
    if (fx < hb->x) return 0;
    if (fy < hb->y) return 0;
    if (fx > hb->x + hb->w) return 0;
    if (fy > hb->y + hb->h) return 0;
    return 1;
}

static int ui_hit_any_visible_button(int x, int y) {
    void* state_ptr = ui_current_state_ptr();
    for (int mi = 0; mi < g_mod_count; mi++) {
        LoadedMod* mod = &g_mods[mi];
        if (!mod->enabled) continue;
        if (mod->ui_hitbox_count <= 0) continue;
        if (mod->ui_state_ptr != state_ptr) continue;
        for (int i = 0; i < mod->ui_hitbox_count; i++) {
            if (ui_point_in_hitbox(x, y, &mod->ui_hitboxes[i])) return 1;
        }
    }
    return 0;
}

static int ui_engine_button_exists(void* btn_ptr) {
    if (!btn_ptr || !p_button_count || !p_button_get) return 0;
    int count = p_button_count();
    if (count <= 0 || count > 3000) return 0;
    for (int i = 0; i < count; i++) {
        if (p_button_get(i) == btn_ptr) return 1;
    }
    return 0;
}

static void ui_button_apply_flags_hidden(void* btn_ptr, int hidden) {
    if (!btn_ptr) return;
    uint32_t* flags = (uint32_t*)((uint8_t*)btn_ptr + BTN_OFS_FLAGS);
    if (hidden) {
        *flags |= (BTN_FLAG_NOCLICK | BTN_FLAG_NOCLICKTHRU | BTN_FLAG_NOEMPTYCLICK);
    } else {
        *flags &= ~(BTN_FLAG_NOCLICK | BTN_FLAG_NOCLICKTHRU | BTN_FLAG_NOEMPTYCLICK);
    }
}

static int ui_safe_string_readable(const char* s, int maxlen) {
    if (!s || maxlen <= 0) return 0;
    if (IsBadReadPtr(s, (SIZE_T)maxlen)) return 0;
    for (int i = 0; i < maxlen; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\0') return 1;
        if (c < 0x09) return 0;
    }
    return 0;
}

// Gameplay telemetry offsets (player + thing structs).
#define PLAYER_SIZE                 0x15C
#define PLAYER_OFS_THING_SLOT       0x00
#define PLAYER_OFS_SPRITE_INDEX     0x04
#define PLAYER_OFS_HAS_SWORD        0x11
#define PLAYER_OFS_X                0x24
#define PLAYER_OFS_Y                0x28
#define PLAYER_OFS_PREV_X           0x2C
#define PLAYER_OFS_PREV_Y           0x30
#define PLAYER_OFS_VX               0x34
#define PLAYER_OFS_VY               0x38
#define PLAYER_OFS_ACTION_BLOB      0x54
#define PLAYER_ACTION_BLOB_LEN      0x24
#define PLAYER_OFS_EVENT_FLAGS      0x70
#define PLAYER_OFS_PREV_EVENT_FLAGS 0x74
#define PLAYER_OFS_PENDING_EVENT_FLAGS 0x84
#define PLAYER_OFS_STATE_ID         0x78
#define PLAYER_OFS_STATE_TIMER      0x8C
#define PLAYER_OFS_FACING_SIGN      0x98
#define PLAYER_OFS_PREV_CMD_BITS    0x9E
#define PLAYER_OFS_CMD_BITS         0x9F
#define PLAYER_OFS_JUMP_BUFFER      0xA0
#define PLAYER_OFS_ATTACK_BUFFER    0xA1
#define PLAYER_OFS_PREV_COLLISION   0xAC
#define PLAYER_OFS_COLLISION_FLAGS  0xAD
#define PLAYER_OFS_ROOM             0x9B
#define PLAYER_OFS_ANIM_PHASE       0x94
#define PLAYER_OFS_RENDER_RGBA      0xD8
#define PLAYER_RENDER_RGBA_LEN      0x20
#define PLAYER_OFS_STATE_BLOB       0x78
#define PLAYER_STATE_BLOB_LEN       0x80
#define PLAYER_OFS_ANIM_PTR         0x158

#define PLAYER_COLLIDE_GROUNDED     0x01
#define PLAYER_COLLIDE_CEILING      0x02
#define PLAYER_COLLIDE_WALL_RIGHT   0x04
#define PLAYER_COLLIDE_WALL_LEFT    0x08

#define THING_SIZE                  0x15C
#define THING_OFS_ACTIVE            0x00
#define THING_OFS_TYPE              0x01
#define THING_OFS_X                 0x24
#define THING_OFS_Y                 0x28
#define THING_OFS_PREV_X            0x2C
#define THING_OFS_PREV_Y            0x30
#define THING_OFS_VX                0x34
#define THING_OFS_VY                0x38
#define THING_OFS_HEAD_BLOB         0x02
#define THING_HEAD_BLOB_LEN         0x22
#define THING_OFS_MOTION_BLOB       0x3C
#define THING_MOTION_BLOB_LEN       0x18
#define THING_OFS_ACTION_BLOB       0x54
#define THING_ACTION_BLOB_LEN       0x24
#define THING_OFS_STATE_ID          0x78
#define THING_OFS_STATE_BLOB        0x78
#define THING_STATE_BLOB_LEN        0x48
#define THING_OFS_FLAGS             0x80
#define THING_OFS_ROOM              0xC0
#define THING_OFS_TAIL_BLOB         0xC4
#define THING_TAIL_BLOB_LEN         0x08
#define THING_TYPE_PLAYER           0x01
#define THING_TYPE_SWORD            0x02

#define THING_INFO_STATE_SIZE       0xC0u
#define ROOM_INFO_STATE_SIZE        0x4444u
#define PARTICLE_STATE_SIZE         0x19900u
#define TILEMAP_MAX_BYTES           (4u * 1024u * 1024u)

#define FULL_STATE_BLOB_MAGIC       0x30474745u /* "EGG0" */
#define FULL_STATE_BLOB_VERSION     4u

typedef struct FullStateBlobHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t thing_count;
    uint32_t player_size;
    uint32_t thing_size;
    int32_t active_room;
    int32_t start_countdown;
    int32_t end_countdown;
    uint32_t game_level;
    uint32_t rng_seed;
    uint32_t native_game_ticks;
    uint32_t leader_mode;
    uint32_t crowd_sound_last_tick;
    uint32_t waterfall_fx_present;
    uint64_t framework_tick_count;
    uintptr_t leader_raw;
    uintptr_t waterfall_fx_raw;
    int32_t map_selector;
    int32_t map_mode;
    int32_t round_end_any;
    int32_t score_target;
    int32_t armed_respawn_limit;
    int32_t score_p0;
    int32_t score_p1;
    int32_t chant_step;
    int32_t chant_timer;
    int32_t crowd_timer;
    int32_t native_thing_count;
    int32_t things_allocated;
    int32_t thing_latest;
    int32_t game_do_lerp_colours;
    int32_t waterfall_count;
    int32_t game_started;
    int32_t lerp_time;
    int32_t game_old_active_room;
    int32_t resumed;
    int32_t freeze;
    int32_t player_mode0;
    int32_t player_mode1;
    uint32_t seed;
    uint32_t loser_mode;
    uintptr_t loser_raw;
    int32_t score_shudder0;
    int32_t score_shudder1;
    int32_t roomdef_count;
    int32_t room_w;
    int32_t room_pixel_w;
    int32_t map_w;
    int32_t map_h;
    int32_t tilemap_w;
    int32_t tilemap_h;
    int32_t tile_w;
    int32_t tile_h;
    int32_t tilemap_pixels_w;
    int32_t tilemap_pixels_h;
    uint32_t tilemap_bytes;
    float camera_x;
    float camera_y;
    float camera_shake;
    float camera_shake_decay;
    float game_w;
    float game_h;
    uint8_t transient_game_state[TRANSIENT_GAME_STATE_SIZE];
    uint8_t thing_info_state[THING_INFO_STATE_SIZE];
    uint8_t room_info_state[ROOM_INFO_STATE_SIZE];
    uint8_t particle_state[PARTICLE_STATE_SIZE];
} FullStateBlobHeader;

enum {
    FULL_STATE_LEADER_NONE = 0,
    FULL_STATE_LEADER_P0   = 1,
    FULL_STATE_LEADER_P1   = 2,
    FULL_STATE_LEADER_RAW  = 3
};

const char* lua_manager_game_state_offset_name(size_t offset) {
#define FULL_STATE_FIELD_RANGE(field) \
    if (offset >= offsetof(FullStateBlobHeader, field) && offset < offsetof(FullStateBlobHeader, field) + sizeof(((FullStateBlobHeader*)0)->field)) return #field

    FULL_STATE_FIELD_RANGE(magic);
    FULL_STATE_FIELD_RANGE(version);
    FULL_STATE_FIELD_RANGE(thing_count);
    FULL_STATE_FIELD_RANGE(player_size);
    FULL_STATE_FIELD_RANGE(thing_size);
    FULL_STATE_FIELD_RANGE(active_room);
    FULL_STATE_FIELD_RANGE(start_countdown);
    FULL_STATE_FIELD_RANGE(end_countdown);
    FULL_STATE_FIELD_RANGE(game_level);
    FULL_STATE_FIELD_RANGE(rng_seed);
    FULL_STATE_FIELD_RANGE(native_game_ticks);
    FULL_STATE_FIELD_RANGE(leader_mode);
    FULL_STATE_FIELD_RANGE(crowd_sound_last_tick);
    FULL_STATE_FIELD_RANGE(waterfall_fx_present);
    FULL_STATE_FIELD_RANGE(framework_tick_count);
    FULL_STATE_FIELD_RANGE(leader_raw);
    FULL_STATE_FIELD_RANGE(waterfall_fx_raw);
    FULL_STATE_FIELD_RANGE(map_selector);
    FULL_STATE_FIELD_RANGE(map_mode);
    FULL_STATE_FIELD_RANGE(round_end_any);
    FULL_STATE_FIELD_RANGE(score_target);
    FULL_STATE_FIELD_RANGE(armed_respawn_limit);
    FULL_STATE_FIELD_RANGE(score_p0);
    FULL_STATE_FIELD_RANGE(score_p1);
    FULL_STATE_FIELD_RANGE(chant_step);
    FULL_STATE_FIELD_RANGE(chant_timer);
    FULL_STATE_FIELD_RANGE(crowd_timer);
    FULL_STATE_FIELD_RANGE(native_thing_count);
    FULL_STATE_FIELD_RANGE(things_allocated);
    FULL_STATE_FIELD_RANGE(thing_latest);
    FULL_STATE_FIELD_RANGE(game_do_lerp_colours);
    FULL_STATE_FIELD_RANGE(waterfall_count);
    FULL_STATE_FIELD_RANGE(game_started);
    FULL_STATE_FIELD_RANGE(lerp_time);
    FULL_STATE_FIELD_RANGE(game_old_active_room);
    FULL_STATE_FIELD_RANGE(resumed);
    FULL_STATE_FIELD_RANGE(freeze);
    FULL_STATE_FIELD_RANGE(player_mode0);
    FULL_STATE_FIELD_RANGE(player_mode1);
    FULL_STATE_FIELD_RANGE(seed);
    FULL_STATE_FIELD_RANGE(loser_mode);
    FULL_STATE_FIELD_RANGE(loser_raw);
    FULL_STATE_FIELD_RANGE(score_shudder0);
    FULL_STATE_FIELD_RANGE(score_shudder1);
    FULL_STATE_FIELD_RANGE(roomdef_count);
    FULL_STATE_FIELD_RANGE(room_w);
    FULL_STATE_FIELD_RANGE(room_pixel_w);
    FULL_STATE_FIELD_RANGE(map_w);
    FULL_STATE_FIELD_RANGE(map_h);
    FULL_STATE_FIELD_RANGE(tilemap_w);
    FULL_STATE_FIELD_RANGE(tilemap_h);
    FULL_STATE_FIELD_RANGE(tile_w);
    FULL_STATE_FIELD_RANGE(tile_h);
    FULL_STATE_FIELD_RANGE(tilemap_pixels_w);
    FULL_STATE_FIELD_RANGE(tilemap_pixels_h);
    FULL_STATE_FIELD_RANGE(tilemap_bytes);
    FULL_STATE_FIELD_RANGE(camera_x);
    FULL_STATE_FIELD_RANGE(camera_y);
    FULL_STATE_FIELD_RANGE(camera_shake);
    FULL_STATE_FIELD_RANGE(camera_shake_decay);
    FULL_STATE_FIELD_RANGE(game_w);
    FULL_STATE_FIELD_RANGE(game_h);
    FULL_STATE_FIELD_RANGE(transient_game_state);
    FULL_STATE_FIELD_RANGE(thing_info_state);
    FULL_STATE_FIELD_RANGE(room_info_state);
    FULL_STATE_FIELD_RANGE(particle_state);

#undef FULL_STATE_FIELD_RANGE

    if (offset < sizeof(FullStateBlobHeader)) return "state_header_padding";
    offset -= sizeof(FullStateBlobHeader);
    if (offset < PLAYER_SIZE) return "player0";
    offset -= PLAYER_SIZE;
    if (offset < PLAYER_SIZE) return "player1";
    return "things";
}

static int ptr_readable(const void* p, SIZE_T len) {
    return (p && len > 0 && !IsBadReadPtr(p, len)) ? 1 : 0;
}

static int ptr_writable(void* p, SIZE_T len) {
    return (p && len > 0 && !IsBadWritePtr(p, len)) ? 1 : 0;
}

static uintptr_t game_get_player_ptr(int player_index) {
    if (player_index < 0 || player_index > 1) return 0;
    if (!ptr_readable(p_player_slots + player_index, sizeof(uintptr_t))) return 0;
    uintptr_t p = p_player_slots[player_index];
    if (!p) return 0;
    if (!ptr_readable((const void*)p, PLAYER_SIZE)) return 0;
    return p;
}

static int game_get_thing_count(void) {
    if (!p_things || !p_thing_info) return 0;
    if (p_thing_info <= p_things) return 0;
    intptr_t bytes = (intptr_t)(p_thing_info - p_things);
    if (bytes <= 0) return 0;
    int n = (int)(bytes / THING_SIZE);
    if (n < 0) n = 0;
    if (n > 128) n = 128;
    return n;
}

static void* game_get_tilemap_data_ptr(void) {
    uintptr_t ptr = 0;
    if (!ptr_readable((const void*)p_tilemap_data_ptr, sizeof(uintptr_t))) return NULL;
    ptr = *p_tilemap_data_ptr;
    return ptr ? (void*)ptr : NULL;
}

static size_t game_get_tilemap_bytes(int* out_w, int* out_h) {
    int w = 0;
    int h = 0;
    size_t cells = 0;
    size_t bytes = 0;

    if (ptr_readable((const void*)p_tilemap_w, sizeof(int))) {
        w = *p_tilemap_w;
    }
    if (ptr_readable((const void*)p_tilemap_h, sizeof(int))) {
        h = *p_tilemap_h;
    }
    if (w < 0 || h < 0 || w > 4096 || h > 4096) {
        w = 0;
        h = 0;
    }

    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    if (w <= 0 || h <= 0 || !game_get_tilemap_data_ptr()) {
        return 0;
    }

    cells = (size_t)w * (size_t)h;
    if (cells > ((size_t)TILEMAP_MAX_BYTES / sizeof(uint32_t))) {
        return 0;
    }
    bytes = cells * sizeof(uint32_t);
    return bytes;
}

static size_t full_state_blob_size_for_counts(int thing_count, size_t tilemap_bytes) {
    if (thing_count < 0) return 0;
    if (tilemap_bytes > (size_t)TILEMAP_MAX_BYTES) return 0;
    return sizeof(FullStateBlobHeader)
        + ((size_t)PLAYER_SIZE * 2u)
        + ((size_t)THING_SIZE * (size_t)thing_count)
        + tilemap_bytes;
}

static size_t full_state_blob_size_for_thing_count(int thing_count) {
    return full_state_blob_size_for_counts(thing_count, game_get_tilemap_bytes(NULL, NULL));
}

static int full_state_transient_range(uintptr_t addr, size_t len, size_t* out_off) {
    uintptr_t start = (uintptr_t)ADDR_TRANSIENT_GAME_STATE;
    uintptr_t end = start + (uintptr_t)TRANSIENT_GAME_STATE_SIZE;
    if (addr < start || len > (size_t)(end - addr)) return 0;
    if (out_off) *out_off = (size_t)(addr - start);
    return 1;
}

static uintptr_t full_state_read_transient_ptr(const FullStateBlobHeader* hdr, uintptr_t addr) {
    size_t off = 0;
    uintptr_t value = 0;
    if (!hdr || !full_state_transient_range(addr, sizeof(value), &off)) return 0;
    memcpy(&value, hdr->transient_game_state + off, sizeof(value));
    return value;
}

static void full_state_zero_transient_range(FullStateBlobHeader* hdr, uintptr_t addr, size_t len) {
    size_t off = 0;
    if (!hdr || !full_state_transient_range(addr, len, &off)) return;
    memset(hdr->transient_game_state + off, 0, len);
}

static uintptr_t full_state_remap_player_ptr(const FullStateBlobHeader* hdr, uintptr_t raw, uintptr_t p0, uintptr_t p1) {
    uintptr_t raw_p0 = full_state_read_transient_ptr(hdr, ADDR_PLAYER_ARRAY);
    uintptr_t raw_p1 = full_state_read_transient_ptr(hdr, ADDR_PLAYER_ARRAY + sizeof(uintptr_t));
    if (raw == 0) return 0;
    if (raw == raw_p0) return p0;
    if (raw == raw_p1) return p1;
    return 0;
}

static void full_state_set_err(char* err, size_t err_cap, const char* msg) {
    if (!err || err_cap == 0) return;
    if (!msg) msg = "unknown error";
    snprintf(err, err_cap, "%s", msg);
}

static int full_state_capture_into(void* dst, size_t dst_len, size_t* out_len, char* err, size_t err_cap) {
    int thing_count = game_get_thing_count();
    int tilemap_w = 0;
    int tilemap_h = 0;
    size_t tilemap_bytes = game_get_tilemap_bytes(&tilemap_w, &tilemap_h);
    void* tilemap_ptr = game_get_tilemap_data_ptr();
    uintptr_t p0 = game_get_player_ptr(0);
    uintptr_t p1 = game_get_player_ptr(1);
    size_t total_size = full_state_blob_size_for_counts(thing_count, tilemap_bytes);
    uint8_t* out = (uint8_t*)dst;
    FullStateBlobHeader* hdr = NULL;
    uint8_t* payload = NULL;
    uintptr_t leader_raw = 0;
    uintptr_t loser_raw = 0;

    if (!dst || dst_len == 0) {
        full_state_set_err(err, err_cap, "destination buffer unavailable");
        return 0;
    }
    if (!p0 || !p1) {
        full_state_set_err(err, err_cap, "player state unavailable");
        return 0;
    }
    if (thing_count < 0) {
        full_state_set_err(err, err_cap, "thing count unavailable");
        return 0;
    }
    if (thing_count > 0 && (!p_things || !ptr_readable((const void*)p_things, (SIZE_T)((size_t)thing_count * THING_SIZE)))) {
        full_state_set_err(err, err_cap, "thing state unavailable");
        return 0;
    }
    if (!p_thing_info || !ptr_readable((const void*)p_thing_info, THING_INFO_STATE_SIZE)) {
        full_state_set_err(err, err_cap, "thing metadata state unavailable");
        return 0;
    }
    if (!p_room_info_state || !ptr_readable((const void*)p_room_info_state, ROOM_INFO_STATE_SIZE)) {
        full_state_set_err(err, err_cap, "room info state unavailable");
        return 0;
    }
    if (!p_particle_state || !ptr_readable((const void*)p_particle_state, PARTICLE_STATE_SIZE)) {
        full_state_set_err(err, err_cap, "particle state unavailable");
        return 0;
    }
    if (tilemap_bytes > 0 && (!tilemap_ptr || !ptr_readable((const void*)tilemap_ptr, (SIZE_T)tilemap_bytes))) {
        full_state_set_err(err, err_cap, "tilemap state unavailable");
        return 0;
    }
    if (total_size == 0) {
        full_state_set_err(err, err_cap, "state blob size unavailable");
        return 0;
    }
    if (dst_len < total_size) {
        full_state_set_err(err, err_cap, "destination buffer too small");
        return 0;
    }

    hdr = (FullStateBlobHeader*)out;
    memset(hdr, 0, sizeof(*hdr));
    hdr->magic = FULL_STATE_BLOB_MAGIC;
    hdr->version = FULL_STATE_BLOB_VERSION;
    hdr->thing_count = (uint32_t)thing_count;
    hdr->player_size = (uint32_t)PLAYER_SIZE;
    hdr->thing_size = (uint32_t)THING_SIZE;
    hdr->tilemap_w = tilemap_w;
    hdr->tilemap_h = tilemap_h;
    hdr->tilemap_bytes = (uint32_t)tilemap_bytes;
    hdr->framework_tick_count = (uint64_t)g_game_tick_count;

    if (ptr_readable((const void*)p_map_selector, sizeof(int))) {
        hdr->map_selector = *p_map_selector;
    }
    if (ptr_readable((const void*)p_map_mode, sizeof(int))) {
        hdr->map_mode = *p_map_mode;
    }
    if (ptr_readable((const void*)p_round_end_any, sizeof(int))) {
        hdr->round_end_any = *p_round_end_any;
    }
    if (ptr_readable((const void*)p_score_target, sizeof(int))) {
        hdr->score_target = *p_score_target;
    }
    if (ptr_readable((const void*)p_armed_respawn_limit, sizeof(int))) {
        hdr->armed_respawn_limit = *p_armed_respawn_limit;
    }
    if (ptr_readable((const void*)p_score_p0, sizeof(int))) {
        hdr->score_p0 = *p_score_p0;
    }
    if (ptr_readable((const void*)p_score_p1, sizeof(int))) {
        hdr->score_p1 = *p_score_p1;
    }
    if (ptr_readable((const void*)p_game_active_room, sizeof(int))) {
        hdr->active_room = *p_game_active_room;
    }
    if (ptr_readable((const void*)p_chant_step, sizeof(int))) {
        hdr->chant_step = *p_chant_step;
    }
    if (ptr_readable((const void*)p_chant_timer, sizeof(int))) {
        hdr->chant_timer = *p_chant_timer;
    }
    if (ptr_readable((const void*)p_crowd_timer, sizeof(int))) {
        hdr->crowd_timer = *p_crowd_timer;
    }
    if (ptr_readable((const void*)p_native_thing_count, sizeof(int))) {
        hdr->native_thing_count = *p_native_thing_count;
    }
    if (ptr_readable((const void*)p_things_allocated, sizeof(int))) {
        hdr->things_allocated = *p_things_allocated;
    }
    if (ptr_readable((const void*)p_thing_latest, sizeof(int))) {
        hdr->thing_latest = *p_thing_latest;
    }
    if (ptr_readable((const void*)p_game_do_lerp_colours, sizeof(int))) {
        hdr->game_do_lerp_colours = *p_game_do_lerp_colours;
    }
    if (ptr_readable((const void*)p_waterfall_count, sizeof(int))) {
        hdr->waterfall_count = *p_waterfall_count;
    }
    if (ptr_readable((const void*)p_game_started, sizeof(int))) {
        hdr->game_started = *p_game_started;
    }
    if (ptr_readable((const void*)p_lerp_time, sizeof(int))) {
        hdr->lerp_time = *p_lerp_time;
    }
    if (ptr_readable((const void*)p_start_countdown, sizeof(int))) {
        hdr->start_countdown = *p_start_countdown;
    }
    if (ptr_readable((const void*)p_end_countdown, sizeof(int))) {
        hdr->end_countdown = *p_end_countdown;
    }
    if (ptr_readable((const void*)p_game_level, sizeof(uint32_t))) {
        hdr->game_level = *p_game_level;
    }
    if (ptr_readable((const void*)p_game_old_active_room, sizeof(int))) {
        hdr->game_old_active_room = *p_game_old_active_room;
    }
    if (ptr_readable((const void*)p_resumed, sizeof(int))) {
        hdr->resumed = *p_resumed;
    }
    if (ptr_readable((const void*)p_freeze, sizeof(int))) {
        hdr->freeze = *p_freeze;
    }
    if (ptr_readable((const void*)p_player_mode0, sizeof(int))) {
        hdr->player_mode0 = *p_player_mode0;
    }
    if (ptr_readable((const void*)p_player_mode1, sizeof(int))) {
        hdr->player_mode1 = *p_player_mode1;
    }
    if (ptr_readable((const void*)p_seed, sizeof(uint32_t))) {
        hdr->seed = *p_seed;
    }
    if (ptr_readable((const void*)p_mrand_seed, sizeof(uint32_t))) {
        hdr->rng_seed = *p_mrand_seed;
    }
    if (ptr_readable((const void*)p_native_game_ticks, sizeof(uint32_t))) {
        hdr->native_game_ticks = *p_native_game_ticks;
    }
    if (ptr_readable((const void*)p_crowd_sound_last_tick, sizeof(uint32_t))) {
        hdr->crowd_sound_last_tick = *p_crowd_sound_last_tick;
    }
    if (ptr_readable((const void*)p_waterfall_fx, sizeof(uintptr_t))) {
        hdr->waterfall_fx_raw = *p_waterfall_fx;
        hdr->waterfall_fx_present = (hdr->waterfall_fx_raw != 0u) ? 1u : 0u;
    }
    if (ptr_readable((const void*)p_game_leader, sizeof(uintptr_t))) {
        leader_raw = *p_game_leader;
        hdr->leader_raw = leader_raw;
        if (leader_raw == 0) hdr->leader_mode = FULL_STATE_LEADER_NONE;
        else if (leader_raw == p0) hdr->leader_mode = FULL_STATE_LEADER_P0;
        else if (leader_raw == p1) hdr->leader_mode = FULL_STATE_LEADER_P1;
        else hdr->leader_mode = FULL_STATE_LEADER_RAW;
    }
    if (ptr_readable((const void*)p_loser, sizeof(uintptr_t))) {
        loser_raw = *p_loser;
        hdr->loser_raw = loser_raw;
        if (loser_raw == 0) hdr->loser_mode = FULL_STATE_LEADER_NONE;
        else if (loser_raw == p0) hdr->loser_mode = FULL_STATE_LEADER_P0;
        else if (loser_raw == p1) hdr->loser_mode = FULL_STATE_LEADER_P1;
        else hdr->loser_mode = FULL_STATE_LEADER_RAW;
    }
    if (ptr_readable((const void*)p_score_shudder, sizeof(int) * 2u)) {
        hdr->score_shudder0 = p_score_shudder[0];
        hdr->score_shudder1 = p_score_shudder[1];
    }
    if (ptr_readable((const void*)p_roomdef_count, sizeof(int))) {
        hdr->roomdef_count = *p_roomdef_count;
    }
    if (ptr_readable((const void*)p_room_w, sizeof(int))) {
        hdr->room_w = *p_room_w;
    }
    if (ptr_readable((const void*)p_room_pixel_w, sizeof(int))) {
        hdr->room_pixel_w = *p_room_pixel_w;
    }
    if (ptr_readable((const void*)p_tile_w_native, sizeof(int))) {
        hdr->tile_w = *p_tile_w_native;
    }
    if (ptr_readable((const void*)p_tile_h_native, sizeof(int))) {
        hdr->tile_h = *p_tile_h_native;
    }
    if (ptr_readable((const void*)p_tilemap_pixels_w, sizeof(int))) {
        hdr->tilemap_pixels_w = *p_tilemap_pixels_w;
    }
    if (ptr_readable((const void*)p_tilemap_pixels_h, sizeof(int))) {
        hdr->tilemap_pixels_h = *p_tilemap_pixels_h;
    }
    if (ptr_readable((const void*)p_map_w, sizeof(int))) {
        hdr->map_w = *p_map_w;
    }
    if (ptr_readable((const void*)p_map_h, sizeof(int))) {
        hdr->map_h = *p_map_h;
    }
    if (ptr_readable((const void*)p_camera_x, sizeof(float))) {
        hdr->camera_x = *p_camera_x;
    }
    if (ptr_readable((const void*)p_camera_y, sizeof(float))) {
        hdr->camera_y = *p_camera_y;
    }
    if (ptr_readable((const void*)p_camera_shake, sizeof(float))) {
        hdr->camera_shake = *p_camera_shake;
    }
    if (ptr_readable((const void*)p_camera_shake_decay, sizeof(float))) {
        hdr->camera_shake_decay = *p_camera_shake_decay;
    }
    if (ptr_readable((const void*)p_game_w, sizeof(float))) {
        hdr->game_w = *p_game_w;
    }
    if (ptr_readable((const void*)p_game_h, sizeof(float))) {
        hdr->game_h = *p_game_h;
    }
    if (ptr_readable((const void*)p_transient_game_state, TRANSIENT_GAME_STATE_SIZE)) {
        memcpy(hdr->transient_game_state, (const void*)p_transient_game_state, TRANSIENT_GAME_STATE_SIZE);
    }
    memcpy(hdr->thing_info_state, (const void*)p_thing_info, THING_INFO_STATE_SIZE);
    memcpy(hdr->room_info_state, (const void*)p_room_info_state, ROOM_INFO_STATE_SIZE);
    memcpy(hdr->particle_state, (const void*)p_particle_state, PARTICLE_STATE_SIZE);

    payload = out + sizeof(*hdr);
    memcpy(payload, (const void*)p0, PLAYER_SIZE);
    payload += PLAYER_SIZE;
    memcpy(payload, (const void*)p1, PLAYER_SIZE);
    payload += PLAYER_SIZE;
    if (thing_count > 0) {
        memcpy(payload, (const void*)p_things, (size_t)thing_count * THING_SIZE);
        payload += (size_t)thing_count * THING_SIZE;
    }
    if (tilemap_bytes > 0) {
        memcpy(payload, (const void*)tilemap_ptr, tilemap_bytes);
    }

    if (out_len) *out_len = total_size;
    return 1;
}

static int full_state_apply_blob(const void* src, size_t src_len, char* err, size_t err_cap) {
    const FullStateBlobHeader* hdr = (const FullStateBlobHeader*)src;
    int current_thing_count = game_get_thing_count();
    int current_tilemap_w = 0;
    int current_tilemap_h = 0;
    size_t current_tilemap_bytes = game_get_tilemap_bytes(&current_tilemap_w, &current_tilemap_h);
    void* tilemap_ptr = game_get_tilemap_data_ptr();
    uintptr_t p0 = game_get_player_ptr(0);
    uintptr_t p1 = game_get_player_ptr(1);
    size_t expected_size;
    const uint8_t* payload;

    if (!src || src_len < sizeof(FullStateBlobHeader)) {
        full_state_set_err(err, err_cap, "state blob too small");
        return 0;
    }
    if (hdr->magic != FULL_STATE_BLOB_MAGIC) {
        full_state_set_err(err, err_cap, "invalid state blob magic");
        return 0;
    }
    if (hdr->version != FULL_STATE_BLOB_VERSION) {
        full_state_set_err(err, err_cap, "unsupported state blob version");
        return 0;
    }
    if (hdr->player_size != (uint32_t)PLAYER_SIZE || hdr->thing_size != (uint32_t)THING_SIZE) {
        full_state_set_err(err, err_cap, "state blob layout mismatch");
        return 0;
    }
    if (hdr->thing_count > 128u) {
        full_state_set_err(err, err_cap, "state blob thing count out of range");
        return 0;
    }
    if (hdr->tilemap_bytes > TILEMAP_MAX_BYTES) {
        full_state_set_err(err, err_cap, "state blob tilemap too large");
        return 0;
    }

    expected_size = full_state_blob_size_for_counts((int)hdr->thing_count, hdr->tilemap_bytes);
    if (src_len != expected_size) {
        full_state_set_err(err, err_cap, "state blob size mismatch");
        return 0;
    }
    if (!p0 || !p1) {
        full_state_set_err(err, err_cap, "player state unavailable");
        return 0;
    }
    if (current_thing_count != (int)hdr->thing_count) {
        full_state_set_err(err, err_cap, "thing count mismatch");
        return 0;
    }
    if (current_thing_count > 0 && (!p_things || !ptr_writable((void*)p_things, (SIZE_T)((size_t)current_thing_count * THING_SIZE)))) {
        full_state_set_err(err, err_cap, "thing state unavailable");
        return 0;
    }
    if (!p_thing_info || !ptr_writable((void*)p_thing_info, THING_INFO_STATE_SIZE)) {
        full_state_set_err(err, err_cap, "thing metadata state unavailable");
        return 0;
    }
    if (!p_room_info_state || !ptr_writable((void*)p_room_info_state, ROOM_INFO_STATE_SIZE)) {
        full_state_set_err(err, err_cap, "room info state unavailable");
        return 0;
    }
    if (!p_particle_state || !ptr_writable((void*)p_particle_state, PARTICLE_STATE_SIZE)) {
        full_state_set_err(err, err_cap, "particle state unavailable");
        return 0;
    }
    if (current_tilemap_w != hdr->tilemap_w || current_tilemap_h != hdr->tilemap_h || current_tilemap_bytes != (size_t)hdr->tilemap_bytes) {
        full_state_set_err(err, err_cap, "tilemap layout mismatch");
        return 0;
    }
    if (hdr->tilemap_bytes > 0 && (!tilemap_ptr || !ptr_writable(tilemap_ptr, (SIZE_T)hdr->tilemap_bytes))) {
        full_state_set_err(err, err_cap, "tilemap state unavailable");
        return 0;
    }

    if (ptr_writable((void*)p_game_active_room, sizeof(int))) {
        *p_game_active_room = hdr->active_room;
    }
    if (ptr_writable((void*)p_map_selector, sizeof(int))) {
        *p_map_selector = hdr->map_selector;
    }
    if (ptr_writable((void*)p_map_mode, sizeof(int))) {
        *p_map_mode = hdr->map_mode;
    }
    if (ptr_writable((void*)p_round_end_any, sizeof(int))) {
        *p_round_end_any = hdr->round_end_any;
    }
    if (ptr_writable((void*)p_score_target, sizeof(int))) {
        *p_score_target = hdr->score_target;
    }
    if (ptr_writable((void*)p_armed_respawn_limit, sizeof(int))) {
        *p_armed_respawn_limit = hdr->armed_respawn_limit;
    }
    if (ptr_writable((void*)p_score_p0, sizeof(int))) {
        *p_score_p0 = hdr->score_p0;
    }
    if (ptr_writable((void*)p_score_p1, sizeof(int))) {
        *p_score_p1 = hdr->score_p1;
    }
    if (ptr_writable((void*)p_chant_step, sizeof(int))) {
        *p_chant_step = hdr->chant_step;
    }
    if (ptr_writable((void*)p_chant_timer, sizeof(int))) {
        *p_chant_timer = hdr->chant_timer;
    }
    if (ptr_writable((void*)p_crowd_timer, sizeof(int))) {
        *p_crowd_timer = hdr->crowd_timer;
    }
    if (ptr_writable((void*)p_native_thing_count, sizeof(int))) {
        *p_native_thing_count = hdr->native_thing_count;
    }
    if (ptr_writable((void*)p_things_allocated, sizeof(int))) {
        *p_things_allocated = hdr->things_allocated;
    }
    if (ptr_writable((void*)p_thing_latest, sizeof(int))) {
        *p_thing_latest = hdr->thing_latest;
    }
    if (ptr_writable((void*)p_game_do_lerp_colours, sizeof(int))) {
        *p_game_do_lerp_colours = hdr->game_do_lerp_colours;
    }
    if (ptr_writable((void*)p_waterfall_count, sizeof(int))) {
        *p_waterfall_count = hdr->waterfall_count;
    }
    if (ptr_writable((void*)p_game_started, sizeof(int))) {
        *p_game_started = hdr->game_started;
    }
    if (ptr_writable((void*)p_lerp_time, sizeof(int))) {
        *p_lerp_time = hdr->lerp_time;
    }
    if (ptr_writable((void*)p_start_countdown, sizeof(int))) {
        *p_start_countdown = hdr->start_countdown;
    }
    if (ptr_writable((void*)p_end_countdown, sizeof(int))) {
        *p_end_countdown = hdr->end_countdown;
    }
    if (ptr_writable((void*)p_game_level, sizeof(uint32_t))) {
        *p_game_level = hdr->game_level;
    }
    if (ptr_writable((void*)p_game_old_active_room, sizeof(int))) {
        *p_game_old_active_room = hdr->game_old_active_room;
    }
    if (ptr_writable((void*)p_resumed, sizeof(int))) {
        *p_resumed = hdr->resumed;
    }
    if (ptr_writable((void*)p_freeze, sizeof(int))) {
        *p_freeze = hdr->freeze;
    }
    if (ptr_writable((void*)p_player_mode0, sizeof(int))) {
        *p_player_mode0 = hdr->player_mode0;
    }
    if (ptr_writable((void*)p_player_mode1, sizeof(int))) {
        *p_player_mode1 = hdr->player_mode1;
    }
    if (ptr_writable((void*)p_seed, sizeof(uint32_t))) {
        *p_seed = hdr->seed;
    }
    if (ptr_writable((void*)p_mrand_seed, sizeof(uint32_t))) {
        *p_mrand_seed = hdr->rng_seed;
    }
    if (ptr_writable((void*)p_native_game_ticks, sizeof(uint32_t))) {
        *p_native_game_ticks = hdr->native_game_ticks;
    }
    if (ptr_writable((void*)p_crowd_sound_last_tick, sizeof(uint32_t))) {
        *p_crowd_sound_last_tick = hdr->crowd_sound_last_tick;
    }
    /*
     * Do NOT restore _fx_15991 (the waterfall sound pointer). It is a live handle
     * into the process-local synth-effects array, not gameplay state - the saved
     * value goes stale the instant that slot is recycled, and writing it back made
     * the next game_update waterfall logic pour waterfall pitch/volume into whatever
     * unrelated sound now occupies the slot (the "waterfall noise with no waterfall"
     * bug). It is already canonicalized out of the rollback checksum, so leaving the
     * LIVE pointer untouched is safe: game_update re-derives the waterfall every
     * frame from the current room's tile scan (ghidra 24127-24164/24348-24360) and
     * starts/stops/updates the sound correctly on its own.
     *
     * (void) the saved field so the captured value is still validated/ignored.
     */
    (void)hdr->waterfall_fx_present;
    (void)hdr->waterfall_fx_raw;
    if (ptr_writable((void*)p_game_leader, sizeof(uintptr_t))) {
        uintptr_t leader_ptr = 0;
        if (hdr->leader_mode == FULL_STATE_LEADER_P0) leader_ptr = p0;
        else if (hdr->leader_mode == FULL_STATE_LEADER_P1) leader_ptr = p1;
        else if (hdr->leader_mode == FULL_STATE_LEADER_RAW) leader_ptr = hdr->leader_raw;
        *p_game_leader = leader_ptr;
    }
    if (ptr_writable((void*)p_loser, sizeof(uintptr_t))) {
        uintptr_t loser_ptr = 0;
        if (hdr->loser_mode == FULL_STATE_LEADER_P0) loser_ptr = p0;
        else if (hdr->loser_mode == FULL_STATE_LEADER_P1) loser_ptr = p1;
        else if (hdr->loser_mode == FULL_STATE_LEADER_RAW) loser_ptr = hdr->loser_raw;
        *p_loser = loser_ptr;
    }
    if (ptr_writable((void*)p_score_shudder, sizeof(int) * 2u)) {
        p_score_shudder[0] = hdr->score_shudder0;
        p_score_shudder[1] = hdr->score_shudder1;
    }
    if (ptr_writable((void*)p_roomdef_count, sizeof(int))) {
        *p_roomdef_count = hdr->roomdef_count;
    }
    if (ptr_writable((void*)p_room_w, sizeof(int))) {
        *p_room_w = hdr->room_w;
    }
    if (ptr_writable((void*)p_room_pixel_w, sizeof(int))) {
        *p_room_pixel_w = hdr->room_pixel_w;
    }
    if (ptr_writable((void*)p_tilemap_w, sizeof(int))) {
        *p_tilemap_w = hdr->tilemap_w;
    }
    if (ptr_writable((void*)p_tilemap_h, sizeof(int))) {
        *p_tilemap_h = hdr->tilemap_h;
    }
    if (ptr_writable((void*)p_tile_w_native, sizeof(int))) {
        *p_tile_w_native = hdr->tile_w;
    }
    if (ptr_writable((void*)p_tile_h_native, sizeof(int))) {
        *p_tile_h_native = hdr->tile_h;
    }
    if (ptr_writable((void*)p_tilemap_pixels_w, sizeof(int))) {
        *p_tilemap_pixels_w = hdr->tilemap_pixels_w;
    }
    if (ptr_writable((void*)p_tilemap_pixels_h, sizeof(int))) {
        *p_tilemap_pixels_h = hdr->tilemap_pixels_h;
    }
    if (ptr_writable((void*)p_map_w, sizeof(int))) {
        *p_map_w = hdr->map_w;
    }
    if (ptr_writable((void*)p_map_h, sizeof(int))) {
        *p_map_h = hdr->map_h;
    }
    if (ptr_writable((void*)p_camera_x, sizeof(float))) {
        *p_camera_x = hdr->camera_x;
    }
    if (ptr_writable((void*)p_camera_y, sizeof(float))) {
        *p_camera_y = hdr->camera_y;
    }
    if (ptr_writable((void*)p_camera_shake, sizeof(float))) {
        *p_camera_shake = hdr->camera_shake;
    }
    if (ptr_writable((void*)p_camera_shake_decay, sizeof(float))) {
        *p_camera_shake_decay = hdr->camera_shake_decay;
    }
    if (ptr_writable((void*)p_game_w, sizeof(float))) {
        *p_game_w = hdr->game_w;
    }
    if (ptr_writable((void*)p_game_h, sizeof(float))) {
        *p_game_h = hdr->game_h;
    }
    if (ptr_writable((void*)p_transient_game_state, TRANSIENT_GAME_STATE_SIZE)) {
        memcpy((void*)p_transient_game_state, hdr->transient_game_state, TRANSIENT_GAME_STATE_SIZE);
    }
    if (ptr_writable((void*)p_player_slots, sizeof(uintptr_t) * 2u)) {
        p_player_slots[0] = p0;
        p_player_slots[1] = p1;
    }
    if (ptr_writable((void*)p_controller, sizeof(uintptr_t))) {
        uintptr_t raw_controller = full_state_read_transient_ptr(hdr, ADDR_CONTROLLER);
        uintptr_t controller_ptr = full_state_remap_player_ptr(hdr, raw_controller, p0, p1);
        if (controller_ptr) {
            *p_controller = controller_ptr;
        }
    }
    if (ptr_writable((void*)p_game_leader, sizeof(uintptr_t))) {
        uintptr_t leader_ptr = 0;
        if (hdr->leader_mode == FULL_STATE_LEADER_P0) leader_ptr = p0;
        else if (hdr->leader_mode == FULL_STATE_LEADER_P1) leader_ptr = p1;
        *p_game_leader = leader_ptr;
    }
    if (ptr_writable((void*)p_loser, sizeof(uintptr_t))) {
        uintptr_t loser_ptr = 0;
        if (hdr->loser_mode == FULL_STATE_LEADER_P0) loser_ptr = p0;
        else if (hdr->loser_mode == FULL_STATE_LEADER_P1) loser_ptr = p1;
        *p_loser = loser_ptr;
    }
    memcpy((void*)p_thing_info, hdr->thing_info_state, THING_INFO_STATE_SIZE);
    memcpy((void*)p_room_info_state, hdr->room_info_state, ROOM_INFO_STATE_SIZE);
    memcpy((void*)p_particle_state, hdr->particle_state, PARTICLE_STATE_SIZE);
    g_game_tick_count = (unsigned long long)hdr->framework_tick_count;

    payload = ((const uint8_t*)src) + sizeof(*hdr);
    memcpy((void*)p0, payload, PLAYER_SIZE);
    payload += PLAYER_SIZE;
    memcpy((void*)p1, payload, PLAYER_SIZE);
    payload += PLAYER_SIZE;
    if (current_thing_count > 0) {
        memcpy((void*)p_things, payload, (size_t)current_thing_count * THING_SIZE);
        payload += (size_t)current_thing_count * THING_SIZE;
    }
    if (hdr->tilemap_bytes > 0) {
        memcpy(tilemap_ptr, payload, hdr->tilemap_bytes);
    }

    return 1;
}

static uint32_t full_state_crc32(const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint32_t)p[i];
        for (int bit = 0; bit < 8; bit++) {
            uint32_t mask = (uint32_t)-(int)(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

static int full_state_validate_blob_header(const void* src, size_t src_len, const FullStateBlobHeader** out_hdr, char* err, size_t err_cap) {
    const FullStateBlobHeader* hdr = (const FullStateBlobHeader*)src;
    size_t expected_size;

    if (!src || src_len < sizeof(FullStateBlobHeader)) {
        full_state_set_err(err, err_cap, "state blob too small");
        return 0;
    }
    if (hdr->magic != FULL_STATE_BLOB_MAGIC) {
        full_state_set_err(err, err_cap, "invalid state blob magic");
        return 0;
    }
    if (hdr->version != FULL_STATE_BLOB_VERSION) {
        full_state_set_err(err, err_cap, "unsupported state blob version");
        return 0;
    }
    if (hdr->player_size != (uint32_t)PLAYER_SIZE || hdr->thing_size != (uint32_t)THING_SIZE) {
        full_state_set_err(err, err_cap, "state blob layout mismatch");
        return 0;
    }
    if (hdr->thing_count > 128u) {
        full_state_set_err(err, err_cap, "state blob thing count out of range");
        return 0;
    }
    if (hdr->tilemap_bytes > TILEMAP_MAX_BYTES) {
        full_state_set_err(err, err_cap, "state blob tilemap too large");
        return 0;
    }
    expected_size = full_state_blob_size_for_counts((int)hdr->thing_count, hdr->tilemap_bytes);
    if (src_len != expected_size) {
        full_state_set_err(err, err_cap, "state blob size mismatch");
        return 0;
    }

    if (out_hdr) *out_hdr = hdr;
    return 1;
}

static void full_state_zero_player_render_colours(FullStateBlobHeader* hdr) {
    uint8_t* payload;
    uint8_t* things;

    if (!hdr) return;
    payload = (uint8_t*)hdr + sizeof(*hdr);
    for (int i = 0; i < 2; i++) {
        memset(payload + ((size_t)i * PLAYER_SIZE) + PLAYER_OFS_RENDER_RGBA, 0, PLAYER_RENDER_RGBA_LEN);
    }

    things = payload + ((size_t)PLAYER_SIZE * 2u);
    for (uint32_t i = 0; i < hdr->thing_count; i++) {
        uint8_t* thing = things + ((size_t)i * THING_SIZE);
        if (thing[THING_OFS_TYPE] == THING_TYPE_PLAYER) {
            memset(thing + PLAYER_OFS_RENDER_RGBA, 0, PLAYER_RENDER_RGBA_LEN);
        }
    }
}

static int full_state_canonicalize_rollback_blob_ex(void* blob, size_t blob_len, int zero_render_colours, char* err, size_t err_cap) {
    FullStateBlobHeader* hdr = (FullStateBlobHeader*)blob;

    if (!full_state_validate_blob_header(blob, blob_len, NULL, err, err_cap)) {
        return 0;
    }

    /*
     * Keep the native gameplay state strict. The framework tick counter is a
     * Lua scheduling clock, not a game-simulation field, and rollback replays
     * intentionally do not re-enter Lua on_tick handlers.
     */
    hdr->framework_tick_count = 0;
    hdr->leader_raw = 0;
    hdr->loser_raw = 0;
    hdr->waterfall_fx_present = 0;
    hdr->waterfall_fx_raw = 0;
    hdr->crowd_sound_last_tick = 0;
    hdr->chant_step = 0;
    hdr->chant_timer = 0;
    hdr->crowd_timer = 0;
    hdr->game_do_lerp_colours = 0;
    hdr->waterfall_count = 0;
    hdr->lerp_time = 0;
    hdr->score_shudder0 = 0;
    hdr->score_shudder1 = 0;
    hdr->resumed = 0;
    full_state_zero_transient_range(hdr, ADDR_LEADER, sizeof(uintptr_t));
    full_state_zero_transient_range(hdr, ADDR_WATERFALL_FX, sizeof(uintptr_t));
    full_state_zero_transient_range(hdr, ADDR_CROWD_SOUND_LAST_TICK, sizeof(uint32_t));
    /* The other five wall-clock sfx debounce timers the dev missed (see defines):
     * pure audio timing, never gameplay - zero them so toggling sound / a sfx
     * firing on one peer can't diverge the transient checksum. */
    full_state_zero_transient_range(hdr, ADDR_SOUND_DEDUP_TIMERS_A, SOUND_DEDUP_TIMERS_A_BYTES);
    full_state_zero_transient_range(hdr, ADDR_SOUND_DEDUP_TIMERS_B, SOUND_DEDUP_TIMERS_B_BYTES);
    full_state_zero_transient_range(hdr, ADDR_CHANT_STEP, sizeof(int));
    full_state_zero_transient_range(hdr, ADDR_CHANT_TIMER, sizeof(int));
    full_state_zero_transient_range(hdr, ADDR_CROWD_TIMER, sizeof(int));
    full_state_zero_transient_range(hdr, ADDR_GAME_DO_LERP_COLOURS, sizeof(int));
    full_state_zero_transient_range(hdr, ADDR_WATERFALL_COUNT, sizeof(int));
    full_state_zero_transient_range(hdr, ADDR_LERP_TIME, sizeof(int));
    full_state_zero_transient_range(hdr, ADDR_SCORE_SHUDDER, sizeof(int) * 2u);
    full_state_zero_transient_range(hdr, ADDR_PLAYER_ARRAY, sizeof(uintptr_t) * 2u);
    full_state_zero_transient_range(hdr, ADDR_CONTROLLER, sizeof(uintptr_t));
    full_state_zero_transient_range(hdr, ADDR_LOSER, sizeof(uintptr_t));
    if (zero_render_colours) {
        full_state_zero_player_render_colours(hdr);
        hdr->rng_seed = 0;
        hdr->game_old_active_room = hdr->active_room;
        hdr->camera_x = 0.0f;
        hdr->camera_y = 0.0f;
        hdr->camera_shake = 0.0f;
        hdr->camera_shake_decay = 0.0f;
        hdr->game_w = 0.0f;
        hdr->game_h = 0.0f;
        /*
         * Room colour-transition lerp values (see ADDR_COLOUR_LERP_BLOCK). The
         * lerp control fields (game_do_lerp_colours, lerp_time) were already
         * canonicalized, but the colour value storage was not - it drifts
         * between peers on render/timing differences and was the dominant
         * source of false-positive "transient" desyncs and the resulting
         * full-state correction storm. Exclude from the checksum only; the
         * live colours are untouched on load (this runs on a throwaway copy).
         */
        full_state_zero_transient_range(hdr, ADDR_COLOUR_LERP_BLOCK, COLOUR_LERP_BLOCK_BYTES);
        memset(hdr->particle_state, 0, sizeof(hdr->particle_state));
        /*
         * _room_info (ADDR_ROOM_INFO 0x543700, the entire ROOM_INFO_STATE_SIZE
         * 0x4444 region) is NOT gameplay state - it is the cosmetic
         * "skeleton_statue" background decoration buffer (ghidra skeleton_statue
         * @0x41fb..; the game does `memset(&_room_info,0,0x4444)` on room load and
         * treats it as a per-column ring of 32 decoration sprites at
         * &_room_info + col*0x404 + ringidx*0x20, with the ring index stored at
         * 0x543b00+col*0x404). It mirrors player position/colour and advances on
         * render timing, so its contents and ring indices drift between peers
         * (camera-culled, timing-dependent) and were the dominant `changed=room_info`
         * desync - even when the gameplay rng_seed matched. The real per-room
         * gameplay layout lives in a separate symbol (_room_templates), not here.
         * Exclude from the checksum only; live decoration memory is untouched.
         */
        memset(hdr->room_info_state, 0, sizeof(hdr->room_info_state));
    }

    return 1;
}

static int full_state_canonicalize_rollback_blob(void* blob, size_t blob_len, char* err, size_t err_cap) {
    return full_state_canonicalize_rollback_blob_ex(blob, blob_len, 0, err, err_cap);
}

static int full_state_canonicalize_rollback_checksum_blob(void* blob, size_t blob_len, char* err, size_t err_cap) {
    return full_state_canonicalize_rollback_blob_ex(blob, blob_len, 1, err, err_cap);
}

static int full_state_rollback_summary_from_canonical_blob(const void* src, size_t src_len, LuaGameStateRollbackSummary* out_summary, char* err, size_t err_cap) {
    const FullStateBlobHeader* hdr = NULL;
    const uint8_t* bytes = (const uint8_t*)src;
    const uint8_t* payload = NULL;
    const uint8_t* things = NULL;
    size_t players_len = (size_t)PLAYER_SIZE * 2u;
    size_t things_len = 0;
    size_t tilemap_len = 0;

    if (!out_summary) {
        full_state_set_err(err, err_cap, "summary output pointer unavailable");
        return 0;
    }
    memset(out_summary, 0, sizeof(*out_summary));
    if (!full_state_validate_blob_header(src, src_len, &hdr, err, err_cap)) {
        return 0;
    }

    things_len = (size_t)hdr->thing_count * (size_t)THING_SIZE;
    tilemap_len = (size_t)hdr->tilemap_bytes;
    payload = bytes + sizeof(*hdr);

    out_summary->full_crc = full_state_crc32(bytes, src_len);
    out_summary->header_crc = full_state_crc32(bytes, offsetof(FullStateBlobHeader, transient_game_state));
    out_summary->transient_crc = full_state_crc32(hdr->transient_game_state, sizeof(hdr->transient_game_state));
    out_summary->thing_info_crc = full_state_crc32(hdr->thing_info_state, sizeof(hdr->thing_info_state));
    out_summary->room_info_crc = full_state_crc32(hdr->room_info_state, sizeof(hdr->room_info_state));
    out_summary->particle_crc = full_state_crc32(hdr->particle_state, sizeof(hdr->particle_state));
    out_summary->player0_crc = full_state_crc32(payload, PLAYER_SIZE);
    out_summary->player1_crc = full_state_crc32(payload + PLAYER_SIZE, PLAYER_SIZE);
    out_summary->players_crc = full_state_crc32(payload, players_len);
    things = payload + players_len;
    out_summary->things_crc = full_state_crc32(things, things_len);
    for (uint32_t i = 0; i < hdr->thing_count && i < LUA_ROLLBACK_SUMMARY_THING_SLOTS; i++) {
        out_summary->thing_slot_crc[i] = full_state_crc32(things + ((size_t)i * (size_t)THING_SIZE), THING_SIZE);
    }
    out_summary->tilemap_crc = full_state_crc32(things + things_len, tilemap_len);
    out_summary->active_room = (uint32_t)hdr->active_room;
    out_summary->native_game_ticks = hdr->native_game_ticks;
    out_summary->rng_seed = hdr->rng_seed;
    out_summary->seed = hdr->seed;
    out_summary->thing_count = hdr->thing_count;
    out_summary->map_selector = (uint32_t)hdr->map_selector;
    out_summary->round_end_any = (uint32_t)hdr->round_end_any;
    out_summary->score_p0 = (uint32_t)hdr->score_p0;
    out_summary->score_p1 = (uint32_t)hdr->score_p1;
    return 1;
}

static int game_get_room_dims(int* out_room_w, int* out_room_h) {
    int room_w = 0;
    int room_h = 0;

    if (ptr_readable((const void*)p_room_w, sizeof(int))) {
        room_w = *p_room_w;
    }
    if (p_map_tiles_h && !IsBadCodePtr((FARPROC)(void*)p_map_tiles_h)) {
        room_h = p_map_tiles_h();
    }
    if (room_w < 0 || room_w > 2048) room_w = 0;
    if (room_h < 0 || room_h > 2048) room_h = 0;

    if (out_room_w) *out_room_w = room_w;
    if (out_room_h) *out_room_h = room_h;
    return (room_w > 0 && room_h > 0) ? 1 : 0;
}

static int game_get_active_room_index(void) {
    if (ptr_readable((const void*)p_game_active_room, sizeof(int))) {
        return *p_game_active_room;
    }
    return 0;
}

static void lua_push_field_number(lua_State* Ls, const char* key, double v) {
    lua_pushnumber(Ls, v);
    lua_setfield(Ls, -2, key);
}

static void lua_push_field_int(lua_State* Ls, const char* key, int v) {
    lua_pushinteger(Ls, v);
    lua_setfield(Ls, -2, key);
}

static void lua_push_field_bool(lua_State* Ls, const char* key, int v) {
    lua_pushboolean(Ls, v ? 1 : 0);
    lua_setfield(Ls, -2, key);
}

static void lua_push_rgba_table(lua_State* Ls, const float rgba[4]) {
    lua_newtable(Ls);
    for (int i = 0; i < 4; i++) {
        lua_pushnumber(Ls, (lua_Number)rgba[i]);
        lua_rawseti(Ls, -2, i + 1);
    }
    lua_push_field_number(Ls, "r", rgba[0]);
    lua_push_field_number(Ls, "g", rgba[1]);
    lua_push_field_number(Ls, "b", rgba[2]);
    lua_push_field_number(Ls, "a", rgba[3]);
}

static void lua_push_rgba_field(lua_State* Ls, const char* key, const float rgba[4]) {
    lua_push_rgba_table(Ls, rgba);
    lua_setfield(Ls, -2, key);
}

static UiNativeButton* mod_ui_native_find(LoadedMod* mod, const char* id, const char* state_name) {
    if (!mod || !id || !id[0] || !state_name || !state_name[0]) return NULL;
    for (int i = 0; i < mod->ui_native_count; i++) {
        UiNativeButton* b = mod->ui_native_buttons[i];
        if (!b) continue;
        if (_stricmp(b->id, id) != 0) continue;
        if (_stricmp(b->state_name, state_name) != 0) continue;
        return b;
    }
    return NULL;
}

static UiNativeButton* mod_ui_native_get_or_add(LoadedMod* mod, const char* id, const char* state_name) {
    UiNativeButton* b;
    if (!mod || !id || !id[0] || !state_name || !state_name[0]) return NULL;

    b = mod_ui_native_find(mod, id, state_name);
    if (b) return b;

    if (mod->ui_native_count + 1 > mod->ui_native_cap) {
        int newcap = (mod->ui_native_cap == 0) ? 8 : (mod->ui_native_cap * 2);
        UiNativeButton** nb = (UiNativeButton**)realloc(mod->ui_native_buttons, sizeof(UiNativeButton*) * newcap);
        if (!nb) return NULL;
        mod->ui_native_buttons = nb;
        mod->ui_native_cap = newcap;
    }

    b = (UiNativeButton*)calloc(1, sizeof(UiNativeButton));
    if (!b) return NULL;

    strncpy(b->id, id, sizeof(b->id) - 1);
    strncpy(b->state_name, state_name, sizeof(b->state_name) - 1);
    b->layout_x = 5.0f;
    b->layout_y = 5.0f;
    b->grid_x = 0.0f;
    b->grid_y = 0.0f;
    b->btn_ptr = NULL;
    b->clicked = 0;
    b->warned_non_menu = 0;

    mod->ui_native_buttons[mod->ui_native_count++] = b;
    return b;
}

static UiNativeButton* ui_native_find_by_btn_ptr(void* btn_ptr) {
    if (!btn_ptr) return NULL;
    for (int mi = 0; mi < g_mod_count; mi++) {
        LoadedMod* mod = &g_mods[mi];
        if (!mod->enabled) continue;
        for (int bi = 0; bi < mod->ui_native_count; bi++) {
            UiNativeButton* b = mod->ui_native_buttons[bi];
            if (!b) continue;
            if (b->btn_ptr == btn_ptr) return b;
        }
    }
    return NULL;
}

static int ui_native_run_player_filter(void* btn, int event_code) {
    uint32_t* tag_ptr;
    uint32_t old_tag;
    int ok = 0;

    if (!btn) return 0;
    if (!p_btn_player_filter) return 1;

    tag_ptr = (uint32_t*)((uint8_t*)btn + 4);
    old_tag = *tag_ptr;

    *tag_ptr = 0x11;
    ok = p_btn_player_filter(btn, event_code);
    if (!ok) {
        *tag_ptr = 0x12;
        ok = p_btn_player_filter(btn, event_code);
    }
    *tag_ptr = old_tag;
    return ok;
}

// Old-style link filter: allow either player selector to activate the same button.
// This mirrors the MODS menu entry button behavior (see hooks.c), but instead of
// switching states it just toggles the UiNativeButton.clicked flag.
//
// NOTE: The selector system does not call the main framed filter. It calls the
// per-player filter function pointer stored on the button struct (observed at
// +0xE4). Native buttons created via button_ex need this field populated.
static int __cdecl ui_native_player_filter_proxy(void* btn, int event_code) {
    if (!btn || !p_btn_player_filter) return 0;

    // Helper: try both selector tags for player 1/2. Some screens use 0x11/0x12,
    // others use 1/2.
    uint32_t* tag_ptr = (uint32_t*)((uint8_t*)btn + 4);
    uint32_t old_tag = *tag_ptr;
    int ok = 0;

    // Activation: if either selector activates, mark clicked and consume so the
    // engine's default link behavior (if any) cannot fire.
    if (event_code == 3) {
        *tag_ptr = 0x11;
        ok = p_btn_player_filter(btn, event_code);
        if (!ok) {
            *tag_ptr = 0x12;
            ok = p_btn_player_filter(btn, event_code);
        }
        if (!ok) {
            *tag_ptr = 1;
            ok = p_btn_player_filter(btn, event_code);
            if (!ok) {
                *tag_ptr = 2;
                ok = p_btn_player_filter(btn, event_code);
            }
        }

        *tag_ptr = old_tag;

        if (ok) {
            UiNativeButton* b = ui_native_find_by_btn_ptr(btn);
            if (b) b->clicked = 1;
        }

        return 0;
    }

    // Non-activation events: report the button as eligible for either selector.
    *tag_ptr = 0x11;
    if (p_btn_player_filter(btn, event_code)) {
        *tag_ptr = old_tag;
        return 1;
    }

    *tag_ptr = 0x12;
    if (p_btn_player_filter(btn, event_code)) {
        *tag_ptr = old_tag;
        return 1;
    }

    *tag_ptr = 1;
    if (p_btn_player_filter(btn, event_code)) {
        *tag_ptr = old_tag;
        return 1;
    }

    *tag_ptr = 2;
    if (p_btn_player_filter(btn, event_code)) {
        *tag_ptr = old_tag;
        return 1;
    }

    *tag_ptr = old_tag;
    return 0;
}

static int __cdecl ui_native_button_filter(void* btn, int event_code) {
    // Use the game's standard framed button behavior so the keyboard selectors
    // (sword cursors) can navigate to mod-created buttons.
    int ret = 0;
    if (p_main_btn_framed) {
        ret = p_main_btn_framed((int)(intptr_t)btn, event_code);
    } else if (p_btn_player_filter) {
        // Fallback: older builds may not have main_btn_framed symbol resolved.
        ret = p_btn_player_filter(btn, event_code);
    }

    // event_code==3 corresponds to activation/click.
    if (event_code == 3 && ret) {
        UiNativeButton* b = ui_native_find_by_btn_ptr(btn);
        if (b) b->clicked = 1;
    }

    return ret;
}

// =============================
// Lua API (per-mod; functions are closures with a LoadedMod* upvalue)
// =============================

static int lua_mod_log(lua_State *Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* msg = luaL_checkstring(Ls, 1);
    log_mod(mod, "INFO", msg);
    return 0;
}

static int lua_mod_warn(lua_State *Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* msg = luaL_checkstring(Ls, 1);
    log_mod(mod, "WARN", msg);
    return 0;
}

static int lua_mod_error(lua_State *Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* msg = luaL_checkstring(Ls, 1);
    log_mod(mod, "ERROR", msg);
    return 0;
}

static int lua_mod_on_frame(lua_State *Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    luaL_checktype(Ls, 1, LUA_TFUNCTION);
    lua_pushvalue(Ls, 1);
    int ref = luaL_ref(Ls, LUA_REGISTRYINDEX);
    reflist_push(&mod->on_frame, ref);
    return 0;
}

static int lua_mod_on_tick(lua_State *Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    luaL_checktype(Ls, 1, LUA_TFUNCTION);
    lua_pushvalue(Ls, 1);
    int ref = luaL_ref(Ls, LUA_REGISTRYINDEX);
    reflist_push(&mod->on_tick, ref);
    return 0;
}

static int lua_mod_on_tick_post(lua_State *Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    luaL_checktype(Ls, 1, LUA_TFUNCTION);
    lua_pushvalue(Ls, 1);
    int ref = luaL_ref(Ls, LUA_REGISTRYINDEX);
    reflist_push(&mod->on_tick_post, ref);
    return 0;
}

static int lua_mod_on_event(lua_State *Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    luaL_checktype(Ls, 1, LUA_TFUNCTION);
    lua_pushvalue(Ls, 1);
    int ref = luaL_ref(Ls, LUA_REGISTRYINDEX);
    reflist_push(&mod->on_event, ref);
    return 0;
}

static int lua_mod_on_load(lua_State *Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    luaL_checktype(Ls, 1, LUA_TFUNCTION);
    lua_pushvalue(Ls, 1);
    int ref = luaL_ref(Ls, LUA_REGISTRYINDEX);
    mod_set_single_ref(Ls, &mod->on_load_ref, ref);
    return 0;
}

static int lua_mod_on_unload(lua_State *Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    luaL_checktype(Ls, 1, LUA_TFUNCTION);
    lua_pushvalue(Ls, 1);
    int ref = luaL_ref(Ls, LUA_REGISTRYINDEX);
    mod_set_single_ref(Ls, &mod->on_unload_ref, ref);
    return 0;
}

static int lua_mod_info(lua_State *Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    lua_newtable(Ls);
    lua_pushstring(Ls, mod->id);          lua_setfield(Ls, -2, "id");
    lua_pushstring(Ls, mod->name);        lua_setfield(Ls, -2, "name");
    lua_pushstring(Ls, mod->version);     lua_setfield(Ls, -2, "version");
    lua_pushstring(Ls, mod->author);      lua_setfield(Ls, -2, "author");
    lua_pushstring(Ls, mod->description); lua_setfield(Ls, -2, "description");
    lua_pushstring(Ls, mod->entry);       lua_setfield(Ls, -2, "entry");
    lua_pushinteger(Ls, mod->api_version);lua_setfield(Ls, -2, "api_version");
    lua_pushboolean(Ls, mod->enabled);    lua_setfield(Ls, -2, "enabled");
    lua_pushstring(Ls, mod->folder_path); lua_setfield(Ls, -2, "folder_path");
    return 1;
}

static int lua_mod_get_path(lua_State *Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* rel = luaL_optstring(Ls, 1, "");
    char out[MAX_PATH];
    if (rel[0] == '\0') {
        snprintf(out, sizeof(out), "%s", mod->folder_path);
    } else {
        snprintf(out, sizeof(out), "%s\\%s", mod->folder_path, rel);
    }
    lua_pushstring(Ls, out);
    return 1;
}

// Like Lua's dofile(), but always runs in *this mod's* environment and is relative to the mod folder.
static int lua_mod_dofile(lua_State *Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* rel = luaL_checkstring(Ls, 1);
    char path[MAX_PATH];

    /* Confine to the mod's own folder: reject parent traversal and absolute paths. */
    if (strstr(rel, "..") || rel[0] == '\\' || rel[0] == '/' ||
        (rel[0] && rel[1] == ':')) {
        return luaL_error(Ls, "mod.dofile: path must stay inside the mod folder");
    }

    snprintf(path, sizeof(path), "%s\\%s", mod->folder_path, rel);

    if (luaL_loadfile(Ls, path) != 0) {
        // Error message already on stack
        return lua_error(Ls);
    }

    // Set the chunk environment to the mod environment
    lua_rawgeti(Ls, LUA_REGISTRYINDEX, mod->env_ref);
    lua_setfenv(Ls, -2);

    if (lua_pcall(Ls, 0, LUA_MULTRET, 0) != 0) {
        return lua_error(Ls);
    }

    // Remove the original argument (so returns match normal dofile())
    int nret = lua_gettop(Ls) - 1;
    lua_remove(Ls, 1);
    return nret;
}

// =============================
// Lua Config API (per-mod)
// =============================

static int lua_cfg_get(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* key = luaL_checkstring(Ls, 1);
    int idx = mod_config_find_index(mod, key);
    if (idx < 0) {
        if (!lua_isnoneornil(Ls, 2)) { lua_pushvalue(Ls, 2); return 1; }
        lua_pushnil(Ls);
        return 1;
    }
    ConfigEntry* e = &mod->cfg_entries[idx];
    switch (e->type) {
        case LUA_CFG_BOOL:
            lua_pushboolean(Ls, parse_bool(e->value));
            return 1;
        case LUA_CFG_INT:
            lua_pushinteger(Ls, atoi(e->value));
            return 1;
        case LUA_CFG_FLOAT:
            lua_pushnumber(Ls, atof(e->value));
            return 1;
        case LUA_CFG_STRING:
            lua_pushstring(Ls, e->value);
            return 1;
        case LUA_CFG_ENUM:
            lua_pushstring(Ls, e->value);
            return 1;
        default:
            lua_pushnil(Ls);
            return 1;
    }
}

static int lua_cfg_set(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* key = luaL_checkstring(Ls, 1);
    int idx = mod_config_find_index(mod, key);
    if (idx < 0) {
        lua_pushboolean(Ls, 0);
        return 1;
    }
    ConfigEntry* e = &mod->cfg_entries[idx];
    if (e->type == LUA_CFG_ACTION) {
        lua_pushboolean(Ls, 0);
        return 1;
    }

    switch (e->type) {
        case LUA_CFG_BOOL: {
            int b = 0;
            if (lua_isboolean(Ls, 2)) b = lua_toboolean(Ls, 2);
            else if (lua_isnumber(Ls, 2)) b = (lua_tonumber(Ls, 2) != 0);
            else b = parse_bool(luaL_checkstring(Ls, 2));
            snprintf(e->value, sizeof(e->value), "%s", b ? "true" : "false");
            break;
        }
        case LUA_CFG_INT: {
            int v = (int)luaL_checkinteger(Ls, 2);
            snprintf(e->value, sizeof(e->value), "%d", v);
            clamp_cfg_value(e);
            break;
        }
        case LUA_CFG_FLOAT: {
            double v = (double)luaL_checknumber(Ls, 2);
            snprintf(e->value, sizeof(e->value), "%.6g", v);
            clamp_cfg_value(e);
            break;
        }
        case LUA_CFG_STRING: {
            const char* s = luaL_checkstring(Ls, 2);
            strncpy(e->value, s, sizeof(e->value) - 1);
            e->value[sizeof(e->value) - 1] = '\0';
            break;
        }
        case LUA_CFG_ENUM: {
            const char* s = luaL_checkstring(Ls, 2);
            int oi = cfg_enum_find_option(e, s);
            if (oi < 0) { lua_pushboolean(Ls, 0); return 1; }
            snprintf(e->value, sizeof(e->value), "%s", e->options[oi]);
            break;
        }
        default:
            lua_pushboolean(Ls, 0);
            return 1;
    }

    mod_config_save(mod);
    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_cfg_on_action(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* key = luaL_checkstring(Ls, 1);
    luaL_checktype(Ls, 2, LUA_TFUNCTION);

    // Best-effort warning if schema doesn't declare this as an action
    int idx = mod_config_find_index(mod, key);
    if (idx < 0 || mod->cfg_entries[idx].type != LUA_CFG_ACTION) {
        char buf[256];
        snprintf(buf, sizeof(buf), "config.on_action('%s') registered, but '%s' is not declared as type 'action' in the config file", key, key);
        log_mod(mod, "WARN", buf);
    }

    ConfigAction* a = mod_config_get_or_add_action(mod, key);
    if (!a) return 0;
    lua_pushvalue(Ls, 2);
    int ref = luaL_ref(Ls, LUA_REGISTRYINDEX);
    reflist_push(&a->handlers, ref);
    return 0;
}

static void push_config_api_table(lua_State* Ls, LoadedMod* mod) {
    lua_newtable(Ls);

    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_cfg_get, 1);      lua_setfield(Ls, -2, "get");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_cfg_set, 1);      lua_setfield(Ls, -2, "set");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_cfg_on_action, 1);lua_setfield(Ls, -2, "on_action");

    lua_pushstring(Ls, mod->config_path); lua_setfield(Ls, -2, "path");
}

static int lua_storage_get(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* key = luaL_checkstring(Ls, 1);
    int idx = mod_storage_find_index(mod, key);
    if (idx < 0) {
        if (!lua_isnoneornil(Ls, 2)) { lua_pushvalue(Ls, 2); return 1; }
        lua_pushnil(Ls);
        return 1;
    }
    StorageEntry* e = &mod->storage_entries[idx];
    if (e->type == MOD_STORAGE_BOOL) {
        lua_pushboolean(Ls, e->bool_value ? 1 : 0);
    } else if (e->type == MOD_STORAGE_NUMBER) {
        lua_pushnumber(Ls, e->num_value);
    } else if (e->type == MOD_STORAGE_STRING) {
        lua_pushstring(Ls, e->str_value);
    } else {
        lua_pushnil(Ls);
    }
    return 1;
}

static int lua_storage_set(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* key = luaL_checkstring(Ls, 1);
    int ok = 0;

    if (!mod_storage_key_valid(key)) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, "invalid storage key");
        return 2;
    }

    if (lua_isnoneornil(Ls, 2)) {
        mod_storage_remove_key(mod, key);
        if (!mod->storage_suspend_save && !mod_storage_save(mod)) {
            lua_pushboolean(Ls, 0);
            lua_pushstring(Ls, "failed to save storage file");
            return 2;
        }
        lua_pushboolean(Ls, 1);
        return 1;
    }

    if (lua_isboolean(Ls, 2)) {
        ok = mod_storage_set_value(mod, key, MOD_STORAGE_BOOL, lua_toboolean(Ls, 2), 0.0, NULL);
    } else if (lua_isnumber(Ls, 2)) {
        ok = mod_storage_set_value(mod, key, MOD_STORAGE_NUMBER, 0, lua_tonumber(Ls, 2), NULL);
    } else if (lua_isstring(Ls, 2)) {
        ok = mod_storage_set_value(mod, key, MOD_STORAGE_STRING, 0, 0.0, lua_tostring(Ls, 2));
    } else {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, "unsupported value type (expected bool, number, string, or nil)");
        return 2;
    }

    if (!ok) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, "failed to set storage value");
        return 2;
    }
    if (!mod->storage_suspend_save && !mod_storage_save(mod)) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, "failed to save storage file");
        return 2;
    }

    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_storage_delete(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* key = luaL_checkstring(Ls, 1);
    int existed = mod_storage_remove_key(mod, key);
    if (!mod->storage_suspend_save && !mod_storage_save(mod)) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, "failed to save storage file");
        return 2;
    }
    lua_pushboolean(Ls, existed ? 1 : 0);
    return 1;
}

static int lua_storage_schema(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    lua_pushinteger(Ls, mod->storage_schema_version);
    return 1;
}

static int lua_storage_set_schema(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    int schema = (int)luaL_checkinteger(Ls, 1);
    if (schema < 0) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, "schema must be >= 0");
        return 2;
    }
    mod->storage_schema_version = schema;
    if (!mod->storage_suspend_save && !mod_storage_save(mod)) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, "failed to save storage file");
        return 2;
    }
    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_storage_migrate(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    int target_schema = (int)luaL_checkinteger(Ls, 1);
    int from_schema = mod->storage_schema_version;
    char errbuf[256];

    luaL_checktype(Ls, 2, LUA_TFUNCTION);

    if (target_schema < 0) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, "target schema must be >= 0");
        return 2;
    }
    if (target_schema <= from_schema) {
        lua_pushboolean(Ls, 1);
        lua_pushinteger(Ls, from_schema);
        return 2;
    }

    mod->storage_suspend_save++;
    lua_pushvalue(Ls, 2);
    lua_pushinteger(Ls, from_schema);
    lua_pushinteger(Ls, target_schema);
    if (lua_pcall(Ls, 2, 2, 0) != 0) {
        const char* err = lua_tostring(Ls, -1);
        snprintf(errbuf, sizeof(errbuf), "%s", err ? err : "storage.migrate callback failed");
        lua_pop(Ls, 1);
        if (mod->storage_suspend_save > 0) mod->storage_suspend_save--;
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, errbuf);
        return 2;
    }

    if (lua_isboolean(Ls, -2) && !lua_toboolean(Ls, -2)) {
        const char* err = lua_tostring(Ls, -1);
        lua_pop(Ls, 2);
        if (mod->storage_suspend_save > 0) mod->storage_suspend_save--;
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, err ? err : "storage migration callback returned false");
        return 2;
    }
    lua_pop(Ls, 2);
    if (mod->storage_suspend_save > 0) mod->storage_suspend_save--;

    mod->storage_schema_version = target_schema;
    if (!mod_storage_save(mod)) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, "failed to save storage file");
        return 2;
    }

    lua_pushboolean(Ls, 1);
    lua_pushinteger(Ls, target_schema);
    return 2;
}

static int lua_storage_save(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    if (!mod_storage_save(mod)) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, "failed to save storage file");
        return 2;
    }
    lua_pushboolean(Ls, 1);
    return 1;
}

static void push_storage_api_table(lua_State* Ls, LoadedMod* mod) {
    lua_newtable(Ls);

    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_storage_get, 1);        lua_setfield(Ls, -2, "get");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_storage_set, 1);        lua_setfield(Ls, -2, "set");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_storage_delete, 1);     lua_setfield(Ls, -2, "delete");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_storage_save, 1);       lua_setfield(Ls, -2, "save");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_storage_schema, 1);     lua_setfield(Ls, -2, "schema");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_storage_set_schema, 1); lua_setfield(Ls, -2, "set_schema");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_storage_migrate, 1);    lua_setfield(Ls, -2, "migrate");
    lua_pushstring(Ls, mod->storage_path);                                             lua_setfield(Ls, -2, "path");
}

static int lua_interop_provide(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* ns = luaL_checkstring(Ls, 1);
    const char* version = luaL_optstring(Ls, 2, mod->version);
    SemVersion parsed;
    int idx = -1;
    int ref = LUA_NOREF;

    luaL_checktype(Ls, 3, LUA_TTABLE);
    if (!ns[0] || strlen(ns) >= MOD_INTEROP_NAMESPACE_MAX) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, "namespace must be 1..95 chars");
        return 2;
    }
    if (!semver_parse(version, &parsed)) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, "version must be valid semver (e.g. 1.2.3)");
        return 2;
    }
    (void)parsed;

    idx = interop_find_index_by_ns(ns);
    if (idx >= 0 && g_interop_providers[idx].owner != mod) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, "namespace already provided by another mod");
        return 2;
    }

    lua_pushvalue(Ls, 3);
    ref = luaL_ref(Ls, LUA_REGISTRYINDEX);

    if (idx < 0) {
        if (!interop_ensure_capacity(g_interop_provider_count + 1)) {
            luaL_unref(Ls, LUA_REGISTRYINDEX, ref);
            lua_pushboolean(Ls, 0);
            lua_pushstring(Ls, "out of memory");
            return 2;
        }
        idx = g_interop_provider_count++;
        memset(&g_interop_providers[idx], 0, sizeof(g_interop_providers[idx]));
    } else if (g_interop_providers[idx].table_ref != LUA_NOREF && g_interop_providers[idx].table_ref != LUA_REFNIL) {
        luaL_unref(Ls, LUA_REGISTRYINDEX, g_interop_providers[idx].table_ref);
    }

    snprintf(g_interop_providers[idx].ns, sizeof(g_interop_providers[idx].ns), "%s", ns);
    snprintf(g_interop_providers[idx].version, sizeof(g_interop_providers[idx].version), "%s", version);
    g_interop_providers[idx].table_ref = ref;
    g_interop_providers[idx].owner = mod;

    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_interop_require(lua_State* Ls) {
    const char* ns = luaL_checkstring(Ls, 1);
    const char* range = luaL_optstring(Ls, 2, "");
    int idx = interop_find_index_by_ns(ns);
    if (idx < 0) {
        lua_pushnil(Ls);
        lua_pushstring(Ls, "namespace not provided");
        return 2;
    }
    if (range[0] && !semver_satisfies_range(g_interop_providers[idx].version, range)) {
        char err[192];
        snprintf(err, sizeof(err), "provider version %s does not satisfy range %s",
                 g_interop_providers[idx].version, range);
        lua_pushnil(Ls);
        lua_pushstring(Ls, err);
        return 2;
    }
    lua_rawgeti(Ls, LUA_REGISTRYINDEX, g_interop_providers[idx].table_ref);
    lua_pushstring(Ls, g_interop_providers[idx].version);
    return 2;
}

static void push_interop_api_table(lua_State* Ls, LoadedMod* mod) {
    lua_newtable(Ls);
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_interop_provide, 1); lua_setfield(Ls, -2, "provide");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_interop_require, 1); lua_setfield(Ls, -2, "require");
}

static UiLayout* mod_ui_layout_or_default(LoadedMod* mod) {
    if (!mod) return NULL;
    if (!mod->ui_layout.active) {
        mod->ui_layout.active = 1;
        mod->ui_layout.cursor_x = 28.0f;
        mod->ui_layout.cursor_y = 28.0f;
        mod->ui_layout.row_h = 30.0f;
        mod->ui_layout.gap = 6.0f;
        mod->ui_layout.width = 260.0f;
        mod->ui_layout.text_scale = 1.0f;
    }
    return &mod->ui_layout;
}

static int lua_ui_state_name(lua_State* Ls) {
    lua_pushstring(Ls, ui_state_name_from_ptr(ui_current_state_ptr()));
    return 1;
}

static int lua_ui_state_ptr(lua_State* Ls) {
    lua_pushnumber(Ls, (lua_Number)(uintptr_t)ui_current_state_ptr());
    return 1;
}

static int lua_ui_is_state(lua_State* Ls) {
    const char* name = luaL_checkstring(Ls, 1);
    lua_pushboolean(Ls, ui_state_matches_name(ui_current_state_ptr(), name));
    return 1;
}

static int lua_ui_screen_size(lua_State* Ls) {
    lua_pushnumber(Ls, ui_screen_w());
    lua_pushnumber(Ls, ui_screen_h());
    return 2;
}

static int lua_ui_mouse_pos(lua_State* Ls) {
    lua_pushinteger(Ls, g_ui_mouse_x);
    lua_pushinteger(Ls, g_ui_mouse_y);
    return 2;
}

static int lua_ui_mouse_buttons(lua_State* Ls) {
    int button = lua_isnumber(Ls, 1) ? (int)lua_tointeger(Ls, 1) : 0;
    if (button == 1) {
        lua_pushboolean(Ls, g_ui_mouse_down_left);
        lua_pushboolean(Ls, g_ui_mouse_pressed_left);
        return 2;
    }
    if (button == 2) {
        lua_pushboolean(Ls, g_ui_mouse_down_middle);
        lua_pushboolean(Ls, g_ui_mouse_pressed_middle);
        return 2;
    }
    if (button == 3) {
        lua_pushboolean(Ls, g_ui_mouse_down_right);
        lua_pushboolean(Ls, g_ui_mouse_pressed_right);
        return 2;
    }

    lua_pushboolean(Ls, g_ui_mouse_down_left);
    lua_pushboolean(Ls, g_ui_mouse_pressed_left);
    lua_pushboolean(Ls, g_ui_mouse_down_right);
    lua_pushboolean(Ls, g_ui_mouse_pressed_right);
    lua_pushboolean(Ls, g_ui_mouse_down_middle);
    lua_pushboolean(Ls, g_ui_mouse_pressed_middle);
    return 6;
}

static int lua_ui_set_default_cursor_visible(lua_State* Ls) {
    int visible = (lua_gettop(Ls) < 1) ? 1 : (lua_toboolean(Ls, 1) ? 1 : 0);
    if (!visible) g_ui_default_custom_cursor_suppressed = 1;
    else g_ui_default_custom_cursor_suppressed = 0;
    return 0;
}

static int lua_ui_hitbox(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    int off = lua_isstring(Ls, 1) ? 1 : 0;
    float x = (float)luaL_checknumber(Ls, 1 + off);
    float y = (float)luaL_checknumber(Ls, 2 + off);
    float w = (float)luaL_checknumber(Ls, 3 + off);
    float h = (float)luaL_checknumber(Ls, 4 + off);
    int button = 1;
    int hovered = 0;
    int clicked = 0;
    int down = 0;
    int top = lua_gettop(Ls);

    if (top >= 5 + off) {
        int opt_index = 5 + off;
        if (lua_isnumber(Ls, opt_index)) {
            button = (int)lua_tointeger(Ls, opt_index);
        } else if (lua_istable(Ls, opt_index)) {
            lua_getfield(Ls, opt_index, "button");
            if (lua_isnumber(Ls, -1)) button = (int)lua_tointeger(Ls, -1);
            lua_pop(Ls, 1);
        }
    }

    if (w > 0.0f && h > 0.0f) {
        hovered = (g_ui_mouse_x >= (int)x &&
                   g_ui_mouse_y >= (int)y &&
                   g_ui_mouse_x <= (int)(x + w) &&
                   g_ui_mouse_y <= (int)(y + h));
        if (button == 3) {
            clicked = hovered && g_ui_mouse_pressed_right;
            down = hovered && g_ui_mouse_down_right;
        } else if (button == 2) {
            clicked = hovered && g_ui_mouse_pressed_middle;
            down = hovered && g_ui_mouse_down_middle;
        } else {
            clicked = hovered && g_ui_mouse_pressed_left;
            down = hovered && g_ui_mouse_down_left;
        }
        mod_ui_push_hitbox(mod, x, y, w, h);
    }

    lua_pushboolean(Ls, hovered);
    lua_pushboolean(Ls, clicked);
    lua_pushboolean(Ls, down);
    return 3;
}

static int lua_ui_rect(lua_State* Ls) {
    float x = (float)luaL_checknumber(Ls, 1);
    float y = (float)luaL_checknumber(Ls, 2);
    float w = (float)luaL_checknumber(Ls, 3);
    float h = (float)luaL_checknumber(Ls, 4);
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
    if (lua_istable(Ls, 5)) {
        if (!ui_lua_read_color_field(Ls, 5, "bg", &r, &g, &b, &a) &&
            !ui_lua_read_color_field(Ls, 5, "fill", &r, &g, &b, &a)) {
            ui_lua_read_color_opts(Ls, 5, NULL, &r, &g, &b, &a);
        }
        a = ui_lua_read_number_field(Ls, 5, "alpha", a);
    } else {
        r = (float)luaL_optnumber(Ls, 5, 1.0);
        g = (float)luaL_optnumber(Ls, 6, 1.0);
        b = (float)luaL_optnumber(Ls, 7, 1.0);
        a = (float)luaL_optnumber(Ls, 8, 1.0);
    }
    hooks_ui_fill_rect(x, y, w, h, r, g, b, a);
    return 0;
}

static int lua_ui_fill_rect(lua_State* Ls) {
    return lua_ui_rect(Ls);
}

static int lua_ui_border(lua_State* Ls) {
    float x = (float)luaL_checknumber(Ls, 1);
    float y = (float)luaL_checknumber(Ls, 2);
    float w = (float)luaL_checknumber(Ls, 3);
    float h = (float)luaL_checknumber(Ls, 4);
    float line_w = 1.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
    if (lua_istable(Ls, 5)) {
        line_w = ui_lua_read_line_width_opts(Ls, 5, 1.0f);
        if (!ui_lua_read_color_field(Ls, 5, "border", &r, &g, &b, &a)) {
            ui_lua_read_color_opts(Ls, 5, NULL, &r, &g, &b, &a);
        }
        a = ui_lua_read_number_field(Ls, 5, "alpha", a);
    } else {
        line_w = (float)luaL_optnumber(Ls, 5, 1.0);
        r = (float)luaL_optnumber(Ls, 6, 1.0);
        g = (float)luaL_optnumber(Ls, 7, 1.0);
        b = (float)luaL_optnumber(Ls, 8, 1.0);
        a = (float)luaL_optnumber(Ls, 9, 1.0);
    }
    hooks_ui_stroke_rect(x, y, w, h, line_w, r, g, b, a);
    return 0;
}

static int lua_ui_stroke_rect(lua_State* Ls) {
    return lua_ui_border(Ls);
}

static int lua_ui_line(lua_State* Ls) {
    float x1 = (float)luaL_checknumber(Ls, 1);
    float y1 = (float)luaL_checknumber(Ls, 2);
    float x2 = (float)luaL_checknumber(Ls, 3);
    float y2 = (float)luaL_checknumber(Ls, 4);
    float line_w = 1.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
    if (lua_istable(Ls, 5)) {
        line_w = ui_lua_read_line_width_opts(Ls, 5, 1.0f);
        ui_lua_read_color_opts(Ls, 5, NULL, &r, &g, &b, &a);
        a = ui_lua_read_number_field(Ls, 5, "alpha", a);
    } else {
        line_w = (float)luaL_optnumber(Ls, 5, 1.0);
        r = (float)luaL_optnumber(Ls, 6, 1.0);
        g = (float)luaL_optnumber(Ls, 7, 1.0);
        b = (float)luaL_optnumber(Ls, 8, 1.0);
        a = (float)luaL_optnumber(Ls, 9, 1.0);
    }
    hooks_ui_draw_line(x1, y1, x2, y2, line_w, r, g, b, a);
    return 0;
}

static int lua_ui_measure_text(lua_State* Ls) {
    const char* text = luaL_checkstring(Ls, 1);
    float scale = 1.0f;
    float w = 0.0f;
    float h = 0.0f;
    if (lua_istable(Ls, 2)) {
        scale = ui_lua_read_number_field(Ls, 2, "scale", 1.0f);
    } else {
        scale = (float)luaL_optnumber(Ls, 2, 1.0);
    }
    ui_measure_text_bounds(text, scale, &w, &h);
    lua_pushnumber(Ls, w);
    lua_pushnumber(Ls, h);
    return 2;
}

static int lua_ui_readable_scale(lua_State* Ls) {
    float scale = (float)luaL_optnumber(Ls, 1, 1.0);
    lua_pushnumber(Ls, ui_readable_text_scale(scale));
    return 1;
}

/* ── UI draw layering ────────────────────────────────────────────────────
 *
 * The engine's sprite batch system queues sprites/text and flushes them at
 * specific points during the render pipeline.  Mod on_frame callbacks fire
 * from SDL_GL_SwapWindow — AFTER the game has already flushed its batches.
 * Anything mods plot into the batch carries over to the NEXT frame and
 * gets flushed BEFORE tiles, making mod UI appear below the game world.
 *
 * The layering API fixes this by giving mods explicit control:
 *
 *   ui.flush()            Force-flush the sprite batch NOW.  Everything
 *                         queued up to this point is drawn immediately.
 *
 *   ui.begin_overlay()    Flush pending game sprites, then enter overlay
 *                         mode.  All subsequent text_at / draw_sprite calls
 *                         will be drawn ON TOP of the game world.
 *
 *   ui.end_overlay()      Flush overlay sprites and restore state.
 *
 * Usage from Lua:
 *   mod.on_frame(function()
 *       mod.ui.begin_overlay()
 *       mod.ui.text_at("HUD TEXT", 10, 10, 1.0, 1, 1, 1)
 *       mod.ui.draw_sprite(spr, 100, 100)
 *       mod.ui.end_overlay()
 *   end)
 */

static int g_overlay_active = 0;

static int lua_ui_flush(lua_State* Ls) {
    (void)Ls;
    if (p_main_sprite_batches_draw) {
        p_main_sprite_batches_draw();
    }
    ui_reset_render_state();
    return 0;
}

static int lua_ui_begin_overlay(lua_State* Ls) {
    (void)Ls;
    /* Flush any pending game sprites so they render BELOW the overlay. */
    if (p_main_sprite_batches_draw) {
        p_main_sprite_batches_draw();
    }
    ui_reset_render_state();
    g_overlay_active = 1;
    return 0;
}

static int lua_ui_end_overlay(lua_State* Ls) {
    (void)Ls;
    /* Flush overlay sprites so they render NOW, on top of everything. */
    if (p_main_sprite_batches_draw) {
        p_main_sprite_batches_draw();
    }
    ui_reset_render_state();
    g_overlay_active = 0;
    return 0;
}

static int lua_ui_sheet_base(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* sheet = luaL_checkstring(Ls, 1);
    int base = -1;
    if (!ui_sheet_base_from_name(sheet, &base)) {
        int found = mod_asset_sheet_find(mod, sheet);
        if (found < 0) {
            lua_pushnil(Ls);
            return 1;
        }
        base = mod->asset_sheets[found].base_id;
    }
    lua_pushinteger(Ls, base);
    return 1;
}

static int lua_ui_sprite_id(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    int base = -1;
    int idx = (int)luaL_checkinteger(Ls, 2);
    if (lua_isnumber(Ls, 1)) {
        base = (int)lua_tointeger(Ls, 1);
    } else {
        const char* sheet = luaL_checkstring(Ls, 1);
        if (!ui_sheet_base_from_name(sheet, &base)) {
            int found = mod_asset_sheet_find(mod, sheet);
            if (found < 0) {
                lua_pushnil(Ls);
                lua_pushfstring(Ls, "unknown sheet '%s'", sheet);
                return 2;
            }
            if (idx < 0 || idx >= mod->asset_sheets[found].count) {
                lua_pushnil(Ls);
                lua_pushstring(Ls, "asset sprite index out of range");
                return 2;
            }
            base = mod->asset_sheets[found].base_id;
        }
    }
    lua_pushinteger(Ls, base + idx);
    return 1;
}

static int lua_ui_draw_sprite(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    int sprite_id = -1;
    float x = (float)luaL_checknumber(Ls, 2);
    float y = (float)luaL_checknumber(Ls, 3);
    int flip = 0;
    int layer = 0;
    float sx = 1.0f;
    float sy = 1.0f;
    float angle = 0.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
    void* sprite_ptr;

    if (lua_istable(Ls, 1)) {
        int base = -1;
        lua_getfield(Ls, 1, "sprite");
        if (lua_isnumber(Ls, -1)) sprite_id = (int)lua_tointeger(Ls, -1);
        lua_pop(Ls, 1);

        if (sprite_id < 0) {
            lua_getfield(Ls, 1, "id");
            if (lua_isnumber(Ls, -1)) sprite_id = (int)lua_tointeger(Ls, -1);
            lua_pop(Ls, 1);
        }

        if (sprite_id < 0) {
            lua_getfield(Ls, 1, "sheet");
            if (lua_isnumber(Ls, -1)) {
                base = (int)lua_tointeger(Ls, -1);
            } else if (lua_isstring(Ls, -1)) {
                const char* sheet_name = lua_tostring(Ls, -1);
                if (!ui_sheet_base_from_name(sheet_name, &base)) {
                    int found = mod_asset_sheet_find(mod, sheet_name);
                    if (found >= 0) base = mod->asset_sheets[found].base_id;
                }
            }
            lua_pop(Ls, 1);

            if (base >= 0) {
                lua_getfield(Ls, 1, "index");
                if (lua_isnumber(Ls, -1)) {
                    sprite_id = base + (int)lua_tointeger(Ls, -1);
                }
                lua_pop(Ls, 1);
            }
        }
    } else {
        sprite_id = (int)luaL_checkinteger(Ls, 1);
    }

    if (sprite_id < 0 || !p_sprite_get || !p_sprite_batch_plot || !p_turtle_set_pos_unscaled || !p_turtle_set_scale || !p_turtle_set_angle) {
        lua_pushboolean(Ls, 0);
        return 1;
    }

    if (!p_turtle_set_rgba && !p_turtle_set_rgb) {
        lua_pushboolean(Ls, 0);
        return 1;
    }

    if (lua_istable(Ls, 1)) {
        ui_lua_apply_sprite_opts(Ls, 1, &flip, &layer, &sx, &sy, &angle, &r, &g, &b, &a);
    }
    if (lua_istable(Ls, 4)) {
        ui_lua_apply_sprite_opts(Ls, 4, &flip, &layer, &sx, &sy, &angle, &r, &g, &b, &a);
    }
    if (layer < 0 || layer > 1) layer = 0;

    sprite_ptr = p_sprite_get((uint32_t)sprite_id);
    if (!sprite_ptr) {
        lua_pushboolean(Ls, 0);
        return 1;
    }

    p_turtle_set_angle((double)angle);
    p_turtle_set_scale((double)sx, (double)sy);
    if (p_turtle_set_rgba) p_turtle_set_rgba(r, g, b, a);
    else p_turtle_set_rgb(r, g, b);
    p_turtle_set_pos_unscaled((double)x, (double)y);
    p_sprite_batch_plot((int)(intptr_t)sprite_ptr, flip ? 1 : 0, layer);

    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_ui_layout(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    UiLayout* layout = mod_ui_layout_or_default(mod);
    if (!layout) return 0;

    layout->active = 1;
    layout->cursor_x = (float)luaL_checknumber(Ls, 1);
    layout->cursor_y = (float)luaL_checknumber(Ls, 2);
    layout->row_h = (float)luaL_optnumber(Ls, 3, 30.0);
    layout->gap = (float)luaL_optnumber(Ls, 4, 6.0);
    layout->width = (float)luaL_optnumber(Ls, 5, 260.0);
    layout->text_scale = (float)luaL_optnumber(Ls, 6, 1.0);

    if (layout->row_h < 8.0f) layout->row_h = 8.0f;
    if (layout->gap < 0.0f) layout->gap = 0.0f;
    if (layout->width < 20.0f) layout->width = 20.0f;
    if (layout->text_scale < 0.4f) layout->text_scale = 0.4f;
    if (layout->text_scale > 3.0f) layout->text_scale = 3.0f;
    return 0;
}

static int lua_ui_cursor(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    UiLayout* layout = mod_ui_layout_or_default(mod);
    int n = lua_gettop(Ls);
    if (!layout) return 0;
    if (n >= 1) layout->cursor_x = (float)luaL_checknumber(Ls, 1);
    if (n >= 2) layout->cursor_y = (float)luaL_checknumber(Ls, 2);
    lua_pushnumber(Ls, layout->cursor_x);
    lua_pushnumber(Ls, layout->cursor_y);
    return 2;
}

static int lua_ui_next_row(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    UiLayout* layout = mod_ui_layout_or_default(mod);
    int rows = (int)luaL_optinteger(Ls, 1, 1);
    if (!layout) return 0;
    if (rows < 1) rows = 1;
    layout->cursor_y += (layout->row_h + layout->gap) * (float)rows;
    return 0;
}

static int lua_ui_text(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    UiLayout* layout = mod_ui_layout_or_default(mod);
    const char* text = luaL_checkstring(Ls, 1);
    float r = 0.95f;
    float g = 0.95f;
    float b = 0.95f;
    float a = 1.0f;
    float scale;
    if (!layout) return 0;
    if (lua_istable(Ls, 2)) {
        ui_lua_read_color_opts(Ls, 2, NULL, &r, &g, &b, &a);
        a = ui_lua_read_number_field(Ls, 2, "alpha", a);
        scale = ui_lua_read_number_field(Ls, 2, "scale", layout->text_scale);
    } else {
        r = (float)luaL_optnumber(Ls, 2, 0.95);
        g = (float)luaL_optnumber(Ls, 3, 0.95);
        b = (float)luaL_optnumber(Ls, 4, 0.95);
        scale = (float)luaL_optnumber(Ls, 5, layout->text_scale);
        a = (float)luaL_optnumber(Ls, 6, 1.0);
    }
    ui_draw_text_mode_alpha(layout->cursor_x, layout->cursor_y, scale, r, g, b, a, text, 0);
    layout->cursor_y += layout->row_h + layout->gap;
    return 0;
}

static int lua_ui_text_at(lua_State* Ls) {
    const char* text = luaL_checkstring(Ls, 1);
    float x = (float)luaL_checknumber(Ls, 2);
    float y = (float)luaL_checknumber(Ls, 3);
    float scale = 1.0f;
    float r = 0.95f;
    float g = 0.95f;
    float b = 0.95f;
    float a = 1.0f;
    if (lua_istable(Ls, 4)) {
        scale = ui_lua_read_number_field(Ls, 4, "scale", 1.0f);
        ui_lua_read_color_opts(Ls, 4, NULL, &r, &g, &b, &a);
        a = ui_lua_read_number_field(Ls, 4, "alpha", a);
    } else {
        scale = (float)luaL_optnumber(Ls, 4, 1.0);
        r = (float)luaL_optnumber(Ls, 5, 0.95);
        g = (float)luaL_optnumber(Ls, 6, 0.95);
        b = (float)luaL_optnumber(Ls, 7, 0.95);
        a = (float)luaL_optnumber(Ls, 8, 1.0);
    }
    ui_draw_text_mode_alpha(x, y, scale, r, g, b, a, text, 0);
    return 0;
}

static int ui_button_common(lua_State* Ls,
                            LoadedMod* mod,
                            const char* id,
                            const char* label,
                            float x,
                            float y,
                            float w,
                            float h,
                            float scale) {
    int hovered;
    int clicked;
    char rendered[256];
    float tx;
    float ty;
    float tr = 0.82f, tg = 0.88f, tb = 0.98f;
    (void)id;

    if (!label) label = "";
    if (scale < 0.4f) scale = 0.4f;
    if (scale > 3.0f) scale = 3.0f;
    snprintf(rendered, sizeof(rendered), "[ %s ]", label);
    if (w <= 0.0f) w = ui_approx_text_width(rendered, scale) + 14.0f;
    if (h <= 0.0f) h = ui_text_line_height(scale) + 14.0f;

    hovered = (g_ui_mouse_x >= (int)x &&
               g_ui_mouse_y >= (int)y &&
               g_ui_mouse_x <= (int)(x + w) &&
               g_ui_mouse_y <= (int)(y + h));
    clicked = hovered && g_ui_mouse_pressed_left;

    if (clicked) {
        tr = 1.00f; tg = 0.93f; tb = 0.45f;
    } else if (hovered && g_ui_mouse_down_left) {
        tr = 0.98f; tg = 0.86f; tb = 0.40f;
    } else if (hovered) {
        tr = 0.95f; tg = 0.90f; tb = 0.60f;
    }

    tx = x + 4.0f;
    ty = y + (h * 0.55f);
    ui_draw_text_mode(tx, ty, scale, tr, tg, tb, rendered, 0);
    mod_ui_push_hitbox(mod, x, y, w, h);

    lua_pushboolean(Ls, clicked);
    return 1;
}

static int lua_ui_button(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    UiLayout* layout = mod_ui_layout_or_default(mod);
    const char* id = luaL_checkstring(Ls, 1);
    const char* label = luaL_checkstring(Ls, 2);
    float w;
    float h;
    int ret;

    if (!layout) {
        lua_pushboolean(Ls, 0);
        return 1;
    }

    w = (float)luaL_optnumber(Ls, 3, layout->width);
    h = (float)luaL_optnumber(Ls, 4, layout->row_h);
    ret = ui_button_common(Ls, mod, id, label, layout->cursor_x, layout->cursor_y, w, h, layout->text_scale);
    layout->cursor_y += h + layout->gap;
    return ret;
}

static int lua_ui_button_at(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    UiLayout* layout = mod_ui_layout_or_default(mod);
    const char* id = luaL_checkstring(Ls, 1);
    const char* label = luaL_checkstring(Ls, 2);
    float x = (float)luaL_checknumber(Ls, 3);
    float y = (float)luaL_checknumber(Ls, 4);
    float w = (float)luaL_optnumber(Ls, 5, 0.0);
    float h = (float)luaL_optnumber(Ls, 6, 0.0);
    float scale = layout ? layout->text_scale : 1.0f;
    return ui_button_common(Ls, mod, id, label, x, y, w, h, scale);
}

// Forward declaration; implementation lives below with other UI string helpers.
static char* mod_ui_pool_strdup(LoadedMod* mod, const char* s, size_t len);

static int lua_ui_native_button(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* id = luaL_checkstring(Ls, 1);
    const char* label = luaL_checkstring(Ls, 2);
    float grid_x = (float)luaL_checknumber(Ls, 3);
    float grid_y = (float)luaL_checknumber(Ls, 4);
    float layout_x = (float)luaL_optnumber(Ls, 5, 5.0);
    float layout_y = (float)luaL_optnumber(Ls, 6, 5.0);
    const char* state_name = ui_state_name_from_ptr(ui_current_state_ptr());
    // Treat main_initial as main so title-menu native buttons appear immediately
    // on startup and persist across the title state handoff.
    const char* button_state_name = (_stricmp(state_name, "main_initial") == 0) ? "main" : state_name;
    UiNativeButton* b;
    int label_changed = 0;
    int created_now = 0;
    int clicked = 0;

    if (!mod) {
        lua_pushboolean(Ls, 0);
        return 1;
    }

    b = mod_ui_native_get_or_add(mod, id, button_state_name);
    if (!b) {
        lua_pushboolean(Ls, 0);
        return 1;
    }

    label_changed = (strcmp(b->label, label ? label : "") != 0);
    strncpy(b->label, label ? label : "", sizeof(b->label) - 1);
    b->label[sizeof(b->label) - 1] = '\0';
    b->grid_x = grid_x;
    b->grid_y = grid_y;
    b->layout_x = layout_x;
    b->layout_y = layout_y;

    if (b->btn_ptr && !ui_engine_button_exists(b->btn_ptr)) {
        b->btn_ptr = NULL;
    }

    if (!ui_is_menu_state_name(state_name)) {
        if (!b->warned_non_menu) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "ui.native_button('%s') ignored in non-menu state '%s'",
                     b->id, state_name);
            log_mod(mod, "WARN", msg);
            b->warned_non_menu = 1;
        }
        clicked = b->clicked;
        b->clicked = 0;
        lua_pushboolean(Ls, clicked);
        return 1;
    }

    b->warned_non_menu = 0;

    if (!b->btn_ptr && p_button_ex) {
        if (p_button_set_layout) p_button_set_layout(b->layout_x, b->layout_y);
        b->btn_ptr = p_button_ex(b->grid_x, b->grid_y, 0, b->label, (int)(intptr_t)&ui_native_button_filter);
        if (b->btn_ptr) {
            created_now = 1;
            // Match MODS menu button behavior so sword selectors can land on it.
            // +0xE0: link target / state bridge (must be non-null for some menus)
            // +0xE4: per-player filter pointer (used by selectors)
            *(void**)((uint8_t*)b->btn_ptr + 0xE0) = ui_current_state_ptr();
        }
    }

    // Keep native button text in sync when Lua changes label for an existing ID.
    if (b->btn_ptr && (created_now || label_changed)) {
        if (!IsBadWritePtr((uint8_t*)b->btn_ptr + BTN_OFS_LABEL_PTR, (SIZE_T)sizeof(void*))) {
            size_t len = strlen(b->label);
            char* owned = mod_ui_pool_strdup(mod, b->label, len);
            if (owned) {
                *(const char**)((uint8_t*)b->btn_ptr + BTN_OFS_LABEL_PTR) = owned;
            }
        }
    }

    clicked = b->clicked;
    b->clicked = 0;
    lua_pushboolean(Ls, clicked);
    return 1;
}

static int lua_ui_native_set_pos(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* id = luaL_checkstring(Ls, 1);
    float x = (float)luaL_checknumber(Ls, 2);
    float y = (float)luaL_checknumber(Ls, 3);
    const char* state_name = ui_state_name_from_ptr(ui_current_state_ptr());
    const char* button_state_name = (_stricmp(state_name, "main_initial") == 0) ? "main" : state_name;
    UiNativeButton* b;

    if (!mod) { lua_pushboolean(Ls, 0); return 1; }
    b = mod_ui_native_find(mod, id, button_state_name);
    if (!b || !b->btn_ptr || !ui_engine_button_exists(b->btn_ptr)) { lua_pushboolean(Ls, 0); return 1; }

    *(float*)((uint8_t*)b->btn_ptr + BTN_OFS_CENTER_X) = x;
    *(float*)((uint8_t*)b->btn_ptr + BTN_OFS_CENTER_Y) = y;

    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_ui_native_set_layout(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* id = luaL_checkstring(Ls, 1);
    float layout_x = (float)luaL_checknumber(Ls, 2);
    float layout_y = (float)luaL_checknumber(Ls, 3);
    const char* state_name = ui_state_name_from_ptr(ui_current_state_ptr());
    const char* button_state_name = (_stricmp(state_name, "main_initial") == 0) ? "main" : state_name;
    UiNativeButton* b;

    if (!mod) { lua_pushboolean(Ls, 0); return 1; }
    b = mod_ui_native_find(mod, id, button_state_name);
    if (!b) { lua_pushboolean(Ls, 0); return 1; }

    b->layout_x = layout_x;
    b->layout_y = layout_y;
    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_ui_native_resize(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* id = luaL_checkstring(Ls, 1);
    float w = (float)luaL_checknumber(Ls, 2);
    float h = (float)luaL_checknumber(Ls, 3);
    float shrink = (float)luaL_optnumber(Ls, 4, 4.0);
    const char* state_name = ui_state_name_from_ptr(ui_current_state_ptr());
    const char* button_state_name = (_stricmp(state_name, "main_initial") == 0) ? "main" : state_name;
    UiNativeButton* b;

    if (!mod || w <= 0.0f || h <= 0.0f) { lua_pushboolean(Ls, 0); return 1; }
    b = mod_ui_native_find(mod, id, button_state_name);
    if (!b || !b->btn_ptr || !ui_engine_button_exists(b->btn_ptr)) { lua_pushboolean(Ls, 0); return 1; }

    if (shrink < 0.0f) shrink = 0.0f;
    if (p_button_set_w_ex) p_button_set_w_ex((int)(intptr_t)b->btn_ptr, w, shrink);
    if (p_button_set_h_ex) p_button_set_h_ex((int)(intptr_t)b->btn_ptr, h, shrink);

    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_ui_native_set_text_scale(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* id = luaL_checkstring(Ls, 1);
    float sx = (float)luaL_checknumber(Ls, 2);
    float sy = (float)luaL_optnumber(Ls, 3, sx);
    const char* state_name = ui_state_name_from_ptr(ui_current_state_ptr());
    const char* button_state_name = (_stricmp(state_name, "main_initial") == 0) ? "main" : state_name;
    UiNativeButton* b;

    if (!mod) { lua_pushboolean(Ls, 0); return 1; }
    if (sx < 0.10f) sx = 0.10f;
    if (sy < 0.10f) sy = 0.10f;
    if (sx > 3.00f) sx = 3.00f;
    if (sy > 3.00f) sy = 3.00f;

    b = mod_ui_native_find(mod, id, button_state_name);
    if (!b || !b->btn_ptr || !ui_engine_button_exists(b->btn_ptr)) { lua_pushboolean(Ls, 0); return 1; }

    *(float*)((uint8_t*)b->btn_ptr + BTN_OFS_TEXT_SCALE_X) = sx;
    *(float*)((uint8_t*)b->btn_ptr + BTN_OFS_TEXT_SCALE_Y) = sy;
    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_ui_native_hide(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* id = luaL_checkstring(Ls, 1);
    int hidden = lua_toboolean(Ls, 2);
    const char* state_name = ui_state_name_from_ptr(ui_current_state_ptr());
    const char* button_state_name = (_stricmp(state_name, "main_initial") == 0) ? "main" : state_name;
    UiNativeButton* b;

    if (!mod) { lua_pushboolean(Ls, 0); return 1; }
    b = mod_ui_native_find(mod, id, button_state_name);
    if (!b || !b->btn_ptr || !ui_engine_button_exists(b->btn_ptr)) { lua_pushboolean(Ls, 0); return 1; }

    ui_button_apply_flags_hidden(b->btn_ptr, hidden);
    lua_pushboolean(Ls, 1);
    return 1;
}



static char* mod_ui_pool_strdup(LoadedMod* mod, const char* s, size_t len) {
    if (!mod || !s) return NULL;
    if (len > 2048) len = 2048; // sanity cap for UI strings
    char* mem = (char*)malloc(len + 1);
    if (!mem) return NULL;
    memcpy(mem, s, len);
    mem[len] = '\0';

    if (mod->ui_string_count + 1 > mod->ui_string_cap) {
        int newcap = (mod->ui_string_cap == 0) ? 8 : (mod->ui_string_cap * 2);
        char** np = (char**)realloc(mod->ui_string_pool, sizeof(char*) * newcap);
        if (!np) {
            free(mem);
            return NULL;
        }
        mod->ui_string_pool = np;
        mod->ui_string_cap = newcap;
    }
    mod->ui_string_pool[mod->ui_string_count++] = mem;
    return mem;
}

static void* ui_lua_ptr_to_button(lua_State* Ls, int idx) {
    if (!lua_isnumber(Ls, idx)) return NULL;
    uintptr_t raw = (uintptr_t)lua_tonumber(Ls, idx);
    void* btn = (void*)raw;
    if (!ui_engine_button_exists(btn)) return NULL;
    return btn;
}

static int lua_ui_find_button_by_action_ptr(lua_State* Ls) {
    uintptr_t action_ptr = (uintptr_t)luaL_checknumber(Ls, 1);
    int nth = (int)luaL_optinteger(Ls, 2, 1);
    const char* state_name = ui_state_name_from_ptr(ui_current_state_ptr());

    if (action_ptr == 0 || nth < 1 || !p_button_count || !p_button_get) {
        lua_pushnil(Ls);
        return 1;
    }
    // NOTE: We used to hard-disable button scanning during main_initial because
    // the button list can be in flux during the startup handoff. That makes it
    // impossible for mods to tweak the initial title screen (e.g. resize/move
    // the START button) unless the user leaves/re-enters the menu.
    //
    // Instead of blanket-disabling, keep scanning but guard all reads so we
    // never dereference an invalid transient pointer.

    {
        int count = p_button_count();
        if (count <= 0 || count > 3000) {
            lua_pushnil(Ls);
            return 1;
        }
        for (int i = 0; i < count; i++) {
            void* btn = p_button_get(i);
            if (!btn) continue;
            if (IsBadReadPtr(btn, (SIZE_T)(BTN_OFS_ACTION_PTR + sizeof(void*)))) continue;
            if ((uintptr_t)(*(void**)((uint8_t*)btn + BTN_OFS_ACTION_PTR)) != action_ptr) continue;
            nth--;
            if (nth == 0) {
                lua_pushnumber(Ls, (lua_Number)(uintptr_t)btn);
                return 1;
            }
        }
    }

    lua_pushnil(Ls);
    return 1;
}

static int lua_ui_find_button_by_label(lua_State* Ls) {
    const char* label = luaL_checkstring(Ls, 1);
    int nth = (int)luaL_optinteger(Ls, 2, 1);
    const char* state_name = ui_state_name_from_ptr(ui_current_state_ptr());
    if (!label || !label[0] || nth < 1 || !p_button_count || !p_button_get) {
        lua_pushnil(Ls);
        return 1;
    }

    // During main_initial handoff the button list is not always stable yet.
    // We still allow scanning, but we guard all pointer reads (see below).

    int count = p_button_count();
    if (count <= 0 || count > 3000) {
        lua_pushnil(Ls);
        return 1;
    }
    for (int i = 0; i < count; i++) {
        void* btn = p_button_get(i);
        if (!btn) continue;
        if (IsBadReadPtr(btn, (SIZE_T)(BTN_OFS_LABEL_PTR + sizeof(void*)))) continue;
        const char* txt = *(const char**)((uint8_t*)btn + BTN_OFS_LABEL_PTR);
        if (!ui_safe_string_readable(txt, 128)) continue;
        if (_stricmp(txt, label) != 0) continue;
        nth--;
        if (nth == 0) {
            lua_pushnumber(Ls, (lua_Number)(uintptr_t)btn);
            return 1;
        }
    }

    lua_pushnil(Ls);
    return 1;
}

static int lua_ui_button_rect_ptr(lua_State* Ls) {
    void* btn = ui_lua_ptr_to_button(Ls, 1);
    if (!btn) {
        lua_pushnil(Ls);
        return 1;
    }

    lua_pushnumber(Ls, *(float*)((uint8_t*)btn + BTN_OFS_CENTER_X));
    lua_pushnumber(Ls, *(float*)((uint8_t*)btn + BTN_OFS_CENTER_Y));
    lua_pushnumber(Ls, *(float*)((uint8_t*)btn + 0x20));
    lua_pushnumber(Ls, *(float*)((uint8_t*)btn + 0x24));
    return 4;
}

static int lua_ui_button_set_pos_ptr(lua_State* Ls) {
    void* btn = ui_lua_ptr_to_button(Ls, 1);
    float x = (float)luaL_checknumber(Ls, 2);
    float y = (float)luaL_checknumber(Ls, 3);
    if (!btn) { lua_pushboolean(Ls, 0); return 1; }
    *(float*)((uint8_t*)btn + BTN_OFS_CENTER_X) = x;
    *(float*)((uint8_t*)btn + BTN_OFS_CENTER_Y) = y;
    lua_pushboolean(Ls, 1);
    return 1;
}
static int lua_ui_button_set_label_ptr(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    void* btn = ui_lua_ptr_to_button(Ls, 1);
    size_t len = 0;
    const char* label = luaL_checklstring(Ls, 2, &len);

    if (!mod || !btn || !label) { lua_pushboolean(Ls, 0); return 1; }
    if (IsBadWritePtr((uint8_t*)btn + BTN_OFS_LABEL_PTR, (SIZE_T)sizeof(void*))) {
        lua_pushboolean(Ls, 0);
        return 1;
    }

    // Avoid churn/leaks when mods call this repeatedly with the same label.
    const char* cur = NULL;
    if (!IsBadReadPtr((uint8_t*)btn + BTN_OFS_LABEL_PTR, (SIZE_T)sizeof(void*))) {
        cur = *(const char**)((uint8_t*)btn + BTN_OFS_LABEL_PTR);
    }
    if (cur && ui_safe_string_readable(cur, 256)) {
        // strcmp is safe here because both strings are NUL-terminated.
        if (strncmp(cur, label, len) == 0 && cur[len] == '\0') {
            lua_pushboolean(Ls, 1);
            return 1;
        }
    }

    char* owned = mod_ui_pool_strdup(mod, label, len);
    if (!owned) { lua_pushboolean(Ls, 0); return 1; }

    *(const char**)((uint8_t*)btn + BTN_OFS_LABEL_PTR) = owned;
    lua_pushboolean(Ls, 1);
    return 1;
}


static int lua_ui_button_invoke_ptr(lua_State* Ls) {
    void* btn = ui_lua_ptr_to_button(Ls, 1);
    int event_code = (int)luaL_optinteger(Ls, 2, 3);

    // Mirror the game's click logic: if an action runs without resetting the button system,
    // and the button has a non-null link target, activation will transition states.
    // (See do_click_ex in ghidra: action return != 0 + link_ptr != NULL => state_switch(link_ptr))
    int reset_before = (p_btn_reset_counter && !IsBadReadPtr((void*)p_btn_reset_counter, sizeof(int)))
                           ? *p_btn_reset_counter
                           : 0;

    if (!btn) {
        lua_pushnil(Ls);
        lua_pushstring(Ls, "invalid button pointer");
        return 2;
    }

    if (IsBadReadPtr((uint8_t*)btn + BTN_OFS_ACTION_PTR, (SIZE_T)sizeof(void*))) {
        lua_pushnil(Ls);
        lua_pushstring(Ls, "cannot read action pointer");
        return 2;
    }

    void* fn_raw = *(void**)((uint8_t*)btn + BTN_OFS_ACTION_PTR);

    // In the base game, buttons are allowed to have no action (NULL) and still act as links.
    int ret = 1;

    if (fn_raw) {
        if (IsBadCodePtr((FARPROC)fn_raw)) {
            lua_pushnil(Ls);
            lua_pushstring(Ls, "invalid action pointer");
            return 2;
        }

        // Action signature in Eggnogg+: int __cdecl action(void* btn, int event_code)
        int (__cdecl *fn)(void*, int) = (int (__cdecl *)(void*, int))(intptr_t)fn_raw;
        ret = fn(btn, event_code);
    }

    // If activation happened and the action returned nonzero, follow link target (if any),
    // but only if the action did not reset the button system.
    if (event_code == 3 && ret != 0) {
        int reset_after = (p_btn_reset_counter && !IsBadReadPtr((void*)p_btn_reset_counter, sizeof(int)))
                              ? *p_btn_reset_counter
                              : reset_before;

        if (reset_after != reset_before) {
            // Match do_click_ex semantics: if the button system was reset, don't state_switch here.
            ret = -1000;
        } else {
            void* link_ptr = NULL;
            if (!IsBadReadPtr((uint8_t*)btn + BTN_OFS_LINK_PTR, sizeof(void*))) {
                link_ptr = *(void**)((uint8_t*)btn + BTN_OFS_LINK_PTR);
            }

            uint8_t nolink = 0;
            if (!IsBadReadPtr((uint8_t*)btn + BTN_OFS_NOLINK_FLAG, sizeof(uint8_t))) {
                nolink = *(uint8_t*)((uint8_t*)btn + BTN_OFS_NOLINK_FLAG);
            }

            if (link_ptr && !nolink && p_state_switch && !IsBadCodePtr((FARPROC)(void*)p_state_switch)) {
                p_state_switch(link_ptr);
                ret = -1000;
            }
        }
    }

    lua_pushinteger(Ls, ret);
    return 1;
}


static int lua_ui_button_activate_ptr(lua_State* Ls) {
    void* btn = ui_lua_ptr_to_button(Ls, 1);
    int event_code = (int)luaL_optinteger(Ls, 2, 3);

    if (!btn) {
        lua_pushnil(Ls);
        lua_pushstring(Ls, "invalid button pointer");
        return 2;
    }

    // Prefer the game's framed dispatcher: this is what real menu clicks go through.
    if (p_main_btn_framed) {
        int ret = p_main_btn_framed((int)(intptr_t)btn, event_code);
        lua_pushinteger(Ls, ret);
        return 1;
    }

    // Fallback: older builds may not have main_btn_framed resolved.
    if (p_btn_player_filter) {
        int ret = p_btn_player_filter(btn, event_code);
        lua_pushinteger(Ls, ret);
        return 1;
    }

    lua_pushnil(Ls);
    lua_pushstring(Ls, "no button dispatcher available");
    return 2;
}

// =============================
// Gameplay API
// =============================

#define GAME_CMD_JUMP   0x01u
#define GAME_CMD_ATTACK 0x02u
#define GAME_CMD_RIGHT  0x04u
#define GAME_CMD_LEFT   0x08u
#define GAME_CMD_UP     0x10u
#define GAME_CMD_DOWN   0x20u
#define GAME_CMD_MENU   0x40u

static uint32_t lua_game_mask_from_value(lua_State* Ls, int arg_index, int* ok) {
    uint32_t mask = 0;
    if (ok) *ok = 0;

    if (lua_isnumber(Ls, arg_index)) {
        if (ok) *ok = 1;
        return (uint32_t)lua_tointeger(Ls, arg_index);
    }

    if (!lua_istable(Ls, arg_index)) {
        return 0;
    }

    struct { const char* key; uint32_t bit; } fields[] = {
        {"jump", GAME_CMD_JUMP},
        {"attack", GAME_CMD_ATTACK},
        {"right", GAME_CMD_RIGHT},
        {"left", GAME_CMD_LEFT},
        {"up", GAME_CMD_UP},
        {"down", GAME_CMD_DOWN},
        {"menu", GAME_CMD_MENU},
    };

    for (int i = 0; i < (int)(sizeof(fields) / sizeof(fields[0])); i++) {
        lua_getfield(Ls, arg_index, fields[i].key);
        if (lua_toboolean(Ls, -1)) mask |= fields[i].bit;
        lua_pop(Ls, 1);
    }

    lua_getfield(Ls, arg_index, "mask");
    if (lua_isnumber(Ls, -1)) mask |= (uint32_t)lua_tointeger(Ls, -1);
    lua_pop(Ls, 1);

    if (ok) *ok = 1;
    return mask;
}

static void lua_game_push_command_constants(lua_State* Ls) {
    lua_pushinteger(Ls, (lua_Integer)GAME_CMD_JUMP);   lua_setfield(Ls, -2, "CMD_JUMP");
    lua_pushinteger(Ls, (lua_Integer)GAME_CMD_ATTACK); lua_setfield(Ls, -2, "CMD_ATTACK");
    lua_pushinteger(Ls, (lua_Integer)GAME_CMD_RIGHT);  lua_setfield(Ls, -2, "CMD_RIGHT");
    lua_pushinteger(Ls, (lua_Integer)GAME_CMD_LEFT);   lua_setfield(Ls, -2, "CMD_LEFT");
    lua_pushinteger(Ls, (lua_Integer)GAME_CMD_UP);     lua_setfield(Ls, -2, "CMD_UP");
    lua_pushinteger(Ls, (lua_Integer)GAME_CMD_DOWN);   lua_setfield(Ls, -2, "CMD_DOWN");
    lua_pushinteger(Ls, (lua_Integer)GAME_CMD_MENU);   lua_setfield(Ls, -2, "CMD_MENU");
}

static int lua_absindex_compat(lua_State* Ls, int idx);

static int hex_nibble(int ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static void lua_push_hex_field(lua_State* Ls, const char* key, const uint8_t* data, size_t len) {
    static const char HEX[] = "0123456789abcdef";
    char* out;
    size_t i;

    if (!key || !data || len == 0) return;
    out = (char*)malloc(len * 2 + 1);
    if (!out) return;

    for (i = 0; i < len; i++) {
        out[i * 2] = HEX[(data[i] >> 4) & 0x0f];
        out[i * 2 + 1] = HEX[data[i] & 0x0f];
    }
    out[len * 2] = '\0';

    lua_pushlstring(Ls, out, len * 2);
    lua_setfield(Ls, -2, key);
    free(out);
}

static int lua_table_copy_hex_field(lua_State* Ls, int idx, const char* key, uint8_t* out, size_t len) {
    size_t hex_len = 0;
    const char* hex = NULL;
    size_t i;

    if (!key || !out || len == 0) return 0;

    idx = lua_absindex_compat(Ls, idx);
    lua_getfield(Ls, idx, key);
    if (!lua_isstring(Ls, -1)) {
        lua_pop(Ls, 1);
        return 0;
    }

    hex = lua_tolstring(Ls, -1, &hex_len);
    if (!hex || hex_len != len * 2) {
        lua_pop(Ls, 1);
        return 0;
    }

    for (i = 0; i < len; i++) {
        int hi = hex_nibble((unsigned char)hex[i * 2]);
        int lo = hex_nibble((unsigned char)hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            lua_pop(Ls, 1);
            return 0;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }

    lua_pop(Ls, 1);
    return 1;
}

static void push_player_snapshot_table(lua_State* Ls, uintptr_t player_ptr, int player_index, double origin_x, double origin_y) {
    if (!player_ptr || !ptr_readable((const void*)player_ptr, PLAYER_SIZE)) {
        lua_pushnil(Ls);
        return;
    }

    {
        float x = *(float*)(player_ptr + PLAYER_OFS_X);
        float y = *(float*)(player_ptr + PLAYER_OFS_Y);
        float prev_x = *(float*)(player_ptr + PLAYER_OFS_PREV_X);
        float prev_y = *(float*)(player_ptr + PLAYER_OFS_PREV_Y);
        float vx = *(float*)(player_ptr + PLAYER_OFS_VX);
        float vy = *(float*)(player_ptr + PLAYER_OFS_VY);
        uint8_t thing_slot = *(uint8_t*)(player_ptr + PLAYER_OFS_THING_SLOT);
        int sprite_index = *(int*)(player_ptr + PLAYER_OFS_SPRITE_INDEX);
        int has_sword = (*(uint8_t*)(player_ptr + PLAYER_OFS_HAS_SWORD) == 0) ? 1 : 0;
        int facing = (int)*(signed char*)(player_ptr + PLAYER_OFS_FACING_SIGN);
        int room_index = (int)*(signed char*)(player_ptr + PLAYER_OFS_ROOM);
        uint8_t cmd_bits = *(uint8_t*)(player_ptr + PLAYER_OFS_CMD_BITS);
        uint8_t prev_cmd_bits = *(uint8_t*)(player_ptr + PLAYER_OFS_PREV_CMD_BITS);
        uint8_t collision_flags = *(uint8_t*)(player_ptr + PLAYER_OFS_COLLISION_FLAGS);
        uint8_t prev_collision_flags = *(uint8_t*)(player_ptr + PLAYER_OFS_PREV_COLLISION);
        uint32_t event_flags = *(uint32_t*)(player_ptr + PLAYER_OFS_EVENT_FLAGS);
        uint32_t prev_event_flags = *(uint32_t*)(player_ptr + PLAYER_OFS_PREV_EVENT_FLAGS);
        uint32_t pending_event_flags = *(uint32_t*)(player_ptr + PLAYER_OFS_PENDING_EVENT_FLAGS);
        uint8_t state_id = *(uint8_t*)(player_ptr + PLAYER_OFS_STATE_ID);
        uint32_t state_timer = *(uint32_t*)(player_ptr + PLAYER_OFS_STATE_TIMER);
        uint8_t jump_buffer = *(uint8_t*)(player_ptr + PLAYER_OFS_JUMP_BUFFER);
        uint8_t attack_buffer = *(uint8_t*)(player_ptr + PLAYER_OFS_ATTACK_BUFFER);
        float anim_phase = *(float*)(player_ptr + PLAYER_OFS_ANIM_PHASE);
        float skin_rgba[4];
        float clothing_rgba[4];

        memcpy(skin_rgba, (const void*)(player_ptr + PLAYER_OFS_RENDER_RGBA), sizeof(skin_rgba));
        memcpy(clothing_rgba, (const void*)(player_ptr + PLAYER_OFS_RENDER_RGBA + sizeof(skin_rgba)), sizeof(clothing_rgba));

        lua_newtable(Ls);
        lua_push_field_int(Ls, "index", player_index);
        lua_push_field_int(Ls, "slot", (int)thing_slot);
        lua_push_field_int(Ls, "thing_slot", (int)thing_slot);
        lua_push_field_number(Ls, "x", x);
        lua_push_field_number(Ls, "y", y);
        lua_push_field_number(Ls, "prev_x", prev_x);
        lua_push_field_number(Ls, "prev_y", prev_y);
        lua_push_field_number(Ls, "vx", vx);
        lua_push_field_number(Ls, "vy", vy);
        lua_push_field_number(Ls, "dx", x - origin_x);
        lua_push_field_number(Ls, "dy", y - origin_y);
        lua_push_field_int(Ls, "sprite_index", sprite_index);
        lua_push_field_int(Ls, "sprite_frame", sprite_index);
        lua_push_field_number(Ls, "anim_phase", anim_phase);
        lua_push_field_bool(Ls, "has_sword", has_sword);
        lua_push_field_int(Ls, "facing", facing);
        lua_push_field_int(Ls, "room_index", room_index);
        lua_push_field_int(Ls, "state_id", state_id);
        lua_push_field_int(Ls, "state_timer", (int)state_timer);
        lua_push_field_int(Ls, "cmd_bits", (int)cmd_bits);
        lua_push_field_int(Ls, "prev_cmd_bits", (int)prev_cmd_bits);
        lua_push_field_int(Ls, "jump_buffer", (int)jump_buffer);
        lua_push_field_int(Ls, "attack_buffer", (int)attack_buffer);
        lua_push_field_int(Ls, "collision_flags", (int)collision_flags);
        lua_push_field_int(Ls, "prev_collision_flags", (int)prev_collision_flags);
        lua_push_field_int(Ls, "event_flags", (int)event_flags);
        lua_push_field_int(Ls, "prev_event_flags", (int)prev_event_flags);
        lua_push_field_int(Ls, "pending_event_flags", (int)pending_event_flags);
        lua_push_rgba_field(Ls, "skin_rgba", skin_rgba);
        lua_push_rgba_field(Ls, "clothing_rgba", clothing_rgba);
        lua_push_hex_field(Ls, "action_blob", (const uint8_t*)(player_ptr + PLAYER_OFS_ACTION_BLOB), PLAYER_ACTION_BLOB_LEN);
        lua_push_hex_field(Ls, "state_blob", (const uint8_t*)(player_ptr + PLAYER_OFS_STATE_BLOB), PLAYER_STATE_BLOB_LEN);
        lua_push_field_number(Ls, "anim_ptr", (lua_Number)(double)(uint32_t)(uintptr_t)(*(void**)(player_ptr + PLAYER_OFS_ANIM_PTR)));
        lua_push_field_bool(Ls, "grounded", (collision_flags & PLAYER_COLLIDE_GROUNDED) != 0);
        lua_push_field_bool(Ls, "ceiling", (collision_flags & PLAYER_COLLIDE_CEILING) != 0);
        lua_push_field_bool(Ls, "wall_right", (collision_flags & PLAYER_COLLIDE_WALL_RIGHT) != 0);
        lua_push_field_bool(Ls, "wall_left", (collision_flags & PLAYER_COLLIDE_WALL_LEFT) != 0);
    }
}

static void push_thing_snapshot_table(lua_State* Ls, const uint8_t* thing_ptr, int slot_index, double origin_x, double origin_y) {
    float x = *(float*)(thing_ptr + THING_OFS_X);
    float y = *(float*)(thing_ptr + THING_OFS_Y);
    float prev_x = *(float*)(thing_ptr + THING_OFS_PREV_X);
    float prev_y = *(float*)(thing_ptr + THING_OFS_PREV_Y);
    float vx = *(float*)(thing_ptr + THING_OFS_VX);
    float vy = *(float*)(thing_ptr + THING_OFS_VY);
    int room_index = *(int*)(thing_ptr + THING_OFS_ROOM);
    uint8_t state_id = *(uint8_t*)(thing_ptr + THING_OFS_STATE_ID);
    uint8_t flags = *(uint8_t*)(thing_ptr + THING_OFS_FLAGS);
    int type = (int)thing_ptr[THING_OFS_TYPE];

    lua_newtable(Ls);
    lua_push_field_int(Ls, "slot", slot_index);
    lua_push_field_int(Ls, "type", type);
    lua_push_field_number(Ls, "x", x);
    lua_push_field_number(Ls, "y", y);
    lua_push_field_number(Ls, "prev_x", prev_x);
    lua_push_field_number(Ls, "prev_y", prev_y);
    lua_push_field_number(Ls, "vx", vx);
    lua_push_field_number(Ls, "vy", vy);
    lua_push_field_number(Ls, "dx", x - origin_x);
    lua_push_field_number(Ls, "dy", y - origin_y);
    lua_push_field_int(Ls, "room_index", room_index);
    lua_push_field_int(Ls, "state_id", (int)state_id);
    lua_push_field_int(Ls, "flags", (int)flags);
    if (type == THING_TYPE_SWORD) {
        lua_push_hex_field(Ls, "head_blob", (const uint8_t*)(thing_ptr + THING_OFS_HEAD_BLOB), THING_HEAD_BLOB_LEN);
        lua_push_hex_field(Ls, "motion_blob", (const uint8_t*)(thing_ptr + THING_OFS_MOTION_BLOB), THING_MOTION_BLOB_LEN);
        lua_push_hex_field(Ls, "action_blob", (const uint8_t*)(thing_ptr + THING_OFS_ACTION_BLOB), THING_ACTION_BLOB_LEN);
        lua_push_hex_field(Ls, "state_blob", (const uint8_t*)(thing_ptr + THING_OFS_STATE_BLOB), THING_STATE_BLOB_LEN);
        lua_push_hex_field(Ls, "tail_blob", (const uint8_t*)(thing_ptr + THING_OFS_TAIL_BLOB), THING_TAIL_BLOB_LEN);
    }
    lua_push_field_bool(Ls, "active", thing_ptr[THING_OFS_ACTIVE] != 0);
}

static void push_sword_state_table(lua_State* Ls, const uint8_t* thing_ptr, int slot_index) {
    lua_newtable(Ls);
    lua_push_field_int(Ls, "slot", slot_index);
    lua_push_field_number(Ls, "x", *(float*)(thing_ptr + THING_OFS_X));
    lua_push_field_number(Ls, "y", *(float*)(thing_ptr + THING_OFS_Y));
    lua_push_field_number(Ls, "prev_x", *(float*)(thing_ptr + THING_OFS_PREV_X));
    lua_push_field_number(Ls, "prev_y", *(float*)(thing_ptr + THING_OFS_PREV_Y));
    lua_push_field_number(Ls, "vx", *(float*)(thing_ptr + THING_OFS_VX));
    lua_push_field_number(Ls, "vy", *(float*)(thing_ptr + THING_OFS_VY));
    lua_push_field_int(Ls, "room_index", *(int*)(thing_ptr + THING_OFS_ROOM));
    lua_push_field_int(Ls, "state_id", (int)*(uint8_t*)(thing_ptr + THING_OFS_STATE_ID));
    lua_push_field_int(Ls, "flags", (int)*(uint8_t*)(thing_ptr + THING_OFS_FLAGS));
    lua_push_hex_field(Ls, "head_blob", (const uint8_t*)(thing_ptr + THING_OFS_HEAD_BLOB), THING_HEAD_BLOB_LEN);
    lua_push_hex_field(Ls, "motion_blob", (const uint8_t*)(thing_ptr + THING_OFS_MOTION_BLOB), THING_MOTION_BLOB_LEN);
    lua_push_hex_field(Ls, "action_blob", (const uint8_t*)(thing_ptr + THING_OFS_ACTION_BLOB), THING_ACTION_BLOB_LEN);
    lua_push_hex_field(Ls, "state_blob", (const uint8_t*)(thing_ptr + THING_OFS_STATE_BLOB), THING_STATE_BLOB_LEN);
    lua_push_hex_field(Ls, "tail_blob", (const uint8_t*)(thing_ptr + THING_OFS_TAIL_BLOB), THING_TAIL_BLOB_LEN);
}

static int lua_game_tick_count(lua_State* Ls) {
    lua_pushnumber(Ls, (lua_Number)g_game_tick_count);
    return 1;
}

static uint32_t lua_game_u32_arg(lua_State* Ls, int arg_index) {
    double raw = luaL_checknumber(Ls, arg_index);
    if (raw < 0.0) raw = 0.0;
    if (raw > 4294967295.0) raw = 4294967295.0;
    return (uint32_t)raw;
}

static int lua_game_native_tick(lua_State* Ls) {
    if (ptr_readable((const void*)p_native_game_ticks, sizeof(uint32_t))) {
        lua_pushnumber(Ls, (lua_Number)(double)(*p_native_game_ticks));
    } else {
        lua_pushnil(Ls);
    }
    return 1;
}

static int lua_game_set_native_tick(lua_State* Ls) {
    uint32_t value = lua_game_u32_arg(Ls, 1);
    if (!ptr_writable((void*)p_native_game_ticks, sizeof(uint32_t))) {
        lua_pushboolean(Ls, 0);
        return 1;
    }
    *p_native_game_ticks = value;
    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_game_rng_seed(lua_State* Ls) {
    if (ptr_readable((const void*)p_mrand_seed, sizeof(uint32_t))) {
        lua_pushnumber(Ls, (lua_Number)(double)(*p_mrand_seed));
    } else {
        lua_pushnil(Ls);
    }
    return 1;
}

static int lua_game_set_rng_seed(lua_State* Ls) {
    uint32_t value = lua_game_u32_arg(Ls, 1);
    if (!ptr_writable((void*)p_mrand_seed, sizeof(uint32_t))) {
        lua_pushboolean(Ls, 0);
        return 1;
    }
    *p_mrand_seed = value;
    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_game_native_state(lua_State* Ls) {
    lua_newtable(Ls);
    if (ptr_readable((const void*)p_native_game_ticks, sizeof(uint32_t))) {
        lua_push_field_number(Ls, "game_ticks", (lua_Number)(double)(*p_native_game_ticks));
    } else {
        lua_pushnil(Ls);
        lua_setfield(Ls, -2, "game_ticks");
    }
    if (ptr_readable((const void*)p_mrand_seed, sizeof(uint32_t))) {
        lua_push_field_number(Ls, "rng_seed", (lua_Number)(double)(*p_mrand_seed));
    } else {
        lua_pushnil(Ls);
        lua_setfield(Ls, -2, "rng_seed");
    }
    if (ptr_readable((const void*)p_game_level, sizeof(uint32_t))) {
        lua_push_field_number(Ls, "game_level", (lua_Number)(double)(*p_game_level));
    } else {
        lua_pushnil(Ls);
        lua_setfield(Ls, -2, "game_level");
    }
    return 1;
}

static int lua_game_player_colour(lua_State* Ls) {
    int player_index = (int)luaL_optinteger(Ls, 1, 0);
    int clothing = (int)luaL_optinteger(Ls, 2, 0);
    float rgba[4] = {1.0f, 1.0f, 1.0f, 1.0f};

    if (!p_game_player_colour || IsBadCodePtr((FARPROC)(void*)p_game_player_colour)) {
        lua_pushnil(Ls);
        lua_pushstring(Ls, "game_player_colour unavailable");
        return 2;
    }

    p_game_player_colour(rgba, (uint32_t)(player_index & 1), clothing ? 1 : 0);
    lua_push_rgba_table(Ls, rgba);
    return 1;
}

static int lua_game_player_colour_index(lua_State* Ls) {
    int player_index = (int)luaL_optinteger(Ls, 1, 0);
    int clothing = (int)luaL_optinteger(Ls, 2, 0);
    lua_pushinteger(Ls, hooks_player_colour_index(player_index, clothing ? 1 : 0));
    return 1;
}

static int lua_game_set_player_colour_index(lua_State* Ls) {
    int player_index = (int)luaL_checkinteger(Ls, 1);
    int clothing = (int)luaL_checkinteger(Ls, 2);
    int colour_index = (int)luaL_checkinteger(Ls, 3);
    lua_pushinteger(Ls, hooks_set_player_colour_index(player_index, clothing ? 1 : 0, colour_index));
    return 1;
}

static int lua_read_rgba_arg(lua_State* Ls, int idx, float out[4]) {
    int any = 0;
    idx = lua_absindex_compat(Ls, idx);
    if (!lua_istable(Ls, idx) || !out) return 0;

    for (int i = 0; i < 4; i++) {
        lua_rawgeti(Ls, idx, i + 1);
        if (lua_isnumber(Ls, -1)) {
            float v = (float)lua_tonumber(Ls, -1);
            if (v < 0.0f) v = 0.0f;
            if (v > 1.0f) v = 1.0f;
            out[i] = v;
            any = 1;
        }
        lua_pop(Ls, 1);
    }

    const char* keys[4] = {"r", "g", "b", "a"};
    for (int i = 0; i < 4; i++) {
        lua_getfield(Ls, idx, keys[i]);
        if (lua_isnumber(Ls, -1)) {
            float v = (float)lua_tonumber(Ls, -1);
            if (v < 0.0f) v = 0.0f;
            if (v > 1.0f) v = 1.0f;
            out[i] = v;
            any = 1;
        }
        lua_pop(Ls, 1);
    }
    return any;
}

static int lua_game_set_player_render_colours(lua_State* Ls) {
    int player_index = (int)luaL_checkinteger(Ls, 1) & 1;
    uintptr_t player_ptr = game_get_player_ptr(player_index);
    float skin[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float clothing[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    uint8_t thing_slot = 0;

    if (!player_ptr || !ptr_writable((void*)player_ptr, PLAYER_SIZE)) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, "player pointer unavailable");
        return 2;
    }

    memcpy(skin, (const void*)(player_ptr + PLAYER_OFS_RENDER_RGBA), sizeof(skin));
    memcpy(clothing, (const void*)(player_ptr + PLAYER_OFS_RENDER_RGBA + sizeof(skin)), sizeof(clothing));
    (void)lua_read_rgba_arg(Ls, 2, skin);
    (void)lua_read_rgba_arg(Ls, 3, clothing);
    memcpy((void*)(player_ptr + PLAYER_OFS_RENDER_RGBA), skin, sizeof(skin));
    memcpy((void*)(player_ptr + PLAYER_OFS_RENDER_RGBA + sizeof(skin)), clothing, sizeof(clothing));

    thing_slot = *(uint8_t*)(player_ptr + PLAYER_OFS_THING_SLOT);
    if (p_things && thing_slot < 128u) {
        uint8_t* thing = p_things + ((size_t)thing_slot * THING_SIZE);
        if ((uintptr_t)thing != player_ptr &&
            ptr_writable(thing, THING_SIZE) &&
            thing[THING_OFS_TYPE] == THING_TYPE_PLAYER) {
            memcpy(thing + PLAYER_OFS_RENDER_RGBA, skin, sizeof(skin));
            memcpy(thing + PLAYER_OFS_RENDER_RGBA + sizeof(skin), clothing, sizeof(clothing));
        }
    }

    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_game_set_player_body_hidden(lua_State* Ls) {
    int player_index = (int)luaL_checkinteger(Ls, 1) & 1;
    int hidden = lua_toboolean(Ls, 2) ? 1 : 0;
    hooks_set_player_body_hidden(player_index, hidden);
    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_game_player_body_hidden(lua_State* Ls) {
    int player_index = (int)luaL_optinteger(Ls, 1, 0) & 1;
    lua_pushboolean(Ls, hooks_player_body_hidden(player_index) != 0);
    return 1;
}

static int lua_game_set_player_sword_idle_offset(lua_State* Ls) {
    int player_index = (int)luaL_checkinteger(Ls, 1) & 1;
    float x = (float)luaL_optnumber(Ls, 2, 0.0);
    float y = (float)luaL_optnumber(Ls, 3, 0.0);
    hooks_set_player_sword_idle_offset(player_index, x, y);
    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_game_state_checksum(lua_State* Ls) {
    uint32_t crc = 0;
    char err[128];

    if (!lua_manager_game_state_checksum(&crc, err, sizeof(err))) {
        lua_pushnil(Ls);
        lua_pushstring(Ls, err);
        return 2;
    }
    lua_pushnumber(Ls, (lua_Number)(double)crc);
    return 1;
}

static int lua_game_full_state_size(lua_State* Ls) {
    size_t blob_len = lua_manager_game_state_size();
    lua_pushnumber(Ls, (lua_Number)(double)blob_len);
    return 1;
}

static int lua_game_full_state_blob(lua_State* Ls) {
    size_t blob_len = lua_manager_game_state_size();
    uint8_t* blob = NULL;
    char err[128];

    if (blob_len == 0) {
        lua_pushnil(Ls);
        lua_pushstring(Ls, "game state unavailable");
        return 2;
    }

    blob = (uint8_t*)malloc(blob_len);
    if (!blob) {
        lua_pushnil(Ls);
        lua_pushstring(Ls, "out of memory");
        return 2;
    }
    if (!lua_manager_game_state_save(blob, blob_len, &blob_len, err, sizeof(err))) {
        free(blob);
        lua_pushnil(Ls);
        lua_pushstring(Ls, err);
        return 2;
    }

    lua_pushlstring(Ls, (const char*)blob, blob_len);
    free(blob);
    return 1;
}

static int lua_game_apply_full_state_blob(lua_State* Ls) {
    size_t blob_len = 0;
    const char* blob = luaL_checklstring(Ls, 1, &blob_len);
    char err[128];

    if (!lua_manager_game_state_load(blob, blob_len, err, sizeof(err))) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, err);
        return 2;
    }

    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_game_block_next_tick(lua_State* Ls) {
    int block = lua_isnoneornil(Ls, 1) ? 1 : (lua_toboolean(Ls, 1) ? 1 : 0);
    hooks_block_next_game_tick(block);
    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_game_simulate_ticks(lua_State* Ls) {
    int count = (int)luaL_optinteger(Ls, 1, 1);
    int arg0 = (int)luaL_optinteger(Ls, 2, 0);
    int ran = 0;

    if (count < 0) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, "count must be >= 0");
        return 2;
    }
    if (count > 600) {
        count = 600;
    }

    ran = hooks_simulate_game_ticks(count, arg0);
    if (ran < 0) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, "game tick simulation unavailable");
        return 2;
    }

    lua_pushboolean(Ls, (ran == count) ? 1 : 0);
    lua_pushinteger(Ls, (lua_Integer)ran);
    return 2;
}

static int lua_game_poll_cmds(lua_State* Ls) {
    int player_index = (int)luaL_optinteger(Ls, 1, 0);
    int mode = (int)luaL_optinteger(Ls, 2, 1);
    uint32_t cmd = hooks_peek_player_cmds_effective(player_index, mode);
    lua_pushinteger(Ls, (lua_Integer)cmd);
    return 1;
}

static int lua_game_poll_cmds_raw(lua_State* Ls) {
    int player_index = (int)luaL_optinteger(Ls, 1, 0);
    int mode = (int)luaL_optinteger(Ls, 2, 1);
    uint32_t cmd = hooks_peek_player_cmds_raw(player_index, mode);
    lua_pushinteger(Ls, (lua_Integer)cmd);
    return 1;
}

static int lua_game_set_input(lua_State* Ls) {
    int ok = 0;
    int player_index = (int)luaL_checkinteger(Ls, 1);
    uint32_t cmd_mask = lua_game_mask_from_value(Ls, 2, &ok);
    int ticks = (int)luaL_optinteger(Ls, 3, 1);
    int replace = lua_isnoneornil(Ls, 4) ? 1 : (lua_toboolean(Ls, 4) ? 1 : 0);

    if (!ok) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, "expected integer mask or input table");
        return 2;
    }

    hooks_set_tick_input(player_index, cmd_mask, ticks, replace);
    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_game_input_override(lua_State* Ls) {
    int ok = 0;
    int player_index = (int)luaL_checkinteger(Ls, 1);
    uint32_t cmd_mask = lua_game_mask_from_value(Ls, 2, &ok);
    int frames = (int)luaL_optinteger(Ls, 3, 1);
    int replace = lua_toboolean(Ls, 4) ? 1 : 0;

    if (!ok) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, "expected integer mask or input table");
        return 2;
    }

    hooks_set_input_override(player_index, cmd_mask, frames, replace);
    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_game_input_clear(lua_State* Ls) {
    int player_index = (int)luaL_checkinteger(Ls, 1);
    hooks_clear_tick_input(player_index);
    hooks_clear_input_override(player_index);
    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_game_input_status(lua_State* Ls) {
    int player_index = (int)luaL_optinteger(Ls, 1, 0);
    uint32_t tick_mask = 0;
    int tick_ticks = 0;
    int tick_replace = 0;
    uint32_t poll_mask = 0;
    int poll_frames = 0;
    int poll_replace = 0;
    int tick_active = hooks_get_tick_input(player_index, &tick_mask, &tick_ticks, &tick_replace);
    int poll_active = hooks_get_input_override(player_index, &poll_mask, &poll_frames, &poll_replace);

    lua_newtable(Ls);
    lua_push_field_bool(Ls, "active", tick_active || poll_active);
    lua_push_field_bool(Ls, "tick_active", tick_active);
    lua_push_field_int(Ls, "tick_mask", (int)tick_mask);
    lua_push_field_int(Ls, "tick_ticks", tick_ticks);
    lua_push_field_bool(Ls, "tick_replace", tick_replace);
    lua_push_field_bool(Ls, "poll_active", poll_active);
    lua_push_field_int(Ls, "poll_mask", (int)poll_mask);
    lua_push_field_int(Ls, "poll_frames", poll_frames);
    lua_push_field_bool(Ls, "poll_replace", poll_replace);
    lua_push_field_bool(Ls, "raw_blocked", hooks_get_raw_input_blocked(player_index));
    lua_push_field_int(Ls, "effective_now", (int)hooks_peek_player_cmds_effective(player_index, 1));
    lua_push_field_int(Ls, "raw_now", (int)hooks_peek_player_cmds_raw(player_index, 1));
    return 1;
}

static int lua_game_block_raw_input(lua_State* Ls) {
    int player_index = (int)luaL_checkinteger(Ls, 1);
    int blocked = lua_toboolean(Ls, 2) ? 1 : 0;
    hooks_set_raw_input_blocked(player_index, blocked);
    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_game_entities(lua_State* Ls) {
    int current_room_only = lua_isnoneornil(Ls, 1) ? 1 : (lua_toboolean(Ls, 1) ? 1 : 0);
    int include_players = lua_toboolean(Ls, 2) ? 1 : 0;
    int room_index = game_get_active_room_index();
    uintptr_t p0 = game_get_player_ptr(0);
    double origin_x = 0.0;
    double origin_y = 0.0;
    if (p0 && ptr_readable((const void*)p0, PLAYER_OFS_Y + sizeof(float))) {
        origin_x = *(float*)(p0 + PLAYER_OFS_X);
        origin_y = *(float*)(p0 + PLAYER_OFS_Y);
    }

    lua_newtable(Ls);
    {
        int out_i = 1;
        int thing_count = game_get_thing_count();
        for (int i = 0; i < thing_count; i++) {
            const uint8_t* t = p_things + (i * THING_SIZE);
            if (!ptr_readable((const void*)t, THING_SIZE)) continue;
            if (t[THING_OFS_ACTIVE] == 0) continue;
            if (!include_players && t[THING_OFS_TYPE] == THING_TYPE_PLAYER) continue;
            if (current_room_only) {
                int thing_room = *(int*)(t + THING_OFS_ROOM);
                if (thing_room != room_index) continue;
            }
            push_thing_snapshot_table(Ls, t, i, origin_x, origin_y);
            lua_rawseti(Ls, -2, out_i++);
        }
    }
    return 1;
}

static int lua_game_sword_snapshot(lua_State* Ls) {
    lua_newtable(Ls);

    for (int player_index = 0; player_index < 2; player_index++) {
        uintptr_t player_ptr = game_get_player_ptr(player_index);
        char key[24];
        snprintf(key, sizeof(key), "p%d_has_sword", player_index);
        if (player_ptr && ptr_readable((const void*)player_ptr, PLAYER_SIZE)) {
            lua_push_field_bool(Ls, key, (*(uint8_t*)(player_ptr + PLAYER_OFS_HAS_SWORD) == 0) ? 1 : 0);
        } else {
            lua_pushnil(Ls);
            lua_setfield(Ls, -2, key);
        }
    }

    lua_newtable(Ls);
    {
        int out_i = 1;
        int thing_count = game_get_thing_count();
        for (int i = 0; i < thing_count; i++) {
            const uint8_t* t = p_things + (i * THING_SIZE);
            if (!ptr_readable((const void*)t, THING_SIZE)) continue;
            if (t[THING_OFS_ACTIVE] == 0) continue;
            if (t[THING_OFS_TYPE] != THING_TYPE_SWORD) continue;
            push_sword_state_table(Ls, t, i);
            lua_rawseti(Ls, -2, out_i++);
        }
    }
    lua_setfield(Ls, -2, "swords");

    return 1;
}

static int lua_game_snapshot(lua_State* Ls) {
    int player_index = (int)luaL_optinteger(Ls, 1, 0);
    int include_tiles = lua_isnoneornil(Ls, 2) ? 1 : (lua_toboolean(Ls, 2) ? 1 : 0);
    int enemy_index;
    uintptr_t player_ptr;
    uintptr_t enemy_ptr;
    int in_game;

    player_index &= 1;
    enemy_index = (player_index + 1) & 1;
    player_ptr = game_get_player_ptr(player_index);
    enemy_ptr = game_get_player_ptr(enemy_index);
    in_game = ui_state_matches_name(ui_current_state_ptr(), "game");

    lua_newtable(Ls);
    lua_push_field_bool(Ls, "in_game", in_game);
    lua_push_field_int(Ls, "player_index", player_index);
    lua_push_field_int(Ls, "enemy_index", enemy_index);
    lua_push_field_number(Ls, "tick", (lua_Number)g_game_tick_count);
    if (ptr_readable((const void*)p_native_game_ticks, sizeof(uint32_t))) {
        lua_push_field_number(Ls, "native_tick", (lua_Number)(double)(*p_native_game_ticks));
    }
    if (ptr_readable((const void*)p_mrand_seed, sizeof(uint32_t))) {
        lua_push_field_number(Ls, "rng_seed", (lua_Number)(double)(*p_mrand_seed));
    }
    if (ptr_readable((const void*)p_game_level, sizeof(uint32_t))) {
        lua_push_field_number(Ls, "game_level", (lua_Number)(double)(*p_game_level));
    }
    {
        int start_countdown = 0;
        int end_countdown = 0;
        int leader_index = -1;
        uintptr_t leader_ptr = 0;
        uintptr_t p0 = game_get_player_ptr(0);
        uintptr_t p1 = game_get_player_ptr(1);

        if (ptr_readable((const void*)p_start_countdown, sizeof(int))) start_countdown = *p_start_countdown;
        if (ptr_readable((const void*)p_end_countdown, sizeof(int))) end_countdown = *p_end_countdown;
        if (ptr_readable((const void*)p_game_leader, sizeof(uintptr_t))) {
            leader_ptr = *p_game_leader;
            if (leader_ptr == p0) leader_index = 0;
            else if (leader_ptr == p1) leader_index = 1;
        }

        lua_push_field_int(Ls, "start_countdown", start_countdown);
        lua_push_field_int(Ls, "end_countdown", end_countdown);
        if (leader_index >= 0) lua_push_field_int(Ls, "leader_index", leader_index);
        else { lua_pushnil(Ls); lua_setfield(Ls, -2, "leader_index"); }
    }

    if (!player_ptr) {
        lua_pushstring(Ls, "player pointer unavailable");
        lua_setfield(Ls, -2, "error");
        return 1;
    }

    {
        float player_x = *(float*)(player_ptr + PLAYER_OFS_X);
        float player_y = *(float*)(player_ptr + PLAYER_OFS_Y);
        int room_index = game_get_active_room_index();
        int room_w = 0;
        int room_h = 0;
        int found_sword = 0;
        double nearest_dx = 0.0;
        double nearest_dy = 0.0;
        double nearest_x = 0.0;
        double nearest_y = 0.0;
        double best_d2 = 0.0;

        push_player_snapshot_table(Ls, player_ptr, player_index, player_x, player_y);
        lua_setfield(Ls, -2, "player");
        push_player_snapshot_table(Ls, enemy_ptr, enemy_index, player_x, player_y);
        lua_setfield(Ls, -2, "enemy");

        lua_push_field_number(Ls, "player_x", *(float*)(player_ptr + PLAYER_OFS_X));
        lua_push_field_number(Ls, "player_y", *(float*)(player_ptr + PLAYER_OFS_Y));
        lua_push_field_number(Ls, "player_vx", *(float*)(player_ptr + PLAYER_OFS_VX));
        lua_push_field_number(Ls, "player_vy", *(float*)(player_ptr + PLAYER_OFS_VY));
        lua_push_field_bool(Ls, "player_has_sword", (*(uint8_t*)(player_ptr + PLAYER_OFS_HAS_SWORD) == 0) ? 1 : 0);
        lua_push_field_int(Ls, "player_facing", (int)*(signed char*)(player_ptr + PLAYER_OFS_FACING_SIGN));
        lua_push_field_bool(Ls, "player_grounded", ((*(uint8_t*)(player_ptr + PLAYER_OFS_COLLISION_FLAGS)) & PLAYER_COLLIDE_GROUNDED) != 0);

        if (enemy_ptr) {
            float enemy_x = *(float*)(enemy_ptr + PLAYER_OFS_X);
            float enemy_y = *(float*)(enemy_ptr + PLAYER_OFS_Y);
            lua_push_field_number(Ls, "enemy_x", enemy_x);
            lua_push_field_number(Ls, "enemy_y", enemy_y);
            lua_push_field_number(Ls, "enemy_dx", enemy_x - player_x);
            lua_push_field_number(Ls, "enemy_dy", enemy_y - player_y);
            lua_push_field_number(Ls, "enemy_vx", *(float*)(enemy_ptr + PLAYER_OFS_VX));
            lua_push_field_number(Ls, "enemy_vy", *(float*)(enemy_ptr + PLAYER_OFS_VY));
            lua_push_field_bool(Ls, "enemy_has_sword", (*(uint8_t*)(enemy_ptr + PLAYER_OFS_HAS_SWORD) == 0) ? 1 : 0);
            lua_push_field_int(Ls, "enemy_facing", (int)*(signed char*)(enemy_ptr + PLAYER_OFS_FACING_SIGN));
            lua_push_field_bool(Ls, "enemy_grounded", ((*(uint8_t*)(enemy_ptr + PLAYER_OFS_COLLISION_FLAGS)) & PLAYER_COLLIDE_GROUNDED) != 0);
        } else {
            lua_pushnil(Ls); lua_setfield(Ls, -2, "enemy_x");
            lua_pushnil(Ls); lua_setfield(Ls, -2, "enemy_y");
            lua_pushnil(Ls); lua_setfield(Ls, -2, "enemy_dx");
            lua_pushnil(Ls); lua_setfield(Ls, -2, "enemy_dy");
            lua_pushnil(Ls); lua_setfield(Ls, -2, "enemy_vx");
            lua_pushnil(Ls); lua_setfield(Ls, -2, "enemy_vy");
            lua_pushnil(Ls); lua_setfield(Ls, -2, "enemy_has_sword");
        }

        game_get_room_dims(&room_w, &room_h);
        lua_push_field_int(Ls, "room_index", room_index);
        lua_push_field_int(Ls, "room_width", room_w);
        lua_push_field_int(Ls, "room_height", room_h);

        {
            int thing_count = game_get_thing_count();
            int out_i = 1;
            lua_newtable(Ls);
            for (int i = 0; i < thing_count; i++) {
                const uint8_t* t = p_things + (i * THING_SIZE);
                if (!ptr_readable((const void*)t, THING_SIZE)) continue;
                if (t[THING_OFS_ACTIVE] == 0) continue;
                if (t[THING_OFS_TYPE] == THING_TYPE_PLAYER) continue;
                if (*(int*)(t + THING_OFS_ROOM) != room_index) continue;
                push_thing_snapshot_table(Ls, t, i, player_x, player_y);
                lua_rawseti(Ls, -2, out_i++);
                if (t[THING_OFS_TYPE] == THING_TYPE_SWORD) {
                    double dx = (double)(*(float*)(t + THING_OFS_X) - player_x);
                    double dy = (double)(*(float*)(t + THING_OFS_Y) - player_y);
                    double d2 = dx * dx + dy * dy;
                    if (!found_sword || d2 < best_d2) {
                        found_sword = 1;
                        best_d2 = d2;
                        nearest_dx = dx;
                        nearest_dy = dy;
                        nearest_x = *(float*)(t + THING_OFS_X);
                        nearest_y = *(float*)(t + THING_OFS_Y);
                    }
                }
            }
            lua_setfield(Ls, -2, "entities");
        }

        if (found_sword) {
            lua_push_field_number(Ls, "nearest_sword_dx", nearest_dx);
            lua_push_field_number(Ls, "nearest_sword_dy", nearest_dy);
            lua_push_field_number(Ls, "nearest_sword_x", nearest_x);
            lua_push_field_number(Ls, "nearest_sword_y", nearest_y);
        } else {
            lua_pushnil(Ls); lua_setfield(Ls, -2, "nearest_sword_dx");
            lua_pushnil(Ls); lua_setfield(Ls, -2, "nearest_sword_dy");
            lua_pushnil(Ls); lua_setfield(Ls, -2, "nearest_sword_x");
            lua_pushnil(Ls); lua_setfield(Ls, -2, "nearest_sword_y");
        }

        if (include_tiles && in_game && room_w > 0 && room_h > 0 && p_map_tile && !IsBadCodePtr((FARPROC)(void*)p_map_tile)) {
            int room_x0 = room_index * room_w;
            lua_newtable(Ls);
            for (int ty = 0; ty < room_h; ty++) {
                lua_newtable(Ls);
                for (int tx = 0; tx < room_w; tx++) {
                    int tile_id = -1;
                    int tile_ptr = p_map_tile(room_x0 + tx, ty);
                    if (tile_ptr != 0 && !IsBadReadPtr((void*)(uintptr_t)tile_ptr, 1)) {
                        tile_id = (int)(*(uint8_t*)(uintptr_t)tile_ptr);
                    }
                    lua_pushinteger(Ls, tile_id);
                    lua_rawseti(Ls, -2, tx + 1);
                }
                lua_rawseti(Ls, -2, ty + 1);
            }
            lua_setfield(Ls, -2, "tiles_of_current_room");
        } else {
            lua_pushnil(Ls);
            lua_setfield(Ls, -2, "tiles_of_current_room");
        }
    }

    return 1;
}

static int lua_absindex_compat(lua_State* Ls, int idx) {
    if (idx > 0 || idx <= LUA_REGISTRYINDEX) return idx;
    return lua_gettop(Ls) + idx + 1;
}

static int lua_table_get_int_field(lua_State* Ls, int idx, const char* key, int def) {
    int v = def;
    idx = lua_absindex_compat(Ls, idx);
    lua_getfield(Ls, idx, key);
    if (lua_isnumber(Ls, -1)) v = (int)lua_tointeger(Ls, -1);
    else if (lua_isboolean(Ls, -1)) v = lua_toboolean(Ls, -1) ? 1 : 0;
    lua_pop(Ls, 1);
    return v;
}

static float lua_table_get_float_field(lua_State* Ls, int idx, const char* key, float def) {
    float v = def;
    idx = lua_absindex_compat(Ls, idx);
    lua_getfield(Ls, idx, key);
    if (lua_isnumber(Ls, -1)) v = (float)lua_tonumber(Ls, -1);
    lua_pop(Ls, 1);
    return v;
}

static uint32_t lua_table_get_u32_field(lua_State* Ls, int idx, const char* key, uint32_t def) {
    lua_Number v = (lua_Number)(double)def;
    idx = lua_absindex_compat(Ls, idx);
    lua_getfield(Ls, idx, key);
    if (lua_isnumber(Ls, -1)) v = lua_tonumber(Ls, -1);
    lua_pop(Ls, 1);
    if (v < 0.0) v = 0.0;
    if (v > 4294967295.0) v = 4294967295.0;
    return (uint32_t)v;
}

static int game_apply_player_table(lua_State* Ls, int idx, int player_index) {
    uintptr_t p = game_get_player_ptr(player_index);
    if (!p || !ptr_writable((void*)p, PLAYER_SIZE)) return 0;
    idx = lua_absindex_compat(Ls, idx);

    *(float*)(p + PLAYER_OFS_X) = lua_table_get_float_field(Ls, idx, "x", *(float*)(p + PLAYER_OFS_X));
    *(float*)(p + PLAYER_OFS_Y) = lua_table_get_float_field(Ls, idx, "y", *(float*)(p + PLAYER_OFS_Y));
    *(float*)(p + PLAYER_OFS_PREV_X) = lua_table_get_float_field(Ls, idx, "prev_x", *(float*)(p + PLAYER_OFS_PREV_X));
    *(float*)(p + PLAYER_OFS_PREV_Y) = lua_table_get_float_field(Ls, idx, "prev_y", *(float*)(p + PLAYER_OFS_PREV_Y));
    *(float*)(p + PLAYER_OFS_VX) = lua_table_get_float_field(Ls, idx, "vx", *(float*)(p + PLAYER_OFS_VX));
    *(float*)(p + PLAYER_OFS_VY) = lua_table_get_float_field(Ls, idx, "vy", *(float*)(p + PLAYER_OFS_VY));
    *(int*)(p + PLAYER_OFS_SPRITE_INDEX) = lua_table_get_int_field(Ls, idx, "sprite_index", *(int*)(p + PLAYER_OFS_SPRITE_INDEX));
    *(float*)(p + PLAYER_OFS_ANIM_PHASE) = lua_table_get_float_field(Ls, idx, "anim_phase", *(float*)(p + PLAYER_OFS_ANIM_PHASE));
    (void)lua_table_copy_hex_field(Ls, idx, "action_blob", (uint8_t*)(p + PLAYER_OFS_ACTION_BLOB), PLAYER_ACTION_BLOB_LEN);
    (void)lua_table_copy_hex_field(Ls, idx, "state_blob", (uint8_t*)(p + PLAYER_OFS_STATE_BLOB), PLAYER_STATE_BLOB_LEN);
    *(uintptr_t*)(p + PLAYER_OFS_ANIM_PTR) = (uintptr_t)lua_table_get_u32_field(
        Ls, idx, "anim_ptr", (uint32_t)(uintptr_t)(*(void**)(p + PLAYER_OFS_ANIM_PTR)));
    *(uint8_t*)(p + PLAYER_OFS_STATE_ID) = (uint8_t)lua_table_get_int_field(Ls, idx, "state_id", *(uint8_t*)(p + PLAYER_OFS_STATE_ID));
    *(uint32_t*)(p + PLAYER_OFS_STATE_TIMER) = (uint32_t)lua_table_get_int_field(Ls, idx, "state_timer", *(uint32_t*)(p + PLAYER_OFS_STATE_TIMER));
    *(signed char*)(p + PLAYER_OFS_FACING_SIGN) = (signed char)lua_table_get_int_field(Ls, idx, "facing", *(signed char*)(p + PLAYER_OFS_FACING_SIGN));
    *(signed char*)(p + PLAYER_OFS_ROOM) = (signed char)lua_table_get_int_field(Ls, idx, "room_index", *(signed char*)(p + PLAYER_OFS_ROOM));
    // *(uint8_t*)(p + PLAYER_OFS_CMD_BITS) = (uint8_t)lua_table_get_int_field(Ls, idx, "cmd_bits", *(uint8_t*)(p + PLAYER_OFS_CMD_BITS));
    // *(uint8_t*)(p + PLAYER_OFS_PREV_CMD_BITS) = (uint8_t)lua_table_get_int_field(Ls, idx, "prev_cmd_bits", *(uint8_t*)(p + PLAYER_OFS_PREV_CMD_BITS));
    *(uint8_t*)(p + PLAYER_OFS_JUMP_BUFFER) = (uint8_t)lua_table_get_int_field(Ls, idx, "jump_buffer", *(uint8_t*)(p + PLAYER_OFS_JUMP_BUFFER));
    *(uint8_t*)(p + PLAYER_OFS_ATTACK_BUFFER) = (uint8_t)lua_table_get_int_field(Ls, idx, "attack_buffer", *(uint8_t*)(p + PLAYER_OFS_ATTACK_BUFFER));
    *(uint8_t*)(p + PLAYER_OFS_COLLISION_FLAGS) = (uint8_t)lua_table_get_int_field(Ls, idx, "collision_flags", *(uint8_t*)(p + PLAYER_OFS_COLLISION_FLAGS));
    *(uint8_t*)(p + PLAYER_OFS_PREV_COLLISION) = (uint8_t)lua_table_get_int_field(Ls, idx, "prev_collision_flags", *(uint8_t*)(p + PLAYER_OFS_PREV_COLLISION));
    *(uint8_t*)(p + PLAYER_OFS_HAS_SWORD) = lua_table_get_int_field(Ls, idx, "has_sword", (*(uint8_t*)(p + PLAYER_OFS_HAS_SWORD) == 0) ? 1 : 0) ? 0 : 1;
    return 1;
}

static int game_apply_entity_table(lua_State* Ls, int idx) {
    int slot;
    uint8_t* t;
    int snap_type;
    idx = lua_absindex_compat(Ls, idx);
    slot = lua_table_get_int_field(Ls, idx, "slot", -1);
    if (slot < 0 || slot >= game_get_thing_count()) return 0;
    t = p_things + (slot * THING_SIZE);
    if (!ptr_writable((void*)t, THING_SIZE)) return 0;

    snap_type = lua_table_get_int_field(Ls, idx, "type", (int)t[THING_OFS_TYPE]);
    if (snap_type != (int)t[THING_OFS_TYPE]) {
        /* Entity type changed at this slot (e.g. mine replaced by sword).
         * Clear the slot and overwrite with the snapshot's type so corrections
         * can actually repair diverged entity state instead of silently skipping. */
        memset(t, 0, THING_SIZE);
        t[THING_OFS_TYPE] = (uint8_t)snap_type;
    }

    t[THING_OFS_ACTIVE] = (uint8_t)(lua_table_get_int_field(Ls, idx, "active", t[THING_OFS_ACTIVE] ? 1 : 0) ? 1 : 0);
    *(float*)(t + THING_OFS_X) = lua_table_get_float_field(Ls, idx, "x", *(float*)(t + THING_OFS_X));
    *(float*)(t + THING_OFS_Y) = lua_table_get_float_field(Ls, idx, "y", *(float*)(t + THING_OFS_Y));
    *(float*)(t + THING_OFS_PREV_X) = lua_table_get_float_field(Ls, idx, "prev_x", *(float*)(t + THING_OFS_PREV_X));
    *(float*)(t + THING_OFS_PREV_Y) = lua_table_get_float_field(Ls, idx, "prev_y", *(float*)(t + THING_OFS_PREV_Y));
    *(float*)(t + THING_OFS_VX) = lua_table_get_float_field(Ls, idx, "vx", *(float*)(t + THING_OFS_VX));
    *(float*)(t + THING_OFS_VY) = lua_table_get_float_field(Ls, idx, "vy", *(float*)(t + THING_OFS_VY));
    if (t[THING_OFS_TYPE] == THING_TYPE_SWORD) {
        (void)lua_table_copy_hex_field(Ls, idx, "head_blob", (uint8_t*)(t + THING_OFS_HEAD_BLOB), THING_HEAD_BLOB_LEN);
        (void)lua_table_copy_hex_field(Ls, idx, "motion_blob", (uint8_t*)(t + THING_OFS_MOTION_BLOB), THING_MOTION_BLOB_LEN);
        (void)lua_table_copy_hex_field(Ls, idx, "action_blob", (uint8_t*)(t + THING_OFS_ACTION_BLOB), THING_ACTION_BLOB_LEN);
        (void)lua_table_copy_hex_field(Ls, idx, "state_blob", (uint8_t*)(t + THING_OFS_STATE_BLOB), THING_STATE_BLOB_LEN);
        (void)lua_table_copy_hex_field(Ls, idx, "tail_blob", (uint8_t*)(t + THING_OFS_TAIL_BLOB), THING_TAIL_BLOB_LEN);
    }
    *(uint8_t*)(t + THING_OFS_STATE_ID) = (uint8_t)lua_table_get_int_field(Ls, idx, "state_id", *(uint8_t*)(t + THING_OFS_STATE_ID));
    *(uint8_t*)(t + THING_OFS_FLAGS) = (uint8_t)lua_table_get_int_field(Ls, idx, "flags", *(uint8_t*)(t + THING_OFS_FLAGS));
    *(int*)(t + THING_OFS_ROOM) = lua_table_get_int_field(Ls, idx, "room_index", *(int*)(t + THING_OFS_ROOM));
    return 1;
}

static int game_apply_sword_table(lua_State* Ls, int idx) {
    int slot;
    uint8_t* t;

    idx = lua_absindex_compat(Ls, idx);
    slot = lua_table_get_int_field(Ls, idx, "slot", -1);
    if (slot <= 0 || slot >= game_get_thing_count()) return 0;

    t = p_things + (slot * THING_SIZE);
    if (!ptr_writable((void*)t, THING_SIZE)) return 0;
    if (t[THING_OFS_TYPE] == THING_TYPE_PLAYER) return 0;

    if (t[THING_OFS_TYPE] != THING_TYPE_SWORD || t[THING_OFS_ACTIVE] == 0) {
        memset(t, 0, THING_SIZE);
        t[THING_OFS_TYPE] = THING_TYPE_SWORD;
    }

    t[THING_OFS_ACTIVE] = (uint8_t)slot;
    *(float*)(t + THING_OFS_X) = lua_table_get_float_field(Ls, idx, "x", *(float*)(t + THING_OFS_X));
    *(float*)(t + THING_OFS_Y) = lua_table_get_float_field(Ls, idx, "y", *(float*)(t + THING_OFS_Y));
    *(float*)(t + THING_OFS_PREV_X) = lua_table_get_float_field(Ls, idx, "prev_x", *(float*)(t + THING_OFS_PREV_X));
    *(float*)(t + THING_OFS_PREV_Y) = lua_table_get_float_field(Ls, idx, "prev_y", *(float*)(t + THING_OFS_PREV_Y));
    *(float*)(t + THING_OFS_VX) = lua_table_get_float_field(Ls, idx, "vx", *(float*)(t + THING_OFS_VX));
    *(float*)(t + THING_OFS_VY) = lua_table_get_float_field(Ls, idx, "vy", *(float*)(t + THING_OFS_VY));
    *(int*)(t + THING_OFS_ROOM) = lua_table_get_int_field(Ls, idx, "room_index", *(int*)(t + THING_OFS_ROOM));
    *(uint8_t*)(t + THING_OFS_STATE_ID) = (uint8_t)lua_table_get_int_field(Ls, idx, "state_id", *(uint8_t*)(t + THING_OFS_STATE_ID));
    *(uint8_t*)(t + THING_OFS_FLAGS) = (uint8_t)lua_table_get_int_field(Ls, idx, "flags", *(uint8_t*)(t + THING_OFS_FLAGS));
    (void)lua_table_copy_hex_field(Ls, idx, "head_blob", (uint8_t*)(t + THING_OFS_HEAD_BLOB), THING_HEAD_BLOB_LEN);
    (void)lua_table_copy_hex_field(Ls, idx, "motion_blob", (uint8_t*)(t + THING_OFS_MOTION_BLOB), THING_MOTION_BLOB_LEN);
    (void)lua_table_copy_hex_field(Ls, idx, "action_blob", (uint8_t*)(t + THING_OFS_ACTION_BLOB), THING_ACTION_BLOB_LEN);
    (void)lua_table_copy_hex_field(Ls, idx, "state_blob", (uint8_t*)(t + THING_OFS_STATE_BLOB), THING_STATE_BLOB_LEN);
    (void)lua_table_copy_hex_field(Ls, idx, "tail_blob", (uint8_t*)(t + THING_OFS_TAIL_BLOB), THING_TAIL_BLOB_LEN);
    return 1;
}

static int lua_game_apply_snapshot(lua_State* Ls) {
    int idx = 1;
    int applied = 0;
    int snapshot_player_index = 0;
    int snapshot_enemy_index = 1;
    int snapshot_room_index = 0;
    if (!lua_istable(Ls, idx)) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, "expected snapshot table");
        return 2;
    }
    idx = lua_absindex_compat(Ls, idx);
    snapshot_player_index = lua_table_get_int_field(Ls, idx, "player_index", 0) & 1;
    snapshot_enemy_index = lua_table_get_int_field(Ls, idx, "enemy_index", 1) & 1;
    snapshot_room_index = lua_table_get_int_field(Ls, idx, "room_index", game_get_active_room_index());

    if (ptr_writable((void*)p_game_active_room, sizeof(int))) {
        *p_game_active_room = snapshot_room_index;
    }
    if (ptr_writable((void*)p_start_countdown, sizeof(int))) {
        *p_start_countdown = lua_table_get_int_field(Ls, idx, "start_countdown", *p_start_countdown);
    }
    if (ptr_writable((void*)p_end_countdown, sizeof(int))) {
        *p_end_countdown = lua_table_get_int_field(Ls, idx, "end_countdown", *p_end_countdown);
    }
    if (ptr_writable((void*)p_native_game_ticks, sizeof(uint32_t))) {
        *p_native_game_ticks = lua_table_get_u32_field(Ls, idx, "native_tick", *p_native_game_ticks);
    }
    if (ptr_writable((void*)p_mrand_seed, sizeof(uint32_t))) {
        *p_mrand_seed = lua_table_get_u32_field(Ls, idx, "rng_seed", *p_mrand_seed);
    }
    if (ptr_writable((void*)p_game_level, sizeof(uint32_t))) {
        *p_game_level = lua_table_get_u32_field(Ls, idx, "game_level", *p_game_level);
    }
    if (ptr_writable((void*)p_game_leader, sizeof(uintptr_t))) {
        int leader_index = lua_table_get_int_field(Ls, idx, "leader_index", -1);
        uintptr_t leader_ptr = (leader_index >= 0 && leader_index <= 1) ? game_get_player_ptr(leader_index) : 0;
        *p_game_leader = leader_ptr;
    }

    lua_getfield(Ls, idx, "player");
    if (lua_istable(Ls, -1)) applied |= game_apply_player_table(Ls, -1, snapshot_player_index);
    lua_pop(Ls, 1);

    lua_getfield(Ls, idx, "enemy");
    if (lua_istable(Ls, -1)) applied |= game_apply_player_table(Ls, -1, snapshot_enemy_index);
    lua_pop(Ls, 1);

    lua_getfield(Ls, idx, "entities");
    if (lua_istable(Ls, -1)) {
        int ent_idx = lua_absindex_compat(Ls, -1);
        int n = (int)lua_objlen(Ls, ent_idx);
        int thing_count = game_get_thing_count();
        uint8_t* seen_slots = NULL;
        if (thing_count > 0) {
            seen_slots = (uint8_t*)calloc((size_t)thing_count, sizeof(uint8_t));
        }
        for (int i = 1; i <= n; i++) {
            lua_rawgeti(Ls, ent_idx, i);
            if (lua_istable(Ls, -1)) {
                int slot = lua_table_get_int_field(Ls, -1, "slot", -1);
                int ent_applied = game_apply_entity_table(Ls, -1);
                applied |= ent_applied;
                /* Only mark the slot as seen if the entity was actually applied.
                 * Previously this was marked BEFORE the apply, so a type-guard
                 * skip would mark the slot seen without correcting it, causing
                 * permanently diverged entity state. */
                if (ent_applied && seen_slots && slot >= 0 && slot < thing_count) {
                    seen_slots[slot] = 1;
                }
            }
            lua_pop(Ls, 1);
        }
        if (seen_slots) {
            for (int i = 0; i < thing_count; i++) {
                uint8_t* t = p_things + (i * THING_SIZE);
                if (!ptr_writable((void*)t, THING_SIZE)) continue;
                if (t[THING_OFS_TYPE] == THING_TYPE_PLAYER) continue;
                if (*(int*)(t + THING_OFS_ROOM) != snapshot_room_index) continue;
                if (seen_slots[i]) continue;
                t[THING_OFS_ACTIVE] = 0;
            }
            free(seen_slots);
        }
    }
    lua_pop(Ls, 1);

    lua_pushboolean(Ls, applied ? 1 : 0);
    return 1;
}

static int lua_game_apply_sword_snapshot(lua_State* Ls) {
    int idx = 1;
    int applied = 0;
    int thing_count = game_get_thing_count();
    uint8_t* seen_slots = NULL;

    if (!lua_istable(Ls, idx)) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, "expected sword snapshot table");
        return 2;
    }
    idx = lua_absindex_compat(Ls, idx);

    for (int player_index = 0; player_index < 2; player_index++) {
        uintptr_t p = game_get_player_ptr(player_index);
        if (p && ptr_writable((void*)p, PLAYER_SIZE)) {
            char key[24];
            int def = (*(uint8_t*)(p + PLAYER_OFS_HAS_SWORD) == 0) ? 1 : 0;
            snprintf(key, sizeof(key), "p%d_has_sword", player_index);
            *(uint8_t*)(p + PLAYER_OFS_HAS_SWORD) = lua_table_get_int_field(Ls, idx, key, def) ? 0 : 1;
            applied = 1;
        }
    }

    if (thing_count > 0) {
        seen_slots = (uint8_t*)calloc((size_t)thing_count, sizeof(uint8_t));
    }

    lua_getfield(Ls, idx, "swords");
    if (lua_istable(Ls, -1)) {
        int swords_idx = lua_absindex_compat(Ls, -1);
        int n = (int)lua_objlen(Ls, swords_idx);
        for (int i = 1; i <= n; i++) {
            lua_rawgeti(Ls, swords_idx, i);
            if (lua_istable(Ls, -1)) {
                int slot = lua_table_get_int_field(Ls, -1, "slot", -1);
                if (seen_slots && slot > 0 && slot < thing_count) {
                    seen_slots[slot] = 1;
                }
                applied |= game_apply_sword_table(Ls, -1);
            }
            lua_pop(Ls, 1);
        }
    }
    lua_pop(Ls, 1);

    if (seen_slots) {
        for (int i = 1; i < thing_count; i++) {
            uint8_t* t = p_things + (i * THING_SIZE);
            if (!ptr_writable((void*)t, THING_SIZE)) continue;
            if (t[THING_OFS_TYPE] != THING_TYPE_SWORD) continue;
            if (seen_slots[i]) continue;
            t[THING_OFS_ACTIVE] = 0;
            applied = 1;
        }
        free(seen_slots);
    }

    lua_pushboolean(Ls, applied ? 1 : 0);
    return 1;
}

static int lua_input_bind(lua_State* Ls) {

    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* key = luaL_checkstring(Ls, 1);
    const char* default_name = luaL_optstring(Ls, 2, "");
    const char* label = luaL_optstring(Ls, 3, key);
    int default_sym = 0;
    int idx;
    if (default_name && default_name[0] && !bind_name_to_sym(default_name, &default_sym)) {
        lua_pushnil(Ls);
        lua_pushfstring(Ls, "unknown bind key '%s'", default_name);
        return 2;
    }
    idx = mod_bind_register(mod, key, label, default_sym);
    if (idx < 0) {
        lua_pushnil(Ls);
        lua_pushstring(Ls, "failed to register bind");
        return 2;
    }
    lua_pushstring(Ls, mod->binds[idx].value_name);
    return 1;
}

static int lua_input_get(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* key = luaL_checkstring(Ls, 1);
    int idx = mod_bind_find_index(mod, key);
    if (idx < 0) {
        lua_pushnil(Ls);
        return 1;
    }
    lua_pushstring(Ls, mod->binds[idx].value_name);
    return 1;
}

static int lua_input_set(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* key = luaL_checkstring(Ls, 1);
    const char* value = luaL_checkstring(Ls, 2);
    int idx = mod_bind_find_index(mod, key);
    int sym = 0;
    if (idx < 0) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, "bind not found");
        return 2;
    }
    if (!bind_name_to_sym(value, &sym)) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, "unknown key name");
        return 2;
    }
    mod->binds[idx].sym = sym;
    mod_bind_update_name(&mod->binds[idx]);
    mod_bind_save(mod);
    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_input_clear(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* key = luaL_checkstring(Ls, 1);
    int idx = mod_bind_find_index(mod, key);
    if (idx < 0) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, "bind not found");
        return 2;
    }
    mod->binds[idx].sym = 0;
    mod_bind_update_name(&mod->binds[idx]);
    mod_bind_save(mod);
    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_input_down(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* key = luaL_checkstring(Ls, 1);
    int idx = mod_bind_find_index(mod, key);
    lua_pushboolean(Ls, (idx >= 0 && mod->binds[idx].down) ? 1 : 0);
    return 1;
}

static int lua_input_pressed(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* key = luaL_checkstring(Ls, 1);
    int idx = mod_bind_find_index(mod, key);
    lua_pushboolean(Ls, (idx >= 0 && mod->binds[idx].pressed) ? 1 : 0);
    return 1;
}

static int lua_input_released(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* key = luaL_checkstring(Ls, 1);
    int idx = mod_bind_find_index(mod, key);
    lua_pushboolean(Ls, (idx >= 0 && mod->binds[idx].released) ? 1 : 0);
    return 1;
}

static int lua_input_list(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    lua_newtable(Ls);
    for (int i = 0; i < mod->bind_count; i++) {
        lua_newtable(Ls);
        lua_pushstring(Ls, mod->binds[i].key); lua_setfield(Ls, -2, "key");
        lua_pushstring(Ls, mod->binds[i].label); lua_setfield(Ls, -2, "label");
        lua_pushstring(Ls, mod->binds[i].value_name); lua_setfield(Ls, -2, "binding");
        lua_pushboolean(Ls, mod_bind_has_conflict(mod, i)); lua_setfield(Ls, -2, "conflict");
        lua_rawseti(Ls, -2, i + 1);
    }
    return 1;
}

static void push_input_api_table(lua_State* Ls, LoadedMod* mod) {
    lua_newtable(Ls);
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_input_bind, 1);     lua_setfield(Ls, -2, "bind");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_input_get, 1);      lua_setfield(Ls, -2, "get");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_input_set, 1);      lua_setfield(Ls, -2, "set");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_input_clear, 1);    lua_setfield(Ls, -2, "clear");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_input_down, 1);     lua_setfield(Ls, -2, "down");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_input_pressed, 1);  lua_setfield(Ls, -2, "pressed");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_input_released, 1); lua_setfield(Ls, -2, "released");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_input_list, 1);     lua_setfield(Ls, -2, "list");
}

// =============================
// Audio API (mod.audio)
// =============================

static int audio_string_looks_like_path(const char* s) {
    if (!s || !s[0]) return 0;
    if (audio_is_absolute_path(s)) return 1;
    if (strchr(s, '\\') || strchr(s, '/')) return 1;
    if (strchr(s, '.')) return 1;
    return 0;
}

static int lua_audio_play_sfx(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* source = luaL_checkstring(Ls, 1);
    char full_path[MAX_PATH];
    char err[256] = {0};
    int treat_as_path = 0;
    int loops = 0;
    int ticks = -1;
    float volume = 1.0f;
    int channel = -1;
    Mix_Chunk* chunk = NULL;

    if (!mod) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, "no mod context");
        return 2;
    }

    if (!source || !source[0]) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, "missing path_or_id");
        return 2;
    }

    treat_as_path = audio_string_looks_like_path(source);
    if (!treat_as_path && audio_resolve_mod_path(mod, source, full_path, (int)sizeof(full_path))) {
        treat_as_path = audio_file_exists(full_path);
    }

    if (!treat_as_path) {
        if (audio_play_builtin_sfx(mod, Ls, source, 2)) {
            lua_pushboolean(Ls, 1);
            return 1;
        }
    }

    if (!audio_resolve_mod_path(mod, source, full_path, (int)sizeof(full_path))) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, "invalid audio path");
        return 2;
    }

    if (!audio_runtime_ensure_ready()) {
        int fallback_loop;
        loops = audio_opts_get_int(Ls, 2, "loops", 0);
        fallback_loop = (loops != 0);

        if (audio_fallback_play_wav(full_path, fallback_loop, 0, err, (int)sizeof(err))) {
            lua_pushboolean(Ls, 1);
            return 1;
        }

        if (!err[0]) snprintf(err, sizeof(err), "%s", audio_backend_error_message());
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, err);
        return 2;
    }

    chunk = mod_audio_get_or_load_chunk(mod, full_path, err, (int)sizeof(err));
    if (!chunk) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, err[0] ? err : "failed to load sound");
        return 2;
    }

    if (!mod_audio_ensure_channel_range(mod, err, (int)sizeof(err))) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, err[0] ? err : "failed to reserve channels");
        return 2;
    }

    loops = audio_opts_get_int(Ls, 2, "loops", 0);
    if (loops < -1) loops = -1;
    ticks = audio_opts_get_int(Ls, 2, "ticks", -1);
    volume = audio_clampf(mod->audio_sfx_volume * audio_opts_get_float(Ls, 2, "volume", 1.0f), 0.0f, 1.0f);

    channel = mod_audio_pick_channel(mod);
    if (channel < 0) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, "no available mixer channel");
        return 2;
    }

    if (p_mix_play_channel_timed(channel, chunk, loops, ticks) < 0) {
        snprintf(err, sizeof(err), "Mix_PlayChannelTimed failed: %s", audio_mix_error());
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, err);
        return 2;
    }

    if (p_mix_volume_channel) {
        int ivol = (int)(volume * (float)AUDIO_MIX_MAX_VOLUME + 0.5f);
        if (ivol < 0) ivol = 0;
        if (ivol > AUDIO_MIX_MAX_VOLUME) ivol = AUDIO_MIX_MAX_VOLUME;
        p_mix_volume_channel(channel, ivol);
    }

    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_audio_play_music(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* rel_path = luaL_checkstring(Ls, 1);
    char full_path[MAX_PATH];
    char err[256];
    int loops = -1;
    float volume = 1.0f;
    Mix_Music* music = NULL;

    if (!mod) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, "no mod context");
        return 2;
    }

    if (!audio_resolve_mod_path(mod, rel_path, full_path, (int)sizeof(full_path))) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, "invalid music path");
        return 2;
    }
    if (!audio_file_exists(full_path)) {
        snprintf(err, sizeof(err), "file not found: %s", full_path);
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, err);
        return 2;
    }

    loops = audio_opts_get_int(Ls, 2, "loops", -1);
    volume = audio_clampf(mod->audio_music_volume * audio_opts_get_float(Ls, 2, "volume", 1.0f), 0.0f, 1.0f);

    if (!audio_runtime_ensure_ready()) {
        int fallback_loop = (loops != 0);
        audio_fallback_stop_music_for_owner(NULL);

        if (audio_fallback_play_wav(full_path, fallback_loop, 0, err, (int)sizeof(err))) {
            g_audio_fallback_music_owner = mod;
            lua_pushboolean(Ls, 1);
            return 1;
        }

        if (!err[0]) snprintf(err, sizeof(err), "%s", audio_backend_error_message());
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, err);
        return 2;
    }

    music = p_mix_load_mus(full_path);
    if (!music) {
        snprintf(err, sizeof(err), "Mix_LoadMUS failed: %s", audio_mix_error());
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, err);
        return 2;
    }

    audio_fallback_stop_music_for_owner(NULL);
    audio_release_music_for_owner(NULL);
    if (p_mix_play_music(music, loops) < 0) {
        snprintf(err, sizeof(err), "Mix_PlayMusic failed: %s", audio_mix_error());
        if (p_mix_free_music) p_mix_free_music(music);
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, err);
        return 2;
    }

    g_audio_music_owner = mod;
    g_audio_music = music;

    if (p_mix_volume_music) {
        int ivol = (int)(volume * (float)AUDIO_MIX_MAX_VOLUME + 0.5f);
        if (ivol < 0) ivol = 0;
        if (ivol > AUDIO_MIX_MAX_VOLUME) ivol = AUDIO_MIX_MAX_VOLUME;
        p_mix_volume_music(ivol);
    }

    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_audio_stop_music(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    if (!mod) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, "no mod context");
        return 2;
    }

    if (!g_audio_music && !g_audio_fallback_music_owner) {
        lua_pushboolean(Ls, 1);
        return 1;
    }

    if ((g_audio_music_owner && g_audio_music_owner != mod) ||
        (g_audio_fallback_music_owner && g_audio_fallback_music_owner != mod)) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, "music is currently owned by another mod");
        return 2;
    }

    audio_release_music_for_owner(mod);
    audio_fallback_stop_music_for_owner(mod);
    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_audio_set_music_volume(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    float v = (float)luaL_checknumber(Ls, 1);
    int ivol;

    if (!mod) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, "no mod context");
        return 2;
    }

    mod->audio_music_volume = audio_clampf(v, 0.0f, 1.0f);
    if (g_audio_music_owner == mod && g_audio_music && p_mix_volume_music) {
        ivol = (int)(mod->audio_music_volume * (float)AUDIO_MIX_MAX_VOLUME + 0.5f);
        if (ivol < 0) ivol = 0;
        if (ivol > AUDIO_MIX_MAX_VOLUME) ivol = AUDIO_MIX_MAX_VOLUME;
        p_mix_volume_music(ivol);
    } else if (g_audio_fallback_music_owner == mod) {
        if (!g_audio_winmm_warned) {
            LOG_WARN("Audio API: WinMM fallback active; music volume changes are not supported");
            g_audio_winmm_warned = 1;
        }
    }

    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_audio_set_sfx_volume(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    float v = (float)luaL_checknumber(Ls, 1);
    if (!mod) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, "no mod context");
        return 2;
    }
    mod->audio_sfx_volume = audio_clampf(v, 0.0f, 1.0f);
    lua_pushboolean(Ls, 1);
    return 1;
}

static void push_audio_api_table(lua_State* Ls, LoadedMod* mod) {
    lua_newtable(Ls);
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_audio_play_sfx, 1);         lua_setfield(Ls, -2, "play_sfx");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_audio_play_music, 1);       lua_setfield(Ls, -2, "play_music");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_audio_stop_music, 1);       lua_setfield(Ls, -2, "stop_music");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_audio_set_music_volume, 1); lua_setfield(Ls, -2, "set_music_volume");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_audio_set_sfx_volume, 1);   lua_setfield(Ls, -2, "set_sfx_volume");
}

// =============================
// Font glyph extension API
// =============================

static int lua_font_loaded(lua_State* Ls) {
    (void)mod_from_upvalue(Ls);
    lua_pushboolean(Ls, font_ext_font_loaded() ? 1 : 0);
    return 1;
}

static int lua_font_alloc_glyph(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* rel = luaL_checkstring(Ls, 1);
    uint8_t b = 0;
    char err[256] = {0};
    if (!mod) {
        lua_pushnil(Ls);
        lua_pushstring(Ls, "no mod context");
        return 2;
    }
    if (!font_ext_alloc_glyph(mod->id, mod->folder_path, rel, &b, err, (int)sizeof(err))) {
        lua_pushnil(Ls);
        lua_pushstring(Ls, err[0] ? err : "alloc_glyph failed");
        return 2;
    }
    (void)mod_font_reg_record(mod, b, rel);
    if (font_ext_font_loaded()) {
        if (!reload_engine_gfx_atlases("font glyph allocated after font load")) {
            LOG_WARN("font_ext: alloc_glyph succeeded, but live apply failed; restart may still be required");
        }
    }
    lua_pushinteger(Ls, (int)b);
    return 1;
}

static int lua_font_register_glyph(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    int byte_value = (int)luaL_checkinteger(Ls, 1);
    const char* rel = luaL_checkstring(Ls, 2);
    int override_other = 0;

    if (!mod) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, "no mod context");
        return 2;
    }

    if (lua_istable(Ls, 3)) {
        lua_getfield(Ls, 3, "override");
        override_other = lua_toboolean(Ls, -1) ? 1 : 0;
        lua_pop(Ls, 1);
    }

    if (byte_value < 0) byte_value = 0;
    if (byte_value > 255) byte_value = 255;

    char err[256] = {0};
    if (!font_ext_register_glyph(mod->id, mod->folder_path, (uint8_t)byte_value, rel, override_other, err, (int)sizeof(err))) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, err[0] ? err : "register_glyph failed");
        return 2;
    }
    (void)mod_font_reg_record(mod, (uint8_t)byte_value, rel);

    if (font_ext_font_loaded()) {
        if (!reload_engine_gfx_atlases("font glyph registered after font load")) {
            LOG_WARN("font_ext: register_glyph succeeded, but live apply failed; restart may still be required");
        }
    }

    lua_pushboolean(Ls, 1);
    return 1;
}

// =============================
// Texture replacement API
// =============================

static int lua_texture_parse_override(lua_State* Ls, int arg_index) {
    int override_other = 0;
    if (lua_istable(Ls, arg_index)) {
        lua_getfield(Ls, arg_index, "override");
        override_other = lua_toboolean(Ls, -1) ? 1 : 0;
        lua_pop(Ls, 1);
    }
    return override_other;
}

static int lua_texture_register_target(
    lua_State* Ls,
    LoadedMod* mod,
    const char* target_path,
    const char* rel_path,
    int override_other,
    const char* fallback_err
) {
    char err[256] = {0};
    if (!mod) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, "no mod context");
        return 2;
    }
    if (!texture_ext_register_png(mod->id, mod->folder_path, target_path, rel_path, override_other, err, (int)sizeof(err))) {
        lua_pushboolean(Ls, 0);
        lua_pushstring(Ls, err[0] ? err : fallback_err);
        return 2;
    }
    (void)mod_texture_reg_record(mod, target_path, rel_path);

    if (texture_ext_path_loaded(target_path)) {
        if (!reload_engine_gfx_atlases("texture registered after atlas load")) {
            LOG_WARN("texture_ext: registration succeeded, but live apply failed for %s; restart may still be required", target_path);
        }
    }

    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_texture_sprites_loaded(lua_State* Ls) {
    (void)mod_from_upvalue(Ls);
    lua_pushboolean(Ls, texture_ext_sprites_loaded() ? 1 : 0);
    return 1;
}

static int lua_texture_loaded(lua_State* Ls) {
    (void)mod_from_upvalue(Ls);
    const char* target_path = luaL_checkstring(Ls, 1);
    lua_pushboolean(Ls, texture_ext_path_loaded(target_path) ? 1 : 0);
    return 1;
}

static int lua_texture_register(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* target_path = luaL_checkstring(Ls, 1);
    const char* rel = luaL_checkstring(Ls, 2);
    int override_other = lua_texture_parse_override(Ls, 3);
    return lua_texture_register_target(Ls, mod, target_path, rel, override_other, "register failed");
}

static int lua_texture_register_spritesheet(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* rel = luaL_checkstring(Ls, 1);
    int override_other = lua_texture_parse_override(Ls, 2);
    return lua_texture_register_target(Ls, mod, "data/sprites.png", rel, override_other, "register_spritesheet failed");
}

static int lua_texture_register_tilesheet(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* rel = luaL_checkstring(Ls, 1);
    int override_other = lua_texture_parse_override(Ls, 2);
    return lua_texture_register_target(Ls, mod, "data/tiles.png", rel, override_other, "register_tilesheet failed");
}

static int lua_texture_register_miscsheet(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* rel = luaL_checkstring(Ls, 1);
    int override_other = lua_texture_parse_override(Ls, 2);
    return lua_texture_register_target(Ls, mod, "data/misc.png", rel, override_other, "register_miscsheet failed");
}

static int lua_texture_register_glowsheet(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* rel = luaL_checkstring(Ls, 1);
    int override_other = lua_texture_parse_override(Ls, 2);
    return lua_texture_register_target(Ls, mod, "data/glow.png", rel, override_other, "register_glowsheet failed");
}

static int lua_texture_reload_all(lua_State* Ls) {
    int tex_reloaded = 0;
    int tex_failed = 0;
    int tex_restart = 0;
    int font_reloaded = 0;
    int font_failed = 0;
    int font_restart = 0;
    (void)mod_from_upvalue(Ls);

    texture_ext_reload_all(&tex_reloaded, &tex_failed, &tex_restart);
    font_ext_reload_all(&font_reloaded, &font_failed, &font_restart);

    if ((tex_reloaded + font_reloaded) > 0) {
        if (!reload_engine_gfx_atlases("manual mod.texture.reload_all()")) {
            tex_restart = tex_reloaded;
            font_restart = font_reloaded;
        } else {
            tex_restart = 0;
            font_restart = 0;
        }
    }

    lua_pushboolean(Ls, (tex_failed + font_failed) == 0);
    lua_newtable(Ls);
    lua_pushinteger(Ls, tex_reloaded); lua_setfield(Ls, -2, "textures_reloaded");
    lua_pushinteger(Ls, tex_failed); lua_setfield(Ls, -2, "textures_failed");
    lua_pushinteger(Ls, tex_restart); lua_setfield(Ls, -2, "textures_restart_required");
    lua_pushinteger(Ls, font_reloaded); lua_setfield(Ls, -2, "fonts_reloaded");
    lua_pushinteger(Ls, font_failed); lua_setfield(Ls, -2, "fonts_failed");
    lua_pushinteger(Ls, font_restart); lua_setfield(Ls, -2, "fonts_restart_required");
    return 2;
}

static int lua_ui_button_resize_ptr(lua_State* Ls) {
    void* btn = ui_lua_ptr_to_button(Ls, 1);
    float w = (float)luaL_checknumber(Ls, 2);
    float h = (float)luaL_checknumber(Ls, 3);
    float shrink = (float)luaL_optnumber(Ls, 4, 4.0);
    if (!btn || w <= 0.0f || h <= 0.0f) { lua_pushboolean(Ls, 0); return 1; }
    if (shrink < 0.0f) shrink = 0.0f;
    if (p_button_set_w_ex) p_button_set_w_ex((int)(intptr_t)btn, w, shrink);
    if (p_button_set_h_ex) p_button_set_h_ex((int)(intptr_t)btn, h, shrink);
    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_ui_button_hide_ptr(lua_State* Ls) {
    void* btn = ui_lua_ptr_to_button(Ls, 1);
    int hidden = lua_toboolean(Ls, 2);
    if (!btn) { lua_pushboolean(Ls, 0); return 1; }
    ui_button_apply_flags_hidden(btn, hidden);
    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_ui_button_remove_ptr(lua_State* Ls) {
    void* btn = ui_lua_ptr_to_button(Ls, 1);
    if (!btn) { lua_pushboolean(Ls, 0); return 1; }

    ui_button_apply_flags_hidden(btn, 1);
    if (p_button_set_w_ex) p_button_set_w_ex((int)(intptr_t)btn, 1.0f, 0.0f);
    if (p_button_set_h_ex) p_button_set_h_ex((int)(intptr_t)btn, 1.0f, 0.0f);
    *(float*)((uint8_t*)btn + BTN_OFS_CENTER_X) = -10000.0f;
    *(float*)((uint8_t*)btn + BTN_OFS_CENTER_Y) = -10000.0f;

    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_ui_native_remove(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* id = luaL_checkstring(Ls, 1);
    const char* state_name = ui_state_name_from_ptr(ui_current_state_ptr());
    const char* button_state_name = (_stricmp(state_name, "main_initial") == 0) ? "main" : state_name;

    if (!mod) { lua_pushboolean(Ls, 0); return 1; }

    for (int i = 0; i < mod->ui_native_count; i++) {
        UiNativeButton* b = mod->ui_native_buttons[i];
        if (!b) continue;
        if (_stricmp(b->id, id) != 0) continue;
        if (_stricmp(b->state_name, button_state_name) != 0) continue;

        free(b);
        for (int j = i + 1; j < mod->ui_native_count; j++) {
            mod->ui_native_buttons[j - 1] = mod->ui_native_buttons[j];
        }
        mod->ui_native_count--;
        lua_pushboolean(Ls, 1);
        return 1;
    }

    lua_pushboolean(Ls, 0);
    return 1;
}

/* ---- custom state helpers (mod.ui) ------------------------------------ */

static int lua_ui_create_state(lua_State *L) {
    const char* name = luaL_checkstring(L, 1);
    lua_pushboolean(L, hooks_register_custom_state(name));
    return 1;
}

static int lua_ui_enter_state(lua_State *L) {
    const char* name = luaL_checkstring(L, 1);
    lua_pushboolean(L, hooks_enter_custom_state(name));
    return 1;
}

static int lua_ui_leave_state(lua_State *L) {
    lua_pushboolean(L, hooks_leave_custom_state());
    return 1;
}

static int lua_ui_goto_main_menu(lua_State *L) {
    (void)L;
    if (!p_state_switch) {
        lua_pushboolean(L, 0);
        return 1;
    }
    p_state_switch((void*)(uintptr_t)ADDR_MAIN_STATE);
    lua_pushboolean(L, 1);
    return 1;
}

static void push_ui_api_table(lua_State* Ls, LoadedMod* mod) {
    lua_newtable(Ls);
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_state_name, 1); lua_setfield(Ls, -2, "state_name");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_state_ptr, 1);  lua_setfield(Ls, -2, "state_ptr");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_is_state, 1);   lua_setfield(Ls, -2, "is_state");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_screen_size, 1);lua_setfield(Ls, -2, "screen_size");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_mouse_pos, 1);  lua_setfield(Ls, -2, "mouse_pos");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_mouse_buttons, 1); lua_setfield(Ls, -2, "mouse_buttons");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_set_default_cursor_visible, 1); lua_setfield(Ls, -2, "_set_default_cursor_visible");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_hitbox, 1);     lua_setfield(Ls, -2, "hitbox");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_rect, 1);       lua_setfield(Ls, -2, "rect");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_border, 1);     lua_setfield(Ls, -2, "border");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_line, 1);       lua_setfield(Ls, -2, "line");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_measure_text, 1); lua_setfield(Ls, -2, "measure_text");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_readable_scale, 1); lua_setfield(Ls, -2, "readable_scale");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_readable_scale, 1); lua_setfield(Ls, -2, "text_scale_factor");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_fill_rect, 1);  lua_setfield(Ls, -2, "fill_rect");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_stroke_rect, 1);lua_setfield(Ls, -2, "stroke_rect");
    lua_pushcfunction(Ls, lua_ui_flush);         lua_setfield(Ls, -2, "flush");
    lua_pushcfunction(Ls, lua_ui_begin_overlay); lua_setfield(Ls, -2, "begin_overlay");
    lua_pushcfunction(Ls, lua_ui_end_overlay);   lua_setfield(Ls, -2, "end_overlay");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_sheet_base, 1); lua_setfield(Ls, -2, "sheet_base");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_sprite_id, 1);  lua_setfield(Ls, -2, "sprite_id");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_draw_sprite, 1);lua_setfield(Ls, -2, "draw_sprite");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_layout, 1);     lua_setfield(Ls, -2, "layout");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_cursor, 1);     lua_setfield(Ls, -2, "cursor");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_next_row, 1);   lua_setfield(Ls, -2, "next_row");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_text, 1);       lua_setfield(Ls, -2, "text");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_text_at, 1);    lua_setfield(Ls, -2, "text_at");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_button, 1);     lua_setfield(Ls, -2, "button");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_button_at, 1);  lua_setfield(Ls, -2, "button_at");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_native_button, 1);      lua_setfield(Ls, -2, "native_button");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_native_set_pos, 1);     lua_setfield(Ls, -2, "native_set_pos");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_native_set_layout, 1);  lua_setfield(Ls, -2, "native_set_layout");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_native_resize, 1);      lua_setfield(Ls, -2, "native_resize");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_native_set_text_scale, 1); lua_setfield(Ls, -2, "native_set_text_scale");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_native_hide, 1);        lua_setfield(Ls, -2, "native_hide");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_native_remove, 1);      lua_setfield(Ls, -2, "native_remove");

    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_find_button_by_label, 1);      lua_setfield(Ls, -2, "find_button_by_label");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_find_button_by_action_ptr, 1); lua_setfield(Ls, -2, "find_button_by_action_ptr");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_button_rect_ptr, 1);      lua_setfield(Ls, -2, "button_rect_ptr");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_button_set_pos_ptr, 1);   lua_setfield(Ls, -2, "button_set_pos_ptr");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_button_set_label_ptr, 1); lua_setfield(Ls, -2, "button_set_label_ptr");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_button_invoke_ptr, 1); lua_setfield(Ls, -2, "button_invoke_ptr");
lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_button_activate_ptr, 1); lua_setfield(Ls, -2, "button_activate_ptr");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_button_resize_ptr, 1);    lua_setfield(Ls, -2, "button_resize_ptr");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_button_hide_ptr, 1);      lua_setfield(Ls, -2, "button_hide_ptr");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_ui_button_remove_ptr, 1);    lua_setfield(Ls, -2, "button_remove_ptr");

    /* Generic custom-state API. */
    lua_pushcfunction(Ls, lua_ui_create_state); lua_setfield(Ls, -2, "create_state");
    lua_pushcfunction(Ls, lua_ui_create_state); lua_setfield(Ls, -2, "register_state");
    lua_pushcfunction(Ls, lua_ui_enter_state);  lua_setfield(Ls, -2, "enter_state");
    lua_pushcfunction(Ls, lua_ui_leave_state);  lua_setfield(Ls, -2, "leave_state");

    lua_pushcfunction(Ls, lua_ui_goto_main_menu); lua_setfield(Ls, -2, "goto_main_menu");
}

/* ---- mod.game map selector helpers -------------------------------- */

static int lua_game_set_map_selector(lua_State *L) {
    int n = (int)luaL_checkinteger(L, 1);
    volatile int *sel = (volatile int *)(uintptr_t)ADDR_MAP_SELECTOR;
    *sel = n;
    lua_pushboolean(L, 1);
    return 1;
}

static int lua_game_get_map_selector(lua_State *L) {
    volatile int *sel = (volatile int *)(uintptr_t)ADDR_MAP_SELECTOR;
    lua_pushinteger(L, *sel);
    return 1;
}

static int lua_game_camera(lua_State* Ls) {
    volatile float* cam_x = (volatile float*)(uintptr_t)ADDR_CAMERA_X;
    volatile float* cam_y = (volatile float*)(uintptr_t)ADDR_CAMERA_Y;
    volatile float* gw    = (volatile float*)(uintptr_t)ADDR_GAME_W;
    volatile float* gh    = (volatile float*)(uintptr_t)ADDR_GAME_H;
    lua_newtable(Ls);
    lua_push_field_number(Ls, "x", (double)*cam_x);
    lua_push_field_number(Ls, "y", (double)*cam_y);
    lua_push_field_number(Ls, "w", (double)*gw);
    lua_push_field_number(Ls, "h", (double)*gh);
    return 1;
}

static int lua_game_is_solid(lua_State* Ls) {
    float x = (float)luaL_checknumber(Ls, 1);
    float y = (float)luaL_checknumber(Ls, 2);

    if (!p_is_pos_solid || IsBadCodePtr((FARPROC)(void*)p_is_pos_solid)) {
        lua_pushboolean(Ls, 0);
        return 1;
    }

    lua_pushboolean(Ls, p_is_pos_solid(x, y) != 0);
    return 1;
}

/* ============================================================================
 * World Control API (mod.game.world) + custom tile behaviors.
 *
 * The general "control the live game world" layer: read/write player state,
 * read tiles, apply effects (kill via native player_die, bounce, teleport, ...),
 * and bind behavior to tile ids. Intended for tick-time use (mod.on_tick). v1 is
 * local-first; tile dispatch is gated off during GGPO online matches.
 * ==========================================================================*/

#define WORLD_MAX_TILE_BEHAVIORS 64

typedef struct TileBehavior {
    int used;
    int mod_index;       /* owning mod (index into g_mods) */
    int id;              /* tile id to match (0..255), or -1 for "any solid" */
    int match_solid;     /* if 1, match any solid tile instead of a specific id */
    char glyph;          /* source glyph, for reporting (0 if registered by id) */
    int on_enter_ref;    /* LUA_NOREF when absent */
    int on_stay_ref;
    int on_exit_ref;
} TileBehavior;

static TileBehavior g_tile_behaviors[WORLD_MAX_TILE_BEHAVIORS];
static unsigned char g_tile_overlap_prev[2][WORLD_MAX_TILE_BEHAVIORS];
static int g_tile_behavior_active = 0;
static int g_world_online_warned = 0;

/* Map a custom-map glyph to its engine tile type (matches the engine's roomdef
 * placement). Returns the tile type, or -1 for unknown/ambiguous glyphs. Only
 * single-tile glyphs are included (multi-tile decals like G/L/N/Y are omitted).
 * Lets mods + map authors refer to tiles by the glyph they place, not a magic id. */
static int world_glyph_to_type(char g) {
    switch (g) {
        case '@': return 1;   case '!': return 3;   case '_': return 4;
        case 'X': case 'v': return 5;   case 'm': return 6;
        case 'C': case 'c': return 8;   case '^': return 9;   case 'E': return 10;
        case 'A': return 11;  case 'P': return 12;  case 'W': return 14;
        case 'w': return 15;  case '~': return 16;
        case 'S': case 'u': case 'x': return 20;
        case '#': case '-': case ':': case '=': case 'H': case 'I': case '`': case 'e': return 21;
        case '+': return 22;  case 'F': case 'Z': return 23;  case 'f': return 24;
        case '|': return 25;  case 'Q': case 'q': return 26;  case 'O': return 27;
        case '*': case 'K': return 28;  case 'i': return 29;
        case '1': case '2': return 30;  case 'l': return 31;
        default: return -1;
    }
}

/* Tile id (0..255) at a world pixel position, or -1 if unavailable. */
static int world_tile_id_at(float x, float y) {
    unsigned char* t;
    if (!p_map_coord_tile || IsBadCodePtr((FARPROC)(void*)p_map_coord_tile)) return -1;
    t = p_map_coord_tile(x, y);
    if (!t || IsBadReadPtr(t, 1)) return -1;
    return (int)*t;
}

static int world_tile_is_solid(float x, float y) {
    if (!p_is_pos_solid || IsBadCodePtr((FARPROC)(void*)p_is_pos_solid)) return 0;
    return p_is_pos_solid(x, y) != 0;
}

/* Resolve the player index carried as an upvalue on a handle method closure,
 * and set *base so params can be read whether the call used p:m() or p.m(). */
static uintptr_t world_method_player(lua_State* Ls, int* base) {
    int pidx = (int)lua_tointeger(Ls, lua_upvalueindex(1)) & 1;
    *base = lua_istable(Ls, 1) ? 1 : 0;
    return game_get_player_ptr(pidx);
}

static int world_p_set_pos(lua_State* Ls) {
    int base; uintptr_t pp = world_method_player(Ls, &base);
    if (!pp || !ptr_writable((void*)pp, PLAYER_SIZE)) { lua_pushboolean(Ls, 0); return 1; }
    *(float*)(pp + PLAYER_OFS_X) = (float)luaL_checknumber(Ls, base + 1);
    *(float*)(pp + PLAYER_OFS_Y) = (float)luaL_checknumber(Ls, base + 2);
    lua_pushboolean(Ls, 1); return 1;
}

static int world_p_teleport(lua_State* Ls) {
    int base; uintptr_t pp = world_method_player(Ls, &base);
    float x, y;
    if (!pp || !ptr_writable((void*)pp, PLAYER_SIZE)) { lua_pushboolean(Ls, 0); return 1; }
    x = (float)luaL_checknumber(Ls, base + 1);
    y = (float)luaL_checknumber(Ls, base + 2);
    *(float*)(pp + PLAYER_OFS_X) = x;
    *(float*)(pp + PLAYER_OFS_Y) = y;
    *(float*)(pp + PLAYER_OFS_PREV_X) = x;  /* avoid interpolation tearing */
    *(float*)(pp + PLAYER_OFS_PREV_Y) = y;
    lua_pushboolean(Ls, 1); return 1;
}

static int world_p_set_velocity(lua_State* Ls) {
    int base; uintptr_t pp = world_method_player(Ls, &base);
    if (!pp || !ptr_writable((void*)pp, PLAYER_SIZE)) { lua_pushboolean(Ls, 0); return 1; }
    if (!lua_isnoneornil(Ls, base + 1)) *(float*)(pp + PLAYER_OFS_VX) = (float)luaL_checknumber(Ls, base + 1);
    if (!lua_isnoneornil(Ls, base + 2)) *(float*)(pp + PLAYER_OFS_VY) = (float)luaL_checknumber(Ls, base + 2);
    lua_pushboolean(Ls, 1); return 1;
}

static int world_p_add_velocity(lua_State* Ls) {
    int base; uintptr_t pp = world_method_player(Ls, &base);
    if (!pp || !ptr_writable((void*)pp, PLAYER_SIZE)) { lua_pushboolean(Ls, 0); return 1; }
    *(float*)(pp + PLAYER_OFS_VX) += (float)luaL_optnumber(Ls, base + 1, 0.0);
    *(float*)(pp + PLAYER_OFS_VY) += (float)luaL_optnumber(Ls, base + 2, 0.0);
    lua_pushboolean(Ls, 1); return 1;
}

static int world_p_knockback(lua_State* Ls) { return world_p_add_velocity(Ls); }

static int world_p_bounce(lua_State* Ls) {
    int base; uintptr_t pp = world_method_player(Ls, &base);
    if (!pp || !ptr_writable((void*)pp, PLAYER_SIZE)) { lua_pushboolean(Ls, 0); return 1; }
    /* default to a solid upward launch if no value given (engine: -y is up) */
    *(float*)(pp + PLAYER_OFS_VY) = (float)luaL_optnumber(Ls, base + 1, -6.0);
    lua_pushboolean(Ls, 1); return 1;
}

static int world_p_set_facing(lua_State* Ls) {
    int base; uintptr_t pp = world_method_player(Ls, &base);
    int sign;
    if (!pp || !ptr_writable((void*)pp, PLAYER_SIZE)) { lua_pushboolean(Ls, 0); return 1; }
    sign = (int)luaL_checkinteger(Ls, base + 1);
    *(signed char*)(pp + PLAYER_OFS_FACING_SIGN) = (signed char)(sign >= 0 ? 1 : -1);
    lua_pushboolean(Ls, 1); return 1;
}

static int world_p_set_has_sword(lua_State* Ls) {
    int base; uintptr_t pp = world_method_player(Ls, &base);
    int want;
    if (!pp || !ptr_writable((void*)pp, PLAYER_SIZE)) { lua_pushboolean(Ls, 0); return 1; }
    want = lua_toboolean(Ls, base + 1);
    /* native semantics: byte == 0 means "has sword" */
    *(uint8_t*)(pp + PLAYER_OFS_HAS_SWORD) = want ? 0 : 1;
    lua_pushboolean(Ls, 1); return 1;
}

static int world_p_kill(lua_State* Ls) {
    int base; uintptr_t pp = world_method_player(Ls, &base);
    (void)base;
    if (!pp) { lua_pushboolean(Ls, 0); return 1; }
    if (!p_player_die || IsBadCodePtr((FARPROC)(void*)p_player_die)) { lua_pushboolean(Ls, 0); return 1; }
    p_player_die((int)pp);
    lua_pushboolean(Ls, 1); return 1;
}

/* Build a player handle table (live-read fields + write methods) on the stack. */
static void world_push_player_handle(lua_State* Ls, int idx) {
    uintptr_t pp;
    idx &= 1;
    pp = game_get_player_ptr(idx);

    lua_newtable(Ls);
    lua_push_field_int(Ls, "index", idx);

    if (pp && ptr_readable((const void*)pp, PLAYER_SIZE)) {
        uint8_t cf = *(uint8_t*)(pp + PLAYER_OFS_COLLISION_FLAGS);
        lua_push_field_number(Ls, "x",  (double)*(float*)(pp + PLAYER_OFS_X));
        lua_push_field_number(Ls, "y",  (double)*(float*)(pp + PLAYER_OFS_Y));
        lua_push_field_number(Ls, "prev_x", (double)*(float*)(pp + PLAYER_OFS_PREV_X));
        lua_push_field_number(Ls, "prev_y", (double)*(float*)(pp + PLAYER_OFS_PREV_Y));
        lua_push_field_number(Ls, "vx", (double)*(float*)(pp + PLAYER_OFS_VX));
        lua_push_field_number(Ls, "vy", (double)*(float*)(pp + PLAYER_OFS_VY));
        lua_push_field_int(Ls, "facing", (int)*(signed char*)(pp + PLAYER_OFS_FACING_SIGN));
        lua_push_field_int(Ls, "room", (int)*(signed char*)(pp + PLAYER_OFS_ROOM));
        lua_push_field_int(Ls, "state_id", (int)*(uint8_t*)(pp + PLAYER_OFS_STATE_ID));
        lua_push_field_int(Ls, "cmd_bits", (int)*(uint8_t*)(pp + PLAYER_OFS_CMD_BITS));
        lua_push_field_int(Ls, "prev_cmd_bits", (int)*(uint8_t*)(pp + PLAYER_OFS_PREV_CMD_BITS));
        lua_push_field_bool(Ls, "has_sword", (*(uint8_t*)(pp + PLAYER_OFS_HAS_SWORD) == 0));
        lua_push_field_bool(Ls, "grounded",   (cf & PLAYER_COLLIDE_GROUNDED) != 0);
        lua_push_field_bool(Ls, "ceiling",    (cf & PLAYER_COLLIDE_CEILING) != 0);
        lua_push_field_bool(Ls, "wall_right", (cf & PLAYER_COLLIDE_WALL_RIGHT) != 0);
        lua_push_field_bool(Ls, "wall_left",  (cf & PLAYER_COLLIDE_WALL_LEFT) != 0);
        lua_push_field_bool(Ls, "valid", 1);
    } else {
        lua_push_field_bool(Ls, "valid", 0);
    }

    /* write methods (player index carried as upvalue) */
    #define WORLD_BIND_METHOD(name, fn) \
        lua_pushinteger(Ls, idx); lua_pushcclosure(Ls, fn, 1); lua_setfield(Ls, -2, name)
    WORLD_BIND_METHOD("set_pos",      world_p_set_pos);
    WORLD_BIND_METHOD("teleport",     world_p_teleport);
    WORLD_BIND_METHOD("set_velocity", world_p_set_velocity);
    WORLD_BIND_METHOD("add_velocity", world_p_add_velocity);
    WORLD_BIND_METHOD("knockback",    world_p_knockback);
    WORLD_BIND_METHOD("bounce",       world_p_bounce);
    WORLD_BIND_METHOD("set_facing",   world_p_set_facing);
    WORLD_BIND_METHOD("set_has_sword", world_p_set_has_sword);
    WORLD_BIND_METHOD("kill",         world_p_kill);
    WORLD_BIND_METHOD("hurt",         world_p_kill);
    #undef WORLD_BIND_METHOD
}

static int lua_world_player(lua_State* Ls) {
    int idx = (int)luaL_optinteger(Ls, 1, 0) & 1;
    world_push_player_handle(Ls, idx);
    return 1;
}

static int lua_world_online_active(lua_State* Ls) {
    lua_pushboolean(Ls, ggpo_net_active());
    return 1;
}

static void world_push_tile_table(lua_State* Ls, int id, int solid, double x, double y) {
    lua_newtable(Ls);
    lua_push_field_int(Ls, "id", id);
    lua_push_field_bool(Ls, "solid", solid);
    lua_push_field_number(Ls, "x", x);
    lua_push_field_number(Ls, "y", y);
}

static int lua_world_tile_at_world(lua_State* Ls) {
    float x = (float)luaL_checknumber(Ls, 1);
    float y = (float)luaL_checknumber(Ls, 2);
    int id = world_tile_id_at(x, y);
    if (id < 0) { lua_pushnil(Ls); return 1; }
    world_push_tile_table(Ls, id, world_tile_is_solid(x, y), (double)x, (double)y);
    return 1;
}

static int lua_world_player_tile(lua_State* Ls) {
    int idx = (int)luaL_optinteger(Ls, 1, 0) & 1;
    uintptr_t pp = game_get_player_ptr(idx);
    float x, y; int id;
    if (!pp || !ptr_readable((const void*)pp, PLAYER_SIZE)) { lua_pushnil(Ls); return 1; }
    x = *(float*)(pp + PLAYER_OFS_X);
    y = *(float*)(pp + PLAYER_OFS_Y);
    id = world_tile_id_at(x, y);
    if (id < 0) { lua_pushnil(Ls); return 1; }
    world_push_tile_table(Ls, id, world_tile_is_solid(x, y), (double)x, (double)y);
    return 1;
}

/* Resolve a glyph (string) or raw id (number) argument to a tile type, or -1. */
static int world_resolve_type_arg(lua_State* Ls, int idx) {
    if (lua_isnumber(Ls, idx)) return (int)lua_tointeger(Ls, idx);
    if (lua_isstring(Ls, idx)) {
        const char* s = lua_tostring(Ls, idx);
        return (s && s[0]) ? world_glyph_to_type(s[0]) : -1;
    }
    return -1;
}

/* Copy the engine tile-property row (solid/deadly/etc, 0x2C bytes at 0x55AB44)
 * from one tile type to another. Making a custom kill tile behave like spikes
 * ('X') gives it native deadliness AND spawn-avoidance (the engine's
 * find_good_spot won't respawn players onto a deadly tile) for free. */
static int lua_world_copy_tile_props(lua_State* Ls) {
    int from = world_resolve_type_arg(Ls, 1);
    int to   = world_resolve_type_arg(Ls, 2);
    unsigned char* base = (unsigned char*)(uintptr_t)0x55AB44u;
    unsigned char* src;
    unsigned char* dst;
    if (from < 0 || from > 255 || to < 0 || to > 255) { lua_pushboolean(Ls, 0); return 1; }
    src = base + (size_t)from * 0x2Cu;
    dst = base + (size_t)to   * 0x2Cu;
    if (IsBadReadPtr(src, 0x2C) || IsBadWritePtr(dst, 0x2C)) { lua_pushboolean(Ls, 0); return 1; }
    memcpy(dst, src, 0x2C);
    lua_pushboolean(Ls, 1);
    return 1;
}

/* Camera + view dimensions, so Lua can project world->screen (kept in Lua so the
 * projection can be tuned without a rebuild). */
static int lua_world_view(lua_State* Ls) {
    volatile float* gh = (volatile float*)(uintptr_t)ADDR_GAME_H;
    lua_newtable(Ls);
    lua_push_field_number(Ls, "cam_x", p_camera_x ? (double)*p_camera_x : 0.0);
    lua_push_field_number(Ls, "cam_y", p_camera_y ? (double)*p_camera_y : 0.0);
    lua_push_field_number(Ls, "game_w", p_game_w ? (double)*p_game_w : 0.0);
    lua_push_field_number(Ls, "game_h", (double)*gh);
    lua_push_field_number(Ls, "ui_w", (double)*(float*)(uintptr_t)ADDR_MAD_W);
    lua_push_field_number(Ls, "ui_h", (double)*(float*)(uintptr_t)ADDR_MAD_H);
    lua_push_field_int(Ls, "tile_w", p_tile_w_native ? *p_tile_w_native : 0);
    lua_push_field_int(Ls, "tile_h", p_tile_h_native ? *p_tile_h_native : 0);
    return 1;
}

/* World-space centers of every tile of a given glyph/type in the current room.
 * Returns an array of { x = , y = } (world pixels). */
static int lua_world_find_tiles(lua_State* Ls) {
    int want = world_resolve_type_arg(Ls, 1);
    int tw = p_tile_w_native ? *p_tile_w_native : 16;
    int th = p_tile_h_native ? *p_tile_h_native : 16;
    int room = p_game_active_room ? *p_game_active_room : 0;
    int rpw = p_room_pixel_w ? *p_room_pixel_w : (33 * tw);
    int room_w = (tw > 0) ? (rpw / tw) : 33;
    int room_h = (p_map_tiles_h && !IsBadCodePtr((FARPROC)(void*)p_map_tiles_h)) ? p_map_tiles_h() : 12;
    int n = 0;

    lua_newtable(Ls);
    if (want < 0 || !p_map_tile || IsBadCodePtr((FARPROC)(void*)p_map_tile) || tw <= 0 || th <= 0) return 1;
    if (room_w <= 0 || room_w > 64) room_w = 33;
    if (room_h <= 0 || room_h > 64) room_h = 12;

    for (int ty = 0; ty < room_h; ty++) {
        for (int tx = 0; tx < room_w; tx++) {
            int gc = room * room_w + tx;
            int tp = p_map_tile(gc, ty);
            if (tp && !IsBadReadPtr((void*)(uintptr_t)tp, 1) &&
                (int)*(unsigned char*)(uintptr_t)tp == want) {
                lua_newtable(Ls);
                lua_push_field_number(Ls, "x", (double)gc * tw + tw * 0.5);
                lua_push_field_number(Ls, "y", (double)ty * th + th * 0.5);
                lua_rawseti(Ls, -2, ++n);
            }
        }
    }
    return 1;
}

static void world_clear_behavior_slot(int b) {
    if (b < 0 || b >= WORLD_MAX_TILE_BEHAVIORS) return;
    /* luaL_ref only ever returns positive refs; 0 is the registry free-list
       sentinel and negatives are LUA_NOREF/LUA_REFNIL, so only unref > 0. */
    if (L) {
        if (g_tile_behaviors[b].on_enter_ref > 0) luaL_unref(L, LUA_REGISTRYINDEX, g_tile_behaviors[b].on_enter_ref);
        if (g_tile_behaviors[b].on_stay_ref  > 0) luaL_unref(L, LUA_REGISTRYINDEX, g_tile_behaviors[b].on_stay_ref);
        if (g_tile_behaviors[b].on_exit_ref  > 0) luaL_unref(L, LUA_REGISTRYINDEX, g_tile_behaviors[b].on_exit_ref);
    }
    memset(&g_tile_behaviors[b], 0, sizeof(g_tile_behaviors[b]));
    g_tile_behaviors[b].on_enter_ref = LUA_NOREF;
    g_tile_behaviors[b].on_stay_ref  = LUA_NOREF;
    g_tile_behaviors[b].on_exit_ref  = LUA_NOREF;
    g_tile_overlap_prev[0][b] = 0;
    g_tile_overlap_prev[1][b] = 0;
}

static void world_recount_behaviors(void) {
    int n = 0;
    for (int i = 0; i < WORLD_MAX_TILE_BEHAVIORS; i++) if (g_tile_behaviors[i].used) n++;
    g_tile_behavior_active = n;
}

static void lua_manager_clear_mod_tiles(int mod_index) {
    for (int b = 0; b < WORLD_MAX_TILE_BEHAVIORS; b++) {
        if (g_tile_behaviors[b].used && g_tile_behaviors[b].mod_index == mod_index) {
            world_clear_behavior_slot(b);
        }
    }
    world_recount_behaviors();
}

static int world_take_callback_ref(lua_State* Ls, int tbl, const char* field) {
    int ref = LUA_NOREF;
    lua_getfield(Ls, tbl, field);
    if (lua_isfunction(Ls, -1)) ref = luaL_ref(Ls, LUA_REGISTRYINDEX);
    else lua_pop(Ls, 1);
    return ref;
}

static int lua_game_register_tile(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    int id = -1, match_solid = 0, slot = -1;
    char glyph = 0;
    int my_mod_index = mod ? (int)(mod - g_mods) : -1;

    luaL_checktype(Ls, 1, LUA_TTABLE);

    /* Preferred: identify the tile by the glyph the map author places. */
    lua_getfield(Ls, 1, "glyph");
    if (lua_isstring(Ls, -1)) {
        const char* gs = lua_tostring(Ls, -1);
        if (gs && gs[0]) {
            glyph = gs[0];
            id = world_glyph_to_type(glyph);
            if (id < 0) { lua_pop(Ls, 1); return luaL_error(Ls, "register_tile: unknown glyph '%c'", glyph); }
        }
    }
    lua_pop(Ls, 1);

    /* Fallback: raw engine tile id, or "any solid". */
    if (id < 0) {
        lua_getfield(Ls, 1, "id");
        if (lua_isnil(Ls, -1)) { lua_pop(Ls, 1); lua_getfield(Ls, 1, "tile_id"); }
        if (lua_isnumber(Ls, -1)) id = (int)lua_tointeger(Ls, -1);
        lua_pop(Ls, 1);
    }

    lua_getfield(Ls, 1, "solid");
    match_solid = lua_toboolean(Ls, -1);
    lua_pop(Ls, 1);

    if (id < 0 && !match_solid) {
        return luaL_error(Ls, "register_tile: needs a 'glyph', an integer 'id' (0..255), or solid=true");
    }

    /* Conflict detection: warn if a different enabled mod already claimed this
     * tile type. Behaviors aren't hard-blocked (a map author may intend both),
     * but the conflict is surfaced as "<modA> vs <modB>" so it can be resolved. */
    if (!match_solid) {
        for (int b = 0; b < WORLD_MAX_TILE_BEHAVIORS; b++) {
            if (g_tile_behaviors[b].used && !g_tile_behaviors[b].match_solid &&
                g_tile_behaviors[b].id == id && g_tile_behaviors[b].mod_index != my_mod_index) {
                const char* other = (g_tile_behaviors[b].mod_index >= 0 && g_tile_behaviors[b].mod_index < g_mod_count)
                                    ? g_mods[g_tile_behaviors[b].mod_index].id : "?";
                LOG_WARN("tile conflict: %s:%c and %s:%c both bind tile id %d (both will fire)",
                         mod ? mod->id : "?", glyph ? glyph : '#',
                         other, g_tile_behaviors[b].glyph ? g_tile_behaviors[b].glyph : '#', id);
                break;
            }
        }
    }

    for (int b = 0; b < WORLD_MAX_TILE_BEHAVIORS; b++) {
        if (!g_tile_behaviors[b].used) { slot = b; break; }
    }
    if (slot < 0) return luaL_error(Ls, "register_tile: too many tile behaviors (max %d)", WORLD_MAX_TILE_BEHAVIORS);

    world_clear_behavior_slot(slot);
    g_tile_behaviors[slot].used = 1;
    g_tile_behaviors[slot].mod_index = my_mod_index;
    g_tile_behaviors[slot].id = id;
    g_tile_behaviors[slot].match_solid = match_solid;
    g_tile_behaviors[slot].glyph = glyph;
    g_tile_behaviors[slot].on_enter_ref = world_take_callback_ref(Ls, 1, "on_enter");
    g_tile_behaviors[slot].on_stay_ref  = world_take_callback_ref(Ls, 1, "on_stay");
    g_tile_behaviors[slot].on_exit_ref  = world_take_callback_ref(Ls, 1, "on_exit");
    world_recount_behaviors();

    lua_pushboolean(Ls, 1);
    return 1;
}

/* Fire one tile callback: cb(player_handle, tile_table). */
static void world_fire_tile_cb(int ref, int player_idx, int tile_id, int solid, double x, double y) {
    if (ref <= 0 || !L) return;  /* >0 == a real luaL_ref; 0/neg == none */
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    world_push_player_handle(L, player_idx);
    world_push_tile_table(L, tile_id, solid, x, y);
    if (lua_pcall(L, 2, 0, 0) != 0) {
        const char* err = lua_tostring(L, -1);
        LOG_WARN("tile behavior callback error: %s", err ? err : "(unknown)");
        lua_pop(L, 1);
    }
}

/* Per-tick dispatch of registered tile behaviors. Called from on_tick_post
 * (after the native player update) so bounce/spring velocity writes survive. */
static void lua_manager_dispatch_tiles(void) {
    int th = 16;
    if (!L || g_tile_behavior_active <= 0) return;

    /* Only while actually in gameplay. */
    if (!ui_state_matches_name(ui_current_state_ptr(), "game")) {
        memset(g_tile_overlap_prev, 0, sizeof(g_tile_overlap_prev));
        return;
    }
    /* Local-first: never mutate world from custom tiles during an online match. */
    if (ggpo_net_active()) {
        if (!g_world_online_warned) {
            LOG_WARN("custom tile behaviors are disabled during online matches (local-first; v1)");
            g_world_online_warned = 1;
        }
        memset(g_tile_overlap_prev, 0, sizeof(g_tile_overlap_prev));
        return;
    }

    if (p_tile_h_native && ptr_readable((const void*)p_tile_h_native, sizeof(int)) && *p_tile_h_native > 0) {
        th = *p_tile_h_native;
    }

    for (int pi = 0; pi < 2; pi++) {
        uintptr_t pp = game_get_player_ptr(pi);
        float x, y;
        int ids[3];
        int solid_any = 0;
        int nids = 0;

        if (!pp || !ptr_readable((const void*)pp, PLAYER_SIZE)) {
            for (int b = 0; b < WORLD_MAX_TILE_BEHAVIORS; b++) g_tile_overlap_prev[pi][b] = 0;
            continue;
        }
        x = *(float*)(pp + PLAYER_OFS_X);
        y = *(float*)(pp + PLAYER_OFS_Y);

        /* Sample the tile at the player point and just below the feet so behavior
         * fires both when touching and when standing on top of a tile. */
        ids[nids++] = world_tile_id_at(x, y);
        ids[nids++] = world_tile_id_at(x, y + (float)th * 0.6f);
        ids[nids++] = world_tile_id_at(x, y - (float)th * 0.4f);
        if (world_tile_is_solid(x, y + (float)th * 0.6f)) solid_any = 1;

        for (int b = 0; b < WORLD_MAX_TILE_BEHAVIORS; b++) {
            int now, prev;
            if (!g_tile_behaviors[b].used) { g_tile_overlap_prev[pi][b] = 0; continue; }

            now = 0;
            if (g_tile_behaviors[b].match_solid) {
                now = solid_any;
            } else {
                for (int k = 0; k < nids; k++) {
                    if (ids[k] == g_tile_behaviors[b].id) { now = 1; break; }
                }
            }
            prev = g_tile_overlap_prev[pi][b];

            if (now && !prev) {
                world_fire_tile_cb(g_tile_behaviors[b].on_enter_ref, pi, g_tile_behaviors[b].id, solid_any, (double)x, (double)y);
            } else if (now && prev) {
                world_fire_tile_cb(g_tile_behaviors[b].on_stay_ref, pi, g_tile_behaviors[b].id, solid_any, (double)x, (double)y);
            } else if (!now && prev) {
                world_fire_tile_cb(g_tile_behaviors[b].on_exit_ref, pi, g_tile_behaviors[b].id, solid_any, (double)x, (double)y);
            }
            g_tile_overlap_prev[pi][b] = (unsigned char)now;
        }
    }
}

static void push_world_api_table(lua_State* Ls, LoadedMod* mod) {
    (void)mod;
    lua_newtable(Ls);
    lua_pushcfunction(Ls, lua_world_player);         lua_setfield(Ls, -2, "player");
    lua_pushcfunction(Ls, lua_world_tile_at_world);  lua_setfield(Ls, -2, "tile_at_world");
    lua_pushcfunction(Ls, lua_world_player_tile);    lua_setfield(Ls, -2, "player_tile");
    lua_pushcfunction(Ls, lua_world_online_active);  lua_setfield(Ls, -2, "online_active");
    lua_pushcfunction(Ls, lua_world_copy_tile_props); lua_setfield(Ls, -2, "copy_tile_props");
    lua_pushcfunction(Ls, lua_world_view);           lua_setfield(Ls, -2, "view");
    lua_pushcfunction(Ls, lua_world_find_tiles);     lua_setfield(Ls, -2, "find_tiles");
}

static void push_game_api_table(lua_State* Ls, LoadedMod* mod) {
    lua_newtable(Ls);
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_game_snapshot, 1);       lua_setfield(Ls, -2, "snapshot");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_game_sword_snapshot, 1); lua_setfield(Ls, -2, "sword_snapshot");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_game_entities, 1);       lua_setfield(Ls, -2, "entities");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_game_tick_count, 1);     lua_setfield(Ls, -2, "tick_count");
    lua_pushcfunction(Ls, lua_game_native_tick);                                       lua_setfield(Ls, -2, "native_tick");
    lua_pushcfunction(Ls, lua_game_set_native_tick);                                   lua_setfield(Ls, -2, "set_native_tick");
    lua_pushcfunction(Ls, lua_game_rng_seed);                                          lua_setfield(Ls, -2, "rng_seed");
    lua_pushcfunction(Ls, lua_game_set_rng_seed);                                      lua_setfield(Ls, -2, "set_rng_seed");
    lua_pushcfunction(Ls, lua_game_native_state);                                      lua_setfield(Ls, -2, "native_state");
    lua_pushcfunction(Ls, lua_game_player_colour);                                     lua_setfield(Ls, -2, "player_colour");
    lua_pushcfunction(Ls, lua_game_player_colour);                                     lua_setfield(Ls, -2, "player_color");
    lua_pushcfunction(Ls, lua_game_player_colour_index);                               lua_setfield(Ls, -2, "player_colour_index");
    lua_pushcfunction(Ls, lua_game_player_colour_index);                               lua_setfield(Ls, -2, "player_color_index");
    lua_pushcfunction(Ls, lua_game_set_player_colour_index);                           lua_setfield(Ls, -2, "set_player_colour_index");
    lua_pushcfunction(Ls, lua_game_set_player_colour_index);                           lua_setfield(Ls, -2, "set_player_color_index");
    lua_pushcfunction(Ls, lua_game_set_player_render_colours);                         lua_setfield(Ls, -2, "set_player_render_colours");
    lua_pushcfunction(Ls, lua_game_set_player_render_colours);                         lua_setfield(Ls, -2, "set_player_render_colors");
    lua_pushcfunction(Ls, lua_game_set_player_body_hidden);                             lua_setfield(Ls, -2, "set_player_body_hidden");
    lua_pushcfunction(Ls, lua_game_player_body_hidden);                                 lua_setfield(Ls, -2, "player_body_hidden");
    lua_pushcfunction(Ls, lua_game_set_player_sword_idle_offset);                       lua_setfield(Ls, -2, "set_player_sword_idle_offset");
    lua_pushcfunction(Ls, lua_game_state_checksum);                                    lua_setfield(Ls, -2, "state_checksum");
    lua_pushcfunction(Ls, lua_game_full_state_blob);                                   lua_setfield(Ls, -2, "full_state_blob");
    lua_pushcfunction(Ls, lua_game_apply_full_state_blob);                             lua_setfield(Ls, -2, "apply_full_state_blob");
    lua_pushcfunction(Ls, lua_game_full_state_size);                                   lua_setfield(Ls, -2, "full_state_size");
    lua_pushcfunction(Ls, lua_game_block_next_tick);                                   lua_setfield(Ls, -2, "block_next_tick");
    lua_pushcfunction(Ls, lua_game_simulate_ticks);                                    lua_setfield(Ls, -2, "simulate_ticks");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_game_poll_cmds, 1);      lua_setfield(Ls, -2, "poll_cmds");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_game_poll_cmds_raw, 1);  lua_setfield(Ls, -2, "poll_cmds_raw");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_game_set_input, 1);      lua_setfield(Ls, -2, "set_input");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_game_input_override, 1); lua_setfield(Ls, -2, "input_override");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_game_input_clear, 1);    lua_setfield(Ls, -2, "input_clear");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_game_input_status, 1);   lua_setfield(Ls, -2, "input_status");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_game_block_raw_input, 1); lua_setfield(Ls, -2, "block_raw_input");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_game_apply_snapshot, 1); lua_setfield(Ls, -2, "apply_snapshot");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_game_apply_sword_snapshot, 1); lua_setfield(Ls, -2, "apply_sword_snapshot");
    lua_game_push_command_constants(Ls);
    lua_pushcfunction(Ls, lua_game_camera);                                              lua_setfield(Ls, -2, "camera");
    lua_pushcfunction(Ls, lua_game_is_solid);                                            lua_setfield(Ls, -2, "is_solid");
    lua_pushcfunction(Ls, lua_game_is_solid);                                            lua_setfield(Ls, -2, "is_pos_solid");
    /* map selector (online mod) */
    lua_pushcfunction(Ls, lua_game_set_map_selector); lua_setfield(Ls, -2, "set_map_selector");
    lua_pushcfunction(Ls, lua_game_get_map_selector); lua_setfield(Ls, -2, "get_map_selector");

    /* World Control (custom content): live world read/write + tile behaviors. */
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_game_register_tile, 1); lua_setfield(Ls, -2, "register_tile");
    push_world_api_table(Ls, mod);
    lua_setfield(Ls, -2, "world");
}

static int lua_online_status(lua_State* Ls) {
    lua_newtable(Ls);
    lua_pushboolean(Ls, ggpo_net_active()); lua_setfield(Ls, -2, "active");
    lua_pushboolean(Ls, ggpo_net_connected()); lua_setfield(Ls, -2, "connected");
    lua_pushstring(Ls, ggpo_net_mode_name()); lua_setfield(Ls, -2, "mode");
    lua_pushinteger(Ls, ggpo_net_local_player()); lua_setfield(Ls, -2, "local_player");
    lua_pushinteger(Ls, ggpo_net_remote_player()); lua_setfield(Ls, -2, "remote_player");
    lua_pushboolean(Ls, ggpo_net_state_synced()); lua_setfield(Ls, -2, "state_synced");
    lua_pushboolean(Ls, ggpo_net_remote_state_synced()); lua_setfield(Ls, -2, "remote_state_synced");
    lua_pushboolean(Ls, ggpo_net_start_state_loaded()); lua_setfield(Ls, -2, "start_state_loaded");
    lua_pushinteger(Ls, ggpo_net_local_cosmetic_profile_revision()); lua_setfield(Ls, -2, "local_cosmetic_revision");
    lua_pushinteger(Ls, ggpo_net_remote_cosmetic_profile_revision()); lua_setfield(Ls, -2, "remote_cosmetic_revision");
    lua_pushinteger(Ls, ggpo_net_remote_cosmetic_profile_applied_revision()); lua_setfield(Ls, -2, "remote_cosmetic_applied_revision");
    lua_pushinteger(Ls, ggpo_net_local_cosmetic_asset_revision()); lua_setfield(Ls, -2, "local_cosmetic_asset_revision");
    lua_pushinteger(Ls, ggpo_net_remote_cosmetic_asset_revision()); lua_setfield(Ls, -2, "remote_cosmetic_asset_revision");
    lua_pushinteger(Ls, ggpo_net_remote_cosmetic_asset_applied_revision()); lua_setfield(Ls, -2, "remote_cosmetic_asset_applied_revision");
    return 1;
}

static int lua_online_set_cosmetic_profile(lua_State* Ls) {
    size_t len = 0;
    const char* profile = luaL_checklstring(Ls, 1, &len);
    if (!ggpo_net_set_local_cosmetic_profile(profile, len)) {
        lua_pushboolean(Ls, 0);
        lua_pushfstring(Ls, "cosmetic profile must be %d bytes or less", GGPO_NET_COSMETIC_PROFILE_BYTES);
        return 2;
    }
    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_online_remote_cosmetic_profile(lua_State* Ls) {
    size_t len = 0;
    uint32_t revision = 0;
    const char* profile = ggpo_net_remote_cosmetic_profile(&len, &revision);
    if (!profile || len == 0) {
        lua_pushnil(Ls);
        lua_pushinteger(Ls, revision);
        return 2;
    }
    lua_pushlstring(Ls, profile, len);
    lua_pushinteger(Ls, revision);
    return 2;
}

static int lua_online_mark_cosmetic_profile_applied(lua_State* Ls) {
    uint32_t revision = (uint32_t)luaL_checkinteger(Ls, 1);
    ggpo_net_mark_remote_cosmetic_profile_applied(revision);
    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_online_set_cosmetic_asset(lua_State* Ls) {
    size_t id_len = 0;
    size_t data_len = 0;
    const char* asset_id = luaL_optlstring(Ls, 1, "", &id_len);
    const char* data = luaL_optlstring(Ls, 2, "", &data_len);
    if (id_len == 0 || data_len == 0) {
        lua_pushboolean(Ls, ggpo_net_set_local_cosmetic_asset(NULL, NULL, 0));
        return 1;
    }
    if (!ggpo_net_set_local_cosmetic_asset(asset_id, data, data_len)) {
        lua_pushboolean(Ls, 0);
        lua_pushfstring(Ls, "cosmetic asset must be %d bytes or less and have a short id", GGPO_NET_COSMETIC_ASSET_MAX_BYTES);
        return 2;
    }
    lua_pushboolean(Ls, 1);
    return 1;
}

static int lua_online_remote_cosmetic_asset(lua_State* Ls) {
    const char* asset_id = NULL;
    size_t len = 0;
    uint32_t revision = 0;
    const void* data = ggpo_net_remote_cosmetic_asset(&asset_id, &len, &revision);
    if (!data || len == 0) {
        lua_pushnil(Ls);
        lua_pushnil(Ls);
        lua_pushinteger(Ls, revision);
        return 3;
    }
    lua_pushstring(Ls, asset_id ? asset_id : "");
    lua_pushlstring(Ls, (const char*)data, len);
    lua_pushinteger(Ls, revision);
    return 3;
}

static int lua_online_mark_cosmetic_asset_applied(lua_State* Ls) {
    uint32_t revision = (uint32_t)luaL_checkinteger(Ls, 1);
    ggpo_net_mark_remote_cosmetic_asset_applied(revision);
    lua_pushboolean(Ls, 1);
    return 1;
}

static void push_online_api_table(lua_State* Ls) {
    lua_newtable(Ls);
    lua_pushcfunction(Ls, lua_online_status); lua_setfield(Ls, -2, "status");
    lua_pushcfunction(Ls, lua_online_set_cosmetic_profile); lua_setfield(Ls, -2, "set_cosmetic_profile");
    lua_pushcfunction(Ls, lua_online_remote_cosmetic_profile); lua_setfield(Ls, -2, "remote_cosmetic_profile");
    lua_pushcfunction(Ls, lua_online_mark_cosmetic_profile_applied); lua_setfield(Ls, -2, "mark_cosmetic_profile_applied");
    lua_pushcfunction(Ls, lua_online_set_cosmetic_asset); lua_setfield(Ls, -2, "set_cosmetic_asset");
    lua_pushcfunction(Ls, lua_online_remote_cosmetic_asset); lua_setfield(Ls, -2, "remote_cosmetic_asset");
    lua_pushcfunction(Ls, lua_online_mark_cosmetic_asset_applied); lua_setfield(Ls, -2, "mark_cosmetic_asset_applied");
}

static int lua_fs_pick_character_file(lua_State* Ls) {
    const char* title = luaL_optstring(Ls, 1, "Import character package");
    char path[MAX_PATH];
    OPENFILENAMEA ofn;
    memset(path, 0, sizeof(path));
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = path;
    ofn.nMaxFile = sizeof(path);
    ofn.lpstrTitle = title;
    ofn.lpstrFilter =
        "Character Packages (*.zip;*.json)\0*.zip;*.json\0"
        "ZIP Files (*.zip)\0*.zip\0"
        "JSON Files (*.json)\0*.json\0"
        "All Files (*.*)\0*.*\0\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn)) {
        lua_pushstring(Ls, path);
        return 1;
    }
    lua_pushnil(Ls);
    lua_pushstring(Ls, "cancelled");
    return 2;
}

static int lua_fs_pick_folder(lua_State* Ls) {
    const char* title = luaL_optstring(Ls, 1, "Import character folder");
    char path[MAX_PATH];
    BROWSEINFOA bi;
    LPITEMIDLIST pidl;
    memset(path, 0, sizeof(path));
    memset(&bi, 0, sizeof(bi));
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    pidl = SHBrowseForFolderA(&bi);
    if (pidl) {
        int ok = SHGetPathFromIDListA(pidl, path);
        CoTaskMemFree(pidl);
        if (ok && path[0]) {
            lua_pushstring(Ls, path);
            return 1;
        }
    }
    lua_pushnil(Ls);
    lua_pushstring(Ls, "cancelled");
    return 2;
}

static int fs_find_file_recursive(const char* dir, const char* name, char* out, size_t out_cap, int depth) {
    char pattern[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    size_t dir_len;
    if (!dir || !name || !out || out_cap == 0 || depth > 12) return 0;
    dir_len = strlen(dir);
    if (dir_len == 0 || dir_len + 3 >= sizeof(pattern)) return 0;
    snprintf(pattern, sizeof(pattern), "%s%s*", dir, (dir[dir_len - 1] == '\\' || dir[dir_len - 1] == '/') ? "" : "\\");
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        char child[MAX_PATH];
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        if (dir_len + strlen(fd.cFileName) + 2 >= sizeof(child)) continue;
        snprintf(child, sizeof(child), "%s%s%s", dir, (dir[dir_len - 1] == '\\' || dir[dir_len - 1] == '/') ? "" : "\\", fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fs_find_file_recursive(child, name, out, out_cap, depth + 1)) {
                FindClose(h);
                return 1;
            }
        } else if (_stricmp(fd.cFileName, name) == 0) {
            snprintf(out, out_cap, "%s", child);
            FindClose(h);
            return 1;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return 0;
}

static int lua_fs_find_file(lua_State* Ls) {
    const char* root = luaL_checkstring(Ls, 1);
    const char* name = luaL_optstring(Ls, 2, "character.json");
    char out[MAX_PATH];
    out[0] = '\0';
    if (fs_find_file_recursive(root, name, out, sizeof(out), 0)) {
        lua_pushstring(Ls, out);
        return 1;
    }
    lua_pushnil(Ls);
    lua_pushstring(Ls, "not found");
    return 2;
}

static void push_fs_api_table(lua_State* Ls) {
    lua_newtable(Ls);
    lua_pushcfunction(Ls, lua_fs_pick_character_file); lua_setfield(Ls, -2, "pick_character_file");
    lua_pushcfunction(Ls, lua_fs_pick_folder); lua_setfield(Ls, -2, "pick_folder");
    lua_pushcfunction(Ls, lua_fs_find_file); lua_setfield(Ls, -2, "find_file");
}

static void push_font_api_table(lua_State* Ls, LoadedMod* mod) {
    lua_newtable(Ls);
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_font_loaded, 1);         lua_setfield(Ls, -2, "font_loaded");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_font_alloc_glyph, 1);    lua_setfield(Ls, -2, "alloc_glyph");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_font_register_glyph, 1); lua_setfield(Ls, -2, "register_glyph");
}

static void push_texture_api_table(lua_State* Ls, LoadedMod* mod) {
    lua_newtable(Ls);
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_texture_loaded, 1);               lua_setfield(Ls, -2, "loaded");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_texture_register, 1);             lua_setfield(Ls, -2, "register");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_texture_sprites_loaded, 1);       lua_setfield(Ls, -2, "sprites_loaded");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_texture_register_spritesheet, 1); lua_setfield(Ls, -2, "register_spritesheet");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_texture_register_tilesheet, 1);   lua_setfield(Ls, -2, "register_tilesheet");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_texture_register_miscsheet, 1);   lua_setfield(Ls, -2, "register_miscsheet");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_texture_register_glowsheet, 1);   lua_setfield(Ls, -2, "register_glowsheet");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_texture_reload_all, 1);           lua_setfield(Ls, -2, "reload_all");
}

static int lua_mod_on_layout(lua_State* Ls) {
    LoadedMod* mod = mod_from_upvalue(Ls);
    const char* state_name = luaL_checkstring(Ls, 1);
    luaL_checktype(Ls, 2, LUA_TFUNCTION);
    if (!mod || !state_name || !state_name[0]) return 0;

    if (mod->on_layout_count + 1 > mod->on_layout_cap) {
        int newcap = (mod->on_layout_cap == 0) ? 8 : (mod->on_layout_cap * 2);
        LayoutHandler* nh = (LayoutHandler*)realloc(mod->on_layout, sizeof(LayoutHandler) * newcap);
        if (!nh) return 0;
        mod->on_layout = nh;
        mod->on_layout_cap = newcap;
    }
    LayoutHandler* h = &mod->on_layout[mod->on_layout_count++];
    strncpy(h->state_name, state_name, sizeof(h->state_name) - 1);
    h->state_name[sizeof(h->state_name) - 1] = '\0';
    lua_pushvalue(Ls, 2);
    h->ref = luaL_ref(Ls, LUA_REGISTRYINDEX);
    return 0;
}

/* ---- mod.net Lua bindings ----------------------------------------- */

static int lua_net_connect(lua_State *L) {
    const char *host = luaL_checkstring(L, 1);
    int         port = (int)luaL_checkinteger(L, 2);
    int slot = net_connect(host, port);
    if (slot < 0) {
        lua_pushnil(L);
        lua_pushstring(L, "connect failed (no free slot or DNS error)");
        return 2;
    }
    lua_pushinteger(L, slot);
    return 1;
}

static int lua_net_check(lua_State *L) {
    int slot = (int)luaL_checkinteger(L, 1);
    int r    = net_check_connect(slot);
    if      (r ==  1) lua_pushstring(L, "connected");
    else if (r ==  0) lua_pushstring(L, "connecting");
    else              lua_pushstring(L, "failed");
    return 1;
}

static int lua_net_send(lua_State *L) {
    int         slot = (int)luaL_checkinteger(L, 1);
    size_t      len  = 0;
    const char *data = luaL_checklstring(L, 2, &len);
    int r = net_send(slot, data, (int)len);
    if (r < 0) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "send error or disconnected");
        return 2;
    }
    lua_pushboolean(L, 1);
    lua_pushinteger(L, r);
    return 2;
}

static int lua_net_recv(lua_State *L) {
    int  slot = (int)luaL_checkinteger(L, 1);
    char buf[4096];
    int  r    = net_recv(slot, buf, sizeof(buf));
    if (r < 0)  { lua_pushboolean(L, 0); return 1; }
    if (r == 0) { lua_pushnil(L);        return 1; }
    lua_pushlstring(L, buf, (size_t)r);
    return 1;
}

static int lua_net_close(lua_State *L) {
    net_close((int)luaL_checkinteger(L, 1));
    return 0;
}

static int lua_net_connected(lua_State *L) {
    lua_pushboolean(L, net_connected((int)luaL_checkinteger(L, 1)));
    return 1;
}

static int lua_net_connecting(lua_State *L) {
    lua_pushboolean(L, net_connecting((int)luaL_checkinteger(L, 1)));
    return 1;
}

static void push_net_api_table(lua_State *Ls) {
    lua_newtable(Ls);
    lua_pushcfunction(Ls, lua_net_connect);    lua_setfield(Ls, -2, "connect");
    lua_pushcfunction(Ls, lua_net_check);      lua_setfield(Ls, -2, "check");
    lua_pushcfunction(Ls, lua_net_send);       lua_setfield(Ls, -2, "send");
    lua_pushcfunction(Ls, lua_net_recv);       lua_setfield(Ls, -2, "recv");
    lua_pushcfunction(Ls, lua_net_close);      lua_setfield(Ls, -2, "close");
    lua_pushcfunction(Ls, lua_net_connected);  lua_setfield(Ls, -2, "connected");
    lua_pushcfunction(Ls, lua_net_connecting); lua_setfield(Ls, -2, "connecting");
}

/* ---- mod.http: async HTTPS-capable GET via WinHTTP + worker thread ------- */

#define HTTP_MAX_SLOTS 8

typedef struct {
    int            in_use;
    HANDLE         thread;
    volatile LONG  done;      /* 0=pending, 1=ok, -1=error — written by thread */
    char          *body;
    size_t         body_len;
    char           error_msg[256];
    wchar_t        url[2048];
} HttpSlot;

static HttpSlot g_http_slots[HTTP_MAX_SLOTS];

static DWORD WINAPI http_worker_thread(LPVOID param) {
    HttpSlot *slot = (HttpSlot *)param;

    URL_COMPONENTS uc;
    wchar_t host[512] = {0};
    wchar_t path[1024] = {0};
    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize      = sizeof(uc);
    uc.lpszHostName      = host;
    uc.dwHostNameLength  = (DWORD)(sizeof(host) / sizeof(host[0]));
    uc.lpszUrlPath       = path;
    uc.dwUrlPathLength   = (DWORD)(sizeof(path) / sizeof(path[0]));

    if (!WinHttpCrackUrl(slot->url, 0, 0, &uc)) {
        _snprintf(slot->error_msg, sizeof(slot->error_msg) - 1,
            "bad URL (WinHttpCrackUrl err %lu)", GetLastError());
        InterlockedExchange(&slot->done, -1);
        return 0;
    }

    HINTERNET session = WinHttpOpen(
        L"EggnoggPlus/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!session) {
        _snprintf(slot->error_msg, sizeof(slot->error_msg) - 1,
            "WinHttpOpen failed %lu", GetLastError());
        InterlockedExchange(&slot->done, -1);
        return 0;
    }

    /* 10-second resolve+connect timeout */
    DWORD timeout_ms = 10000;
    WinHttpSetOption(session, WINHTTP_OPTION_CONNECT_TIMEOUT,    &timeout_ms, sizeof(timeout_ms));
    WinHttpSetOption(session, WINHTTP_OPTION_RECEIVE_TIMEOUT,    &timeout_ms, sizeof(timeout_ms));
    WinHttpSetOption(session, WINHTTP_OPTION_SEND_TIMEOUT,       &timeout_ms, sizeof(timeout_ms));
    WinHttpSetOption(session, WINHTTP_OPTION_RESOLVE_TIMEOUT,    &timeout_ms, sizeof(timeout_ms));

    INTERNET_PORT port = uc.nPort
        ? uc.nPort
        : (uc.nScheme == INTERNET_SCHEME_HTTPS
            ? INTERNET_DEFAULT_HTTPS_PORT
            : INTERNET_DEFAULT_HTTP_PORT);

    HINTERNET conn = WinHttpConnect(session, host, port, 0);
    if (!conn) {
        _snprintf(slot->error_msg, sizeof(slot->error_msg) - 1,
            "WinHttpConnect failed %lu", GetLastError());
        WinHttpCloseHandle(session);
        InterlockedExchange(&slot->done, -1);
        return 0;
    }

    DWORD req_flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(conn, L"GET",
        path[0] ? path : L"/",
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, req_flags);
    if (!req) {
        _snprintf(slot->error_msg, sizeof(slot->error_msg) - 1,
            "WinHttpOpenRequest failed %lu", GetLastError());
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        InterlockedExchange(&slot->done, -1);
        return 0;
    }

    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(req, NULL)) {
        _snprintf(slot->error_msg, sizeof(slot->error_msg) - 1,
            "request failed %lu", GetLastError());
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        InterlockedExchange(&slot->done, -1);
        return 0;
    }

    /* Verify HTTP status code */
    DWORD status_code = 0;
    DWORD status_size = sizeof(status_code);
    WinHttpQueryHeaders(req,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &status_code, &status_size, WINHTTP_NO_HEADER_INDEX);
    if (status_code != 200) {
        _snprintf(slot->error_msg, sizeof(slot->error_msg) - 1,
            "HTTP %lu", status_code);
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        InterlockedExchange(&slot->done, -1);
        return 0;
    }

    /* Read body incrementally */
    size_t cap = 8192, len = 0;
    char  *buf = (char *)malloc(cap);
    int    ok  = (buf != NULL);

    while (ok) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail)) break;
        if (avail == 0) break;
        if (len + avail + 1 > cap) {
            size_t newcap = (len + avail + 1) * 2;
            char *tmp = (char *)realloc(buf, newcap);
            if (!tmp) { ok = 0; break; }
            buf = tmp;
            cap = newcap;
        }
        DWORD nread = 0;
        if (!WinHttpReadData(req, buf + len, avail, &nread)) { ok = 0; break; }
        len += nread;
    }

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);

    if (ok && buf) {
        buf[len]      = '\0';
        slot->body     = buf;
        slot->body_len = len;
        InterlockedExchange(&slot->done, 1);
    } else {
        free(buf);
        _snprintf(slot->error_msg, sizeof(slot->error_msg) - 1,
            "failed reading response body");
        InterlockedExchange(&slot->done, -1);
    }
    return 0;
}

/* mod.http.get(url_string) → handle_int  or  nil, errmsg */
static int lua_http_get(lua_State *L) {
    const char *url_utf8 = luaL_checkstring(L, 1);

    int idx = -1;
    for (int i = 0; i < HTTP_MAX_SLOTS; i++) {
        if (!g_http_slots[i].in_use) { idx = i; break; }
    }
    if (idx < 0) {
        lua_pushnil(L);
        lua_pushstring(L, "too many concurrent HTTP requests");
        return 2;
    }

    HttpSlot *slot = &g_http_slots[idx];
    memset(slot, 0, sizeof(*slot));

    if (!MultiByteToWideChar(CP_UTF8, 0, url_utf8, -1,
                             slot->url,
                             (int)(sizeof(slot->url) / sizeof(slot->url[0])))) {
        lua_pushnil(L);
        lua_pushstring(L, "URL too long or invalid UTF-8");
        return 2;
    }

    slot->in_use = 1;
    slot->done   = 0;
    slot->thread = CreateThread(NULL, 0, http_worker_thread, slot, 0, NULL);
    if (!slot->thread) {
        slot->in_use = 0;
        lua_pushnil(L);
        lua_pushstring(L, "CreateThread failed");
        return 2;
    }

    lua_pushinteger(L, idx);
    return 1;
}

/* mod.http.poll(handle) → "pending"  |  "done", body  |  "error", msg */
static int lua_http_poll(lua_State *L) {
    int idx = (int)luaL_checkinteger(L, 1);
    if (idx < 0 || idx >= HTTP_MAX_SLOTS || !g_http_slots[idx].in_use) {
        lua_pushstring(L, "error");
        lua_pushstring(L, "invalid handle");
        return 2;
    }
    HttpSlot *slot = &g_http_slots[idx];
    LONG d = InterlockedCompareExchange(&slot->done, 0, 0);  /* atomic read */
    if (d == 0) {
        lua_pushstring(L, "pending");
        return 1;
    }
    if (slot->thread) { CloseHandle(slot->thread); slot->thread = NULL; }
    if (d == 1) {
        lua_pushstring(L, "done");
        lua_pushlstring(L, slot->body ? slot->body : "", slot->body_len);
        free(slot->body);
        slot->body   = NULL;
        slot->in_use = 0;
        return 2;
    }
    lua_pushstring(L, "error");
    lua_pushstring(L, slot->error_msg);
    slot->in_use = 0;
    return 2;
}

/* mod.http.cancel(handle) */
static int lua_http_cancel(lua_State *L) {
    int idx = (int)luaL_checkinteger(L, 1);
    if (idx >= 0 && idx < HTTP_MAX_SLOTS && g_http_slots[idx].in_use) {
        HttpSlot *slot = &g_http_slots[idx];
        /* Thread may still be running — detach it; it will clean up its own
           WinHTTP handles. We just abandon the result buffer. */
        if (slot->thread) { CloseHandle(slot->thread); slot->thread = NULL; }
        if (slot->body)   { free(slot->body); slot->body = NULL; }
        slot->in_use = 0;
    }
    return 0;
}

static void push_http_api_table(lua_State *Ls) {
    lua_newtable(Ls);
    lua_pushcfunction(Ls, lua_http_get);    lua_setfield(Ls, -2, "get");
    lua_pushcfunction(Ls, lua_http_poll);   lua_setfield(Ls, -2, "poll");
    lua_pushcfunction(Ls, lua_http_cancel); lua_setfield(Ls, -2, "cancel");
}

static void push_mod_api_table(lua_State* Ls, LoadedMod* mod) {
    lua_newtable(Ls);

    // Functions
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_mod_on_load,   1); lua_setfield(Ls, -2, "on_load");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_mod_on_unload, 1); lua_setfield(Ls, -2, "on_unload");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_mod_on_frame,  1); lua_setfield(Ls, -2, "on_frame");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_mod_on_tick,   1); lua_setfield(Ls, -2, "on_tick");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_mod_on_tick_post, 1); lua_setfield(Ls, -2, "on_tick_post");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_mod_on_event,  1); lua_setfield(Ls, -2, "on_event");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_mod_on_layout, 1); lua_setfield(Ls, -2, "on_layout");

    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_mod_log,   1); lua_setfield(Ls, -2, "log");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_mod_warn,  1); lua_setfield(Ls, -2, "warn");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_mod_error, 1); lua_setfield(Ls, -2, "error");

    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_mod_info,     1); lua_setfield(Ls, -2, "info");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_mod_get_path, 1); lua_setfield(Ls, -2, "get_path");
    lua_pushlightuserdata(Ls, mod); lua_pushcclosure(Ls, lua_mod_dofile,   1); lua_setfield(Ls, -2, "dofile");

    // UI helpers (immediate mode, usable from on_frame in any game state).
    push_ui_api_table(Ls, mod);
    lua_setfield(Ls, -2, "ui");

    // Gameplay helpers (snapshot + input simulation).
    push_game_api_table(Ls, mod);
    lua_setfield(Ls, -2, "game");

    // Named input bindings.
    push_input_api_table(Ls, mod);
    lua_setfield(Ls, -2, "input");

    // Audio helpers (SFX + music playback from mod assets and built-in IDs).
    push_audio_api_table(Ls, mod);
    lua_setfield(Ls, -2, "audio");

    // Font glyph extension (for "VS " .. string.char(byte) style icons).
    push_font_api_table(Ls, mod);
    lua_setfield(Ls, -2, "font");

    // Texture replacement helpers (resource-pack style sprite swaps).
    push_texture_api_table(Ls, mod);
    lua_setfield(Ls, -2, "texture");

    // Mod-owned PNG spritesheets for UI icons and small overlay assets.
    push_assets_api_table(Ls, mod);
    lua_setfield(Ls, -2, "assets");

    // Persistent key/value storage with schema migration helpers.
    push_storage_api_table(Ls, mod);
    lua_setfield(Ls, -2, "storage");

    // Shared mod-to-mod service table registry.
    push_interop_api_table(Ls, mod);
    lua_setfield(Ls, -2, "interop");

    // Non-blocking TCP networking (online mod).
    push_net_api_table(Ls);
    lua_setfield(Ls, -2, "net");

    // GGPO UDP prototype status/profile bridge.
    push_online_api_table(Ls);
    lua_setfield(Ls, -2, "online");

    // Local file/folder pickers for user-selected import packages.
    push_fs_api_table(Ls);
    lua_setfield(Ls, -2, "fs");

    // HTTPS-capable async HTTP GET (WinHTTP).
    push_http_api_table(Ls);
    lua_setfield(Ls, -2, "http");

    // Fields (convenience)
    lua_pushstring(Ls, mod->id);      lua_setfield(Ls, -2, "id");
    lua_pushstring(Ls, mod->name);    lua_setfield(Ls, -2, "name");
    lua_pushstring(Ls, mod->version); lua_setfield(Ls, -2, "version");
    lua_pushinteger(Ls, MOD_API_VERSION); lua_setfield(Ls, -2, "framework_api");
}

static const char* k_mod_ui_helpers_lua =
    "local ui = mod and mod.ui\n"
    "if not ui then return end\n"
    "\n"
    "local function copy(src)\n"
    "  local out = {}\n"
    "  if type(src) ~= 'table' then return out end\n"
    "  for k, v in pairs(src) do\n"
    "    if type(v) == 'table' then out[k] = copy(v) else out[k] = v end\n"
    "  end\n"
    "  return out\n"
    "end\n"
    "\n"
    "local function merge(dst, src)\n"
    "  if type(src) ~= 'table' then return dst end\n"
    "  for k, v in pairs(src) do\n"
    "    if type(v) == 'table' and type(dst[k]) == 'table' then\n"
    "      merge(dst[k], v)\n"
    "    elseif type(v) == 'table' then\n"
    "      dst[k] = copy(v)\n"
    "    else\n"
    "      dst[k] = v\n"
    "    end\n"
    "  end\n"
    "  return dst\n"
    "end\n"
    "\n"
    "local default_dark = {\n"
    "  bg = {0.06, 0.07, 0.08, 0.88},\n"
    "  fg = {0.92, 0.94, 0.96, 1.0},\n"
    "  muted = {0.55, 0.60, 0.68, 1.0},\n"
    "  accent = {1.0, 0.78, 0.25, 1.0},\n"
    "  border = {0.23, 0.27, 0.32, 1.0},\n"
    "  hover = {0.13, 0.16, 0.19, 0.94},\n"
    "  active = {0.18, 0.17, 0.10, 0.96},\n"
    "  disabled = {0.16, 0.17, 0.18, 0.50},\n"
    "  pad = 8,\n"
    "  gap = 6,\n"
    "  text_scale = 1.0,\n"
    "}\n"
    "\n"
    "local themes = { default_dark = default_dark }\n"
    "local style_stack = { copy(default_dark) }\n"
    "\n"
    "local function style_for(opts)\n"
    "  local s = copy(style_stack[#style_stack] or default_dark)\n"
    "  if type(opts) == 'table' and type(opts.style) == 'table' then merge(s, opts.style) end\n"
    "  return s\n"
    "end\n"
    "\n"
    "local function color(s, key, fallback)\n"
    "  local v = s and s[key] or fallback\n"
    "  if type(v) ~= 'table' then v = fallback or {1, 1, 1, 1} end\n"
    "  return v\n"
    "end\n"
    "\n"
    "local function clamp01(v)\n"
    "  v = tonumber(v) or 0\n"
    "  if v < 0 then return 0 elseif v > 1 then return 1 end\n"
    "  return v\n"
    "end\n"
    "\n"
    "local function shade(c, amount, alpha)\n"
    "  c = type(c) == 'table' and c or {1, 1, 1, 1}\n"
    "  amount = tonumber(amount) or 0\n"
    "  local a = alpha ~= nil and alpha or c[4] or 1\n"
    "  return { clamp01((c[1] or 0) + amount), clamp01((c[2] or 0) + amount), clamp01((c[3] or 0) + amount), a }\n"
    "end\n"
    "\n"
    "function ui.push_style(style)\n"
    "  local s = copy(style_stack[#style_stack] or default_dark)\n"
    "  merge(s, style)\n"
    "  style_stack[#style_stack + 1] = s\n"
    "  return copy(s)\n"
    "end\n"
    "\n"
    "function ui.pop_style()\n"
    "  if #style_stack > 1 then return table.remove(style_stack) end\n"
    "  return copy(style_stack[1])\n"
    "end\n"
    "\n"
    "function ui.current_style()\n"
    "  return copy(style_stack[#style_stack] or default_dark)\n"
    "end\n"
    "\n"
    "function ui.define_theme(name, style)\n"
    "  if type(name) ~= 'string' or name == '' or type(style) ~= 'table' then return false end\n"
    "  themes[name] = copy(style)\n"
    "  return true\n"
    "end\n"
    "\n"
    "function ui.style_color(key, fallback)\n"
    "  return copy(color(style_stack[#style_stack] or default_dark, key, fallback))\n"
    "end\n"
    "\n"
    "function ui.theme(style)\n"
    "  if type(style) == 'table' then merge(style_stack[#style_stack], style) end\n"
    "  return copy(style_stack[#style_stack])\n"
    "end\n"
    "\n"
    "function ui.set_theme(name)\n"
    "  local t = themes[tostring(name or 'default_dark')]\n"
    "  if not t then return false end\n"
    "  style_stack = { copy(t) }\n"
    "  return true\n"
    "end\n"
    "\n"
    "local function resolve_bounds(opts, def_w, def_h)\n"
    "  opts = opts or {}\n"
    "  local x = tonumber(opts.x)\n"
    "  local y = tonumber(opts.y)\n"
    "  if (not x or not y) and ui.cursor then x, y = ui.cursor() end\n"
    "  x = x or 0\n"
    "  y = y or 0\n"
    "  local w = tonumber(opts.w or opts.width) or def_w or 80\n"
    "  local h = tonumber(opts.h or opts.height) or def_h or 28\n"
    "  return x, y, w, h\n"
    "end\n"
    "\n"
    "local function advance_if_layout(opts, x, y, h, gap)\n"
    "  opts = opts or {}\n"
    "  if opts.no_advance then return end\n"
    "  if opts.x ~= nil or opts.y ~= nil or not ui.cursor then return end\n"
    "  ui.cursor(x, y + h + (tonumber(opts.gap) or gap or 6))\n"
    "end\n"
    "\n"
    "local function text_center(text, x, y, w, h, scale, c)\n"
    "  text = tostring(text or '')\n"
    "  scale = tonumber(scale) or 1\n"
    "  local tw = 0\n"
    "  local th = 9 * scale\n"
    "  if ui.measure_text then tw, th = ui.measure_text(text, scale) end\n"
    "  local max_w = math.max(4, w - 4)\n"
    "  while tw > max_w and scale > 0.55 do\n"
    "    scale = scale * 0.9\n"
    "    if ui.measure_text then tw, th = ui.measure_text(text, scale) else break end\n"
    "  end\n"
    "  ui.text_at(text, x + (w - tw) * 0.5, y + h * 0.58, scale, c[1] or 1, c[2] or 1, c[3] or 1, c[4] or 1)\n"
    "end\n"
    "\n"
    "local function measured_width(text, scale)\n"
    "  if ui.measure_text then local w = ui.measure_text(tostring(text or ''), scale) return w or 0 end\n"
    "  return #tostring(text or '') * 9 * (tonumber(scale) or 1)\n"
    "end\n"
    "\n"
    "function ui.wrap_text(text, max_w, scale)\n"
    "  text = tostring(text or '')\n"
    "  max_w = tonumber(max_w) or 0\n"
    "  scale = tonumber(scale) or 1\n"
    "  local lines = {}\n"
    "  local function push(line) lines[#lines + 1] = tostring(line or '') end\n"
    "  local function push_long_word(word)\n"
    "    local chunk = ''\n"
    "    for i = 1, #word do\n"
    "      local next_chunk = chunk .. word:sub(i, i)\n"
    "      if max_w > 0 and chunk ~= '' and measured_width(next_chunk, scale) > max_w then\n"
    "        push(chunk)\n"
    "        chunk = word:sub(i, i)\n"
    "      else\n"
    "        chunk = next_chunk\n"
    "      end\n"
    "    end\n"
    "    return chunk\n"
    "  end\n"
    "  local function emit_para(para)\n"
    "    if para == '' then push('') return end\n"
    "    local line = ''\n"
    "    for word in tostring(para):gmatch('%S+') do\n"
    "      if max_w > 0 and measured_width(word, scale) > max_w then\n"
    "        if line ~= '' then push(line) line = '' end\n"
    "        line = push_long_word(word)\n"
    "      else\n"
    "        local candidate = (line == '') and word or (line .. ' ' .. word)\n"
    "        if max_w > 0 and line ~= '' and measured_width(candidate, scale) > max_w then\n"
    "          push(line)\n"
    "          line = word\n"
    "        else\n"
    "          line = candidate\n"
    "        end\n"
    "      end\n"
    "    end\n"
    "    push(line)\n"
    "  end\n"
    "  for para in (text .. '\\n'):gmatch('(.-)\\n') do emit_para(para) end\n"
    "  if #lines == 0 then lines[1] = '' end\n"
    "  return lines\n"
    "end\n"
    "\n"
    "function ui.text_wrapped(text, x, y, w, opts)\n"
    "  opts = opts or {}\n"
    "  local scale = tonumber(opts.scale) or 1\n"
    "  local line_gap = tonumber(opts.line_gap) or 4\n"
    "  local s = style_for(opts)\n"
    "  local fg = opts.color or opts.fg or color(s, 'fg')\n"
    "  local lines = ui.wrap_text(text, w, scale)\n"
    "  local line_h = 9 * scale\n"
    "  if ui.measure_text then local _, measured_h = ui.measure_text('Ag', scale) line_h = tonumber(measured_h) or line_h end\n"
    "  for i = 1, #lines do\n"
    "    ui.text_at(lines[i], x, y + (i - 1) * (line_h + line_gap) + line_h * 0.82, scale, fg[1] or 1, fg[2] or 1, fg[3] or 1, fg[4] or 1)\n"
    "  end\n"
    "  return #lines * line_h + math.max(#lines - 1, 0) * line_gap, #lines\n"
    "end\n"
    "\n"
    "function ui.tooltip(text, opts)\n"
    "  if not text or text == '' then return false end\n"
    "  opts = opts or {}\n"
    "  local s = style_for(opts)\n"
    "  local mx, my = ui.mouse_pos()\n"
    "  local scale = tonumber(opts.scale) or 0.85\n"
    "  local pad = tonumber(opts.pad) or 6\n"
    "  local max_w = tonumber(opts.max_w or opts.w) or 280\n"
    "  local lines = ui.wrap_text(tostring(text), max_w, scale)\n"
    "  local tw, th = 0, 9 * scale\n"
    "  for i = 1, #lines do\n"
    "    local lw, lh = ui.measure_text(lines[i], scale)\n"
    "    if lw > tw then tw = lw end\n"
    "    if lh > th then th = lh end\n"
    "  end\n"
    "  local box_w = tw + pad * 2\n"
    "  local box_h = (#lines * th) + math.max(#lines - 1, 0) * 4 + pad * 2\n"
    "  local x = tonumber(opts.x) or (mx + 12)\n"
    "  local y = tonumber(opts.y) or (my + 12)\n"
    "  local sw, sh = ui.screen_size()\n"
    "  if x + box_w > sw - 4 then x = sw - box_w - 4 end\n"
    "  if y + box_h > sh - 4 then y = sh - box_h - 4 end\n"
    "  if x < 4 then x = 4 end\n"
    "  if y < 4 then y = 4 end\n"
    "  ui.rect(x, y, box_w, box_h, { color = color(s, 'bg') })\n"
    "  ui.border(x, y, box_w, box_h, { color = color(s, 'border') })\n"
    "  ui.text_wrapped(tostring(text), x + pad, y + pad, tw, { scale = scale, color = color(s, 'fg'), line_gap = 4 })\n"
    "  return true\n"
    "end\n"
    "\n"
    "local function maybe_tooltip(opts, hovered)\n"
    "  if hovered and type(opts) == 'table' and opts.tooltip then ui.tooltip(opts.tooltip, opts.tooltip_opts) end\n"
    "end\n"
    "\n"
    "function ui.icon_button(id, icon, x, y, w, h, opts)\n"
    "  opts = opts or {}\n"
    "  local s = style_for(opts)\n"
    "  local hovered, clicked, down = false, false, false\n"
    "  if ui.hitbox then hovered, clicked, down = ui.hitbox(tostring(id or ''), x, y, w, h) end\n"
    "  if opts.disabled then clicked = false end\n"
    "  local bg = color(s, 'bg')\n"
    "  if opts.disabled then bg = color(s, 'disabled')\n"
    "  elseif opts.selected then bg = color(s, 'active')\n"
    "  elseif down then bg = color(s, 'active')\n"
    "  elseif hovered then bg = color(s, 'hover') end\n"
    "  ui.rect(x, y, w, h, { color = bg })\n"
    "  ui.border(x, y, w, h, { line_w = opts.line_w or 1, color = opts.selected and color(s, 'accent') or color(s, 'border') })\n"
    "  local fg = opts.disabled and color(s, 'muted') or color(s, 'fg')\n"
    "  if type(icon) == 'number' and ui.draw_sprite then\n"
    "    ui.draw_sprite(icon, x + w * 0.5, y + h * 0.5, { scale = opts.icon_scale or opts.scale or 1, tint = opts.tint or fg })\n"
    "  elseif type(icon) == 'table' and ui.draw_sprite then\n"
    "    local draw_opts = copy(icon)\n"
    "    merge(draw_opts, opts.sprite_opts)\n"
    "    if not draw_opts.scale then draw_opts.scale = opts.icon_scale or opts.scale or 1 end\n"
    "    if not draw_opts.tint then draw_opts.tint = opts.tint or fg end\n"
    "    ui.draw_sprite(draw_opts, x + w * 0.5, y + h * 0.5, draw_opts)\n"
    "  else\n"
    "    text_center(icon or opts.label or '', x, y, w, h, opts.text_scale or s.text_scale or 1, fg)\n"
    "  end\n"
    "  maybe_tooltip(opts, hovered)\n"
    "  return clicked and not opts.disabled, hovered\n"
    "end\n"
    "\n"
    "function ui.panel(a, b, c, d, e, f)\n"
    "  local id, x, y, w, h, opts\n"
    "  if type(a) == 'string' then id, x, y, w, h, opts = a, b, c, d, e, f or {} else x, y, w, h, opts = a, b, c, d, e or {} end\n"
    "  opts = opts or {}\n"
    "  local s = style_for(opts)\n"
    "  local hovered, clicked, down = false, false, false\n"
    "  if id and ui.hitbox then hovered, clicked, down = ui.hitbox(tostring(id), x, y, w, h, opts.hitbox or opts) end\n"
    "  if opts.disabled then clicked, down = false, false end\n"
    "  local bg = opts.bg or opts.fill or color(s, 'bg')\n"
    "  if opts.disabled then bg = opts.disabled_bg or color(s, 'disabled') elseif down and opts.active then bg = opts.active elseif hovered and opts.hover then bg = opts.hover end\n"
    "  ui.rect(x, y, w, h, { color = bg, alpha = opts.alpha })\n"
    "  if opts.inner ~= false then\n"
    "    local inner = type(opts.inner) == 'table' and opts.inner or shade(bg, tonumber(opts.inner_shade) or 0.025, opts.inner_alpha or ((bg[4] or 1) * 0.48))\n"
    "    local inset = tonumber(opts.inset) or 3\n"
    "    if w > inset * 2 and h > inset * 2 then ui.rect(x + inset, y + inset, w - inset * 2, h - inset * 2, { color = inner }) end\n"
    "  end\n"
    "  if opts.accent_edge then\n"
    "    local aw = tonumber(opts.accent_w or opts.accent_width) or 4\n"
    "    local accent = opts.accent or color(s, 'accent')\n"
    "    if opts.accent_edge == 'right' then ui.rect(x + w - aw, y, aw, h, { color = accent, alpha = opts.accent_alpha })\n"
    "    elseif opts.accent_edge == 'top' then ui.rect(x, y, w, aw, { color = accent, alpha = opts.accent_alpha })\n"
    "    elseif opts.accent_edge == 'bottom' then ui.rect(x, y + h - aw, w, aw, { color = accent, alpha = opts.accent_alpha })\n"
    "    else ui.rect(x, y, aw, h, { color = accent, alpha = opts.accent_alpha }) end\n"
    "  end\n"
    "  if opts.border ~= false then\n"
    "    local border = type(opts.border) == 'table' and opts.border or color(s, 'border')\n"
    "    ui.border(x, y, w, h, { line_w = opts.line_w or opts.line_width or 1, color = border, alpha = opts.border_alpha })\n"
    "  end\n"
    "  maybe_tooltip(opts, hovered)\n"
    "  return clicked, hovered, down\n"
    "end\n"
    "\n"
    "function ui.progress_bar(id, value, x, y, w, h, opts)\n"
    "  if type(id) ~= 'string' then opts, h, w, y, x, value, id = h, w, y, x, value, id, nil end\n"
    "  opts = opts or {}\n"
    "  local s = style_for(opts)\n"
    "  value = clamp01(value)\n"
    "  local hovered, clicked, down = false, false, false\n"
    "  if id and ui.hitbox and (opts.interactive or opts.hitbox) then hovered, clicked, down = ui.hitbox(tostring(id), x, y, w, h, opts.hitbox or opts) end\n"
    "  local changed = false\n"
    "  if opts.interactive and (clicked or down) and ui.mouse_pos then\n"
    "    local mx, my = ui.mouse_pos()\n"
    "    local next_value = opts.vertical and ((my - y) / h) or ((mx - x) / w)\n"
    "    if opts.reverse then next_value = 1 - next_value end\n"
    "    next_value = clamp01(next_value)\n"
    "    if next_value ~= value then value, changed = next_value, true end\n"
    "  end\n"
    "  local bg = opts.bg or opts.rail or color(s, 'border')\n"
    "  local fill = opts.fill or opts.color or color(s, 'accent')\n"
    "  ui.rect(x, y, w, h, { color = bg, alpha = opts.alpha })\n"
    "  if opts.vertical then\n"
    "    local fh = h * value\n"
    "    local fy = opts.reverse and y or (y + h - fh)\n"
    "    ui.rect(x, fy, w, fh, { color = fill, alpha = opts.fill_alpha })\n"
    "  else\n"
    "    local fw = w * value\n"
    "    local fx = opts.reverse and (x + w - fw) or x\n"
    "    ui.rect(fx, y, fw, h, { color = fill, alpha = opts.fill_alpha })\n"
    "  end\n"
    "  if opts.border ~= false then ui.border(x, y, w, h, { line_w = opts.line_w or 1, color = type(opts.border) == 'table' and opts.border or color(s, 'border'), alpha = opts.border_alpha }) end\n"
    "  if opts.label then text_center(opts.label, x, y, w, h, opts.text_scale or s.text_scale or 1, opts.text_color or color(s, 'fg')) end\n"
    "  maybe_tooltip(opts, hovered)\n"
    "  return value, changed, hovered\n"
    "end\n"
    "\n"
    "function ui.close_button(id, x, y, size, opts)\n"
    "  opts = opts or {}\n"
    "  size = tonumber(size) or 24\n"
    "  local s = style_for(opts)\n"
    "  local hovered, clicked, down = false, false, false\n"
    "  if ui.hitbox then hovered, clicked, down = ui.hitbox(tostring(id or 'close'), x, y, size, size, opts.hitbox or opts) end\n"
    "  if opts.disabled then clicked, down = false, false end\n"
    "  local bg = opts.bg or (hovered and (opts.hover or color(s, 'hover')) or color(s, 'bg'))\n"
    "  if down and opts.active then bg = opts.active end\n"
    "  ui.rect(x, y, size, size, { color = bg, alpha = opts.alpha })\n"
    "  if opts.border ~= false then ui.border(x, y, size, size, { line_w = opts.line_w or 1, color = type(opts.border) == 'table' and opts.border or color(s, 'border'), alpha = opts.border_alpha }) end\n"
    "  local fg = opts.color or opts.fg or color(s, 'fg')\n"
    "  local p = size * (tonumber(opts.pad_ratio) or 0.30)\n"
    "  ui.line(x + p, y + p, x + size - p, y + size - p, { line_w = opts.stroke or 2, color = fg, alpha = opts.fg_alpha })\n"
    "  ui.line(x + size - p, y + p, x + p, y + size - p, { line_w = opts.stroke or 2, color = fg, alpha = opts.fg_alpha })\n"
    "  maybe_tooltip(opts, hovered)\n"
    "  return clicked and not opts.disabled, hovered, down\n"
    "end\n"
    "\n"
    "local function item_key_label(item, i)\n"
    "  if type(item) == 'table' then return item.id or item.value or i, item.label or item.name or tostring(item.id or item.value or i) end\n"
    "  return i, tostring(item)\n"
    "end\n"
    "\n"
    "function ui.tabs(id, tabs, selected, opts)\n"
    "  opts = opts or {}\n"
    "  tabs = tabs or {}\n"
    "  local s = style_for(opts)\n"
    "  local x, y, w, h = resolve_bounds(opts, (#tabs > 0 and #tabs or 1) * 88, 30)\n"
    "  local gap = tonumber(opts.gap) or 0\n"
    "  local tab_w = tonumber(opts.tab_w) or ((w - gap * math.max(#tabs - 1, 0)) / math.max(#tabs, 1))\n"
    "  local changed = false\n"
    "  for i = 1, #tabs do\n"
    "    local key, label = item_key_label(tabs[i], i)\n"
    "    local bx = x + (i - 1) * (tab_w + gap)\n"
    "    local clicked = ui.icon_button(tostring(id) .. ':' .. tostring(key), label, bx, y, tab_w, h, merge({ selected = selected == key, text_scale = opts.text_scale or s.text_scale }, opts.item_opts))\n"
    "    if clicked and selected ~= key then selected = key changed = true end\n"
    "  end\n"
    "  advance_if_layout(opts, x, y, h, s.gap)\n"
    "  return selected, changed\n"
    "end\n"
    "\n"
    "ui.segmented = ui.tabs\n"
    "\n"
    "function ui.swatch_grid(id, colors, selected, opts)\n"
    "  opts = opts or {}\n"
    "  colors = colors or {}\n"
    "  local s = style_for(opts)\n"
    "  local cols = math.max(1, math.floor(tonumber(opts.cols or opts.columns) or 8))\n"
    "  local cell = tonumber(opts.cell or opts.cell_w) or 24\n"
    "  local gap = tonumber(opts.gap) or 5\n"
    "  local x, y = resolve_bounds(opts, cols * cell + (cols - 1) * gap, cell)\n"
    "  local changed = false\n"
    "  for i = 1, #colors do\n"
    "    local item = colors[i]\n"
    "    local key = type(item) == 'table' and (item.id or item.value or i) or i\n"
    "    local c = type(item) == 'table' and (item.color or item.tint or item) or {1, 1, 1, 1}\n"
    "    local col = (i - 1) % cols\n"
    "    local row = math.floor((i - 1) / cols)\n"
    "    local bx = x + col * (cell + gap)\n"
    "    local by = y + row * (cell + gap)\n"
    "    local hovered, clicked = ui.hitbox(tostring(id) .. ':' .. tostring(key), bx, by, cell, cell)\n"
    "    ui.rect(bx, by, cell, cell, { color = c })\n"
    "    ui.border(bx, by, cell, cell, { line_w = selected == key and 2 or 1, color = selected == key and color(s, 'accent') or color(s, 'border') })\n"
    "    if hovered then ui.border(bx + 2, by + 2, cell - 4, cell - 4, { color = color(s, 'fg') }) end\n"
    "    if clicked and selected ~= key then selected = key changed = true end\n"
    "  end\n"
    "  local rows = math.ceil(#colors / cols)\n"
    "  advance_if_layout(opts, x, y, rows * cell + math.max(rows - 1, 0) * gap, s.gap)\n"
    "  return selected, changed\n"
    "end\n"
    "\n"
    "function ui.item_grid(id, items, selected, opts)\n"
    "  opts = opts or {}\n"
    "  items = items or {}\n"
    "  local s = style_for(opts)\n"
    "  local cols = math.max(1, math.floor(tonumber(opts.cols or opts.columns) or 5))\n"
    "  local cell_w = tonumber(opts.cell_w or opts.cell or opts.w_cell) or 84\n"
    "  local cell_h = tonumber(opts.cell_h or opts.cell or opts.h_cell) or 64\n"
    "  local gap = tonumber(opts.gap) or 6\n"
    "  local x, y = resolve_bounds(opts, cols * cell_w + (cols - 1) * gap, cell_h)\n"
    "  local changed = false\n"
    "  for i = 1, #items do\n"
    "    local item = items[i]\n"
    "    local key, label = item_key_label(item, i)\n"
    "    local col = (i - 1) % cols\n"
    "    local row = math.floor((i - 1) / cols)\n"
    "    local bx = x + col * (cell_w + gap)\n"
    "    local by = y + row * (cell_h + gap)\n"
    "    local hovered, clicked, down = ui.hitbox(tostring(id) .. ':' .. tostring(key), bx, by, cell_w, cell_h)\n"
    "    ui.rect(bx, by, cell_w, cell_h, { color = down and color(s, 'active') or (hovered and color(s, 'hover') or color(s, 'bg')) })\n"
    "    ui.border(bx, by, cell_w, cell_h, { line_w = selected == key and 2 or 1, color = selected == key and color(s, 'accent') or color(s, 'border') })\n"
    "    if type(item) == 'table' and item.color then ui.rect(bx + 8, by + 8, cell_w - 16, cell_h - 26, { color = item.color }) end\n"
    "    if type(item) == 'table' and item.sprite and ui.draw_sprite then ui.draw_sprite(item.sprite, bx + cell_w * 0.5, by + cell_h * 0.42, item.sprite_opts or {}) end\n"
    "    text_center(label, bx + 4, by + cell_h - 22, cell_w - 8, 18, opts.text_scale or 0.75, color(s, 'fg'))\n"
    "    if clicked and selected ~= key then selected = key changed = true end\n"
    "  end\n"
    "  local rows = math.ceil(#items / cols)\n"
    "  advance_if_layout(opts, x, y, rows * cell_h + math.max(rows - 1, 0) * gap, s.gap)\n"
    "  return selected, changed\n"
    "end\n"
    "\n"
    "function ui.slider(id, value, min_value, max_value, opts)\n"
    "  opts = opts or {}\n"
    "  local s = style_for(opts)\n"
    "  local x, y, w, h = resolve_bounds(opts, 180, 26)\n"
    "  value = tonumber(value) or 0\n"
    "  min_value = tonumber(min_value) or 0\n"
    "  max_value = tonumber(max_value) or 1\n"
    "  if max_value == min_value then max_value = min_value + 1 end\n"
    "  local hovered, clicked, down = ui.hitbox(tostring(id), x, y, w, h)\n"
    "  local changed = false\n"
    "  if clicked or down then\n"
    "    local mx = ui.mouse_pos()\n"
    "    local t = (mx - x) / w\n"
    "    if t < 0 then t = 0 elseif t > 1 then t = 1 end\n"
    "    local nv = min_value + (max_value - min_value) * t\n"
    "    local step = tonumber(opts.step)\n"
    "    if step and step > 0 then nv = math.floor((nv / step) + 0.5) * step end\n"
    "    if nv ~= value then value = nv changed = true end\n"
    "  end\n"
    "  local t = (value - min_value) / (max_value - min_value)\n"
    "  if t < 0 then t = 0 elseif t > 1 then t = 1 end\n"
    "  local track_y = y + h * 0.5 - 2\n"
    "  ui.rect(x, track_y, w, 4, { color = color(s, 'border') })\n"
    "  ui.rect(x, track_y, w * t, 4, { color = color(s, 'accent') })\n"
    "  ui.rect(x + w * t - 4, y + 4, 8, h - 8, { color = hovered and color(s, 'fg') or color(s, 'accent') })\n"
    "  if opts.label then local fg = color(s, 'fg'); ui.text_at(tostring(opts.label), x, y - 4, opts.text_scale or 0.75, fg[1] or 1, fg[2] or 1, fg[3] or 1, fg[4] or 1) end\n"
    "  maybe_tooltip(opts, hovered)\n"
    "  advance_if_layout(opts, x, y, h, s.gap)\n"
    "  return value, changed\n"
    "end\n"
    "\n"
    "function ui.checkbox(id, value, opts)\n"
    "  opts = opts or {}\n"
    "  local s = style_for(opts)\n"
    "  local size = tonumber(opts.size) or 22\n"
    "  local x, y = resolve_bounds(opts, size, size)\n"
    "  local clicked = ui.icon_button(id, value and 'X' or '', x, y, size, size, opts)\n"
    "  if opts.label then local fg = color(s, 'fg'); ui.text_at(tostring(opts.label), x + size + (opts.gap or s.gap), y + size * 0.72, opts.text_scale or s.text_scale, fg[1] or 1, fg[2] or 1, fg[3] or 1, fg[4] or 1) end\n"
    "  advance_if_layout(opts, x, y, size, s.gap)\n"
    "  if clicked then return not value, true end\n"
    "  return not not value, false\n"
    "end\n"
    "\n"
    "function ui.cursor_sprite(index)\n"
    "  if not ui.sprite_id then return nil end\n"
    "  return ui.sprite_id('misc', tonumber(index) or 7)\n"
    "end\n"
    "\n"
    "function ui.draw_cursor(opts)\n"
    "  opts = opts or {}\n"
    "  local mx, my = ui.mouse_pos()\n"
    "  local scale = tonumber(opts.scale) or (ui.readable_scale and ui.readable_scale(1.0)) or 1\n"
    "  local sprite = opts.sprite or ui.cursor_sprite(opts.index)\n"
    "  if sprite and ui.draw_sprite then\n"
    "    local size = tonumber(opts.size) or 16\n"
    "    local hot_x = tonumber(opts.hot_x) or 0\n"
    "    local hot_y = tonumber(opts.hot_y) or 0\n"
    "    if ui.draw_sprite(sprite, mx + (size * 0.5 - hot_x) * scale, my + (size * 0.5 - hot_y) * scale, { scale = scale, tint = opts.tint, layer = opts.layer or 1000 }) then return true end\n"
    "  end\n"
    "  if ui.line then\n"
    "    ui.line(mx - 6, my, mx + 6, my, { color = opts.color or {1, 1, 1, 1}, line_w = 1 })\n"
    "    ui.line(mx, my - 6, mx, my + 6, { color = opts.color or {1, 1, 1, 1}, line_w = 1 })\n"
    "    return true\n"
    "  end\n"
    "  return false\n"
    "end\n"
    "\n"
    "if not ui.tile_preview then\n"
    "  function ui.tile_preview(id, frame, arg, x, y, scale, tile_y, opts)\n"
    "    if not ui.sprite_id or not ui.draw_sprite then return false end\n"
    "    local sprite = ui.sprite_id('tiles', tonumber(id) or 0)\n"
    "    if not sprite then return false end\n"
    "    local draw_opts = type(opts) == 'table' and copy(opts) or {}\n"
    "    if not draw_opts.scale then draw_opts.scale = tonumber(scale) or 1 end\n"
    "    return ui.draw_sprite(sprite, tonumber(x) or 0, tonumber(y) or 0, draw_opts) and true or false\n"
    "  end\n"
    "end\n"
    "\n"
    "local state_specs = {}\n"
    "local state_router_installed = false\n"
    "local active_state = nil\n"
    "local last_clock = os.clock()\n"
    "\n"
    "local function install_state_router()\n"
    "  if state_router_installed then return end\n"
    "  state_router_installed = true\n"
    "  mod.on_frame(function()\n"
    "    local now = os.clock()\n"
    "    local dt = now - last_clock\n"
    "    if dt < 0 then dt = 0 elseif dt > 0.25 then dt = 0.25 end\n"
    "    last_clock = now\n"
    "    local name = ui.state_name()\n"
    "    if name ~= active_state then\n"
    "      local old = active_state\n"
    "      local old_spec = old and state_specs[old]\n"
    "      if old_spec and old_spec.leave then old_spec.leave(name) end\n"
    "      active_state = name\n"
    "      local new_spec = state_specs[name]\n"
    "      if new_spec and new_spec.enter then new_spec.enter(old) end\n"
    "    end\n"
    "    local spec = state_specs[name]\n"
    "    if spec then\n"
    "      if spec.update then spec.update(dt) end\n"
    "      if spec.render then spec.render() end\n"
    "      local cursor_opts = type(spec.cursor) == 'table' and spec.cursor or spec.cursor_opts\n"
    "      if spec.cursor == false then\n"
    "        if ui._set_default_cursor_visible then ui._set_default_cursor_visible(false) end\n"
    "      elseif cursor_opts and ui.draw_cursor then\n"
    "        local drew = false\n"
    "        if ui.begin_overlay and ui.end_overlay then ui.begin_overlay() drew = ui.draw_cursor(cursor_opts) ui.end_overlay() else drew = ui.draw_cursor(cursor_opts) end\n"
    "        if drew and ui._set_default_cursor_visible then ui._set_default_cursor_visible(false) end\n"
    "      end\n"
    "    end\n"
    "  end)\n"
    "  mod.on_event(function(e)\n"
    "    local spec = state_specs[ui.state_name()]\n"
    "    if spec and spec.event then return spec.event(e) and true or false end\n"
    "    return false\n"
    "  end)\n"
    "end\n"
    "\n"
    "function ui.define_state(name, spec)\n"
    "  if type(name) ~= 'string' or name == '' then return false, 'state name required' end\n"
    "  spec = spec or {}\n"
    "  state_specs[name] = spec\n"
    "  if ui.create_state then ui.create_state(name) end\n"
    "  install_state_router()\n"
    "  return true\n"
    "end\n";

static void install_mod_ui_helpers(LoadedMod* mod) {
    if (!L || !mod) return;
    if (mod->env_ref == LUA_NOREF || mod->env_ref == LUA_REFNIL) return;

    if (luaL_loadbuffer(L, k_mod_ui_helpers_lua, strlen(k_mod_ui_helpers_lua), "@mod_ui_helpers") != 0) {
        const char* err = lua_tostring(L, -1);
        char buf[512];
        snprintf(buf, sizeof(buf), "failed to load UI helpers: %s", err ? err : "(unknown)");
        log_mod(mod, "ERROR", buf);
        lua_pop(L, 1);
        return;
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, mod->env_ref);
    lua_setfenv(L, -2);
    if (lua_pcall(L, 0, 0, 0) != 0) {
        const char* err = lua_tostring(L, -1);
        char buf[512];
        snprintf(buf, sizeof(buf), "failed to run UI helpers: %s", err ? err : "(unknown)");
        log_mod(mod, "ERROR", buf);
        lua_pop(L, 1);
    }
}

static const char* k_mod_anim_helpers_lua =
    "local ui = mod and mod.ui\n"
    "if not ui then return end\n"
    "\n"
    "local anim = mod.anim or {}\n"
    "\n"
    "local function resolve_base(sheet)\n"
    "  if type(sheet) == 'number' then return math.floor(sheet) end\n"
    "  if ui.sheet_base then\n"
    "    local b = ui.sheet_base(tostring(sheet or 'sprites'))\n"
    "    if type(b) == 'number' then return math.floor(b) end\n"
    "  end\n"
    "  return nil\n"
    "end\n"
    "\n"
    "local function copy_table(src)\n"
    "  local out = {}\n"
    "  if type(src) ~= 'table' then return out end\n"
    "  for k, v in pairs(src) do out[k] = v end\n"
    "  return out\n"
    "end\n"
    "\n"
    "local function normalize_frames(frames, default_base)\n"
    "  local out = {}\n"
    "  if type(frames) ~= 'table' then return out end\n"
    "  for i = 1, #frames do\n"
    "    local entry = frames[i]\n"
    "    if type(entry) == 'number' then\n"
    "      local sprite = math.floor(entry)\n"
    "      if default_base then sprite = default_base + sprite end\n"
    "      out[#out + 1] = { sprite = sprite }\n"
    "    elseif type(entry) == 'table' then\n"
    "      local f = copy_table(entry)\n"
    "      local sprite = f.sprite\n"
    "      if sprite == nil then sprite = f.id end\n"
    "      if sprite == nil and f.index ~= nil and default_base then\n"
    "        sprite = default_base + tonumber(f.index or 0)\n"
    "      elseif sprite ~= nil and default_base and f.relative then\n"
    "        sprite = default_base + tonumber(sprite or 0)\n"
    "      end\n"
    "      if sprite ~= nil then\n"
    "        f.sprite = math.floor(tonumber(sprite) or 0)\n"
    "        out[#out + 1] = f\n"
    "      end\n"
    "    end\n"
    "  end\n"
    "  return out\n"
    "end\n"
    "\n"
    "function anim.frame_strip(opts)\n"
    "  opts = opts or {}\n"
    "  local base = resolve_base(opts.sheet or opts.atlas or 'sprites')\n"
    "  local first = math.floor(tonumber(opts.first or opts.start or 0) or 0)\n"
    "  local count = math.floor(tonumber(opts.count or opts.len or 0) or 0)\n"
    "  local step = math.floor(tonumber(opts.step or 1) or 1)\n"
    "  local out = {}\n"
    "  if not base or count <= 0 then return out end\n"
    "  for i = 0, count - 1 do\n"
    "    out[#out + 1] = { sprite = base + first + (i * step) }\n"
    "  end\n"
    "  return out\n"
    "end\n"
    "\n"
    "function anim.frame_table(opts)\n"
    "  if type(opts) ~= 'table' then return {} end\n"
    "  local list = opts\n"
    "  local base = nil\n"
    "  if opts.frames then\n"
    "    list = opts.frames\n"
    "    base = resolve_base(opts.sheet or opts.atlas)\n"
    "  end\n"
    "  if base then\n"
    "    return normalize_frames(list, base)\n"
    "  end\n"
    "  return normalize_frames(list, nil)\n"
    "end\n"
    "\n"
    "local function frame_count(a)\n"
    "  return (a and type(a.frames) == 'table') and #a.frames or 0\n"
    "end\n"
    "\n"
    "local function frame_duration(a, f)\n"
    "  local duration = tonumber(f and f.duration)\n"
    "  if duration and duration > 0 then return duration end\n"
    "  local fps = tonumber(f and f.fps) or tonumber(a and a.fps) or 12\n"
    "  if fps <= 0 then fps = 12 end\n"
    "  return 1 / fps\n"
    "end\n"
    "\n"
    "local function clamp_index(i, n)\n"
    "  if n <= 0 then return 0 end\n"
    "  if i < 1 then return 1 end\n"
    "  if i > n then return n end\n"
    "  return i\n"
    "end\n"
    "\n"
    "function anim.new(opts)\n"
    "  opts = opts or {}\n"
    "  local frames = opts.frames\n"
    "  if frames == nil and opts.strip then\n"
    "    frames = anim.frame_strip(opts.strip)\n"
    "  end\n"
    "  if type(frames) ~= 'table' and type(opts.frame_table) == 'table' then\n"
    "    frames = anim.frame_table(opts.frame_table)\n"
    "  end\n"
    "  if type(frames) == 'table' and (opts.sheet or opts.atlas) then\n"
    "    frames = normalize_frames(frames, resolve_base(opts.sheet or opts.atlas))\n"
    "  else\n"
    "    frames = normalize_frames(frames or {}, nil)\n"
    "  end\n"
    "\n"
    "  local n = #frames\n"
    "  local start = math.floor(tonumber(opts.start_frame or opts.start or 1) or 1)\n"
    "  local a = {\n"
    "    frames = frames,\n"
    "    fps = tonumber(opts.fps) or 12,\n"
    "    loop = (opts.loop ~= false),\n"
    "    ping_pong = (opts.ping_pong == true) or (opts.pingpong == true),\n"
    "    speed = tonumber(opts.speed) or 1.0,\n"
    "    flip = not not opts.flip,\n"
    "    scale = tonumber(opts.scale),\n"
    "    scale_x = tonumber(opts.scale_x),\n"
    "    scale_y = tonumber(opts.scale_y),\n"
    "    r = tonumber(opts.r),\n"
    "    g = tonumber(opts.g),\n"
    "    b = tonumber(opts.b),\n"
    "    a = tonumber(opts.a),\n"
    "    tint = opts.tint,\n"
    "    paused = not not opts.paused,\n"
    "    accum = tonumber(opts.accum) or 0,\n"
    "    dir = ((tonumber(opts.direction) or 1) >= 0) and 1 or -1,\n"
    "    index = clamp_index(start, n),\n"
    "  }\n"
    "  if n == 0 then\n"
    "    a.index = 0\n"
    "  elseif a.ping_pong and n == 1 then\n"
    "    a.dir = 1\n"
    "  end\n"
    "  return setmetatable(a, { __index = anim.instance })\n"
    "end\n"
    "\n"
    "function anim.frame(a)\n"
    "  if type(a) ~= 'table' then return nil end\n"
    "  local n = frame_count(a)\n"
    "  if n <= 0 then return nil end\n"
    "  local i = clamp_index(math.floor(tonumber(a.index) or 1), n)\n"
    "  a.index = i\n"
    "  return a.frames[i], i\n"
    "end\n"
    "\n"
    "local function step_once(a, n)\n"
    "  if n <= 0 then a.index = 0 return end\n"
    "  local i = clamp_index(math.floor(tonumber(a.index) or 1), n)\n"
    "  local dir = (tonumber(a.dir) or 1) >= 0 and 1 or -1\n"
    "  if a.ping_pong and n > 1 then\n"
    "    i = i + dir\n"
    "    if i > n then i = n - 1 dir = -1 end\n"
    "    if i < 1 then i = 2 dir = 1 end\n"
    "    if i < 1 then i = 1 end\n"
    "    if i > n then i = n end\n"
    "    a.dir = dir\n"
    "    a.index = i\n"
    "    return\n"
    "  end\n"
    "  i = i + 1\n"
    "  if i > n then\n"
    "    if a.loop then i = 1 else i = n end\n"
    "  end\n"
    "  a.index = i\n"
    "  a.dir = 1\n"
    "end\n"
    "\n"
    "function anim.update(a, dt)\n"
    "  if type(a) ~= 'table' then return nil end\n"
    "  local n = frame_count(a)\n"
    "  if n <= 0 then a.index = 0 return a end\n"
    "  if a.paused then return a end\n"
    "  local t = tonumber(dt) or 0\n"
    "  local speed = tonumber(a.speed) or 1.0\n"
    "  if t <= 0 or speed == 0 then return a end\n"
    "  local accum = tonumber(a.accum) or 0\n"
    "  accum = accum + (t * speed)\n"
    "  local frame = anim.frame(a)\n"
    "  local guard = 0\n"
    "  while frame do\n"
    "    local d = frame_duration(a, frame)\n"
    "    if accum < d or d <= 0 then break end\n"
    "    accum = accum - d\n"
    "    step_once(a, n)\n"
    "    frame = anim.frame(a)\n"
    "    guard = guard + 1\n"
    "    if guard > 1024 then break end\n"
    "  end\n"
    "  a.accum = accum\n"
    "  return a\n"
    "end\n"
    "\n"
    "local function merge_draw_opts(a, frame, opts)\n"
    "  local out = {}\n"
    "  if type(opts) == 'table' then\n"
    "    for k, v in pairs(opts) do out[k] = v end\n"
    "  end\n"
    "  local function fill_from(src)\n"
    "    if type(src) ~= 'table' then return end\n"
    "    if out.flip == nil and src.flip ~= nil then out.flip = src.flip end\n"
    "    if out.scale == nil and src.scale ~= nil then out.scale = src.scale end\n"
    "    if out.scale_x == nil and src.scale_x ~= nil then out.scale_x = src.scale_x end\n"
    "    if out.scale_y == nil and src.scale_y ~= nil then out.scale_y = src.scale_y end\n"
    "    if out.sx == nil and src.sx ~= nil then out.sx = src.sx end\n"
    "    if out.sy == nil and src.sy ~= nil then out.sy = src.sy end\n"
    "    if out.layer == nil and src.layer ~= nil then out.layer = src.layer end\n"
    "    if out.angle == nil and src.angle ~= nil then out.angle = src.angle end\n"
    "    if out.r == nil and src.r ~= nil then out.r = src.r end\n"
    "    if out.g == nil and src.g ~= nil then out.g = src.g end\n"
    "    if out.b == nil and src.b ~= nil then out.b = src.b end\n"
    "    if out.a == nil and src.a ~= nil then out.a = src.a end\n"
    "    if out.tint == nil and src.tint ~= nil then out.tint = src.tint end\n"
    "  end\n"
    "  fill_from(a)\n"
    "  fill_from(frame)\n"
    "  return out\n"
    "end\n"
    "\n"
    "function anim.draw(a, x, y, opts)\n"
    "  if not ui.draw_sprite then return false end\n"
    "  local frame = anim.frame(a)\n"
    "  if type(frame) ~= 'table' then return false end\n"
    "  local sprite = frame.sprite or frame.id\n"
    "  if type(sprite) ~= 'number' then return false end\n"
    "  return ui.draw_sprite(sprite, x, y, merge_draw_opts(a, frame, opts))\n"
    "end\n"
    "\n"
    "anim.instance = anim.instance or {}\n"
    "function anim.instance:update(dt) return anim.update(self, dt) end\n"
    "function anim.instance:frame() return anim.frame(self) end\n"
    "function anim.instance:draw(x, y, opts) return anim.draw(self, x, y, opts) end\n"
    "function anim.instance:reset(frame_index)\n"
    "  local n = frame_count(self)\n"
    "  if n <= 0 then self.index = 0 self.accum = 0 self.dir = 1 return self end\n"
    "  self.index = clamp_index(math.floor(tonumber(frame_index) or 1), n)\n"
    "  self.accum = 0\n"
    "  self.dir = 1\n"
    "  return self\n"
    "end\n"
    "\n"
    "mod.anim = anim\n";

static void install_mod_anim_helpers(LoadedMod* mod) {
    if (!L || !mod) return;
    if (mod->env_ref == LUA_NOREF || mod->env_ref == LUA_REFNIL) return;

    if (luaL_loadbuffer(L, k_mod_anim_helpers_lua, strlen(k_mod_anim_helpers_lua), "@mod_anim_helpers") != 0) {
        const char* err = lua_tostring(L, -1);
        char buf[512];
        snprintf(buf, sizeof(buf), "failed to load anim helpers: %s", err ? err : "(unknown)");
        log_mod(mod, "ERROR", buf);
        lua_pop(L, 1);
        return;
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, mod->env_ref);
    lua_setfenv(L, -2);
    if (lua_pcall(L, 0, 0, 0) != 0) {
        const char* err = lua_tostring(L, -1);
        char buf[512];
        snprintf(buf, sizeof(buf), "failed to run anim helpers: %s", err ? err : "(unknown)");
        log_mod(mod, "ERROR", buf);
        lua_pop(L, 1);
    }
}

static int call_lua_ref0(lua_State* Ls, LoadedMod* mod, int ref, const char* where) {
    if (ref == LUA_NOREF || ref == LUA_REFNIL) return 1;
    lua_rawgeti(Ls, LUA_REGISTRYINDEX, ref);
    if (lua_pcall(Ls, 0, 0, 0) != 0) {
        const char* err = lua_tostring(Ls, -1);
        char buf[512];
        snprintf(buf, sizeof(buf), "%s error: %s", where, err ? err : "(unknown)");
        log_mod(mod, "ERROR", buf);
        lua_pop(Ls, 1);
        mod->error_count++;
        return 0;
    }
    return 1;
}

// =============================
// Loading
// =============================

static void loaded_mod_apply_manifest(LoadedMod* mod, const ModManifest* manifest) {
    if (!mod || !manifest) return;
    snprintf(mod->id, sizeof(mod->id), "%s", manifest->id);
    snprintf(mod->name, sizeof(mod->name), "%s", manifest->name);
    snprintf(mod->version, sizeof(mod->version), "%s", manifest->version);
    snprintf(mod->author, sizeof(mod->author), "%s", manifest->author);
    snprintf(mod->description, sizeof(mod->description), "%s", manifest->description);
    snprintf(mod->entry, sizeof(mod->entry), "%s", manifest->entry);
    snprintf(mod->folder_path, sizeof(mod->folder_path), "%s", manifest->folder_path);
    snprintf(mod->config_rel, sizeof(mod->config_rel), "%s", manifest->config_rel);
    snprintf(mod->storage_rel, sizeof(mod->storage_rel), "%s", manifest->storage_rel);
    snprintf(mod->binds_rel, sizeof(mod->binds_rel), "%s", manifest->binds_rel);
    mod->depends = manifest->depends;
    mod->optional_deps = manifest->optional_deps;
    mod->conflicts = manifest->conflicts;
    mod->api_version = manifest->api_version;
}

static void loaded_mod_build_manifest(const LoadedMod* mod, ModManifest* manifest) {
    if (!mod || !manifest) return;
    memset(manifest, 0, sizeof(*manifest));
    snprintf(manifest->id, sizeof(manifest->id), "%s", mod->id);
    snprintf(manifest->name, sizeof(manifest->name), "%s", mod->name);
    snprintf(manifest->version, sizeof(manifest->version), "%s", mod->version);
    snprintf(manifest->author, sizeof(manifest->author), "%s", mod->author);
    snprintf(manifest->description, sizeof(manifest->description), "%s", mod->description);
    snprintf(manifest->entry, sizeof(manifest->entry), "%s", mod->entry);
    snprintf(manifest->folder_path, sizeof(manifest->folder_path), "%s", mod->folder_path);
    snprintf(manifest->config_rel, sizeof(manifest->config_rel), "%s", mod->config_rel);
    snprintf(manifest->storage_rel, sizeof(manifest->storage_rel), "%s", mod->storage_rel);
    snprintf(manifest->binds_rel, sizeof(manifest->binds_rel), "%s", mod->binds_rel);
    manifest->api_version = mod->api_version;
    manifest->allow_api_mismatch = 1;
    manifest->depends = mod->depends;
    manifest->optional_deps = mod->optional_deps;
    manifest->conflicts = mod->conflicts;
}

static int load_mod_lua(LoadedMod* mod, const ModManifest* manifest) {
    if (!mod || !manifest) return 0;
    loaded_mod_apply_manifest(mod, manifest);

    // Optional config (line-based config file)
    if (mod->config_rel[0]) {
        snprintf(mod->config_path, sizeof(mod->config_path), "%s\\%s", mod->folder_path, mod->config_rel);
        mod_config_load(mod);
    } else {
        mod->config_path[0] = '\0';
    }

    if (mod->storage_rel[0]) {
        snprintf(mod->storage_path, sizeof(mod->storage_path), "%s\\%s", mod->folder_path, mod->storage_rel);
    } else {
        snprintf(mod->storage_path, sizeof(mod->storage_path), "%s\\storage.cfg", mod->folder_path);
    }
    if (mod->binds_rel[0]) {
        snprintf(mod->binds_path, sizeof(mod->binds_path), "%s\\%s", mod->folder_path, mod->binds_rel);
    } else {
        snprintf(mod->binds_path, sizeof(mod->binds_path), "%s\\binds.cfg", mod->folder_path);
    }
    mod_storage_load(mod);

    if (mod->api_version != MOD_API_VERSION && !(manifest->allow_api_mismatch || global_allow_api_mismatch())) {
        LOG_ERROR("Skipping mod %s: api_version=%d but framework is %d (set allow_api_mismatch=true to override)",
                  mod->id, mod->api_version, MOD_API_VERSION);
        return 0;
    }

    if (mod->api_version != MOD_API_VERSION && (manifest->allow_api_mismatch || global_allow_api_mismatch())) {
        LOG_WARN("Loading mod %s with api_version=%d on framework=%d due explicit override",
                 mod->id, mod->api_version, MOD_API_VERSION);
    }

    LOG_INFO("Loading mod: %s (%s) v%s by %s", mod->id, mod->name, mod->version, mod->author);
    if (mod->description[0]) LOG_INFO("  %s", mod->description);

    char lua_path[MAX_PATH];
    snprintf(lua_path, sizeof(lua_path), "%s\\%s", mod->folder_path, mod->entry);

    // Load chunk
    if (luaL_loadfile(L, lua_path) != 0) {
        const char* err = lua_tostring(L, -1);
        LOG_ERROR("Error loading %s: %s", mod->id, err ? err : "(unknown)");
        lua_pop(L, 1);
        return 0;
    }

    // Create environment table for the mod
    lua_newtable(L); // env
    {
        // env metatable: __index = sandbox allowlist (NOT the real _G), so mods
        // can only reach vetted globals + their injected APIs.
        lua_newtable(L); // mt
        if (g_safe_env_ref != LUA_NOREF) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, g_safe_env_ref);
        } else {
            lua_pushvalue(L, LUA_GLOBALSINDEX); // fallback (should not happen)
        }
        lua_setfield(L, -2, "__index");
        lua_setmetatable(L, -2);
    }

    // Create per-mod API table and store it in env.mod
    push_mod_api_table(L, mod);
    lua_pushvalue(L, -1);
    mod->mod_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_setfield(L, -2, "mod");

    // Create per-mod config API table and store it in env.config
    push_config_api_table(L, mod);
    lua_setfield(L, -2, "config");

    // Create per-mod storage API table and store it in env.storage
    push_storage_api_table(L, mod);
    lua_setfield(L, -2, "storage");

    // Create interop API table and store it in env.interop
    push_interop_api_table(L, mod);
    lua_setfield(L, -2, "interop");

    // Mods share one Lua state, so shadow os.exit per-mod to keep deliberate
    // process termination on the crash-handler path instead of a clean shutdown.
    push_mod_os_table(L, mod);
    lua_setfield(L, -2, "os");

    // Keep env alive
    lua_pushvalue(L, -1);
    mod->env_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    install_mod_ui_helpers(mod);
    install_mod_anim_helpers(mod);

    // Set env for the chunk (Lua 5.1 / LuaJIT)
    lua_setfenv(L, -2);

    // Execute chunk
    if (lua_pcall(L, 0, 0, 0) != 0) {
        const char* err = lua_tostring(L, -1);
        LOG_ERROR("Error running %s: %s", mod->id, err ? err : "(unknown)");
        lua_pop(L, 1);
        return 0;
    }

    // Apply persisted bind overrides after the mod has had a chance to register named binds.
    mod_bind_load(mod);

    // Call on_load if registered
    call_lua_ref0(L, mod, mod->on_load_ref, "on_load");
    LOG_INFO("Loaded mod: %s", mod->id);
    return 1;
}

// =============================
// Init / Shutdown
// =============================

static void extend_package_path(void) {
    // package.path = package.path .. ";mods\\?\\?.lua;mods\\?\\init.lua"
    lua_getglobal(L, "package");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }
    lua_getfield(L, -1, "path");
    const char* old = lua_tostring(L, -1);
    if (!old) old = "";

    char newpath[2048];
    snprintf(newpath, sizeof(newpath), "%s;mods\\?\\?.lua;mods\\?\\init.lua", old);
    lua_pop(L, 1);
    lua_pushstring(L, newpath);
    lua_setfield(L, -2, "path");
    lua_pop(L, 1);
}

static void reset_runtime_ui_state(void) {
    g_ui_mouse_x = 0;
    g_ui_mouse_y = 0;
    g_ui_mouse_down_left = 0;
    g_ui_mouse_pressed_left = 0;
    g_ui_mouse_down_middle = 0;
    g_ui_mouse_pressed_middle = 0;
    g_ui_mouse_down_right = 0;
    g_ui_mouse_pressed_right = 0;
    g_force_layout_refresh = 1;
    g_last_layout_state = (void*)-1;
    g_last_btn_count = -1;
}

/* Build the allowlist of globals a sandboxed mod environment may see. Anything
 * NOT added here is unreachable from mods: io, os.execute/remove/rename,
 * package/require, dofile/loadfile/loadstring/load, debug, ffi, jit,
 * getfenv/setfenv, and the real _G. So a malicious mod cannot touch the
 * filesystem (outside its scoped storage/dofile), run shell commands, load
 * native code, or escape its sandbox. Trusted/official mods that genuinely need
 * more would be granted it via a future mod.json capability declaration. */
static void build_safe_globals(lua_State* Ls) {
    static const char* k_value_globals[] = {
        "assert", "error", "ipairs", "pairs", "next", "pcall", "xpcall", "select",
        "tonumber", "tostring", "type", "unpack", "rawequal", "rawget", "rawset",
        "rawlen", "setmetatable", "getmetatable", "print", "collectgarbage", "_VERSION",
        NULL
    };
    static const char* k_lib_globals[] = { "math", "string", "table", "coroutine", "bit", NULL };
    int safe;

    if (g_safe_env_ref != LUA_NOREF) return;

    lua_newtable(Ls);
    safe = lua_gettop(Ls);

    for (int i = 0; k_value_globals[i]; i++) {
        lua_getglobal(Ls, k_value_globals[i]);
        if (lua_isnil(Ls, -1)) lua_pop(Ls, 1);
        else lua_setfield(Ls, safe, k_value_globals[i]);
    }
    for (int i = 0; k_lib_globals[i]; i++) {
        lua_getglobal(Ls, k_lib_globals[i]);
        if (lua_isnil(Ls, -1)) lua_pop(Ls, 1);
        else lua_setfield(Ls, safe, k_lib_globals[i]);
    }

    /* _G inside a mod resolves to the sandbox itself, never the real globals. */
    lua_pushvalue(Ls, safe);
    lua_setfield(Ls, safe, "_G");

    lua_pushvalue(Ls, safe);
    g_safe_env_ref = luaL_ref(Ls, LUA_REGISTRYINDEX);
    lua_pop(Ls, 1);
}

static int create_lua_runtime(void) {
    if (L) return 1;
    L = luaL_newstate();
    if (!L) {
        LOG_ERROR("Failed to create Lua state");
        return 0;
    }
    luaL_openlibs(L);
    extend_package_path();
    build_safe_globals(L);
    return 1;
}

static void unload_single_mod_runtime(LoadedMod* mod, int call_on_unload_cb) {
    if (!mod || !L) return;

    if (call_on_unload_cb && mod->enabled) {
        call_lua_ref0(L, mod, mod->on_unload_ref, "on_unload");
    }

    reflist_clear(L, &mod->on_frame);
    reflist_clear(L, &mod->on_tick);
    reflist_clear(L, &mod->on_tick_post);
    reflist_clear(L, &mod->on_event);
    lua_manager_clear_mod_tiles((int)(mod - g_mods));
    mod_ui_free(mod);

    if (mod->on_layout) {
        for (int li = 0; li < mod->on_layout_count; li++) {
            if (mod->on_layout[li].ref != LUA_NOREF && mod->on_layout[li].ref != LUA_REFNIL) {
                luaL_unref(L, LUA_REGISTRYINDEX, mod->on_layout[li].ref);
            }
        }
        free(mod->on_layout);
        mod->on_layout = NULL;
    }
    mod->on_layout_count = 0;
    mod->on_layout_cap = 0;

    interop_remove_owner(L, mod);
    mod_config_clear(L, mod);
    mod_bind_clear(mod);
    mod_storage_clear(mod);
    mod_audio_clear(mod);
    font_ext_forget_owner_cache(mod->id);
    mod_resource_regs_clear(mod);
    mod_asset_sheets_clear(mod);

    if (mod->on_load_ref != LUA_NOREF && mod->on_load_ref != LUA_REFNIL) {
        luaL_unref(L, LUA_REGISTRYINDEX, mod->on_load_ref);
    }
    mod->on_load_ref = LUA_NOREF;
    if (mod->on_unload_ref != LUA_NOREF && mod->on_unload_ref != LUA_REFNIL) {
        luaL_unref(L, LUA_REGISTRYINDEX, mod->on_unload_ref);
    }
    mod->on_unload_ref = LUA_NOREF;
    if (mod->env_ref != LUA_NOREF && mod->env_ref != LUA_REFNIL) {
        luaL_unref(L, LUA_REGISTRYINDEX, mod->env_ref);
    }
    mod->env_ref = LUA_NOREF;
    if (mod->mod_ref != LUA_NOREF && mod->mod_ref != LUA_REFNIL) {
        luaL_unref(L, LUA_REGISTRYINDEX, mod->mod_ref);
    }
    mod->mod_ref = LUA_NOREF;
}

static void unload_all_mods(void) {
    if (!g_mods || g_mod_count <= 0) {
        free(g_mods);
        g_mods = NULL;
        g_mod_count = 0;
        g_mod_cap = 0;
        interop_clear_all(L);
        audio_runtime_reset_channels();
        return;
    }

    if (!L) {
        for (int i = 0; i < g_mod_count; i++) {
            mod_bind_clear(&g_mods[i]);
            mod_storage_clear(&g_mods[i]);
            mod_audio_clear(&g_mods[i]);
        }
        interop_clear_all(NULL);
        free(g_mods);
        g_mods = NULL;
        g_mod_count = 0;
        g_mod_cap = 0;
        audio_runtime_reset_channels();
        return;
    }

    // Free refs + per-mod allocations (includes best-effort on_unload)
    for (int i = 0; i < g_mod_count; i++) {
        LoadedMod* mod = &g_mods[i];
        unload_single_mod_runtime(mod, 1);
    }

    free(g_mods);
    g_mods = NULL;
    g_mod_count = 0;
    g_mod_cap = 0;
    interop_clear_all(L);
    audio_runtime_reset_channels();
}

static int discovered_mods_push(DiscoveredMod** mods, int* count, int* cap, const DiscoveredMod* item) {
    if (!mods || !count || !cap || !item) return 0;
    if (*count + 1 > *cap) {
        int newcap = (*cap == 0) ? 8 : (*cap * 2);
        DiscoveredMod* nm = (DiscoveredMod*)realloc(*mods, sizeof(DiscoveredMod) * newcap);
        if (!nm) return 0;
        *mods = nm;
        *cap = newcap;
    }
    (*mods)[*count] = *item;
    (*count)++;
    return 1;
}

static int manifest_precedence_cmp(const ModManifest* a, const ModManifest* b) {
    if (!a && !b) return 0;
    if (!a) return 1;
    if (!b) return -1;
    if (a->priority != b->priority) return (a->priority > b->priority) ? -1 : 1;
    int c = _stricmp(a->id, b->id);
    if (c != 0) return c;
    return _stricmp(a->folder_name, b->folder_name);
}

static int discovered_find_index_by_id(const DiscoveredMod* mods, const int* active, int count, const char* id) {
    if (!mods || count <= 0 || !id || !id[0]) return -1;
    for (int i = 0; i < count; i++) {
        if (active && !active[i]) continue;
        if (_stricmp(mods[i].manifest.id, id) == 0) return i;
    }
    return -1;
}

static int manifests_conflict(const ModManifest* a, const ModManifest* b) {
    if (!a || !b) return 0;
    if (mod_id_list_contains(&a->conflicts, b->id)) return 1;
    if (mod_id_list_contains(&b->conflicts, a->id)) return 1;
    return 0;
}

static void add_manifest_edge(const int* active, int count, uint8_t* edges, int* indegree, int from, int to) {
    if (!active || !edges || !indegree) return;
    if (from < 0 || to < 0 || from >= count || to >= count) return;
    if (!active[from] || !active[to]) return;
    if (from == to) return;
    size_t idx = (size_t)from * (size_t)count + (size_t)to;
    if (edges[idx]) return;
    edges[idx] = 1;
    indegree[to]++;
}

static int mods_dir_name_ignored(const char* name) {
    if (!name || !name[0]) return 1;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return 1;
    if (name[0] == '_') return 1;
    return 0;
}

static int loaded_mod_requires_id(const LoadedMod* mod, const char* dep_id) {
    if (!mod || !dep_id || !dep_id[0]) return 0;
    for (int i = 0; i < mod->depends.count; i++) {
        if (_stricmp(mod->depends.items[i].id, dep_id) == 0) return 1;
    }
    return 0;
}

static int rebuild_registered_assets_from_enabled_mods(const char* reason) {
    int had_loaded_assets = font_ext_font_loaded() || texture_ext_any_path_loaded();
    char err[256];

    texture_ext_reset_runtime_state();
    font_ext_reset_runtime_state();

    for (int mi = 0; mi < g_mod_count; mi++) {
        LoadedMod* mod = &g_mods[mi];
        if (!mod->enabled) continue;

        for (int fi = 0; fi < mod->font_reg_count; fi++) {
            FontRegistration* reg = &mod->font_regs[fi];
            err[0] = '\0';
            if (!font_ext_register_glyph(mod->id, mod->folder_path,
                                         reg->byte_value, reg->relpath, 1,
                                         err, (int)sizeof(err))) {
                LOG_WARN("font_ext: failed to replay glyph 0x%02X for %s: %s",
                         (unsigned)reg->byte_value,
                         mod->id,
                         err[0] ? err : "unknown error");
            }
        }

        for (int ti = 0; ti < mod->texture_reg_count; ti++) {
            TextureRegistration* reg = &mod->texture_regs[ti];
            err[0] = '\0';
            if (!texture_ext_register_png(mod->id, mod->folder_path,
                                          reg->target, reg->relpath, 1,
                                          err, (int)sizeof(err))) {
                LOG_WARN("texture_ext: failed to replay %s for %s: %s",
                         reg->target,
                         mod->id,
                         err[0] ? err : "unknown error");
            }
        }
    }

    if (had_loaded_assets) {
        if (!reload_engine_gfx_atlases(reason && reason[0] ? reason : "asset ownership changed")) {
            LOG_WARN("Asset registry rebuild failed; restart may still be required");
            return 0;
        }
    }

    return 1;
}

static int scan_and_load_mods(void) {
    if (!L) return 0;

    LOG_INFO("Scanning mods/ folder...");

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA("mods\\*", &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        LOG_WARN("No mods/ folder found");
        return 0;
    }

    int scanned = 0;
    DiscoveredMod* discovered = NULL;
    int discovered_count = 0;
    int discovered_cap = 0;

    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (mods_dir_name_ignored(fd.cFileName)) continue;

        scanned++;

        DiscoveredMod d;
        memset(&d, 0, sizeof(d));
        d.active = 1;
        d.loaded = 0;

        char err[256];
        err[0] = '\0';
        if (!parse_mod_manifest_file(fd.cFileName, &d.manifest, err, (int)sizeof(err))) {
            LOG_WARN("Skipping mod folder %s: %s", fd.cFileName, err[0] ? err : "invalid mod.json");
            continue;
        }

        if (!discovered_mods_push(&discovered, &discovered_count, &discovered_cap, &d)) {
            LOG_ERROR("Out of memory while collecting mod manifests");
            break;
        }
    } while (FindNextFileA(hFind, &fd));

    FindClose(hFind);

    if (discovered_count <= 0) {
        LOG_INFO("Done. %d mod folder(s) scanned, 0 loaded.", scanned);
        free(discovered);
        return scanned;
    }

    int* active = (int*)malloc(sizeof(int) * discovered_count);
    int* indegree = (int*)malloc(sizeof(int) * discovered_count);
    int* placed = (int*)malloc(sizeof(int) * discovered_count);
    int* ordered = (int*)malloc(sizeof(int) * discovered_count);
    uint8_t* edges = (uint8_t*)calloc((size_t)discovered_count * (size_t)discovered_count, sizeof(uint8_t));

    if (!active || !indegree || !placed || !ordered || !edges) {
        LOG_ERROR("Out of memory while resolving mod load order");
        free(active);
        free(indegree);
        free(placed);
        free(ordered);
        free(edges);
        free(discovered);
        return scanned;
    }

    for (int i = 0; i < discovered_count; i++) {
        active[i] = discovered[i].active ? 1 : 0;
        indegree[i] = 0;
        placed[i] = 0;
        ordered[i] = -1;
    }

    // Reject duplicate ids (fail closed: skip all duplicates).
    for (int i = 0; i < discovered_count; i++) {
        if (!active[i]) continue;
        for (int j = i + 1; j < discovered_count; j++) {
            if (!active[j]) continue;
            if (_stricmp(discovered[i].manifest.id, discovered[j].manifest.id) == 0) {
                LOG_ERROR("Skipping duplicate mod id \"%s\" (folders: %s, %s)",
                          discovered[i].manifest.id,
                          discovered[i].manifest.folder_name,
                          discovered[j].manifest.folder_name);
                active[i] = 0;
                active[j] = 0;
            }
        }
    }

    // Enforce API compatibility unless explicitly overridden.
    for (int i = 0; i < discovered_count; i++) {
        if (!active[i]) continue;
        const ModManifest* m = &discovered[i].manifest;
        if (m->api_version == MOD_API_VERSION) continue;
        if (m->allow_api_mismatch || global_allow_api_mismatch()) continue;
        LOG_ERROR("Skipping mod %s: api_version=%d but framework is %d (set allow_api_mismatch=true to override)",
                  m->id, m->api_version, MOD_API_VERSION);
        active[i] = 0;
    }

    // Repeatedly prune invalid dependency/conflict sets until stable.
    int changed = 1;
    while (changed) {
        changed = 0;

        // Dependency pruning (includes transitive pruning).
        for (int i = 0; i < discovered_count; i++) {
            if (!active[i]) continue;
            const ModManifest* m = &discovered[i].manifest;
            for (int d = 0; d < m->depends.count; d++) {
                const ModDepSpec* dep = &m->depends.items[d];
                int dep_idx = discovered_find_index_by_id(discovered, active, discovered_count, dep->id);
                if (dep_idx < 0 || dep_idx == i) {
                    LOG_ERROR("Skipping mod %s: unsatisfied dependency \"%s\"",
                              m->id, dep->id);
                    active[i] = 0;
                    changed = 1;
                    break;
                }
                if (dep->range[0] && !semver_satisfies_range(discovered[dep_idx].manifest.version, dep->range)) {
                    LOG_ERROR("Skipping mod %s: dependency \"%s\" version %s does not satisfy \"%s\"",
                              m->id,
                              dep->id,
                              discovered[dep_idx].manifest.version,
                              dep->range);
                    active[i] = 0;
                    changed = 1;
                    break;
                }
            }
        }

        // Conflict pruning (deterministic winner: priority desc, then id asc).
        for (int i = 0; i < discovered_count; i++) {
            if (!active[i]) continue;
            for (int j = i + 1; j < discovered_count; j++) {
                if (!active[j]) continue;
                const ModManifest* a = &discovered[i].manifest;
                const ModManifest* b = &discovered[j].manifest;
                if (!manifests_conflict(a, b)) continue;

                int loser = (manifest_precedence_cmp(a, b) <= 0) ? j : i;
                int winner = (loser == i) ? j : i;
                LOG_WARN("Conflict between %s and %s; keeping %s, skipping %s",
                         a->id, b->id,
                         discovered[winner].manifest.id,
                         discovered[loser].manifest.id);
                active[loser] = 0;
                changed = 1;
            }
        }
    }

    // Build ordering graph across remaining active manifests.
    for (int i = 0; i < discovered_count; i++) {
        if (!active[i]) continue;
        const ModManifest* m = &discovered[i].manifest;

        for (int d = 0; d < m->depends.count; d++) {
            const ModDepSpec* dep = &m->depends.items[d];
            int dep_idx = discovered_find_index_by_id(discovered, active, discovered_count, dep->id);
            if (dep_idx >= 0) add_manifest_edge(active, discovered_count, edges, indegree, dep_idx, i);
        }

        for (int d = 0; d < m->optional_deps.count; d++) {
            const ModDepSpec* dep = &m->optional_deps.items[d];
            int dep_idx = discovered_find_index_by_id(discovered, active, discovered_count, dep->id);
            if (dep_idx < 0 || dep_idx == i) continue;
            if (dep->range[0] && !semver_satisfies_range(discovered[dep_idx].manifest.version, dep->range)) {
                LOG_WARN("Ignoring optional_deps on %s: dependency \"%s\" version %s does not satisfy \"%s\"",
                         m->id,
                         dep->id,
                         discovered[dep_idx].manifest.version,
                         dep->range);
                continue;
            }
            add_manifest_edge(active, discovered_count, edges, indegree, dep_idx, i);
        }

        for (int d = 0; d < m->load_after.count; d++) {
            const char* target = m->load_after.ids[d];
            int target_idx = discovered_find_index_by_id(discovered, active, discovered_count, target);
            if (target_idx < 0) {
                LOG_WARN("Ignoring load_after on %s: target \"%s\" is not active", m->id, target);
                continue;
            }
            add_manifest_edge(active, discovered_count, edges, indegree, target_idx, i);
        }

        for (int d = 0; d < m->load_before.count; d++) {
            const char* target = m->load_before.ids[d];
            int target_idx = discovered_find_index_by_id(discovered, active, discovered_count, target);
            if (target_idx < 0) {
                LOG_WARN("Ignoring load_before on %s: target \"%s\" is not active", m->id, target);
                continue;
            }
            add_manifest_edge(active, discovered_count, edges, indegree, i, target_idx);
        }
    }

    int active_count = 0;
    for (int i = 0; i < discovered_count; i++) {
        if (active[i]) active_count++;
    }

    // Kahn topological sort with deterministic tie-breaks.
    int ordered_count = 0;
    while (ordered_count < active_count) {
        int pick = -1;
        for (int i = 0; i < discovered_count; i++) {
            if (!active[i] || placed[i]) continue;
            if (indegree[i] != 0) continue;
            if (pick < 0 || manifest_precedence_cmp(&discovered[i].manifest, &discovered[pick].manifest) < 0) {
                pick = i;
            }
        }

        if (pick < 0) {
            // Cycle fallback.
            for (int i = 0; i < discovered_count; i++) {
                if (!active[i] || placed[i]) continue;
                if (pick < 0 || manifest_precedence_cmp(&discovered[i].manifest, &discovered[pick].manifest) < 0) {
                    pick = i;
                }
            }
            if (pick < 0) break;
            LOG_WARN("Load-order cycle detected; breaking cycle at %s", discovered[pick].manifest.id);
        }

        placed[pick] = 1;
        ordered[ordered_count++] = pick;
        for (int to = 0; to < discovered_count; to++) {
            if (!active[to] || placed[to]) continue;
            size_t edge_idx = (size_t)pick * (size_t)discovered_count + (size_t)to;
            if (edges[edge_idx] && indegree[to] > 0) indegree[to]--;
        }
    }

    int loaded_count = 0;
    for (int oi = 0; oi < ordered_count; oi++) {
        int idx = ordered[oi];
        if (idx < 0 || idx >= discovered_count || !active[idx]) continue;
        ModManifest* mf = &discovered[idx].manifest;

        int blocked = 0;
        for (int d = 0; d < mf->depends.count; d++) {
            const ModDepSpec* dep = &mf->depends.items[d];
            int dep_idx = discovered_find_index_by_id(discovered, NULL, discovered_count, dep->id);
            if (dep_idx < 0 || !discovered[dep_idx].loaded) {
                LOG_ERROR("Skipping mod %s: dependency \"%s\" did not load",
                          mf->id, dep->id);
                blocked = 1;
                break;
            }
            if (dep->range[0] && !semver_satisfies_range(discovered[dep_idx].manifest.version, dep->range)) {
                LOG_ERROR("Skipping mod %s: dependency \"%s\" version %s does not satisfy \"%s\"",
                          mf->id,
                          dep->id,
                          discovered[dep_idx].manifest.version,
                          dep->range);
                blocked = 1;
                break;
            }
        }
        if (blocked) continue;

        for (int j = 0; j < discovered_count; j++) {
            if (!discovered[j].loaded) continue;
            if (manifests_conflict(mf, &discovered[j].manifest)) {
                LOG_WARN("Skipping mod %s: conflicts with already-loaded mod %s",
                         mf->id, discovered[j].manifest.id);
                blocked = 1;
                break;
            }
        }
        if (blocked) continue;

        LoadedMod* mod = mods_add();
        if (!mod) {
            LOG_ERROR("Out of memory while creating runtime slot for %s", mf->id);
            break;
        }

        if (!load_mod_lua(mod, mf)) {
            unload_single_mod_runtime(mod, 0);
            memset(mod, 0, sizeof(*mod));
            g_mod_count--;
            (void)rebuild_registered_assets_from_enabled_mods("failed mod load cleanup");
            continue;
        }

        discovered[idx].loaded = 1;
        loaded_count++;
    }

    LOG_INFO("Done. %d mod folder(s) scanned, %d candidate manifest(s), %d mod(s) loaded.",
             scanned, active_count, loaded_count);

    free(active);
    free(indegree);
    free(placed);
    free(ordered);
    free(edges);
    free(discovered);
    return scanned;
}

static uint64_t hot_reload_hash_bytes(uint64_t h, const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < len; i++) {
        h ^= (uint64_t)p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static uint64_t hot_reload_hash_path_ci(uint64_t h, const char* s) {
    if (!s) return h;
    for (size_t i = 0; s[i]; i++) {
        unsigned char c = (unsigned char)s[i];
        c = (unsigned char)tolower(c);
        h ^= (uint64_t)c;
        h *= 1099511628211ULL;
    }
    return h;
}

static uint64_t hot_reload_hash_entry(const char* rel_path, uint32_t attrs, uint64_t write_time, uint64_t size) {
    uint64_t h = 1469598103934665603ULL;
    h = hot_reload_hash_path_ci(h, rel_path);
    h = hot_reload_hash_bytes(h, &attrs, sizeof(attrs));
    h = hot_reload_hash_bytes(h, &write_time, sizeof(write_time));
    h = hot_reload_hash_bytes(h, &size, sizeof(size));
    return h;
}

static void hot_reload_sig_add(uint64_t* xor_accum, uint64_t* add_accum, uint64_t* count, uint64_t value) {
    if (xor_accum) *xor_accum ^= value;
    if (add_accum) *add_accum += (value * 0x9E3779B185EBCA87ULL);
    if (count) (*count)++;
}

static int hot_reload_should_ignore_path(const char* full_path) {
    if (!full_path || !full_path[0]) return 0;
    if (texture_ext_is_tracked_path(full_path)) return 1;
    if (font_ext_is_tracked_path(full_path)) return 1;
    for (int i = 0; i < g_mod_count; i++) {
        LoadedMod* mod = &g_mods[i];
        if (mod->config_path[0] && _stricmp(mod->config_path, full_path) == 0) return 1;
        if (mod->storage_path[0] && _stricmp(mod->storage_path, full_path) == 0) return 1;
        if (mod->binds_path[0] && _stricmp(mod->binds_path, full_path) == 0) return 1;
    }
    return 0;
}

static void hot_reload_scan_mod_dir(const char* full_dir, const char* rel_dir,
                                    uint64_t* xor_accum, uint64_t* add_accum, uint64_t* count) {
    if (!full_dir || !rel_dir) return;

    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", full_dir);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;

        char child_full[MAX_PATH];
        char child_rel[MAX_PATH];
        snprintf(child_full, sizeof(child_full), "%s\\%s", full_dir, fd.cFileName);
        snprintf(child_rel, sizeof(child_rel), "%s\\%s", rel_dir, fd.cFileName);

        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            _stricmp(fd.cFileName, "cache") == 0) {
            continue;
        }

        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            hot_reload_should_ignore_path(child_full)) {
            continue;
        }

        uint64_t write_time = ((uint64_t)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
        uint64_t size = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        uint64_t entry_hash = hot_reload_hash_entry(child_rel, fd.dwFileAttributes, write_time, size);
        hot_reload_sig_add(xor_accum, add_accum, count, entry_hash);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
            hot_reload_scan_mod_dir(child_full, child_rel, xor_accum, add_accum, count);
        }
    } while (FindNextFileA(hFind, &fd));

    FindClose(hFind);
}

static uint64_t hot_reload_compute_signature(void) {
    uint64_t xor_accum = 0;
    uint64_t add_accum = 0;
    uint64_t count = 0;

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA("mods\\*", &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        uint64_t marker = hot_reload_hash_entry("mods_missing", 0, 0, 0);
        hot_reload_sig_add(&xor_accum, &add_accum, &count, marker);
    } else {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (mods_dir_name_ignored(fd.cFileName)) continue;

            char mod_full[MAX_PATH];
            char mod_rel[MAX_PATH];
            snprintf(mod_full, sizeof(mod_full), "mods\\%s", fd.cFileName);
            snprintf(mod_rel, sizeof(mod_rel), "%s", fd.cFileName);

            uint64_t write_time = ((uint64_t)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
            uint64_t folder_hash = hot_reload_hash_entry(mod_rel, fd.dwFileAttributes, write_time, 0);
            hot_reload_sig_add(&xor_accum, &add_accum, &count, folder_hash);
            hot_reload_scan_mod_dir(mod_full, mod_rel, &xor_accum, &add_accum, &count);
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }

    uint64_t final_hash = 1469598103934665603ULL;
    final_hash = hot_reload_hash_bytes(final_hash, &xor_accum, sizeof(xor_accum));
    final_hash = hot_reload_hash_bytes(final_hash, &add_accum, sizeof(add_accum));
    final_hash = hot_reload_hash_bytes(final_hash, &count, sizeof(count));
    return final_hash;
}

static uint64_t hot_reload_compute_modset_signature(void) {
    uint64_t xor_accum = 0;
    uint64_t add_accum = 0;
    uint64_t count = 0;

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA("mods\\*", &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        uint64_t marker = hot_reload_hash_entry("mods_missing", 0, 0, 0);
        hot_reload_sig_add(&xor_accum, &add_accum, &count, marker);
    } else {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (mods_dir_name_ignored(fd.cFileName)) continue;

            uint64_t h = hot_reload_hash_entry(fd.cFileName, FILE_ATTRIBUTE_DIRECTORY, 0, 0);
            hot_reload_sig_add(&xor_accum, &add_accum, &count, h);
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }

    uint64_t final_hash = 1469598103934665603ULL;
    final_hash = hot_reload_hash_bytes(final_hash, &xor_accum, sizeof(xor_accum));
    final_hash = hot_reload_hash_bytes(final_hash, &add_accum, sizeof(add_accum));
    final_hash = hot_reload_hash_bytes(final_hash, &count, sizeof(count));
    return final_hash;
}

static void hot_reload_schedule_menu_state_reset_if_needed(int structural_change) {
    if (!structural_change) return;
    if (!p_state_switch) return;

    void* cur = ui_current_state_ptr();
    const char* st_name = ui_state_name_from_ptr(cur);
    if (!ui_is_menu_state_name(st_name)) return;

    void* target = cur;
    if (_stricmp(st_name, "main") == 0 || _stricmp(st_name, "main_initial") == 0) {
        target = (void*)(uintptr_t)ADDR_MAIN_STATE;
    }

    if (!target) return;
    g_pending_menu_state_ptr = target;
    g_pending_menu_state_reset = 1;
}

static void hot_reload_apply_pending_menu_state_reset(void) {
    if (!g_pending_menu_state_reset) return;

    void* target = g_pending_menu_state_ptr;
    g_pending_menu_state_ptr = NULL;
    g_pending_menu_state_reset = 0;

    if (!target || !p_state_switch) return;
    LOG_INFO("Hot reload: refreshing menu state after mod add/remove");
    p_state_switch(target);
}

static int reload_mod_runtime(const char* reason) {
    if (reason && reason[0]) {
        LOG_INFO("Hot reload: %s", reason);
    }

    g_unloading_for_shutdown = 0;
    unload_all_mods();
    texture_ext_reset_runtime_state();
    font_ext_reset_runtime_state();
    if (L) {
        lua_close(L);
        L = NULL;
    }

    if (!create_lua_runtime()) {
        LOG_ERROR("Hot reload: failed to recreate Lua runtime");
        return 0;
    }

    scan_and_load_mods();
    reset_runtime_ui_state();
    hooks_mods_menu_notify_reload();
    return 1;
}

static void hot_reload_poll(void) {
#if !AUTO_HOT_RELOAD_ENABLED
    return;
#else
    int tex_reloaded = texture_ext_poll_hot_reload();
    int font_reloaded = font_ext_poll_hot_reload();
    if ((tex_reloaded + font_reloaded) > 0) {
        if (!reload_engine_gfx_atlases("overlay png changed")) {
            LOG_WARN("Asset hot reload: atlas rebuild failed; restart may still be required");
        }
    }

    ULONGLONG now = GetTickCount64();
    if (now < g_hot_reload_next_poll_ms) return;
    g_hot_reload_next_poll_ms = now + HOT_RELOAD_INTERVAL_MS;

    uint64_t sig = hot_reload_compute_signature();
    if (sig == g_hot_reload_signature) {
        g_hot_reload_pending_signature = 0;
        return;
    }

    // Debounce: require seeing the same changed signature twice in a row.
    // This avoids reloading on transient intermediate states while files are
    // being copied/rewritten.
    if (sig != g_hot_reload_pending_signature) {
        g_hot_reload_pending_signature = sig;
        return;
    }

    uint64_t modset_sig_before = hot_reload_compute_modset_signature();
    int structural_change = (modset_sig_before != g_hot_reload_modset_signature);

    if (reload_mod_runtime("mods/ filesystem change detected")) {
        g_hot_reload_signature = hot_reload_compute_signature();
        g_hot_reload_modset_signature = hot_reload_compute_modset_signature();
        g_hot_reload_pending_signature = 0;
        g_hot_reload_next_poll_ms = GetTickCount64() + HOT_RELOAD_INTERVAL_MS;
        hot_reload_schedule_menu_state_reset_if_needed(structural_change);
    } else {
        LOG_ERROR("Hot reload failed; will retry on next poll.");
    }
#endif
}

// Global time-scale state (read by SDL time wrappers through exported API).
static float g_time_scale = 1.0f;
static int g_manual_time_scale_enabled = 0;
static float g_manual_time_scale = 1.0f;

void lua_manager_init() {
    font_ext_init();
    texture_ext_init();
    g_unloading_for_shutdown = 0;
    g_manual_time_scale_enabled = 0;
    g_manual_time_scale = 1.0f;
    g_time_scale = 1.0f;
    reset_runtime_ui_state();

    LOG_INFO("Mod framework API version: %d", MOD_API_VERSION);
    if (!create_lua_runtime()) return;

    scan_and_load_mods();
    g_hot_reload_signature = hot_reload_compute_signature();
    g_hot_reload_modset_signature = hot_reload_compute_modset_signature();
    g_hot_reload_pending_signature = 0;
    g_hot_reload_next_poll_ms = GetTickCount64() + HOT_RELOAD_INTERVAL_MS;
    g_pending_menu_state_reset = 0;
    g_pending_menu_state_ptr = NULL;
}

void lua_manager_shutdown() {
    g_unloading_for_shutdown = 1;
    unload_all_mods();
    if (L) {
        lua_close(L);
        L = NULL;
    }
    g_unloading_for_shutdown = 0;

    font_ext_shutdown();
    texture_ext_shutdown();
    audio_runtime_shutdown();
    orphan_ui_strings_free_all();
    g_hot_reload_signature = 0;
    g_hot_reload_next_poll_ms = 0;
    g_hot_reload_pending_signature = 0;
    g_hot_reload_modset_signature = 0;
    g_pending_menu_state_reset = 0;
    g_pending_menu_state_ptr = NULL;
    g_force_layout_refresh = 1;
    g_last_layout_state = (void*)-1;
    g_last_btn_count = -1;
    g_manual_time_scale_enabled = 0;
    g_manual_time_scale = 1.0f;
    g_time_scale = 1.0f;
}

// =============================
// Runtime callbacks
// =============================

float lua_manager_get_time_scale(void) {
    return g_time_scale;
}

int lua_manager_set_time_scale(float scale) {
    if (scale < 0.05f) scale = 0.05f;
    if (scale > 100.0f) scale = 100.0f;
    g_manual_time_scale = scale;
    g_manual_time_scale_enabled = 1;
    g_time_scale = scale;
    return 1;
}

void lua_manager_clear_time_scale(void) {
    g_manual_time_scale_enabled = 0;
    g_manual_time_scale = 1.0f;
}

int lua_manager_get_time_scale_manual(float* out_scale) {
    if (out_scale) *out_scale = g_manual_time_scale;
    return g_manual_time_scale_enabled;
}

int lua_manager_reload_mods(void) {
    if (!reload_mod_runtime("manual console reload.mods")) {
        return 0;
    }
    g_hot_reload_signature = hot_reload_compute_signature();
    g_hot_reload_modset_signature = hot_reload_compute_modset_signature();
    g_hot_reload_pending_signature = 0;
    g_hot_reload_next_poll_ms = GetTickCount64() + HOT_RELOAD_INTERVAL_MS;
    return 1;
}

int lua_manager_reload_assets(
    int* out_textures_reloaded,
    int* out_textures_failed,
    int* out_textures_restart_required,
    int* out_fonts_reloaded,
    int* out_fonts_failed,
    int* out_fonts_restart_required
) {
    int tex_reloaded = 0;
    int tex_failed = 0;
    int tex_restart = 0;
    int font_reloaded = 0;
    int font_failed = 0;
    int font_restart = 0;

    texture_ext_reload_all(&tex_reloaded, &tex_failed, &tex_restart);
    font_ext_reload_all(&font_reloaded, &font_failed, &font_restart);

    if ((tex_reloaded + font_reloaded) > 0) {
        if (!reload_engine_gfx_atlases("manual console reload.assets")) {
            tex_restart = tex_reloaded;
            font_restart = font_reloaded;
        } else {
            tex_restart = 0;
            font_restart = 0;
        }
    }

    if (out_textures_reloaded) *out_textures_reloaded = tex_reloaded;
    if (out_textures_failed) *out_textures_failed = tex_failed;
    if (out_textures_restart_required) *out_textures_restart_required = tex_restart;
    if (out_fonts_reloaded) *out_fonts_reloaded = font_reloaded;
    if (out_fonts_failed) *out_fonts_failed = font_failed;
    if (out_fonts_restart_required) *out_fonts_restart_required = font_restart;

    return (tex_failed + font_failed) == 0;
}

static int ui_handle_event(const char* type, int x, int y, int button) {
    if (!type) return 0;

    if (_stricmp(type, "mousemotion") == 0) {
        g_ui_mouse_x = x;
        g_ui_mouse_y = y;
        return 0;
    }

    if (_stricmp(type, "mousebuttondown") == 0) {
        g_ui_mouse_x = x;
        g_ui_mouse_y = y;
        if (button == 1) {
            g_ui_mouse_down_left = 1;
            g_ui_mouse_pressed_left = 1;
            return ui_hit_any_visible_button(x, y);
        }
        if (button == 2) {
            g_ui_mouse_down_middle = 1;
            g_ui_mouse_pressed_middle = 1;
            return ui_hit_any_visible_button(x, y);
        }
        if (button == 3) {
            g_ui_mouse_down_right = 1;
            g_ui_mouse_pressed_right = 1;
            return ui_hit_any_visible_button(x, y);
        }
        return 0;
    }

    if (_stricmp(type, "mousebuttonup") == 0) {
        g_ui_mouse_x = x;
        g_ui_mouse_y = y;
        if (button == 1) {
            g_ui_mouse_down_left = 0;
            return ui_hit_any_visible_button(x, y);
        }
        if (button == 2) {
            g_ui_mouse_down_middle = 0;
            return ui_hit_any_visible_button(x, y);
        }
        if (button == 3) {
            g_ui_mouse_down_right = 0;
            return ui_hit_any_visible_button(x, y);
        }
        return 0;
    }

    return 0;
}

double lua_manager_on_delta_time(double dt_seconds) {
    if (!L) {
        if (g_manual_time_scale_enabled) {
            g_time_scale = g_manual_time_scale;
            return dt_seconds * (double)g_manual_time_scale;
        }
        g_time_scale = 1.0f;
        return dt_seconds;
    }

    if (dt_seconds <= 0.0) {
        g_time_scale = g_manual_time_scale_enabled ? g_manual_time_scale : 1.0f;
        return dt_seconds;
    }

    // Build event table once
    lua_newtable(L);
    lua_pushstring(L, "delta_time"); lua_setfield(L, -2, "type");
    lua_pushnumber(L, dt_seconds);   lua_setfield(L, -2, "value");

    for (int mi = 0; mi < g_mod_count; mi++) {
        LoadedMod* mod = &g_mods[mi];
        if (!mod->enabled) continue;
        int* refs = NULL;
        int ref_count = 0;
        if (!reflist_snapshot(&mod->on_event, &refs, &ref_count)) {
            log_mod(mod, "ERROR", "on_event dispatch snapshot failed: out of memory");
            mod->error_count++;
            continue;
        }

        for (int i = 0; i < ref_count; i++) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, refs[i]);
            lua_pushvalue(L, -2); // event table
            if (lua_pcall(L, 1, 1, 0) != 0) {
                const char* err = lua_tostring(L, -1);
                char buf[512];
                snprintf(buf, sizeof(buf), "on_event error: %s", err ? err : "(unknown)");
                log_mod(mod, "ERROR", buf);
                lua_pop(L, 1);
                mod->error_count++;
                continue;
            }
            lua_pop(L, 1); // handler return
        }
        free(refs);
    }

    // Read back (potentially modified) dt
    double out = dt_seconds;
    lua_getfield(L, -1, "value");
    if (lua_isnumber(L, -1)) {
        out = lua_tonumber(L, -1);
    }
    lua_pop(L, 1); // value
    lua_pop(L, 1); // event

    // Update time scale multiplier (manual override is applied after mod events).
    double scale = out / dt_seconds;
    if (g_manual_time_scale_enabled) {
        scale = (double)g_manual_time_scale;
        out = dt_seconds * scale;
    }
    if (scale < 0.05) scale = 0.05;
    if (scale > 100.0)  scale = 100.0;
    g_time_scale = (float)scale;

    return out;
}

unsigned long long lua_manager_get_tick_count(void) {
    return g_game_tick_count;
}

void lua_manager_on_tick(void) {
    g_game_tick_count++;
    if (!L) return;

    for (int mi = 0; mi < g_mod_count; mi++) {
        LoadedMod* mod = &g_mods[mi];
        if (!mod->enabled) continue;
        int* refs = NULL;
        int ref_count = 0;
        if (!reflist_snapshot(&mod->on_tick, &refs, &ref_count)) {
            log_mod(mod, "ERROR", "on_tick dispatch snapshot failed: out of memory");
            mod->error_count++;
            continue;
        }

        for (int i = 0; i < ref_count; i++) {
            double started_ms = perf_now_ms();
            lua_rawgeti(L, LUA_REGISTRYINDEX, refs[i]);
            if (lua_pcall(L, 0, 0, 0) != 0) {
                const char* err = lua_tostring(L, -1);
                char buf[512];
                snprintf(buf, sizeof(buf), "on_tick error: %s", err ? err : "(unknown)");
                log_mod(mod, "ERROR", buf);
                lua_pop(L, 1);
                mod->error_count++;
            }
            mod_perf_counter_record(&mod->perf_tick, perf_now_ms() - started_ms);
        }
        free(refs);
    }
}

void lua_manager_on_tick_post(void) {
    if (!L) return;

    for (int mi = 0; mi < g_mod_count; mi++) {
        LoadedMod* mod = &g_mods[mi];
        if (!mod->enabled) continue;
        int* refs = NULL;
        int ref_count = 0;
        if (!reflist_snapshot(&mod->on_tick_post, &refs, &ref_count)) {
            log_mod(mod, "ERROR", "on_tick_post dispatch snapshot failed: out of memory");
            mod->error_count++;
            continue;
        }

        for (int i = 0; i < ref_count; i++) {
            double started_ms = perf_now_ms();
            lua_rawgeti(L, LUA_REGISTRYINDEX, refs[i]);
            if (lua_pcall(L, 0, 0, 0) != 0) {
                const char* err = lua_tostring(L, -1);
                char buf[512];
                snprintf(buf, sizeof(buf), "on_tick_post error: %s", err ? err : "(unknown)");
                log_mod(mod, "ERROR", buf);
                lua_pop(L, 1);
                mod->error_count++;
            }
            mod_perf_counter_record(&mod->perf_tick, perf_now_ms() - started_ms);
        }
        free(refs);
    }

    /* Custom-content tile behaviors run AFTER the native player update so that
     * velocity writes (bounce/spring) survive this frame's physics. */
    lua_manager_dispatch_tiles();
}

void lua_manager_on_frame() {
    hot_reload_poll();
    hot_reload_apply_pending_menu_state_reset();
    if (!L) return;
    void* state_ptr = ui_current_state_ptr();

    if (g_force_layout_refresh) {
        g_last_layout_state = (void*)-1;
        g_force_layout_refresh = 0;
    }

    // Detect state transitions and fire on_layout handlers.
    // main_layout() (and equivalents for other states) run synchronously in
    // each state's enter() before the first frame, so by the time we get here
    // the button list is fully built.
    if (state_ptr != g_last_layout_state) {
        // Normalise new state: treat main_initial as "main" so on_layout("main")
        // handlers see it as a main-menu entry regardless of which phase we're in.
        const char* new_name = ui_state_name_from_ptr(state_ptr);
        if (_stricmp(new_name, "main_initial") == 0) new_name = "main";

        // Do NOT normalise old_name — we want main_initial→main to count as a
        // real transition so the callback fires once the button list is stable.
        const char* old_name = ui_state_name_from_ptr(g_last_layout_state);

        // Only fire if the canonical name actually changed.
        if (_stricmp(new_name, old_name) != 0) {
            for (int mi = 0; mi < g_mod_count; mi++) {
                LoadedMod* mod = &g_mods[mi];
                if (!mod->enabled) continue;
                for (int i = 0; i < mod->on_layout_count; i++) {
                    LayoutHandler* h = &mod->on_layout[i];
                    double started_ms;
                    if (_stricmp(h->state_name, new_name) != 0) continue;
                    if (h->ref == LUA_NOREF || h->ref == LUA_REFNIL) continue;
                    started_ms = perf_now_ms();
                    lua_rawgeti(L, LUA_REGISTRYINDEX, h->ref);
                    if (lua_pcall(L, 0, 0, 0) != 0) {
                        const char* err = lua_tostring(L, -1);
                        char buf[512];
                        snprintf(buf, sizeof(buf), "on_layout('%s') error: %s",
                                 new_name, err ? err : "(unknown)");
                        log_mod(mod, "ERROR", buf);
                        lua_pop(L, 1);
                        mod->error_count++;
                    }
                    mod_perf_counter_record(&mod->perf_layout, perf_now_ms() - started_ms);
                }
            }
        }
        g_last_layout_state = state_ptr;
    }

    for (int mi = 0; mi < g_mod_count; mi++) {
        LoadedMod* mod = &g_mods[mi];
        mod_ui_reset_frame(mod, state_ptr);
    }

    // If the engine's button list was wiped (main_buttons_start was called by
    // a state enter), any cached native btn_ptrs are stale — the slots may now
    // hold a completely different state's buttons.  Detect this by watching for
    // the count to drop back to near-zero and null out all cached ptrs so the
    // native_button path re-creates them cleanly on the next frame.
    {
        int cur_count = p_button_count ? p_button_count() : -1;
        if (cur_count >= 0 && cur_count < 4 && g_last_btn_count >= 4) {
            for (int mi = 0; mi < g_mod_count; mi++) {
                LoadedMod* mod = &g_mods[mi];
                for (int bi = 0; bi < mod->ui_native_count; bi++) {
                    UiNativeButton* b = mod->ui_native_buttons[bi];
                    if (b) b->btn_ptr = NULL;
                }
            }
        }
        g_last_btn_count = cur_count;
    }

    for (int mi = 0; mi < g_mod_count; mi++) {
        LoadedMod* mod = &g_mods[mi];
        if (!mod->enabled) continue;
        int* refs = NULL;
        int ref_count = 0;
        if (!reflist_snapshot(&mod->on_frame, &refs, &ref_count)) {
            log_mod(mod, "ERROR", "on_frame dispatch snapshot failed: out of memory");
            mod->error_count++;
            continue;
        }

        for (int i = 0; i < ref_count; i++) {
            double started_ms = perf_now_ms();
            lua_rawgeti(L, LUA_REGISTRYINDEX, refs[i]);
            if (lua_pcall(L, 0, 0, 0) != 0) {
                const char* err = lua_tostring(L, -1);
                char buf[512];
                snprintf(buf, sizeof(buf), "on_frame error: %s", err ? err : "(unknown)");
                log_mod(mod, "ERROR", buf);
                lua_pop(L, 1);
                mod->error_count++;
            }
            mod_perf_counter_record(&mod->perf_frame, perf_now_ms() - started_ms);
        }
        free(refs);
    }

    // Always flush sprite batches after mod on_frame callbacks.  We fire from
    // SDL_GL_SwapWindow which is AFTER the game's entire render pass, so any
    // sprites still in the batch are either stale game leftovers or mod draws.
    // Without this flush, mod text/sprites carry over to the NEXT frame and
    // get rendered below tiles (the old "everything we draw is under the
    // tiles" bug).  Flushing here draws them on top, right before SwapWindow.
    ui_draw_default_custom_state_cursor();
    if (p_main_sprite_batches_draw) {
        p_main_sprite_batches_draw();
    }

    ui_reset_render_state();
    g_ui_default_custom_cursor_suppressed = 0;
    g_ui_mouse_pressed_left = 0;
    g_ui_mouse_pressed_middle = 0;
    g_ui_mouse_pressed_right = 0;

    for (int mi = 0; mi < g_mod_count; mi++) {
        LoadedMod* mod = &g_mods[mi];
        for (int bi = 0; bi < mod->bind_count; bi++) {
            mod->binds[bi].pressed = 0;
            mod->binds[bi].released = 0;
        }
    }
}

// Returns 1 if consumed by any mod (a handler returned true), else 0.
int lua_manager_on_event(const char* type, int sym, int scancode, int modmask, int x, int y, int button) {
    if (!L) return 0;
    int ui_consumed = ui_handle_event(type, x, y, button);

    // Build event table once
    lua_newtable(L);
    lua_pushstring(L, type);      lua_setfield(L, -2, "type");
    lua_pushinteger(L, sym);      lua_setfield(L, -2, "sym");
    lua_pushinteger(L, scancode); lua_setfield(L, -2, "scancode");
    lua_pushinteger(L, modmask);  lua_setfield(L, -2, "mod");
    lua_pushinteger(L, x);        lua_setfield(L, -2, "x");
    lua_pushinteger(L, y);        lua_setfield(L, -2, "y");
    lua_pushinteger(L, button);   lua_setfield(L, -2, "button");

    int consumed = ui_consumed;
    for (int mi = 0; mi < g_mod_count; mi++) {
        LoadedMod* mod = &g_mods[mi];
        int mod_consumed = 0;
        if (!mod->enabled) continue;
        int* refs = NULL;
        int ref_count = 0;
        if (!reflist_snapshot(&mod->on_event, &refs, &ref_count)) {
            log_mod(mod, "ERROR", "on_event dispatch snapshot failed: out of memory");
            mod->error_count++;
            continue;
        }

        for (int i = 0; i < ref_count; i++) {
            double started_ms = perf_now_ms();
            lua_rawgeti(L, LUA_REGISTRYINDEX, refs[i]);
            lua_pushvalue(L, -2); // event table
            if (lua_pcall(L, 1, 1, 0) != 0) {
                const char* err = lua_tostring(L, -1);
                char buf[512];
                snprintf(buf, sizeof(buf), "on_event error: %s", err ? err : "(unknown)");
                log_mod(mod, "ERROR", buf);
                lua_pop(L, 1);
                mod->error_count++;
                mod_perf_counter_record(&mod->perf_event, perf_now_ms() - started_ms);
                continue;
            }
            if (lua_toboolean(L, -1)) {
                consumed = 1;
                mod_consumed = 1;
            }
            lua_pop(L, 1); // handler return
            mod_perf_counter_record(&mod->perf_event, perf_now_ms() - started_ms);
        }
        free(refs);
        mod_trace_event_log(mod, type, sym, x, y, button, ref_count, mod_consumed);
    }

    lua_pop(L, 1); // event table
    return consumed;
}

void lua_manager_on_key_event(int sym, int is_down) {
    if (!L) return;

    for (int mi = 0; mi < g_mod_count; mi++) {
        LoadedMod* mod = &g_mods[mi];
        if (!mod->enabled) continue;
        for (int bi = 0; bi < mod->bind_count; bi++) {
            InputBinding* bind = &mod->binds[bi];
            int was_down;
            if (bind->sym == 0 || bind->sym != sym) continue;
            was_down = bind->down;
            bind->down = is_down ? 1 : 0;
            bind->pressed = (!was_down && is_down) ? 1 : 0;
            bind->released = (was_down && !is_down) ? 1 : 0;
        }
    }
}

static int loaded_mod_can_enable(const LoadedMod* mod, char* err, int err_sz) {
    LoadedMod* other = NULL;
    if (err && err_sz > 0) err[0] = '\0';
    if (!mod) {
        if (err && err_sz > 0) snprintf(err, err_sz, "invalid mod");
        return 0;
    }

    for (int i = 0; i < mod->depends.count; i++) {
        const ModDepSpec* dep = &mod->depends.items[i];
        other = get_mod_by_id_ci(dep->id);
        if (!other || !other->enabled) {
            if (err && err_sz > 0) {
                snprintf(err, err_sz, "required dependency \"%s\" is not enabled", dep->id);
            }
            return 0;
        }
        if (dep->range[0] && !semver_satisfies_range(other->version, dep->range)) {
            if (err && err_sz > 0) {
                snprintf(err, err_sz, "dependency \"%s\" version %s does not satisfy \"%s\"",
                         dep->id, other->version, dep->range);
            }
            return 0;
        }
    }

    for (int i = 0; i < mod->conflicts.count; i++) {
        other = get_mod_by_id_ci(mod->conflicts.ids[i]);
        if (!other || !other->enabled) continue;
        if (err && err_sz > 0) {
            snprintf(err, err_sz, "conflicts with enabled mod \"%s\"", other->id);
        }
        return 0;
    }

    for (int i = 0; i < g_mod_count; i++) {
        other = &g_mods[i];
        if (other == mod || !other->enabled) continue;
        if (!mod_id_list_contains(&other->conflicts, mod->id)) continue;
        if (err && err_sz > 0) {
            snprintf(err, err_sz, "enabled mod \"%s\" conflicts with it", other->id);
        }
        return 0;
    }

    return 1;
}

static int disable_mod_runtime_with_dependents(int mod_index) {
    int* to_disable = NULL;
    int changed = 1;
    int disabled_count = 0;

    if (mod_index < 0 || mod_index >= g_mod_count) return 0;
    to_disable = (int*)calloc((size_t)g_mod_count, sizeof(int));
    if (!to_disable) return 0;
    to_disable[mod_index] = 1;

    while (changed) {
        changed = 0;
        for (int i = 0; i < g_mod_count; i++) {
            LoadedMod* mod = &g_mods[i];
            if (to_disable[i] || !mod->enabled) continue;
            for (int j = 0; j < g_mod_count; j++) {
                if (!to_disable[j]) continue;
                if (!loaded_mod_requires_id(mod, g_mods[j].id)) continue;
                to_disable[i] = 1;
                changed = 1;
                break;
            }
        }
    }

    for (int i = g_mod_count - 1; i >= 0; i--) {
        LoadedMod* mod = &g_mods[i];
        if (!to_disable[i] || !mod->enabled) continue;
        unload_single_mod_runtime(mod, 1);
        mod->enabled = 0;
        disabled_count++;
    }

    free(to_disable);

    if (disabled_count > 0) {
        (void)rebuild_registered_assets_from_enabled_mods("mod disable/unload");
    }

    return disabled_count > 0;
}

static int enable_single_mod_runtime(int mod_index) {
    LoadedMod* mod = get_mod_by_index(mod_index);
    ModManifest manifest;
    char err[256];

    if (!mod) return 0;
    if (mod->enabled) return 1;
    if (!L) return 0;

    if (!loaded_mod_can_enable(mod, err, (int)sizeof(err))) {
        LOG_WARN("Cannot enable mod %s: %s", mod->id, err[0] ? err : "unsatisfied prerequisites");
        return 0;
    }

    loaded_mod_build_manifest(mod, &manifest);
    mod->error_count = 0;
    mod_perf_reset(mod);
    mod->enabled = 1;

    if (!load_mod_lua(mod, &manifest)) {
        unload_single_mod_runtime(mod, 0);
        mod->enabled = 0;
        (void)rebuild_registered_assets_from_enabled_mods("failed mod enable cleanup");
        return 0;
    }

    return 1;
}

// =============================
// Public C API (used by hooks.c)
// =============================

int lua_manager_framework_api(void) {
    return MOD_API_VERSION;
}

int lua_manager_get_mod_count(void) {
    return g_mod_count;
}

static LoadedMod* get_mod_by_index(int mod_index) {
    if (mod_index < 0 || mod_index >= g_mod_count) return NULL;
    return &g_mods[mod_index];
}

static LoadedMod* get_mod_by_id_ci(const char* mod_id) {
    if (!mod_id || !mod_id[0]) return NULL;
    for (int i = 0; i < g_mod_count; i++) {
        if (_stricmp(g_mods[i].id, mod_id) == 0) return &g_mods[i];
    }
    return NULL;
}

static void console_out_set(char* out, int out_sz, const char* text) {
    if (!out || out_sz <= 0) return;
    if (!text) text = "";
    strncpy(out, text, (size_t)out_sz - 1);
    out[out_sz - 1] = '\0';
}

static void console_out_append(char* out, int out_sz, int* pos, const char* text) {
    size_t len;
    int remain;
    if (!out || out_sz <= 0 || !pos || !text) return;
    if (*pos < 0) *pos = 0;
    if (*pos >= out_sz - 1) return;
    len = strlen(text);
    remain = (out_sz - 1) - *pos;
    if ((int)len > remain) len = (size_t)remain;
    if (len <= 0) return;
    memcpy(out + *pos, text, len);
    *pos += (int)len;
    out[*pos] = '\0';
}

static void console_lua_format_results(lua_State* Ls, int first_result_index, char* out, int out_sz) {
    int top = lua_gettop(Ls);
    int pos = 0;

    if (!out || out_sz <= 0) return;
    out[0] = '\0';

    if (first_result_index > top) {
        console_out_set(out, out_sz, "ok");
        return;
    }

    for (int i = first_result_index; i <= top; i++) {
        const char* s = NULL;
        int t;
        char tmp[128];
        if (i > first_result_index) {
            console_out_append(out, out_sz, &pos, " | ");
        }
        t = lua_type(Ls, i);
        switch (t) {
            case LUA_TNIL:
                s = "nil";
                break;
            case LUA_TBOOLEAN:
                s = lua_toboolean(Ls, i) ? "true" : "false";
                break;
            case LUA_TNUMBER:
            case LUA_TSTRING:
                s = lua_tostring(Ls, i);
                if (!s) s = "";
                break;
            default:
                snprintf(tmp, sizeof(tmp), "<%s:%p>", lua_typename(Ls, t), lua_topointer(Ls, i));
                s = tmp;
                break;
        }
        console_out_append(out, out_sz, &pos, s);
    }

    if (!out[0]) {
        console_out_set(out, out_sz, "ok");
    }
}

static int console_lua_eval_impl(const char* code, int use_env, int env_ref, char* out, int out_sz) {
    int top0;
    int status;
    int loaded_as_expr = 0;
    char* expr_chunk = NULL;

    if (!code || !code[0]) {
        console_out_set(out, out_sz, "empty lua code");
        return 0;
    }
    if (!L) {
        console_out_set(out, out_sz, "lua runtime is not initialized");
        return 0;
    }

    top0 = lua_gettop(L);

    {
        size_t n = strlen(code);
        expr_chunk = (char*)malloc(n + 8);
        if (expr_chunk) {
            memcpy(expr_chunk, "return ", 7);
            memcpy(expr_chunk + 7, code, n + 1);
            status = luaL_loadstring(L, expr_chunk);
            if (status == 0) loaded_as_expr = 1;
            free(expr_chunk);
            expr_chunk = NULL;
        } else {
            status = LUA_ERRMEM;
        }
    }

    if (!loaded_as_expr) {
        if (status != 0 && lua_gettop(L) > top0) {
            lua_pop(L, 1); // drop expression compile error
        }
        status = luaL_loadstring(L, code);
    }

    if (status != 0) {
        const char* err = lua_tostring(L, -1);
        console_out_set(out, out_sz, err ? err : "lua compile error");
        lua_settop(L, top0);
        return 0;
    }

    if (use_env) {
        if (env_ref == LUA_NOREF || env_ref == LUA_REFNIL) {
            console_out_set(out, out_sz, "target mod has no Lua environment");
            lua_settop(L, top0);
            return 0;
        }
        lua_rawgeti(L, LUA_REGISTRYINDEX, env_ref);
        if (!lua_istable(L, -1)) {
            console_out_set(out, out_sz, "target mod environment is not a table");
            lua_settop(L, top0);
            return 0;
        }
        lua_setfenv(L, -2);
    }

    status = lua_pcall(L, 0, LUA_MULTRET, 0);
    if (status != 0) {
        const char* err = lua_tostring(L, -1);
        console_out_set(out, out_sz, err ? err : "lua runtime error");
        lua_settop(L, top0);
        return 0;
    }

    console_lua_format_results(L, top0 + 1, out, out_sz);
    lua_settop(L, top0);
    return 1;
}

const char* lua_manager_get_mod_id(int mod_index) {
    LoadedMod* m = get_mod_by_index(mod_index);
    return m ? m->id : "";
}

int lua_manager_console_eval(const char* code, char* out, int out_sz) {
    return console_lua_eval_impl(code, 0, LUA_NOREF, out, out_sz);
}

int lua_manager_console_eval_mod(const char* mod_id, const char* code, char* out, int out_sz) {
    LoadedMod* m;
    if (!mod_id || !mod_id[0]) {
        console_out_set(out, out_sz, "missing mod id");
        return 0;
    }
    m = get_mod_by_id_ci(mod_id);
    if (!m) {
        console_out_set(out, out_sz, "mod not found");
        return 0;
    }
    return console_lua_eval_impl(code, 1, m->env_ref, out, out_sz);
}

int lua_manager_console_run_file(const char* path, char* out, int out_sz) {
    int top0;
    int status;
    if (!path || !path[0]) {
        console_out_set(out, out_sz, "missing file path");
        return 0;
    }
    if (!L) {
        console_out_set(out, out_sz, "lua runtime is not initialized");
        return 0;
    }

    top0 = lua_gettop(L);
    status = luaL_loadfile(L, path);
    if (status != 0) {
        const char* err = lua_tostring(L, -1);
        console_out_set(out, out_sz, err ? err : "failed to load lua file");
        lua_settop(L, top0);
        return 0;
    }

    status = lua_pcall(L, 0, LUA_MULTRET, 0);
    if (status != 0) {
        const char* err = lua_tostring(L, -1);
        console_out_set(out, out_sz, err ? err : "lua runtime error");
        lua_settop(L, top0);
        return 0;
    }

    console_lua_format_results(L, top0 + 1, out, out_sz);
    lua_settop(L, top0);
    return 1;
}

const char* lua_manager_get_mod_name(int mod_index) {
    LoadedMod* m = get_mod_by_index(mod_index);
    return m ? m->name : "";
}

const char* lua_manager_get_mod_version(int mod_index) {
    LoadedMod* m = get_mod_by_index(mod_index);
    return m ? m->version : "";
}

const char* lua_manager_get_mod_author(int mod_index) {
    LoadedMod* m = get_mod_by_index(mod_index);
    return m ? m->author : "";
}

const char* lua_manager_get_mod_description(int mod_index) {
    LoadedMod* m = get_mod_by_index(mod_index);
    return m ? m->description : "";
}

int lua_manager_get_mod_dependency_count(int mod_index) {
    LoadedMod* m = get_mod_by_index(mod_index);
    return m ? (m->depends.count + m->optional_deps.count) : 0;
}

const char* lua_manager_get_mod_dependency_id(int mod_index, int dep_index) {
    LoadedMod* m = get_mod_by_index(mod_index);
    if (!m || dep_index < 0) return "";
    if (dep_index < m->depends.count) return m->depends.items[dep_index].id;
    dep_index -= m->depends.count;
    if (dep_index < m->optional_deps.count) return m->optional_deps.items[dep_index].id;
    return "";
}

int lua_manager_get_mod_dependency_optional(int mod_index, int dep_index) {
    LoadedMod* m = get_mod_by_index(mod_index);
    if (!m || dep_index < 0) return 0;
    if (dep_index < m->depends.count) return 0;
    dep_index -= m->depends.count;
    return (dep_index >= 0 && dep_index < m->optional_deps.count) ? 1 : 0;
}

int lua_manager_mod_dependency_satisfied(int mod_index, int dep_index) {
    LoadedMod* m = get_mod_by_index(mod_index);
    const char* dep_id = lua_manager_get_mod_dependency_id(mod_index, dep_index);
    LoadedMod* other;
    if (!m || !dep_id[0]) return 0;
    other = get_mod_by_id_ci(dep_id);
    return (other && other->enabled) ? 1 : 0;
}

int lua_manager_get_mod_conflict_count(int mod_index) {
    LoadedMod* m = get_mod_by_index(mod_index);
    return m ? m->conflicts.count : 0;
}

size_t lua_manager_game_state_size(void) {
    return full_state_blob_size_for_thing_count(game_get_thing_count());
}

int lua_manager_game_state_save(void* dst, size_t dst_len, size_t* out_len, char* err, size_t err_cap) {
    size_t required = lua_manager_game_state_size();
    if (required == 0) {
        full_state_set_err(err, err_cap, "game state unavailable");
        return 0;
    }
    return full_state_capture_into(dst, dst_len, out_len, err, err_cap);
}

int lua_manager_game_state_load(const void* src, size_t src_len, char* err, size_t err_cap) {
    return full_state_apply_blob(src, src_len, err, err_cap);
}

static void full_state_sanitize_live_rollback_fields(void) {
    uintptr_t waterfall_fx = 0;

    if (ptr_readable((const void*)p_waterfall_fx, sizeof(uintptr_t))) {
        waterfall_fx = *p_waterfall_fx;
    }
    if (waterfall_fx && waterfall_fx < UINTPTR_MAX - 0x2cu &&
        ptr_writable((void*)(waterfall_fx + 0x2cu), sizeof(uint32_t))) {
        *(uint32_t*)(waterfall_fx + 0x2cu) = 0u;
    }
    if (ptr_writable((void*)p_waterfall_fx, sizeof(uintptr_t))) {
        *p_waterfall_fx = 0u;
    }
    if (ptr_writable((void*)p_waterfall_count, sizeof(int))) {
        *p_waterfall_count = 0;
    }
    if (ptr_writable((void*)p_crowd_sound_last_tick, sizeof(uint32_t))) {
        *p_crowd_sound_last_tick = 0u;
    }
    if (ptr_writable((void*)p_chant_step, sizeof(int))) {
        *p_chant_step = 0;
    }
    if (ptr_writable((void*)p_chant_timer, sizeof(int))) {
        *p_chant_timer = 0;
    }
    if (ptr_writable((void*)p_crowd_timer, sizeof(int))) {
        *p_crowd_timer = 0;
    }
    if (ptr_writable((void*)p_game_do_lerp_colours, sizeof(int))) {
        *p_game_do_lerp_colours = 0;
    }
    if (ptr_writable((void*)p_lerp_time, sizeof(int))) {
        *p_lerp_time = 0;
    }
    if (ptr_writable((void*)p_score_shudder, sizeof(int) * 2u)) {
        p_score_shudder[0] = 0;
        p_score_shudder[1] = 0;
    }
    if (ptr_writable((void*)p_resumed, sizeof(int))) {
        *p_resumed = 0;
    }
}

int lua_manager_game_state_load_rollback(const void* src, size_t src_len, char* err, size_t err_cap) {
    unsigned long long framework_tick_count = g_game_tick_count;
    int ok = full_state_apply_blob(src, src_len, err, err_cap);
    if (ok) {
        full_state_sanitize_live_rollback_fields();
    }
    g_game_tick_count = framework_tick_count;
    return ok;
}

int lua_manager_game_state_checksum(uint32_t* out_crc, char* err, size_t err_cap) {
    size_t blob_len = lua_manager_game_state_size();
    uint8_t* blob = NULL;
    uint32_t crc = 0;

    if (!out_crc) {
        full_state_set_err(err, err_cap, "checksum output pointer unavailable");
        return 0;
    }
    if (blob_len == 0) {
        full_state_set_err(err, err_cap, "game state unavailable");
        return 0;
    }

    blob = (uint8_t*)malloc(blob_len);
    if (!blob) {
        full_state_set_err(err, err_cap, "out of memory");
        return 0;
    }
    if (!lua_manager_game_state_save(blob, blob_len, &blob_len, err, err_cap)) {
        free(blob);
        return 0;
    }

    crc = full_state_crc32(blob, blob_len);
    free(blob);
    *out_crc = crc;
    return 1;
}

int lua_manager_game_state_canonicalize_rollback(void* blob, size_t blob_len, char* err, size_t err_cap) {
    return full_state_canonicalize_rollback_blob(blob, blob_len, err, err_cap);
}

int lua_manager_game_state_rollback_checksum(uint32_t* out_crc, char* err, size_t err_cap) {
    size_t blob_len = lua_manager_game_state_size();
    uint8_t* blob = NULL;
    uint32_t crc = 0;

    if (!out_crc) {
        full_state_set_err(err, err_cap, "checksum output pointer unavailable");
        return 0;
    }
    if (blob_len == 0) {
        full_state_set_err(err, err_cap, "game state unavailable");
        return 0;
    }

    blob = (uint8_t*)malloc(blob_len);
    if (!blob) {
        full_state_set_err(err, err_cap, "out of memory");
        return 0;
    }
    if (!lua_manager_game_state_save(blob, blob_len, &blob_len, err, err_cap)) {
        free(blob);
        return 0;
    }
    if (!full_state_canonicalize_rollback_checksum_blob(blob, blob_len, err, err_cap)) {
        free(blob);
        return 0;
    }

    crc = full_state_crc32(blob, blob_len);
    free(blob);
    *out_crc = crc;
    return 1;
}

int lua_manager_game_state_rollback_summary(LuaGameStateRollbackSummary* out_summary, char* err, size_t err_cap) {
    size_t blob_len = lua_manager_game_state_size();
    uint8_t* blob = NULL;
    int ok = 0;

    if (!out_summary) {
        full_state_set_err(err, err_cap, "summary output pointer unavailable");
        return 0;
    }
    if (blob_len == 0) {
        full_state_set_err(err, err_cap, "game state unavailable");
        return 0;
    }

    blob = (uint8_t*)malloc(blob_len);
    if (!blob) {
        full_state_set_err(err, err_cap, "out of memory");
        return 0;
    }
    if (!lua_manager_game_state_save(blob, blob_len, &blob_len, err, err_cap)) {
        free(blob);
        return 0;
    }

    /*
     * Diagnostic: capture the REAL gameplay rng seed before canonicalization
     * zeroes it. The summary's rng_seed field is transmitted to the peer and
     * logged in the desync detail (printed as rng=...), but it is NOT part of
     * any checksum or desync trigger, so stuffing the live seed here is purely
     * observational. It lets us see directly whether _mrand_seed has drifted
     * between the two machines at a desync frame (seeds differ => RNG drift;
     * seeds equal => a non-RNG thing field is non-deterministic).
     */
    {
        uint32_t real_seed = 0;
        if (blob_len >= sizeof(FullStateBlobHeader)) {
            real_seed = ((const FullStateBlobHeader*)blob)->rng_seed;
        }
        if (!full_state_canonicalize_rollback_checksum_blob(blob, blob_len, err, err_cap)) {
            free(blob);
            return 0;
        }
        ok = full_state_rollback_summary_from_canonical_blob(blob, blob_len, out_summary, err, err_cap);
        if (ok) {
            out_summary->rng_seed = real_seed;
        }
    }
    free(blob);
    return ok;
}

int lua_manager_game_rng_seed(uint32_t* out_seed) {
    if (!out_seed) return 0;
    if (!ptr_readable((const void*)p_mrand_seed, sizeof(uint32_t))) return 0;
    *out_seed = *p_mrand_seed;
    return 1;
}

int lua_manager_game_set_rng_seed(uint32_t seed) {
    if (!ptr_writable((void*)p_mrand_seed, sizeof(uint32_t))) return 0;
    *p_mrand_seed = seed;
    return 1;
}

int lua_manager_game_native_ticks(uint32_t* out_ticks) {
    if (!out_ticks) return 0;
    if (!ptr_readable((const void*)p_native_game_ticks, sizeof(uint32_t))) return 0;
    *out_ticks = *p_native_game_ticks;
    return 1;
}

int lua_manager_game_set_native_ticks(uint32_t ticks) {
    if (!ptr_writable((void*)p_native_game_ticks, sizeof(uint32_t))) return 0;
    *p_native_game_ticks = ticks;
    return 1;
}

const char* lua_manager_get_mod_conflict_id(int mod_index, int conflict_index) {
    LoadedMod* m = get_mod_by_index(mod_index);
    if (!m || conflict_index < 0 || conflict_index >= m->conflicts.count) return "";
    return m->conflicts.ids[conflict_index];
}

int lua_manager_mod_conflict_active(int mod_index, int conflict_index) {
    LoadedMod* m = get_mod_by_index(mod_index);
    const char* other_id = lua_manager_get_mod_conflict_id(mod_index, conflict_index);
    LoadedMod* other;
    if (!m || !other_id[0]) return 0;
    other = get_mod_by_id_ci(other_id);
    return (other && other->enabled) ? 1 : 0;
}

int lua_manager_get_mod_enabled(int mod_index) {
    LoadedMod* m = get_mod_by_index(mod_index);
    return m ? m->enabled : 0;
}

int lua_manager_get_mod_error_count(int mod_index) {
    LoadedMod* m = get_mod_by_index(mod_index);
    return m ? m->error_count : 0;
}

int lua_manager_get_mod_diagnostics(int mod_index, LuaModDiagnostics* out_diag) {
    LoadedMod* m = get_mod_by_index(mod_index);
    if (!m || !out_diag) return 0;

    memset(out_diag, 0, sizeof(*out_diag));
    out_diag->trace_events = m->trace_events ? 1 : 0;
    out_diag->on_frame_handlers = m->on_frame.count;
    out_diag->on_event_handlers = m->on_event.count;
    out_diag->on_layout_handlers = m->on_layout_count;
    out_diag->config_entries = m->cfg_count;
    out_diag->bind_entries = m->bind_count;
    out_diag->storage_entries = m->storage_count;
    out_diag->audio_chunks = m->audio_chunk_count;
    out_diag->font_registrations = m->font_reg_count;
    out_diag->texture_registrations = m->texture_reg_count;
    out_diag->approx_memory_bytes = mod_diag_estimate_memory_bytes(m);

    out_diag->frame_calls = m->perf_frame.call_count;
    out_diag->frame_last_ms = m->perf_frame.last_ms;
    out_diag->frame_avg_ms = (m->perf_frame.call_count > 0)
                           ? (m->perf_frame.total_ms / (double)m->perf_frame.call_count)
                           : 0.0;
    out_diag->frame_max_ms = m->perf_frame.max_ms;

    out_diag->event_calls = m->perf_event.call_count;
    out_diag->event_last_ms = m->perf_event.last_ms;
    out_diag->event_avg_ms = (m->perf_event.call_count > 0)
                           ? (m->perf_event.total_ms / (double)m->perf_event.call_count)
                           : 0.0;
    out_diag->event_max_ms = m->perf_event.max_ms;

    out_diag->layout_calls = m->perf_layout.call_count;
    out_diag->layout_last_ms = m->perf_layout.last_ms;
    out_diag->layout_avg_ms = (m->perf_layout.call_count > 0)
                            ? (m->perf_layout.total_ms / (double)m->perf_layout.call_count)
                            : 0.0;
    out_diag->layout_max_ms = m->perf_layout.max_ms;
    return 1;
}

int lua_manager_set_mod_trace_events(int mod_index, int enabled) {
    LoadedMod* m = get_mod_by_index(mod_index);
    if (!m) return 0;
    m->trace_events = enabled ? 1 : 0;
    return 1;
}

int lua_manager_set_mod_enabled(int mod_index, int enabled) {
    LoadedMod* m = get_mod_by_index(mod_index);
    if (!m) return 0;
    if (!!enabled == !!m->enabled) return 1;
    if (enabled) {
        return enable_single_mod_runtime(mod_index);
    }
    return disable_mod_runtime_with_dependents(mod_index);
}

int lua_manager_get_mod_config_count(int mod_index) {
    LoadedMod* m = get_mod_by_index(mod_index);
    return m ? m->cfg_count : 0;
}

int lua_manager_get_mod_config_type(int mod_index, int entry_index) {
    LoadedMod* m = get_mod_by_index(mod_index);
    if (!m) return LUA_CFG_NONE;
    if (entry_index < 0 || entry_index >= m->cfg_count) return LUA_CFG_NONE;
    return m->cfg_entries[entry_index].type;
}

const char* lua_manager_get_mod_config_key(int mod_index, int entry_index) {
    LoadedMod* m = get_mod_by_index(mod_index);
    if (!m) return "";
    if (entry_index < 0 || entry_index >= m->cfg_count) return "";
    return m->cfg_entries[entry_index].key;
}

const char* lua_manager_get_mod_config_label(int mod_index, int entry_index) {
    LoadedMod* m = get_mod_by_index(mod_index);
    if (!m) return "";
    if (entry_index < 0 || entry_index >= m->cfg_count) return "";
    return m->cfg_entries[entry_index].label;
}

const char* lua_manager_get_mod_config_value_str(int mod_index, int entry_index) {
    LoadedMod* m = get_mod_by_index(mod_index);
    if (!m) return "";
    if (entry_index < 0 || entry_index >= m->cfg_count) return "";
    return m->cfg_entries[entry_index].value;
}

int lua_manager_find_mod_config_index(int mod_index, const char* key) {
    LoadedMod* m = get_mod_by_index(mod_index);
    if (!m || !key || !key[0]) return -1;
    for (int i = 0; i < m->cfg_count; i++) {
        if (_stricmp(m->cfg_entries[i].key, key) == 0) return i;
    }
    return -1;
}

int lua_manager_get_mod_config_option_count(int mod_index, int entry_index) {
    LoadedMod* m = get_mod_by_index(mod_index);
    if (!m) return 0;
    if (entry_index < 0 || entry_index >= m->cfg_count) return 0;
    ConfigEntry* e = &m->cfg_entries[entry_index];
    return (e->type == LUA_CFG_ENUM) ? e->option_count : 0;
}

const char* lua_manager_get_mod_config_option(int mod_index, int entry_index, int option_index) {
    LoadedMod* m = get_mod_by_index(mod_index);
    if (!m) return "";
    if (entry_index < 0 || entry_index >= m->cfg_count) return "";
    ConfigEntry* e = &m->cfg_entries[entry_index];
    if (e->type != LUA_CFG_ENUM) return "";
    if (option_index < 0 || option_index >= e->option_count) return "";
    return e->options[option_index];
}

int lua_manager_get_mod_bind_count(int mod_index) {
    LoadedMod* m = get_mod_by_index(mod_index);
    return m ? m->bind_count : 0;
}

const char* lua_manager_get_mod_bind_key(int mod_index, int bind_index) {
    InputBinding* b;
    LoadedMod* m = get_mod_by_index(mod_index);
    if (!m) return "";
    b = mod_bind_by_index(m, bind_index);
    return b ? b->key : "";
}

const char* lua_manager_get_mod_bind_label(int mod_index, int bind_index) {
    InputBinding* b;
    LoadedMod* m = get_mod_by_index(mod_index);
    if (!m) return "";
    b = mod_bind_by_index(m, bind_index);
    return b ? b->label : "";
}

const char* lua_manager_get_mod_bind_value_str(int mod_index, int bind_index) {
    InputBinding* b;
    LoadedMod* m = get_mod_by_index(mod_index);
    if (!m) return "";
    b = mod_bind_by_index(m, bind_index);
    return b ? b->value_name : "";
}

int lua_manager_set_mod_bind_value(int mod_index, int bind_index, int sym) {
    InputBinding* b;
    LoadedMod* m = get_mod_by_index(mod_index);
    if (!m) return 0;
    b = mod_bind_by_index(m, bind_index);
    if (!b) return 0;
    b->sym = sym;
    mod_bind_update_name(b);
    return mod_bind_save(m);
}

int lua_manager_clear_mod_bind_value(int mod_index, int bind_index) {
    return lua_manager_set_mod_bind_value(mod_index, bind_index, 0);
}

int lua_manager_mod_bind_has_conflict(int mod_index, int bind_index) {
    LoadedMod* m = get_mod_by_index(mod_index);
    if (!m) return 0;
    return mod_bind_has_conflict(m, bind_index);
}

int lua_manager_config_toggle_bool(int mod_index, int entry_index) {
    LoadedMod* m = get_mod_by_index(mod_index);
    if (!m) return 0;
    if (entry_index < 0 || entry_index >= m->cfg_count) return 0;
    ConfigEntry* e = &m->cfg_entries[entry_index];
    if (e->type != LUA_CFG_BOOL) return 0;
    int b = !parse_bool(e->value);
    snprintf(e->value, sizeof(e->value), "%s", b ? "true" : "false");
    return mod_config_save(m);
}

int lua_manager_config_increment_int(int mod_index, int entry_index, int delta) {
    LoadedMod* m = get_mod_by_index(mod_index);
    if (!m) return 0;
    if (entry_index < 0 || entry_index >= m->cfg_count) return 0;
    ConfigEntry* e = &m->cfg_entries[entry_index];
    if (e->type != LUA_CFG_INT) return 0;
    int v = atoi(e->value);
    v += delta;
    if (e->has_min && (double)v < e->min_value) v = (int)e->min_value;
    if (e->has_max && (double)v > e->max_value) v = (int)e->max_value;
    snprintf(e->value, sizeof(e->value), "%d", v);
    return mod_config_save(m);
}

int lua_manager_config_increment_float(int mod_index, int entry_index, double delta) {
    LoadedMod* m = get_mod_by_index(mod_index);
    if (!m) return 0;
    if (entry_index < 0 || entry_index >= m->cfg_count) return 0;
    ConfigEntry* e = &m->cfg_entries[entry_index];
    if (e->type != LUA_CFG_FLOAT) return 0;
    double v = atof(e->value);
    v += delta;
    if (e->has_min && v < e->min_value) v = e->min_value;
    if (e->has_max && v > e->max_value) v = e->max_value;
    snprintf(e->value, sizeof(e->value), "%.6g", v);
    return mod_config_save(m);
}

int lua_manager_config_set_string(int mod_index, int entry_index, const char* value) {
    LoadedMod* m = get_mod_by_index(mod_index);
    if (!m) return 0;
    if (entry_index < 0 || entry_index >= m->cfg_count) return 0;
    ConfigEntry* e = &m->cfg_entries[entry_index];
    if (e->type != LUA_CFG_STRING) return 0;
    if (!value) value = "";
    strncpy(e->value, value, sizeof(e->value) - 1);
    e->value[sizeof(e->value) - 1] = '\0';
    return mod_config_save(m);
}

int lua_manager_config_cycle_option(int mod_index, int entry_index, int delta) {
    LoadedMod* m = get_mod_by_index(mod_index);
    if (!m) return 0;
    if (entry_index < 0 || entry_index >= m->cfg_count) return 0;
    ConfigEntry* e = &m->cfg_entries[entry_index];
    if (e->type != LUA_CFG_ENUM || e->option_count <= 0) return 0;
    int n = e->option_count;
    int idx = cfg_enum_find_option(e, e->value);
    if (idx < 0) idx = 0;
    idx = ((idx + delta) % n + n) % n;  // wrap around in both directions
    snprintf(e->value, sizeof(e->value), "%s", e->options[idx]);
    return mod_config_save(m);
}

int lua_manager_config_set_option(int mod_index, int entry_index, const char* value) {
    LoadedMod* m = get_mod_by_index(mod_index);
    if (!m) return 0;
    if (entry_index < 0 || entry_index >= m->cfg_count) return 0;
    ConfigEntry* e = &m->cfg_entries[entry_index];
    if (e->type != LUA_CFG_ENUM) return 0;
    int idx = cfg_enum_find_option(e, value);
    if (idx < 0) return 0;
    snprintf(e->value, sizeof(e->value), "%s", e->options[idx]);
    return mod_config_save(m);
}

void lua_manager_config_trigger_action(int mod_index, int entry_index) {
    LoadedMod* m = get_mod_by_index(mod_index);
    if (!m || !L) return;
    if (entry_index < 0 || entry_index >= m->cfg_count) return;
    ConfigEntry* e = &m->cfg_entries[entry_index];
    if (e->type != LUA_CFG_ACTION) return;

    // Find registered handlers.
    //
    // We snapshot refs before invoking callbacks so action handlers that mutate
    // config registrations (directly or indirectly) cannot invalidate the
    // ConfigAction pointer while this loop is running.
    for (int ai = 0; ai < m->cfg_action_count; ai++) {
        ConfigAction* a = &m->cfg_actions[ai];
        if (_stricmp(a->key, e->key) != 0) continue;

        if (a->handlers.count <= 0) break;

        int count = a->handlers.count;
        int* refs = (int*)malloc(sizeof(int) * count);
        if (!refs) {
            log_mod(m, "ERROR", "config action handler dispatch failed: out of memory");
            return;
        }

        for (int i = 0; i < count; i++) {
            refs[i] = a->handlers.refs[i];
        }

        for (int hi = 0; hi < count; hi++) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, refs[hi]);
            if (lua_pcall(L, 0, 0, 0) != 0) {
                const char* err = lua_tostring(L, -1);
                char buf[512];
                snprintf(buf, sizeof(buf), "config action '%s' error: %s", e->key, err ? err : "(unknown)");
                log_mod(m, "ERROR", buf);
                lua_pop(L, 1);
                m->error_count++;
            }
        }

        free(refs);
        break;
    }
}
