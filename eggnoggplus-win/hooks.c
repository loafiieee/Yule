#include <windows.h>

#ifdef __INTELLISENSE__
#define HOOKS_INTELLISENSE 1
#endif
#include <excpt.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include <GL/gl.h>
#ifdef __has_include
#  if __has_include(<GL/glext.h>)
#    include <GL/glext.h>
#  endif
#endif

/* Fallback defines for texture-combine constants (in case glext.h isn't available). */
#ifndef GL_COMBINE
#define GL_COMBINE 0x8570
#endif
#ifndef GL_COMBINE_RGB
#define GL_COMBINE_RGB 0x8571
#endif
#ifndef GL_SOURCE0_RGB
#define GL_SOURCE0_RGB 0x8580
#endif
#ifndef GL_SOURCE1_RGB
#define GL_SOURCE1_RGB 0x8581
#endif
#ifndef GL_OPERAND0_RGB
#define GL_OPERAND0_RGB 0x8590
#endif
#ifndef GL_OPERAND1_RGB
#define GL_OPERAND1_RGB 0x8591
#endif
#ifndef GL_COMBINE_ALPHA
#define GL_COMBINE_ALPHA 0x8572
#endif
#ifndef GL_SOURCE0_ALPHA
#define GL_SOURCE0_ALPHA 0x8588
#endif
#ifndef GL_OPERAND0_ALPHA
#define GL_OPERAND0_ALPHA 0x8598
#endif
#ifndef GL_PREVIOUS
#define GL_PREVIOUS 0x8578
#endif

#ifndef SDLK_F2
#define SDLK_F2 1073741883
#endif
#ifndef SDLK_F3
#define SDLK_F3 1073741884
#endif
#ifndef SDLK_F4
#define SDLK_F4 1073741885
#endif
#ifndef SDLK_F5
#define SDLK_F5 1073741886
#endif
#ifndef SDLK_F6
#define SDLK_F6 1073741887
#endif
#ifndef SDLK_F7
#define SDLK_F7 1073741888
#endif
#ifndef SDLK_F9
#define SDLK_F9 1073741890
#endif
#ifndef SDLK_F10
#define SDLK_F10 1073741891
#endif

#include "hooks.h"
#include "ggpo_ext.h"
#include "ggpo_loopback.h"
#include "ggpo_local.h"
#include "ggpo_net.h"
#include "lua_manager.h"
#include "font_ext.h"
#include "texture_ext.h"
#include "custom_maps.h"
#include "log.h"
#include "net_ext.h"
#include "fwsettings.h"

extern char* SDL_GetClipboardText(void);
extern int SDL_SetClipboardText(const char* text);
extern void SDL_free(void* mem);


#define ADDR_STATE_CURRENT            0x405DB0u
#define ADDR_STATE_LAST               0x405DB8u
#define ADDR_STATE_SWITCH             0x405DC0u
#define ADDR_MAIN_UPDATE_WITH_BUTTONS 0x4340E0u
#define ADDR_GAME_UPDATE              0x42C590u
#define ADDR_MAD_TICKS                0x45F160u
#define ADDR_DEBUG                    0x541E04u
#define ADDR_DEBUG_SLOWMO             0x547BA4u
#define ADDR_MAIN_DRAW              0x4331A0u
#define ADDR_MENU_COMMON_RENDER       0x4334E0u
#define ADDR_MAIN_BUTTONS_START       0x432FD0u
#define ADDR_GAME_RESET               0x42F750u
#define ADDR_GAME_START               0x42F760u
#define ADDR_MAIN_CURSORS_RESET       0x431200u
#define ADDR_MAIN_CURSOR_SPIN        0x4311E0u
#define ADDR_MAIN_CURSOR_DATA        0x549140u
#define ADDR_MAIN_SPRITE_BATCHES_DRAW 0x431890u
#define ADDR_SPRITE_BATCH_PLOT        0x405890u
#define ADDR_SPRITE_BATCH_DRAW        0x405A90u
#define ADDR_ATLAS_UPLOAD             0x401480u
#define ADDR_MENU_BUTTON_LINK         0x433950u
#define ADDR_BUTTON_GET              0x415D00u
#define ADDR_BUTTON_COUNT            0x416890u
#define ADDR_BUTTON_EX               0x416310u
#define ADDR_BUTTON_SET_LAYOUT        0x415F80u
#define ADDR_BTN_PLAYER_FILTER        0x4325C0u
#define ADDR_PLOT_TEXT                0x4304E0u
#define ADDR_PLOT_TEXT_SET_SHADOW     0x430390u
#define ADDR_TURTLE_TRANS             0x409210u
#define ADDR_TURTLE_SET_ANGLE         0x409000u
#define ADDR_TURTLE_SET_POS_UNSCALED  0x409040u
#define ADDR_TURTLE_SET_SCALE         0x409080u
#define ADDR_TURTLE_SET_RGB           0x4091E0u
#define ADDR_TURTLE_SET_RGBA          0x4090C0u
#define ADDR_TURTLE_RESET             0x4092D0u
#define ADDR_MAD_W                    0x404300u
#define ADDR_MAD_H                    0x404320u
#define ADDR_OPTIONS_STATE            0x448398u
#define ADDR_OPTIONS_STATE_PAUSED     0x448388u
#define ADDR_GAME_STATE               0x448220u
#define ADDR_PAUSED                   0x549120u
#define ADDR_MAIN_STATE               0x448350u
#define ADDR_MAIN_STATE_INITIAL       0x448340u
#define ADDR_PREGAME_STATE            0x4483A8u
#define ADDR_GAME_OLD_ACTIVE_ROOM     0x448330u
#define ADDR_RESUMED                  0x448334u
#define ADDR_MAP_SELECTOR             0x55A2F4u
#define ADDR_ROUND_END_ANY            0x55A304u
#define ADDR_GAME_ACTIVE_ROOM         0x541E08u
#define ADDR_WATERFALL_FX            0x541E44u
#define ADDR_WATERFALL_COUNT         0x542038u
#define ADDR_START_COUNTDOWN          0x542048u
#define ADDR_OPTIONS_ENTER            0x4381F0u
#define ADDR_OPTIONS_ENTER_PAUSED     0x438200u
#define ADDR_MAIN_PLAYER_POLL_CMDS    0x433F90u
#define ADDR_MAPGEN_INIT              0x437D30u
#define ADDR_HIGH_WATER_ACTION        0x43C730u
#define ADDR_GAME_WATER_HI_COLOUR     0x420100u
#define ADDR_GAME_WATER_COLOUR        0x4201D0u
#define ADDR_GAME_PLAYER_COLOUR_INDEX 0x41FE40u
#define ADDR_GAME_SET_PLAYER_COLOUR_INDEX 0x41FE60u
#define ADDR_GAME_PLAYER_COLOUR       0x4207C0u
#define ADDR_GAME_INC_PLAYER_COLOUR_EX 0x420E30u
#define ADDR_ANGLE_COLOUR             0x4180F0u
#define ADDR_DRAW_PLAYER_BODY         0x41BDD0u
#define ADDR_PLAYER_ARRAY             0x542058u
#define ADDR_CAMERA_X                 0x55A360u
#define ADDR_CAMERA_Y                 0x55A364u
#define ADDR_GAME_W                   0x55A394u
#define ADDR_GAME_H                   0x55A324u
#define ADDR_LAYER                    0x55A33Cu
#define ADDR_TURTLE_R                 0x448110u
#define ADDR_TURTLE_G                 0x448114u
#define ADDR_TURTLE_B                 0x448118u
#define ADDR_TURTLE_A                 0x44811Cu
#define ADDR_GAME_STARTED             0x54203Cu
#define ADDR_PLAYER_CLR_INDEX         0x448320u
#define ADDR_PLAYER_COLOURS           0x448240u
#define ADDR_LEADER                   0x541E0Cu
#define ADDR_LOSER                    0x542064u
#define ADDR_END_COUNTDOWN            0x542044u
#define ADDR_SCORE_TARGET             0x55A30Cu
#define ADDR_SCORE_PLAYER0            0x55A314u
#define ADDR_SCORE_PLAYER1            0x55A318u
#define ADDR_GAME_TICKS               0x547BA0u
#define ADDR_NATIVE_SYNTH_ENABLED     0x54C0CAu
/* _game_settings sound flag (DAT_0055a170): 1=sound on, 0=off. Loaded from
 * settings.nogg in app_state_init, but the engine force-enables the synth there
 * AFTER the load and never re-applies the setting, so it only takes effect once
 * main_update_settings runs (an options toggle). We apply it once at startup. */
#define ADDR_SOUND_SETTING            0x55A170u
#define ADDR_SEED                     0x542074u
#define ADDR_MRAND_SEED               0x496DA0u
#define ADDR_MRAND                    0x405080u
#define ADDR_RND                      0x4050B0u
#define ADDR_FRND                     0x405170u
#define ADDR_RND5050                  0x405210u
#define ADDR_RNDSIGN                  0x4052C0u
#define ADDR_RESPAWN_WARBLE           0x41BB70u
#define ADDR_SYNTH_EFFECT_WHISTLING   0x41BBD0u
#define ADDR_SOUND_SWORD_CHING        0x425D70u
#define ADDR_SYNTH_EFFECTS_INIT       0x408780u
#define ADDR_SYN_ENABLE_RANGE         0x406F40u
#define ADDR_SYNTH_ENGINE             0x5540E0u

#define PLAYER_SIZE                   0x15Cu
#define PLAYER_OFS_X                  0x24u
#define PLAYER_OFS_Y                  0x28u
#define PLAYER_OFS_ROOM               0x9Bu

// Asset load hook used for moddable font glyph overlays.
#define ADDR_RGBA_LOAD                0x4022A0u

#define MAX_MENU_ROWS     2048
#define MAX_MODS_TRACKED   512
#define CAPTURE_BUF_SIZE   256
#define BASE_UI_W        1280.0f
#define BASE_UI_H         720.0f
#define MODS_CURSOR_ROW_Y_FACTOR   0.50f
#define MODS_CURSOR_ROW_Y_NUDGE    0.0f
#define MODS_CURSOR_OUTER_PAD_X   22.0f

#define MODS_BTN_GRID_X      1.0f
#define MODS_BTN_GRID_Y      0.0f

#define BTN_OFS_CENTER_X          0x10
#define BTN_OFS_CENTER_Y          0x14
#define BTN_OFS_WIDTH             0x20
#define BTN_OFS_HEIGHT            0x24
#define BTN_OFS_LABEL_PTR         0xC8
#define BTN_OFS_LINK_PTR          0xE0
#define BTN_OFS_ACTION_PTR        0xE4
#define BTN_OFS_NOLINK_FLAG       0xBD

#define MAIN_START_ACTION_PTR     0x432440u

#define SDLK_BACKSPACE      8
#define SDLK_TAB            9
#define SDLK_RETURN        13
#define SDLK_ESCAPE        27
#define SDLK_SPACE         32
#define SDLK_DELETE       127
#define SDLK_HOME 1073741898
#define SDLK_END  1073741901
#define SDLK_PAGEUP 1073741899
#define SDLK_PAGEDOWN 1073741902
#define SDLK_RIGHT 1073741903
#define SDLK_LEFT  1073741904
#define SDLK_DOWN  1073741905
#define SDLK_UP    1073741906
#define SDLK_KP_ENTER 1073741912

#define KMOD_SHIFT 0x0003
#define KMOD_CTRL  0x00C0

#define CONSOLE_INPUT_BUF      512
#define CONSOLE_LINE_TEXT      384
#define CONSOLE_MAX_LINES      256
#define CONSOLE_HISTORY_MAX     64
#define CONSOLE_BG_DOWNSAMPLE    4
#define CONSOLE_MAX_MATCHES       32
#define MAX_CUSTOM_STATES         16
#define CUSTOM_STATE_NAME_MAX     64
#define GGPO_SELFTEST_FRAMES_PER_TICK 2
#define ONLINE_HUB_MAX_ROWS      160
#define ONLINE_HUB_MAX_FRIENDS    32
#define ONLINE_HUB_MAX_INBOX      32
#define ONLINE_HUB_TEXT_MAX      128
#define ONLINE_HUB_STATUS_MAX    256
#define ONLINE_HUB_CFG_PATH      "mods\\online_hub.cfg"
#define ONLINE_DEFAULT_SERVER_HOST "eggnogg.loafiieee.com"
#define ONLINE_DEFAULT_SERVER_PORT 47778
#define ONLINE_ABANDON_GRACE_TICKS 90
#define ONLINE_MATCH_COUNTDOWN_FRAMES 150
#define ONLINE_RESULT_TOAST_FRAMES 420
#define ONLINE_CHALLENGE_TOAST_FADE_FRAMES 30

typedef struct GameState {
    void (__cdecl *enter)(void);
    void (__cdecl *update)(void);
    void (__cdecl *render)(void);
    void (__cdecl *leave)(void);
} GameState;

typedef struct HookCustomState {
    int used;
    char name[CUSTOM_STATE_NAME_MAX];
    void* return_state;
    GameState state;
} HookCustomState;

typedef enum RowKind {
    ROW_NONE = 0,
    ROW_MOD_HEADER,
    ROW_DIVIDER,
    ROW_MOD_TOGGLE,
    ROW_CONFIG,
    ROW_BIND,
    ROW_BACK,
    ROW_INFO,
    ROW_FW_HEADER,   // "Framework" section title (non-selectable)
    ROW_FW_TOGGLE,   // framework on/off option (cfg_index = FwToggle id)
    ROW_FW_ACTION,   // framework button (cfg_index = FwAction id)
} RowKind;

// Framework-section row identifiers (stored in MenuRow.cfg_index; mod_index = -1).
enum { FW_TOGGLE_HIDE_CONSOLE = 0 };
enum { FW_ACTION_RELOAD_MODS = 0 };

typedef enum CaptureKind {
    CAPTURE_NONE = 0,
    CAPTURE_CONFIG_STRING,
    CAPTURE_BIND,
} CaptureKind;

typedef struct MenuRow {
    RowKind kind;
    int selectable;
    int mod_index;
    int cfg_index;
    char left[128];
    char right[192];
} MenuRow;

typedef struct RowKey {
    RowKind kind;
    int mod_index;
    int cfg_index;
} RowKey;

typedef enum OnlineHubTab {
    ONLINE_TAB_PLAY = 0,
    ONLINE_TAB_FRIENDS = 1,
    ONLINE_TAB_SETTINGS = 2,
    ONLINE_TAB_COUNT = 3,
} OnlineHubTab;

typedef enum OnlineHubRowKind {
    ONLINE_ROW_NONE = 0,
    ONLINE_ROW_INFO,
    ONLINE_ROW_ACTION,
    ONLINE_ROW_SETTING,
    ONLINE_ROW_FRIEND,
    ONLINE_ROW_FRIEND_REQUEST,
    ONLINE_ROW_CHALLENGE,
    ONLINE_ROW_BACK,
} OnlineHubRowKind;

typedef enum OnlineHubAction {
    ONLINE_ACTION_NONE = 0,
    ONLINE_ACTION_CONNECT,
    ONLINE_ACTION_LOGIN,
    ONLINE_ACTION_REGISTER,
    ONLINE_ACTION_DISCONNECT_SERVER,
    ONLINE_ACTION_QUEUE_CASUAL,
    ONLINE_ACTION_QUEUE_COMPETITIVE,
    ONLINE_ACTION_LEAVE_QUEUE,
    ONLINE_ACTION_HOST,
    ONLINE_ACTION_JOIN,
    ONLINE_ACTION_STOP,
    ONLINE_ACTION_ADD_FRIEND,
    ONLINE_ACTION_CHALLENGE_FRIEND,
    ONLINE_ACTION_ACCEPT_FRIEND,
    ONLINE_ACTION_DECLINE_FRIEND,
    ONLINE_ACTION_ACCEPT_CHALLENGE,
    ONLINE_ACTION_DECLINE_CHALLENGE,
    ONLINE_ACTION_REMOVE_FRIEND,
    ONLINE_ACTION_BLOCK_FRIEND,
    ONLINE_ACTION_SAVE_SETTINGS,
} OnlineHubAction;

typedef enum OnlineHubSetting {
    ONLINE_SETTING_NONE = 0,
    ONLINE_SETTING_USERNAME,
    ONLINE_SETTING_PASSWORD,
    ONLINE_SETTING_SERVER_HOST,
    ONLINE_SETTING_SERVER_PORT,
    ONLINE_SETTING_PEER_HOST,
    ONLINE_SETTING_PEER_PORT,
    ONLINE_SETTING_LOCAL_PORT,
    ONLINE_SETTING_P2P_ENABLED,
    ONLINE_SETTING_RELAY_FALLBACK,
    ONLINE_SETTING_INPUT_DELAY,
    ONLINE_SETTING_FRAME_ADVANTAGE,
    ONLINE_SETTING_MAX_PREDICTION,
    ONLINE_SETTING_CORRECTION,
    ONLINE_SETTING_SIM_LOSS,
    ONLINE_SETTING_SIM_MIN_DELAY,
    ONLINE_SETTING_SIM_MAX_DELAY,
    ONLINE_SETTING_CHALLENGE_NOTIFICATIONS,
} OnlineHubSetting;

typedef enum OnlineHubCaptureKind {
    ONLINE_CAPTURE_NONE = 0,
    ONLINE_CAPTURE_SETTING,
    ONLINE_CAPTURE_ADD_FRIEND,
} OnlineHubCaptureKind;

typedef enum OnlineMatchResult {
    ONLINE_MATCH_RESULT_NONE = 0,
    ONLINE_MATCH_RESULT_WIN = 1,
    ONLINE_MATCH_RESULT_LOSS = -1,
    ONLINE_MATCH_RESULT_DRAW = 2,
} OnlineMatchResult;

typedef struct OnlineHubRow {
    OnlineHubRowKind kind;
    int selectable;
    int id;
    int aux;
    char left[128];
    char right[192];
} OnlineHubRow;

typedef struct OnlineFriend {
    char name[48];
    char host[ONLINE_HUB_TEXT_MAX];
    uint16_t port;
    int blocked;
    int elo;
    int online;
} OnlineFriend;

typedef struct OnlineFriendRequest {
    char name[48];
    int elo;
} OnlineFriendRequest;

typedef struct OnlineChallenge {
    int id;
    char from[48];
    int elo;
    int expires_in;
} OnlineChallenge;

typedef struct OnlineSentChallenge {
    char username[48];
    int id;
    uint32_t expires_ms;
} OnlineSentChallenge;

typedef struct OnlineChallengeToast {
    int active;
    int age;
    int id;
    int elo;
    uint32_t expires_ms;
    char from[48];
} OnlineChallengeToast;

typedef struct OnlineHubConfig {
    char username[48];
    char password[96];
    char server_host[ONLINE_HUB_TEXT_MAX];
    uint16_t server_port;
    char peer_host[ONLINE_HUB_TEXT_MAX];
    uint16_t peer_port;
    uint16_t local_port;
    int p2p_enabled;
    int relay_fallback;
    int input_delay;
    int max_frame_advantage;
    int max_prediction;
    int correction_enabled;
    int sim_loss;
    int sim_min_delay;
    int sim_max_delay;
    int challenge_notifications;
} OnlineHubConfig;

typedef struct OnlineLayout {
    float w;
    float h;
    float ui;
    float text_scale;
    float panel_x;
    float panel_y;
    float panel_w;
    float panel_h;
    float header_h;
    float footer_h;
    float content_x;
    float content_y;
    float content_w;
    float content_h;
    float row_h;
    float list_top;
    float list_bottom;
    float left_x;
    float right_x;
    float center_x;
} OnlineLayout;

typedef struct ConsoleLine {
    char text[CONSOLE_LINE_TEXT];
    float r;
    float g;
    float b;
} ConsoleLine;

typedef struct Detour {
    void* target;
    uint8_t original[16];
    size_t length;
    void* trampoline;
} Detour;

typedef void* (__cdecl *fn_state_current_t)(void);
typedef void* (__cdecl *fn_state_switch_t)(void*);
typedef int   (__cdecl *fn_main_update_with_buttons_t)(int);
typedef void  (__cdecl *fn_game_update_t)(int);
typedef void  (__cdecl *fn_void_void_t)(void);
typedef void  (__cdecl *fn_main_cursors_reset_t)(float, float);
typedef void  (__cdecl *fn_main_cursor_spin_t)(int);
typedef void  (__cdecl *fn_main_sprite_batches_draw_t)(void);
typedef void  (__cdecl *fn_sprite_batch_plot_t)(int sprite, int flip, int layer);
typedef void  (__cdecl *fn_sprite_batch_draw_t)(int atlas);
typedef int   (__cdecl *fn_atlas_upload_t)(int atlas, int arg2, int format);
typedef void* (__cdecl *fn_menu_button_link_t)(float, float, const char*, void*);
typedef void* (__cdecl *fn_button_ex_t)(float, float, uint32_t, const char*, int);
typedef void* (__cdecl *fn_button_get_t)(int);
typedef int   (__cdecl *fn_button_count_t)(void);
typedef void  (__cdecl *fn_button_set_layout_t)(float, float);
typedef int   (__cdecl *fn_btn_player_filter_t)(void* btn, int event_code);
typedef void  (__cdecl *fn_plot_text_t)(const char*, int);
typedef void  (__cdecl *fn_plot_text_set_shadow_t)(float, float, float, float);
typedef void  (__cdecl *fn_turtle_trans_t)(double, double);
typedef void  (__cdecl *fn_turtle_set_angle_t)(double);
typedef void  (__cdecl *fn_turtle_set_pos_unscaled_t)(double, double);
typedef void  (__cdecl *fn_turtle_set_scale_t)(double, double);
typedef void  (__cdecl *fn_turtle_set_rgb_t)(float, float, float);
typedef void  (__cdecl *fn_turtle_set_rgba_t)(float, float, float, float);
typedef void  (__cdecl *fn_turtle_reset_t)(void);
typedef float (__cdecl *fn_mad_dim_t)(void);
typedef RgbaImage* (__cdecl *fn_rgba_load_t)(const char*);
typedef uint32_t (__cdecl *fn_main_player_poll_cmds_t)(uint32_t, uint32_t);
typedef int (__cdecl *fn_tile_action_t)(void*, int, int, int, int);
typedef void (__cdecl *fn_colour_query_t)(float*);
typedef int (__cdecl *fn_synth_callback_t)(void*);
typedef void (__cdecl *fn_synth_effects_init_t)(int, int);
typedef void* (__cdecl *fn_sound_sword_ching_t)(float, float);
typedef void (__cdecl *fn_syn_enable_range_t)(int, uint32_t, uint32_t, int);
typedef int   (__cdecl *fn_game_player_colour_index_t)(uint32_t, int);
typedef int   (__cdecl *fn_game_set_player_colour_index_t)(uint32_t, int, uint32_t);
typedef void  (__cdecl *fn_game_player_colour_t)(float*, uint32_t, int);
typedef int   (__cdecl *fn_game_inc_player_colour_ex_t)(uint32_t, int, int);
typedef void  (__cdecl *fn_angle_colour_t)(float*, float, float, float);

static fn_state_current_t            p_state_current = (fn_state_current_t)(uintptr_t)ADDR_STATE_CURRENT;
static fn_state_current_t            p_state_last = (fn_state_current_t)(uintptr_t)ADDR_STATE_LAST;
static fn_state_switch_t             p_state_switch = (fn_state_switch_t)(uintptr_t)ADDR_STATE_SWITCH;
static fn_main_update_with_buttons_t p_main_update_with_buttons = (fn_main_update_with_buttons_t)(uintptr_t)ADDR_MAIN_UPDATE_WITH_BUTTONS;
static fn_game_update_t              p_game_update = (fn_game_update_t)(uintptr_t)ADDR_GAME_UPDATE;
static fn_void_void_t                p_main_draw = (fn_void_void_t)(uintptr_t)ADDR_MAIN_DRAW;
static fn_void_void_t                p_menu_common_render = (fn_void_void_t)(uintptr_t)ADDR_MENU_COMMON_RENDER;
static fn_void_void_t                p_main_buttons_start = (fn_void_void_t)(uintptr_t)ADDR_MAIN_BUTTONS_START;
static fn_void_void_t                p_game_reset = (fn_void_void_t)(uintptr_t)ADDR_GAME_RESET;
static fn_void_void_t                p_game_start = (fn_void_void_t)(uintptr_t)ADDR_GAME_START;
static fn_main_cursors_reset_t       p_main_cursors_reset = (fn_main_cursors_reset_t)(uintptr_t)ADDR_MAIN_CURSORS_RESET;
static fn_main_cursor_spin_t        p_main_cursor_spin = (fn_main_cursor_spin_t)(uintptr_t)ADDR_MAIN_CURSOR_SPIN;
static fn_main_sprite_batches_draw_t p_main_sprite_batches_draw = (fn_main_sprite_batches_draw_t)(uintptr_t)ADDR_MAIN_SPRITE_BATCHES_DRAW;
static fn_sprite_batch_plot_t        p_sprite_batch_plot = (fn_sprite_batch_plot_t)(uintptr_t)ADDR_SPRITE_BATCH_PLOT;
static fn_atlas_upload_t             p_atlas_upload = (fn_atlas_upload_t)(uintptr_t)ADDR_ATLAS_UPLOAD;
static fn_menu_button_link_t         p_menu_button_link = (fn_menu_button_link_t)(uintptr_t)ADDR_MENU_BUTTON_LINK;
static fn_button_ex_t                p_button_ex = (fn_button_ex_t)(uintptr_t)ADDR_BUTTON_EX;
static fn_button_get_t               p_button_get = (fn_button_get_t)(uintptr_t)ADDR_BUTTON_GET;
static fn_button_count_t             p_button_count = (fn_button_count_t)(uintptr_t)ADDR_BUTTON_COUNT;
static fn_button_set_layout_t        p_button_set_layout = (fn_button_set_layout_t)(uintptr_t)ADDR_BUTTON_SET_LAYOUT;
static fn_btn_player_filter_t        p_btn_player_filter = (fn_btn_player_filter_t)(uintptr_t)ADDR_BTN_PLAYER_FILTER;
static fn_plot_text_t                p_plot_text = (fn_plot_text_t)(uintptr_t)ADDR_PLOT_TEXT;
static fn_plot_text_set_shadow_t     p_plot_text_set_shadow = (fn_plot_text_set_shadow_t)(uintptr_t)ADDR_PLOT_TEXT_SET_SHADOW;
static fn_turtle_trans_t             p_turtle_trans = (fn_turtle_trans_t)(uintptr_t)ADDR_TURTLE_TRANS;
static fn_turtle_set_angle_t         p_turtle_set_angle = (fn_turtle_set_angle_t)(uintptr_t)ADDR_TURTLE_SET_ANGLE;
static fn_turtle_set_pos_unscaled_t  p_turtle_set_pos_unscaled = (fn_turtle_set_pos_unscaled_t)(uintptr_t)ADDR_TURTLE_SET_POS_UNSCALED;
static fn_turtle_set_scale_t         p_turtle_set_scale = (fn_turtle_set_scale_t)(uintptr_t)ADDR_TURTLE_SET_SCALE;
static fn_turtle_set_rgb_t           p_turtle_set_rgb = (fn_turtle_set_rgb_t)(uintptr_t)ADDR_TURTLE_SET_RGB;
static fn_turtle_set_rgba_t          p_turtle_set_rgba = (fn_turtle_set_rgba_t)(uintptr_t)ADDR_TURTLE_SET_RGBA;
static fn_turtle_reset_t             p_turtle_reset = (fn_turtle_reset_t)(uintptr_t)ADDR_TURTLE_RESET;
static fn_mad_dim_t                  p_mad_w = (fn_mad_dim_t)(uintptr_t)ADDR_MAD_W;
static fn_mad_dim_t                  p_mad_h = (fn_mad_dim_t)(uintptr_t)ADDR_MAD_H;
static fn_void_void_t                p_options_enter = (fn_void_void_t)(uintptr_t)ADDR_OPTIONS_ENTER;
static fn_void_void_t                p_options_enter_paused = (fn_void_void_t)(uintptr_t)ADDR_OPTIONS_ENTER_PAUSED;
static fn_void_void_t                p_mapgen_init = (fn_void_void_t)(uintptr_t)ADDR_MAPGEN_INIT;
static fn_void_void_t                p_options_enter_trampoline = NULL;
static fn_void_void_t                p_options_enter_paused_trampoline = NULL;
static fn_void_void_t                p_mapgen_init_trampoline = NULL;
static fn_state_switch_t             p_state_switch_trampoline = NULL;
static fn_main_update_with_buttons_t p_main_update_with_buttons_trampoline = NULL;
static fn_game_update_t              p_game_update_trampoline = NULL;
static fn_main_player_poll_cmds_t    p_main_player_poll_cmds = (fn_main_player_poll_cmds_t)(uintptr_t)ADDR_MAIN_PLAYER_POLL_CMDS;
static fn_main_player_poll_cmds_t    p_main_player_poll_cmds_trampoline = NULL;
static fn_tile_action_t              p_high_water_action_trampoline = NULL;
static fn_atlas_upload_t             p_atlas_upload_trampoline = NULL;
static fn_colour_query_t             p_game_water_hi_colour = (fn_colour_query_t)(uintptr_t)ADDR_GAME_WATER_HI_COLOUR;
static fn_colour_query_t             p_game_water_colour = (fn_colour_query_t)(uintptr_t)ADDR_GAME_WATER_COLOUR;
static fn_game_player_colour_index_t p_game_player_colour_index = (fn_game_player_colour_index_t)(uintptr_t)ADDR_GAME_PLAYER_COLOUR_INDEX;
static fn_game_player_colour_index_t p_game_player_colour_index_trampoline = NULL;
static fn_game_set_player_colour_index_t p_game_set_player_colour_index = (fn_game_set_player_colour_index_t)(uintptr_t)ADDR_GAME_SET_PLAYER_COLOUR_INDEX;
static fn_game_set_player_colour_index_t p_game_set_player_colour_index_trampoline = NULL;
static fn_game_player_colour_t       p_game_player_colour = (fn_game_player_colour_t)(uintptr_t)ADDR_GAME_PLAYER_COLOUR;
static fn_game_player_colour_t       p_game_player_colour_trampoline = NULL;
static fn_game_inc_player_colour_ex_t p_game_inc_player_colour_ex = (fn_game_inc_player_colour_ex_t)(uintptr_t)ADDR_GAME_INC_PLAYER_COLOUR_EX;
static fn_game_inc_player_colour_ex_t p_game_inc_player_colour_ex_trampoline = NULL;
static fn_angle_colour_t             p_angle_colour = (fn_angle_colour_t)(uintptr_t)ADDR_ANGLE_COLOUR;
static uintptr_t*                    p_player_slots = (uintptr_t*)(uintptr_t)ADDR_PLAYER_ARRAY;
static volatile int* g_layer = (volatile int*)(uintptr_t)ADDR_LAYER;
static volatile uint32_t* g_mad_ticks = (volatile uint32_t*)(uintptr_t)ADDR_MAD_TICKS;
static volatile uint32_t* g_game_ticks = (volatile uint32_t*)(uintptr_t)ADDR_GAME_TICKS;
static volatile int* g_debug = (volatile int*)(uintptr_t)ADDR_DEBUG;
static volatile int* g_debug_slowmo = (volatile int*)(uintptr_t)ADDR_DEBUG_SLOWMO;
static volatile int* g_game_started = (volatile int*)(uintptr_t)ADDR_GAME_STARTED;
static volatile int* g_native_paused = (volatile int*)(uintptr_t)ADDR_PAUSED;
static volatile float* g_camera_x = (volatile float*)(uintptr_t)ADDR_CAMERA_X;
static volatile float* g_camera_y = (volatile float*)(uintptr_t)ADDR_CAMERA_Y;
static volatile float* g_game_w_native = (volatile float*)(uintptr_t)ADDR_GAME_W;
static volatile float* g_game_h_native = (volatile float*)(uintptr_t)ADDR_GAME_H;
static volatile int* g_player_clr_index = (volatile int*)(uintptr_t)ADDR_PLAYER_CLR_INDEX;
static volatile float* g_player_colours = (volatile float*)(uintptr_t)ADDR_PLAYER_COLOURS;
static volatile unsigned char* g_native_synth_enabled = (volatile unsigned char*)(uintptr_t)ADDR_NATIVE_SYNTH_ENABLED;
static volatile uint32_t* g_native_seed = (volatile uint32_t*)(uintptr_t)ADDR_SEED;
static volatile uint32_t* g_native_mrand_seed = (volatile uint32_t*)(uintptr_t)ADDR_MRAND_SEED;

static fn_rgba_load_t                p_rgba_load = (fn_rgba_load_t)(uintptr_t)ADDR_RGBA_LOAD;
static fn_rgba_load_t                p_rgba_load_trampoline = NULL;
static fn_synth_callback_t           p_respawn_warble_trampoline = NULL;
static fn_synth_callback_t           p_synth_effect_whistling_trampoline = NULL;
static fn_sound_sword_ching_t        p_sound_sword_ching_trampoline = NULL;
static fn_synth_effects_init_t       p_synth_effects_init = (fn_synth_effects_init_t)(uintptr_t)ADDR_SYNTH_EFFECTS_INIT;
static fn_syn_enable_range_t         p_syn_enable_range = (fn_syn_enable_range_t)(uintptr_t)ADDR_SYN_ENABLE_RANGE;

void* g_hooks_rng_mrand_trampoline = NULL;
void* g_hooks_rng_rnd_trampoline = NULL;
void* g_hooks_rng_frnd_trampoline = NULL;
void* g_hooks_rng_rnd5050_trampoline = NULL;
void* g_hooks_rng_rndsign_trampoline = NULL;
void* g_draw_player_body_trampoline = NULL;
void* g_sprite_batch_plot_trampoline = NULL;
void* g_turtle_trans_trampoline = NULL;
static volatile int g_player_body_hidden[2] = {0, 0};
static volatile float g_player_sword_idle_offset[2][2] = {{0.0f, 0.0f}, {0.0f, 0.0f}};

static Detour g_options_enter_detour;
static Detour g_options_enter_paused_detour;
static Detour g_state_switch_detour;
static Detour g_main_update_with_buttons_detour;
static Detour g_game_update_detour;
static Detour g_main_player_poll_cmds_detour;
static Detour g_rgba_load_detour;
static Detour g_mapgen_init_detour;
static Detour g_high_water_action_detour;
static Detour g_atlas_upload_detour;
static Detour g_game_player_colour_index_detour;
static Detour g_game_set_player_colour_index_detour;
static Detour g_game_player_colour_detour;
static Detour g_game_inc_player_colour_ex_detour;
static Detour g_draw_player_body_detour;
static Detour g_sprite_batch_plot_detour;
static Detour g_turtle_trans_detour;
static Detour g_rng_mrand_detour;
static Detour g_rng_rnd_detour;
static Detour g_rng_frnd_detour;
static Detour g_rng_rnd5050_detour;
static Detour g_rng_rndsign_detour;
static Detour g_respawn_warble_detour;
static Detour g_synth_effect_whistling_detour;
static Detour g_sound_sword_ching_detour;

static MenuRow g_rows[MAX_MENU_ROWS];
static int g_row_count = 0;
static int g_selected_row = -1;
static int g_scroll_row = 0;
static int g_mod_collapsed[MAX_MODS_TRACKED];

static int g_capture_active = 0;
static CaptureKind g_capture_kind = CAPTURE_NONE;
static int g_capture_mod = -1;
static int g_capture_cfg = -1;
static char g_capture_buf[CAPTURE_BUF_SIZE];
static int g_console_history_loaded = 0;
static float g_ui_scale = 1.0f;

// Minimal developer console state.
static void* g_console_return_state = (void*)(uintptr_t)ADDR_MAIN_STATE;
static void* g_console_pending_return_state = (void*)(uintptr_t)ADDR_MAIN_STATE;
static int g_console_open_pending = 0;
static int g_console_open_ready = 0;
static int g_console_suppress_next_textinput = 0;
static int g_console_scroll = 0;
static char g_console_input[CONSOLE_INPUT_BUF];
static int g_console_cursor = 0;
static char g_console_edit_stash[CONSOLE_INPUT_BUF];
static int g_console_has_edit_stash = 0;
static int g_console_history_pos = -1;
static char g_console_history[CONSOLE_HISTORY_MAX][CONSOLE_INPUT_BUF];
static int g_console_history_count = 0;
static ConsoleLine g_console_lines[CONSOLE_MAX_LINES];
static int g_console_line_head = 0;
static int g_console_line_count = 0;
static GLuint g_console_bg_tex = 0;
static int g_console_bg_w = 0;
static int g_console_bg_h = 0;
static int g_console_bg_ready = 0;

// Tick-synchronous input scheduling (applied for an entire gameplay update).
static volatile uint32_t g_tick_input_mask[2] = { 0, 0 };
static volatile int g_tick_input_ticks[2] = { 0, 0 };
static volatile int g_tick_input_replace[2] = { 0, 0 };

// Command-bit overrides applied in the main_player_poll_cmds detour.
static volatile uint32_t g_input_override_mask[2] = { 0, 0 };
static volatile int g_input_override_frames[2] = { 0, 0 };
static volatile int g_input_override_replace[2] = { 0, 0 };

static volatile uint32_t g_last_raw_cmd[2] = { 0, 0 };
static volatile uint32_t g_last_effective_cmd[2] = { 0, 0 };
static volatile int g_raw_input_blocked[2] = { 0, 0 };
static volatile int g_block_game_tick_once = 0;
static volatile int g_allow_paused_game_tick = 0;
static volatile int g_ggpo_selftest_pending = 0;
static volatile int g_ggpo_selftest_frames = 0;

static const uint32_t k_ggpo_selftest_masks[] = {
    0u,
    0x08u,
    0x04u,
    0x01u,
    0x02u,
    0x09u,
    0x06u,
    0x0Au
};

typedef struct GgpoSelftestSession {
    int active;
    int frames;
    int pass;
    int frame_index;
    size_t state_size;
    size_t initial_len;
    size_t work_len;
    uint8_t* initial_blob;
    uint8_t* work_blob;
    uint8_t* live_blob;
    uint32_t* frame_checksums;
    uint32_t base_checksum;
    uint32_t replay1_checksum;
    uint32_t replay2_checksum;
} GgpoSelftestSession;

static GgpoSelftestSession g_ggpo_selftest;

// Forward decls for UI layout + state checks used by cursor hijack.
typedef struct ModsLayout {
    float w;
    float h;
    float ui;
    float text_scale;
    float center_x;
    float content_w;
    float left;
    float right;
    float label_x;
    float value_x;
    float list_top;
    float list_bottom;
    float row_h;
} ModsLayout;

static int is_mods_state_active(void);
static int is_console_state_active(void);
static int is_online_result_state_active(void);
static uint32_t hooks_apply_effective_overrides(uint32_t player_index, uint32_t cmd, int consume_poll_override);
static void hooks_finish_game_tick(void);
static void online_sent_challenge_add(const char* username, int id, int expires_in);
static void online_sent_challenge_remove(const char* username, int id);
static void online_sent_challenge_prune(void);
static int online_sent_challenge_pending(const char* username);
static void online_challenge_toast_show(const char* from, int id, int elo, int expires_in);
static void online_challenge_toast_clear(int id, const char* from);
static void online_challenge_toast_render(void);
static void online_challenge_toast_tick(void);
static int online_challenge_toast_action_at(float x, float y);
static int online_challenge_toast_activate(int action);

static int hooks_consume_block_game_tick(void) {
    int block = (g_block_game_tick_once != 0);
    g_block_game_tick_once = 0;
    return block;
}

static void mods_calc_layout(ModsLayout* L);
static void console_draw_rect(float x, float y, float w, float h, float r, float g, float b, float a);
static void console_draw_rect_outline(float x, float y, float w, float h, float line_w, float r, float g, float b, float a);

typedef struct MainCursor {
    float x;
    float y;
    float tx;
    float ty;
    float v;
    float spin;
} MainCursor;

static volatile MainCursor* g_main_cursors = (volatile MainCursor*)(uintptr_t)ADDR_MAIN_CURSOR_DATA;
static float g_cursor_x[2] = { 0.0f, 0.0f };
static float g_cursor_y[2] = { 0.0f, 0.0f };
static float g_cursor_tx[2] = { 0.0f, 0.0f };
static float g_cursor_ty[2] = { 0.0f, 0.0f };
static int g_cursor_initialized = 0;
static int g_cursor_bump = 0;

static void write_main_cursor_pos(int idx, float x, float y) {
    if (idx < 0 || idx >= 2) return;
    if (!g_main_cursors) return;

    g_main_cursors[idx].x = x;
    g_main_cursors[idx].y = y;
    g_main_cursors[idx].tx = x;
    g_main_cursors[idx].ty = y;
}

static void mods_cursor_on_selection_changed(void) {
    g_cursor_bump = 7;
}

static void mods_cursor_tick(void) {
    if (!is_mods_state_active()) return;
    if (g_selected_row < 0 || g_selected_row >= g_row_count) return;

    ModsLayout L;
    mods_calc_layout(&L);
    g_ui_scale = L.text_scale;

    // Determine target position from selected row.
    float row_y = L.list_top + ((float)(g_selected_row - g_scroll_row) * L.row_h)
                + (L.row_h * MODS_CURSOR_ROW_Y_FACTOR)
                + (MODS_CURSOR_ROW_Y_NUDGE * L.ui);

    // Place swords just outside the content region so they don't clip into text.
    float left_x  = L.left  - (MODS_CURSOR_OUTER_PAD_X * L.ui);
    float right_x = L.right + (MODS_CURSOR_OUTER_PAD_X * L.ui);
    // Keep within window bounds.
    {
        float w = L.w;
        float margin = 18.0f * L.ui;
        if (left_x < margin) left_x = margin;
        if (right_x > w - margin) right_x = w - margin;
    }

    if (g_rows[g_selected_row].kind == ROW_BACK) {
        left_x  = L.center_x - (84.0f * L.ui);
        right_x = L.center_x + (84.0f * L.ui);
    }

    g_cursor_tx[0] = left_x;
    g_cursor_ty[0] = row_y;
    g_cursor_tx[1] = right_x;
    g_cursor_ty[1] = row_y;

    if (!g_cursor_initialized) {
        g_cursor_x[0] = g_cursor_tx[0];
        g_cursor_y[0] = g_cursor_ty[0];
        g_cursor_x[1] = g_cursor_tx[1];
        g_cursor_y[1] = g_cursor_ty[1];
        g_cursor_initialized = 1;
    } else {
        // Smooth follow.
        float k = 0.35f;
        g_cursor_x[0] += (g_cursor_tx[0] - g_cursor_x[0]) * k;
        g_cursor_y[0] += (g_cursor_ty[0] - g_cursor_y[0]) * k;
        g_cursor_x[1] += (g_cursor_tx[1] - g_cursor_x[1]) * k;
        g_cursor_y[1] += (g_cursor_ty[1] - g_cursor_y[1]) * k;
    }
    // Keep engine cursor state initialized, then override exact sword positions
    // for this non-button list UI.
    if (g_cursor_bump > 0) g_cursor_bump--;

    if (p_main_cursors_reset) {
        float cx = (g_cursor_x[0] + g_cursor_x[1]) * 0.5f;
        float cy = (g_cursor_y[0] + g_cursor_y[1]) * 0.5f;
        p_main_cursors_reset(cx, cy);
    }

    write_main_cursor_pos(0, g_cursor_x[0], g_cursor_y[0]);
    write_main_cursor_pos(1, g_cursor_x[1], g_cursor_y[1]);
}

static void* g_mods_return_state = (void*)(uintptr_t)ADDR_OPTIONS_STATE;
static void* g_online_return_state = (void*)(uintptr_t)ADDR_MAIN_STATE;
static HookCustomState g_custom_states[MAX_CUSTOM_STATES];

static void __cdecl console_enter(void);
static void __cdecl console_update(void);
static void __cdecl console_render(void);
static void __cdecl console_leave(void);

static void __cdecl mods_enter(void);
static void __cdecl mods_update(void);
static void __cdecl mods_render(void);
static void __cdecl mods_leave(void);
static void __cdecl online_hub_enter(void);
static void __cdecl online_hub_update(void);
static void __cdecl online_hub_render(void);
static void __cdecl online_hub_leave(void);
static void __cdecl online_result_enter(void);
static void __cdecl online_result_update(void);
static void __cdecl online_result_render(void);
static void __cdecl online_result_leave(void);
static void online_result_render_overlay(const OnlineLayout* L);
static void __cdecl custom_state_enter(void);
static void __cdecl custom_state_update(void);
static void __cdecl custom_state_render(void);
static void __cdecl custom_state_leave(void);
static HookCustomState* find_custom_state_by_name(const char* name);
static HookCustomState* find_custom_state_by_ptr(void* state_ptr);
static HookCustomState* find_active_custom_state(void);
static int is_custom_state_ptr(void* state_ptr);
static void __cdecl mods_entry_enter(void);
static void __cdecl mods_entry_update(void);
static void __cdecl mods_entry_render(void);
static void __cdecl mods_entry_leave(void);

static void console_push_line_rgb(const char* text, float r, float g, float b);
static void console_set_input(const char* s);
static void console_insert_text(const char* text);
static int  console_find_mod_index_by_id(const char* id);
static int  console_find_cfg_index_by_key(int mod_index, const char* key);
static void console_compl_reset(void);
static const char* console_stristr(const char* haystack, const char* needle);
static char* console_parse_token(char** inout_cursor);
static int console_try_parse_long(const char* s, long* out_value);
static int console_try_parse_double(const char* s, double* out_value);
static int console_try_parse_bool(const char* s, int* out_value);
static void console_strip_crlf(char* s);
static void console_run_ggpo_selftest(const char* arg);
static void console_run_ggpo_roundtrip(const char* arg);
static void queue_ggpo_selftest(int frames, const char* source);
static void toggle_ggpo_loopback(const char* source);
static void console_run_ggpo_local(const char* arg);
static void toggle_ggpo_local(const char* source);
static void start_ggpo_net_host(uint16_t port, const char* source);
static void start_ggpo_net_join(const char* host, uint16_t remote_port, uint16_t local_port, const char* source);
static void start_ggpo_net_join_deferred(uint16_t local_port, const char* source);
static void stop_ggpo_net(const char* source);
static void online_hub_open(void);
static void online_hub_set_status(const char* msg);
static void online_hub_load(void);
static void online_hub_save(void);
static void online_hub_apply_net_settings(void);
static void online_hub_rebuild_rows(void);
static void online_hub_render_ui(void);
static void online_server_update(void);
static void online_server_disconnect(const char* reason);
static void online_match_pump_launch(void);
static void online_match_poll_completion(void);
static void console_draw_background(float w, float h);

static GameState g_mods_state = {
    mods_enter,
    mods_update,
    mods_render,
    mods_leave,
};

static GameState g_console_state = {
    console_enter,
    console_update,
    console_render,
    console_leave,
};

static GameState g_online_hub_state = {
    online_hub_enter,
    online_hub_update,
    online_hub_render,
    online_hub_leave,
};

static GameState g_online_result_state = {
    online_result_enter,
    online_result_update,
    online_result_render,
    online_result_leave,
};

static GameState g_mods_entry_state = {
    mods_entry_enter,
    mods_entry_update,
    mods_entry_render,
    mods_entry_leave,
};

typedef enum OnlineServerState {
    ONLINE_SERVER_DISCONNECTED = 0,
    ONLINE_SERVER_CONNECTING,
    ONLINE_SERVER_CONNECTED,
} OnlineServerState;

typedef struct OnlinePendingMatch {
    int active;
    int launch_stage;
    int launch_delay_frames;
    int launch_countdown_frames;
    int p2p_started;
    int match_id;
    int role;
    int selector;
    int local_port;
    int peer_port;
    int input_delay;
    unsigned int seed;
    int competitive;
    int queue_mode;
    char p2p_role[12];
    char peer_host[ONLINE_HUB_TEXT_MAX];
    char p2p_token[96];
    char opponent[48];
    char map_key[128];
    char map_label[128];
} OnlinePendingMatch;

typedef struct OnlineActiveMatch {
    int active;
    int match_id;
    int local_player;
    int competitive;
    int queue_mode;
    int result_reported;
    int invalid_state_ticks;
    int p2p_probe_cooldown;
    int p2p_probe_logged;
    int p2p_probe_warned;
    OnlineMatchResult result;
    char opponent[48];
    char map_label[128];
    char p2p_token[96];
} OnlineActiveMatch;

typedef struct OnlineResultScreen {
    int active;
    int selected;
    int reported;
    int server_confirmed;
    int toast_age;
    int toast_lifetime;
    OnlineMatchResult result;
    int competitive;
    int queue_mode;
    int elo_before;
    int elo_after;
    int elo_delta_valid;
    char opponent[48];
    char map_label[128];
    char status[ONLINE_HUB_STATUS_MAX];
} OnlineResultScreen;

static OnlineHubConfig g_online_cfg;
static OnlineFriend g_online_friends[ONLINE_HUB_MAX_FRIENDS];
static int g_online_friend_count = 0;
static OnlineFriendRequest g_online_requests[ONLINE_HUB_MAX_INBOX];
static int g_online_request_count = 0;
static OnlineChallenge g_online_challenges[ONLINE_HUB_MAX_INBOX];
static int g_online_challenge_count = 0;
static OnlineSentChallenge g_online_sent_challenges[ONLINE_HUB_MAX_INBOX];
static int g_online_sent_challenge_count = 0;
static int g_online_loaded = 0;
static OnlineHubTab g_online_tab = ONLINE_TAB_PLAY;
static OnlineHubRow g_online_rows[ONLINE_HUB_MAX_ROWS];
static int g_online_row_count = 0;
static int g_online_selected_row = -1;
static int g_online_scroll_row = 0;
static char g_online_status[ONLINE_HUB_STATUS_MAX];
static int g_online_capture_active = 0;
static OnlineHubCaptureKind g_online_capture_kind = ONLINE_CAPTURE_NONE;
static int g_online_capture_target = 0;
static char g_online_capture_buf[ONLINE_HUB_TEXT_MAX];
static OnlineServerState g_online_server_state = ONLINE_SERVER_DISCONNECTED;
static int g_online_server_slot = -1;
static int g_online_authed = 0;
static int g_online_register_after_connect = 0;
static char g_online_recv_buf[65536];
static size_t g_online_recv_len = 0;
static int g_online_queue_mode = 0; /* 0 none, 1 casual, 2 competitive */
static int g_online_public_elo = 1000;
static int g_online_queue_casual_count = 0;
static int g_online_queue_competitive_count = 0;
static float g_online_mouse_x = BASE_UI_W * 0.5f;
static float g_online_mouse_y = BASE_UI_H * 0.5f;
static int g_online_context_active = 0;
static int g_online_context_friend = -1;
static float g_online_context_x = 0.0f;
static float g_online_context_y = 0.0f;
static int g_online_open_pending = 0;
static void* g_online_pending_return_state = (void*)(uintptr_t)ADDR_MAIN_STATE;
static OnlinePendingMatch g_online_pending_match;
static OnlineActiveMatch g_online_active_match;
static OnlineResultScreen g_online_result;
static OnlineChallengeToast g_online_challenge_toast;
static volatile int* g_hook_map_selector = (volatile int*)(uintptr_t)ADDR_MAP_SELECTOR;
static volatile uintptr_t* g_game_leader = (volatile uintptr_t*)(uintptr_t)ADDR_LEADER;
static volatile uintptr_t* g_game_loser = (volatile uintptr_t*)(uintptr_t)ADDR_LOSER;
static volatile int* g_game_end_countdown = (volatile int*)(uintptr_t)ADDR_END_COUNTDOWN;
static volatile int* g_game_start_countdown = (volatile int*)(uintptr_t)ADDR_START_COUNTDOWN;
static volatile int* g_game_active_room = (volatile int*)(uintptr_t)ADDR_GAME_ACTIVE_ROOM;
static volatile uintptr_t* g_game_waterfall_fx = (volatile uintptr_t*)(uintptr_t)ADDR_WATERFALL_FX;
static volatile int* g_game_waterfall_count = (volatile int*)(uintptr_t)ADDR_WATERFALL_COUNT;
static volatile int* g_game_old_active_room = (volatile int*)(uintptr_t)ADDR_GAME_OLD_ACTIVE_ROOM;
static volatile int* g_game_resumed = (volatile int*)(uintptr_t)ADDR_RESUMED;
static volatile int* g_game_round_end_any = (volatile int*)(uintptr_t)ADDR_ROUND_END_ANY;
static volatile int* g_game_score_target = (volatile int*)(uintptr_t)ADDR_SCORE_TARGET;
static volatile int* g_game_score_player0 = (volatile int*)(uintptr_t)ADDR_SCORE_PLAYER0;
static volatile int* g_game_score_player1 = (volatile int*)(uintptr_t)ADDR_SCORE_PLAYER1;

static int clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void online_clear_waterfall_audio_state(const char* reason) {
    uintptr_t fx = 0u;
    int count = 0;
    int changed = 0;

    if (g_game_waterfall_fx && !IsBadReadPtr((const void*)g_game_waterfall_fx, sizeof(uintptr_t))) {
        fx = *g_game_waterfall_fx;
    }
    if (g_game_waterfall_count && !IsBadReadPtr((const void*)g_game_waterfall_count, sizeof(int))) {
        count = *g_game_waterfall_count;
    }
    if (fx && fx < UINTPTR_MAX - 0x2cu &&
        !IsBadWritePtr((void*)(fx + 0x2cu), (SIZE_T)sizeof(uint32_t))) {
        *(uint32_t*)(fx + 0x2cu) = 0u;
        changed = 1;
    }
    if (g_game_waterfall_fx && !IsBadWritePtr((void*)g_game_waterfall_fx, (SIZE_T)sizeof(uintptr_t))) {
        *g_game_waterfall_fx = 0u;
        if (fx) changed = 1;
    }
    if (g_game_waterfall_count && !IsBadWritePtr((void*)g_game_waterfall_count, (SIZE_T)sizeof(int))) {
        *g_game_waterfall_count = 0;
        if (count) changed = 1;
    }
    if (changed) {
        LOG_DEBUG("online.match: cleared waterfall audio state reason=%s fx=0x%08X count=%d",
                  (reason && reason[0]) ? reason : "unknown",
                  (unsigned int)fx,
                  count);
    }
}

/*
 * Protect the waterfall ambience across a rollback. The waterfall is a persistent
 * native sound whose handle (_fx_15991), per-room count, and the slot's "active"
 * flag (+0x2c) live in process-local audio memory that is NOT real gameplay state.
 * A rollback re-runs game_update for the replayed frames (with the synth muted),
 * and that replay mutates these handles - leaving them pointing at a stale/cleared
 * slot. The next LIVE frame then drives the waterfall from those corrupted handles,
 * which is heard as waterfall noise in rooms that have no waterfall. We snapshot the
 * live handles before the rollback and put them back afterwards, so the rollback is
 * transparent to the ambience and the live frame keeps managing the real sound.
 */
static uintptr_t g_wf_saved_fx = 0u;
static int       g_wf_saved_count = 0;
static uint32_t  g_wf_saved_active = 0u;
static int       g_wf_saved_has_active = 0;

void hooks_waterfall_audio_save(void) {
    g_wf_saved_fx = 0u;
    g_wf_saved_count = 0;
    g_wf_saved_active = 0u;
    g_wf_saved_has_active = 0;
    if (g_game_waterfall_fx && !IsBadReadPtr((const void*)g_game_waterfall_fx, sizeof(uintptr_t))) {
        g_wf_saved_fx = *g_game_waterfall_fx;
    }
    if (g_game_waterfall_count && !IsBadReadPtr((const void*)g_game_waterfall_count, sizeof(int))) {
        g_wf_saved_count = *g_game_waterfall_count;
    }
    if (g_wf_saved_fx && g_wf_saved_fx < UINTPTR_MAX - 0x2cu &&
        !IsBadReadPtr((const void*)(g_wf_saved_fx + 0x2cu), sizeof(uint32_t))) {
        g_wf_saved_active = *(uint32_t*)(g_wf_saved_fx + 0x2cu);
        g_wf_saved_has_active = 1;
    }
}

void hooks_waterfall_audio_restore(void) {
    if (g_game_waterfall_fx && !IsBadWritePtr((void*)g_game_waterfall_fx, (SIZE_T)sizeof(uintptr_t))) {
        *g_game_waterfall_fx = g_wf_saved_fx;
    }
    if (g_game_waterfall_count && !IsBadWritePtr((void*)g_game_waterfall_count, (SIZE_T)sizeof(int))) {
        *g_game_waterfall_count = g_wf_saved_count;
    }
    if (g_wf_saved_has_active && g_wf_saved_fx && g_wf_saved_fx < UINTPTR_MAX - 0x2cu &&
        !IsBadWritePtr((void*)(g_wf_saved_fx + 0x2cu), (SIZE_T)sizeof(uint32_t))) {
        *(uint32_t*)(g_wf_saved_fx + 0x2cu) = g_wf_saved_active;
    }
}

static void online_reset_native_sound_state(const char* reason) {
    online_clear_waterfall_audio_state(reason);
    if (p_synth_effects_init && !IsBadCodePtr((FARPROC)(void*)p_synth_effects_init)) {
        p_synth_effects_init(-1, -1);
    }
    if (p_syn_enable_range && !IsBadCodePtr((FARPROC)(void*)p_syn_enable_range)) {
        p_syn_enable_range((int)ADDR_SYNTH_ENGINE, 0u, 0x100u, 0);
    }
    LOG_DEBUG("online.match: reset native synth sounds reason=%s",
              (reason && reason[0]) ? reason : "unknown");
}

#define VANILLA_PLAYER_COLOUR_COUNT 14

typedef enum HooksPlayerColourKind {
    HOOKS_PLAYER_COLOUR_SOLID = 0,
    HOOKS_PLAYER_COLOUR_HUE,
    HOOKS_PLAYER_COLOUR_LERP,
} HooksPlayerColourKind;

typedef struct HooksPlayerColourDef {
    HooksPlayerColourKind kind;
    float r;
    float g;
    float b;
    float a;
    float r2;
    float g2;
    float b2;
    float hue;
    float hue_step;
    uint32_t period;
} HooksPlayerColourDef;

static const HooksPlayerColourDef k_extra_player_colours[] = {
    { HOOKS_PLAYER_COLOUR_SOLID, 0.98f, 0.74f, 0.55f, 1.0f, 0, 0, 0, 0, 0, 0 },       // warm skin
    { HOOKS_PLAYER_COLOUR_SOLID, 0.58f, 0.34f, 0.18f, 1.0f, 0, 0, 0, 0, 0, 0 },       // bronze
    { HOOKS_PLAYER_COLOUR_SOLID, 0.30f, 0.16f, 0.09f, 1.0f, 0, 0, 0, 0, 0, 0 },       // umber
    { HOOKS_PLAYER_COLOUR_SOLID, 0.92f, 0.38f, 0.56f, 1.0f, 0, 0, 0, 0, 0, 0 },       // rose
    { HOOKS_PLAYER_COLOUR_SOLID, 1.00f, 0.33f, 0.23f, 1.0f, 0, 0, 0, 0, 0, 0 },       // coral
    { HOOKS_PLAYER_COLOUR_SOLID, 1.00f, 0.62f, 0.12f, 1.0f, 0, 0, 0, 0, 0, 0 },       // amber
    { HOOKS_PLAYER_COLOUR_SOLID, 1.00f, 0.84f, 0.18f, 1.0f, 0, 0, 0, 0, 0, 0 },       // gold
    { HOOKS_PLAYER_COLOUR_SOLID, 0.54f, 0.92f, 0.18f, 1.0f, 0, 0, 0, 0, 0, 0 },       // lime
    { HOOKS_PLAYER_COLOUR_SOLID, 0.20f, 0.95f, 0.62f, 1.0f, 0, 0, 0, 0, 0, 0 },       // mint
    { HOOKS_PLAYER_COLOUR_SOLID, 0.08f, 0.74f, 0.76f, 1.0f, 0, 0, 0, 0, 0, 0 },       // teal
    { HOOKS_PLAYER_COLOUR_SOLID, 0.10f, 0.83f, 1.00f, 1.0f, 0, 0, 0, 0, 0, 0 },       // cyan
    { HOOKS_PLAYER_COLOUR_SOLID, 0.35f, 0.60f, 1.00f, 1.0f, 0, 0, 0, 0, 0, 0 },       // sky
    { HOOKS_PLAYER_COLOUR_SOLID, 0.12f, 0.22f, 0.95f, 1.0f, 0, 0, 0, 0, 0, 0 },       // cobalt
    { HOOKS_PLAYER_COLOUR_SOLID, 0.55f, 0.25f, 1.00f, 1.0f, 0, 0, 0, 0, 0, 0 },       // violet
    { HOOKS_PLAYER_COLOUR_SOLID, 0.95f, 0.20f, 0.90f, 1.0f, 0, 0, 0, 0, 0, 0 },       // magenta
    { HOOKS_PLAYER_COLOUR_SOLID, 0.92f, 0.92f, 0.88f, 1.0f, 0, 0, 0, 0, 0, 0 },       // pearl
    { HOOKS_PLAYER_COLOUR_SOLID, 0.12f, 0.13f, 0.15f, 1.0f, 0, 0, 0, 0, 0, 0 },       // charcoal
    { HOOKS_PLAYER_COLOUR_SOLID, 0.35f, 0.42f, 0.50f, 1.0f, 0, 0, 0, 0, 0, 0 },       // slate
    { HOOKS_PLAYER_COLOUR_SOLID, 0.72f, 0.03f, 0.10f, 1.0f, 0, 0, 0, 0, 0, 0 },       // crimson
    { HOOKS_PLAYER_COLOUR_SOLID, 0.04f, 0.45f, 0.16f, 1.0f, 0, 0, 0, 0, 0, 0 },       // forest
    { HOOKS_PLAYER_COLOUR_SOLID, 0.03f, 0.10f, 0.35f, 1.0f, 0, 0, 0, 0, 0, 0 },       // navy
    { HOOKS_PLAYER_COLOUR_SOLID, 0.65f, 0.95f, 1.00f, 1.0f, 0, 0, 0, 0, 0, 0 },       // ice
    { HOOKS_PLAYER_COLOUR_SOLID, 0.75f, 0.55f, 1.00f, 1.0f, 0, 0, 0, 0, 0, 0 },       // lavender
    { HOOKS_PLAYER_COLOUR_SOLID, 0.95f, 0.48f, 0.14f, 1.0f, 0, 0, 0, 0, 0, 0 },       // tangerine
    { HOOKS_PLAYER_COLOUR_HUE,   0, 0, 0, 1.0f, 0, 0, 0,   0.0f, 6.0f, 0 },          // RGB shift fast
    { HOOKS_PLAYER_COLOUR_HUE,   0, 0, 0, 1.0f, 0, 0, 0,  45.0f, 2.0f, 0 },          // RGB shift slow
    { HOOKS_PLAYER_COLOUR_LERP,  1.00f, 0.10f, 0.72f, 1.0f, 0.20f, 0.92f, 1.00f, 0, 0, 90 },  // neon pulse
    { HOOKS_PLAYER_COLOUR_LERP,  1.00f, 0.15f, 0.05f, 1.0f, 1.00f, 0.84f, 0.12f, 0, 0, 72 },  // fire shift
    { HOOKS_PLAYER_COLOUR_LERP,  0.12f, 0.78f, 1.00f, 1.0f, 0.84f, 0.96f, 1.00f, 0, 0, 96 },  // ice shift
    { HOOKS_PLAYER_COLOUR_LERP,  0.10f, 0.05f, 0.22f, 1.0f, 0.35f, 1.00f, 0.78f, 0, 0, 110 }, // void shimmer
    { HOOKS_PLAYER_COLOUR_LERP,  0.55f, 0.56f, 0.60f, 1.0f, 1.00f, 0.98f, 0.82f, 0, 0, 64 },  // metallic shimmer
};

static int hooks_player_colour_count(void) {
    return VANILLA_PLAYER_COLOUR_COUNT +
           (int)(sizeof(k_extra_player_colours) / sizeof(k_extra_player_colours[0]));
}

static int hooks_player_colour_slot(uint32_t player_index, int clothing) {
    int slot = (int)(player_index & 1u);
    if (clothing) slot += 2;
    return slot;
}

static int hooks_wrap_player_colour_index(int index) {
    int count = hooks_player_colour_count();
    if (count <= 0) return 0;
    index %= count;
    if (index < 0) index += count;
    return index;
}

static uint32_t hooks_player_colour_tick(void) {
    if (g_game_started && *g_game_started && g_game_ticks) return *g_game_ticks;
    if (g_mad_ticks) return *g_mad_ticks;
    if (g_game_ticks) return *g_game_ticks;
    return 0u;
}

static float hooks_wrap_degrees(float v) {
    while (v >= 360.0f) v -= 360.0f;
    while (v < 0.0f) v += 360.0f;
    return v;
}

static float hooks_triangle01(uint32_t tick, uint32_t period) {
    uint32_t t;
    float f;
    if (period < 2u) period = 2u;
    t = tick % period;
    f = (float)t / (float)period;
    return (f < 0.5f) ? (f * 2.0f) : ((1.0f - f) * 2.0f);
}

static void hooks_hue_to_rgb_fallback(float hue, float sat, float val, float out[4]) {
    float c;
    float x;
    float m;
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;

    hue = hooks_wrap_degrees(hue);
    sat = clampf(sat, 0.0f, 1.0f);
    val = clampf(val, 0.0f, 1.0f);
    c = val * sat;
    {
        float h = hue / 60.0f;
        int sector = (int)h;
        float frac = h - (float)sector;
        x = c * ((sector & 1) ? (1.0f - frac) : frac);
        switch (sector) {
            case 0: r = c; g = x; b = 0.0f; break;
            case 1: r = x; g = c; b = 0.0f; break;
            case 2: r = 0.0f; g = c; b = x; break;
            case 3: r = 0.0f; g = x; b = c; break;
            case 4: r = x; g = 0.0f; b = c; break;
            default: r = c; g = 0.0f; b = x; break;
        }
    }
    m = val - c;
    out[0] = r + m;
    out[1] = g + m;
    out[2] = b + m;
    out[3] = 1.0f;
}

static void hooks_native_player_colour(int index, float out[4]) {
    int i = hooks_wrap_player_colour_index(index);
    if (i >= VANILLA_PLAYER_COLOUR_COUNT) i %= VANILLA_PLAYER_COLOUR_COUNT;
    if (g_player_colours) {
        out[0] = g_player_colours[i * 4 + 0];
        out[1] = g_player_colours[i * 4 + 1];
        out[2] = g_player_colours[i * 4 + 2];
        out[3] = g_player_colours[i * 4 + 3];
    } else {
        out[0] = out[1] = out[2] = out[3] = 1.0f;
    }
}

static void hooks_resolve_player_colour(int index, float out[4]) {
    int wrapped = hooks_wrap_player_colour_index(index);
    const HooksPlayerColourDef* def;
    uint32_t tick;
    float t;

    if (wrapped < VANILLA_PLAYER_COLOUR_COUNT) {
        hooks_native_player_colour(wrapped, out);
        return;
    }

    def = &k_extra_player_colours[wrapped - VANILLA_PLAYER_COLOUR_COUNT];
    tick = hooks_player_colour_tick();
    switch (def->kind) {
        case HOOKS_PLAYER_COLOUR_HUE: {
            float hue = def->hue + (float)tick * def->hue_step;
            if (p_angle_colour) {
                p_angle_colour(out, hooks_wrap_degrees(hue), 1.0f, 1.0f);
                out[3] = def->a;
            } else {
                hooks_hue_to_rgb_fallback(hue, 1.0f, 1.0f, out);
                out[3] = def->a;
            }
            break;
        }
        case HOOKS_PLAYER_COLOUR_LERP:
            t = hooks_triangle01(tick, def->period);
            out[0] = def->r + (def->r2 - def->r) * t;
            out[1] = def->g + (def->g2 - def->g) * t;
            out[2] = def->b + (def->b2 - def->b) * t;
            out[3] = def->a;
            break;
        case HOOKS_PLAYER_COLOUR_SOLID:
        default:
            out[0] = def->r;
            out[1] = def->g;
            out[2] = def->b;
            out[3] = def->a;
            break;
    }
}

static int __cdecl hooked_game_player_colour_index(uint32_t player_index, int clothing) {
    int slot = hooks_player_colour_slot(player_index, clothing);
    return g_player_clr_index ? g_player_clr_index[slot] : 0;
}

static int __cdecl hooked_game_set_player_colour_index(uint32_t player_index, int clothing, uint32_t colour_index) {
    int slot = hooks_player_colour_slot(player_index, clothing);
    int wrapped = hooks_wrap_player_colour_index((int32_t)colour_index);
    if (g_player_clr_index) g_player_clr_index[slot] = wrapped;
    return wrapped;
}

int hooks_player_colour_index(int player_index, int clothing) {
    int slot = hooks_player_colour_slot((uint32_t)player_index, clothing);
    return g_player_clr_index ? g_player_clr_index[slot] : 0;
}

int hooks_set_player_colour_index(int player_index, int clothing, int colour_index) {
    int slot = hooks_player_colour_slot((uint32_t)player_index, clothing);
    int wrapped = hooks_wrap_player_colour_index(colour_index);
    if (g_player_clr_index) g_player_clr_index[slot] = wrapped;
    return wrapped;
}

void hooks_set_player_body_hidden(int player_index, int hidden) {
    g_player_body_hidden[player_index & 1] = hidden ? 1 : 0;
}

int hooks_player_body_hidden(int player_index) {
    return g_player_body_hidden[player_index & 1] ? 1 : 0;
}

void hooks_set_player_sword_idle_offset(int player_index, float x, float y) {
    int slot = player_index & 1;
    if (x < -16.0f) x = -16.0f;
    if (x > 16.0f) x = 16.0f;
    if (y < -16.0f) y = -16.0f;
    if (y > 16.0f) y = 16.0f;
    g_player_sword_idle_offset[slot][0] = x;
    g_player_sword_idle_offset[slot][1] = y;
}

static int hooks_player_index_for_ptr(uintptr_t player_ptr) {
    if (!player_ptr || !p_player_slots) return -1;
    if ((uintptr_t)p_player_slots[0] == player_ptr) return 0;
    if ((uintptr_t)p_player_slots[1] == player_ptr) return 1;
    return -1;
}

int __cdecl hooks_should_skip_draw_player_body(uintptr_t player_ptr) {
    int slot = hooks_player_index_for_ptr(player_ptr);
    return slot >= 0 ? hooks_player_body_hidden(slot) : 0;
}

static int hooks_is_player_body_sprite_call(uintptr_t return_addr) {
    switch ((uint32_t)return_addr) {
        case 0x41C1FFu: /* draw_player_swordfight arm */
        case 0x41CA71u: /* draw_things thrown/grabbed arm */
        case 0x41CCF4u: /* draw_things pose arm skin */
        case 0x41CD5Bu: /* draw_things pose arm clothing */
        case 0x41CE2Eu: /* draw_things secondary arm */
        case 0x41CF1Bu: /* draw_things prone/duck arm */
        case 0x41D053u: /* draw_things flipped secondary arm */
            return 1;
        default:
            return 0;
    }
}

int __cdecl hooks_should_skip_sprite_batch_plot(uintptr_t return_addr, uintptr_t player_ptr, int sprite) {
    (void)sprite;
    if (!hooks_is_player_body_sprite_call(return_addr)) return 0;
    return hooks_should_skip_draw_player_body(player_ptr);
}

static int hooks_is_player_sword_idle_trans_call(uintptr_t return_addr) {
    return return_addr == 0x41C02Eu || return_addr == 0x41CF9Fu;
}

int __cdecl hooks_prepare_turtle_trans(uintptr_t return_addr, uintptr_t player_ptr, double* x, double* y) {
    int slot;
    if (!x || !y || !hooks_is_player_sword_idle_trans_call(return_addr)) return 0;
    slot = hooks_player_index_for_ptr(player_ptr);
    if (slot < 0 || !hooks_player_body_hidden(slot)) return 0;
    *x = (double)g_player_sword_idle_offset[slot][0];
    *y = (double)g_player_sword_idle_offset[slot][1];
    return 1;
}

#ifdef HOOKS_INTELLISENSE
static void hooked_draw_player_body(void) { }
static void hooked_sprite_batch_plot(void) { }
static void hooked_turtle_trans(void) { }
#else
static void __attribute__((naked)) hooked_draw_player_body(void) {
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "pushl %eax\n\t"
        "call _hooks_should_skip_draw_player_body\n\t"
        "addl $4, %esp\n\t"
        "testl %eax, %eax\n\t"
        "jnz 1f\n\t"
        "popal\n\t"
        "popfl\n\t"
        "jmp *_g_draw_player_body_trampoline\n\t"
        "1:\n\t"
        "popal\n\t"
        "popfl\n\t"
        "ret\n\t"
    );
}

static void __attribute__((naked)) hooked_sprite_batch_plot(void) {
    __asm__ __volatile__(
        "pushfl\n\t"
        "pushal\n\t"
        "movl 40(%esp), %eax\n\t"
        "pushl %eax\n\t"
        "pushl %ebx\n\t"
        "movl 44(%esp), %eax\n\t"
        "pushl %eax\n\t"
        "call _hooks_should_skip_sprite_batch_plot\n\t"
        "addl $12, %esp\n\t"
        "testl %eax, %eax\n\t"
        "jnz 1f\n\t"
        "popal\n\t"
        "popfl\n\t"
        "jmp *_g_sprite_batch_plot_trampoline\n\t"
        "1:\n\t"
        "popal\n\t"
        "popfl\n\t"
        "ret\n\t"
    );
}

static void __attribute__((naked)) hooked_turtle_trans(void) {
    __asm__ __volatile__(
        "pushal\n\t"
        "movl %esp, %edx\n\t"
        "leal 44(%edx), %eax\n\t"
        "pushl %eax\n\t"
        "leal 36(%edx), %eax\n\t"
        "pushl %eax\n\t"
        "pushl %ebx\n\t"
        "pushl 32(%edx)\n\t"
        "call _hooks_prepare_turtle_trans\n\t"
        "addl $16, %esp\n\t"
        "popal\n\t"
        "jmp *_g_turtle_trans_trampoline\n\t"
    );
}
#endif

static void __cdecl hooked_game_player_colour(float* out, uint32_t player_index, int clothing) {
    int player = (int)(player_index & 1u);
    int skin_index = g_player_clr_index ? g_player_clr_index[player] : 0;
    int clothing_index = g_player_clr_index ? g_player_clr_index[player + 2] : 0;
    int selected_index = clothing ? clothing_index : skin_index;
    float colour[4];

    if (!out) return;
    if (p_game_player_colour_trampoline &&
        skin_index >= 0 && skin_index < VANILLA_PLAYER_COLOUR_COUNT &&
        clothing_index >= 0 && clothing_index < VANILLA_PLAYER_COLOUR_COUNT) {
        p_game_player_colour_trampoline(out, player_index, clothing);
        return;
    }

    hooks_resolve_player_colour(selected_index, colour);
    if (clothing && skin_index == clothing_index && skin_index != 0x0d) {
        colour[0] *= 0.75f;
        colour[1] *= 0.75f;
        colour[2] *= 0.75f;
    }

    out[0] = colour[0];
    out[1] = colour[1];
    out[2] = colour[2];
    out[3] = colour[3];
}

static int hooks_player_colour_combo_matches(int player) {
    int other = (player + 1) & 1;
    if (!g_player_clr_index) return 0;
    return g_player_clr_index[player] == g_player_clr_index[other] &&
           g_player_clr_index[player + 2] == g_player_clr_index[other + 2];
}

static int __cdecl hooked_game_inc_player_colour_ex(uint32_t player_index, int clothing, int delta) {
    int player = (int)(player_index & 1u);
    int slot = hooks_player_colour_slot(player_index, clothing);
    int count = hooks_player_colour_count();
    int step = (delta < 0) ? -1 : 1;
    int index;

    if (!g_player_clr_index || count <= 0) return 0;
    index = g_player_clr_index[slot];
    for (int attempt = 0; attempt < count; attempt++) {
        index = hooks_wrap_player_colour_index(index + step);
        g_player_clr_index[slot] = index;
        if (!hooks_player_colour_combo_matches(player)) break;
    }
    return index;
}

static float calc_ui_scale(void) {
    float w = p_mad_w ? p_mad_w() : BASE_UI_W;
    float h = p_mad_h ? p_mad_h() : BASE_UI_H;
    float sx = w / BASE_UI_W;
    float sy = h / BASE_UI_H;
    float s = (sx < sy) ? sx : sy;
    // Our custom mods menu should remain readable across a much wider range
    // of window sizes than the base game UI.
    return clampf(s, 0.75f, 1.60f);
}

static void safe_copy(char* dst, size_t dst_sz, const char* src) {
    if (!dst || dst_sz == 0) return;
    if (!src) src = "";
    strncpy(dst, src, dst_sz - 1);
    dst[dst_sz - 1] = '\0';
}

static void format_bytes_compact(unsigned int bytes, char* out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    if (bytes >= 1024u * 1024u) {
        snprintf(out, out_sz, "%.2fMB", (double)bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024u) {
        snprintf(out, out_sz, "%.1fKB", (double)bytes / 1024.0);
    } else {
        snprintf(out, out_sz, "%uB", bytes);
    }
}

static volatile int g_hooks_rng_trace_active = 0;
static HooksRngTrace g_hooks_rng_trace;

static const char* hooks_rng_kind_name(uint32_t kind) {
    switch (kind) {
        case 1: return "mrand";
        case 2: return "rnd";
        case 3: return "frnd";
        case 4: return "rnd5050";
        case 5: return "rndsign";
        default: return "rng";
    }
}

static const char* hooks_rng_known_caller(uintptr_t caller) {
    switch ((uint32_t)caller) {
        case 0x0041BB8Cu: return "respawn_warble";
        case 0x0041BC18u: return "synth_whistling.frnd";
        case 0x0041BC2Fu: return "synth_whistling.rnd";
        case 0x0041BC36u: return "synth_whistling.rndsign";
        case 0x00427E5Bu: return "find_good_spot";
        case 0x00427F23u: return "find_good_spot";
        case 0x00428204u: return "player_respawn.particle_rnd";
        case 0x00428217u: return "player_respawn.particle_flip";
        case 0x0042847Cu: return "player_respawn_self";
        default: return NULL;
    }
}

static void hooks_rng_trace_format_event(const HooksRngTraceEvent* ev, char* out, size_t out_cap) {
    const char* known = NULL;
    if (!out || out_cap == 0) return;
    if (!ev || ev->kind == 0) {
        snprintf(out, out_cap, "none");
        return;
    }
    known = hooks_rng_known_caller(ev->caller);
    if (known) {
        snprintf(out, out_cap, "%s@%08X:%s",
                 hooks_rng_kind_name(ev->kind),
                 (unsigned int)ev->caller,
                 known);
    } else {
        snprintf(out, out_cap, "%s@%08X",
                 hooks_rng_kind_name(ev->kind),
                 (unsigned int)ev->caller);
    }
}

void hooks_rng_trace_begin(uint32_t frame, uint32_t phase) {
    memset(&g_hooks_rng_trace, 0, sizeof(g_hooks_rng_trace));
    g_hooks_rng_trace.frame = frame;
    g_hooks_rng_trace.phase = phase;
    g_hooks_rng_trace_active = 1;
}

void hooks_rng_trace_end(void) {
    g_hooks_rng_trace_active = 0;
}

void hooks_rng_trace_copy(HooksRngTrace* out_trace) {
    if (!out_trace) return;
    *out_trace = g_hooks_rng_trace;
}

void hooks_rng_trace_describe_diff(const HooksRngTrace* expected, const HooksRngTrace* got, char* out, size_t out_cap) {
    uint32_t expected_count = expected ? expected->count : 0u;
    uint32_t got_count = got ? got->count : 0u;
    uint32_t min_count = (expected_count < got_count) ? expected_count : got_count;
    uint32_t first_diff = min_count;
    char expected_ev[96];
    char got_ev[96];

    if (!out || out_cap == 0) return;

    for (uint32_t i = 0; i < min_count; i++) {
        const HooksRngTraceEvent* a = &expected->events[i];
        const HooksRngTraceEvent* b = &got->events[i];
        if (a->kind != b->kind || a->caller != b->caller) {
            first_diff = i;
            break;
        }
    }

    if (first_diff < expected_count && first_diff < HOOKS_RNG_TRACE_CAPACITY) {
        hooks_rng_trace_format_event(&expected->events[first_diff], expected_ev, sizeof(expected_ev));
    } else {
        snprintf(expected_ev, sizeof(expected_ev), "none");
    }
    if (first_diff < got_count && first_diff < HOOKS_RNG_TRACE_CAPACITY) {
        hooks_rng_trace_format_event(&got->events[first_diff], got_ev, sizeof(got_ev));
    } else {
        snprintf(got_ev, sizeof(got_ev), "none");
    }

    snprintf(out,
             out_cap,
             "rng_trace expected_count=%u got_count=%u first_diff=%u expected=%s got=%s expected_overflow=%u got_overflow=%u",
             (unsigned int)expected_count,
             (unsigned int)got_count,
             (unsigned int)first_diff,
             expected_ev,
             got_ev,
             expected ? (unsigned int)expected->overflow : 0u,
             got ? (unsigned int)got->overflow : 0u);
}

void __cdecl hooks_rng_trace_record_from_hook(uint32_t kind, uintptr_t caller) {
    uint32_t index;
    if (!g_hooks_rng_trace_active) return;
    index = g_hooks_rng_trace.count++;
    if (index < HOOKS_RNG_TRACE_CAPACITY) {
        g_hooks_rng_trace.events[index].kind = kind;
        g_hooks_rng_trace.events[index].caller = caller;
    } else {
        g_hooks_rng_trace.overflow++;
    }
}

/*
 * Per-frame cosmetic RNG isolation (bug-1 fix).
 *
 * The rngtrace diagnostic showed ~80 frnd/rnd draws PER FRAME coming from a
 * tight cluster of call sites (0x0043E4xx-0x0043EAxx, a camera-culled tile/
 * background animation) that advance the shared gameplay seed _mrand_seed. The
 * NUMBER of draws depends on the local camera, which is intentionally excluded
 * from the synced/checksummed state, so two peers draw a different count and
 * the gameplay seed drifts - surfacing as desyncs on respawn/teleport (the
 * camera jumps) and invisible to single-process replay (F3) because one
 * process is internally self-consistent. Route exactly those call sites to a
 * private cosmetic seed so they never perturb the gameplay seed; the visuals
 * stay random while gameplay RNG (respawn side, etc.) keeps the deterministic
 * seed. Caller addresses come straight from the rngtrace dump (return address
 * captured via __builtin_return_address in the C hooks below).
 */
typedef long double (__cdecl *fn_rng_frnd_t)(float, float);
typedef int         (__cdecl *fn_rng_rnd_t)(int, int);
typedef unsigned    (__cdecl *fn_rng_rnd5050_t)(void);

static uint32_t g_cosmetic_rng_seed = 0x1ED37A11u;
static volatile int g_cosmetic_rng_depth = 0;

/*
 * Audio-thread RNG isolation (the "toggle sound = desync" / map-dependent fix).
 *
 * The native synth's per-voice DSP callbacks run from SDL's audio callback
 * (mixaudio@0x414d70 -> synth_effects_update@0x408600 -> the effect callback),
 * i.e. on a SEPARATE SDL audio thread. Several of those callbacks draw
 * frnd/rnd/rndsign for waveform variation, and the vanilla RNG reads+writes the
 * single shared _mrand_seed. So the audio thread was asynchronously advancing the
 * GAMEPLAY rng an unpredictable number of times per frame - count depends on how
 * many voices are live (=> map/scene dependent), it stops entirely when sound is
 * off (=> toggling sound flips it), and the timing races the sim thread. That
 * corrupts the deterministic simulation and shows up as rng/things/players
 * desyncs. (The prior code wrapped two callbacks with a seed-swap, but that only
 * covered 2 of N voices AND still wrote _mrand_seed from the audio thread.)
 *
 * Fix: the audio thread must NEVER touch _mrand_seed. Detect "not the sim thread"
 * and serve those draws from a private audio seed that reproduces the game's exact
 * LCG (so the audio stays bit-identical to vanilla) without reading/writing
 * _mrand_seed. The sim thread (whoever runs game_update) keeps the real RNG.
 * Lock-free: the gameplay seed is touched only by the sim thread, the audio seed
 * only by the audio thread.
 */
static volatile DWORD g_sim_thread_id = 0;             /* thread that runs game_update */
static uint32_t       g_audio_thread_rng_seed = 0x2545F491u; /* private to the audio thread */

static int hooks_on_audio_thread(void) {
    DWORD sim = g_sim_thread_id;
    return sim != 0 && GetCurrentThreadId() != sim;
}

/* One step of the native mad RNG LCG (matches mrand/rnd/frnd @0x405080..0x405224),
 * on the private audio seed. Callers shift/mask the result exactly like vanilla. */
static uint32_t hooks_audio_rng_step(void) {
    uint32_t s = g_audio_thread_rng_seed * 0x41c64e6du + 0x3039u;
    s = (s >> 1) ^ s ^ ((uint32_t)(-(int32_t)(s & 1u)) & 0xd0000001u);
    g_audio_thread_rng_seed = s;
    return s;
}

static int hooks_rng_caller_is_cosmetic(uintptr_t caller) {
    uint32_t c = (uint32_t)caller;
    /*
     * Cosmetic background tile/decoration "action" handlers, registered in the
     * tile-type table by tiledef_init. These are render-only and draw frnd/rnd/
     * rnd5050 purely for visual variation - crowd spectators, waterfall spray
     * particles (~50 draws/frame), and "puzzley" decoration tiles. Their per-frame
     * draw COUNT depends on camera culling / particle state, which is NOT synced
     * between peers (waterfall state is even canonicalized out of the rollback
     * checksum), so on the shared gameplay seed they drift it. None of these
     * functions write gameplay state. Isolated by whole-function address range
     * (bounds from the objdump symbol table); the intervening tile actions
     * (spikes/wall/scroll/lighting) draw no RNG, so the gaps are moot.
     */
    if (c >= 0x0043E1B0u && c < 0x0043E360u) return 1; /* _puzzley_action   */
    if (c >= 0x0043E450u && c < 0x0043EB10u) return 1; /* _crowd_action     */
    if (c >= 0x0043EE20u && c < 0x0043F4B0u) return 1; /* _waterfall_action */
    /*
     * Self-contained cosmetic SOUND helpers that draw frnd/rnd purely for sfx
     * pitch/variation off the shared gameplay _mrand_seed. Each is gated by
     * state that is intentionally NOT synced/rolled-back (the wall-clock
     * _mad_ticks debounce, or the camera-shake intensity which is canonicalized
     * out of the checksum), so the two peers draw a different NUMBER of times
     * and the gameplay seed drifts - surfacing as desyncs on death/respawn/
     * score (when these fire). Isolated by whole-function address range
     * (bounds from objdump); verified each range contains ONLY cosmetic RNG, no
     * gameplay/particle/tilemap draws. This is the actual death-desync fix:
     *   do_cheer is called straight from player_die (ghidra 20839) before the
     *   dropped-sword spawn frnd, so an unisolated cheer draw corrupts the very
     *   next gameplay draw -> the dropped sword (a "thing") diverges.
     */
    if (c >= 0x004224E0u && c < 0x004227C0u) return 1; /* do_cheer  (frnd/rnd cheer pitch) - player_die + score */
    if (c >= 0x0041EF40u && c < 0x0041EFB0u) return 1; /* do_whistle (rnd whistle pitch) */
    if (c >= 0x004222A0u && c < 0x00422460u) return 1; /* game_update_camera (frnd x2 screen shake) */
    /*
     * "creepy" room ambience inside player_update_movement (sound_creepy@0x4240f3):
     * rnd5050 + 5 frnd to randomize the sfx, gated ONLY by a wall-clock dedup
     * (_mad_ticks - __last__14917 < 0xf, ghidra 21704), so it fires on different
     * frames per peer and drifts the gameplay seed (residual death-free desyncs
     * near ambience tiles). The enclosing function is mixed gameplay/cosmetic, so
     * isolate ONLY the creepy draws by return address, NOT the whole function. The
     * adjacent thump/jump sounds are gameplay-triggered (deterministic) and left
     * alone. Main path 0x4240ff..0x424193; the rnd5050==0 branch's frnd is 0x424410. */
    if (c >= 0x004240FFu && c < 0x00424194u) return 1;
    switch (c) {
        case 0x00424410u: /* creepy frnd(0,45) - rnd5050()==0 branch */
        /* Cosmetic RNG draws inline in game_update (a mixed gameplay/cosmetic fn,
         * so isolated per call site, not by range). Verified against objdump:
         * each is a sound pitch/pan draw stored into a sound object, gated by the
         * (non-synced) crowd/chant timers. Waterfall sound pitch/volume stored
         * into the _fx_15991 sound object (ADDR_WATERFALL_FX 0x541E44 +0x3c/+0x44).
         * NB: 0x42CA16/0x42CA2C are NOT here despite a prior comment calling them
         * "crowd ambient pan" - disasm shows they are rnd(0,32)/rnd(0,11) feeding
         * map_tile@0x42ca33 (gameplay tile-decoration that writes the checksummed
         * tilemap), so they MUST stay on the gameplay seed. The real crowd-murmur
         * pan rnd(-10,10) is 0x42dc79/0x42dca5 below. */
        case 0x0042C801u: /* frnd(0,2)   - crowd cheer colour trigger */
        case 0x0042C9B8u: /* frnd(0.9,1) - chant sound pitch (sound_noise@0x42c971) */
        case 0x0042DC79u: /* rnd(-10,10) - crowd murmur sound pan */
        case 0x0042DCA5u: /* rnd(-10,10) - crowd murmur sound pan */
        case 0x0042D19Cu: /* frnd(-1,1)  - waterfall sound randomize */
        case 0x0042D23Eu: /* frnd(1,0.5) - waterfall sound randomize */
            return 1;
        default:
            return 0;
    }
}

#ifdef HOOKS_INTELLISENSE
static uint32_t __cdecl hooked_mrand(void) { return 0; }
static int __cdecl hooked_rnd(int a, int b) { (void)a; (void)b; return 0; }
static long double __cdecl hooked_frnd(float a, float b) { (void)a; (void)b; return 0.0L; }
static unsigned __cdecl hooked_rnd5050(void) { return 0; }
static long double __cdecl hooked_rndsign(void) { return 0.0L; }
#else
/*
 * All five RNG hooks are C wrappers (reached via the detour's jmp, so
 * __builtin_return_address(0) is the original draw site). Two isolation layers:
 *   1. AUDIO THREAD: any draw off the sim thread is the synth running on SDL's
 *      audio thread - serve it from the private audio seed, never touch _mrand_seed.
 *   2. SIM-thread COSMETIC call sites (hooks_rng_caller_is_cosmetic): swap in the
 *      private cosmetic seed around the real draw so visuals stay random but the
 *      gameplay seed is untouched.
 * The trace records only NON-cosmetic sim-thread draws so a desync dump isn't
 * flooded. Everything else passes straight through on the real gameplay seed.
 * (mrand/rndsign used to be naked tail-jumps; converted to C so they can take the
 * audio-thread branch like rnd/frnd/rnd5050.)
 */
static uint32_t __cdecl hooked_mrand(void) {
    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    if (hooks_on_audio_thread()) return hooks_audio_rng_step() >> 16;
    hooks_rng_trace_record_from_hook(1u, caller);
    if (!g_hooks_rng_mrand_trampoline) return 0;
    return ((uint32_t (__cdecl*)(void))g_hooks_rng_mrand_trampoline)();
}

static int __cdecl hooked_rnd(int lo, int hi) {
    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    fn_rng_rnd_t real = (fn_rng_rnd_t)g_hooks_rng_rnd_trampoline;
    int cosmetic;
    int result;
    if (hooks_on_audio_thread()) {
        /* rnd(lo,hi): base + (seed>>16) % (|hi-lo|+1), matching rnd @0x4050b0 */
        uint32_t s = hooks_audio_rng_step();
        int base = (hi <= lo) ? hi : lo;
        uint32_t span = (uint32_t)((hi >= lo) ? (hi - lo) : (lo - hi));
        return (int)((s >> 16) % (span + 1u)) + base;
    }
    cosmetic = hooks_rng_caller_is_cosmetic(caller);
    /* Trace only non-cosmetic draws so a desync dump isn't flooded by isolated
     * cosmetic spray; cosmetic draws no longer touch the gameplay seed anyway. */
    if (!cosmetic) hooks_rng_trace_record_from_hook(2u, caller);
    if (!real) return 0;
    if (g_native_mrand_seed && g_cosmetic_rng_depth == 0 && cosmetic) {
        uint32_t game_seed = *g_native_mrand_seed;
        g_cosmetic_rng_depth++;
        *g_native_mrand_seed = g_cosmetic_rng_seed;
        result = real(lo, hi);
        g_cosmetic_rng_seed = *g_native_mrand_seed;
        *g_native_mrand_seed = game_seed;
        g_cosmetic_rng_depth--;
        return result;
    }
    return real(lo, hi);
}

static long double __cdecl hooked_frnd(float lo, float hi) {
    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    fn_rng_frnd_t real = (fn_rng_frnd_t)g_hooks_rng_frnd_trampoline;
    int cosmetic;
    long double result;
    if (hooks_on_audio_thread()) {
        /* frnd(lo,hi): (seed>>16)*(hi-lo)/65535 + lo, matching frnd @0x405170 */
        uint32_t s = hooks_audio_rng_step();
        return ((long double)(s >> 16) * ((long double)hi - (long double)lo)) / 65535.0L
               + (long double)lo;
    }
    cosmetic = hooks_rng_caller_is_cosmetic(caller);
    if (!cosmetic) hooks_rng_trace_record_from_hook(3u, caller);
    if (!real) return 0.0L;
    if (g_native_mrand_seed && g_cosmetic_rng_depth == 0 && cosmetic) {
        uint32_t game_seed = *g_native_mrand_seed;
        g_cosmetic_rng_depth++;
        *g_native_mrand_seed = g_cosmetic_rng_seed;
        result = real(lo, hi);
        g_cosmetic_rng_seed = *g_native_mrand_seed;
        *g_native_mrand_seed = game_seed;
        g_cosmetic_rng_depth--;
        return result;
    }
    return real(lo, hi);
}

static unsigned __cdecl hooked_rnd5050(void) {
    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    fn_rng_rnd5050_t real = (fn_rng_rnd5050_t)g_hooks_rng_rnd5050_trampoline;
    int cosmetic;
    unsigned result;
    if (hooks_on_audio_thread()) {
        /* rnd5050(): (seed>>16)&1, matching rnd5050 @0x405210 */
        return (hooks_audio_rng_step() >> 16) & 1u;
    }
    cosmetic = hooks_rng_caller_is_cosmetic(caller);
    if (!cosmetic) hooks_rng_trace_record_from_hook(4u, caller);
    if (!real) return 0;
    if (g_native_mrand_seed && g_cosmetic_rng_depth == 0 && cosmetic) {
        uint32_t game_seed = *g_native_mrand_seed;
        g_cosmetic_rng_depth++;
        *g_native_mrand_seed = g_cosmetic_rng_seed;
        result = real();
        g_cosmetic_rng_seed = *g_native_mrand_seed;
        *g_native_mrand_seed = game_seed;
        g_cosmetic_rng_depth--;
        return result;
    }
    return real();
}

static long double __cdecl hooked_rndsign(void) {
    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    if (hooks_on_audio_thread()) {
        /* rndsign(): (seed & 0x10000) ? -1 : 1, matching rndsign @0x4052c0 */
        uint32_t s = hooks_audio_rng_step();
        return (s & 0x10000u) ? -1.0L : 1.0L;
    }
    hooks_rng_trace_record_from_hook(5u, caller);
    if (!g_hooks_rng_rndsign_trampoline) return 0.0L;
    return ((long double (__cdecl*)(void))g_hooks_rng_rndsign_trampoline)();
}
#endif

int hooks_get_native_synth_enabled(void) {
    return g_native_synth_enabled ? ((*g_native_synth_enabled != 0) ? 1 : 0) : 0;
}

int hooks_set_native_synth_enabled(int enabled) {
    int old_enabled = hooks_get_native_synth_enabled();
    if (g_native_synth_enabled) {
        *g_native_synth_enabled = enabled ? 1u : 0u;
    }
    return old_enabled;
}

static uint32_t g_audio_rng_seed = 0xA53C9E21u;
static volatile int g_audio_rng_wrap_depth = 0;

static int hooks_call_synth_callback_with_audio_rng(fn_synth_callback_t callback, void* effect) {
    int result = 0;
    uint32_t game_seed = 0;

    if (!callback) return 0;
    if (!g_native_mrand_seed || g_audio_rng_wrap_depth > 0) {
        return callback(effect);
    }

    game_seed = *g_native_mrand_seed;
    g_audio_rng_wrap_depth++;
    *g_native_mrand_seed = g_audio_rng_seed;
    result = callback(effect);
    g_audio_rng_seed = *g_native_mrand_seed;
    *g_native_mrand_seed = game_seed;
    g_audio_rng_wrap_depth--;
    return result;
}

static int __cdecl hooked_respawn_warble(void* effect) {
    return hooks_call_synth_callback_with_audio_rng(p_respawn_warble_trampoline, effect);
}

static int __cdecl hooked_synth_effect_whistling(void* effect) {
    return hooks_call_synth_callback_with_audio_rng(p_synth_effect_whistling_trampoline, effect);
}

/* sound_sword_ching() draws frnd() (off the shared _mrand_seed) at trigger time
 * for pitch variation, gated by a per-tick dedup static that is NOT part of the
 * rolled-back state. Under rollback replay that gate goes stale, so the number
 * of RNG draws differs from the live frame and the gameplay RNG stream drifts -
 * eventually corrupting checksummed state (the desync seen on death/respawn/
 * teleport). Run it against the dedicated audio RNG so these cosmetic draws can
 * never touch the gameplay stream, exactly like the synth DSP callbacks above. */
static void* __cdecl hooked_sound_sword_ching(float pitch, float secondary) {
    void* result;
    uint32_t game_seed;

    if (!p_sound_sword_ching_trampoline) return NULL;
    if (!g_native_mrand_seed || g_audio_rng_wrap_depth > 0) {
        return p_sound_sword_ching_trampoline(pitch, secondary);
    }

    game_seed = *g_native_mrand_seed;
    g_audio_rng_wrap_depth++;
    *g_native_mrand_seed = g_audio_rng_seed;
    result = p_sound_sword_ching_trampoline(pitch, secondary);
    g_audio_rng_seed = *g_native_mrand_seed;
    *g_native_mrand_seed = game_seed;
    g_audio_rng_wrap_depth--;
    return result;
}

static int str_bool_true(const char* s) {
    if (!s) return 0;
    if (_stricmp(s, "1") == 0) return 1;
    if (_stricmp(s, "true") == 0) return 1;
    if (_stricmp(s, "yes") == 0) return 1;
    if (_stricmp(s, "on") == 0) return 1;
    return 0;
}

static int str_bool_false(const char* s) {
    if (!s) return 0;
    if (_stricmp(s, "0") == 0) return 1;
    if (_stricmp(s, "false") == 0) return 1;
    if (_stricmp(s, "no") == 0) return 1;
    if (_stricmp(s, "off") == 0) return 1;
    return 0;
}

static int is_mods_state_active(void) {
    return p_state_current && (p_state_current() == (void*)&g_mods_state);
}

static int is_console_state_active(void) {
    return p_state_current && (p_state_current() == (void*)&g_console_state);
}

static int is_online_hub_state_active(void) {
    return p_state_current && (p_state_current() == (void*)&g_online_hub_state);
}

static int is_online_result_state_active(void) {
    return p_state_current && (p_state_current() == (void*)&g_online_result_state);
}

static const char* state_name_from_ptr(void* st) {
    HookCustomState* custom;
    if (!st) return "none";
    if (st == (void*)&g_console_state) return "console";
    if (st == (void*)&g_mods_state) return "mods";
    if (st == (void*)&g_mods_entry_state) return "mods_entry";
    if (st == (void*)&g_online_hub_state) return "online_hub";
    if (st == (void*)&g_online_result_state) return "online_result";
    custom = find_custom_state_by_ptr(st);
    if (custom) return custom->name;
    if (st == (void*)(uintptr_t)ADDR_MAIN_STATE) return "main";
    if (st == (void*)(uintptr_t)ADDR_MAIN_STATE_INITIAL) return "main_initial";
    if (st == (void*)(uintptr_t)ADDR_GAME_STATE) return "game";
    if (st == (void*)(uintptr_t)ADDR_OPTIONS_STATE) return "options";
    if (st == (void*)(uintptr_t)ADDR_OPTIONS_STATE_PAUSED) return "options_paused";
    return "unknown";
}

static HookCustomState* find_custom_state_by_name(const char* name) {
    int i;
    if (!name || !name[0]) return NULL;
    for (i = 0; i < MAX_CUSTOM_STATES; i++) {
        if (!g_custom_states[i].used) continue;
        if (_stricmp(g_custom_states[i].name, name) == 0) return &g_custom_states[i];
    }
    return NULL;
}

static HookCustomState* find_custom_state_by_ptr(void* state_ptr) {
    int i;
    if (!state_ptr) return NULL;
    for (i = 0; i < MAX_CUSTOM_STATES; i++) {
        if (!g_custom_states[i].used) continue;
        if (state_ptr == (void*)&g_custom_states[i].state) return &g_custom_states[i];
    }
    return NULL;
}

static HookCustomState* find_active_custom_state(void) {
    void* cur = p_state_current ? p_state_current() : NULL;
    return find_custom_state_by_ptr(cur);
}

static int is_custom_state_ptr(void* state_ptr) {
    return find_custom_state_by_ptr(state_ptr) ? 1 : 0;
}

static float approx_text_width(const char* text, float scale) {
    if (!text) return 0.0f;
    // font8x8 atlas is 145x145 (16x16 cells with 1px gutters):
    // effective advance is 9px per glyph at scale=1.
    return (float)strlen(text) * 9.0f * scale;
}

static void mods_restore_render_state(void) {
    // Reset full turtle state when possible. The cursor/shadow pass depends on
    // more than angle/scale/color, and partial restores can make shadows look
    // like extra mini swords.
    if (p_turtle_reset) {
        p_turtle_reset();
        return;
    }

    // Fallback for builds where turtle_reset is unavailable.
    p_turtle_set_angle(0.0);
    p_turtle_set_scale(1.0, 1.0);
    if (p_turtle_set_rgba) p_turtle_set_rgba(1.0f, 1.0f, 1.0f, 1.0f);
    else p_turtle_set_rgb(1.0f, 1.0f, 1.0f);
}

static void draw_text_scaled_mode_alpha(float x, float y, float scale,
                                        float r, float g, float b, float a,
                                        const char* text, int mode) {
    if (!text || !p_plot_text) return;
    p_turtle_set_angle(0.0);
    p_turtle_set_scale((double)scale, (double)scale);
    if (p_turtle_set_rgba) p_turtle_set_rgba(r, g, b, a);
    else p_turtle_set_rgb(r, g, b);
    p_turtle_set_pos_unscaled((double)x, (double)y);
    p_plot_text(text, mode);
}

static void draw_text_scaled_mode(float x, float y, float scale, float r, float g, float b, const char* text, int mode) {
    draw_text_scaled_mode_alpha(x, y, scale, r, g, b, 1.0f, text, mode);
}

static void draw_text_scaled(float x, float y, float scale, float r, float g, float b, const char* text) {
    // Left-aligned (base engine: plot_text align=0).
    draw_text_scaled_mode(x, y, scale, r, g, b, text, 0);
}

static void draw_text(float x, float y, float r, float g, float b, const char* text) {
    draw_text_scaled(x, y, g_ui_scale, r, g, b, text);
}

static void draw_text_centered_scaled(float cx, float y, float scale, float r, float g, float b, const char* text) {
    // plot_text mode=1 is already centered around turtle x in the base UI.
    draw_text_scaled_mode(cx, y, scale, r, g, b, text, 1);
}

static void draw_text_right_scaled(float right_x, float y, float scale, float r, float g, float b, const char* text) {
    // Right-aligned (base engine: plot_text align=2).
    draw_text_scaled_mode(right_x, y, scale, r, g, b, text, 2);
}

static void draw_text_centered(float cx, float y, float r, float g, float b, const char* text) {
    draw_text_centered_scaled(cx, y, g_ui_scale, r, g, b, text);
}

static void draw_text_right(float right_x, float y, float r, float g, float b, const char* text) {
    draw_text_right_scaled(right_x, y, g_ui_scale, r, g, b, text);
}


static void build_repeat(char* dst, size_t dst_sz, char ch, int count) {
    if (!dst || dst_sz == 0) return;
    if (count < 0) count = 0;
    if ((size_t)count > dst_sz - 1) count = (int)(dst_sz - 1);
    for (int i = 0; i < count; i++) dst[i] = ch;
    dst[count] = '\0';
}
static void mods_calc_layout(ModsLayout* L) {
    if (!L) return;
    memset(L, 0, sizeof(*L));

    L->w = p_mad_w ? p_mad_w() : BASE_UI_W;
    L->h = p_mad_h ? p_mad_h() : BASE_UI_H;
    L->ui = calc_ui_scale();

    // Text scale tuned for readability. We also clamp it using the current
    // window size to avoid going off-screen on tiny windows.
    {
        float s = 1.48f * L->ui; // a bit bigger than v2
        // If the window is very short, shrink slightly to keep rows visible.
        if (L->h < 520.0f) s *= 0.92f;
        if (L->h < 420.0f) s *= 0.88f;
        L->text_scale = clampf(s, 0.98f, 2.20f);
    }

    L->center_x = L->w * 0.5f;

    // Centered content area (Everest-ish), responsive to window size.
    // - On small windows we must stay within safe margins.
    // - On large windows we cap width to keep text comfortably readable.
    {
        float safe_margin = 44.0f * L->ui;
        float max_by_window = L->w - (safe_margin * 2.0f);
        float min_w = 520.0f * L->ui;
        float max_w = 980.0f * L->ui;
        float ideal = L->w * 0.70f;
        L->content_w = clampf(ideal, min_w, max_w);
        if (L->content_w > max_by_window) L->content_w = max_by_window;
        if (L->content_w < 320.0f) L->content_w = 320.0f;
    }

    L->left  = L->center_x - (L->content_w * 0.5f);
    L->right = L->center_x + (L->content_w * 0.5f);

    // Columns: label left, value right. Keep a consistent value column even
    // when content_w changes.
    {
        float pad_left = 96.0f * L->ui;
        float pad_right = 24.0f * L->ui;
        L->label_x = L->left + pad_left;
        // value_x is the start of the value column.
        L->value_x = L->left + (L->content_w * 0.60f);
        // Ensure the value column has enough breathing room.
        if (L->value_x > L->right - (180.0f * L->ui)) {
            L->value_x = L->right - (180.0f * L->ui);
        }
        // Clamp label_x to not collide with value_x on narrow windows.
        if (L->label_x > L->value_x - (90.0f * L->ui)) {
            L->label_x = L->value_x - (90.0f * L->ui);
        }
        (void)pad_right;
    }

    // List box: proportional top/bottom padding so it scales with window height.
    {
        float top_pad = (L->h * 0.18f);
        float bot_pad = (L->h * 0.14f);
        float min_top = 108.0f * L->ui;
        float min_bot = 84.0f * L->ui;
        if (top_pad < min_top) top_pad = min_top;
        if (bot_pad < min_bot) bot_pad = min_bot;
        L->list_top = top_pad;
        L->list_bottom = L->h - bot_pad;
        // Never invert.
        if (L->list_bottom < L->list_top + (80.0f * L->ui)) {
            L->list_bottom = L->list_top + (80.0f * L->ui);
        }
    }

    L->row_h = 34.0f * L->ui;
    if (L->row_h < 12.0f) L->row_h = 12.0f;
}
static int visible_rows_capacity(void) {
    ModsLayout L;
    mods_calc_layout(&L);

    // Keep g_ui_scale in sync with render/update.
    g_ui_scale = L.text_scale;

    int list_top = (int)L.list_top;
    int list_bottom = (int)L.list_bottom;
    int row_h = (int)L.row_h;

    int cap = (list_bottom - list_top) / row_h;
    if (cap < 4) cap = 4;
    return cap;
}

static int mods_is_collapsed(int mod_index) {
    if (mod_index < 0 || mod_index >= MAX_MODS_TRACKED) return 0;
    return g_mod_collapsed[mod_index] ? 1 : 0;
}

static void mods_set_collapsed(int mod_index, int collapsed) {
    if (mod_index < 0 || mod_index >= MAX_MODS_TRACKED) return;
    g_mod_collapsed[mod_index] = collapsed ? 1 : 0;
}

static void mods_toggle_collapsed(int mod_index) {
    if (mod_index < 0 || mod_index >= MAX_MODS_TRACKED) return;
    g_mod_collapsed[mod_index] = g_mod_collapsed[mod_index] ? 0 : 1;
}

static int find_mod_header_row_for_index(int row_index) {
    if (row_index < 0 || row_index >= g_row_count) return -1;
    int mod_index = g_rows[row_index].mod_index;
    if (mod_index < 0) return -1;
    for (int i = row_index; i >= 0; --i) {
        if (g_rows[i].kind == ROW_MOD_HEADER && g_rows[i].mod_index == mod_index) return i;
    }
    return -1;
}

static void ensure_scroll_visible(void) {
    int cap = visible_rows_capacity();
    int max_scroll = (g_row_count > cap) ? (g_row_count - cap) : 0;
    int margin = (cap >= 8) ? 2 : 1;

    if (g_selected_row < 0 || g_selected_row >= g_row_count) {
        g_scroll_row = clampi(g_scroll_row, 0, max_scroll);
        return;
    }

    if (g_selected_row < g_scroll_row + margin) {
        g_scroll_row = g_selected_row - margin;
    } else if (g_selected_row > g_scroll_row + cap - margin - 1) {
        g_scroll_row = g_selected_row - (cap - margin - 1);
    }

    if (g_rows[g_selected_row].mod_index >= 0) {
        int header_row = find_mod_header_row_for_index(g_selected_row);
        if (header_row >= 0) {
            int distance = g_selected_row - header_row;
            if (distance <= 4 && g_scroll_row > header_row) {
                g_scroll_row = header_row;
            }
        }
    }

    g_scroll_row = clampi(g_scroll_row, 0, max_scroll);
}


static void rows_clear(void) {
    g_row_count = 0;
}

static void rows_add(RowKind kind, int selectable, int mod_index, int cfg_index, const char* left, const char* right) {
    if (g_row_count >= MAX_MENU_ROWS) return;

    MenuRow* row = &g_rows[g_row_count++];
    row->kind = kind;
    row->selectable = selectable;
    row->mod_index = mod_index;
    row->cfg_index = cfg_index;
    safe_copy(row->left, sizeof(row->left), left ? left : "");
    safe_copy(row->right, sizeof(row->right), right ? right : "");
}

static int first_selectable_index(void) {
    for (int i = 0; i < g_row_count; i++) {
        if (g_rows[i].selectable) return i;
    }
    return -1;
}

static int last_selectable_index(void) {
    for (int i = g_row_count - 1; i >= 0; i--) {
        if (g_rows[i].selectable) return i;
    }
    return -1;
}

static int next_selectable(int start, int dir) {
    int i = start;
    while (1) {
        i += dir;
        if (i < 0 || i >= g_row_count) return -1;
        if (g_rows[i].selectable) return i;
    }
}

static RowKey selected_key(void) {
    RowKey k;
    k.kind = ROW_NONE;
    k.mod_index = -1;
    k.cfg_index = -1;

    if (g_selected_row >= 0 && g_selected_row < g_row_count) {
        MenuRow* row = &g_rows[g_selected_row];
        k.kind = row->kind;
        k.mod_index = row->mod_index;
        k.cfg_index = row->cfg_index;
    }
    return k;
}

static int row_matches_key(const MenuRow* row, RowKey key) {
    if (!row) return 0;
    if (row->kind != key.kind) return 0;
    if (row->mod_index != key.mod_index) return 0;
    if (row->cfg_index != key.cfg_index) return 0;
    return 1;
}

static void capture_clear(void) {
    g_capture_active = 0;
    g_capture_kind = CAPTURE_NONE;
    g_capture_mod = -1;
    g_capture_cfg = -1;
    g_capture_buf[0] = '\0';
}

static void begin_string_capture(int mod_index, int cfg_index) {
    const char* v = lua_manager_get_mod_config_value_str(mod_index, cfg_index);
    capture_clear();
    g_capture_active = 1;
    g_capture_kind = CAPTURE_CONFIG_STRING;
    g_capture_mod = mod_index;
    g_capture_cfg = cfg_index;
    safe_copy(g_capture_buf, sizeof(g_capture_buf), v ? v : "");
}

static void begin_bind_capture(int mod_index, int bind_index) {
    capture_clear();
    g_capture_active = 1;
    g_capture_kind = CAPTURE_BIND;
    g_capture_mod = mod_index;
    g_capture_cfg = bind_index;
    safe_copy(g_capture_buf, sizeof(g_capture_buf), "Press a key... Esc cancel, Backspace/Delete clear");
}

static int bind_name_to_sym(const char* name) {
    if (!name || !name[0]) return 0;
    if (_stricmp(name, "none") == 0 || _stricmp(name, "unbound") == 0 || _stricmp(name, "clear") == 0) return 0;
    if (_stricmp(name, "space") == 0) return SDLK_SPACE;
    if (_stricmp(name, "tab") == 0) return SDLK_TAB;
    if (_stricmp(name, "enter") == 0 || _stricmp(name, "return") == 0) return SDLK_RETURN;
    if (_stricmp(name, "escape") == 0 || _stricmp(name, "esc") == 0) return SDLK_ESCAPE;
    if (_stricmp(name, "backspace") == 0) return SDLK_BACKSPACE;
    if (_stricmp(name, "delete") == 0 || _stricmp(name, "del") == 0) return SDLK_DELETE;
    if (_stricmp(name, "left") == 0) return SDLK_LEFT;
    if (_stricmp(name, "right") == 0) return SDLK_RIGHT;
    if (_stricmp(name, "up") == 0) return SDLK_UP;
    if (_stricmp(name, "down") == 0) return SDLK_DOWN;
    if (_stricmp(name, "home") == 0) return SDLK_HOME;
    if (_stricmp(name, "end") == 0) return SDLK_END;
    if (_stricmp(name, "pageup") == 0 || _stricmp(name, "pgup") == 0) return SDLK_PAGEUP;
    if (_stricmp(name, "pagedown") == 0 || _stricmp(name, "pgdn") == 0) return SDLK_PAGEDOWN;
    if (_stricmp(name, "kp_enter") == 0) return SDLK_KP_ENTER;
    if (name[0] && !name[1]) return (unsigned char)tolower((unsigned char)name[0]);
    {
        char* end = NULL;
        long v = strtol(name, &end, 0);
        if (end && *end == '\0') return (int)v;
    }
    return 0;
}

static int console_find_bind_index_by_key(int mod_index, const char* key) {
    int count = lua_manager_get_mod_bind_count(mod_index);
    if (!key || !key[0]) return -1;
    for (int i = 0; i < count; i++) {
        const char* bind_key = lua_manager_get_mod_bind_key(mod_index, i);
        if (bind_key && _stricmp(bind_key, key) == 0) return i;
    }
    return -1;
}

static int console_apply_bind_value(int mod_index, int bind_index, const char* value) {
    int sym = bind_name_to_sym(value);
    if (!value) return 0;
    if (sym == 0 && value[0] && _stricmp(value, "none") != 0 && _stricmp(value, "unbound") != 0 && _stricmp(value, "clear") != 0) {
        return 0;
    }
    return (sym == 0) ? lua_manager_clear_mod_bind_value(mod_index, bind_index)
                      : lua_manager_set_mod_bind_value(mod_index, bind_index, sym);
}

static int console_apply_config_value(int idx, int cfg_idx, const char* value) {
    int type;
    int ok = 0;
    if (idx < 0 || cfg_idx < 0 || !value) return 0;
    type = lua_manager_get_mod_config_type(idx, cfg_idx);
    if (type == LUA_CFG_BOOL) {
        int want = 0;
        int cur = str_bool_true(lua_manager_get_mod_config_value_str(idx, cfg_idx)) ? 1 : 0;
        if (!console_try_parse_bool(value, &want)) return 0;
        ok = (want == cur) ? 1 : lua_manager_config_toggle_bool(idx, cfg_idx);
    } else if (type == LUA_CFG_INT) {
        long want = 0;
        long cur = 0;
        const char* cur_text = lua_manager_get_mod_config_value_str(idx, cfg_idx);
        if (!console_try_parse_long(value, &want)) return 0;
        if (!console_try_parse_long(cur_text, &cur)) cur = strtol(cur_text, NULL, 10);
        ok = lua_manager_config_increment_int(idx, cfg_idx, (int)(want - cur));
    } else if (type == LUA_CFG_FLOAT) {
        double want = 0.0;
        double cur = 0.0;
        const char* cur_text = lua_manager_get_mod_config_value_str(idx, cfg_idx);
        if (!console_try_parse_double(value, &want)) return 0;
        if (!console_try_parse_double(cur_text, &cur)) cur = atof(cur_text);
        ok = lua_manager_config_increment_float(idx, cfg_idx, want - cur);
    } else if (type == LUA_CFG_STRING) {
        ok = lua_manager_config_set_string(idx, cfg_idx, value);
    } else if (type == LUA_CFG_ENUM) {
        ok = lua_manager_config_set_option(idx, cfg_idx, value);
    }
    return ok;
}

static void console_run_reload_mods(void); // defined later, reused by the menu

// "Hide log console" framework toggle: persisted in mods/modframework.cfg and
// applied to the AllocConsole window. value 1 = console hidden.
static int fw_get_hide_console(void) {
    return fwsettings_get_int("hide_log_console", 1) ? 1 : 0;
}

static void fw_set_hide_console(int hide) {
    hide = hide ? 1 : 0;
    fwsettings_set_int("hide_log_console", hide);
    log_set_console_visible(hide ? 0 : 1);
}

static void rebuild_rows(void) {
    RowKey keep = selected_key();
    rows_clear();

    int mod_count = lua_manager_get_mod_count();
    if (mod_count <= 0) {
        rows_add(ROW_INFO, 0, -1, -1, "No mods detected in mods/", "");
        rows_add(ROW_DIVIDER, 0, -1, -1, "", "");
    }

    for (int mi = 0; mi < mod_count; mi++) {
        char header[192];
        char header_right[64];
        char desc_line[192];
        char warn[192];
        char runtime_line[192];
        char perf_line[192];
        char mem_buf[32];
        const char* name = lua_manager_get_mod_name(mi);
        const char* id = lua_manager_get_mod_id(mi);
        const char* author = lua_manager_get_mod_author(mi);
        const char* desc = lua_manager_get_mod_description(mi);
        int enabled = lua_manager_get_mod_enabled(mi);
        int error_count = lua_manager_get_mod_error_count(mi);
        int dep_count = lua_manager_get_mod_dependency_count(mi);
        int conflict_count = lua_manager_get_mod_conflict_count(mi);
        int missing_required = 0;
        int active_conflicts = 0;
        int collapsed = mods_is_collapsed(mi);
        LuaModDiagnostics diag;

        memset(&diag, 0, sizeof(diag));
        (void)lua_manager_get_mod_diagnostics(mi, &diag);

        if (!name || !name[0]) name = (id && id[0]) ? id : "(unnamed mod)";

        if (id && id[0] && author && author[0]) {
            snprintf(header, sizeof(header), "%s %s (%s) by %s", collapsed ? "[+]" : "[-]", name, id, author);
        } else if (id && id[0]) {
            snprintf(header, sizeof(header), "%s %s (%s)", collapsed ? "[+]" : "[-]", name, id);
        } else if (author && author[0]) {
            snprintf(header, sizeof(header), "%s %s by %s", collapsed ? "[+]" : "[-]", name, author);
        } else {
            snprintf(header, sizeof(header), "%s %s", collapsed ? "[+]" : "[-]", name);
        }

        for (int di = 0; di < dep_count; di++) {
            int optional = lua_manager_get_mod_dependency_optional(mi, di);
            int satisfied = lua_manager_mod_dependency_satisfied(mi, di);
            if (!optional && !satisfied) missing_required++;
        }
        for (int ci = 0; ci < conflict_count; ci++) {
            int active = lua_manager_mod_conflict_active(mi, ci);
            if (active) active_conflicts++;
        }

        header_right[0] = '\0';
        if (error_count > 0) {
            snprintf(header_right, sizeof(header_right), "errors=%d", error_count);
        } else if (missing_required > 0 || active_conflicts > 0) {
            snprintf(header_right, sizeof(header_right), "issues");
        }

        rows_add(ROW_MOD_HEADER, 1, mi, -1, header, header_right);

        if (desc && desc[0]) {
            safe_copy(desc_line, sizeof(desc_line), desc);
            rows_add(ROW_INFO, 0, mi, -1, desc_line, "");
        }

        if (!collapsed) {
            rows_add(ROW_INFO, 0, mi, -1, "Options", "");
            rows_add(ROW_MOD_TOGGLE, 1, mi, -1, "  Enabled", enabled ? "ON" : "OFF");

            if (error_count > 0 || missing_required > 0 || active_conflicts > 0) {
                snprintf(warn, sizeof(warn), "  Status: %d errors, %d missing required, %d active conflicts",
                         error_count, missing_required, active_conflicts);
                rows_add(ROW_INFO, 0, mi, -1, warn, "");
            }

            {
                int cfg_count = lua_manager_get_mod_config_count(mi);
                for (int ci = 0; ci < cfg_count; ci++) {
                    char label[128];
                    char value[192];
                    int type = lua_manager_get_mod_config_type(mi, ci);
                    const char* raw_label = lua_manager_get_mod_config_label(mi, ci);
                    const char* key = lua_manager_get_mod_config_key(mi, ci);
                    const char* raw_value = lua_manager_get_mod_config_value_str(mi, ci);
                    if (!raw_label || !raw_label[0]) raw_label = key;
                    if (!raw_label || !raw_label[0]) raw_label = "(option)";
                    if (!raw_value) raw_value = "";
                    snprintf(label, sizeof(label), "  %s", raw_label);
                    switch (type) {
                        case LUA_CFG_BOOL: safe_copy(value, sizeof(value), str_bool_true(raw_value) ? "ON" : "OFF"); break;
                        case LUA_CFG_ACTION: safe_copy(value, sizeof(value), "[Run]"); break;
                        case LUA_CFG_STRING: snprintf(value, sizeof(value), "\"%s\"", raw_value); break;
                        case LUA_CFG_ENUM: snprintf(value, sizeof(value), "< %s >", raw_value); break;
                        default: safe_copy(value, sizeof(value), raw_value); break;
                    }
                    rows_add(ROW_CONFIG, 1, mi, ci, label, value);
                }
            }

            {
                int bind_count = lua_manager_get_mod_bind_count(mi);
                for (int bi = 0; bi < bind_count; bi++) {
                    char label[128];
                    char value[192];
                    const char* raw_label = lua_manager_get_mod_bind_label(mi, bi);
                    const char* raw_key = lua_manager_get_mod_bind_key(mi, bi);
                    const char* raw_value = lua_manager_get_mod_bind_value_str(mi, bi);
                    snprintf(label, sizeof(label), "  Bind: %s", (raw_label && raw_label[0]) ? raw_label : ((raw_key && raw_key[0]) ? raw_key : "(bind)"));
                    safe_copy(value, sizeof(value), (raw_value && raw_value[0]) ? raw_value : "[Unbound]");
                    if (lua_manager_mod_bind_has_conflict(mi, bi) && strlen(value) + 11 < sizeof(value)) {
                        strcat(value, " [conflict]");
                    }
                    rows_add(ROW_BIND, 1, mi, bi, label, value);
                }
            }
        }

        if (mi != mod_count - 1) rows_add(ROW_DIVIDER, 0, -1, -1, "", "");
    }

    rows_add(ROW_DIVIDER, 0, -1, -1, "", "");

    // Framework options: QOL controls, just above Back.
    rows_add(ROW_FW_HEADER, 0, -1, -1, "Options", "");
    rows_add(ROW_FW_TOGGLE, 1, -1, FW_TOGGLE_HIDE_CONSOLE,
             "  Hide log console", fw_get_hide_console() ? "ON" : "OFF");
    rows_add(ROW_FW_ACTION, 1, -1, FW_ACTION_RELOAD_MODS,
             "  Reload mods", "[Run]");

    rows_add(ROW_BACK, 1, -1, -1, "Back", "");

    int found = -1;
    for (int i = 0; i < g_row_count; i++) {
        if (row_matches_key(&g_rows[i], keep)) {
            found = i;
            break;
        }
    }

    if (found >= 0) {
        g_selected_row = found;
    } else if (g_selected_row >= g_row_count) {
        g_selected_row = g_row_count - 1;
    }

    if (g_selected_row < 0 || g_selected_row >= g_row_count || !g_rows[g_selected_row].selectable) {
        int first = first_selectable_index();
        g_selected_row = first;
    }

    ensure_scroll_visible();
}

static void move_selection(int dir, int amount) {
    if (g_selected_row < 0) return;
    if (amount < 1) amount = 1;

    int prev = g_selected_row;
    int cur = g_selected_row;
    for (int i = 0; i < amount; i++) {
        int n = next_selectable(cur, dir);
        if (n < 0) break;
        cur = n;
    }

    g_selected_row = cur;
    ensure_scroll_visible();
    if (g_selected_row != prev) {
        mods_cursor_on_selection_changed();
    }
}

static void mods_go_back(void) {
    capture_clear();

    void* target = g_mods_return_state;
    if (target == (void*)&g_console_state) {
        target = g_console_return_state;
    }
    if (!target || target == (void*)&g_mods_state || target == (void*)&g_mods_entry_state) {
        target = (void*)(uintptr_t)ADDR_OPTIONS_STATE;
    }
    p_state_switch(target);
}

static void apply_adjustment_on_selected(int delta) {
    if (g_selected_row < 0 || g_selected_row >= g_row_count) return;

    MenuRow* row = &g_rows[g_selected_row];
    if (row->kind == ROW_MOD_HEADER) {
        mods_toggle_collapsed(row->mod_index);
    } else if (row->kind == ROW_FW_TOGGLE) {
        if (row->cfg_index == FW_TOGGLE_HIDE_CONSOLE) {
            int cur = fw_get_hide_console();
            fw_set_hide_console(delta > 0 ? 1 : (delta < 0 ? 0 : !cur));
        }
    } else if (row->kind == ROW_MOD_TOGGLE) {
        int enabled = lua_manager_get_mod_enabled(row->mod_index);
        lua_manager_set_mod_enabled(row->mod_index, delta > 0 ? 1 : (delta < 0 ? 0 : !enabled));
    } else if (row->kind == ROW_CONFIG) {
        int type = lua_manager_get_mod_config_type(row->mod_index, row->cfg_index);
        if (type == LUA_CFG_BOOL) {
            lua_manager_config_toggle_bool(row->mod_index, row->cfg_index);
        } else if (type == LUA_CFG_INT) {
            lua_manager_config_increment_int(row->mod_index, row->cfg_index, delta);
        } else if (type == LUA_CFG_FLOAT) {
            lua_manager_config_increment_float(row->mod_index, row->cfg_index, (double)delta * 0.1);
        } else if (type == LUA_CFG_ENUM) {
            lua_manager_config_cycle_option(row->mod_index, row->cfg_index, delta > 0 ? 1 : -1);
        }
    }

    rebuild_rows();
    mods_cursor_tick();
}

static void activate_selected(void) {
    if (g_selected_row < 0 || g_selected_row >= g_row_count) return;

    MenuRow* row = &g_rows[g_selected_row];
    if (row->kind == ROW_BACK) {
        mods_go_back();
        return;
    }

    if (row->kind == ROW_FW_TOGGLE) {
        if (row->cfg_index == FW_TOGGLE_HIDE_CONSOLE) {
            fw_set_hide_console(!fw_get_hide_console());
        }
        rebuild_rows();
        return;
    }
    if (row->kind == ROW_FW_ACTION) {
        if (row->cfg_index == FW_ACTION_RELOAD_MODS) {
            console_run_reload_mods();
        }
        rebuild_rows();
        return;
    }

    if (row->kind == ROW_MOD_HEADER) {
        mods_toggle_collapsed(row->mod_index);
    } else if (row->kind == ROW_MOD_TOGGLE) {
        lua_manager_set_mod_enabled(row->mod_index, lua_manager_get_mod_enabled(row->mod_index) ? 0 : 1);
    } else if (row->kind == ROW_BIND) {
        begin_bind_capture(row->mod_index, row->cfg_index);
    } else if (row->kind == ROW_CONFIG) {
        int type = lua_manager_get_mod_config_type(row->mod_index, row->cfg_index);
        if (type == LUA_CFG_BOOL) {
            lua_manager_config_toggle_bool(row->mod_index, row->cfg_index);
        } else if (type == LUA_CFG_INT) {
            lua_manager_config_increment_int(row->mod_index, row->cfg_index, 1);
        } else if (type == LUA_CFG_FLOAT) {
            lua_manager_config_increment_float(row->mod_index, row->cfg_index, 0.1);
        } else if (type == LUA_CFG_ACTION) {
            lua_manager_config_trigger_action(row->mod_index, row->cfg_index);
        } else if (type == LUA_CFG_STRING) {
            begin_string_capture(row->mod_index, row->cfg_index);
        } else if (type == LUA_CFG_ENUM) {
            lua_manager_config_cycle_option(row->mod_index, row->cfg_index, 1);
        }
    }

    rebuild_rows();
}

static char* trim_ws(char* s) {
    char* end;
    if (!s) return s;
    while (*s && isspace((unsigned char)*s)) s++;
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    return s;
}

static void console_clear_output(void) {
    g_console_line_head = 0;
    g_console_line_count = 0;
    g_console_scroll = 0;
}

static ConsoleLine* console_line_at_oldest_index(int idx) {
    int first;
    int slot;
    if (idx < 0 || idx >= g_console_line_count) return NULL;
    first = g_console_line_head - g_console_line_count;
    while (first < 0) first += CONSOLE_MAX_LINES;
    slot = first + idx;
    while (slot >= CONSOLE_MAX_LINES) slot -= CONSOLE_MAX_LINES;
    return &g_console_lines[slot];
}

static void console_push_line_rgb(const char* text, float r, float g, float b) {
    ConsoleLine* line = &g_console_lines[g_console_line_head];
    safe_copy(line->text, sizeof(line->text), text ? text : "");
    line->r = r;
    line->g = g;
    line->b = b;

    g_console_line_head++;
    if (g_console_line_head >= CONSOLE_MAX_LINES) g_console_line_head = 0;
    if (g_console_line_count < CONSOLE_MAX_LINES) g_console_line_count++;
    g_console_scroll = 0;
}

static void console_push_command_line(const char* cmd) {
    char line[CONSOLE_LINE_TEXT];
    snprintf(line, sizeof(line), "> %s", cmd ? cmd : "");
    console_push_line_rgb(line, 0.95f, 0.86f, 0.34f);
}

static int console_clipboard_set(const char* text) {
    return SDL_SetClipboardText(text ? text : "") == 0;
}

static void console_copy_input_to_clipboard(void) {
    if (console_clipboard_set(g_console_input)) {
        console_push_line_rgb("console.copy: copied input", 0.64f, 0.92f, 0.66f);
    } else {
        console_push_line_rgb("console.copy: clipboard write failed", 0.98f, 0.45f, 0.45f);
    }
}

static void console_copy_output_to_clipboard(void) {
    size_t cap = ((size_t)CONSOLE_LINE_TEXT + 2u) * (size_t)(g_console_line_count + 1);
    size_t pos = 0;
    char* buf = (char*)malloc(cap);
    if (!buf) {
        console_push_line_rgb("console.copy: out of memory", 0.98f, 0.45f, 0.45f);
        return;
    }
    buf[0] = '\0';
    for (int i = 0; i < g_console_line_count; i++) {
        ConsoleLine* line = console_line_at_oldest_index(i);
        size_t len;
        if (!line) continue;
        len = strlen(line->text);
        if (pos + len + 2 >= cap) break;
        memcpy(buf + pos, line->text, len);
        pos += len;
        buf[pos++] = '\r';
        buf[pos++] = '\n';
        buf[pos] = '\0';
    }
    if (console_clipboard_set(buf)) {
        char out[CONSOLE_LINE_TEXT];
        snprintf(out, sizeof(out), "console.copy: copied %d output line(s)", g_console_line_count);
        console_push_line_rgb(out, 0.64f, 0.92f, 0.66f);
    } else {
        console_push_line_rgb("console.copy: clipboard write failed", 0.98f, 0.45f, 0.45f);
    }
    free(buf);
}

static void console_paste_clipboard(void) {
    char* raw = SDL_GetClipboardText();
    char clean[CONSOLE_INPUT_BUF];
    size_t pos = 0;
    if (!raw || !raw[0]) {
        if (raw) SDL_free(raw);
        return;
    }
    for (const char* p = raw; *p && pos + 1 < sizeof(clean); p++) {
        unsigned char ch = (unsigned char)*p;
        if (ch == '\r' || ch == '\n' || ch == '\t') {
            if (pos > 0 && clean[pos - 1] != ' ') clean[pos++] = ' ';
        } else if (ch >= 32) {
            clean[pos++] = (char)ch;
        }
    }
    clean[pos] = '\0';
    console_insert_text(clean);
    SDL_free(raw);
}

static void console_copy_cmd(const char* arg) {
    char arg_buf[CONSOLE_INPUT_BUF];
    const char* mode = "";
    if (arg && arg[0]) {
        safe_copy(arg_buf, sizeof(arg_buf), arg);
        mode = trim_ws(arg_buf);
    }
    if (!mode || !mode[0] || _stricmp(mode, "output") == 0 || _stricmp(mode, "all") == 0) {
        console_copy_output_to_clipboard();
        return;
    }
    if (_stricmp(mode, "input") == 0) {
        console_copy_input_to_clipboard();
        return;
    }
    console_push_line_rgb("Usage: console.copy [output|input]", 0.98f, 0.76f, 0.40f);
}

static const char* k_console_commands[] = {
    "help", "commands", "clear", "history", "echo", "console.stats", "console.copy",
    "state", "state.last", "state.return", "state.switch", "sys.info", "ui.size",
    "time.scale", "framework.api",
    "mods.count", "mods.list", "mods.find", "mods.info", "mods.trace", "mods.enable", "mods.disable", "mods.toggle",
    "mods.config", "mods.config.find", "mods.config.get", "mods.config.set", "mods.config.action",
    "binds.list", "binds.find", "binds.set", "binds.clear",
    "reload.mods", "mods.reload", "reload.assets",
    "online.hub",
    "ggpo.net",
    "log.level", "log.tail", "input.show", "input.override", "input.clear",
    "lua", "eval", "lua.mod", "eval.mod", "lua.file", "exit", "quit",
};

static void console_history_path(char* out, size_t out_sz) {
    CreateDirectoryA("mods", NULL);
    snprintf(out, out_sz, "mods\\console_history.txt");
}

static void console_history_save(void) {
    char path[MAX_PATH];
    FILE* f;
    console_history_path(path, sizeof(path));
    f = fopen(path, "w");
    if (!f) return;
    for (int i = 0; i < g_console_history_count; i++) {
        fprintf(f, "%s\n", g_console_history[i]);
    }
    fclose(f);
}

static void console_history_load(void) {
    char path[MAX_PATH];
    FILE* f;
    char line[CONSOLE_INPUT_BUF];
    if (g_console_history_loaded) return;
    g_console_history_loaded = 1;
    g_console_history_count = 0;
    console_history_path(path, sizeof(path));
    f = fopen(path, "r");
    if (!f) return;
    while (fgets(line, sizeof(line), f)) {
        console_strip_crlf(line);
        if (!line[0]) continue;
        if (g_console_history_count >= CONSOLE_HISTORY_MAX) break;
        safe_copy(g_console_history[g_console_history_count], sizeof(g_console_history[0]), line);
        g_console_history_count++;
    }
    fclose(f);
}

static void console_history_add(const char* cmd) {
    int i;
    if (!cmd || !cmd[0]) return;

    if (g_console_history_count > 0) {
        const char* last = g_console_history[g_console_history_count - 1];
        if (_stricmp(last, cmd) == 0) return;
    }

    if (g_console_history_count >= CONSOLE_HISTORY_MAX) {
        for (i = 1; i < CONSOLE_HISTORY_MAX; i++) {
            safe_copy(g_console_history[i - 1], sizeof(g_console_history[0]), g_console_history[i]);
        }
        g_console_history_count = CONSOLE_HISTORY_MAX - 1;
    }

    safe_copy(g_console_history[g_console_history_count], sizeof(g_console_history[0]), cmd);
    g_console_history_count++;
    console_history_save();
}

static int console_common_prefix_len(const char* a, const char* b) {
    int n = 0;
    if (!a || !b) return 0;
    while (a[n] && b[n] && tolower((unsigned char)a[n]) == tolower((unsigned char)b[n])) n++;
    return n;
}

// =====================================================================
// Context-aware tab completion + reverse history search
// =====================================================================

#define CONSOLE_COMPL_MAX  64
#define CONSOLE_COMPL_ITEM 96

static char g_compl_items[CONSOLE_COMPL_MAX][CONSOLE_COMPL_ITEM];
static int  g_compl_count = 0;
static int  g_compl_index = -1;        // applied candidate while cycling
static int  g_compl_active = 0;        // a Tab cycle is in progress
static int  g_compl_token_start = -1;  // offset where the completed token starts

static int  g_console_search_active = 0;
static char g_console_search_query[CONSOLE_INPUT_BUF];
static char g_console_search_saved[CONSOLE_INPUT_BUF];
static int  g_console_search_index = -1; // history index of the current match

static void console_compl_reset(void) {
    g_compl_active = 0;
    g_compl_index = -1;
    g_compl_count = 0;
    g_compl_token_start = -1;
}

static void console_compl_add(const char* s) {
    if (!s || !s[0]) return;
    if (g_compl_count >= CONSOLE_COMPL_MAX) return;
    for (int i = 0; i < g_compl_count; i++) {
        if (_stricmp(g_compl_items[i], s) == 0) return;
    }
    safe_copy(g_compl_items[g_compl_count], CONSOLE_COMPL_ITEM, s);
    g_compl_count++;
}

static void console_compl_add_pref(const char* s, const char* prefix, int plen) {
    if (!s) return;
    if (plen > 0 && _strnicmp(s, prefix, (size_t)plen) != 0) return;
    console_compl_add(s);
}

// Copy the Nth whitespace-delimited token of the full input line into out.
static void console_input_token(int index, char* out, size_t outsz) {
    const char* s = g_console_input;
    int i = 0, t = 0;
    out[0] = '\0';
    while (s[i]) {
        while (s[i] && isspace((unsigned char)s[i])) i++;
        if (!s[i]) break;
        int start = i;
        while (s[i] && !isspace((unsigned char)s[i])) i++;
        if (t == index) {
            int len = i - start;
            if (len > (int)outsz - 1) len = (int)outsz - 1;
            memcpy(out, s + start, (size_t)len);
            out[len] = '\0';
            return;
        }
        t++;
    }
}

// Locate the token under the cursor. Returns its arg index (0 = command),
// and fills cmd (token 0), prefix (token text up to the cursor), *tok_start.
static int console_compl_context(char* cmd, size_t cmdsz, char* prefix, size_t prefixsz, int* tok_start) {
    const char* s = g_console_input;
    int cur = g_console_cursor;
    int i, in_tok = 0, start = 0, token_no = -1;

    if (cur < 0) cur = 0;
    console_input_token(0, cmd, cmdsz);

    for (i = 0; i < cur; i++) {
        if (!isspace((unsigned char)s[i])) {
            if (!in_tok) { in_tok = 1; start = i; token_no++; }
        } else {
            in_tok = 0;
        }
    }

    int cur_start, arg_index;
    if (in_tok) { cur_start = start; arg_index = token_no; }
    else        { cur_start = cur;   arg_index = token_no + 1; }

    int plen = cur - cur_start;
    if (plen < 0) plen = 0;
    if (plen > (int)prefixsz - 1) plen = (int)prefixsz - 1;
    memcpy(prefix, s + cur_start, (size_t)plen);
    prefix[plen] = '\0';
    *tok_start = cur_start;
    return arg_index;
}

static void console_compl_mod_ids(const char* prefix, int plen, int allow_all) {
    if (allow_all) console_compl_add_pref("all", prefix, plen);
    int n = lua_manager_get_mod_count();
    for (int i = 0; i < n; i++) {
        const char* id = lua_manager_get_mod_id(i);
        if (id && id[0]) console_compl_add_pref(id, prefix, plen);
    }
}

static void console_compl_cfg_keys(const char* mod_id, const char* prefix, int plen) {
    int idx = console_find_mod_index_by_id(mod_id);
    if (idx < 0) return;
    int n = lua_manager_get_mod_config_count(idx);
    for (int i = 0; i < n; i++) {
        const char* k = lua_manager_get_mod_config_key(idx, i);
        if (k && k[0]) console_compl_add_pref(k, prefix, plen);
    }
}

static void console_compl_bind_keys(const char* mod_id, const char* prefix, int plen) {
    int idx = console_find_mod_index_by_id(mod_id);
    if (idx < 0) return;
    int n = lua_manager_get_mod_bind_count(idx);
    for (int i = 0; i < n; i++) {
        const char* k = lua_manager_get_mod_bind_key(idx, i);
        if (k && k[0]) console_compl_add_pref(k, prefix, plen);
    }
}

// Complete the value of `mods.config.set <id> <key> <value>` for bool/options.
static void console_compl_cfg_values(const char* mod_id, const char* key, const char* prefix, int plen) {
    int idx = console_find_mod_index_by_id(mod_id);
    if (idx < 0) return;
    int ci = console_find_cfg_index_by_key(idx, key);
    if (ci < 0) return;
    int type = lua_manager_get_mod_config_type(idx, ci);
    if (type == LUA_CFG_BOOL) {
        static const char* k[] = { "true", "false", "on", "off" };
        for (int i = 0; i < 4; i++) console_compl_add_pref(k[i], prefix, plen);
    } else if (type == LUA_CFG_ENUM) {
        int n = lua_manager_get_mod_config_option_count(idx, ci);
        for (int i = 0; i < n; i++) {
            console_compl_add_pref(lua_manager_get_mod_config_option(idx, ci, i), prefix, plen);
        }
    }
}

static void console_compl_keywords(const char* const* kws, int n, const char* prefix, int plen) {
    for (int i = 0; i < n; i++) console_compl_add_pref(kws[i], prefix, plen);
}

static int console_cmd_eq(const char* cmd, const char* a) { return _stricmp(cmd, a) == 0; }

static void console_build_candidates(const char* cmd, int arg_index, const char* prefix) {
    int plen = (int)strlen(prefix);
    char tok1[CONSOLE_COMPL_ITEM];
    char tok2[CONSOLE_COMPL_ITEM];

    g_compl_count = 0;

    if (arg_index == 0) {
        for (int i = 0; i < (int)(sizeof(k_console_commands) / sizeof(k_console_commands[0])); i++)
            console_compl_add_pref(k_console_commands[i], prefix, plen);
        return;
    }

    console_input_token(1, tok1, sizeof(tok1));
    console_input_token(2, tok2, sizeof(tok2));

    if (console_cmd_eq(cmd,"mods.enable") || console_cmd_eq(cmd,"mods.disable") || console_cmd_eq(cmd,"mods.toggle")) {
        if (arg_index == 1) console_compl_mod_ids(prefix, plen, 1);
    } else if (console_cmd_eq(cmd,"mods.info") || console_cmd_eq(cmd,"mods.config") || console_cmd_eq(cmd,"mods.cfg") ||
               console_cmd_eq(cmd,"binds.list") || console_cmd_eq(cmd,"lua.mod") || console_cmd_eq(cmd,"eval.mod")) {
        if (arg_index == 1) console_compl_mod_ids(prefix, plen, 0);
    } else if (console_cmd_eq(cmd,"mods.trace")) {
        if (arg_index == 1) console_compl_mod_ids(prefix, plen, 1);
        else if (arg_index == 2) { static const char* k[] = {"on","off"}; console_compl_keywords(k,2,prefix,plen); }
    } else if (console_cmd_eq(cmd,"mods.config.get") || console_cmd_eq(cmd,"mods.cfg.get") ||
               console_cmd_eq(cmd,"mods.config.action") || console_cmd_eq(cmd,"mods.cfg.action")) {
        if (arg_index == 1) console_compl_mod_ids(prefix, plen, 0);
        else if (arg_index == 2) console_compl_cfg_keys(tok1, prefix, plen);
    } else if (console_cmd_eq(cmd,"mods.config.set") || console_cmd_eq(cmd,"mods.cfg.set")) {
        if (arg_index == 1) console_compl_mod_ids(prefix, plen, 0);
        else if (arg_index == 2) console_compl_cfg_keys(tok1, prefix, plen);
        else if (arg_index == 3) console_compl_cfg_values(tok1, tok2, prefix, plen);
    } else if (console_cmd_eq(cmd,"binds.set") || console_cmd_eq(cmd,"binds.clear")) {
        if (arg_index == 1) console_compl_mod_ids(prefix, plen, 0);
        else if (arg_index == 2) console_compl_bind_keys(tok1, prefix, plen);
    } else if (console_cmd_eq(cmd,"log.level")) {
        static const char* k[] = {"debug","info","warn","error"}; if (arg_index == 1) console_compl_keywords(k,4,prefix,plen);
    } else if (console_cmd_eq(cmd,"console.copy")) {
        static const char* k[] = {"output","input"}; if (arg_index == 1) console_compl_keywords(k,2,prefix,plen);
    } else if (console_cmd_eq(cmd,"state.switch")) {
        static const char* k[] = {"main","main_initial","options","options_paused","mods","mods_entry","online","console","return"};
        if (arg_index == 1) console_compl_keywords(k,9,prefix,plen);
    } else if (console_cmd_eq(cmd,"state.return")) {
        static const char* k[] = {"main","main_initial","options","options_paused","mods","mods_entry"};
        if (arg_index == 1) console_compl_keywords(k,6,prefix,plen);
    } else if (console_cmd_eq(cmd,"time.scale")) {
        static const char* k[] = {"auto"}; if (arg_index == 1) console_compl_keywords(k,1,prefix,plen);
    } else if (console_cmd_eq(cmd,"ggpo.local")) {
        static const char* k[] = {"toggle","on","off","status"}; if (arg_index == 1) console_compl_keywords(k,4,prefix,plen);
    } else if (console_cmd_eq(cmd,"ggpo.net")) {
        if (arg_index == 1) {
            static const char* k[] = {"host","join","delay","advantage","predict","highping","smoothping","correction","sim","status","off"};
            console_compl_keywords(k,11,prefix,plen);
        } else if (arg_index == 2 && console_cmd_eq(tok1,"correction")) {
            static const char* k[] = {"on","off"}; console_compl_keywords(k,2,prefix,plen);
        }
    }
}

// Replace the token at tok_start (extending to its end) with `replacement`.
static void console_compl_apply(int tok_start, const char* replacement, int add_space) {
    char buf[CONSOLE_INPUT_BUF];
    const char* s = g_console_input;
    int token_end = tok_start;
    while (s[token_end] && !isspace((unsigned char)s[token_end])) token_end++;
    int has_tail = (s[token_end] != '\0');
    snprintf(buf, sizeof(buf), "%.*s%s%s%s",
             tok_start, s, replacement,
             (add_space && !has_tail) ? " " : "",
             s + token_end);
    int new_cursor = tok_start + (int)strlen(replacement) + ((add_space && !has_tail) ? 1 : 0);
    safe_copy(g_console_input, sizeof(g_console_input), buf);
    if (new_cursor > (int)strlen(g_console_input)) new_cursor = (int)strlen(g_console_input);
    g_console_cursor = new_cursor;
}

static void console_autocomplete(int reverse) {
    char cmd[CONSOLE_COMPL_ITEM];
    char prefix[128];
    int tok_start = 0;
    int arg_index;

    if (g_console_search_active) return;

    // Repeated Tab cycles through an already-built candidate list.
    if (g_compl_active && g_compl_count > 1) {
        if (reverse) g_compl_index = (g_compl_index - 1 + g_compl_count) % g_compl_count;
        else         g_compl_index = (g_compl_index + 1) % g_compl_count;
        console_compl_apply(g_compl_token_start, g_compl_items[g_compl_index], 0);
        return;
    }

    arg_index = console_compl_context(cmd, sizeof(cmd), prefix, sizeof(prefix), &tok_start);
    console_build_candidates(cmd, arg_index, prefix);
    g_compl_token_start = tok_start;

    if (g_compl_count == 0) { console_compl_reset(); return; }

    if (g_compl_count == 1) {
        console_compl_apply(tok_start, g_compl_items[0], 1);
        console_compl_reset();
        return;
    }

    // Multiple matches: extend to the common prefix, list them, arm cycling.
    int common = (int)strlen(g_compl_items[0]);
    for (int i = 1; i < g_compl_count; i++) {
        int c = console_common_prefix_len(g_compl_items[0], g_compl_items[i]);
        if (c < common) common = c;
    }
    if (common > (int)strlen(prefix)) {
        char partial[CONSOLE_COMPL_ITEM];
        if (common > CONSOLE_COMPL_ITEM - 1) common = CONSOLE_COMPL_ITEM - 1;
        memcpy(partial, g_compl_items[0], (size_t)common);
        partial[common] = '\0';
        console_compl_apply(tok_start, partial, 0);
    }
    console_push_line_rgb("Matches (Tab to cycle):", 0.62f, 0.86f, 1.00f);
    for (int i = 0; i < g_compl_count && i < CONSOLE_MAX_MATCHES; i++) {
        char out[CONSOLE_LINE_TEXT];
        snprintf(out, sizeof(out), "   %s", g_compl_items[i]);
        console_push_line_rgb(out, 0.78f, 0.84f, 0.94f);
    }
    if (g_compl_count > CONSOLE_MAX_MATCHES) {
        char out[64];
        snprintf(out, sizeof(out), "   ...and %d more", g_compl_count - CONSOLE_MAX_MATCHES);
        console_push_line_rgb(out, 0.56f, 0.62f, 0.74f);
    }
    g_compl_active = 1;
    g_compl_index = -1;
}

// ---- reverse history search (Ctrl+R) ----
static int console_search_find(int from_index) {
    if (g_console_search_query[0] == '\0') {
        return (from_index >= 0 && from_index < g_console_history_count) ? from_index : -1;
    }
    for (int i = from_index; i >= 0; i--) {
        if (i < g_console_history_count && console_stristr(g_console_history[i], g_console_search_query)) return i;
    }
    return -1;
}

static void console_search_requery(void) {
    g_console_search_index = console_search_find(g_console_history_count - 1);
}

static void console_search_begin(void) {
    g_console_search_active = 1;
    g_console_search_query[0] = '\0';
    safe_copy(g_console_search_saved, sizeof(g_console_search_saved), g_console_input);
    console_compl_reset();
    console_search_requery();
}

static void console_search_step_older(void) {
    int start = (g_console_search_index < 0) ? g_console_history_count - 1 : g_console_search_index - 1;
    int m = console_search_find(start);
    if (m >= 0) g_console_search_index = m;
}

static void console_search_type(const char* text) {
    size_t qlen;
    if (!text) return;
    qlen = strlen(g_console_search_query);
    for (const char* p = text; *p; p++) {
        if (qlen + 1 >= sizeof(g_console_search_query)) break;
        g_console_search_query[qlen++] = *p;
    }
    g_console_search_query[qlen] = '\0';
    console_search_requery();
}

static void console_search_backspace(void) {
    size_t qlen = strlen(g_console_search_query);
    if (qlen > 0) g_console_search_query[qlen - 1] = '\0';
    console_search_requery();
}

static void console_search_accept(void) {
    if (g_console_search_index >= 0 && g_console_search_index < g_console_history_count) {
        console_set_input(g_console_history[g_console_search_index]);
    }
    g_console_search_active = 0;
    g_console_search_query[0] = '\0';
    g_console_search_index = -1;
}

static void console_search_cancel(void) {
    console_set_input(g_console_search_saved);
    g_console_search_active = 0;
    g_console_search_query[0] = '\0';
    g_console_search_index = -1;
}

// ---- "did you mean?" for unknown commands ----
static int console_levenshtein(const char* a, const char* b) {
    int la = (int)strlen(a), lb = (int)strlen(b);
    int prev[40], cur[40];
    if (la > 39) la = 39;
    if (lb > 39) lb = 39;
    for (int j = 0; j <= lb; j++) prev[j] = j;
    for (int i = 1; i <= la; i++) {
        cur[0] = i;
        for (int j = 1; j <= lb; j++) {
            int cost = (tolower((unsigned char)a[i-1]) == tolower((unsigned char)b[j-1])) ? 0 : 1;
            int del = prev[j] + 1, ins = cur[j-1] + 1, sub = prev[j-1] + cost;
            int m = del < ins ? del : ins;
            if (sub < m) m = sub;
            cur[j] = m;
        }
        for (int j = 0; j <= lb; j++) prev[j] = cur[j];
    }
    return prev[lb];
}

static void console_suggest_commands(const char* cmd) {
    const char* best[3] = { NULL, NULL, NULL };
    int bestd[3] = { 99, 99, 99 };
    int clen = (int)strlen(cmd);
    int thresh = (clen <= 4) ? 2 : 3;

    for (int i = 0; i < (int)(sizeof(k_console_commands) / sizeof(k_console_commands[0])); i++) {
        const char* c = k_console_commands[i];
        int d = console_levenshtein(cmd, c);
        if (clen >= 2 && _strnicmp(c, cmd, (size_t)clen) == 0) d = 0; // favor prefix matches
        if (d > thresh) continue;
        for (int s = 0; s < 3; s++) {
            if (d < bestd[s]) {
                for (int t = 2; t > s; t--) { bestd[t] = bestd[t-1]; best[t] = best[t-1]; }
                bestd[s] = d; best[s] = c;
                break;
            }
        }
    }

    if (!best[0]) return;
    {
        char list[256] = "";
        char out[CONSOLE_LINE_TEXT];
        for (int s = 0; s < 3 && best[s]; s++) {
            if (s) strncat(list, ", ", sizeof(list) - strlen(list) - 1);
            strncat(list, best[s], sizeof(list) - strlen(list) - 1);
        }
        snprintf(out, sizeof(out), "Did you mean: %s?", list);
        console_push_line_rgb(out, 0.88f, 0.82f, 0.52f);
    }
}

// One-line usage hint shown in the footer while typing a known command.
static const char* console_usage_for(const char* cmd) {
    static const struct { const char* name; const char* usage; } u[] = {
        {"mods.enable","mods.enable <id|all>"}, {"mods.disable","mods.disable <id|all>"},
        {"mods.toggle","mods.toggle <id|all>"}, {"mods.info","mods.info <id>"},
        {"mods.trace","mods.trace <id|all> [on|off]"}, {"mods.find","mods.find <text>"},
        {"mods.config","mods.config <id>"}, {"mods.config.get","mods.config.get <id> <key>"},
        {"mods.config.set","mods.config.set <id> <key> <value>"}, {"mods.config.action","mods.config.action <id> <key>"},
        {"binds.list","binds.list [id]"}, {"binds.find","binds.find <text>"},
        {"binds.set","binds.set <id> <key> <value>"}, {"binds.clear","binds.clear <id> <key>"},
        {"log.level","log.level [debug|info|warn|error]"}, {"log.tail","log.tail [lines]"},
        {"state.switch","state.switch <name>"}, {"state.return","state.return [name]"},
        {"time.scale","time.scale [value|auto]"}, {"ggpo.net","ggpo.net <host|join|delay|...|status|off>"},
        {"echo","echo <text>"}, {"lua","lua <code>"}, {"lua.mod","lua.mod <id> <code>"},
        {"lua.file","lua.file <path>"}, {"input.override","input.override <player> <mask> [frames] [replace]"},
        {"input.clear","input.clear <player>"}, {"history","history [count]"},
    };
    if (!cmd || !cmd[0]) return NULL;
    for (int i = 0; i < (int)(sizeof(u) / sizeof(u[0])); i++) {
        if (_stricmp(cmd, u[i].name) == 0) return u[i].usage;
    }
    return NULL;
}

static void console_reset_history_nav(void) {
    g_console_history_pos = -1;
    g_console_has_edit_stash = 0;
    g_console_edit_stash[0] = '\0';
}

static void console_set_input(const char* s) {
    size_t len;
    safe_copy(g_console_input, sizeof(g_console_input), s ? s : "");
    len = strlen(g_console_input);
    g_console_cursor = (int)len;
    console_compl_reset();
}

static void console_detach_from_history(void) {
    if (g_console_history_pos == -1) return;
    g_console_history_pos = -1;
    g_console_has_edit_stash = 0;
    g_console_edit_stash[0] = '\0';
}

static void console_history_step(int dir) {
    if (g_console_history_count <= 0) return;

    if (dir < 0) {
        if (g_console_history_pos == -1) {
            safe_copy(g_console_edit_stash, sizeof(g_console_edit_stash), g_console_input);
            g_console_has_edit_stash = 1;
            g_console_history_pos = g_console_history_count - 1;
        } else if (g_console_history_pos > 0) {
            g_console_history_pos--;
        }
        console_set_input(g_console_history[g_console_history_pos]);
    } else {
        if (g_console_history_pos == -1) return;
        if (g_console_history_pos < g_console_history_count - 1) {
            g_console_history_pos++;
            console_set_input(g_console_history[g_console_history_pos]);
            return;
        }
        g_console_history_pos = -1;
        if (g_console_has_edit_stash) console_set_input(g_console_edit_stash);
        else console_set_input("");
        g_console_has_edit_stash = 0;
    }
}

static void console_insert_text(const char* text) {
    size_t len;
    size_t ins_len;
    if (!text || !text[0]) return;
    len = strlen(g_console_input);
    ins_len = strlen(text);
    if ((size_t)g_console_cursor > len) g_console_cursor = (int)len;
    if (ins_len > (sizeof(g_console_input) - 1) - len) {
        ins_len = (sizeof(g_console_input) - 1) - len;
    }
    if (ins_len <= 0) return;
    console_detach_from_history();
    console_compl_reset();
    memmove(g_console_input + g_console_cursor + ins_len,
            g_console_input + g_console_cursor,
            len - (size_t)g_console_cursor + 1);
    memcpy(g_console_input + g_console_cursor, text, ins_len);
    g_console_cursor += (int)ins_len;
}

static void console_backspace(void) {
    size_t len = strlen(g_console_input);
    if (g_console_cursor <= 0 || len <= 0) return;
    console_detach_from_history();
    console_compl_reset();
    memmove(g_console_input + g_console_cursor - 1,
            g_console_input + g_console_cursor,
            len - (size_t)g_console_cursor + 1);
    g_console_cursor--;
}

static void console_delete(void) {
    size_t len = strlen(g_console_input);
    if ((size_t)g_console_cursor >= len) return;
    console_detach_from_history();
    console_compl_reset();
    memmove(g_console_input + g_console_cursor,
            g_console_input + g_console_cursor + 1,
            len - (size_t)g_console_cursor);
}

static void console_scroll_by(int delta) {
    int max_scroll = g_console_line_count > 0 ? g_console_line_count - 1 : 0;
    g_console_scroll += delta;
    if (g_console_scroll < 0) g_console_scroll = 0;
    if (g_console_scroll > max_scroll) g_console_scroll = max_scroll;
}

static void console_downsample_rgba(const unsigned char* src, int sw, int sh, unsigned char* dst, int dw, int dh) {
    int y;
    int x;
    if (!src || !dst || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;

    for (y = 0; y < dh; y++) {
        int sy0 = (y * sh) / dh;
        int sy1 = ((y + 1) * sh) / dh;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        if (sy1 > sh) sy1 = sh;

        for (x = 0; x < dw; x++) {
            int sx0 = (x * sw) / dw;
            int sx1 = ((x + 1) * sw) / dw;
            uint64_t ar = 0, ag = 0, ab = 0, aa = 0, count = 0;
            int sy;
            int sx;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            if (sx1 > sw) sx1 = sw;

            for (sy = sy0; sy < sy1; sy++) {
                const unsigned char* row = src + ((size_t)sy * (size_t)sw * 4);
                for (sx = sx0; sx < sx1; sx++) {
                    const unsigned char* p = row + ((size_t)sx * 4);
                    ar += p[0];
                    ag += p[1];
                    ab += p[2];
                    aa += p[3];
                    count++;
                }
            }

            if (count == 0) count = 1;
            dst[((size_t)y * (size_t)dw + (size_t)x) * 4 + 0] = (unsigned char)(ar / count);
            dst[((size_t)y * (size_t)dw + (size_t)x) * 4 + 1] = (unsigned char)(ag / count);
            dst[((size_t)y * (size_t)dw + (size_t)x) * 4 + 2] = (unsigned char)(ab / count);
            dst[((size_t)y * (size_t)dw + (size_t)x) * 4 + 3] = (unsigned char)(aa / count);
        }
    }
}

static void console_box_blur_rgba(const unsigned char* src, unsigned char* dst, int w, int h, int radius) {
    int y;
    int x;
    if (!src || !dst || w <= 0 || h <= 0 || radius <= 0) return;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            uint64_t ar = 0, ag = 0, ab = 0, aa = 0, count = 0;
            int ky;
            for (ky = -radius; ky <= radius; ky++) {
                int sy = y + ky;
                int kx;
                if (sy < 0) sy = 0;
                if (sy >= h) sy = h - 1;
                for (kx = -radius; kx <= radius; kx++) {
                    int sx = x + kx;
                    const unsigned char* p;
                    if (sx < 0) sx = 0;
                    if (sx >= w) sx = w - 1;
                    p = src + ((size_t)sy * (size_t)w + (size_t)sx) * 4;
                    ar += p[0];
                    ag += p[1];
                    ab += p[2];
                    aa += p[3];
                    count++;
                }
            }
            if (count == 0) count = 1;
            dst[((size_t)y * (size_t)w + (size_t)x) * 4 + 0] = (unsigned char)(ar / count);
            dst[((size_t)y * (size_t)w + (size_t)x) * 4 + 1] = (unsigned char)(ag / count);
            dst[((size_t)y * (size_t)w + (size_t)x) * 4 + 2] = (unsigned char)(ab / count);
            dst[((size_t)y * (size_t)w + (size_t)x) * 4 + 3] = (unsigned char)(aa / count);
        }
    }
}

static int console_capture_background_now(void) {
    int w = (int)(p_mad_w ? p_mad_w() : BASE_UI_W);
    int h = (int)(p_mad_h ? p_mad_h() : BASE_UI_H);
    int bw;
    int bh;
    GLint prev_pack_alignment = 4;
    GLint prev_read_buffer = GL_BACK;
    GLint prev_tex_binding_2d = 0;
    unsigned char* src;
    unsigned char* downsampled;
    unsigned char* blur_tmp;

    if (w < 2 || h < 2) return 0;
    bw = w / CONSOLE_BG_DOWNSAMPLE;
    bh = h / CONSOLE_BG_DOWNSAMPLE;
    if (bw < 16) bw = 16;
    if (bh < 16) bh = 16;

    src = (unsigned char*)malloc((size_t)w * (size_t)h * 4);
    downsampled = (unsigned char*)malloc((size_t)bw * (size_t)bh * 4);
    blur_tmp = (unsigned char*)malloc((size_t)bw * (size_t)bh * 4);
    if (!src || !downsampled || !blur_tmp) {
        free(src);
        free(downsampled);
        free(blur_tmp);
        return 0;
    }

    glGetIntegerv(GL_PACK_ALIGNMENT, &prev_pack_alignment);
    glGetIntegerv(GL_READ_BUFFER, &prev_read_buffer);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex_binding_2d);

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, src);

    console_downsample_rgba(src, w, h, downsampled, bw, bh);
    console_box_blur_rgba(downsampled, blur_tmp, bw, bh, 1);
    console_box_blur_rgba(blur_tmp, downsampled, bw, bh, 1);

    if (!g_console_bg_tex) {
        glGenTextures(1, &g_console_bg_tex);
    }
    glBindTexture(GL_TEXTURE_2D, g_console_bg_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
#ifdef GL_CLAMP_TO_EDGE
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
#else
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
#endif
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bw, bh, 0, GL_RGBA, GL_UNSIGNED_BYTE, downsampled);

    g_console_bg_w = bw;
    g_console_bg_h = bh;
    g_console_bg_ready = 1;

    glPixelStorei(GL_PACK_ALIGNMENT, prev_pack_alignment);
    glReadBuffer(prev_read_buffer);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prev_tex_binding_2d);

    free(src);
    free(downsampled);
    free(blur_tmp);
    return 1;
}

static void console_open(void) {
    void* cur;
    if (!p_state_switch) return;
    if (is_console_state_active()) return;
    if (g_console_open_pending || g_console_open_ready) return;

    cur = p_state_current ? p_state_current() : NULL;
    if (!cur || cur == (void*)&g_console_state) {
        cur = (void*)(uintptr_t)ADDR_MAIN_STATE;
    } else if (cur == (void*)&g_mods_entry_state) {
        cur = (void*)(uintptr_t)ADDR_OPTIONS_STATE;
    }

    g_console_pending_return_state = cur;
    g_console_open_pending = 1;
    g_console_open_ready = 0;
    g_console_suppress_next_textinput = 1;
}

static void console_close(void) {
    void* target = g_console_return_state;
    g_console_open_pending = 0;
    g_console_open_ready = 0;
    if (!p_state_switch) return;
    if (!target || target == (void*)&g_console_state) {
        target = (void*)(uintptr_t)ADDR_MAIN_STATE;
    }
    p_state_switch(target);
}

static int console_find_mod_index_by_id(const char* id) {
    int count = lua_manager_get_mod_count();
    if (!id || !id[0]) return -1;
    for (int i = 0; i < count; i++) {
        const char* mid = lua_manager_get_mod_id(i);
        if (mid && _stricmp(mid, id) == 0) return i;
    }
    return -1;
}

static const char* console_cfg_type_name(int type) {
    switch (type) {
        case LUA_CFG_BOOL: return "bool";
        case LUA_CFG_INT: return "int";
        case LUA_CFG_FLOAT: return "float";
        case LUA_CFG_STRING: return "string";
        case LUA_CFG_ACTION: return "action";
        case LUA_CFG_ENUM: return "options";
        default: return "unknown";
    }
}

static const char* console_stristr(const char* haystack, const char* needle) {
    const char* h;
    size_t nlen;
    if (!haystack || !needle) return NULL;
    if (!needle[0]) return haystack;
    nlen = strlen(needle);
    for (h = haystack; *h; h++) {
        size_t i;
        for (i = 0; i < nlen; i++) {
            unsigned char hc = (unsigned char)h[i];
            unsigned char nc = (unsigned char)needle[i];
            if (!hc) break;
            if (tolower(hc) != tolower(nc)) break;
        }
        if (i == nlen) return h;
    }
    return NULL;
}

static char* console_parse_token(char** inout_cursor) {
    char* s;
    char* tok;
    char quote;
    if (!inout_cursor || !*inout_cursor) return NULL;
    s = trim_ws(*inout_cursor);
    if (!s || !s[0]) {
        *inout_cursor = s;
        return NULL;
    }

    if (*s == '"' || *s == '\'') {
        quote = *s;
        s++;
        tok = s;
        while (*s && *s != quote) s++;
        if (*s == quote) {
            *s = '\0';
            s++;
        }
        *inout_cursor = s;
        return tok;
    }

    tok = s;
    while (*s && !isspace((unsigned char)*s)) s++;
    if (*s) {
        *s = '\0';
        s++;
    }
    *inout_cursor = s;
    return tok;
}

static int console_try_parse_long(const char* s, long* out_value) {
    char* end = NULL;
    long v;
    if (!s) return 0;
    while (*s && isspace((unsigned char)*s)) s++;
    if (!s[0]) return 0;
    v = strtol(s, &end, 0);
    if (end == s) return 0;
    while (*end && isspace((unsigned char)*end)) end++;
    if (*end) return 0;
    if (out_value) *out_value = v;
    return 1;
}

static int console_try_parse_double(const char* s, double* out_value) {
    char* end = NULL;
    double v;
    if (!s) return 0;
    while (*s && isspace((unsigned char)*s)) s++;
    if (!s[0]) return 0;
    v = strtod(s, &end);
    if (end == s) return 0;
    while (*end && isspace((unsigned char)*end)) end++;
    if (*end) return 0;
    if (out_value) *out_value = v;
    return 1;
}

static int console_try_parse_bool(const char* s, int* out_value) {
    if (!s || !s[0]) return 0;
    if (str_bool_true(s)) {
        if (out_value) *out_value = 1;
        return 1;
    }
    if (str_bool_false(s)) {
        if (out_value) *out_value = 0;
        return 1;
    }
    return 0;
}

static void console_strip_crlf(char* s) {
    size_t len;
    if (!s) return;
    len = strlen(s);
    while (len > 0 && (s[len - 1] == '\r' || s[len - 1] == '\n')) {
        s[len - 1] = '\0';
        len--;
    }
}

static void console_show_help(const char* topic) {
    char topic_buf[CONSOLE_INPUT_BUF];
    const char* t = "";
    if (topic && topic[0]) {
        safe_copy(topic_buf, sizeof(topic_buf), topic);
        t = trim_ws(topic_buf);
    }
    if (!t || !t[0]) {
        console_push_line_rgb("Commands:", 0.72f, 0.90f, 1.00f);
        console_push_line_rgb("  help [topic]", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  commands", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  clear", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  history [count]", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  echo <text>", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  console.stats", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  console.copy [output|input]", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  state", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  state.last", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  state.return [main|main_initial|options|options_paused|mods|mods_entry]", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  state.switch <main|main_initial|options|options_paused|mods|mods_entry|online|console|return>", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  sys.info", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  ui.size", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  time.scale [value|auto]", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  framework.api", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  mods.count", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  mods.list", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  mods.find <text>", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  mods.info <id>", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  mods.trace <id|all> [on|off]", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  mods.enable <id|all>", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  mods.disable <id|all>", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  mods.toggle <id|all>", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  mods.config <id>", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  mods.config.find <text>", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  mods.config.get <id> <key>", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  mods.config.set <id> <key> <value>", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  mods.config.action <id> <key>", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  binds.list [id]", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  binds.find <text>", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  binds.set <id> <key> <value>", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  binds.clear <id> <key>", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  reload.mods", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  mods.reload", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  reload.assets", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  online.hub", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  ggpo.roundtrip", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  ggpo.selftest [frames]", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  ggpo.local [toggle|on|off|status]", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  ggpo.net <host|join|off|status|delay|advantage|predict|highping|smoothping|correction|sim>", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  log.level [debug|info|warn|error]", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  log.tail [lines]", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  input.show [player]", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  input.override <player> <mask> [frames] [replace]", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  input.clear <player>", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  lua <code>", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  eval <code>", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  lua.mod <id> <code>", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  eval.mod <id> <code>", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  lua.file <path>", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  exit", 0.87f, 0.87f, 0.87f);
        return;
    }

    if (_stricmp(t, "mods.config.set") == 0) {
        console_push_line_rgb("mods.config.set <id> <key> <value>: set bool/int/float/string config by key.", 0.72f, 0.90f, 1.00f);
        return;
    }
    if (_stricmp(t, "console.copy") == 0) {
        console_push_line_rgb("console.copy [output|input]: copy console output or current input to clipboard.", 0.72f, 0.90f, 1.00f);
        console_push_line_rgb("Shortcuts: Ctrl+C copies input or output, Ctrl+V/right-click pastes.", 0.72f, 0.90f, 1.00f);
        return;
    }
    if (_stricmp(t, "mods.config.action") == 0) {
        console_push_line_rgb("mods.config.action <id> <key>: trigger an action config entry.", 0.72f, 0.90f, 1.00f);
        return;
    }
    if (_stricmp(t, "mods.info") == 0) {
        console_push_line_rgb("mods.info <id>: show runtime diagnostics, perf counters, config-count, deps, and conflicts.", 0.72f, 0.90f, 1.00f);
        return;
    }
    if (_stricmp(t, "mods.trace") == 0) {
        console_push_line_rgb("mods.trace <id|all> [on|off]: show or toggle per-mod event tracing in modframework.log.", 0.72f, 0.90f, 1.00f);
        return;
    }
    if (_stricmp(t, "binds.set") == 0) {
        console_push_line_rgb("binds.set <id> <key> <value>: set a named mod bind (example values: space, a, left, escape).", 0.72f, 0.90f, 1.00f);
        return;
    }
    if (_stricmp(t, "mods.toggle") == 0) {
        console_push_line_rgb("mods.toggle <id|all>: invert enabled state.", 0.72f, 0.90f, 1.00f);
        return;
    }
    if (_stricmp(t, "mods.config.find") == 0) {
        console_push_line_rgb("mods.config.find <text>: search mod config keys/labels/values.", 0.72f, 0.90f, 1.00f);
        return;
    }
    if (_stricmp(t, "log.level") == 0) {
        console_push_line_rgb("log.level <debug|info|warn|error>: set framework log threshold.", 0.72f, 0.90f, 1.00f);
        return;
    }
    if (_stricmp(t, "log.tail") == 0) {
        console_push_line_rgb("log.tail [lines]: show last lines from mods/modframework.log.", 0.72f, 0.90f, 1.00f);
        return;
    }
    if (_stricmp(t, "time.scale") == 0) {
        console_push_line_rgb("time.scale: show current timescale.", 0.72f, 0.90f, 1.00f);
        console_push_line_rgb("time.scale <value>: set manual timescale (0.05..100).", 0.72f, 0.90f, 1.00f);
        console_push_line_rgb("time.scale auto: clear manual override.", 0.72f, 0.90f, 1.00f);
        return;
    }
    if (_stricmp(t, "reload.assets") == 0) {
        console_push_line_rgb("reload.assets: reload tracked texture/font overlays and rebuild atlases.", 0.72f, 0.90f, 1.00f);
        return;
    }
    if (_stricmp(t, "ggpo.selftest") == 0) {
        console_push_line_rgb("ggpo.selftest [frames]: invasive hidden-frame replay test. Prefer F3 for live rollback testing.", 0.72f, 0.90f, 1.00f);
        return;
    }
    if (_stricmp(t, "ggpo.roundtrip") == 0) {
        console_push_line_rgb("ggpo.roundtrip: non-invasive save/load/checksum callback test.", 0.72f, 0.90f, 1.00f);
        return;
    }
    if (_stricmp(t, "ggpo.local") == 0) {
        console_push_line_rgb("ggpo.local [toggle|on|off|status]: route gameplay through the GGPO-shaped local callback session.", 0.72f, 0.90f, 1.00f);
        return;
    }
    if (_stricmp(t, "ggpo.net") == 0) {
        console_push_line_rgb("ggpo.net host [port]: host a UDP rollback input session as player 0.", 0.72f, 0.90f, 1.00f);
        console_push_line_rgb("ggpo.net join <host> [port] [local_port]: join as player 1.", 0.72f, 0.90f, 1.00f);
        console_push_line_rgb("ggpo.net delay [frames]: show or set local input delay (0..8).", 0.72f, 0.90f, 1.00f);
        console_push_line_rgb("ggpo.net advantage [frames]: show or set frame-advantage throttle (0..220).", 0.72f, 0.90f, 1.00f);
        console_push_line_rgb("ggpo.net predict [frames]: show or set max prediction before stalling (1..220).", 0.72f, 0.90f, 1.00f);
        console_push_line_rgb("ggpo.net highping [frames]: conservative high-latency profile; favors shorter pauses over long prediction.", 0.72f, 0.90f, 1.00f);
        console_push_line_rgb("ggpo.net smoothping [frames]: aggressive high-latency profile; favors smoothness over correction risk.", 0.72f, 0.90f, 1.00f);
        console_push_line_rgb("ggpo.net correction [on|off]: recover desyncs with host-authoritative state.", 0.72f, 0.90f, 1.00f);
        console_push_line_rgb("ggpo.net sim [loss_pct] [min_delay] [max_delay]: simulate outgoing UDP loss/jitter.", 0.72f, 0.90f, 1.00f);
        console_push_line_rgb("ggpo.net sim off: clear simulated network loss and delay.", 0.72f, 0.90f, 1.00f);
        console_push_line_rgb("F6 hosts on 47777. F7 joins 127.0.0.1:47777.", 0.72f, 0.90f, 1.00f);
        return;
    }
    if (_stricmp(t, "online.hub") == 0 || _stricmp(t, "online") == 0) {
        console_push_line_rgb("online.hub: open the built-in online hub with Play, Friends, and Settings tabs.", 0.72f, 0.90f, 1.00f);
        console_push_line_rgb("Play uses server login plus casual/competitive queues; Friends handles requests and challenges.", 0.72f, 0.90f, 1.00f);
        return;
    }
    if (_stricmp(t, "reload.mods") == 0) {
        console_push_line_rgb("reload.mods: unload+reload all mods and refresh runtime.", 0.72f, 0.90f, 1.00f);
        return;
    }
    if (_stricmp(t, "input.override") == 0) {
        console_push_line_rgb("input.override <player> <mask> [frames] [replace]: force command bits.", 0.72f, 0.90f, 1.00f);
        console_push_line_rgb("mask supports decimal or hex (example: 0x10). frames<0 means persistent.", 0.72f, 0.90f, 1.00f);
        return;
    }
    if (_stricmp(t, "state.switch") == 0) {
        console_push_line_rgb("state.switch <name>: switch to main/main_initial/options/options_paused/mods/mods_entry/online/console/return.", 0.72f, 0.90f, 1.00f);
        return;
    }
    if (_stricmp(t, "state.return") == 0) {
        console_push_line_rgb("state.return [main|main_initial|options|options_paused|mods|mods_entry]: show/set return state.", 0.72f, 0.90f, 1.00f);
        return;
    }
    if (_stricmp(t, "lua") == 0) {
        console_push_line_rgb("lua <code>: execute Lua code in global runtime.", 0.72f, 0.90f, 1.00f);
        return;
    }
    if (_stricmp(t, "lua.mod") == 0) {
        console_push_line_rgb("lua.mod <id> <code>: execute Lua code in a mod environment.", 0.72f, 0.90f, 1.00f);
        return;
    }
    if (_stricmp(t, "lua.file") == 0) {
        console_push_line_rgb("lua.file <path>: execute a Lua file in global runtime.", 0.72f, 0.90f, 1.00f);
        return;
    }

    {
        char out[CONSOLE_LINE_TEXT];
        snprintf(out, sizeof(out), "No detailed help for topic: %s", t);
        console_push_line_rgb(out, 0.98f, 0.76f, 0.40f);
    }
}

static void console_show_history(const char* count_arg) {
    int limit = 20;
    int start;
    if (count_arg && count_arg[0]) {
        long parsed = 0;
        if (!console_try_parse_long(count_arg, &parsed) || parsed <= 0) {
            console_push_line_rgb("Usage: history [positive_count]", 0.98f, 0.76f, 0.40f);
            return;
        }
        if (parsed > CONSOLE_HISTORY_MAX) parsed = CONSOLE_HISTORY_MAX;
        limit = (int)parsed;
    }
    if (g_console_history_count <= 0) {
        console_push_line_rgb("(history empty)", 0.62f, 0.70f, 0.82f);
        return;
    }
    start = g_console_history_count > limit ? (g_console_history_count - limit) : 0;
    for (int i = start; i < g_console_history_count; i++) {
        char out[CONSOLE_LINE_TEXT];
        snprintf(out, sizeof(out), "%2d: %s", i + 1, g_console_history[i]);
        console_push_line_rgb(out, 0.80f, 0.83f, 0.90f);
    }
}

static void console_show_console_stats(void) {
    char out[CONSOLE_LINE_TEXT];
    snprintf(out, sizeof(out),
             "console.stats: lines=%d/%d history=%d/%d scroll=%d cursor=%d input_len=%d",
             g_console_line_count, CONSOLE_MAX_LINES,
             g_console_history_count, CONSOLE_HISTORY_MAX,
             g_console_scroll, g_console_cursor, (int)strlen(g_console_input));
    console_push_line_rgb(out, 0.72f, 0.90f, 1.00f);
}

static void console_echo(const char* text) {
    console_push_line_rgb((text && text[0]) ? text : "", 0.80f, 0.83f, 0.90f);
}

static void console_show_state(void) {
    void* cur = p_state_current ? p_state_current() : NULL;
    char out[CONSOLE_LINE_TEXT];
    snprintf(out, sizeof(out), "current=%s (%p) return=%s (%p)",
             state_name_from_ptr(cur), cur,
             state_name_from_ptr(g_console_return_state), g_console_return_state);
    console_push_line_rgb(out, 0.60f, 0.88f, 0.72f);
}

static void* console_state_ptr_from_name(const char* name, int allow_return_alias) {
    if (!name || !name[0]) return NULL;
    if (_stricmp(name, "main") == 0) return (void*)(uintptr_t)ADDR_MAIN_STATE;
    if (_stricmp(name, "main_initial") == 0) return (void*)(uintptr_t)ADDR_MAIN_STATE_INITIAL;
    if (_stricmp(name, "options") == 0) return (void*)(uintptr_t)ADDR_OPTIONS_STATE;
    if (_stricmp(name, "options_paused") == 0) return (void*)(uintptr_t)ADDR_OPTIONS_STATE_PAUSED;
    if (_stricmp(name, "mods") == 0) return (void*)&g_mods_state;
    if (_stricmp(name, "mods_entry") == 0) return (void*)&g_mods_entry_state;
    if (_stricmp(name, "online") == 0 || _stricmp(name, "online_hub") == 0) return (void*)&g_online_hub_state;
    if (_stricmp(name, "console") == 0) return (void*)&g_console_state;
    if (allow_return_alias && _stricmp(name, "return") == 0) return g_console_return_state;
    return NULL;
}

static void console_show_last_state(void) {
    void* st = p_state_last ? p_state_last() : NULL;
    char out[CONSOLE_LINE_TEXT];
    snprintf(out, sizeof(out), "last=%s (%p)", state_name_from_ptr(st), st);
    console_push_line_rgb(out, 0.60f, 0.88f, 0.72f);
}

static void console_show_return_state(void) {
    char out[CONSOLE_LINE_TEXT];
    snprintf(out, sizeof(out), "return=%s (%p)", state_name_from_ptr(g_console_return_state), g_console_return_state);
    console_push_line_rgb(out, 0.60f, 0.88f, 0.72f);
}

static void console_set_return_state(const char* state_arg) {
    char name_buf[CONSOLE_INPUT_BUF];
    char* name;
    void* target;
    char out[CONSOLE_LINE_TEXT];

    safe_copy(name_buf, sizeof(name_buf), state_arg ? state_arg : "");
    name = trim_ws(name_buf);
    if (!name || !name[0]) {
        console_show_return_state();
        return;
    }

    target = console_state_ptr_from_name(name, 0);
    if (!target || target == (void*)&g_console_state) {
        console_push_line_rgb("Usage: state.return [main|main_initial|options|options_paused|mods|mods_entry]", 0.98f, 0.76f, 0.40f);
        return;
    }

    g_console_return_state = target;
    snprintf(out, sizeof(out), "state.return -> %s", state_name_from_ptr(target));
    console_push_line_rgb(out, 0.64f, 0.92f, 0.66f);
}

static void console_switch_state(const char* state_arg) {
    char name_buf[CONSOLE_INPUT_BUF];
    char* name;
    void* target;
    safe_copy(name_buf, sizeof(name_buf), state_arg ? state_arg : "");
    name = trim_ws(name_buf);
    if (!name || !name[0]) {
        console_push_line_rgb("Usage: state.switch <main|main_initial|options|options_paused|mods|mods_entry|online|console|return>", 0.98f, 0.76f, 0.40f);
        return;
    }
    if (!p_state_switch) {
        console_push_line_rgb("state.switch unavailable: missing state switch pointer", 0.98f, 0.45f, 0.45f);
        return;
    }

    target = console_state_ptr_from_name(name, 1);
    if (!target) {
        console_push_line_rgb("Unknown state. Use: main/main_initial/options/options_paused/mods/mods_entry/online/console/return", 0.98f, 0.76f, 0.40f);
        return;
    }

    p_state_switch(target);
    {
        char out[CONSOLE_LINE_TEXT];
        snprintf(out, sizeof(out), "state.switch -> %s", state_name_from_ptr(target));
        console_push_line_rgb(out, 0.64f, 0.92f, 0.66f);
    }
}

static void console_show_ui_size(void) {
    float w = p_mad_w ? p_mad_w() : BASE_UI_W;
    float h = p_mad_h ? p_mad_h() : BASE_UI_H;
    float ui = calc_ui_scale();
    char out[CONSOLE_LINE_TEXT];
    snprintf(out, sizeof(out), "ui.size: %.1fx%.1f ui_scale=%.3f", w, h, ui);
    console_push_line_rgb(out, 0.72f, 0.90f, 1.00f);
}

static void console_show_system_info(void) {
    char out[CONSOLE_LINE_TEXT];
    int manual_ts = lua_manager_get_time_scale_manual(NULL);
    snprintf(out, sizeof(out), "framework.api=%d mods=%d state=%s time.scale=%.3f%s log=%s",
             lua_manager_framework_api(),
             lua_manager_get_mod_count(),
             state_name_from_ptr(p_state_current ? p_state_current() : NULL),
             (double)lua_manager_get_time_scale(),
             manual_ts ? "(manual)" : "",
             log_get_level_name());
    console_push_line_rgb(out, 0.72f, 0.90f, 1.00f);
    console_show_ui_size();
}

static void console_show_mods_count(void) {
    char out[CONSOLE_LINE_TEXT];
    snprintf(out, sizeof(out), "mods.count: %d", lua_manager_get_mod_count());
    console_push_line_rgb(out, 0.72f, 0.90f, 1.00f);
}

static void console_show_mods_list(void) {
    int count = lua_manager_get_mod_count();
    if (count <= 0) {
        console_push_line_rgb("No mods loaded.", 0.62f, 0.70f, 0.82f);
        return;
    }
    for (int i = 0; i < count; i++) {
        const char* id = lua_manager_get_mod_id(i);
        const char* name = lua_manager_get_mod_name(i);
        const char* ver = lua_manager_get_mod_version(i);
        int enabled = lua_manager_get_mod_enabled(i);
        int errors = lua_manager_get_mod_error_count(i);
        char out[CONSOLE_LINE_TEXT];
        snprintf(out, sizeof(out), "[%s] %s (%s) id=%s errors=%d",
                 enabled ? "on" : "off",
                 name ? name : "",
                 ver ? ver : "",
                 id ? id : "",
                 errors);
        console_push_line_rgb(out, enabled ? 0.64f : 0.72f, enabled ? 0.92f : 0.72f, enabled ? 0.66f : 0.72f);
    }
}

static void console_find_mods(const char* query) {
    int count = lua_manager_get_mod_count();
    int found = 0;
    if (!query || !query[0]) {
        console_push_line_rgb("Usage: mods.find <text>", 0.98f, 0.76f, 0.40f);
        return;
    }
    for (int i = 0; i < count; i++) {
        const char* id = lua_manager_get_mod_id(i);
        const char* name = lua_manager_get_mod_name(i);
        const char* ver = lua_manager_get_mod_version(i);
        if (console_stristr(id, query) || console_stristr(name, query) || console_stristr(ver, query)) {
            char out[CONSOLE_LINE_TEXT];
            snprintf(out, sizeof(out), "[%s] %s (%s) id=%s errors=%d",
                     lua_manager_get_mod_enabled(i) ? "on" : "off",
                     name ? name : "",
                     ver ? ver : "",
                     id ? id : "",
                     lua_manager_get_mod_error_count(i));
            console_push_line_rgb(out, 0.72f, 0.90f, 1.00f);
            found++;
        }
    }
    if (!found) {
        char out[CONSOLE_LINE_TEXT];
        snprintf(out, sizeof(out), "mods.find: no matches for '%s'", query);
        console_push_line_rgb(out, 0.98f, 0.76f, 0.40f);
    }
}

static void console_show_mod_info(const char* id) {
    int idx;
    char out[CONSOLE_LINE_TEXT];
    LuaModDiagnostics diag;
    char mem_buf[32];
    if (!id || !id[0]) {
        console_push_line_rgb("Usage: mods.info <id>", 0.98f, 0.76f, 0.40f);
        return;
    }

    idx = console_find_mod_index_by_id(id);
    if (idx < 0) {
        snprintf(out, sizeof(out), "Mod not found: %s", id);
        console_push_line_rgb(out, 0.98f, 0.45f, 0.45f);
        return;
    }

    snprintf(out, sizeof(out), "id=%s name=%s version=%s enabled=%s errors=%d cfg=%d binds=%d",
             lua_manager_get_mod_id(idx),
             lua_manager_get_mod_name(idx),
             lua_manager_get_mod_version(idx),
             lua_manager_get_mod_enabled(idx) ? "true" : "false",
             lua_manager_get_mod_error_count(idx),
             lua_manager_get_mod_config_count(idx),
             lua_manager_get_mod_bind_count(idx));
    console_push_line_rgb(out, 0.72f, 0.90f, 1.00f);
    memset(&diag, 0, sizeof(diag));
    if (lua_manager_get_mod_diagnostics(idx, &diag)) {
        format_bytes_compact(diag.approx_memory_bytes, mem_buf, sizeof(mem_buf));
        snprintf(out, sizeof(out), "trace.events=%s mem~%s handlers frame=%d event=%d layout=%d",
                 diag.trace_events ? "on" : "off",
                 mem_buf,
                 diag.on_frame_handlers,
                 diag.on_event_handlers,
                 diag.on_layout_handlers);
        console_push_line_rgb(out, 0.80f, 0.83f, 0.90f);
        snprintf(out, sizeof(out), "runtime storage=%d audio=%d font_regs=%d texture_regs=%d",
                 diag.storage_entries,
                 diag.audio_chunks,
                 diag.font_registrations,
                 diag.texture_registrations);
        console_push_line_rgb(out, 0.80f, 0.83f, 0.90f);
        snprintf(out, sizeof(out), "perf.frame calls=%u last=%.3f avg=%.3f max=%.3f ms",
                 diag.frame_calls, diag.frame_last_ms, diag.frame_avg_ms, diag.frame_max_ms);
        console_push_line_rgb(out, 0.80f, 0.83f, 0.90f);
        snprintf(out, sizeof(out), "perf.event calls=%u last=%.3f avg=%.3f max=%.3f ms",
                 diag.event_calls, diag.event_last_ms, diag.event_avg_ms, diag.event_max_ms);
        console_push_line_rgb(out, 0.80f, 0.83f, 0.90f);
        snprintf(out, sizeof(out), "perf.layout calls=%u last=%.3f avg=%.3f max=%.3f ms",
                 diag.layout_calls, diag.layout_last_ms, diag.layout_avg_ms, diag.layout_max_ms);
        console_push_line_rgb(out, 0.80f, 0.83f, 0.90f);
    }
    if (lua_manager_get_mod_author(idx)[0]) {
        snprintf(out, sizeof(out), "author=%s", lua_manager_get_mod_author(idx));
        console_push_line_rgb(out, 0.80f, 0.83f, 0.90f);
    }
    if (lua_manager_get_mod_description(idx)[0]) {
        console_push_line_rgb(lua_manager_get_mod_description(idx), 0.80f, 0.83f, 0.90f);
    }
    for (int di = 0; di < lua_manager_get_mod_dependency_count(idx); di++) {
        snprintf(out, sizeof(out), "dep %s%s%s",
                 lua_manager_get_mod_dependency_id(idx, di),
                 lua_manager_get_mod_dependency_optional(idx, di) ? " [optional]" : "",
                 lua_manager_mod_dependency_satisfied(idx, di) ? "" : " [missing]");
        console_push_line_rgb(out,
                              lua_manager_mod_dependency_satisfied(idx, di) ? 0.80f : 0.98f,
                              lua_manager_mod_dependency_satisfied(idx, di) ? 0.83f : 0.72f,
                              lua_manager_mod_dependency_satisfied(idx, di) ? 0.90f : 0.40f);
    }
    for (int ci = 0; ci < lua_manager_get_mod_conflict_count(idx); ci++) {
        snprintf(out, sizeof(out), "conflict %s%s",
                 lua_manager_get_mod_conflict_id(idx, ci),
                 lua_manager_mod_conflict_active(idx, ci) ? " [active]" : "");
        console_push_line_rgb(out,
                              lua_manager_mod_conflict_active(idx, ci) ? 0.98f : 0.80f,
                              lua_manager_mod_conflict_active(idx, ci) ? 0.72f : 0.83f,
                              lua_manager_mod_conflict_active(idx, ci) ? 0.40f : 0.90f);
    }
}

static void console_set_mod_trace(const char* args) {
    char args_buf[CONSOLE_INPUT_BUF];
    char out[CONSOLE_LINE_TEXT];
    char* cursor;
    char* id;
    char* value;
    int want = 0;

    if (!args || !args[0]) {
        console_push_line_rgb("Usage: mods.trace <id|all> [on|off]", 0.98f, 0.76f, 0.40f);
        return;
    }

    safe_copy(args_buf, sizeof(args_buf), args);
    cursor = args_buf;
    id = console_parse_token(&cursor);
    value = console_parse_token(&cursor);
    if (!id || !id[0]) {
        console_push_line_rgb("Usage: mods.trace <id|all> [on|off]", 0.98f, 0.76f, 0.40f);
        return;
    }

    if (_stricmp(id, "all") == 0) {
        int count = lua_manager_get_mod_count();
        int trace_on = 0;
        if (!value || !value[0]) {
            for (int i = 0; i < count; i++) {
                LuaModDiagnostics diag;
                memset(&diag, 0, sizeof(diag));
                if (lua_manager_get_mod_diagnostics(i, &diag) && diag.trace_events) trace_on++;
            }
            snprintf(out, sizeof(out), "mods.trace all: on=%d off=%d total=%d", trace_on, count - trace_on, count);
            console_push_line_rgb(out, 0.72f, 0.90f, 1.00f);
            return;
        }
        if (!console_try_parse_bool(value, &want)) {
            console_push_line_rgb("mods.trace: value must be on/off/true/false/1/0", 0.98f, 0.76f, 0.40f);
            return;
        }
        for (int i = 0; i < count; i++) {
            (void)lua_manager_set_mod_trace_events(i, want);
        }
        snprintf(out, sizeof(out), "mods.trace all: %s", want ? "on" : "off");
        console_push_line_rgb(out, 0.64f, 0.92f, 0.66f);
        return;
    }

    {
        int idx = console_find_mod_index_by_id(id);
        LuaModDiagnostics diag;
        if (idx < 0) {
            snprintf(out, sizeof(out), "Mod not found: %s", id);
            console_push_line_rgb(out, 0.98f, 0.45f, 0.45f);
            return;
        }
        if (!value || !value[0]) {
            memset(&diag, 0, sizeof(diag));
            (void)lua_manager_get_mod_diagnostics(idx, &diag);
            snprintf(out, sizeof(out), "mods.trace: %s -> %s",
                     lua_manager_get_mod_id(idx),
                     diag.trace_events ? "on" : "off");
            console_push_line_rgb(out, 0.72f, 0.90f, 1.00f);
            return;
        }
        if (!console_try_parse_bool(value, &want)) {
            console_push_line_rgb("mods.trace: value must be on/off/true/false/1/0", 0.98f, 0.76f, 0.40f);
            return;
        }
        if (!lua_manager_set_mod_trace_events(idx, want)) {
            snprintf(out, sizeof(out), "mods.trace failed: %s", lua_manager_get_mod_id(idx));
            console_push_line_rgb(out, 0.98f, 0.45f, 0.45f);
            return;
        }
        snprintf(out, sizeof(out), "mods.trace: %s -> %s", lua_manager_get_mod_id(idx), want ? "on" : "off");
        console_push_line_rgb(out, 0.64f, 0.92f, 0.66f);
    }
}

static void console_set_mod_enabled(const char* id, int enabled) {
    int idx;
    char out[CONSOLE_LINE_TEXT];
    if (!id || !id[0]) {
        console_push_line_rgb(enabled ? "Usage: mods.enable <id|all>" : "Usage: mods.disable <id|all>", 0.98f, 0.76f, 0.40f);
        return;
    }
    if (_stricmp(id, "all") == 0) {
        int count = lua_manager_get_mod_count();
        int before_on = 0;
        int after_on = 0;
        for (int i = 0; i < count; i++) {
            if (lua_manager_get_mod_enabled(i)) before_on++;
        }

        if (enabled) {
            int progress;
            do {
                progress = 0;
                for (int i = 0; i < count; i++) {
                    if (lua_manager_get_mod_enabled(i)) continue;
                    if (lua_manager_set_mod_enabled(i, 1)) progress++;
                }
            } while (progress > 0);
        } else {
            for (int i = 0; i < count; i++) {
                if (!lua_manager_get_mod_enabled(i)) continue;
                (void)lua_manager_set_mod_enabled(i, 0);
            }
        }

        for (int i = 0; i < count; i++) {
            if (lua_manager_get_mod_enabled(i)) after_on++;
        }

        snprintf(out, sizeof(out), "%s all: changed=%d total=%d",
                 enabled ? "mods.enable" : "mods.disable",
                 enabled ? (after_on - before_on) : (before_on - after_on),
                 count);
        console_push_line_rgb(out, 0.64f, 0.92f, 0.66f);
        return;
    }
    idx = console_find_mod_index_by_id(id);
    if (idx < 0) {
        snprintf(out, sizeof(out), "Mod not found: %s", id);
        console_push_line_rgb(out, 0.98f, 0.45f, 0.45f);
        return;
    }
    if (!lua_manager_set_mod_enabled(idx, enabled ? 1 : 0)) {
        snprintf(out, sizeof(out), "%s failed: %s",
                 enabled ? "mods.enable" : "mods.disable",
                 lua_manager_get_mod_id(idx));
        console_push_line_rgb(out, 0.98f, 0.45f, 0.45f);
        return;
    }
    snprintf(out, sizeof(out), "%s: %s",
             enabled ? "mods.enable" : "mods.disable",
             lua_manager_get_mod_id(idx));
    console_push_line_rgb(out, 0.64f, 0.92f, 0.66f);
}

static void console_toggle_mod_enabled(const char* id) {
    char out[CONSOLE_LINE_TEXT];
    if (!id || !id[0]) {
        console_push_line_rgb("Usage: mods.toggle <id|all>", 0.98f, 0.76f, 0.40f);
        return;
    }
    if (_stricmp(id, "all") == 0) {
        int count = lua_manager_get_mod_count();
        int on_count = 0;
        int off_count = 0;
        for (int i = 0; i < count; i++) {
            int new_enabled = lua_manager_get_mod_enabled(i) ? 0 : 1;
            if (lua_manager_set_mod_enabled(i, new_enabled)) {
                if (lua_manager_get_mod_enabled(i)) on_count++;
                else off_count++;
            }
        }
        snprintf(out, sizeof(out), "mods.toggle all: now_on=%d now_off=%d total=%d", on_count, off_count, count);
        console_push_line_rgb(out, 0.64f, 0.92f, 0.66f);
        return;
    }
    {
        int idx = console_find_mod_index_by_id(id);
        if (idx < 0) {
            snprintf(out, sizeof(out), "Mod not found: %s", id);
            console_push_line_rgb(out, 0.98f, 0.45f, 0.45f);
            return;
        }
        {
            int new_enabled = lua_manager_get_mod_enabled(idx) ? 0 : 1;
            if (!lua_manager_set_mod_enabled(idx, new_enabled)) {
                snprintf(out, sizeof(out), "mods.toggle failed: %s", lua_manager_get_mod_id(idx));
                console_push_line_rgb(out, 0.98f, 0.45f, 0.45f);
                return;
            }
            snprintf(out, sizeof(out), "mods.toggle: %s -> %s",
                     lua_manager_get_mod_id(idx),
                     lua_manager_get_mod_enabled(idx) ? "on" : "off");
            console_push_line_rgb(out, 0.64f, 0.92f, 0.66f);
        }
    }
}

static void console_show_mod_config(const char* id) {
    int idx;
    int cfg_count;
    if (!id || !id[0]) {
        console_push_line_rgb("Usage: mods.config <id>", 0.98f, 0.76f, 0.40f);
        return;
    }
    idx = console_find_mod_index_by_id(id);
    if (idx < 0) {
        char out[CONSOLE_LINE_TEXT];
        snprintf(out, sizeof(out), "Mod not found: %s", id);
        console_push_line_rgb(out, 0.98f, 0.45f, 0.45f);
        return;
    }

    cfg_count = lua_manager_get_mod_config_count(idx);
    {
        char out[CONSOLE_LINE_TEXT];
        snprintf(out, sizeof(out), "mods.config %s: %d entries", lua_manager_get_mod_id(idx), cfg_count);
        console_push_line_rgb(out, 0.72f, 0.90f, 1.00f);
    }
    if (cfg_count <= 0) return;

    for (int ci = 0; ci < cfg_count; ci++) {
        const char* key = lua_manager_get_mod_config_key(idx, ci);
        const char* val = lua_manager_get_mod_config_value_str(idx, ci);
        const char* label = lua_manager_get_mod_config_label(idx, ci);
        int type = lua_manager_get_mod_config_type(idx, ci);
        char out[CONSOLE_LINE_TEXT];
        if (label && label[0] && _stricmp(label, key) != 0) {
            snprintf(out, sizeof(out), "  %s (%s) = %s [%s]",
                     key ? key : "", console_cfg_type_name(type),
                     val ? val : "", label);
        } else {
            snprintf(out, sizeof(out), "  %s (%s) = %s",
                     key ? key : "", console_cfg_type_name(type),
                     val ? val : "");
        }
        console_push_line_rgb(out, 0.80f, 0.83f, 0.90f);
    }
}

static void console_find_mod_config(const char* query) {
    int match_count = 0;
    if (!query || !query[0]) {
        console_push_line_rgb("Usage: mods.config.find <text>", 0.98f, 0.76f, 0.40f);
        return;
    }

    for (int mi = 0; mi < lua_manager_get_mod_count(); mi++) {
        const char* mod_id = lua_manager_get_mod_id(mi);
        int cfg_count = lua_manager_get_mod_config_count(mi);
        for (int ci = 0; ci < cfg_count; ci++) {
            const char* key = lua_manager_get_mod_config_key(mi, ci);
            const char* label = lua_manager_get_mod_config_label(mi, ci);
            const char* value = lua_manager_get_mod_config_value_str(mi, ci);
            if (!console_stristr(mod_id, query) &&
                !console_stristr(key, query) &&
                !console_stristr(label, query) &&
                !console_stristr(value, query)) {
                continue;
            }
            {
                char out[CONSOLE_LINE_TEXT];
                snprintf(out, sizeof(out), "%s.%s (%s) = %s",
                         mod_id ? mod_id : "",
                         key ? key : "",
                         console_cfg_type_name(lua_manager_get_mod_config_type(mi, ci)),
                         value ? value : "");
                console_push_line_rgb(out, 0.80f, 0.83f, 0.90f);
            }
            match_count++;
        }
    }

    if (match_count == 0) {
        char out[CONSOLE_LINE_TEXT];
        snprintf(out, sizeof(out), "mods.config.find: no matches for '%s'", query);
        console_push_line_rgb(out, 0.98f, 0.76f, 0.40f);
    } else {
        char out[CONSOLE_LINE_TEXT];
        snprintf(out, sizeof(out), "mods.config.find: %d match(es)", match_count);
        console_push_line_rgb(out, 0.64f, 0.92f, 0.66f);
    }
}

static int console_find_cfg_index_by_key(int mod_index, const char* key) {
    return lua_manager_find_mod_config_index(mod_index, key);
}

static void console_show_mod_config_value(const char* id, const char* key) {
    int idx;
    int cfg_idx;
    char out[CONSOLE_LINE_TEXT];
    if (!id || !id[0] || !key || !key[0]) {
        console_push_line_rgb("Usage: mods.config.get <id> <key>", 0.98f, 0.76f, 0.40f);
        return;
    }
    idx = console_find_mod_index_by_id(id);
    if (idx < 0) {
        snprintf(out, sizeof(out), "Mod not found: %s", id);
        console_push_line_rgb(out, 0.98f, 0.45f, 0.45f);
        return;
    }
    cfg_idx = console_find_cfg_index_by_key(idx, key);
    if (cfg_idx < 0) {
        snprintf(out, sizeof(out), "Config key not found: %s", key);
        console_push_line_rgb(out, 0.98f, 0.45f, 0.45f);
        return;
    }

    snprintf(out, sizeof(out), "%s.%s (%s) = %s",
             lua_manager_get_mod_id(idx),
             lua_manager_get_mod_config_key(idx, cfg_idx),
             console_cfg_type_name(lua_manager_get_mod_config_type(idx, cfg_idx)),
             lua_manager_get_mod_config_value_str(idx, cfg_idx));
    console_push_line_rgb(out, 0.72f, 0.90f, 1.00f);
}

static void console_set_mod_config_value(const char* id, const char* key, const char* value) {
    int idx;
    int cfg_idx;
    int type;
    int ok = 0;
    char out[CONSOLE_LINE_TEXT];
    if (!id || !id[0] || !key || !key[0] || !value) {
        console_push_line_rgb("Usage: mods.config.set <id> <key> <value>", 0.98f, 0.76f, 0.40f);
        return;
    }
    idx = console_find_mod_index_by_id(id);
    if (idx < 0) {
        snprintf(out, sizeof(out), "Mod not found: %s", id);
        console_push_line_rgb(out, 0.98f, 0.45f, 0.45f);
        return;
    }
    cfg_idx = console_find_cfg_index_by_key(idx, key);
    if (cfg_idx < 0) {
        snprintf(out, sizeof(out), "Config key not found: %s", key);
        console_push_line_rgb(out, 0.98f, 0.45f, 0.45f);
        return;
    }

    type = lua_manager_get_mod_config_type(idx, cfg_idx);
    if (type == LUA_CFG_BOOL) {
        int want = 0;
        int cur = str_bool_true(lua_manager_get_mod_config_value_str(idx, cfg_idx)) ? 1 : 0;
        if (!console_try_parse_bool(value, &want)) {
            console_push_line_rgb("Bool value must be true/false/on/off/1/0", 0.98f, 0.76f, 0.40f);
            return;
        }
        ok = (want == cur) ? 1 : lua_manager_config_toggle_bool(idx, cfg_idx);
    } else if (type == LUA_CFG_INT) {
        long want = 0;
        long cur = 0;
        const char* cur_text = lua_manager_get_mod_config_value_str(idx, cfg_idx);
        if (!console_try_parse_long(value, &want)) {
            console_push_line_rgb("Int value is invalid", 0.98f, 0.76f, 0.40f);
            return;
        }
        if (!console_try_parse_long(cur_text, &cur)) cur = strtol(cur_text, NULL, 10);
        ok = lua_manager_config_increment_int(idx, cfg_idx, (int)(want - cur));
    } else if (type == LUA_CFG_FLOAT) {
        double want = 0.0;
        double cur = 0.0;
        const char* cur_text = lua_manager_get_mod_config_value_str(idx, cfg_idx);
        if (!console_try_parse_double(value, &want)) {
            console_push_line_rgb("Float value is invalid", 0.98f, 0.76f, 0.40f);
            return;
        }
        if (!console_try_parse_double(cur_text, &cur)) cur = atof(cur_text);
        ok = lua_manager_config_increment_float(idx, cfg_idx, want - cur);
    } else if (type == LUA_CFG_STRING) {
        ok = lua_manager_config_set_string(idx, cfg_idx, value);
    } else if (type == LUA_CFG_ENUM) {
        ok = lua_manager_config_set_option(idx, cfg_idx, value);
        if (!ok) {
            console_push_line_rgb("Value is not one of the declared options.", 0.98f, 0.76f, 0.40f);
            return;
        }
    } else if (type == LUA_CFG_ACTION) {
        console_push_line_rgb("Use mods.config.action for action config keys.", 0.98f, 0.76f, 0.40f);
        return;
    } else {
        console_push_line_rgb("Unsupported config type", 0.98f, 0.76f, 0.40f);
        return;
    }

    if (!ok) {
        console_push_line_rgb("Config update failed (see modframework.log)", 0.98f, 0.45f, 0.45f);
        return;
    }

    snprintf(out, sizeof(out), "%s.%s = %s",
             lua_manager_get_mod_id(idx),
             lua_manager_get_mod_config_key(idx, cfg_idx),
             lua_manager_get_mod_config_value_str(idx, cfg_idx));
    console_push_line_rgb(out, 0.64f, 0.92f, 0.66f);
}

static void console_trigger_mod_config_action(const char* id, const char* key) {
    int idx;
    int cfg_idx;
    int type;
    char out[CONSOLE_LINE_TEXT];
    if (!id || !id[0] || !key || !key[0]) {
        console_push_line_rgb("Usage: mods.config.action <id> <key>", 0.98f, 0.76f, 0.40f);
        return;
    }
    idx = console_find_mod_index_by_id(id);
    if (idx < 0) {
        snprintf(out, sizeof(out), "Mod not found: %s", id);
        console_push_line_rgb(out, 0.98f, 0.45f, 0.45f);
        return;
    }
    cfg_idx = console_find_cfg_index_by_key(idx, key);
    if (cfg_idx < 0) {
        snprintf(out, sizeof(out), "Config key not found: %s", key);
        console_push_line_rgb(out, 0.98f, 0.45f, 0.45f);
        return;
    }

    type = lua_manager_get_mod_config_type(idx, cfg_idx);
    if (type != LUA_CFG_ACTION) {
        console_push_line_rgb("Config key is not an action.", 0.98f, 0.76f, 0.40f);
        return;
    }

    lua_manager_config_trigger_action(idx, cfg_idx);
    snprintf(out, sizeof(out), "Triggered action: %s.%s", lua_manager_get_mod_id(idx), key);
    console_push_line_rgb(out, 0.64f, 0.92f, 0.66f);
}

static void console_run_reload_mods(void) {
    int ok = lua_manager_reload_mods();
    if (ok) console_push_line_rgb("reload.mods: success", 0.64f, 0.92f, 0.66f);
    else console_push_line_rgb("reload.mods: failed (see modframework.log)", 0.98f, 0.45f, 0.45f);
}

static void console_run_reload_assets(void) {
    int tex_reloaded = 0;
    int tex_failed = 0;
    int tex_restart = 0;
    int font_reloaded = 0;
    int font_failed = 0;
    int font_restart = 0;
    int ok = lua_manager_reload_assets(
        &tex_reloaded, &tex_failed, &tex_restart,
        &font_reloaded, &font_failed, &font_restart
    );

    {
        char out[CONSOLE_LINE_TEXT];
        snprintf(out, sizeof(out),
                 "reload.assets: ok=%s tex=%d fail=%d restart=%d font=%d fail=%d restart=%d",
                 ok ? "true" : "false",
                 tex_reloaded, tex_failed, tex_restart,
                 font_reloaded, font_failed, font_restart);
        console_push_line_rgb(out, ok ? 0.64f : 0.98f, ok ? 0.92f : 0.45f, ok ? 0.66f : 0.45f);
    }
}

static void run_ggpo_roundtrip_check(const char* source) {
    char out[CONSOLE_LINE_TEXT];
    char err[256];
    size_t state_size = ggpo_ext_game_state_size();
    uint8_t* state_blob = NULL;
    size_t state_len = 0;
    uint32_t saved_checksum = 0;
    uint32_t restored_checksum = 0;

    if (ggpo_loopback_active()) {
        snprintf(out, sizeof(out), "ggpo.roundtrip: skipped from %s (loopback active; press F3 to disable first)",
                 (source && source[0]) ? source : "unknown");
        console_push_line_rgb(out, 0.98f, 0.76f, 0.40f);
        LOG_WARN("%s", out);
        return;
    }
    if (ggpo_local_active()) {
        snprintf(out, sizeof(out), "ggpo.roundtrip: skipped from %s (local session active; press F4 to disable first)",
                 (source && source[0]) ? source : "unknown");
        console_push_line_rgb(out, 0.98f, 0.76f, 0.40f);
        LOG_WARN("%s", out);
        return;
    }
    if (ggpo_net_active()) {
        snprintf(out, sizeof(out), "ggpo.roundtrip: skipped from %s (net session active; stop ggpo.net first)",
                 (source && source[0]) ? source : "unknown");
        console_push_line_rgb(out, 0.98f, 0.76f, 0.40f);
        LOG_WARN("%s", out);
        return;
    }
    if (g_ggpo_selftest.active || g_ggpo_selftest_pending) {
        snprintf(out, sizeof(out), "ggpo.roundtrip: skipped from %s (selftest already running)",
                 (source && source[0]) ? source : "unknown");
        console_push_line_rgb(out, 0.98f, 0.76f, 0.40f);
        LOG_WARN("%s", out);
        return;
    }
    if (state_size == 0) {
        snprintf(out, sizeof(out), "ggpo.roundtrip: failed from %s (game state unavailable)",
                 (source && source[0]) ? source : "unknown");
        console_push_line_rgb(out, 0.98f, 0.45f, 0.45f);
        LOG_ERROR("%s", out);
        return;
    }

    state_blob = (uint8_t*)malloc(state_size);
    if (!state_blob) {
        snprintf(out, sizeof(out), "ggpo.roundtrip: failed from %s (out of memory)",
                 (source && source[0]) ? source : "unknown");
        console_push_line_rgb(out, 0.98f, 0.45f, 0.45f);
        LOG_ERROR("%s", out);
        return;
    }

    err[0] = '\0';
    if (!ggpo_ext_save_game_state(state_blob, state_size, &state_len, &saved_checksum, err, sizeof(err))) {
        snprintf(out, sizeof(out), "ggpo.roundtrip: failed from %s (save failed: %s)",
                 (source && source[0]) ? source : "unknown",
                 err[0] ? err : "unknown error");
        console_push_line_rgb(out, 0.98f, 0.45f, 0.45f);
        LOG_ERROR("%s", out);
        free(state_blob);
        return;
    }
    if (!ggpo_ext_load_game_state(state_blob, state_len, err, sizeof(err))) {
        snprintf(out, sizeof(out), "ggpo.roundtrip: failed from %s (load failed: %s)",
                 (source && source[0]) ? source : "unknown",
                 err[0] ? err : "unknown error");
        console_push_line_rgb(out, 0.98f, 0.45f, 0.45f);
        LOG_ERROR("%s", out);
        free(state_blob);
        return;
    }
    if (!lua_manager_game_state_rollback_checksum(&restored_checksum, err, sizeof(err))) {
        snprintf(out, sizeof(out), "ggpo.roundtrip: failed from %s (checksum failed: %s)",
                 (source && source[0]) ? source : "unknown",
                 err[0] ? err : "unknown error");
        console_push_line_rgb(out, 0.98f, 0.45f, 0.45f);
        LOG_ERROR("%s", out);
        free(state_blob);
        return;
    }
    if (restored_checksum != saved_checksum) {
        snprintf(out, sizeof(out), "ggpo.roundtrip: failed from %s (expected=%u got=%u)",
                 (source && source[0]) ? source : "unknown",
                 (unsigned int)saved_checksum,
                 (unsigned int)restored_checksum);
        console_push_line_rgb(out, 0.98f, 0.45f, 0.45f);
        LOG_ERROR("%s", out);
        free(state_blob);
        return;
    }

    snprintf(out,
             sizeof(out),
             "ggpo.roundtrip: ok from %s state_size=%u checksum=%u",
             (source && source[0]) ? source : "unknown",
             (unsigned int)state_len,
             (unsigned int)saved_checksum);
    console_push_line_rgb(out, 0.64f, 0.92f, 0.66f);
    LOG_INFO("%s", out);
    free(state_blob);
}

static void queue_ggpo_selftest(int frames, const char* source) {
    char out[CONSOLE_LINE_TEXT];
    if (ggpo_loopback_active()) {
        snprintf(out, sizeof(out), "ggpo.selftest: skipped from %s (loopback active; press F3 to disable first)",
                 (source && source[0]) ? source : "unknown");
        console_push_line_rgb(out, 0.98f, 0.76f, 0.40f);
        LOG_WARN("%s", out);
        return;
    }
    if (ggpo_local_active()) {
        snprintf(out, sizeof(out), "ggpo.selftest: skipped from %s (local session active; press F4 to disable first)",
                 (source && source[0]) ? source : "unknown");
        console_push_line_rgb(out, 0.98f, 0.76f, 0.40f);
        LOG_WARN("%s", out);
        return;
    }
    if (ggpo_net_active()) {
        snprintf(out, sizeof(out), "ggpo.selftest: skipped from %s (net session active; stop ggpo.net first)",
                 (source && source[0]) ? source : "unknown");
        console_push_line_rgb(out, 0.98f, 0.76f, 0.40f);
        LOG_WARN("%s", out);
        return;
    }
    if (g_ggpo_selftest.active || g_ggpo_selftest_pending) {
        snprintf(out, sizeof(out), "ggpo.selftest: skipped from %s (selftest already running)",
                 (source && source[0]) ? source : "unknown");
        console_push_line_rgb(out, 0.98f, 0.76f, 0.40f);
        LOG_WARN("%s", out);
        return;
    }
    if (frames <= 0) frames = 120;
    g_ggpo_selftest_frames = frames;
    g_ggpo_selftest_pending = 1;
    snprintf(out, sizeof(out), "ggpo.selftest: queued from %s (frames=%d)",
             (source && source[0]) ? source : "unknown", frames);
    console_push_line_rgb(out, 0.64f, 0.92f, 0.66f);
    LOG_INFO("%s", out);
}

static void toggle_ggpo_loopback(const char* source) {
    char out[CONSOLE_LINE_TEXT];
    char err[256];

    if (ggpo_loopback_active()) {
        uint32_t frames = ggpo_loopback_frame_count();
        uint32_t checksum = ggpo_loopback_last_checksum();
        uint32_t verify_count = ggpo_loopback_verify_count();
        uint32_t verify_failures = ggpo_loopback_verify_failure_count();
        ggpo_loopback_stop();
        snprintf(out, sizeof(out), "ggpo.loopback: disabled from %s after %u frame(s), last_checksum=%u verifies=%u verify_failures=%u",
                 (source && source[0]) ? source : "unknown",
                 (unsigned int)frames,
                 (unsigned int)checksum,
                 (unsigned int)verify_count,
                 (unsigned int)verify_failures);
        console_push_line_rgb(out, 0.64f, 0.92f, 0.66f);
        LOG_INFO("%s", out);
        return;
    }

    if (g_ggpo_selftest_pending || g_ggpo_selftest.active) {
        snprintf(out, sizeof(out), "ggpo.loopback: skipped from %s (selftest pending)",
                 (source && source[0]) ? source : "unknown");
        console_push_line_rgb(out, 0.98f, 0.76f, 0.40f);
        LOG_WARN("%s", out);
        return;
    }
    if (ggpo_local_active()) {
        snprintf(out, sizeof(out), "ggpo.loopback: skipped from %s (local session active; press F4 to disable first)",
                 (source && source[0]) ? source : "unknown");
        console_push_line_rgb(out, 0.98f, 0.76f, 0.40f);
        LOG_WARN("%s", out);
        return;
    }
    if (ggpo_net_active()) {
        snprintf(out, sizeof(out), "ggpo.loopback: skipped from %s (net session active; stop ggpo.net first)",
                 (source && source[0]) ? source : "unknown");
        console_push_line_rgb(out, 0.98f, 0.76f, 0.40f);
        LOG_WARN("%s", out);
        return;
    }

    err[0] = '\0';
    if (!ggpo_loopback_start(err, sizeof(err))) {
        snprintf(out, sizeof(out), "ggpo.loopback: failed to enable from %s (%s)",
                 (source && source[0]) ? source : "unknown",
                 err[0] ? err : "unknown error");
        console_push_line_rgb(out, 0.98f, 0.45f, 0.45f);
        LOG_ERROR("%s", out);
        return;
    }

    snprintf(out, sizeof(out), "ggpo.loopback: enabled from %s state_size=%u history=%d verify_interval=%d verify_distance=%d",
             (source && source[0]) ? source : "unknown",
             (unsigned int)ggpo_loopback_state_size(),
             ggpo_loopback_history_capacity(),
             ggpo_loopback_verify_interval(),
             ggpo_loopback_verify_distance());
    console_push_line_rgb(out, 0.64f, 0.92f, 0.66f);
    LOG_INFO("%s", out);
}

static void print_ggpo_local_status(void) {
    char out[CONSOLE_LINE_TEXT];
    if (ggpo_local_active()) {
        snprintf(out,
                 sizeof(out),
                 "ggpo.local: active frames=%u last_checksum=%u state_size=%u",
                 (unsigned int)ggpo_local_frame_count(),
                 (unsigned int)ggpo_local_last_checksum(),
                 (unsigned int)ggpo_local_state_size());
    } else {
        snprintf(out, sizeof(out), "ggpo.local: inactive");
    }
    console_push_line_rgb(out, 0.72f, 0.90f, 1.00f);
    LOG_INFO("%s", out);
}

static void toggle_ggpo_local(const char* source) {
    char out[CONSOLE_LINE_TEXT];
    char err[256];

    if (ggpo_local_active()) {
        uint32_t frames = ggpo_local_frame_count();
        uint32_t checksum = ggpo_local_last_checksum();
        ggpo_local_stop();
        snprintf(out,
                 sizeof(out),
                 "ggpo.local: disabled from %s after %u frame(s), last_checksum=%u",
                 (source && source[0]) ? source : "unknown",
                 (unsigned int)frames,
                 (unsigned int)checksum);
        console_push_line_rgb(out, 0.64f, 0.92f, 0.66f);
        LOG_INFO("%s", out);
        return;
    }

    if (ggpo_loopback_active()) {
        snprintf(out, sizeof(out), "ggpo.local: skipped from %s (loopback active; press F3 to disable first)",
                 (source && source[0]) ? source : "unknown");
        console_push_line_rgb(out, 0.98f, 0.76f, 0.40f);
        LOG_WARN("%s", out);
        return;
    }
    if (g_ggpo_selftest_pending || g_ggpo_selftest.active) {
        snprintf(out, sizeof(out), "ggpo.local: skipped from %s (selftest pending)",
                 (source && source[0]) ? source : "unknown");
        console_push_line_rgb(out, 0.98f, 0.76f, 0.40f);
        LOG_WARN("%s", out);
        return;
    }
    if (ggpo_net_active()) {
        snprintf(out, sizeof(out), "ggpo.local: skipped from %s (net session active; stop ggpo.net first)",
                 (source && source[0]) ? source : "unknown");
        console_push_line_rgb(out, 0.98f, 0.76f, 0.40f);
        LOG_WARN("%s", out);
        return;
    }

    err[0] = '\0';
    if (!ggpo_local_start(err, sizeof(err))) {
        snprintf(out, sizeof(out), "ggpo.local: failed to enable from %s (%s)",
                 (source && source[0]) ? source : "unknown",
                 err[0] ? err : "unknown error");
        console_push_line_rgb(out, 0.98f, 0.45f, 0.45f);
        LOG_ERROR("%s", out);
        return;
    }

    snprintf(out,
             sizeof(out),
             "ggpo.local: enabled from %s state_size=%u callbacks=save/load/advance",
             (source && source[0]) ? source : "unknown",
             (unsigned int)ggpo_local_state_size());
    console_push_line_rgb(out, 0.64f, 0.92f, 0.66f);
    LOG_INFO("%s", out);
}

static void console_run_ggpo_local(const char* arg) {
    char arg_buf[64];
    const char* t = "";

    if (arg && arg[0]) {
        safe_copy(arg_buf, sizeof(arg_buf), arg);
        t = trim_ws(arg_buf);
    }

    if (!t || !t[0] || _stricmp(t, "toggle") == 0) {
        toggle_ggpo_local("console");
        console_close();
        return;
    }
    if (_stricmp(t, "status") == 0) {
        print_ggpo_local_status();
        return;
    }
    if (_stricmp(t, "on") == 0 || _stricmp(t, "enable") == 0 || _stricmp(t, "start") == 0) {
        if (!ggpo_local_active()) toggle_ggpo_local("console");
        else print_ggpo_local_status();
        console_close();
        return;
    }
    if (_stricmp(t, "off") == 0 || _stricmp(t, "disable") == 0 || _stricmp(t, "stop") == 0) {
        if (ggpo_local_active()) toggle_ggpo_local("console");
        else print_ggpo_local_status();
        console_close();
        return;
    }

    console_push_line_rgb("Usage: ggpo.local [toggle|on|off|status]", 0.98f, 0.76f, 0.40f);
}

static void print_ggpo_net_status(void) {
    char out[CONSOLE_LINE_TEXT];
    if (ggpo_net_active()) {
        snprintf(out,
                 sizeof(out),
                 "ggpo.net: %s connected=%d frame=%u remote_frame=%u lp=%d rp=%d delay=%u adv=%u predcap=%u corr=%s active=%d wait=%d port=%u peer_port=%u checksum=%u state=%u",
                 ggpo_net_mode_name(),
                 ggpo_net_connected(),
                 (unsigned int)ggpo_net_frame_count(),
                 (unsigned int)ggpo_net_remote_frame_count(),
                 ggpo_net_local_player(),
                 ggpo_net_remote_player(),
                 (unsigned int)ggpo_net_input_delay(),
                 (unsigned int)ggpo_net_max_frame_advantage(),
                 (unsigned int)ggpo_net_max_prediction(),
                 ggpo_net_correction_enabled() ? "on" : "off",
                 ggpo_net_correction_active(),
                 ggpo_net_awaiting_correction(),
                 (unsigned int)ggpo_net_local_port(),
                 (unsigned int)ggpo_net_remote_port(),
                 (unsigned int)ggpo_net_last_checksum(),
                 (unsigned int)ggpo_net_state_size());
        console_push_line_rgb(out, 0.72f, 0.90f, 1.00f);
        LOG_INFO("%s", out);
        snprintf(out,
                 sizeof(out),
                 "ggpo.net.stats: tx=%u rx=%u pred=%u rb=%u late=%u drop=%u adv_stall=%u pred_stall=%u ts_stall=%u silence=%u desync=%u corr_tx=%u corr_rx=%u corr_req=%u stale_req=%u dup_chunk=%u corr_id=%u applied_id=%u",
                 (unsigned int)ggpo_net_packets_sent(),
                 (unsigned int)ggpo_net_packets_received(),
                 (unsigned int)ggpo_net_prediction_count(),
                 (unsigned int)ggpo_net_rollback_count(),
                 (unsigned int)ggpo_net_late_input_count(),
                 (unsigned int)ggpo_net_dropped_input_count(),
                 (unsigned int)ggpo_net_frame_advantage_stall_count(),
                 (unsigned int)ggpo_net_prediction_stall_count(),
                 (unsigned int)ggpo_net_timesync_stall_count(),
                 (unsigned int)ggpo_net_peer_silence_ticks(),
                 (unsigned int)ggpo_net_desync_count(),
                 (unsigned int)ggpo_net_corrections_sent(),
                 (unsigned int)ggpo_net_corrections_received(),
                 (unsigned int)ggpo_net_correction_request_count(),
                 (unsigned int)ggpo_net_stale_correction_request_count(),
                 (unsigned int)ggpo_net_duplicate_state_chunk_count(),
                 (unsigned int)ggpo_net_correction_id(),
                 (unsigned int)ggpo_net_last_correction_applied_id());
    } else {
        snprintf(out, sizeof(out), "ggpo.net: inactive");
    }
    console_push_line_rgb(out, 0.72f, 0.90f, 1.00f);
    LOG_INFO("%s", out);
    if (ggpo_net_active()) {
        snprintf(out,
                 sizeof(out),
                 "ggpo.net.sim: loss=%u%% delay=%u-%u pending=%u sim_drop=%u sim_delay=%u qdrop=%u",
                 (unsigned int)ggpo_net_sim_loss_percent(),
                 (unsigned int)ggpo_net_sim_delay_min_ticks(),
                 (unsigned int)ggpo_net_sim_delay_max_ticks(),
                 (unsigned int)ggpo_net_sim_pending_packets(),
                 (unsigned int)ggpo_net_sim_dropped_packets(),
                 (unsigned int)ggpo_net_sim_delayed_packets(),
                 (unsigned int)ggpo_net_sim_queue_drop_count());
        console_push_line_rgb(out, 0.72f, 0.90f, 1.00f);
        LOG_INFO("%s", out);
        snprintf(out,
                 sizeof(out),
                 "ggpo.net.build: local=%08X exe=%08X dll=%08X remote=%08X exe=%08X dll=%08X mismatch=%d",
                 (unsigned int)ggpo_net_local_build_id(),
                 (unsigned int)ggpo_net_local_exe_id(),
                 (unsigned int)ggpo_net_local_dll_id(),
                 (unsigned int)ggpo_net_remote_build_id(),
                 (unsigned int)ggpo_net_remote_exe_id(),
                 (unsigned int)ggpo_net_remote_dll_id(),
                 ggpo_net_build_mismatch());
        console_push_line_rgb(out, ggpo_net_build_mismatch() ? 0.98f : 0.72f, ggpo_net_build_mismatch() ? 0.76f : 0.90f, ggpo_net_build_mismatch() ? 0.40f : 1.00f);
        LOG_INFO("%s", out);
    }
    if (ggpo_net_active() && ggpo_net_desync_count() > 0) {
        snprintf(out,
                 sizeof(out),
                 "ggpo.net: last_desync frame=%u local=%u remote=%u",
                 (unsigned int)ggpo_net_desync_frame(),
                 (unsigned int)ggpo_net_desync_local_checksum(),
                 (unsigned int)ggpo_net_desync_remote_checksum());
        console_push_line_rgb(out, 0.98f, 0.45f, 0.45f);
        LOG_INFO("%s", out);
    }
}

static void stop_ggpo_net(const char* source) {
    char out[CONSOLE_LINE_TEXT];
    uint32_t frames = ggpo_net_frame_count();
    uint32_t checksum = ggpo_net_last_checksum();
    uint32_t tx = ggpo_net_packets_sent();
    uint32_t rx = ggpo_net_packets_received();
    uint32_t pred = ggpo_net_prediction_count();
    uint32_t rb = ggpo_net_rollback_count();
    uint32_t desync = ggpo_net_desync_count();
    uint32_t stalls = ggpo_net_frame_advantage_stall_count();
    uint32_t pred_stalls = ggpo_net_prediction_stall_count();
    uint32_t ts_stalls = ggpo_net_timesync_stall_count();
    if (!ggpo_net_active()) {
        print_ggpo_net_status();
        return;
    }
    ggpo_net_stop();
    snprintf(out,
             sizeof(out),
             "ggpo.net: stopped from %s after %u frame(s), checksum=%u tx=%u rx=%u pred=%u rb=%u adv_stall=%u pred_stall=%u ts_stall=%u desync=%u",
             (source && source[0]) ? source : "unknown",
             (unsigned int)frames,
             (unsigned int)checksum,
             (unsigned int)tx,
             (unsigned int)rx,
             (unsigned int)pred,
             (unsigned int)rb,
             (unsigned int)stalls,
             (unsigned int)pred_stalls,
             (unsigned int)ts_stalls,
             (unsigned int)desync);
    console_push_line_rgb(out, 0.64f, 0.92f, 0.66f);
    LOG_INFO("%s", out);
}

static int can_start_ggpo_net(const char* source) {
    char out[CONSOLE_LINE_TEXT];
    if (ggpo_loopback_active()) {
        snprintf(out, sizeof(out), "ggpo.net: skipped from %s (loopback active; press F3 to disable first)",
                 (source && source[0]) ? source : "unknown");
        console_push_line_rgb(out, 0.98f, 0.76f, 0.40f);
        LOG_WARN("%s", out);
        return 0;
    }
    if (ggpo_local_active()) {
        snprintf(out, sizeof(out), "ggpo.net: skipped from %s (local session active; press F4 to disable first)",
                 (source && source[0]) ? source : "unknown");
        console_push_line_rgb(out, 0.98f, 0.76f, 0.40f);
        LOG_WARN("%s", out);
        return 0;
    }
    if (g_ggpo_selftest_pending || g_ggpo_selftest.active) {
        snprintf(out, sizeof(out), "ggpo.net: skipped from %s (selftest pending)",
                 (source && source[0]) ? source : "unknown");
        console_push_line_rgb(out, 0.98f, 0.76f, 0.40f);
        LOG_WARN("%s", out);
        return 0;
    }
    return 1;
}

static void start_ggpo_net_host(uint16_t port, const char* source) {
    char out[CONSOLE_LINE_TEXT];
    char err[256];

    if (ggpo_net_active()) {
        stop_ggpo_net(source);
        return;
    }
    if (!can_start_ggpo_net(source)) return;

    err[0] = '\0';
    if (!ggpo_net_start_host(port, err, sizeof(err))) {
        snprintf(out, sizeof(out), "ggpo.net: host failed from %s (%s)",
                 (source && source[0]) ? source : "unknown",
                 err[0] ? err : "unknown error");
        console_push_line_rgb(out, 0.98f, 0.45f, 0.45f);
        LOG_ERROR("%s", out);
        return;
    }

    snprintf(out,
             sizeof(out),
             "ggpo.net: hosting from %s udp=%u player=0 delay=%u adv=%u predcap=%u corr=%s state_size=%u",
             (source && source[0]) ? source : "unknown",
             (unsigned int)ggpo_net_local_port(),
             (unsigned int)ggpo_net_input_delay(),
             (unsigned int)ggpo_net_max_frame_advantage(),
             (unsigned int)ggpo_net_max_prediction(),
             ggpo_net_correction_enabled() ? "on" : "off",
             (unsigned int)ggpo_net_state_size());
    console_push_line_rgb(out, 0.64f, 0.92f, 0.66f);
    LOG_INFO("%s", out);
}

static void start_ggpo_net_join(const char* host, uint16_t remote_port, uint16_t local_port, const char* source) {
    char out[CONSOLE_LINE_TEXT];
    char err[256];

    if (ggpo_net_active()) {
        stop_ggpo_net(source);
        return;
    }
    if (!can_start_ggpo_net(source)) return;

    err[0] = '\0';
    if (!ggpo_net_start_join(host, remote_port, local_port, err, sizeof(err))) {
        snprintf(out, sizeof(out), "ggpo.net: join failed from %s (%s)",
                 (source && source[0]) ? source : "unknown",
                 err[0] ? err : "unknown error");
        console_push_line_rgb(out, 0.98f, 0.45f, 0.45f);
        LOG_ERROR("%s", out);
        return;
    }

    snprintf(out,
             sizeof(out),
             "ggpo.net: joining from %s %s:%u local_udp=%u player=1 delay=%u adv=%u predcap=%u corr=%s state_size=%u",
             (source && source[0]) ? source : "unknown",
             host ? host : "",
             (unsigned int)remote_port,
             (unsigned int)ggpo_net_local_port(),
             (unsigned int)ggpo_net_input_delay(),
             (unsigned int)ggpo_net_max_frame_advantage(),
             (unsigned int)ggpo_net_max_prediction(),
             ggpo_net_correction_enabled() ? "on" : "off",
             (unsigned int)ggpo_net_state_size());
    console_push_line_rgb(out, 0.64f, 0.92f, 0.66f);
    LOG_INFO("%s", out);
}

static void start_ggpo_net_join_deferred(uint16_t local_port, const char* source) {
    char out[CONSOLE_LINE_TEXT];
    char err[256];

    if (ggpo_net_active()) {
        stop_ggpo_net(source);
        return;
    }
    if (!can_start_ggpo_net(source)) return;

    err[0] = '\0';
    if (!ggpo_net_start_join_deferred(local_port, err, sizeof(err))) {
        snprintf(out, sizeof(out), "ggpo.net: deferred join failed from %s (%s)",
                 (source && source[0]) ? source : "unknown",
                 err[0] ? err : "unknown error");
        console_push_line_rgb(out, 0.98f, 0.45f, 0.45f);
        LOG_ERROR("%s", out);
        return;
    }

    snprintf(out,
             sizeof(out),
             "ggpo.net: joining from %s waiting for peer endpoint local_udp=%u player=1 delay=%u adv=%u predcap=%u corr=%s state_size=%u",
             (source && source[0]) ? source : "unknown",
             (unsigned int)ggpo_net_local_port(),
             (unsigned int)ggpo_net_input_delay(),
             (unsigned int)ggpo_net_max_frame_advantage(),
             (unsigned int)ggpo_net_max_prediction(),
             ggpo_net_correction_enabled() ? "on" : "off",
             (unsigned int)ggpo_net_state_size());
    console_push_line_rgb(out, 0.64f, 0.92f, 0.66f);
    LOG_INFO("%s", out);
}

static int parse_port_token(const char* tok, uint16_t* out_port) {
    long port = 0;
    if (!tok || !tok[0]) return 0;
    if (!console_try_parse_long(tok, &port) || port < 0 || port > 65535) return 0;
    if (out_port) *out_port = (uint16_t)port;
    return 1;
}

static void console_run_ggpo_net(const char* arg) {
    char arg_buf[CONSOLE_INPUT_BUF];
    char* cursor;
    char* action;

    if (!arg || !arg[0]) {
        print_ggpo_net_status();
        return;
    }

    safe_copy(arg_buf, sizeof(arg_buf), arg);
    cursor = arg_buf;
    action = console_parse_token(&cursor);
    if (!action || !action[0] || _stricmp(action, "status") == 0) {
        print_ggpo_net_status();
        return;
    }
    if (_stricmp(action, "off") == 0 || _stricmp(action, "stop") == 0 || _stricmp(action, "disable") == 0) {
        stop_ggpo_net("console");
        console_close();
        return;
    }
    if (_stricmp(action, "delay") == 0 || _stricmp(action, "input_delay") == 0) {
        char out[CONSOLE_LINE_TEXT];
        char* tok_delay = console_parse_token(&cursor);
        if (!tok_delay || !tok_delay[0]) {
            snprintf(out,
                     sizeof(out),
                     "ggpo.net: input delay=%u frame(s)",
                     (unsigned int)ggpo_net_input_delay());
            console_push_line_rgb(out, 0.72f, 0.90f, 1.00f);
            return;
        }
        long delay = 0;
        if (!console_try_parse_long(tok_delay, &delay) || delay < 0 || delay > GGPO_NET_MAX_INPUT_DELAY) {
            console_push_line_rgb("Usage: ggpo.net delay [0..8]", 0.98f, 0.76f, 0.40f);
            return;
        }
        if (!ggpo_net_set_input_delay((uint32_t)delay)) {
            console_push_line_rgb("ggpo.net: failed to set input delay", 0.98f, 0.45f, 0.45f);
            return;
        }
        snprintf(out,
                 sizeof(out),
                 "ggpo.net: input delay set to %ld frame(s)%s",
                 delay,
                 ggpo_net_active() ? " for future inputs" : "");
        console_push_line_rgb(out, 0.64f, 0.92f, 0.66f);
        LOG_INFO("%s", out);
        return;
    }
    if (_stricmp(action, "autodelay") == 0 || _stricmp(action, "auto_delay") == 0) {
        char out[CONSOLE_LINE_TEXT];
        char* tok = console_parse_token(&cursor);
        if (tok && tok[0]) {
            int on = (_stricmp(tok, "on") == 0 || _stricmp(tok, "1") == 0 || _stricmp(tok, "true") == 0);
            int off = (_stricmp(tok, "off") == 0 || _stricmp(tok, "0") == 0 || _stricmp(tok, "false") == 0);
            if (!on && !off) {
                console_push_line_rgb("Usage: ggpo.net autodelay [on|off]", 0.98f, 0.76f, 0.40f);
                return;
            }
            ggpo_net_set_auto_input_delay(on);
            LOG_INFO("ggpo.net: auto input delay %s", on ? "on" : "off");
        }
        snprintf(out,
                 sizeof(out),
                 "ggpo.net: auto input delay %s (rtt~%u frames, current delay=%u)",
                 ggpo_net_auto_input_delay() ? "on" : "off",
                 (unsigned int)ggpo_net_rtt_ticks(),
                 (unsigned int)ggpo_net_input_delay());
        console_push_line_rgb(out, 0.72f, 0.90f, 1.00f);
        return;
    }
    if (_stricmp(action, "rngtrace") == 0) {
        char out[CONSOLE_LINE_TEXT];
        char* tok = console_parse_token(&cursor);
        if (tok && tok[0]) {
            int on = (_stricmp(tok, "on") == 0 || _stricmp(tok, "1") == 0 || _stricmp(tok, "true") == 0);
            int off = (_stricmp(tok, "off") == 0 || _stricmp(tok, "0") == 0 || _stricmp(tok, "false") == 0);
            if (!on && !off) {
                console_push_line_rgb("Usage: ggpo.net rngtrace [on|off]", 0.98f, 0.76f, 0.40f);
                return;
            }
            ggpo_net_set_rng_trace(on);
            LOG_INFO("ggpo.net: per-frame rng trace %s", on ? "on" : "off");
        }
        snprintf(out,
                 sizeof(out),
                 "ggpo.net: per-frame rng trace %s (logs live frames that draw RNG to modframework.log)",
                 ggpo_net_rng_trace() ? "on" : "off");
        console_push_line_rgb(out, 0.72f, 0.90f, 1.00f);
        return;
    }
    if (_stricmp(action, "advantage") == 0 || _stricmp(action, "max_advantage") == 0 || _stricmp(action, "timesync") == 0) {
        char out[CONSOLE_LINE_TEXT];
        char* tok_advantage = console_parse_token(&cursor);
        if (!tok_advantage || !tok_advantage[0]) {
            snprintf(out,
                     sizeof(out),
                     "ggpo.net: frame advantage throttle=%u frame(s)",
                     (unsigned int)ggpo_net_max_frame_advantage());
            console_push_line_rgb(out, 0.72f, 0.90f, 1.00f);
            return;
        }
        long advantage = 0;
        if (!console_try_parse_long(tok_advantage, &advantage) || advantage < 0 || advantage > GGPO_NET_MAX_FRAME_ADVANTAGE_LIMIT) {
            console_push_line_rgb("Usage: ggpo.net advantage [0..220]", 0.98f, 0.76f, 0.40f);
            return;
        }
        if (!ggpo_net_set_max_frame_advantage((uint32_t)advantage)) {
            console_push_line_rgb("ggpo.net: failed to set frame advantage throttle", 0.98f, 0.45f, 0.45f);
            return;
        }
        snprintf(out,
                 sizeof(out),
                 "ggpo.net: frame advantage throttle set to %ld frame(s)%s",
                 advantage,
                 ggpo_net_active() ? " now active" : "");
        console_push_line_rgb(out, 0.64f, 0.92f, 0.66f);
        LOG_INFO("%s", out);
        return;
    }
    if (_stricmp(action, "predict") == 0 || _stricmp(action, "prediction") == 0 || _stricmp(action, "max_prediction") == 0) {
        char out[CONSOLE_LINE_TEXT];
        char* tok_prediction = console_parse_token(&cursor);
        if (!tok_prediction || !tok_prediction[0]) {
            snprintf(out,
                     sizeof(out),
                     "ggpo.net: max prediction=%u frame(s)",
                     (unsigned int)ggpo_net_max_prediction());
            console_push_line_rgb(out, 0.72f, 0.90f, 1.00f);
            return;
        }
        long prediction = 0;
        if (!console_try_parse_long(tok_prediction, &prediction) || prediction <= 0 || prediction > GGPO_NET_MAX_PREDICTION_LIMIT) {
            console_push_line_rgb("Usage: ggpo.net predict [1..220]", 0.98f, 0.76f, 0.40f);
            return;
        }
        if (!ggpo_net_set_max_prediction((uint32_t)prediction)) {
            console_push_line_rgb("ggpo.net: failed to set max prediction", 0.98f, 0.45f, 0.45f);
            return;
        }
        snprintf(out,
                 sizeof(out),
                 "ggpo.net: max prediction set to %ld frame(s)%s",
                 prediction,
                 ggpo_net_active() ? " now active" : "");
        console_push_line_rgb(out, 0.64f, 0.92f, 0.66f);
        LOG_INFO("%s", out);
        return;
    }
    if (_stricmp(action, "highping") == 0 || _stricmp(action, "latency") == 0 || _stricmp(action, "budget") == 0) {
        char out[CONSOLE_LINE_TEXT];
        char* tok_budget = console_parse_token(&cursor);
        if (!tok_budget || !tok_budget[0]) {
            snprintf(out,
                     sizeof(out),
                     "ggpo.net: conservative latency profile adv=%u predcap=%u frame(s)",
                     (unsigned int)ggpo_net_max_frame_advantage(),
                     (unsigned int)ggpo_net_max_prediction());
            console_push_line_rgb(out, 0.72f, 0.90f, 1.00f);
            return;
        }
        long budget = 0;
        if (!console_try_parse_long(tok_budget, &budget) || budget <= 0 || budget > GGPO_NET_MAX_PREDICTION_LIMIT) {
            console_push_line_rgb("Usage: ggpo.net highping [1..220]", 0.98f, 0.76f, 0.40f);
            return;
        }
        uint32_t predcap = (uint32_t)budget;
        uint32_t advantage = (uint32_t)budget;
        if (predcap > 32u) {
            predcap = ((uint32_t)budget + 3u) / 4u;
            if (predcap < 16u) predcap = 16u;
            if (predcap > 48u) predcap = 48u;
        }
        if (advantage > 48u) {
            advantage = ((uint32_t)budget + 1u) / 2u;
            if (advantage < 24u) advantage = 24u;
            if (advantage > 72u) advantage = 72u;
        }
        if (!ggpo_net_set_max_prediction(predcap) ||
            !ggpo_net_set_max_frame_advantage(advantage)) {
            console_push_line_rgb("ggpo.net: failed to set high ping budget", 0.98f, 0.45f, 0.45f);
            return;
        }
        snprintf(out,
                 sizeof(out),
                 "ggpo.net: high ping profile budget=%ld adv=%u predcap=%u%s",
                 budget,
                 (unsigned int)advantage,
                 (unsigned int)predcap,
                 ggpo_net_active() ? " now active" : "");
        console_push_line_rgb(out, 0.64f, 0.92f, 0.66f);
        LOG_INFO("%s", out);
        return;
    }
    if (_stricmp(action, "smoothping") == 0 || _stricmp(action, "smooth") == 0 || _stricmp(action, "aggressive") == 0) {
        char out[CONSOLE_LINE_TEXT];
        char* tok_budget = console_parse_token(&cursor);
        if (!tok_budget || !tok_budget[0]) {
            snprintf(out,
                     sizeof(out),
                     "ggpo.net: smooth latency profile adv=%u predcap=%u frame(s)",
                     (unsigned int)ggpo_net_max_frame_advantage(),
                     (unsigned int)ggpo_net_max_prediction());
            console_push_line_rgb(out, 0.72f, 0.90f, 1.00f);
            return;
        }
        long budget = 0;
        if (!console_try_parse_long(tok_budget, &budget) || budget <= 0 || budget > GGPO_NET_MAX_PREDICTION_LIMIT) {
            console_push_line_rgb("Usage: ggpo.net smoothping [1..220]", 0.98f, 0.76f, 0.40f);
            return;
        }
        if (!ggpo_net_set_max_prediction((uint32_t)budget) ||
            !ggpo_net_set_max_frame_advantage((uint32_t)budget)) {
            console_push_line_rgb("ggpo.net: failed to set smooth ping budget", 0.98f, 0.45f, 0.45f);
            return;
        }
        snprintf(out,
                 sizeof(out),
                 "ggpo.net: smooth ping budget set to %ld frame(s)%s",
                 budget,
                 ggpo_net_active() ? " now active" : "");
        console_push_line_rgb(out, 0.64f, 0.92f, 0.66f);
        LOG_INFO("%s", out);
        return;
    }
    if (_stricmp(action, "correction") == 0 || _stricmp(action, "correct") == 0 || _stricmp(action, "resync") == 0) {
        char out[CONSOLE_LINE_TEXT];
        char* tok_enabled = console_parse_token(&cursor);
        int enabled = 0;
        if (!tok_enabled || !tok_enabled[0] || _stricmp(tok_enabled, "status") == 0) {
            snprintf(out,
                     sizeof(out),
                     "ggpo.net: correction=%s active=%d awaiting=%d sent=%u received=%u requests=%u stale=%u dup_chunk=%u id=%u applied=%u",
                     ggpo_net_correction_enabled() ? "on" : "off",
                     ggpo_net_correction_active(),
                     ggpo_net_awaiting_correction(),
                     (unsigned int)ggpo_net_corrections_sent(),
                     (unsigned int)ggpo_net_corrections_received(),
                     (unsigned int)ggpo_net_correction_request_count(),
                     (unsigned int)ggpo_net_stale_correction_request_count(),
                     (unsigned int)ggpo_net_duplicate_state_chunk_count(),
                     (unsigned int)ggpo_net_correction_id(),
                     (unsigned int)ggpo_net_last_correction_applied_id());
            console_push_line_rgb(out, 0.72f, 0.90f, 1.00f);
            LOG_INFO("%s", out);
            return;
        }
        if (!console_try_parse_bool(tok_enabled, &enabled)) {
            console_push_line_rgb("Usage: ggpo.net correction [on|off]", 0.98f, 0.76f, 0.40f);
            return;
        }
        (void)ggpo_net_set_correction_enabled(enabled);
        snprintf(out,
                 sizeof(out),
                 "ggpo.net: correction %s%s",
                 enabled ? "enabled" : "disabled",
                 ggpo_net_active() ? " for this session" : "");
        console_push_line_rgb(out, 0.64f, 0.92f, 0.66f);
        LOG_INFO("%s", out);
        return;
    }
    if (_stricmp(action, "sim") == 0 || _stricmp(action, "netem") == 0) {
        char out[CONSOLE_LINE_TEXT];
        char* tok_loss = console_parse_token(&cursor);
        long loss = 0;
        long min_delay = 0;
        long max_delay = 0;
        if (!tok_loss || !tok_loss[0]) {
            snprintf(out,
                     sizeof(out),
                     "ggpo.net: sim loss=%u%% delay=%u-%u tick(s)",
                     (unsigned int)ggpo_net_sim_loss_percent(),
                     (unsigned int)ggpo_net_sim_delay_min_ticks(),
                     (unsigned int)ggpo_net_sim_delay_max_ticks());
            console_push_line_rgb(out, 0.72f, 0.90f, 1.00f);
            return;
        }
        if (_stricmp(tok_loss, "off") == 0 || _stricmp(tok_loss, "clear") == 0 || _stricmp(tok_loss, "none") == 0) {
            if (!ggpo_net_set_network_sim(0u, 0u, 0u)) {
                console_push_line_rgb("ggpo.net: failed to clear network simulation", 0.98f, 0.45f, 0.45f);
                return;
            }
            console_push_line_rgb("ggpo.net: network simulation cleared", 0.64f, 0.92f, 0.66f);
            LOG_INFO("ggpo.net: network simulation cleared");
            return;
        }
        if (!console_try_parse_long(tok_loss, &loss) || loss < 0 || loss > 100) {
            console_push_line_rgb("Usage: ggpo.net sim [loss_pct 0..100] [min_delay 0..120] [max_delay 0..120]", 0.98f, 0.76f, 0.40f);
            return;
        }
        {
            char* tok_min = console_parse_token(&cursor);
            char* tok_max = console_parse_token(&cursor);
            if (tok_min && tok_min[0]) {
                if (!console_try_parse_long(tok_min, &min_delay) || min_delay < 0 || min_delay > GGPO_NET_SIM_MAX_DELAY_TICKS) {
                    console_push_line_rgb("Usage: ggpo.net sim [loss_pct 0..100] [min_delay 0..120] [max_delay 0..120]", 0.98f, 0.76f, 0.40f);
                    return;
                }
                max_delay = min_delay;
            }
            if (tok_max && tok_max[0]) {
                if (!console_try_parse_long(tok_max, &max_delay) || max_delay < 0 || max_delay > GGPO_NET_SIM_MAX_DELAY_TICKS || max_delay < min_delay) {
                    console_push_line_rgb("Usage: ggpo.net sim [loss_pct 0..100] [min_delay 0..120] [max_delay 0..120]", 0.98f, 0.76f, 0.40f);
                    return;
                }
            }
        }
        if (!ggpo_net_set_network_sim((uint32_t)loss, (uint32_t)min_delay, (uint32_t)max_delay)) {
            console_push_line_rgb("ggpo.net: failed to set network simulation", 0.98f, 0.45f, 0.45f);
            return;
        }
        snprintf(out,
                 sizeof(out),
                 "ggpo.net: sim loss=%ld%% delay=%ld-%ld tick(s)%s",
                 loss,
                 min_delay,
                 max_delay,
                 ggpo_net_active() ? " now active" : "");
        console_push_line_rgb(out, 0.64f, 0.92f, 0.66f);
        LOG_INFO("%s", out);
        return;
    }
    if (_stricmp(action, "host") == 0) {
        uint16_t port = GGPO_NET_DEFAULT_PORT;
        char* tok_port = console_parse_token(&cursor);
        if (tok_port && tok_port[0] && !parse_port_token(tok_port, &port)) {
            console_push_line_rgb("Usage: ggpo.net host [port]", 0.98f, 0.76f, 0.40f);
            return;
        }
        start_ggpo_net_host(port, "console");
        console_close();
        return;
    }
    if (_stricmp(action, "join") == 0) {
        uint16_t remote_port = GGPO_NET_DEFAULT_PORT;
        uint16_t local_port = 0;
        char* host = console_parse_token(&cursor);
        char* tok_remote_port = console_parse_token(&cursor);
        char* tok_local_port = console_parse_token(&cursor);
        if (!host || !host[0]) {
            console_push_line_rgb("Usage: ggpo.net join <host> [port] [local_port]", 0.98f, 0.76f, 0.40f);
            return;
        }
        if (tok_remote_port && tok_remote_port[0] && !parse_port_token(tok_remote_port, &remote_port)) {
            console_push_line_rgb("Usage: ggpo.net join <host> [port] [local_port]", 0.98f, 0.76f, 0.40f);
            return;
        }
        if (tok_local_port && tok_local_port[0] && !parse_port_token(tok_local_port, &local_port)) {
            console_push_line_rgb("Usage: ggpo.net join <host> [port] [local_port]", 0.98f, 0.76f, 0.40f);
            return;
        }
        start_ggpo_net_join(host, remote_port, local_port, "console");
        console_close();
        return;
    }

    console_push_line_rgb("Usage: ggpo.net <host|join|off|status|delay|advantage|predict|highping|smoothping|correction|sim>", 0.98f, 0.76f, 0.40f);
}

static void console_run_ggpo_selftest(const char* arg) {
    long frames = 120;

    if (arg && arg[0]) {
        if (!console_try_parse_long(arg, &frames) || frames <= 0) {
            console_push_line_rgb("Usage: ggpo.selftest [positive_frames]", 0.98f, 0.76f, 0.40f);
            return;
        }
    }

    queue_ggpo_selftest((int)frames, "console");
    console_close();
}

static void console_run_ggpo_roundtrip(const char* arg) {
    (void)arg;
    run_ggpo_roundtrip_check("console");
}

static void console_set_log_level(const char* level_arg) {
    char out[CONSOLE_LINE_TEXT];
    if (!level_arg || !level_arg[0]) {
        snprintf(out, sizeof(out), "log.level is %s", log_get_level_name());
        console_push_line_rgb(out, 0.72f, 0.90f, 1.00f);
        return;
    }

    if (!log_set_level_name(level_arg)) {
        console_push_line_rgb("Usage: log.level <debug|info|warn|error>", 0.98f, 0.76f, 0.40f);
        return;
    }

    snprintf(out, sizeof(out), "log.level set to %s", log_get_level_name());
    console_push_line_rgb(out, 0.64f, 0.92f, 0.66f);
}

static void console_handle_time_scale(const char* arg) {
    char out[CONSOLE_LINE_TEXT];
    int manual_active = lua_manager_get_time_scale_manual(NULL);

    if (!arg || !arg[0]) {
        snprintf(out, sizeof(out), "time.scale: %.6g%s",
                 (double)lua_manager_get_time_scale(),
                 manual_active ? " (manual)" : "");
        console_push_line_rgb(out, 0.72f, 0.90f, 1.00f);
        return;
    }

    {
        char arg_buf[CONSOLE_INPUT_BUF];
        char* a;
        double value = 1.0;
        safe_copy(arg_buf, sizeof(arg_buf), arg);
        a = trim_ws(arg_buf);
        if (!a || !a[0]) {
            snprintf(out, sizeof(out), "time.scale: %.6g%s",
                     (double)lua_manager_get_time_scale(),
                     manual_active ? " (manual)" : "");
            console_push_line_rgb(out, 0.72f, 0.90f, 1.00f);
            return;
        }

        if (_stricmp(a, "auto") == 0 || _stricmp(a, "default") == 0) {
            lua_manager_clear_time_scale();
            snprintf(out, sizeof(out), "time.scale manual override cleared (current %.6g)",
                     (double)lua_manager_get_time_scale());
            console_push_line_rgb(out, 0.64f, 0.92f, 0.66f);
            return;
        }

        if (!console_try_parse_double(a, &value)) {
            console_push_line_rgb("Usage: time.scale [value|auto]", 0.98f, 0.76f, 0.40f);
            return;
        }

        lua_manager_set_time_scale((float)value);
        snprintf(out, sizeof(out), "time.scale set to %.6g",
                 (double)lua_manager_get_time_scale());
        console_push_line_rgb(out, 0.64f, 0.92f, 0.66f);
    }
}

static void console_tail_log(const char* lines_arg) {
    const char* path = "mods\\modframework.log";
    FILE* f;
    long want = 30;
    int total = 0;
    int skip = 0;
    char line[1024];
    char out[CONSOLE_LINE_TEXT];

    if (lines_arg && lines_arg[0]) {
        long parsed = 0;
        if (!console_try_parse_long(lines_arg, &parsed) || parsed <= 0) {
            console_push_line_rgb("Usage: log.tail [positive_line_count]", 0.98f, 0.76f, 0.40f);
            return;
        }
        if (parsed > 300) parsed = 300;
        want = parsed;
    }

    f = fopen(path, "r");
    if (!f) {
        console_push_line_rgb("log.tail: failed to open mods/modframework.log", 0.98f, 0.45f, 0.45f);
        return;
    }

    while (fgets(line, sizeof(line), f)) total++;
    if (total > want) skip = total - (int)want;
    rewind(f);
    while (skip > 0 && fgets(line, sizeof(line), f)) skip--;

    snprintf(out, sizeof(out), "log.tail: showing last %d of %d line(s)", total < (int)want ? total : (int)want, total);
    console_push_line_rgb(out, 0.72f, 0.90f, 1.00f);

    while (fgets(line, sizeof(line), f)) {
        console_strip_crlf(line);
        if (!line[0]) continue;
        console_push_line_rgb(line, 0.78f, 0.82f, 0.88f);
    }
    fclose(f);
}

static void console_run_lua_code(const char* code) {
    char out[1024];
    int ok;
    if (!code || !code[0]) {
        console_push_line_rgb("Usage: lua <code>", 0.98f, 0.76f, 0.40f);
        return;
    }
    ok = lua_manager_console_eval(code, out, (int)sizeof(out));
    console_push_line_rgb(out[0] ? out : (ok ? "ok" : "lua error"),
                          ok ? 0.64f : 0.98f,
                          ok ? 0.92f : 0.45f,
                          ok ? 0.66f : 0.45f);
}

static void console_run_lua_mod_code(const char* args) {
    char buf[CONSOLE_INPUT_BUF];
    char* cursor;
    char* mod_id;
    char* code;
    char out[1024];
    int ok;

    if (!args || !args[0]) {
        console_push_line_rgb("Usage: lua.mod <id> <code>", 0.98f, 0.76f, 0.40f);
        return;
    }

    safe_copy(buf, sizeof(buf), args);
    cursor = buf;
    mod_id = console_parse_token(&cursor);
    code = trim_ws(cursor ? cursor : "");
    if (!mod_id || !mod_id[0] || !code || !code[0]) {
        console_push_line_rgb("Usage: lua.mod <id> <code>", 0.98f, 0.76f, 0.40f);
        return;
    }

    ok = lua_manager_console_eval_mod(mod_id, code, out, (int)sizeof(out));
    console_push_line_rgb(out[0] ? out : (ok ? "ok" : "lua error"),
                          ok ? 0.64f : 0.98f,
                          ok ? 0.92f : 0.45f,
                          ok ? 0.66f : 0.45f);
}

static void console_run_lua_file(const char* arg) {
    char buf[CONSOLE_INPUT_BUF];
    char* cursor;
    char* path;
    char out[1024];
    int ok;

    if (!arg || !arg[0]) {
        console_push_line_rgb("Usage: lua.file <path>", 0.98f, 0.76f, 0.40f);
        return;
    }

    safe_copy(buf, sizeof(buf), arg);
    cursor = buf;
    path = console_parse_token(&cursor);
    if (!path || !path[0]) {
        console_push_line_rgb("Usage: lua.file <path>", 0.98f, 0.76f, 0.40f);
        return;
    }

    ok = lua_manager_console_run_file(path, out, (int)sizeof(out));
    console_push_line_rgb(out[0] ? out : (ok ? "ok" : "lua error"),
                          ok ? 0.64f : 0.98f,
                          ok ? 0.92f : 0.45f,
                          ok ? 0.66f : 0.45f);
}

static void console_show_input_override(const char* player_arg) {
    int p0 = 0;
    int p1 = 1;
    if (player_arg && player_arg[0]) {
        long p = 0;
        if (!console_try_parse_long(player_arg, &p) || p < 0 || p > 1) {
            console_push_line_rgb("Usage: input.show [0|1]", 0.98f, 0.76f, 0.40f);
            return;
        }
        p0 = (int)p;
        p1 = (int)p;
    }

    for (int pi = p0; pi <= p1; pi++) {
        uint32_t mask = 0;
        int frames = 0;
        int replace = 0;
        int active = hooks_get_input_override(pi, &mask, &frames, &replace);
        char out[CONSOLE_LINE_TEXT];
        snprintf(out, sizeof(out), "input[%d]: active=%s mask=0x%08X frames=%d replace=%d",
                 pi, active ? "true" : "false", (unsigned int)mask, frames, replace);
        console_push_line_rgb(out, 0.72f, 0.90f, 1.00f);
    }
}

static void console_set_input_override_cmd(const char* args) {
    char buf[CONSOLE_INPUT_BUF];
    char* cursor;
    char* tok_player;
    char* tok_mask;
    char* tok_frames;
    char* tok_replace;
    long p = 0;
    long mask = 0;
    long frames = -1;
    long replace = 0;
    if (!args || !args[0]) {
        console_push_line_rgb("Usage: input.override <player> <mask> [frames] [replace]", 0.98f, 0.76f, 0.40f);
        return;
    }
    safe_copy(buf, sizeof(buf), args);
    cursor = buf;
    tok_player = console_parse_token(&cursor);
    tok_mask = console_parse_token(&cursor);
    tok_frames = console_parse_token(&cursor);
    tok_replace = console_parse_token(&cursor);

    if (!tok_player || !tok_mask) {
        console_push_line_rgb("Usage: input.override <player> <mask> [frames] [replace]", 0.98f, 0.76f, 0.40f);
        return;
    }
    if (!console_try_parse_long(tok_player, &p) || p < 0 || p > 1) {
        console_push_line_rgb("input.override: player must be 0 or 1", 0.98f, 0.76f, 0.40f);
        return;
    }
    if (!console_try_parse_long(tok_mask, &mask)) {
        console_push_line_rgb("input.override: mask must be an integer (decimal or hex)", 0.98f, 0.76f, 0.40f);
        return;
    }
    if (tok_frames && tok_frames[0]) {
        if (!console_try_parse_long(tok_frames, &frames)) {
            console_push_line_rgb("input.override: frames must be an integer", 0.98f, 0.76f, 0.40f);
            return;
        }
    }
    if (tok_replace && tok_replace[0]) {
        if (!console_try_parse_long(tok_replace, &replace)) {
            console_push_line_rgb("input.override: replace must be 0 or 1", 0.98f, 0.76f, 0.40f);
            return;
        }
        replace = replace ? 1 : 0;
    }

    hooks_set_input_override((int)p, (uint32_t)mask, (int)frames, (int)replace);
    console_show_input_override(tok_player);
}

static void console_clear_input_override_cmd(const char* player_arg) {
    long p = 0;
    if (!player_arg || !player_arg[0]) {
        console_push_line_rgb("Usage: input.clear <player>", 0.98f, 0.76f, 0.40f);
        return;
    }
    if (!console_try_parse_long(player_arg, &p) || p < 0 || p > 1) {
        console_push_line_rgb("input.clear: player must be 0 or 1", 0.98f, 0.76f, 0.40f);
        return;
    }
    hooks_clear_input_override((int)p);
    console_show_input_override(player_arg);
}

static void console_show_binds(const char* id) {
    int start = 0;
    int end = lua_manager_get_mod_count() - 1;
    if (id && id[0]) {
        int idx = console_find_mod_index_by_id(id);
        if (idx < 0) {
            char out[CONSOLE_LINE_TEXT];
            snprintf(out, sizeof(out), "Mod not found: %s", id);
            console_push_line_rgb(out, 0.98f, 0.45f, 0.45f);
            return;
        }
        start = idx;
        end = idx;
    }
    for (int mi = start; mi <= end; mi++) {
        int bind_count = lua_manager_get_mod_bind_count(mi);
        char head[CONSOLE_LINE_TEXT];
        snprintf(head, sizeof(head), "binds %s: %d entries", lua_manager_get_mod_id(mi), bind_count);
        console_push_line_rgb(head, 0.72f, 0.90f, 1.00f);
        for (int bi = 0; bi < bind_count; bi++) {
            char out[CONSOLE_LINE_TEXT];
            snprintf(out, sizeof(out), "  %s (%s) = %s%s",
                     lua_manager_get_mod_bind_key(mi, bi),
                     lua_manager_get_mod_bind_label(mi, bi),
                     lua_manager_get_mod_bind_value_str(mi, bi),
                     lua_manager_mod_bind_has_conflict(mi, bi) ? " [conflict]" : "");
            console_push_line_rgb(out,
                                  lua_manager_mod_bind_has_conflict(mi, bi) ? 0.98f : 0.80f,
                                  lua_manager_mod_bind_has_conflict(mi, bi) ? 0.72f : 0.83f,
                                  lua_manager_mod_bind_has_conflict(mi, bi) ? 0.40f : 0.90f);
        }
    }
}

static void console_find_binds(const char* query) {
    int matches = 0;
    if (!query || !query[0]) {
        console_push_line_rgb("Usage: binds.find <text>", 0.98f, 0.76f, 0.40f);
        return;
    }
    for (int mi = 0; mi < lua_manager_get_mod_count(); mi++) {
        for (int bi = 0; bi < lua_manager_get_mod_bind_count(mi); bi++) {
            const char* mod_id = lua_manager_get_mod_id(mi);
            const char* key = lua_manager_get_mod_bind_key(mi, bi);
            const char* label = lua_manager_get_mod_bind_label(mi, bi);
            const char* value = lua_manager_get_mod_bind_value_str(mi, bi);
            if (!console_stristr(mod_id, query) && !console_stristr(key, query) && !console_stristr(label, query) && !console_stristr(value, query)) continue;
            {
                char out[CONSOLE_LINE_TEXT];
                snprintf(out, sizeof(out), "%s.%s = %s%s", mod_id, key, value, lua_manager_mod_bind_has_conflict(mi, bi) ? " [conflict]" : "");
                console_push_line_rgb(out, 0.80f, 0.83f, 0.90f);
            }
            matches++;
        }
    }
    if (matches == 0) {
        char out[CONSOLE_LINE_TEXT];
        snprintf(out, sizeof(out), "binds.find: no matches for '%s'", query);
        console_push_line_rgb(out, 0.98f, 0.76f, 0.40f);
    }
}

static void console_set_bind_cmd(const char* args, int clear_only) {
    char buf[CONSOLE_INPUT_BUF];
    char* cursor;
    char* id;
    char* key;
    char* value;
    int idx;
    int bind_idx;
    if (!args || !args[0]) {
        console_push_line_rgb(clear_only ? "Usage: binds.clear <id> <key>" : "Usage: binds.set <id> <key> <value>", 0.98f, 0.76f, 0.40f);
        return;
    }
    safe_copy(buf, sizeof(buf), args);
    cursor = buf;
    id = console_parse_token(&cursor);
    key = console_parse_token(&cursor);
    value = cursor ? trim_ws(cursor) : "";
    if (!id || !id[0] || !key || !key[0] || (!clear_only && (!value || !value[0]))) {
        console_push_line_rgb(clear_only ? "Usage: binds.clear <id> <key>" : "Usage: binds.set <id> <key> <value>", 0.98f, 0.76f, 0.40f);
        return;
    }
    idx = console_find_mod_index_by_id(id);
    if (idx < 0) {
        char out[CONSOLE_LINE_TEXT];
        snprintf(out, sizeof(out), "Mod not found: %s", id);
        console_push_line_rgb(out, 0.98f, 0.45f, 0.45f);
        return;
    }
    bind_idx = console_find_bind_index_by_key(idx, key);
    if (bind_idx < 0) {
        char out[CONSOLE_LINE_TEXT];
        snprintf(out, sizeof(out), "Bind key not found: %s", key);
        console_push_line_rgb(out, 0.98f, 0.45f, 0.45f);
        return;
    }
    if (!(clear_only ? lua_manager_clear_mod_bind_value(idx, bind_idx) : console_apply_bind_value(idx, bind_idx, value))) {
        console_push_line_rgb("binds.set: invalid bind value", 0.98f, 0.76f, 0.40f);
        return;
    }
    console_show_binds(lua_manager_get_mod_id(idx));
}



static void console_execute_input(void) {
    char work[CONSOLE_INPUT_BUF];
    char* cmd;
    char* arg = NULL;
    char* p;

    safe_copy(work, sizeof(work), g_console_input);
    cmd = trim_ws(work);
    if (!cmd || !cmd[0]) {
        console_set_input("");
        console_reset_history_nav();
        return;
    }

    console_push_command_line(cmd);
    console_history_add(cmd);
    console_reset_history_nav();

    p = cmd;
    while (*p && !isspace((unsigned char)*p)) p++;
    if (*p) {
        *p = '\0';
        p++;
        arg = trim_ws(p);
    } else {
        arg = "";
    }

    if (_stricmp(cmd, "help") == 0 || _stricmp(cmd, "commands") == 0) {
        console_show_help(arg);
    } else if (_stricmp(cmd, "clear") == 0) {
        console_clear_output();
    } else if (_stricmp(cmd, "history") == 0) {
        console_show_history(arg);
    } else if (_stricmp(cmd, "echo") == 0) {
        console_echo(arg);
    } else if (_stricmp(cmd, "console.stats") == 0) {
        console_show_console_stats();
    } else if (_stricmp(cmd, "console.copy") == 0) {
        console_copy_cmd(arg);
    } else if (_stricmp(cmd, "state") == 0) {
        console_show_state();
    } else if (_stricmp(cmd, "state.last") == 0) {
        console_show_last_state();
    } else if (_stricmp(cmd, "state.return") == 0) {
        console_set_return_state(arg);
    } else if (_stricmp(cmd, "state.switch") == 0) {
        console_switch_state(arg);
    } else if (_stricmp(cmd, "sys.info") == 0) {
        console_show_system_info();
    } else if (_stricmp(cmd, "ui.size") == 0) {
        console_show_ui_size();
    } else if (_stricmp(cmd, "time.scale") == 0) {
        console_handle_time_scale(arg);
    } else if (_stricmp(cmd, "framework.api") == 0) {
        char out[CONSOLE_LINE_TEXT];
        snprintf(out, sizeof(out), "framework.api: %d", lua_manager_framework_api());
        console_push_line_rgb(out, 0.72f, 0.90f, 1.00f);
    } else if (_stricmp(cmd, "mods.count") == 0) {
        console_show_mods_count();
    } else if (_stricmp(cmd, "mods.list") == 0) {
        console_show_mods_list();
    } else if (_stricmp(cmd, "mods.find") == 0) {
        console_find_mods(arg);
    } else if (_stricmp(cmd, "mods.info") == 0) {
        console_show_mod_info(arg);
    } else if (_stricmp(cmd, "mods.trace") == 0) {
        console_set_mod_trace(arg);
    } else if (_stricmp(cmd, "mods.enable") == 0) {
        console_set_mod_enabled(arg, 1);
    } else if (_stricmp(cmd, "mods.disable") == 0) {
        console_set_mod_enabled(arg, 0);
    } else if (_stricmp(cmd, "mods.toggle") == 0) {
        console_toggle_mod_enabled(arg);
    } else if (_stricmp(cmd, "mods.config") == 0 || _stricmp(cmd, "mods.cfg") == 0) {
        console_show_mod_config(arg);
    } else if (_stricmp(cmd, "mods.config.find") == 0 || _stricmp(cmd, "mods.cfg.find") == 0) {
        console_find_mod_config(arg);
    } else if (_stricmp(cmd, "mods.config.get") == 0 || _stricmp(cmd, "mods.cfg.get") == 0) {
        char args_buf[CONSOLE_INPUT_BUF];
        char* cursor;
        char* id;
        char* key;
        safe_copy(args_buf, sizeof(args_buf), arg);
        cursor = args_buf;
        id = console_parse_token(&cursor);
        key = console_parse_token(&cursor);
        console_show_mod_config_value(id ? id : "", key ? key : "");
    } else if (_stricmp(cmd, "mods.config.set") == 0 || _stricmp(cmd, "mods.cfg.set") == 0) {
        char args_buf[CONSOLE_INPUT_BUF];
        char* cursor;
        char* id;
        char* key;
        char* value;
        safe_copy(args_buf, sizeof(args_buf), arg);
        cursor = args_buf;
        id = console_parse_token(&cursor);
        key = console_parse_token(&cursor);
        value = cursor ? trim_ws(cursor) : "";
        if (value && value[0] && (value[0] == '"' || value[0] == '\'')) {
            char q = value[0];
            size_t len;
            value++;
            len = strlen(value);
            if (len > 0 && value[len - 1] == q) value[len - 1] = '\0';
        }
        console_set_mod_config_value(id ? id : "", key ? key : "", value ? value : "");
    } else if (_stricmp(cmd, "mods.config.action") == 0 || _stricmp(cmd, "mods.cfg.action") == 0) {
        char args_buf[CONSOLE_INPUT_BUF];
        char* cursor;
        char* id;
        char* key;
        safe_copy(args_buf, sizeof(args_buf), arg);
        cursor = args_buf;
        id = console_parse_token(&cursor);
        key = console_parse_token(&cursor);
        console_trigger_mod_config_action(id ? id : "", key ? key : "");
    } else if (_stricmp(cmd, "binds.list") == 0) {
        console_show_binds(arg);
    } else if (_stricmp(cmd, "binds.find") == 0) {
        console_find_binds(arg);
    } else if (_stricmp(cmd, "binds.set") == 0) {
        console_set_bind_cmd(arg, 0);
    } else if (_stricmp(cmd, "binds.clear") == 0) {
        console_set_bind_cmd(arg, 1);
    } else if (_stricmp(cmd, "reload.mods") == 0) {
        console_run_reload_mods();
    } else if (_stricmp(cmd, "mods.reload") == 0) {
        console_run_reload_mods();
    } else if (_stricmp(cmd, "reload.assets") == 0) {
        console_run_reload_assets();
    } else if (_stricmp(cmd, "online.hub") == 0 || _stricmp(cmd, "online") == 0) {
        online_hub_load();
        console_close();
        online_hub_open();
    } else if (_stricmp(cmd, "ggpo.roundtrip") == 0) {
        console_run_ggpo_roundtrip(arg);
    } else if (_stricmp(cmd, "ggpo.selftest") == 0) {
        console_run_ggpo_selftest(arg);
    } else if (_stricmp(cmd, "ggpo.local") == 0) {
        console_run_ggpo_local(arg);
    } else if (_stricmp(cmd, "ggpo.net") == 0) {
        console_run_ggpo_net(arg);
    } else if (_stricmp(cmd, "log.level") == 0) {
        console_set_log_level(arg);
    } else if (_stricmp(cmd, "log.tail") == 0) {
        console_tail_log(arg);
    } else if (_stricmp(cmd, "input.show") == 0) {
        console_show_input_override(arg);
    } else if (_stricmp(cmd, "input.override") == 0) {
        console_set_input_override_cmd(arg);
    } else if (_stricmp(cmd, "input.clear") == 0) {
        console_clear_input_override_cmd(arg);
    } else if (_stricmp(cmd, "lua") == 0 || _stricmp(cmd, "eval") == 0) {
        console_run_lua_code(arg);
    } else if (_stricmp(cmd, "lua.mod") == 0 || _stricmp(cmd, "eval.mod") == 0) {
        console_run_lua_mod_code(arg);
    } else if (_stricmp(cmd, "lua.file") == 0) {
        console_run_lua_file(arg);
    } else if (_stricmp(cmd, "exit") == 0) {
        console_close();
    } else if (_stricmp(cmd, "quit") == 0) {
        console_close();
    } else {
        char out[CONSOLE_LINE_TEXT];
        snprintf(out, sizeof(out), "Unknown command: %s (type 'help')", cmd);
        console_push_line_rgb(out, 0.98f, 0.45f, 0.45f);
        console_suggest_commands(cmd);
    }

    console_set_input("");
}

static char keycode_to_char(int sym, int mod) {
    int shift = (mod & KMOD_SHIFT) != 0;

    if (sym >= 'a' && sym <= 'z') {
        return (char)(shift ? toupper(sym) : sym);
    }

    if (sym >= '0' && sym <= '9') {
        if (!shift) return (char)sym;
        switch (sym) {
            case '1': return '!';
            case '2': return '@';
            case '3': return '#';
            case '4': return '$';
            case '5': return '%';
            case '6': return '^';
            case '7': return '&';
            case '8': return '*';
            case '9': return '(';
            case '0': return ')';
        }
    }

    switch (sym) {
        case SDLK_SPACE: return ' ';
        case '-': return shift ? '_' : '-';
        case '=': return shift ? '+' : '=';
        case '[': return shift ? '{' : '[';
        case ']': return shift ? '}' : ']';
        case '\\': return shift ? '|' : '\\';
        case ';': return shift ? ':' : ';';
        case '\'': return shift ? '"' : '\'';
        case ',': return shift ? '<' : ',';
        case '.': return shift ? '>' : '.';
        case '/': return shift ? '?' : '/';
        case '`': return shift ? '~' : '`';
        default: return 0;
    }
}

static void online_hub_defaults(void) {
    memset(&g_online_cfg, 0, sizeof(g_online_cfg));
    g_online_cfg.username[0] = '\0';
    g_online_cfg.password[0] = '\0';
    safe_copy(g_online_cfg.server_host, sizeof(g_online_cfg.server_host), ONLINE_DEFAULT_SERVER_HOST);
    g_online_cfg.server_port = ONLINE_DEFAULT_SERVER_PORT;
    safe_copy(g_online_cfg.peer_host, sizeof(g_online_cfg.peer_host), "127.0.0.1");
    g_online_cfg.peer_port = GGPO_NET_DEFAULT_PORT;
    g_online_cfg.local_port = 0;
    g_online_cfg.p2p_enabled = 1;
    g_online_cfg.relay_fallback = 0;
    g_online_cfg.input_delay = (int)ggpo_net_input_delay();
    g_online_cfg.max_frame_advantage = (int)ggpo_net_max_frame_advantage();
    g_online_cfg.max_prediction = (int)ggpo_net_max_prediction();
    g_online_cfg.correction_enabled = 1;
    g_online_cfg.sim_loss = 0;
    g_online_cfg.sim_min_delay = 0;
    g_online_cfg.sim_max_delay = 0;
    g_online_cfg.challenge_notifications = 1;
}

static int online_parse_long_range(const char* s, long lo, long hi, long* out) {
    char* end = NULL;
    long v;
    if (!s || !s[0]) return 0;
    v = strtol(s, &end, 0);
    if (!end || *trim_ws(end) != '\0') return 0;
    if (v < lo || v > hi) return 0;
    if (out) *out = v;
    return 1;
}

static void online_hub_set_status(const char* msg) {
    safe_copy(g_online_status, sizeof(g_online_status), msg ? msg : "");
    if (msg && msg[0]) {
        LOG_INFO("online.hub: %s", msg);
    }
}

static uint16_t online_u16_or_default(long value, uint16_t fallback) {
    if (value < 0 || value > 65535) return fallback;
    return (uint16_t)value;
}

static void online_hub_clamp_config(void) {
    g_online_cfg.server_port = online_u16_or_default(g_online_cfg.server_port, ONLINE_DEFAULT_SERVER_PORT);
    g_online_cfg.peer_port = online_u16_or_default(g_online_cfg.peer_port, GGPO_NET_DEFAULT_PORT);
    g_online_cfg.input_delay = clampi(g_online_cfg.input_delay, 0, GGPO_NET_MAX_INPUT_DELAY);
    g_online_cfg.max_frame_advantage = clampi(g_online_cfg.max_frame_advantage, 0, GGPO_NET_MAX_FRAME_ADVANTAGE_LIMIT);
    g_online_cfg.max_prediction = clampi(g_online_cfg.max_prediction, 1, GGPO_NET_MAX_PREDICTION_LIMIT);
    g_online_cfg.correction_enabled = 1;
    g_online_cfg.p2p_enabled = 1;
    g_online_cfg.relay_fallback = 0;
    g_online_cfg.sim_loss = 0;
    g_online_cfg.sim_min_delay = 0;
    g_online_cfg.sim_max_delay = 0;
    g_online_cfg.challenge_notifications = g_online_cfg.challenge_notifications ? 1 : 0;
    if (!g_online_cfg.server_host[0]) safe_copy(g_online_cfg.server_host, sizeof(g_online_cfg.server_host), ONLINE_DEFAULT_SERVER_HOST);
    if (!g_online_cfg.peer_host[0]) safe_copy(g_online_cfg.peer_host, sizeof(g_online_cfg.peer_host), "127.0.0.1");
}

static void online_hub_add_friend(const char* name, const char* host, uint16_t port, int blocked) {
    OnlineFriend* f;
    if (!name || !name[0] || g_online_friend_count >= ONLINE_HUB_MAX_FRIENDS) return;
    f = &g_online_friends[g_online_friend_count++];
    memset(f, 0, sizeof(*f));
    safe_copy(f->name, sizeof(f->name), name);
    safe_copy(f->host, sizeof(f->host), (host && host[0]) ? host : g_online_cfg.peer_host);
    f->port = port ? port : g_online_cfg.peer_port;
    f->blocked = blocked ? 1 : 0;
}

static int online_hub_parse_host_port(const char* src, char* out_host, size_t host_sz, uint16_t* out_port, uint16_t fallback_port) {
    char tmp[ONLINE_HUB_TEXT_MAX * 2];
    char* s;
    char* colon;
    long port;
    if (!src || !src[0] || !out_host || host_sz == 0) return 0;
    safe_copy(tmp, sizeof(tmp), src);
    s = trim_ws(tmp);
    colon = strrchr(s, ':');
    if (colon && colon[1]) {
        *colon = '\0';
        if (!online_parse_long_range(colon + 1, 0, 65535, &port)) return 0;
        safe_copy(out_host, host_sz, trim_ws(s));
        if (out_port) *out_port = (uint16_t)port;
    } else {
        safe_copy(out_host, host_sz, s);
        if (out_port) *out_port = fallback_port;
    }
    return out_host[0] != '\0';
}

static int online_hub_add_friend_spec(const char* spec) {
    char tmp[ONLINE_HUB_TEXT_MAX * 2];
    char name[48];
    char host[ONLINE_HUB_TEXT_MAX];
    uint16_t port = g_online_cfg.peer_port;
    char* s;
    char* sep;
    if (!spec || !spec[0]) return 0;
    safe_copy(tmp, sizeof(tmp), spec);
    s = trim_ws(tmp);
    if (!s[0]) return 0;

    sep = strchr(s, '@');
    if (!sep) {
        sep = s;
        while (*sep && !isspace((unsigned char)*sep)) sep++;
    }
    if (!sep || !*sep) {
        safe_copy(name, sizeof(name), s);
        safe_copy(host, sizeof(host), g_online_cfg.peer_host);
        port = g_online_cfg.peer_port;
    } else {
        *sep = '\0';
        safe_copy(name, sizeof(name), trim_ws(s));
        sep++;
        while (*sep && isspace((unsigned char)*sep)) sep++;
        if (!online_hub_parse_host_port(sep, host, sizeof(host), &port, g_online_cfg.peer_port)) {
            return 0;
        }
    }
    if (!name[0]) return 0;
    online_hub_add_friend(name, host, port, 0);
    return 1;
}

static void online_hub_load(void) {
    FILE* f;
    char line[512];
    if (g_online_loaded) return;
    g_online_loaded = 1;
    online_hub_defaults();
    g_online_friend_count = 0;
    g_online_request_count = 0;
    g_online_challenge_count = 0;

    f = fopen(ONLINE_HUB_CFG_PATH, "r");
    if (!f) {
        online_hub_clamp_config();
        return;
    }

    while (fgets(line, sizeof(line), f)) {
        char* key;
        char* value;
        long parsed = 0;
        console_strip_crlf(line);
        key = trim_ws(line);
        if (!key[0] || key[0] == '#') continue;
        value = strchr(key, '=');
        if (!value) continue;
        *value++ = '\0';
        key = trim_ws(key);
        value = trim_ws(value);

        if (_stricmp(key, "username") == 0) safe_copy(g_online_cfg.username, sizeof(g_online_cfg.username), value);
        else if (_stricmp(key, "server_host") == 0) safe_copy(g_online_cfg.server_host, sizeof(g_online_cfg.server_host), value);
        else if (_stricmp(key, "server_port") == 0 && online_parse_long_range(value, 0, 65535, &parsed)) g_online_cfg.server_port = (uint16_t)parsed;
        else if (_stricmp(key, "peer_host") == 0) safe_copy(g_online_cfg.peer_host, sizeof(g_online_cfg.peer_host), value);
        else if (_stricmp(key, "peer_port") == 0 && online_parse_long_range(value, 0, 65535, &parsed)) g_online_cfg.peer_port = (uint16_t)parsed;
        else if (_stricmp(key, "local_port") == 0 && online_parse_long_range(value, 0, 65535, &parsed)) g_online_cfg.local_port = (uint16_t)parsed;
        else if (_stricmp(key, "challenge_notifications") == 0 && online_parse_long_range(value, 0, 1, &parsed)) g_online_cfg.challenge_notifications = (int)parsed;
        else if (_stricmp(key, "friend") == 0) {
            /* v1 local peer-address friends are ignored; friends now live on the server. */
#if 0
            char friend_buf[256];
            char* p;
            char* name;
            char* host;
            char* port_s;
            char* blocked_s;
            uint16_t port = GGPO_NET_DEFAULT_PORT;
            int blocked = 0;
            safe_copy(friend_buf, sizeof(friend_buf), value);
            p = friend_buf;
            name = p;
            host = strchr(p, '|');
            if (!host) continue;
            *host++ = '\0';
            port_s = strchr(host, '|');
            if (!port_s) continue;
            *port_s++ = '\0';
            blocked_s = strchr(port_s, '|');
            if (blocked_s) *blocked_s++ = '\0';
            if (online_parse_long_range(port_s, 0, 65535, &parsed)) port = (uint16_t)parsed;
            if (blocked_s && online_parse_long_range(blocked_s, 0, 1, &parsed)) blocked = (int)parsed;
            online_hub_add_friend(trim_ws(name), trim_ws(host), port, blocked);
#endif
        }
    }
    fclose(f);
    online_hub_clamp_config();
    online_hub_apply_net_settings();
}

static void online_hub_save(void) {
    FILE* f;
    CreateDirectoryA("mods", NULL);
    online_hub_clamp_config();
    f = fopen(ONLINE_HUB_CFG_PATH, "w");
    if (!f) {
        online_hub_set_status("Could not save online settings.");
        return;
    }
    fprintf(f, "# Eggnogg+ built-in online hub config v1\n");
    fprintf(f, "username=%s\n", g_online_cfg.username);
    fprintf(f, "server_host=%s\n", g_online_cfg.server_host);
    fprintf(f, "server_port=%u\n", (unsigned int)g_online_cfg.server_port);
    fprintf(f, "local_port=%u\n", (unsigned int)g_online_cfg.local_port);
    fprintf(f, "challenge_notifications=%d\n", g_online_cfg.challenge_notifications ? 1 : 0);
    fclose(f);
}

static void online_hub_apply_net_settings(void) {
    online_hub_clamp_config();
    (void)ggpo_net_set_input_delay((uint32_t)g_online_cfg.input_delay);
    (void)ggpo_net_set_max_frame_advantage((uint32_t)g_online_cfg.max_frame_advantage);
    (void)ggpo_net_set_max_prediction((uint32_t)g_online_cfg.max_prediction);
    (void)ggpo_net_set_correction_enabled(g_online_cfg.correction_enabled);
    (void)ggpo_net_set_network_sim((uint32_t)g_online_cfg.sim_loss,
                                   (uint32_t)g_online_cfg.sim_min_delay,
                                   (uint32_t)g_online_cfg.sim_max_delay);
}

static const char* online_server_state_text(void) {
    if (g_online_server_state == ONLINE_SERVER_CONNECTED) return g_online_authed ? "online" : "connected";
    if (g_online_server_state == ONLINE_SERVER_CONNECTING) return "connecting";
    return "offline";
}

static void online_json_escape(char* out, size_t out_sz, const char* s) {
    size_t pos = 0;
    if (!out || out_sz == 0) return;
    out[0] = '\0';
    for (; s && *s && pos + 1 < out_sz; s++) {
        unsigned char ch = (unsigned char)*s;
        const char* esc = NULL;
        switch (ch) {
            case '\\': esc = "\\\\"; break;
            case '"': esc = "\\\""; break;
            case '\n': esc = "\\n"; break;
            case '\r': esc = "\\r"; break;
            case '\t': esc = "\\t"; break;
            default: break;
        }
        if (esc) {
            size_t n = strlen(esc);
            if (pos + n >= out_sz) break;
            memcpy(out + pos, esc, n);
            pos += n;
        } else if (ch >= 0x20) {
            out[pos++] = (char)ch;
        }
    }
    out[pos] = '\0';
}

static const char* online_json_find_key(const char* json, const char* key) {
    char needle[96];
    const char* p;
    if (!json || !key || !key[0]) return NULL;
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    p = strstr(json, needle);
    if (!p) return NULL;
    p += strlen(needle);
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != ':') return NULL;
    p++;
    while (*p && isspace((unsigned char)*p)) p++;
    return p;
}

static int online_json_get_string(const char* json, const char* key, char* out, size_t out_sz) {
    const char* p = online_json_find_key(json, key);
    size_t pos = 0;
    if (!out || out_sz == 0) return 0;
    out[0] = '\0';
    if (!p || *p != '"') return 0;
    p++;
    while (*p && *p != '"' && pos + 1 < out_sz) {
        if (*p == '\\' && p[1]) {
            p++;
            switch (*p) {
                case 'n': out[pos++] = '\n'; break;
                case 'r': out[pos++] = '\r'; break;
                case 't': out[pos++] = '\t'; break;
                default: out[pos++] = *p; break;
            }
            p++;
        } else {
            out[pos++] = *p++;
        }
    }
    out[pos] = '\0';
    return 1;
}

static int online_json_get_int(const char* json, const char* key, int* out) {
    const char* p = online_json_find_key(json, key);
    char* end = NULL;
    long value;
    if (!p) return 0;
    value = strtol(p, &end, 10);
    if (end == p) return 0;
    if (out) *out = (int)value;
    return 1;
}

static int online_server_send_raw(const char* line) {
    int len;
    int sent;
    if (g_online_server_state != ONLINE_SERVER_CONNECTED || g_online_server_slot < 0 || !line) return 0;
    len = (int)strlen(line);
    sent = net_send(g_online_server_slot, line, len);
    if (sent < 0) {
        online_server_disconnect("server send failed");
        return 0;
    }
    return sent == len;
}

static void online_server_send_map_manifest(void) {
    static char maps_json[49152];
    static char line[53248];
    char lan_host[64];
    char lan_json[96];
    int needed;
    needed = custom_maps_build_manifest_json(maps_json, sizeof(maps_json));
    if (needed >= (int)sizeof(maps_json)) {
        online_hub_set_status("Map manifest too large; only part was sent.");
    }
    lan_host[0] = '\0';
    lan_json[0] = '\0';
    if (net_local_ipv4(lan_host, sizeof(lan_host)) && lan_host[0]) {
        online_json_escape(lan_json, sizeof(lan_json), lan_host);
    }
    snprintf(line, sizeof(line), "{\"type\":\"map_manifest\",\"p2p_port\":%u,\"lan_host\":\"%s\",\"route_version\":2,\"maps\":%s}\n",
             (unsigned int)g_online_cfg.local_port,
             lan_json,
             maps_json);
    online_server_send_raw(line);
}

static void online_server_send_auth(int register_account) {
    char user[128];
    char pass[192];
    char line[512];
    online_json_escape(user, sizeof(user), g_online_cfg.username);
    online_json_escape(pass, sizeof(pass), g_online_cfg.password);
    snprintf(line, sizeof(line), "{\"type\":\"%s\",\"username\":\"%s\",\"password\":\"%s\"}\n",
             register_account ? "register" : "login", user, pass);
    online_server_send_raw(line);
}

static void online_server_disconnect(const char* reason) {
    if (g_online_server_slot >= 0) net_close(g_online_server_slot);
    g_online_server_slot = -1;
    g_online_server_state = ONLINE_SERVER_DISCONNECTED;
    g_online_authed = 0;
    g_online_recv_len = 0;
    g_online_queue_mode = 0;
    if (reason && reason[0]) online_hub_set_status(reason);
}

static void online_server_connect(int register_account) {
    online_hub_save();
    online_server_disconnect(NULL);
    if (!g_online_cfg.username[0] || !g_online_cfg.password[0]) {
        online_hub_set_status("Enter username and password on Play.");
        return;
    }
    g_online_server_slot = net_connect(g_online_cfg.server_host, g_online_cfg.server_port);
    if (g_online_server_slot < 0) {
        online_hub_set_status("Could not start server connection.");
        return;
    }
    g_online_register_after_connect = register_account ? 1 : 0;
    g_online_server_state = ONLINE_SERVER_CONNECTING;
    online_hub_set_status("Connecting to online server...");
}

static void online_server_send_queue(const char* queue) {
    char line[128];
    if (!g_online_authed) {
        online_hub_set_status("Log in before joining a queue.");
        return;
    }
    snprintf(line, sizeof(line), "{\"type\":\"join_queue\",\"queue\":\"%s\"}\n", queue ? queue : "casual");
    online_server_send_raw(line);
    g_online_queue_mode = (queue && _stricmp(queue, "competitive") == 0) ? 2 : 1;
    online_hub_set_status(g_online_queue_mode == 2 ? "Joining competitive queue..." : "Joining casual queue...");
}

static void online_server_leave_queue(void) {
    online_server_send_raw("{\"type\":\"leave_queue\"}\n");
    g_online_queue_mode = 0;
    online_hub_set_status("Left queue.");
}

static void online_server_send_username_action(const char* type, const char* username) {
    char user[128];
    char line[256];
    if (!g_online_authed) {
        online_hub_set_status("Log in first.");
        return;
    }
    online_json_escape(user, sizeof(user), username ? username : "");
    snprintf(line, sizeof(line), "{\"type\":\"%s\",\"username\":\"%s\"}\n", type, user);
    online_server_send_raw(line);
}

static void online_server_send_challenge_action(const char* type, int id, const char* username) {
    char user[128];
    char line[256];
    if (!g_online_authed) {
        online_hub_set_status("Log in first.");
        return;
    }
    online_json_escape(user, sizeof(user), username ? username : "");
    snprintf(line, sizeof(line), "{\"type\":\"%s\",\"id\":%d,\"username\":\"%s\"}\n", type, id, user);
    online_server_send_raw(line);
}

static void online_server_send_match_end(OnlineMatchResult result) {
    char line[192];
    const char* text = "draw";
    if (result == ONLINE_MATCH_RESULT_WIN) text = "win";
    else if (result == ONLINE_MATCH_RESULT_LOSS) text = "loss";
    if (!g_online_authed || g_online_server_state != ONLINE_SERVER_CONNECTED) return;
    snprintf(line, sizeof(line), "{\"type\":\"match_end\",\"match_id\":%d,\"result\":\"%s\"}\n",
             g_online_active_match.match_id,
             text);
    online_server_send_raw(line);
}

static void online_result_prepare(OnlineMatchResult result, const char* status) {
    memset(&g_online_result, 0, sizeof(g_online_result));
    g_online_result.active = 1;
    g_online_result.selected = 0;
    g_online_result.result = result;
    g_online_result.toast_age = 0;
    g_online_result.toast_lifetime = ONLINE_RESULT_TOAST_FRAMES;
    g_online_result.reported = g_online_active_match.result_reported;
    g_online_result.server_confirmed = 0;
    g_online_result.competitive = g_online_active_match.competitive;
    g_online_result.queue_mode = g_online_active_match.queue_mode;
    g_online_result.elo_before = g_online_public_elo;
    g_online_result.elo_after = g_online_public_elo;
    safe_copy(g_online_result.opponent, sizeof(g_online_result.opponent), g_online_active_match.opponent);
    safe_copy(g_online_result.map_label, sizeof(g_online_result.map_label), g_online_active_match.map_label);
    safe_copy(g_online_result.status, sizeof(g_online_result.status), status ? status : "Waiting for server result...");
}

static void online_open_result_screen(void) {
    g_online_queue_mode = 0;
    g_online_capture_active = 0;
    g_online_context_active = 0;
}

static void online_active_match_begin_from_pending(void) {
    online_clear_waterfall_audio_state("match begin");
    memset(&g_online_active_match, 0, sizeof(g_online_active_match));
    g_online_active_match.active = 1;
    g_online_active_match.match_id = g_online_pending_match.match_id;
    g_online_active_match.local_player = ggpo_net_local_player();
    if (g_online_active_match.local_player < 0 || g_online_active_match.local_player > 1) {
        g_online_active_match.local_player = clampi(g_online_pending_match.role, 0, 1);
    }
    g_online_active_match.competitive = g_online_pending_match.competitive;
    g_online_active_match.queue_mode = g_online_pending_match.queue_mode;
    g_online_active_match.p2p_probe_cooldown = 0;
    safe_copy(g_online_active_match.p2p_token, sizeof(g_online_active_match.p2p_token), g_online_pending_match.p2p_token);
    safe_copy(g_online_active_match.opponent, sizeof(g_online_active_match.opponent), g_online_pending_match.opponent);
    safe_copy(g_online_active_match.map_label, sizeof(g_online_active_match.map_label), g_online_pending_match.map_label);
}

static void online_finish_active_match(OnlineMatchResult result, const char* status, int send_report) {
    if (!g_online_active_match.active) return;
    g_online_active_match.result = result;
    if (send_report && !g_online_active_match.result_reported) {
        g_online_active_match.result_reported = 1;
        online_server_send_match_end(result);
    }
    if (ggpo_net_active()) stop_ggpo_net("online match complete");
    memset(&g_online_pending_match, 0, sizeof(g_online_pending_match));
    online_result_prepare(result, status);
    online_open_result_screen();
}

static void online_clear_match_state(void) {
    memset(&g_online_pending_match, 0, sizeof(g_online_pending_match));
    memset(&g_online_active_match, 0, sizeof(g_online_active_match));
}

static OnlineMatchResult online_match_result_from_text(const char* text) {
    if (!text) return ONLINE_MATCH_RESULT_NONE;
    if (_stricmp(text, "win") == 0) return ONLINE_MATCH_RESULT_WIN;
    if (_stricmp(text, "loss") == 0) return ONLINE_MATCH_RESULT_LOSS;
    if (_stricmp(text, "draw") == 0) return ONLINE_MATCH_RESULT_DRAW;
    return ONLINE_MATCH_RESULT_NONE;
}

static int online_pending_match_is_host(void) {
    if (_stricmp(g_online_pending_match.p2p_role, "host") == 0) return 1;
    if (_stricmp(g_online_pending_match.p2p_role, "join") == 0) return 0;
    return g_online_pending_match.role == 0;
}

static void online_normalize_pending_match_ports(void) {
    if (g_online_pending_match.local_port < 0 || g_online_pending_match.local_port > 65535) {
        g_online_pending_match.local_port = 0;
    }
    if (g_online_pending_match.peer_port < 0 || g_online_pending_match.peer_port > 65535) {
        g_online_pending_match.peer_port = 0;
    }
    if (online_pending_match_is_host() && !g_online_pending_match.p2p_role[0]) {
        safe_copy(g_online_pending_match.p2p_role, sizeof(g_online_pending_match.p2p_role), "host");
        return;
    }
    if (!g_online_pending_match.p2p_role[0]) {
        safe_copy(g_online_pending_match.p2p_role, sizeof(g_online_pending_match.p2p_role), "join");
    }
}

static void online_ensure_native_game_started(void) {
    if (g_game_started && *g_game_started) return;
    if (p_game_start) {
        p_game_start();
    } else if (g_game_started) {
        *g_game_started = 1;
    }
}

static void online_launch_pending_match_to_game(void) {
    if (!g_online_pending_match.active) return;
    online_clear_waterfall_audio_state("match launch");
    if (g_hook_map_selector) {
        *g_hook_map_selector = g_online_pending_match.selector;
    }
    if (g_online_pending_match.seed) {
        (void)lua_manager_game_set_rng_seed(g_online_pending_match.seed);
        if (g_native_seed) *g_native_seed = g_online_pending_match.seed;
        if (g_native_mrand_seed) *g_native_mrand_seed = g_online_pending_match.seed;
    }
    memset(&g_online_active_match, 0, sizeof(g_online_active_match));
    if (p_game_reset) {
        p_game_reset();
    }
    g_online_pending_match.launch_stage = 4;
    g_online_pending_match.launch_delay_frames = 2;
    g_online_pending_match.p2p_started = 0;
    if (p_state_switch) {
        p_state_switch((void*)(uintptr_t)ADDR_GAME_STATE);
    }
}

static void online_server_begin_pending_match(const char* line) {
    char text[128];
    char queue[32];
    int value = 0;
    memset(&g_online_pending_match, 0, sizeof(g_online_pending_match));
    g_online_pending_match.active = 1;
    g_online_pending_match.launch_stage = 1;
    g_online_pending_match.selector = 0;
    g_online_pending_match.input_delay = g_online_cfg.input_delay;
    g_online_pending_match.local_port = g_online_cfg.local_port;

    if (online_json_get_int(line, "match_id", &value)) g_online_pending_match.match_id = value;
    if (online_json_get_int(line, "role", &value)) g_online_pending_match.role = value;
    if (online_json_get_int(line, "map_sel", &value)) g_online_pending_match.selector = value;
    if (online_json_get_int(line, "local_port", &value)) g_online_pending_match.local_port = value;
    if (online_json_get_int(line, "peer_port", &value)) g_online_pending_match.peer_port = value;
    if (online_json_get_int(line, "input_delay", &value)) g_online_pending_match.input_delay = value;
    if (online_json_get_int(line, "seed", &value)) g_online_pending_match.seed = (unsigned int)value;
    online_json_get_string(line, "p2p_role", g_online_pending_match.p2p_role, sizeof(g_online_pending_match.p2p_role));
    online_json_get_string(line, "peer_host", g_online_pending_match.peer_host, sizeof(g_online_pending_match.peer_host));
    online_json_get_string(line, "p2p_token", g_online_pending_match.p2p_token, sizeof(g_online_pending_match.p2p_token));
    online_json_get_string(line, "opponent", g_online_pending_match.opponent, sizeof(g_online_pending_match.opponent));
    online_json_get_string(line, "map_key", g_online_pending_match.map_key, sizeof(g_online_pending_match.map_key));
    online_json_get_string(line, "map_label", g_online_pending_match.map_label, sizeof(g_online_pending_match.map_label));
    queue[0] = '\0';
    online_json_get_string(line, "queue", queue, sizeof(queue));
    if (_stricmp(queue, "competitive") == 0) {
        g_online_pending_match.competitive = 1;
        g_online_pending_match.queue_mode = 2;
    } else if (_stricmp(queue, "casual") == 0) {
        g_online_pending_match.queue_mode = 1;
    }

    if (g_online_pending_match.map_key[0]) {
        int selector = 0;
        if (custom_maps_selector_for_key(g_online_pending_match.map_key, &selector)) {
            g_online_pending_match.selector = selector;
        }
    }
    online_normalize_pending_match_ports();
    online_hub_apply_net_settings();
    (void)ggpo_net_set_input_delay((uint32_t)clampi(g_online_pending_match.input_delay, 0, GGPO_NET_MAX_INPUT_DELAY));
    snprintf(text, sizeof(text), "Match found: %s on %s",
             g_online_pending_match.opponent[0] ? g_online_pending_match.opponent : "opponent",
             g_online_pending_match.map_label[0] ? g_online_pending_match.map_label : "selected map");
    online_hub_set_status(text);
    g_online_pending_match.launch_countdown_frames = ONLINE_MATCH_COUNTDOWN_FRAMES;
    g_online_pending_match.launch_stage = 0;
    if (!is_online_hub_state_active()) {
        online_hub_open();
    }
}

static void online_apply_p2p_peer(const char* line) {
    char host[ONLINE_HUB_TEXT_MAX];
    char err[256];
    int match_id = 0;
    int port = 0;
    int applies = 0;
    if (!online_json_get_int(line, "match_id", &match_id)) return;
    if (!online_json_get_int(line, "peer_port", &port)) return;
    host[0] = '\0';
    online_json_get_string(line, "peer_host", host, sizeof(host));
    if (match_id <= 0 || port <= 0 || port > 65535 || !host[0]) return;

    if (g_online_pending_match.active && g_online_pending_match.match_id == match_id) {
        safe_copy(g_online_pending_match.peer_host, sizeof(g_online_pending_match.peer_host), host);
        g_online_pending_match.peer_port = port;
        applies = 1;
    }
    if (g_online_active_match.active && g_online_active_match.match_id == match_id) {
        applies = 1;
    }
    if (!applies) return;

    if (ggpo_net_active()) {
        err[0] = '\0';
        if (ggpo_net_set_peer(host, (uint16_t)port, err, sizeof(err))) {
            online_hub_set_status("");
        } else {
            char status[192];
            snprintf(status, sizeof(status), "P2P endpoint failed: %s", err[0] ? err : "unknown error");
            online_hub_set_status(status);
            LOG_ERROR("online.p2p: failed to set peer %s:%d match=%d (%s)",
                      host,
                      port,
                      match_id,
                      err[0] ? err : "unknown error");
        }
    }
}

static void online_pump_p2p_probe(void) {
    char err[256];
    if (!g_online_active_match.active) return;
    /* Keep probing until the session connects. This keeps NAT mappings warm and
     * lets the server resend a changed observed endpoint during hole punching. */
    if (!ggpo_net_active() || ggpo_net_connected()) return;
    if (!g_online_active_match.p2p_token[0] || !g_online_cfg.username[0]) return;
    if (g_online_server_state != ONLINE_SERVER_CONNECTED || g_online_server_slot < 0) return;
    if (g_online_active_match.p2p_probe_cooldown > 0) {
        g_online_active_match.p2p_probe_cooldown--;
        return;
    }
    g_online_active_match.p2p_probe_cooldown = 12;
    err[0] = '\0';
    if (ggpo_net_send_server_probe(g_online_cfg.server_host,
                                   g_online_cfg.server_port,
                                   g_online_active_match.match_id,
                                   g_online_cfg.username,
                                   g_online_active_match.p2p_token,
                                   err,
                                   sizeof(err))) {
        if (!g_online_active_match.p2p_probe_logged) {
            g_online_active_match.p2p_probe_logged = 1;
            LOG_INFO("online.p2p: sent UDP discovery probe match=%d server=%s:%u local_udp=%u",
                     g_online_active_match.match_id,
                     g_online_cfg.server_host,
                     (unsigned int)g_online_cfg.server_port,
                     (unsigned int)ggpo_net_local_port());
        }
    } else {
        if (!g_online_active_match.p2p_probe_warned) {
            g_online_active_match.p2p_probe_warned = 1;
            LOG_WARN("online.p2p: server UDP probe failed match=%d (%s)",
                     g_online_active_match.match_id,
                     err[0] ? err : "unknown error");
        }
    }
}

static void online_server_handle_line(const char* line) {
    char type[64];
    char text[256];
    int value = 0;
    if (!online_json_get_string(line, "type", type, sizeof(type))) return;

    if (_stricmp(type, "auth_ok") == 0) {
        g_online_authed = 1;
        if (online_json_get_string(line, "username", text, sizeof(text))) {
            safe_copy(g_online_cfg.username, sizeof(g_online_cfg.username), text);
        }
        if (online_json_get_int(line, "elo", &value)) g_online_public_elo = value;
        online_server_send_map_manifest();
        online_hub_set_status("Logged in.");
        online_hub_save();
    } else if (_stricmp(type, "auth_fail") == 0) {
        online_json_get_string(line, "reason", text, sizeof(text));
        snprintf(g_online_status, sizeof(g_online_status), "Login failed: %s", text[0] ? text : "?");
        g_online_authed = 0;
    } else if (_stricmp(type, "error") == 0) {
        online_json_get_string(line, "message", text, sizeof(text));
        online_hub_set_status(text[0] ? text : "Server error.");
    } else if (_stricmp(type, "queue_update") == 0) {
        if (online_json_get_int(line, "casual", &value)) g_online_queue_casual_count = value;
        if (online_json_get_int(line, "competitive", &value)) g_online_queue_competitive_count = value;
    } else if (_stricmp(type, "queue_left") == 0) {
        g_online_queue_mode = 0;
    } else if (_stricmp(type, "p2p_peer") == 0) {
        online_apply_p2p_peer(line);
    } else if (_stricmp(type, "friend_snapshot_begin") == 0) {
        g_online_friend_count = 0;
        g_online_request_count = 0;
        g_online_challenge_count = 0;
        g_online_context_active = 0;
    } else if (_stricmp(type, "friend") == 0) {
        if (g_online_friend_count < ONLINE_HUB_MAX_FRIENDS &&
            online_json_get_string(line, "username", text, sizeof(text))) {
            OnlineFriend* fr = &g_online_friends[g_online_friend_count++];
            memset(fr, 0, sizeof(*fr));
            safe_copy(fr->name, sizeof(fr->name), text);
            if (online_json_get_int(line, "elo", &value)) fr->elo = value;
            if (online_json_get_int(line, "online", &value)) fr->online = value ? 1 : 0;
        }
    } else if (_stricmp(type, "friend_request") == 0) {
        if (g_online_request_count < ONLINE_HUB_MAX_INBOX &&
            online_json_get_string(line, "from", text, sizeof(text))) {
            OnlineFriendRequest* req = &g_online_requests[g_online_request_count++];
            memset(req, 0, sizeof(*req));
            safe_copy(req->name, sizeof(req->name), text);
            if (online_json_get_int(line, "elo", &value)) req->elo = value;
        }
    } else if (_stricmp(type, "challenge") == 0) {
        if (g_online_challenge_count < ONLINE_HUB_MAX_INBOX &&
            online_json_get_string(line, "from", text, sizeof(text))) {
            OnlineChallenge* ch = &g_online_challenges[g_online_challenge_count++];
            memset(ch, 0, sizeof(*ch));
            safe_copy(ch->from, sizeof(ch->from), text);
            if (online_json_get_int(line, "id", &value)) ch->id = value;
            if (online_json_get_int(line, "elo", &value)) ch->elo = value;
            if (online_json_get_int(line, "expires_in", &value)) ch->expires_in = value;
            online_challenge_toast_show(ch->from, ch->id, ch->elo, ch->expires_in);
        }
    } else if (_stricmp(type, "friend_request_sent") == 0) {
        online_hub_set_status("Friend request sent.");
    } else if (_stricmp(type, "challenge_sent") == 0) {
        int id = 0;
        int expires_in = 300;
        text[0] = '\0';
        online_json_get_string(line, "username", text, sizeof(text));
        online_json_get_int(line, "id", &id);
        online_json_get_int(line, "expires_in", &expires_in);
        online_sent_challenge_add(text, id, expires_in);
        online_hub_set_status("");
    } else if (_stricmp(type, "challenge_accepted") == 0) {
        int id = 0;
        text[0] = '\0';
        online_json_get_string(line, "username", text, sizeof(text));
        online_json_get_int(line, "id", &id);
        online_sent_challenge_remove(text, id);
        online_challenge_toast_clear(id, text);
    } else if (_stricmp(type, "challenge_declined") == 0) {
        int id = 0;
        text[0] = '\0';
        online_json_get_string(line, "username", text, sizeof(text));
        online_json_get_int(line, "id", &id);
        online_sent_challenge_remove(text, id);
        online_challenge_toast_clear(id, text);
        online_hub_set_status("Challenge declined.");
    } else if (_stricmp(type, "challenge_expired") == 0) {
        int id = 0;
        text[0] = '\0';
        if (!online_json_get_string(line, "username", text, sizeof(text))) {
            online_json_get_string(line, "from", text, sizeof(text));
        }
        online_json_get_int(line, "id", &id);
        online_sent_challenge_remove(text, id);
        online_challenge_toast_clear(id, text);
        online_hub_set_status("Challenge expired.");
    } else if (_stricmp(type, "rating_update") == 0) {
        if (online_json_get_int(line, "elo", &value)) {
            g_online_public_elo = value;
            if (g_online_result.active) {
                g_online_result.elo_after = value;
                g_online_result.elo_delta_valid = 1;
            }
        }
        if (g_online_result.active && online_json_get_int(line, "elo_before", &value)) {
            g_online_result.elo_before = value;
            g_online_result.elo_delta_valid = 1;
        }
    } else if (_stricmp(type, "match_found") == 0) {
        g_online_queue_mode = 0;
        online_server_begin_pending_match(line);
        online_sent_challenge_remove(g_online_pending_match.opponent, 0);
        online_challenge_toast_clear(0, g_online_pending_match.opponent);
    } else if (_stricmp(type, "match_report_ack") == 0) {
        if (g_online_result.active) {
            safe_copy(g_online_result.status, sizeof(g_online_result.status), "Result reported; waiting for opponent.");
        } else {
            online_hub_set_status("Result reported; waiting for opponent.");
        }
    } else if (_stricmp(type, "match_result") == 0) {
        OnlineMatchResult result;
        int got_elo = 0;
        int elo_value = 0;
        int got_elo_before = 0;
        int elo_before_value = 0;
        if (ggpo_net_active()) stop_ggpo_net("online server result");
        text[0] = '\0';
        online_json_get_string(line, "result", text, sizeof(text));
        result = online_match_result_from_text(text);
        if (result == ONLINE_MATCH_RESULT_NONE) result = g_online_active_match.result;
        if (result == ONLINE_MATCH_RESULT_NONE) result = ONLINE_MATCH_RESULT_DRAW;
        if (online_json_get_int(line, "competitive", &value)) g_online_active_match.competitive = value ? 1 : 0;
        if (online_json_get_int(line, "elo", &value)) {
            got_elo = 1;
            elo_value = value;
        }
        if (online_json_get_int(line, "elo_before", &value)) {
            got_elo_before = 1;
            elo_before_value = value;
        }
        if (!g_online_result.active) {
            if (!g_online_active_match.active) {
                g_online_active_match.active = 1;
                g_online_active_match.queue_mode = 0;
            }
            online_result_prepare(result, "Match complete.");
            online_open_result_screen();
        }
        g_online_result.result = result;
        g_online_result.toast_age = 0;
        if (g_online_result.toast_lifetime <= 0) g_online_result.toast_lifetime = ONLINE_RESULT_TOAST_FRAMES;
        g_online_result.server_confirmed = 1;
        g_online_result.reported = 1;
        g_online_result.competitive = g_online_active_match.competitive;
        if (got_elo_before) {
            g_online_result.elo_before = elo_before_value;
            g_online_result.elo_delta_valid = 1;
        }
        if (got_elo) {
            g_online_public_elo = elo_value;
            g_online_result.elo_after = elo_value;
            g_online_result.elo_delta_valid = 1;
        }
        safe_copy(g_online_result.status, sizeof(g_online_result.status), "Match complete.");
        memset(&g_online_pending_match, 0, sizeof(g_online_pending_match));
        memset(&g_online_active_match, 0, sizeof(g_online_active_match));
    } else if (_stricmp(type, "match_end") == 0) {
        if (ggpo_net_active()) stop_ggpo_net("online server");
        online_clear_match_state();
        online_hub_set_status("Online match ended.");
    }
    if (is_online_hub_state_active() || is_online_result_state_active()) {
        online_hub_rebuild_rows();
    }
}

static void online_server_update(void) {
    online_pump_p2p_probe();
    if (g_online_server_state == ONLINE_SERVER_CONNECTING) {
        int r = net_check_connect(g_online_server_slot);
        if (r == 1) {
            g_online_server_state = ONLINE_SERVER_CONNECTED;
            online_hub_set_status("Connected. Authenticating...");
            online_server_send_auth(g_online_register_after_connect);
        } else if (r < 0) {
            online_server_disconnect("Server connection failed.");
        }
        return;
    }
    if (g_online_server_state != ONLINE_SERVER_CONNECTED || g_online_server_slot < 0) return;

    while (1) {
        char chunk[4096];
        int got = net_recv(g_online_server_slot, chunk, (int)sizeof(chunk));
        if (got < 0) {
            online_server_disconnect("Server disconnected.");
            return;
        }
        if (got == 0) break;
        if (g_online_recv_len + (size_t)got >= sizeof(g_online_recv_buf)) {
            g_online_recv_len = 0;
            online_hub_set_status("Server message too large; dropped buffer.");
            continue;
        }
        memcpy(g_online_recv_buf + g_online_recv_len, chunk, (size_t)got);
        g_online_recv_len += (size_t)got;
        g_online_recv_buf[g_online_recv_len] = '\0';

        while (1) {
            char* nl = memchr(g_online_recv_buf, '\n', g_online_recv_len);
            size_t line_len;
            size_t consumed;
            char line[8192];
            if (!nl) break;
            line_len = (size_t)(nl - g_online_recv_buf);
            consumed = line_len + 1;
            if (line_len >= sizeof(line)) line_len = sizeof(line) - 1;
            memcpy(line, g_online_recv_buf, line_len);
            line[line_len] = '\0';
            console_strip_crlf(line);
            memmove(g_online_recv_buf, g_online_recv_buf + consumed, g_online_recv_len - consumed);
            g_online_recv_len -= consumed;
            g_online_recv_buf[g_online_recv_len] = '\0';
            if (line[0]) online_server_handle_line(line);
        }
    }
}

static void online_sent_challenge_prune(void) {
    uint32_t now_ms = (uint32_t)GetTickCount();
    int out = 0;
    for (int i = 0; i < g_online_sent_challenge_count; i++) {
        OnlineSentChallenge* ch = &g_online_sent_challenges[i];
        if (!ch->username[0]) continue;
        if ((int32_t)(ch->expires_ms - now_ms) <= 0) continue;
        if (out != i) g_online_sent_challenges[out] = *ch;
        out++;
    }
    g_online_sent_challenge_count = out;
}

static int online_sent_challenge_index(const char* username, int id) {
    if (id > 0) {
        for (int i = 0; i < g_online_sent_challenge_count; i++) {
            if (g_online_sent_challenges[i].id == id) return i;
        }
    }
    if (!username || !username[0]) return -1;
    for (int i = 0; i < g_online_sent_challenge_count; i++) {
        if (_stricmp(g_online_sent_challenges[i].username, username) == 0) return i;
    }
    return -1;
}

static int online_sent_challenge_pending(const char* username) {
    online_sent_challenge_prune();
    return online_sent_challenge_index(username, 0) >= 0;
}

static void online_sent_challenge_add(const char* username, int id, int expires_in) {
    int idx;
    if (!username || !username[0]) return;
    online_sent_challenge_prune();
    idx = online_sent_challenge_index(username, id);
    if (idx < 0) {
        if (g_online_sent_challenge_count >= ONLINE_HUB_MAX_INBOX) return;
        idx = g_online_sent_challenge_count++;
        memset(&g_online_sent_challenges[idx], 0, sizeof(g_online_sent_challenges[idx]));
    }
    safe_copy(g_online_sent_challenges[idx].username, sizeof(g_online_sent_challenges[idx].username), username);
    g_online_sent_challenges[idx].id = id;
    if (expires_in <= 0) expires_in = 300;
    g_online_sent_challenges[idx].expires_ms = (uint32_t)GetTickCount() + (uint32_t)expires_in * 1000u;
}

static void online_sent_challenge_remove(const char* username, int id) {
    int idx;
    online_sent_challenge_prune();
    idx = online_sent_challenge_index(username, id);
    if (idx < 0) return;
    for (int i = idx; i + 1 < g_online_sent_challenge_count; i++) {
        g_online_sent_challenges[i] = g_online_sent_challenges[i + 1];
    }
    g_online_sent_challenge_count--;
    if (g_online_sent_challenge_count < 0) g_online_sent_challenge_count = 0;
}

static void online_rows_clear(void) {
    g_online_row_count = 0;
}

static void online_rows_add(OnlineHubRowKind kind, int selectable, int id, int aux, const char* left, const char* right) {
    OnlineHubRow* row;
    if (g_online_row_count >= ONLINE_HUB_MAX_ROWS) return;
    row = &g_online_rows[g_online_row_count++];
    memset(row, 0, sizeof(*row));
    row->kind = kind;
    row->selectable = selectable;
    row->id = id;
    row->aux = aux;
    safe_copy(row->left, sizeof(row->left), left ? left : "");
    safe_copy(row->right, sizeof(row->right), right ? right : "");
}

static void online_format_bool(char* out, size_t out_sz, int enabled) {
    safe_copy(out, out_sz, enabled ? "ON" : "OFF");
}

static void online_format_setting_value(OnlineHubSetting setting, char* out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    switch (setting) {
        case ONLINE_SETTING_USERNAME: snprintf(out, out_sz, "%s", g_online_cfg.username); break;
        case ONLINE_SETTING_PASSWORD: snprintf(out, out_sz, "%s", g_online_cfg.password[0] ? "********" : ""); break;
        case ONLINE_SETTING_SERVER_HOST: snprintf(out, out_sz, "%s:%u", g_online_cfg.server_host, (unsigned int)g_online_cfg.server_port); break;
        case ONLINE_SETTING_SERVER_PORT: snprintf(out, out_sz, "%u", (unsigned int)g_online_cfg.server_port); break;
        case ONLINE_SETTING_PEER_HOST: snprintf(out, out_sz, "%s", g_online_cfg.peer_host); break;
        case ONLINE_SETTING_PEER_PORT: snprintf(out, out_sz, "%u", (unsigned int)g_online_cfg.peer_port); break;
        case ONLINE_SETTING_LOCAL_PORT:
            if (g_online_cfg.local_port) snprintf(out, out_sz, "%u", (unsigned int)g_online_cfg.local_port);
            else snprintf(out, out_sz, "Auto");
            break;
        case ONLINE_SETTING_P2P_ENABLED: online_format_bool(out, out_sz, g_online_cfg.p2p_enabled); break;
        case ONLINE_SETTING_RELAY_FALLBACK: online_format_bool(out, out_sz, g_online_cfg.relay_fallback); break;
        case ONLINE_SETTING_INPUT_DELAY: snprintf(out, out_sz, "%d", g_online_cfg.input_delay); break;
        case ONLINE_SETTING_FRAME_ADVANTAGE: snprintf(out, out_sz, "%d", g_online_cfg.max_frame_advantage); break;
        case ONLINE_SETTING_MAX_PREDICTION: snprintf(out, out_sz, "%d", g_online_cfg.max_prediction); break;
        case ONLINE_SETTING_CORRECTION: online_format_bool(out, out_sz, g_online_cfg.correction_enabled); break;
        case ONLINE_SETTING_SIM_LOSS: snprintf(out, out_sz, "%d%%", g_online_cfg.sim_loss); break;
        case ONLINE_SETTING_SIM_MIN_DELAY: snprintf(out, out_sz, "%d", g_online_cfg.sim_min_delay); break;
        case ONLINE_SETTING_SIM_MAX_DELAY: snprintf(out, out_sz, "%d", g_online_cfg.sim_max_delay); break;
        case ONLINE_SETTING_CHALLENGE_NOTIFICATIONS: online_format_bool(out, out_sz, g_online_cfg.challenge_notifications); break;
        default: out[0] = '\0'; break;
    }
}

static int online_first_selectable(void) {
    for (int i = 0; i < g_online_row_count; i++) {
        if (g_online_rows[i].selectable) return i;
    }
    return -1;
}

static int online_last_selectable(void) {
    for (int i = g_online_row_count - 1; i >= 0; i--) {
        if (g_online_rows[i].selectable) return i;
    }
    return -1;
}

static int online_next_selectable(int start, int dir) {
    int i = start;
    while (1) {
        i += dir;
        if (i < 0 || i >= g_online_row_count) return -1;
        if (g_online_rows[i].selectable) return i;
    }
}

static float online_hub_ui_scale(void) {
    float w = p_mad_w ? p_mad_w() : BASE_UI_W;
    float h = p_mad_h ? p_mad_h() : BASE_UI_H;
    float sx = w / BASE_UI_W;
    float sy = h / BASE_UI_H;
    float s = (sx < sy) ? sx : sy;
    return clampf(s, 1.08f, 1.55f);
}

static void online_calc_layout(OnlineLayout* L) {
    float margin;
    if (!L) return;
    memset(L, 0, sizeof(*L));
    L->w = p_mad_w ? p_mad_w() : BASE_UI_W;
    L->h = p_mad_h ? p_mad_h() : BASE_UI_H;
    L->ui = online_hub_ui_scale();
    L->text_scale = L->ui;
    margin = 28.0f * L->ui;
    L->panel_w = clampf(860.0f * L->ui, L->w * 0.72f, L->w - margin * 2.0f);
    if (L->panel_w > L->w - margin * 2.0f) L->panel_w = L->w - margin * 2.0f;
    L->panel_h = clampf(560.0f * L->ui, L->h * 0.60f, L->h - margin * 2.0f);
    if (L->panel_h > L->h - margin * 2.0f) L->panel_h = L->h - margin * 2.0f;
    if (L->panel_w < 360.0f) L->panel_w = L->w - 20.0f;
    if (L->panel_h < 280.0f) L->panel_h = L->h - 20.0f;
    L->panel_x = (L->w - L->panel_w) * 0.5f;
    L->panel_y = (L->h - L->panel_h) * 0.5f;
    L->header_h = 78.0f * L->ui;
    L->footer_h = 50.0f * L->ui;
    L->content_x = L->panel_x + 28.0f * L->ui;
    L->content_y = L->panel_y + L->header_h + 28.0f * L->ui;
    L->content_w = L->panel_w - 56.0f * L->ui;
    L->content_h = L->panel_h - L->header_h - L->footer_h - 56.0f * L->ui;
    L->row_h = 48.0f * L->ui;
    if (L->row_h < 32.0f) L->row_h = 32.0f;
    L->list_top = L->content_y;
    L->list_bottom = L->panel_y + L->panel_h - L->footer_h - 20.0f * L->ui;
    L->left_x = L->content_x + 18.0f * L->ui;
    L->right_x = L->content_x + L->content_w - 18.0f * L->ui;
    L->center_x = L->panel_x + L->panel_w * 0.5f;
}

static int online_visible_rows_capacity(void) {
    OnlineLayout L;
    int cap;
    online_calc_layout(&L);
    cap = (int)((L.list_bottom - L.list_top) / L.row_h);
    if (cap < 4) cap = 4;
    return cap;
}

static void online_ensure_scroll_visible(void) {
    int cap = online_visible_rows_capacity();
    int max_scroll = (g_online_row_count > cap) ? (g_online_row_count - cap) : 0;
    int margin = (cap >= 8) ? 2 : 1;
    if (g_online_selected_row < 0 || g_online_selected_row >= g_online_row_count) {
        g_online_scroll_row = clampi(g_online_scroll_row, 0, max_scroll);
        return;
    }
    if (g_online_selected_row < g_online_scroll_row + margin) {
        g_online_scroll_row = g_online_selected_row - margin;
    } else if (g_online_selected_row > g_online_scroll_row + cap - margin - 1) {
        g_online_scroll_row = g_online_selected_row - (cap - margin - 1);
    }
    g_online_scroll_row = clampi(g_online_scroll_row, 0, max_scroll);
}

static void online_hub_rebuild_rows(void) {
    OnlineHubRowKind keep_kind = ONLINE_ROW_NONE;
    int keep_id = 0;
    int keep_aux = 0;
    if (!g_online_authed && g_online_tab != ONLINE_TAB_PLAY) {
        g_online_tab = ONLINE_TAB_PLAY;
    }
    if (g_online_selected_row >= 0 && g_online_selected_row < g_online_row_count) {
        keep_kind = g_online_rows[g_online_selected_row].kind;
        keep_id = g_online_rows[g_online_selected_row].id;
        keep_aux = g_online_rows[g_online_selected_row].aux;
    }

    online_rows_clear();
    if (g_online_tab == ONLINE_TAB_PLAY) {
        char line[192];
        snprintf(line, sizeof(line), "%s  Elo %d",
                 g_online_cfg.username[0] ? g_online_cfg.username : "not signed in",
                 g_online_public_elo);
        online_rows_add(ONLINE_ROW_INFO, 0, 0, 0, "Account", line);
        if (!g_online_authed) {
            char value[192];
            online_format_setting_value(ONLINE_SETTING_USERNAME, value, sizeof(value));
            online_rows_add(ONLINE_ROW_SETTING, 1, ONLINE_SETTING_USERNAME, 0, "USERNAME", value);
            online_format_setting_value(ONLINE_SETTING_PASSWORD, value, sizeof(value));
            online_rows_add(ONLINE_ROW_SETTING, 1, ONLINE_SETTING_PASSWORD, 0, "PASSWORD", value);
            online_rows_add(ONLINE_ROW_ACTION, 1, ONLINE_ACTION_LOGIN, 0, "LOG IN", "");
            online_rows_add(ONLINE_ROW_ACTION, 1, ONLINE_ACTION_REGISTER, 0, "REGISTER", "");
        } else {
            snprintf(line, sizeof(line), "%d waiting", g_online_queue_casual_count);
            online_rows_add(ONLINE_ROW_ACTION, 1, ONLINE_ACTION_QUEUE_CASUAL, 0, "CASUAL QUEUE", line);
            snprintf(line, sizeof(line), "%d waiting", g_online_queue_competitive_count);
            online_rows_add(ONLINE_ROW_ACTION, 1, ONLINE_ACTION_QUEUE_COMPETITIVE, 0, "COMPETITIVE QUEUE", line);
            if (g_online_queue_mode) {
                online_rows_add(ONLINE_ROW_ACTION, 1, ONLINE_ACTION_LEAVE_QUEUE, 0, "LEAVE QUEUE", g_online_queue_mode == 2 ? "competitive" : "casual");
            }
            if (ggpo_net_active()) {
                online_rows_add(ONLINE_ROW_ACTION, 1, ONLINE_ACTION_STOP, 0, "STOP SESSION", ggpo_net_mode_name());
            }
            online_rows_add(ONLINE_ROW_ACTION, 1, ONLINE_ACTION_DISCONNECT_SERVER, 0, "DISCONNECT", "");
        }
    } else if (g_online_tab == ONLINE_TAB_FRIENDS) {
        online_rows_add(ONLINE_ROW_ACTION, 1, ONLINE_ACTION_ADD_FRIEND, 0, "SEARCH USER", "username");
        for (int i = 0; i < g_online_request_count; i++) {
            online_rows_add(ONLINE_ROW_FRIEND_REQUEST, 1, i, 0, g_online_requests[i].name, "sent a friend request!");
        }
        for (int i = 0; i < g_online_challenge_count; i++) {
            char right[96];
            snprintf(right, sizeof(right), "sent a challenge!  %ds", g_online_challenges[i].expires_in);
            online_rows_add(ONLINE_ROW_CHALLENGE, 1, i, 0, g_online_challenges[i].from, right);
        }
        for (int i = 0; i < g_online_friend_count; i++) {
            OnlineFriend* fr = &g_online_friends[i];
            if (!fr->online) continue;
            online_rows_add(ONLINE_ROW_FRIEND, 1, i, 0, fr->name, "online");
        }
        for (int i = 0; i < g_online_friend_count; i++) {
            OnlineFriend* fr = &g_online_friends[i];
            if (fr->online) continue;
            online_rows_add(ONLINE_ROW_FRIEND, 1, i, 0, fr->name, "offline");
        }
    } else if (g_online_tab == ONLINE_TAB_SETTINGS) {
        struct SettingRow { OnlineHubSetting id; const char* label; } settings[] = {
            { ONLINE_SETTING_SERVER_HOST, "Server Address" },
            { ONLINE_SETTING_LOCAL_PORT, "P2P UDP Port" },
            { ONLINE_SETTING_CHALLENGE_NOTIFICATIONS, "Challenge Notifications" },
        };
        online_rows_add(ONLINE_ROW_INFO, 0, 0, 0, "Settings", "server and P2P connection");
        for (int i = 0; i < (int)(sizeof(settings) / sizeof(settings[0])); i++) {
            char value[192];
            online_format_setting_value(settings[i].id, value, sizeof(value));
            online_rows_add(ONLINE_ROW_SETTING, 1, settings[i].id, 0, settings[i].label, value);
        }
        online_rows_add(ONLINE_ROW_ACTION, 1, ONLINE_ACTION_SAVE_SETTINGS, 0, "Save Settings", ONLINE_HUB_CFG_PATH);
    }

    online_rows_add(ONLINE_ROW_BACK, 1, 0, 0, "BACK", "");

    g_online_selected_row = -1;
    for (int i = 0; i < g_online_row_count; i++) {
        OnlineHubRow* row = &g_online_rows[i];
        if (row->kind == keep_kind && row->id == keep_id && row->aux == keep_aux && row->selectable) {
            g_online_selected_row = i;
            break;
        }
    }
    if (g_online_selected_row < 0) g_online_selected_row = online_first_selectable();
    online_ensure_scroll_visible();
}

static int online_selected_friend_index(void) {
    if (g_online_selected_row >= 0 && g_online_selected_row < g_online_row_count &&
        g_online_rows[g_online_selected_row].kind == ONLINE_ROW_FRIEND) {
        return g_online_rows[g_online_selected_row].id;
    }
    for (int i = 0; i < g_online_row_count; i++) {
        if (g_online_rows[i].kind == ONLINE_ROW_FRIEND) return g_online_rows[i].id;
    }
    return -1;
}

static int online_selected_request_index(void) {
    if (g_online_selected_row >= 0 && g_online_selected_row < g_online_row_count &&
        g_online_rows[g_online_selected_row].kind == ONLINE_ROW_FRIEND_REQUEST) {
        return g_online_rows[g_online_selected_row].id;
    }
    for (int i = 0; i < g_online_row_count; i++) {
        if (g_online_rows[i].kind == ONLINE_ROW_FRIEND_REQUEST) return g_online_rows[i].id;
    }
    return -1;
}

static int online_selected_challenge_index(void) {
    if (g_online_selected_row >= 0 && g_online_selected_row < g_online_row_count &&
        g_online_rows[g_online_selected_row].kind == ONLINE_ROW_CHALLENGE) {
        return g_online_rows[g_online_selected_row].id;
    }
    for (int i = 0; i < g_online_row_count; i++) {
        if (g_online_rows[i].kind == ONLINE_ROW_CHALLENGE) return g_online_rows[i].id;
    }
    return -1;
}

static void online_remove_request_index(int idx) {
    if (idx < 0 || idx >= g_online_request_count) return;
    for (int i = idx; i + 1 < g_online_request_count; i++) {
        g_online_requests[i] = g_online_requests[i + 1];
    }
    g_online_request_count--;
    if (g_online_request_count < 0) g_online_request_count = 0;
    online_hub_rebuild_rows();
}

static void online_remove_challenge_index(int idx) {
    if (idx < 0 || idx >= g_online_challenge_count) return;
    online_challenge_toast_clear(g_online_challenges[idx].id, g_online_challenges[idx].from);
    for (int i = idx; i + 1 < g_online_challenge_count; i++) {
        g_online_challenges[i] = g_online_challenges[i + 1];
    }
    g_online_challenge_count--;
    if (g_online_challenge_count < 0) g_online_challenge_count = 0;
    online_hub_rebuild_rows();
}

static void online_remove_friend_index(int idx) {
    if (idx < 0 || idx >= g_online_friend_count) return;
    for (int i = idx; i + 1 < g_online_friend_count; i++) {
        g_online_friends[i] = g_online_friends[i + 1];
    }
    g_online_friend_count--;
    if (g_online_friend_count < 0) g_online_friend_count = 0;
    g_online_context_active = 0;
    online_hub_rebuild_rows();
}

static void online_move_selection(int dir, int amount) {
    int cur = g_online_selected_row;
    if (cur < 0) return;
    if (amount < 1) amount = 1;
    for (int i = 0; i < amount; i++) {
        int n = online_next_selectable(cur, dir);
        if (n < 0) break;
        cur = n;
    }
    g_online_selected_row = cur;
    online_ensure_scroll_visible();
}

static void online_switch_tab(int delta) {
    int tab = (int)g_online_tab + delta;
    while (tab < 0) tab += ONLINE_TAB_COUNT;
    while (tab >= ONLINE_TAB_COUNT) tab -= ONLINE_TAB_COUNT;
    g_online_tab = (OnlineHubTab)tab;
    g_online_selected_row = -1;
    g_online_scroll_row = 0;
    g_online_capture_active = 0;
    g_online_context_active = 0;
    online_hub_rebuild_rows();
}

static int online_setting_is_text(OnlineHubSetting setting) {
    return setting == ONLINE_SETTING_USERNAME ||
           setting == ONLINE_SETTING_PASSWORD ||
           setting == ONLINE_SETTING_SERVER_HOST ||
           setting == ONLINE_SETTING_LOCAL_PORT ||
           setting == ONLINE_SETTING_PEER_HOST;
}

static void online_begin_setting_capture(OnlineHubSetting setting) {
    if (!online_setting_is_text(setting)) return;
    g_online_capture_active = 1;
    g_online_capture_kind = ONLINE_CAPTURE_SETTING;
    g_online_capture_target = setting;
    switch (setting) {
        case ONLINE_SETTING_USERNAME: safe_copy(g_online_capture_buf, sizeof(g_online_capture_buf), g_online_cfg.username); break;
        case ONLINE_SETTING_PASSWORD: safe_copy(g_online_capture_buf, sizeof(g_online_capture_buf), g_online_cfg.password); break;
        case ONLINE_SETTING_SERVER_HOST: online_format_setting_value(setting, g_online_capture_buf, sizeof(g_online_capture_buf)); break;
        case ONLINE_SETTING_LOCAL_PORT: online_format_setting_value(setting, g_online_capture_buf, sizeof(g_online_capture_buf)); break;
        case ONLINE_SETTING_PEER_HOST: safe_copy(g_online_capture_buf, sizeof(g_online_capture_buf), g_online_cfg.peer_host); break;
        default: g_online_capture_buf[0] = '\0'; break;
    }
}

static void online_begin_friend_capture(void) {
    g_online_capture_active = 1;
    g_online_capture_kind = ONLINE_CAPTURE_ADD_FRIEND;
    g_online_capture_target = 0;
    g_online_capture_buf[0] = '\0';
    online_hub_set_status("Type a username, then Enter.");
}

static void online_commit_capture(void) {
    if (!g_online_capture_active) return;
    if (g_online_capture_kind == ONLINE_CAPTURE_SETTING) {
        switch ((OnlineHubSetting)g_online_capture_target) {
            case ONLINE_SETTING_USERNAME: safe_copy(g_online_cfg.username, sizeof(g_online_cfg.username), trim_ws(g_online_capture_buf)); break;
            case ONLINE_SETTING_PASSWORD: safe_copy(g_online_cfg.password, sizeof(g_online_cfg.password), trim_ws(g_online_capture_buf)); break;
            case ONLINE_SETTING_SERVER_HOST: {
                char host[ONLINE_HUB_TEXT_MAX];
                uint16_t port = g_online_cfg.server_port ? g_online_cfg.server_port : ONLINE_DEFAULT_SERVER_PORT;
                if (!online_hub_parse_host_port(trim_ws(g_online_capture_buf), host, sizeof(host), &port, ONLINE_DEFAULT_SERVER_PORT)) {
                    online_hub_set_status("Enter server as host:port.");
                    return;
                }
                safe_copy(g_online_cfg.server_host, sizeof(g_online_cfg.server_host), host);
                g_online_cfg.server_port = port;
            } break;
            case ONLINE_SETTING_LOCAL_PORT: {
                char* value = trim_ws(g_online_capture_buf);
                long port = 0;
                if (!value[0] || _stricmp(value, "auto") == 0) {
                    g_online_cfg.local_port = 0;
                } else if (online_parse_long_range(value, 0, 65535, &port)) {
                    g_online_cfg.local_port = (uint16_t)port;
                } else {
                    online_hub_set_status("Enter a UDP port or Auto.");
                    return;
                }
            } break;
            case ONLINE_SETTING_PEER_HOST: safe_copy(g_online_cfg.peer_host, sizeof(g_online_cfg.peer_host), trim_ws(g_online_capture_buf)); break;
            default: break;
        }
        online_hub_clamp_config();
        online_hub_save();
        online_hub_set_status("Settings saved.");
    } else if (g_online_capture_kind == ONLINE_CAPTURE_ADD_FRIEND) {
        char* name = trim_ws(g_online_capture_buf);
        if (name && name[0]) online_server_send_username_action("friend_request", name);
        else online_hub_set_status("Enter a username.");
    }
    g_online_capture_active = 0;
    g_online_capture_kind = ONLINE_CAPTURE_NONE;
    g_online_capture_target = 0;
    g_online_capture_buf[0] = '\0';
    online_hub_rebuild_rows();
}

static void online_cancel_capture(void) {
    g_online_capture_active = 0;
    g_online_capture_kind = ONLINE_CAPTURE_NONE;
    g_online_capture_target = 0;
    g_online_capture_buf[0] = '\0';
    online_hub_rebuild_rows();
}

static void online_adjust_setting(OnlineHubSetting setting, int delta) {
    if (delta == 0) delta = 1;
    switch (setting) {
        case ONLINE_SETTING_SERVER_PORT:
            g_online_cfg.server_port = (uint16_t)clampi((int)g_online_cfg.server_port + delta, 0, 65535);
            break;
        case ONLINE_SETTING_PEER_PORT:
            g_online_cfg.peer_port = (uint16_t)clampi((int)g_online_cfg.peer_port + delta, 0, 65535);
            break;
        case ONLINE_SETTING_LOCAL_PORT:
            g_online_cfg.local_port = (uint16_t)clampi((int)g_online_cfg.local_port + delta, 0, 65535);
            break;
        case ONLINE_SETTING_P2P_ENABLED:
            g_online_cfg.p2p_enabled = g_online_cfg.p2p_enabled ? 0 : 1;
            break;
        case ONLINE_SETTING_RELAY_FALLBACK:
            g_online_cfg.relay_fallback = g_online_cfg.relay_fallback ? 0 : 1;
            break;
        case ONLINE_SETTING_INPUT_DELAY:
            g_online_cfg.input_delay = clampi(g_online_cfg.input_delay + delta, 0, GGPO_NET_MAX_INPUT_DELAY);
            break;
        case ONLINE_SETTING_FRAME_ADVANTAGE:
            g_online_cfg.max_frame_advantage = clampi(g_online_cfg.max_frame_advantage + delta, 0, GGPO_NET_MAX_FRAME_ADVANTAGE_LIMIT);
            break;
        case ONLINE_SETTING_MAX_PREDICTION:
            g_online_cfg.max_prediction = clampi(g_online_cfg.max_prediction + delta, 1, GGPO_NET_MAX_PREDICTION_LIMIT);
            break;
        case ONLINE_SETTING_CORRECTION:
            g_online_cfg.correction_enabled = g_online_cfg.correction_enabled ? 0 : 1;
            break;
        case ONLINE_SETTING_SIM_LOSS:
            g_online_cfg.sim_loss = clampi(g_online_cfg.sim_loss + delta, 0, 100);
            break;
        case ONLINE_SETTING_SIM_MIN_DELAY:
            g_online_cfg.sim_min_delay = clampi(g_online_cfg.sim_min_delay + delta, 0, GGPO_NET_SIM_MAX_DELAY_TICKS);
            if (g_online_cfg.sim_max_delay < g_online_cfg.sim_min_delay) g_online_cfg.sim_max_delay = g_online_cfg.sim_min_delay;
            break;
        case ONLINE_SETTING_SIM_MAX_DELAY:
            g_online_cfg.sim_max_delay = clampi(g_online_cfg.sim_max_delay + delta, 0, GGPO_NET_SIM_MAX_DELAY_TICKS);
            if (g_online_cfg.sim_max_delay < g_online_cfg.sim_min_delay) g_online_cfg.sim_min_delay = g_online_cfg.sim_max_delay;
            break;
        case ONLINE_SETTING_CHALLENGE_NOTIFICATIONS:
            g_online_cfg.challenge_notifications = g_online_cfg.challenge_notifications ? 0 : 1;
            if (!g_online_cfg.challenge_notifications) {
                memset(&g_online_challenge_toast, 0, sizeof(g_online_challenge_toast));
            }
            break;
        default:
            return;
    }
    online_hub_apply_net_settings();
    online_hub_save();
    online_hub_rebuild_rows();
}

static void online_start_host_from_hub(void) {
    uint16_t port;
    if (!g_online_cfg.p2p_enabled) {
        online_hub_set_status("Direct P2P is required for online play.");
        return;
    }
    if (ggpo_net_active()) {
        online_hub_set_status("Stop the active online session first.");
        return;
    }
    online_hub_apply_net_settings();
    port = g_online_cfg.local_port ? g_online_cfg.local_port : GGPO_NET_DEFAULT_PORT;
    start_ggpo_net_host(port, "online hub");
    online_hub_set_status(ggpo_net_active() ? "Hosting P2P match. Give your peer the UDP port." : "Host failed; open console or log for details.");
    online_hub_rebuild_rows();
}

static void online_start_join_from_hub(const char* host, uint16_t port) {
    if (!g_online_cfg.p2p_enabled) {
        online_hub_set_status("Direct P2P is required for online play.");
        return;
    }
    if (ggpo_net_active()) {
        online_hub_set_status("Stop the active online session first.");
        return;
    }
    if (!host || !host[0]) {
        online_hub_set_status("Peer address is empty.");
        return;
    }
    online_hub_apply_net_settings();
    start_ggpo_net_join(host, port ? port : GGPO_NET_DEFAULT_PORT, g_online_cfg.local_port, "online hub");
    online_hub_set_status(ggpo_net_active() ? "Joining P2P match." : "Join failed; open console or log for details.");
    online_hub_rebuild_rows();
}

static int online_try_send_friend_challenge(int idx) {
    if (idx < 0 || idx >= g_online_friend_count) {
        online_hub_set_status("Select a friend first.");
        return 0;
    }
    if (!g_online_friends[idx].online) {
        online_hub_set_status("Friend is offline.");
        return 0;
    }
    if (online_sent_challenge_pending(g_online_friends[idx].name)) {
        online_hub_set_status("");
        return 0;
    }
    online_server_send_username_action("challenge", g_online_friends[idx].name);
    return 1;
}

static void online_activate_action(OnlineHubAction action) {
    switch (action) {
        case ONLINE_ACTION_CONNECT:
        case ONLINE_ACTION_LOGIN:
            online_server_connect(0);
            break;
        case ONLINE_ACTION_REGISTER:
            online_server_connect(1);
            break;
        case ONLINE_ACTION_DISCONNECT_SERVER:
            online_server_disconnect("Disconnected from online server.");
            break;
        case ONLINE_ACTION_QUEUE_CASUAL:
            online_server_send_queue("casual");
            break;
        case ONLINE_ACTION_QUEUE_COMPETITIVE:
            online_server_send_queue("competitive");
            break;
        case ONLINE_ACTION_LEAVE_QUEUE:
            online_server_leave_queue();
            break;
        case ONLINE_ACTION_HOST:
            online_start_host_from_hub();
            break;
        case ONLINE_ACTION_JOIN:
            online_start_join_from_hub(g_online_cfg.peer_host, g_online_cfg.peer_port);
            break;
        case ONLINE_ACTION_STOP:
            if (ggpo_net_active()) {
                stop_ggpo_net("online hub");
                online_hub_set_status("Online session stopped.");
                online_hub_rebuild_rows();
            } else {
                online_hub_set_status("No online session is active.");
            }
            break;
        case ONLINE_ACTION_ADD_FRIEND:
            online_begin_friend_capture();
            break;
        case ONLINE_ACTION_CHALLENGE_FRIEND: {
            int idx = online_selected_friend_index();
            online_try_send_friend_challenge(idx);
        } break;
        case ONLINE_ACTION_ACCEPT_FRIEND: {
            int idx = online_selected_request_index();
            if (idx < 0 || idx >= g_online_request_count) {
                online_hub_set_status("Select a friend request first.");
                break;
            }
            online_server_send_username_action("friend_accept", g_online_requests[idx].name);
            online_remove_request_index(idx);
        } break;
        case ONLINE_ACTION_DECLINE_FRIEND: {
            int idx = online_selected_request_index();
            if (idx < 0 || idx >= g_online_request_count) {
                online_hub_set_status("Select a friend request first.");
                break;
            }
            online_server_send_username_action("friend_decline", g_online_requests[idx].name);
            online_remove_request_index(idx);
        } break;
        case ONLINE_ACTION_ACCEPT_CHALLENGE: {
            int idx = online_selected_challenge_index();
            if (idx < 0 || idx >= g_online_challenge_count) {
                online_hub_set_status("Select a challenge first.");
                break;
            }
            online_server_send_challenge_action("challenge_accept", g_online_challenges[idx].id, g_online_challenges[idx].from);
            online_remove_challenge_index(idx);
        } break;
        case ONLINE_ACTION_DECLINE_CHALLENGE: {
            int idx = online_selected_challenge_index();
            if (idx < 0 || idx >= g_online_challenge_count) {
                online_hub_set_status("Select a challenge first.");
                break;
            }
            online_server_send_challenge_action("challenge_decline", g_online_challenges[idx].id, g_online_challenges[idx].from);
            online_remove_challenge_index(idx);
        } break;
        case ONLINE_ACTION_REMOVE_FRIEND: {
            int idx = online_selected_friend_index();
            if (idx < 0 || idx >= g_online_friend_count) {
                online_hub_set_status("Select a friend first.");
                break;
            }
            online_server_send_username_action("friend_remove", g_online_friends[idx].name);
        } break;
        case ONLINE_ACTION_BLOCK_FRIEND: {
            int idx = online_selected_friend_index();
            if (idx < 0 || idx >= g_online_friend_count) {
                online_hub_set_status("Select a friend first.");
                break;
            }
            g_online_friends[idx].blocked = g_online_friends[idx].blocked ? 0 : 1;
            online_hub_save();
            online_hub_set_status(g_online_friends[idx].blocked ? "Friend blocked." : "Friend unblocked.");
            online_hub_rebuild_rows();
        } break;
        case ONLINE_ACTION_SAVE_SETTINGS:
            online_hub_apply_net_settings();
            online_hub_save();
            online_hub_set_status("Settings saved and applied.");
            online_hub_rebuild_rows();
            break;
        default:
            break;
    }
}

static void online_activate_selected(void) {
    OnlineHubRow* row;
    if (g_online_selected_row < 0 || g_online_selected_row >= g_online_row_count) return;
    row = &g_online_rows[g_online_selected_row];
    if (row->kind == ONLINE_ROW_BACK) {
        if (p_state_switch) {
            void* target = g_online_return_state ? g_online_return_state : (void*)(uintptr_t)ADDR_MAIN_STATE;
            if (target == (void*)&g_online_hub_state) target = (void*)(uintptr_t)ADDR_MAIN_STATE;
            p_state_switch(target);
        }
        return;
    }
    if (row->kind == ONLINE_ROW_ACTION) {
        online_activate_action((OnlineHubAction)row->id);
    } else if (row->kind == ONLINE_ROW_SETTING) {
        OnlineHubSetting setting = (OnlineHubSetting)row->id;
        if (online_setting_is_text(setting)) online_begin_setting_capture(setting);
        else online_adjust_setting(setting, 1);
    } else if (row->kind == ONLINE_ROW_FRIEND) {
        int idx = row->id;
        online_try_send_friend_challenge(idx);
    } else if (row->kind == ONLINE_ROW_FRIEND_REQUEST) {
        int idx = row->id;
        if (idx >= 0 && idx < g_online_request_count) {
            online_server_send_username_action("friend_accept", g_online_requests[idx].name);
            online_remove_request_index(idx);
        }
    } else if (row->kind == ONLINE_ROW_CHALLENGE) {
        int idx = row->id;
        if (idx >= 0 && idx < g_online_challenge_count) {
            online_server_send_challenge_action("challenge_accept", g_online_challenges[idx].id, g_online_challenges[idx].from);
            online_remove_challenge_index(idx);
        }
    }
}

static int online_selected_decline(void) {
    OnlineHubRow* row;
    if (g_online_selected_row < 0 || g_online_selected_row >= g_online_row_count) return 0;
    row = &g_online_rows[g_online_selected_row];
    if (row->kind == ONLINE_ROW_FRIEND_REQUEST) {
        int idx = row->id;
        if (idx >= 0 && idx < g_online_request_count) {
            online_server_send_username_action("friend_decline", g_online_requests[idx].name);
            online_remove_request_index(idx);
            return 1;
        }
    } else if (row->kind == ONLINE_ROW_CHALLENGE) {
        int idx = row->id;
        if (idx >= 0 && idx < g_online_challenge_count) {
            online_server_send_challenge_action("challenge_decline", g_online_challenges[idx].id, g_online_challenges[idx].from);
            online_remove_challenge_index(idx);
            return 1;
        }
    }
    return 0;
}

static int online_click_hits_inline_decline(float x) {
    OnlineLayout L;
    online_calc_layout(&L);
    return x >= (L.content_x + L.content_w - 132.0f * L.ui);
}

static void online_activate_selected_from_mouse(float x) {
    OnlineHubRow* row;
    if (g_online_selected_row < 0 || g_online_selected_row >= g_online_row_count) return;
    row = &g_online_rows[g_online_selected_row];
    if (online_click_hits_inline_decline(x) && online_selected_decline()) return;
    if (row->kind == ONLINE_ROW_FRIEND) {
        int idx = row->id;
        if (idx >= 0 && idx < g_online_friend_count &&
            g_online_friends[idx].online &&
            online_click_hits_inline_decline(x)) {
            online_try_send_friend_challenge(idx);
        }
        return;
    }
    online_activate_selected();
}

static void online_open_friend_context(int friend_idx, float x, float y) {
    if (friend_idx < 0 || friend_idx >= g_online_friend_count) return;
    g_online_context_active = 1;
    g_online_context_friend = friend_idx;
    g_online_context_x = x;
    g_online_context_y = y;
}

static int online_context_action_at(float x, float y) {
    OnlineLayout L;
    OnlineFriend* fr;
    float s;
    float menu_w;
    float item_h;
    float mx;
    float my;
    int count;
    int item;
    if (!g_online_context_active || g_online_context_friend < 0 || g_online_context_friend >= g_online_friend_count) return 0;
    fr = &g_online_friends[g_online_context_friend];
    online_calc_layout(&L);
    s = L.ui;
    menu_w = 176.0f * s;
    item_h = 34.0f * s;
    count = fr->online ? 2 : 1;
    mx = clampf(g_online_context_x, 8.0f * s, L.w - menu_w - 8.0f * s);
    my = clampf(g_online_context_y, 8.0f * s, L.h - (float)count * item_h - 8.0f * s);
    if (x < mx || x > mx + menu_w || y < my || y > my + (float)count * item_h) return 0;
    item = (int)((y - my) / item_h);
    if (fr->online && item == 0) return 1; /* challenge */
    return 2; /* unfriend */
}

static int online_context_activate(int action) {
    int idx = g_online_context_friend;
    g_online_context_active = 0;
    if (idx < 0 || idx >= g_online_friend_count) return 0;
    if (action == 1) {
        online_try_send_friend_challenge(idx);
        return 1;
    }
    if (action == 2) {
        online_server_send_username_action("friend_remove", g_online_friends[idx].name);
        online_hub_set_status("Friend removed.");
        online_remove_friend_index(idx);
        return 1;
    }
    return 0;
}

static void online_adjust_selected(int delta) {
    OnlineHubRow* row;
    if (g_online_selected_row < 0 || g_online_selected_row >= g_online_row_count) return;
    row = &g_online_rows[g_online_selected_row];
    if (row->kind == ONLINE_ROW_SETTING) {
        OnlineHubSetting setting = (OnlineHubSetting)row->id;
        if (online_setting_is_text(setting)) return;
        online_adjust_setting(setting, delta);
    }
}

static const char* online_tab_name(OnlineHubTab tab) {
    switch (tab) {
        case ONLINE_TAB_PLAY: return "PLAY";
        case ONLINE_TAB_FRIENDS: return "FRIENDS";
        case ONLINE_TAB_SETTINGS: return "SETTINGS";
        default: return "?";
    }
}

static void online_hub_draw_rect(float x, float y, float w, float h, float r, float g, float b, float a) {
    hooks_ui_fill_rect(x, y, w, h, r, g, b, a);
}

static void online_hub_draw_border(float x, float y, float w, float h, float line_w, float r, float g, float b, float a) {
    hooks_ui_stroke_rect(x, y, w, h, line_w, r, g, b, a);
}

static void online_hub_text_alpha(float x, float y, float scale, float r, float g, float b, float a, const char* text) {
    scale = clampf(scale, 0.76f, 3.00f);
    a = clampf(a, 0.0f, 1.0f);
    if (a <= 0.01f) return;
    float o = clampf(2.0f * scale, 2.0f, 4.0f);
    draw_text_scaled_mode_alpha(x + o, y + o, scale, 0.0f, 0.0f, 0.0f, a * 0.80f, text, 0);
    draw_text_scaled_mode_alpha(x + 1.0f, y + o + 1.0f, scale, 0.0f, 0.0f, 0.0f, a * 0.70f, text, 0);
    draw_text_scaled_mode_alpha(x, y, scale, r, g, b, a, text, 0);
}

static void online_hub_text(float x, float y, float scale, float r, float g, float b, const char* text) {
    online_hub_text_alpha(x, y, scale, r, g, b, 1.0f, text);
}

static void online_hub_text_center_alpha(float x, float y, float scale, float r, float g, float b, float a, const char* text) {
    scale = clampf(scale, 0.76f, 3.00f);
    a = clampf(a, 0.0f, 1.0f);
    if (a <= 0.01f) return;
    float o = clampf(2.0f * scale, 2.0f, 4.0f);
    draw_text_scaled_mode_alpha(x + o, y + o, scale, 0.0f, 0.0f, 0.0f, a * 0.80f, text, 1);
    draw_text_scaled_mode_alpha(x + 1.0f, y + o + 1.0f, scale, 0.0f, 0.0f, 0.0f, a * 0.70f, text, 1);
    draw_text_scaled_mode_alpha(x, y, scale, r, g, b, a, text, 1);
}

static void online_hub_text_center(float x, float y, float scale, float r, float g, float b, const char* text) {
    online_hub_text_center_alpha(x, y, scale, r, g, b, 1.0f, text);
}

static void online_hub_text_right_alpha(float x, float y, float scale, float r, float g, float b, float a, const char* text) {
    scale = clampf(scale, 0.76f, 3.00f);
    a = clampf(a, 0.0f, 1.0f);
    if (a <= 0.01f) return;
    float o = clampf(2.0f * scale, 2.0f, 4.0f);
    draw_text_scaled_mode_alpha(x + o, y + o, scale, 0.0f, 0.0f, 0.0f, a * 0.80f, text, 2);
    draw_text_scaled_mode_alpha(x + 1.0f, y + o + 1.0f, scale, 0.0f, 0.0f, 0.0f, a * 0.70f, text, 2);
    draw_text_scaled_mode_alpha(x, y, scale, r, g, b, a, text, 2);
}

static void online_hub_text_right(float x, float y, float scale, float r, float g, float b, const char* text) {
    online_hub_text_right_alpha(x, y, scale, r, g, b, 1.0f, text);
}

static int online_hub_row_primary(const OnlineHubRow* row) {
    if (!row || row->kind != ONLINE_ROW_ACTION) return 0;
    return row->id == ONLINE_ACTION_LOGIN ||
           row->id == ONLINE_ACTION_REGISTER ||
           row->id == ONLINE_ACTION_QUEUE_CASUAL ||
           row->id == ONLINE_ACTION_QUEUE_COMPETITIVE ||
           row->id == ONLINE_ACTION_ACCEPT_CHALLENGE;
}

static int online_hub_row_boxed(const OnlineHubRow* row) {
    if (!row) return 0;
    return row->kind == ONLINE_ROW_ACTION ||
           row->kind == ONLINE_ROW_SETTING ||
           row->kind == ONLINE_ROW_FRIEND ||
           row->kind == ONLINE_ROW_FRIEND_REQUEST ||
           row->kind == ONLINE_ROW_CHALLENGE ||
           row->kind == ONLINE_ROW_BACK;
}

static void online_hub_draw_button_box(float x, float y, float w, float h,
                                       const char* label, const char* detail,
                                       int selected, int primary, float s) {
    float br = primary ? 0.06f : 0.075f;
    float bg = primary ? 0.24f : 0.095f;
    float bb = primary ? 0.21f : 0.125f;
    float ba = primary ? 0.98f : 0.94f;
    float tr = primary ? 0.82f : 0.90f;
    float tg = primary ? 1.00f : 0.94f;
    float tb = primary ? 0.94f : 0.98f;

    if (selected) {
        br += 0.05f;
        bg += 0.05f;
        bb += 0.05f;
    }

    online_hub_draw_rect(x, y, w, h, br, bg, bb, ba);
    online_hub_draw_border(x, y, w, h, selected ? 2.0f : 1.0f,
                           selected ? 0.48f : 0.30f,
                           selected ? 0.94f : 0.52f,
                           selected ? 0.86f : 0.62f,
                           selected ? 1.0f : 0.84f);

    mods_restore_render_state();
    online_hub_text_center(x + w * 0.5f, y + h * 0.5f - 12.0f * s,
                           1.12f * s, tr, tg, tb, label ? label : "");
    if (detail && detail[0]) {
        online_hub_text_right(x + w - 16.0f * s, y + h * 0.5f - 9.0f * s,
                              0.68f * s, 0.62f, 0.72f, 0.88f, detail);
    }
}

static void online_hub_draw_panel(const OnlineLayout* L) {
    float s;
    uint32_t tick;
    float pulse;
    if (!L) return;
    s = L->ui;
    tick = hooks_player_colour_tick();
    pulse = 0.5f + 0.5f * hooks_triangle01(tick, 96u);

    online_hub_draw_rect(0.0f, 0.0f, L->w, L->h, 0.01f, 0.015f, 0.02f, 0.16f);

    online_hub_draw_rect(L->panel_x, L->panel_y, L->panel_w, L->panel_h, 0.022f, 0.026f, 0.034f, 0.90f);
    online_hub_draw_rect(L->panel_x + 3.0f, L->panel_y + 3.0f,
                         L->panel_w - 6.0f, L->panel_h - 6.0f, 0.050f, 0.058f, 0.074f, 0.62f);
    online_hub_draw_rect(L->panel_x, L->panel_y, L->panel_w, L->header_h, 0.042f, 0.050f, 0.064f, 0.98f);
    online_hub_draw_rect(L->panel_x, L->panel_y + L->panel_h - L->footer_h,
                         L->panel_w, L->footer_h, 0.025f, 0.032f, 0.045f, 0.96f);
    online_hub_draw_rect(L->content_x, L->content_y, L->content_w, L->content_h,
                         0.035f, 0.044f, 0.058f, 0.60f);

    online_hub_draw_border(L->panel_x, L->panel_y, L->panel_w, L->panel_h, 2.0f,
                           0.22f + pulse * 0.14f,
                           0.72f + pulse * 0.18f,
                           0.66f + pulse * 0.16f,
                           1.0f);
    online_hub_draw_border(L->panel_x + 4.0f, L->panel_y + 4.0f,
                           L->panel_w - 8.0f, L->panel_h - 8.0f, 1.0f,
                           0.21f, 0.26f, 0.36f, 0.94f);
    online_hub_draw_rect(L->panel_x + 4.0f, L->panel_y + L->header_h - 2.0f * s,
                         L->panel_w - 8.0f, 2.0f * s,
                         0.18f + pulse * 0.12f,
                         0.70f + pulse * 0.18f,
                         0.64f + pulse * 0.16f,
                         0.88f);
}

static void online_hub_draw_tabs(const OnlineLayout* L) {
    float s;
    float gap;
    float total_w;
    float tab_w;
    float tab_h;
    float tab_x;
    float tab_y;
    if (!L) return;
    s = L->ui;
    gap = 6.0f * s;
    total_w = clampf(250.0f * s, 205.0f * s, L->panel_w * 0.34f);
    tab_w = (total_w - gap * 2.0f) / 3.0f;
    tab_h = 30.0f * s;
    tab_x = L->panel_x + L->panel_w - total_w - 14.0f * s;
    tab_y = L->panel_y + (L->header_h - tab_h) * 0.5f;

    for (int i = 0; i < ONLINE_TAB_COUNT; i++) {
        int selected = (i == (int)g_online_tab);
        float x = tab_x + (tab_w + gap) * (float)i;
        online_hub_draw_rect(x, tab_y, tab_w, tab_h,
                             selected ? 0.055f : 0.055f,
                             selected ? 0.205f : 0.075f,
                             selected ? 0.185f : 0.105f,
                             selected ? 0.96f : 0.80f);
        online_hub_draw_border(x, tab_y, tab_w, tab_h, selected ? 2.0f : 1.0f,
                               selected ? 0.36f : 0.24f,
                               selected ? 0.92f : 0.44f,
                               selected ? 0.82f : 0.54f,
                               selected ? 1.0f : 0.90f);
        mods_restore_render_state();
        online_hub_text_center(x + tab_w * 0.5f, tab_y + tab_h * 0.5f - 9.0f * s,
                               0.62f * s,
                               selected ? 0.82f : 0.72f,
                               selected ? 1.00f : 0.82f,
                               selected ? 0.94f : 0.92f,
                               online_tab_name((OnlineHubTab)i));
        if (i == ONLINE_TAB_FRIENDS && (g_online_request_count + g_online_challenge_count) > 0) {
            char badge[16];
            int pending = g_online_request_count + g_online_challenge_count;
            float bw = pending > 9 ? 24.0f * s : 18.0f * s;
            float bh = 17.0f * s;
            float bx = x + tab_w - bw - 4.0f * s;
            float by = tab_y - 5.0f * s;
            snprintf(badge, sizeof(badge), "%d", pending > 99 ? 99 : pending);
            online_hub_draw_rect(bx, by, bw, bh, 0.82f, 0.20f, 0.24f, 0.96f);
            online_hub_draw_border(bx, by, bw, bh, 1.0f, 1.0f, 0.76f, 0.78f, 0.96f);
            mods_restore_render_state();
            online_hub_text_center(bx + bw * 0.5f, by + 2.0f * s,
                                   0.54f * s, 1.0f, 0.94f, 0.94f, badge);
        }
    }
}

static int online_login_gateway_active(void) {
    return !g_online_authed && g_online_tab == ONLINE_TAB_PLAY;
}

static void online_login_gateway_metrics(const OnlineLayout* L,
                                         float* out_x,
                                         float* out_y,
                                         float* out_w,
                                         float* out_row_h,
                                         float* out_gap) {
    float s = L ? L->ui : online_hub_ui_scale();
    float form_w = L ? clampf(500.0f * s, 420.0f, L->panel_w - 96.0f * s) : 540.0f;
    float row_h = 52.0f * s;
    float gap = 12.0f * s;
    if (out_w) *out_w = form_w;
    if (out_row_h) *out_row_h = row_h;
    if (out_gap) *out_gap = gap;
    if (out_x) *out_x = L ? (L->center_x - form_w * 0.5f) : 0.0f;
    if (out_y) *out_y = L ? (L->content_y + 78.0f * s) : 0.0f;
}

static int online_find_row_by_kind_id(OnlineHubRowKind kind, int id) {
    for (int i = 0; i < g_online_row_count; i++) {
        if (g_online_rows[i].kind == kind && g_online_rows[i].id == id) return i;
    }
    return -1;
}

static int online_login_row_at_point(float x, float y) {
    OnlineLayout L;
    float form_x;
    float form_y;
    float form_w;
    float row_h;
    float gap;
    float button_w;
    online_hub_rebuild_rows();
    online_calc_layout(&L);
    online_login_gateway_metrics(&L, &form_x, &form_y, &form_w, &row_h, &gap);
    if (x >= form_x && x <= form_x + form_w && y >= form_y && y <= form_y + row_h) {
        return online_find_row_by_kind_id(ONLINE_ROW_SETTING, ONLINE_SETTING_USERNAME);
    }
    if (x >= form_x && x <= form_x + form_w &&
        y >= form_y + row_h + gap && y <= form_y + row_h * 2.0f + gap) {
        return online_find_row_by_kind_id(ONLINE_ROW_SETTING, ONLINE_SETTING_PASSWORD);
    }
    button_w = (form_w - gap) * 0.5f;
    y = y - (form_y + row_h * 2.0f + gap * 2.0f);
    if (y >= 0.0f && y <= row_h) {
        if (x >= form_x && x <= form_x + button_w) {
            return online_find_row_by_kind_id(ONLINE_ROW_ACTION, ONLINE_ACTION_LOGIN);
        }
        if (x >= form_x + button_w + gap && x <= form_x + form_w) {
            return online_find_row_by_kind_id(ONLINE_ROW_ACTION, ONLINE_ACTION_REGISTER);
        }
    }
    return -1;
}

static int online_tab_at_point(float x, float y) {
    OnlineLayout L;
    float s;
    float gap;
    float total_w;
    float tab_w;
    float tab_h;
    float tab_x;
    float tab_y;
    online_calc_layout(&L);
    s = L.ui;
    gap = 6.0f * s;
    total_w = clampf(250.0f * s, 205.0f * s, L.panel_w * 0.34f);
    tab_w = (total_w - gap * 2.0f) / 3.0f;
    tab_h = 30.0f * s;
    tab_x = L.panel_x + L.panel_w - total_w - 14.0f * s;
    tab_y = L.panel_y + (L.header_h - tab_h) * 0.5f;
    if (y < tab_y || y > tab_y + tab_h) return -1;
    for (int i = 0; i < ONLINE_TAB_COUNT; i++) {
        float tx = tab_x + (tab_w + gap) * (float)i;
        if (x >= tx && x <= tx + tab_w) return i;
    }
    return -1;
}

static int online_row_at_point(float x, float y) {
    OnlineLayout L;
    float rows_top;
    int cap;
    int start;
    int end;
    online_hub_rebuild_rows();
    if (online_login_gateway_active()) {
        return online_login_row_at_point(x, y);
    }
    online_calc_layout(&L);
    rows_top = L.list_top + 14.0f * L.ui;
    if (g_online_status[0] && !g_online_queue_mode && !g_online_pending_match.active) rows_top += 32.0f * L.ui;
    cap = (int)((L.list_bottom - rows_top) / L.row_h);
    if (cap < 4) cap = 4;
    start = clampi(g_online_scroll_row, 0, g_online_row_count);
    end = start + cap;
    if (end > g_online_row_count) end = g_online_row_count;
    for (int i = start; i < end; i++) {
        float row_y = rows_top + (float)(i - start) * L.row_h;
        if (x >= L.content_x && x <= L.content_x + L.content_w &&
            y >= row_y && y <= row_y + L.row_h) {
            return i;
        }
    }
    return -1;
}

static void online_hub_draw_cursor(void) {
    static const char* const cursor[] = {
        "YYY.........",
        "OYYY........",
        "OOYYY.......",
        ".OOYYY......",
        "..OOYYY.....",
        "...OOYYY....",
        "....OOY.OOO.",
        ".....O.OYY..",
        "......OYY...",
        "......OO.O..",
        "......O...YO",
        "..........OO",
    };
    float s = online_hub_ui_scale();
    float px = clampf(2.0f * s, 2.0f, 4.0f);
    float ox = g_online_mouse_x + 8.0f * s;
    float oy = g_online_mouse_y + 4.0f * s;
    for (int pass = 0; pass < 2; pass++) {
        float dx = pass == 0 ? px * 0.8f : 0.0f;
        float dy = pass == 0 ? px * 0.8f : 0.0f;
        for (int row = 0; row < 12; row++) {
            const char* line = cursor[row];
            for (int col = 0; line[col]; col++) {
                char c = line[col];
                if (c == '.') continue;
                if (pass == 0) {
                    online_hub_draw_rect(ox + dx + (float)col * px, oy + dy + (float)row * px, px, px,
                                         0.0f, 0.0f, 0.0f, 0.35f);
                } else if (c == 'Y') {
                    online_hub_draw_rect(ox + (float)col * px, oy + (float)row * px, px, px,
                                         1.0f, 231.0f / 255.0f, 4.0f / 255.0f, 1.0f);
                } else {
                    online_hub_draw_rect(ox + (float)col * px, oy + (float)row * px, px, px,
                                         154.0f / 255.0f, 119.0f / 255.0f, 0.0f, 1.0f);
                }
            }
        }
    }
}

static void online_hub_draw_context_menu(void) {
    OnlineLayout L;
    OnlineFriend* fr;
    float s;
    float menu_w;
    float item_h;
    float mx;
    float my;
    int count;
    if (!g_online_context_active || g_online_tab != ONLINE_TAB_FRIENDS ||
        g_online_context_friend < 0 || g_online_context_friend >= g_online_friend_count) {
        return;
    }
    fr = &g_online_friends[g_online_context_friend];
    online_calc_layout(&L);
    s = L.ui;
    menu_w = 176.0f * s;
    item_h = 34.0f * s;
    count = fr->online ? 2 : 1;
    mx = clampf(g_online_context_x, 8.0f * s, L.w - menu_w - 8.0f * s);
    my = clampf(g_online_context_y, 8.0f * s, L.h - (float)count * item_h - 8.0f * s);
    online_hub_draw_rect(mx, my, menu_w, (float)count * item_h, 0.025f, 0.030f, 0.040f, 0.98f);
    online_hub_draw_border(mx, my, menu_w, (float)count * item_h, 1.0f, 0.58f, 0.68f, 0.78f, 0.98f);
    for (int i = 0; i < count; i++) {
        int action = (fr->online && i == 0) ? 1 : 2;
        int hover = (online_context_action_at(g_online_mouse_x, g_online_mouse_y) == action);
        int sent = (action == 1) ? online_sent_challenge_pending(fr->name) : 0;
        float y = my + (float)i * item_h;
        if (hover) {
            online_hub_draw_rect(mx + 2.0f * s, y + 2.0f * s, menu_w - 4.0f * s, item_h - 4.0f * s,
                                 action == 1 ? (sent ? 0.08f : 0.06f) : 0.16f,
                                 action == 1 ? (sent ? 0.10f : 0.18f) : 0.06f,
                                 action == 1 ? (sent ? 0.14f : 0.15f) : 0.08f,
                                 0.96f);
        }
        mods_restore_render_state();
        online_hub_text(mx + 12.0f * s, y + item_h * 0.5f - 9.0f * s,
                        0.76f * s,
                        action == 1 ? (sent ? 0.72f : 0.76f) : 1.0f,
                        action == 1 ? (sent ? 0.82f : 1.0f) : 0.70f,
                        action == 1 ? (sent ? 0.94f : 0.92f) : 0.72f,
                        action == 1 ? (sent ? "Sent!" : "Challenge") : "Unfriend");
    }
}

static void online_hub_draw_wait_dots(float cx, float y, float s) {
    uint32_t tick = hooks_player_colour_tick();
    float dot = 9.0f * s;
    float gap = 14.0f * s;
    for (int i = 0; i < 5; i++) {
        uint32_t local = (tick + (uint32_t)i * 12u) % 72u;
        float pulse = hooks_triangle01(local, 72u);
        float size = dot * (0.70f + pulse * 0.55f);
        float x = cx - (2.0f * (dot + gap)) + (float)i * (dot + gap) - size * 0.5f;
        online_hub_draw_rect(x, y - size * 0.5f, size, size,
                             0.24f + pulse * 0.10f,
                             0.76f + pulse * 0.16f,
                             0.70f + pulse * 0.18f,
                             0.62f + pulse * 0.30f);
    }
}

static void online_hub_draw_overlay_card(const OnlineLayout* L, float card_w, float card_h, float* out_x, float* out_y) {
    float s;
    float x;
    float y;
    if (!L) return;
    s = L->ui;
    x = L->center_x - card_w * 0.5f;
    y = L->content_y + (L->content_h - card_h) * 0.5f;
    online_hub_draw_rect(L->panel_x, L->panel_y, L->panel_w, L->panel_h,
                         0.005f, 0.008f, 0.012f, 0.44f);
    online_hub_draw_rect(x, y, card_w, card_h, 0.030f, 0.038f, 0.050f, 0.96f);
    online_hub_draw_rect(x + 2.0f * s, y + 2.0f * s,
                         card_w - 4.0f * s, card_h - 4.0f * s,
                         0.052f, 0.066f, 0.080f, 0.50f);
    online_hub_draw_border(x, y, card_w, card_h, 1.0f,
                           0.26f, 0.78f, 0.72f, 0.90f);
    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
}

static float online_ui_fade_alpha(int age, int lifetime, int fade_frames) {
    if (lifetime <= 0) return 1.0f;
    if (age < 0) age = 0;
    if (fade_frames < 1) fade_frames = 1;
    if (age >= lifetime) return 0.0f;
    if (age <= lifetime - fade_frames) return 1.0f;
    return clampf((float)(lifetime - age) / (float)fade_frames, 0.0f, 1.0f);
}

static float online_ui_pulse01(uint32_t period, uint32_t phase) {
    uint32_t tick = hooks_player_colour_tick();
    if (period < 2u) period = 2u;
    return hooks_triangle01((tick + phase) % period, period);
}

static void online_ui_panel(float x, float y, float w, float h, float alpha, int hot) {
    float pulse = online_ui_pulse01(120u, 0u);
    alpha = clampf(alpha, 0.0f, 1.0f);
    online_hub_draw_rect(x, y, w, h,
                         0.025f, 0.032f, 0.044f, 0.92f * alpha);
    online_hub_draw_rect(x + 3.0f, y + 3.0f, w - 6.0f, h - 6.0f,
                         0.058f, 0.072f, 0.088f, 0.48f * alpha);
    online_hub_draw_border(x, y, w, h, hot ? 2.0f : 1.0f,
                           0.26f + pulse * 0.10f,
                           0.72f + pulse * 0.16f,
                           0.68f + pulse * 0.12f,
                           (hot ? 0.98f : 0.82f) * alpha);
}

static int online_ui_close_hit(float x, float y, float bx, float by, float size) {
    return x >= bx && x <= bx + size && y >= by && y <= by + size;
}

static void online_ui_close_button(float x, float y, float size, float alpha, int hover) {
    float pad = size * 0.30f;
    alpha = clampf(alpha, 0.0f, 1.0f);
    online_hub_draw_rect(x, y, size, size,
                         hover ? 0.30f : 0.12f,
                         hover ? 0.10f : 0.12f,
                         hover ? 0.12f : 0.15f,
                         (hover ? 0.98f : 0.72f) * alpha);
    online_hub_draw_border(x, y, size, size, 1.0f,
                           hover ? 1.0f : 0.60f,
                           hover ? 0.58f : 0.70f,
                           hover ? 0.62f : 0.82f,
                           0.90f * alpha);
    hooks_ui_draw_line(x + pad, y + pad, x + size - pad, y + size - pad,
                       2.0f, 1.0f, 0.88f, 0.88f, alpha);
    hooks_ui_draw_line(x + size - pad, y + pad, x + pad, y + size - pad,
                       2.0f, 1.0f, 0.88f, 0.88f, alpha);
}

static void online_queue_panel_metrics(const OnlineLayout* L,
                                       float* out_x,
                                       float* out_y,
                                       float* out_w,
                                       float* out_h,
                                       float* out_bx,
                                       float* out_by,
                                       float* out_bw,
                                       float* out_bh) {
    float s = L ? L->ui : online_hub_ui_scale();
    float w = L ? clampf(520.0f * s, 420.0f, L->content_w - 48.0f * s) : 520.0f * s;
    float h = 214.0f * s;
    float x = L ? (L->center_x - w * 0.5f) : 0.0f;
    float y = L ? (L->content_y + clampf(34.0f * s, 18.0f, L->content_h * 0.20f)) : 0.0f;
    float bw = 176.0f * s;
    float bh = 42.0f * s;
    float bx = x + w - bw - 22.0f * s;
    float by = y + h - bh - 20.0f * s;
    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    if (out_bx) *out_bx = bx;
    if (out_by) *out_by = by;
    if (out_bw) *out_bw = bw;
    if (out_bh) *out_bh = bh;
}

static int online_queue_cancel_button_at(float x, float y) {
    OnlineLayout L;
    float bw;
    float bh;
    float bx;
    float by;
    if (!g_online_queue_mode || g_online_pending_match.active) return 0;
    online_calc_layout(&L);
    online_queue_panel_metrics(&L, NULL, NULL, NULL, NULL, &bx, &by, &bw, &bh);
    return x >= bx && x <= bx + bw && y >= by && y <= by + bh;
}

static void online_hub_render_matchmaking_panel(const OnlineLayout* L, int match_found) {
    float s;
    float card_w;
    float card_h;
    float card_x;
    float card_y;
    float bw;
    float bh;
    float bx;
    float by;
    char line[128];
    float rail_x;
    float rail_y;
    float rail_w;
    float rail_h;
    if (!L) return;
    if (match_found) {
        if (!g_online_pending_match.active || g_online_pending_match.launch_countdown_frames <= 0) return;
    } else {
        if (!g_online_queue_mode || g_online_pending_match.active) return;
    }
    s = L->ui;
    online_queue_panel_metrics(L, &card_x, &card_y, &card_w, &card_h, &bx, &by, &bw, &bh);
    online_ui_panel(card_x, card_y, card_w, card_h, 1.0f, 1);
    mods_restore_render_state();

    if (match_found) {
        int seconds = (g_online_pending_match.launch_countdown_frames + 59) / 60;
        if (seconds < 1) seconds = 1;
        online_hub_text(card_x + 26.0f * s, card_y + 25.0f * s,
                        1.28f * s, 0.96f, 0.98f, 1.00f, "MATCH FOUND");
        snprintf(line, sizeof(line), "vs %s",
                 g_online_pending_match.opponent[0] ? g_online_pending_match.opponent : "opponent");
        online_hub_text(card_x + 26.0f * s, card_y + 70.0f * s,
                        0.98f * s, 0.76f, 0.98f, 0.86f, line);
        snprintf(line, sizeof(line), "%s",
                 g_online_pending_match.map_label[0] ? g_online_pending_match.map_label : "selected map");
        online_hub_text(card_x + 26.0f * s, card_y + 106.0f * s,
                        0.88f * s, 0.74f, 0.82f, 0.94f, line);
        snprintf(line, sizeof(line), "Starting in %d", seconds);
        online_hub_text(card_x + 26.0f * s, card_y + 144.0f * s,
                        1.00f * s, 1.00f, 0.86f, 0.44f, line);
    } else {
        online_hub_text(card_x + 26.0f * s, card_y + 25.0f * s,
                        1.28f * s, 0.94f, 0.98f, 1.00f, "SEARCHING");
        snprintf(line, sizeof(line), "%s queue",
                 g_online_queue_mode == 2 ? "Competitive" : "Casual");
        online_hub_text(card_x + 26.0f * s, card_y + 70.0f * s,
                        0.98f * s, 0.72f, 0.92f, 0.88f, line);
        snprintf(line, sizeof(line), "%d waiting",
                 g_online_queue_mode == 2 ? g_online_queue_competitive_count : g_online_queue_casual_count);
        online_hub_text(card_x + 26.0f * s, card_y + 106.0f * s,
                        0.90f * s, 0.78f, 0.84f, 0.92f, line);
        online_hub_draw_wait_dots(card_x + 86.0f * s, card_y + 157.0f * s, s);
    }

    rail_x = card_x + 26.0f * s;
    rail_y = card_y + card_h - 28.0f * s;
    rail_h = 8.0f * s;
    rail_w = match_found ? (card_w - 52.0f * s) : (card_w - 26.0f * s - bw - 58.0f * s);
    if (rail_w > 80.0f * s) {
        online_hub_draw_rect(rail_x, rail_y, rail_w, rail_h, 0.13f, 0.17f, 0.22f, 0.86f);
        online_hub_draw_border(rail_x, rail_y, rail_w, rail_h, 1.0f, 0.28f, 0.36f, 0.46f, 0.72f);
        if (match_found) {
            float total = (float)ONLINE_MATCH_COUNTDOWN_FRAMES;
            float remaining = clampf((float)g_online_pending_match.launch_countdown_frames, 0.0f, total);
            float progress = 1.0f - (remaining / total);
            online_hub_draw_rect(rail_x + 1.0f * s, rail_y + 1.0f * s,
                                 (rail_w - 2.0f * s) * progress, rail_h - 2.0f * s,
                                 0.96f, 0.80f, 0.28f, 0.96f);
        }
    }

    if (!match_found) {
        online_hub_draw_button_box(bx, by, bw, bh, "CANCEL", "",
                                   online_queue_cancel_button_at(g_online_mouse_x, g_online_mouse_y), 0, s);
    }
}

static void online_hub_render_queue_overlay(const OnlineLayout* L) {
    online_hub_render_matchmaking_panel(L, 0);
}

static void online_hub_render_match_countdown(const OnlineLayout* L) {
    online_hub_render_matchmaking_panel(L, 1);
}

static void online_hub_render_login_gateway(const OnlineLayout* L) {
    float s;
    float form_x;
    float form_y;
    float form_w;
    float row_h;
    float gap;
    float button_w;
    char username[ONLINE_HUB_TEXT_MAX];
    char password[ONLINE_HUB_TEXT_MAX];
    char server[192];
    int user_selected;
    int pass_selected;
    int login_selected;
    int register_selected;
    if (!L) return;
    s = L->ui;
    online_login_gateway_metrics(L, &form_x, &form_y, &form_w, &row_h, &gap);
    button_w = (form_w - gap) * 0.5f;

    online_format_setting_value(ONLINE_SETTING_USERNAME, username, sizeof(username));
    online_format_setting_value(ONLINE_SETTING_PASSWORD, password, sizeof(password));
    if (g_online_capture_active && g_online_capture_kind == ONLINE_CAPTURE_SETTING) {
        if (g_online_capture_target == ONLINE_SETTING_USERNAME) {
            snprintf(username, sizeof(username), "%s_", g_online_capture_buf);
        } else if (g_online_capture_target == ONLINE_SETTING_PASSWORD) {
            size_t n = strlen(g_online_capture_buf);
            if (n > sizeof(password) - 2) n = sizeof(password) - 2;
            memset(password, '*', n);
            password[n] = '_';
            password[n + 1] = '\0';
        }
    }
    snprintf(server, sizeof(server), "%s:%u", g_online_cfg.server_host, (unsigned int)g_online_cfg.server_port);
    user_selected = (g_online_selected_row == online_find_row_by_kind_id(ONLINE_ROW_SETTING, ONLINE_SETTING_USERNAME));
    pass_selected = (g_online_selected_row == online_find_row_by_kind_id(ONLINE_ROW_SETTING, ONLINE_SETTING_PASSWORD));
    login_selected = (g_online_selected_row == online_find_row_by_kind_id(ONLINE_ROW_ACTION, ONLINE_ACTION_LOGIN));
    register_selected = (g_online_selected_row == online_find_row_by_kind_id(ONLINE_ROW_ACTION, ONLINE_ACTION_REGISTER));

    mods_restore_render_state();
    online_hub_text_center(L->center_x, L->panel_y + 48.0f * s,
                           2.05f * s, 0.94f, 0.98f, 1.00f, "EGGNOGG+ ONLINE");
    online_hub_text_center(L->center_x, L->panel_y + 91.0f * s,
                           1.00f * s, 0.70f, 0.94f, 0.90f,
                           g_online_server_state == ONLINE_SERVER_CONNECTED ? "Connected. Sign in to continue." : "Sign in to play online.");

    online_hub_draw_rect(form_x - 30.0f * s, form_y - 42.0f * s,
                         form_w + 60.0f * s, row_h * 3.0f + gap * 2.0f + 106.0f * s,
                         0.030f, 0.038f, 0.050f, 0.90f);
    online_hub_draw_border(form_x - 30.0f * s, form_y - 42.0f * s,
                           form_w + 60.0f * s, row_h * 3.0f + gap * 2.0f + 106.0f * s,
                           1.0f, 0.22f, 0.70f, 0.66f, 0.72f);

    online_hub_draw_rect(form_x, form_y, form_w, row_h,
                         user_selected ? 0.060f : 0.045f,
                         user_selected ? 0.135f : 0.070f,
                         user_selected ? 0.130f : 0.090f,
                         0.96f);
    online_hub_draw_border(form_x, form_y, form_w, row_h, user_selected ? 2.0f : 1.0f,
                           user_selected ? 0.42f : 0.28f,
                           user_selected ? 0.92f : 0.52f,
                           user_selected ? 0.82f : 0.62f,
                           0.92f);
    mods_restore_render_state();
    online_hub_text(form_x + 18.0f * s, form_y + 14.0f * s,
                    0.98f * s, 0.70f, 0.82f, 0.90f, "USERNAME");
    online_hub_text_right(form_x + form_w - 18.0f * s, form_y + 14.0f * s,
                          1.04f * s, 0.94f, 0.98f, 1.00f, username[0] ? username : " ");

    online_hub_draw_rect(form_x, form_y + row_h + gap, form_w, row_h,
                         pass_selected ? 0.060f : 0.045f,
                         pass_selected ? 0.135f : 0.070f,
                         pass_selected ? 0.130f : 0.090f,
                         0.96f);
    online_hub_draw_border(form_x, form_y + row_h + gap, form_w, row_h, pass_selected ? 2.0f : 1.0f,
                           pass_selected ? 0.42f : 0.28f,
                           pass_selected ? 0.92f : 0.52f,
                           pass_selected ? 0.82f : 0.62f,
                           0.92f);
    mods_restore_render_state();
    online_hub_text(form_x + 18.0f * s, form_y + row_h + gap + 14.0f * s,
                    0.98f * s, 0.70f, 0.82f, 0.90f, "PASSWORD");
    online_hub_text_right(form_x + form_w - 18.0f * s, form_y + row_h + gap + 14.0f * s,
                          1.04f * s, 0.94f, 0.98f, 1.00f, password[0] ? password : " ");

    online_hub_draw_button_box(form_x, form_y + row_h * 2.0f + gap * 2.0f,
                               button_w, row_h, "LOG IN", "",
                               login_selected, 1, s);
    online_hub_draw_button_box(form_x + button_w + gap, form_y + row_h * 2.0f + gap * 2.0f,
                               button_w, row_h, "REGISTER", "",
                               register_selected, 0, s);

    mods_restore_render_state();
    online_hub_text_center(L->center_x, form_y + row_h * 3.0f + gap * 2.0f + 28.0f * s,
                           0.92f * s, 0.66f, 0.76f, 0.86f, server);
    if (g_online_status[0]) {
        online_hub_text_center(L->center_x, form_y + row_h * 3.0f + gap * 2.0f + 58.0f * s,
                               0.98f * s, 0.84f, 0.94f, 1.00f, g_online_status);
    }
    online_hub_text(L->panel_x + 18.0f * s, L->panel_y + L->panel_h - L->footer_h + 12.0f * s,
                    0.92f * s, 0.54f, 0.66f, 0.74f, "ESC");
    online_hub_draw_cursor();
}

static void online_hub_render_ui(void) {
    OnlineLayout L;
    int cap;
    int start;
    int end;
    float s;
    float rows_top;
    online_hub_rebuild_rows();
    online_calc_layout(&L);
    s = L.ui;
    g_ui_scale = L.text_scale;

    online_hub_draw_panel(&L);
    if (online_login_gateway_active()) {
        online_hub_render_login_gateway(&L);
        return;
    }

    mods_restore_render_state();
    online_hub_text_center(L.center_x, L.panel_y + 18.0f * s, 1.90f * s, 0.96f, 0.98f, 1.00f, "ONLINE HUB");
    {
        char subtitle[256];
        if (g_online_authed) {
            snprintf(subtitle, sizeof(subtitle), "%s  Elo %d", g_online_cfg.username, g_online_public_elo);
        } else {
            snprintf(subtitle, sizeof(subtitle), "%s", online_server_state_text());
        }
        online_hub_text_center(L.center_x, L.panel_y + 52.0f * s, 1.10f * s, 0.70f, 0.96f, 0.90f, subtitle);
    }
    online_hub_draw_tabs(&L);

    rows_top = L.list_top + 14.0f * s;
    if (g_online_status[0] && !g_online_queue_mode && !g_online_pending_match.active) {
        online_hub_text_center(L.center_x, L.panel_y + L.header_h + 34.0f * s,
                               1.04f * s, 0.82f, 0.94f, 1.00f, g_online_status);
        rows_top += 32.0f * s;
    }

    if (g_online_pending_match.active && g_online_pending_match.launch_countdown_frames > 0) {
        online_hub_render_match_countdown(&L);
        online_hub_draw_cursor();
        return;
    }

    if (g_online_queue_mode && !g_online_pending_match.active) {
        online_hub_render_queue_overlay(&L);
        online_hub_text(L.panel_x + 18.0f * s, L.panel_y + L.panel_h - L.footer_h + 12.0f * s,
                        0.88f * s, 0.40f, 0.48f, 0.58f, "ESC");
        online_hub_draw_cursor();
        return;
    }

    cap = (int)((L.list_bottom - rows_top) / L.row_h);
    if (cap < 4) cap = 4;
    start = clampi(g_online_scroll_row, 0, g_online_row_count);
    end = start + cap;
    if (end > g_online_row_count) end = g_online_row_count;
    for (int i = start; i < end; i++) {
        OnlineHubRow* row = &g_online_rows[i];
        float y = rows_top + (float)(i - start) * L.row_h;
        int selected = (i == g_online_selected_row);
        float box_x = L.content_x + 18.0f * s;
        float box_w = L.content_w - 36.0f * s;
        float box_h = L.row_h - 8.0f * s;
        char right[224];

        safe_copy(right, sizeof(right), row->right);
        if (g_online_capture_active && row->kind == ONLINE_ROW_SETTING &&
            g_online_capture_kind == ONLINE_CAPTURE_SETTING &&
            row->id == g_online_capture_target) {
            if (row->id == ONLINE_SETTING_PASSWORD) {
                size_t n = strlen(g_online_capture_buf);
                if (n > sizeof(right) - 2) n = sizeof(right) - 2;
                memset(right, '*', n);
                right[n] = '_';
                right[n + 1] = '\0';
            } else {
                snprintf(right, sizeof(right), "%s_", g_online_capture_buf);
            }
        } else if (g_online_capture_active && row->kind == ONLINE_ROW_ACTION &&
                   g_online_capture_kind == ONLINE_CAPTURE_ADD_FRIEND &&
                   row->id == ONLINE_ACTION_ADD_FRIEND) {
            snprintf(right, sizeof(right), "%s_", g_online_capture_buf);
        }

        if (row->kind == ONLINE_ROW_INFO) {
            float label_scale = (_stricmp(row->left, "Players in queue") == 0) ? 1.10f * s : 0.90f * s;
            float value_scale = (_stricmp(row->left, "Players in queue") == 0) ? 1.44f * s : 0.86f * s;
            online_hub_text(box_x, y + 8.0f * s, label_scale, 0.72f, 0.80f, 0.92f, row->left);
            if (right[0]) {
                online_hub_text_right(box_x + box_w, y + 7.0f * s, value_scale, 0.96f, 0.88f, 0.42f, right);
            }
        } else if (online_hub_row_boxed(row)) {
            if (row->kind == ONLINE_ROW_FRIEND_REQUEST || row->kind == ONLINE_ROW_CHALLENGE) {
                float accept_w = 86.0f * s;
                float decline_w = 92.0f * s;
                float action_h = box_h - 14.0f * s;
                float decline_x = box_x + box_w - 14.0f * s - decline_w;
                float accept_x = decline_x - 8.0f * s - accept_w;
                float action_y = y + 7.0f * s;
                float br = selected ? 0.075f : 0.050f;
                float bg = selected ? 0.140f : 0.070f;
                float bb = selected ? 0.135f : 0.090f;
                online_hub_draw_rect(box_x, y, box_w, box_h, br, bg, bb, selected ? 0.98f : 0.94f);
                online_hub_draw_border(box_x, y, box_w, box_h, selected ? 2.0f : 1.0f,
                                       selected ? 0.40f : 0.34f,
                                       selected ? 0.92f : 0.50f,
                                       selected ? 0.82f : 0.58f,
                                       selected ? 1.0f : 0.88f);
                mods_restore_render_state();
                online_hub_text(box_x + 16.0f * s, y + box_h * 0.5f - 11.0f * s,
                                0.98f * s, 0.92f, 0.98f, 1.00f, row->left);
                online_hub_text_right(accept_x - 14.0f * s, y + box_h * 0.5f - 10.0f * s,
                                      0.76f * s, 0.84f, 0.90f, 0.98f, right);

                online_hub_draw_rect(accept_x, action_y, accept_w, action_h,
                                     0.08f, 0.22f, 0.15f, selected ? 0.98f : 0.92f);
                online_hub_draw_border(accept_x, action_y, accept_w, action_h, 1.0f,
                                       0.36f, 0.86f, 0.52f, 0.95f);
                online_hub_draw_rect(decline_x, action_y, decline_w, action_h,
                                     0.22f, 0.08f, 0.10f, selected ? 0.98f : 0.92f);
                online_hub_draw_border(decline_x, action_y, decline_w, action_h, 1.0f,
                                       0.90f, 0.42f, 0.44f, 0.95f);
                mods_restore_render_state();
                online_hub_text_center(accept_x + accept_w * 0.5f, action_y + action_h * 0.5f - 8.0f * s,
                                       0.68f * s, 0.78f, 1.0f, 0.82f, "ACCEPT");
                online_hub_text_center(decline_x + decline_w * 0.5f, action_y + action_h * 0.5f - 8.0f * s,
                                       0.68f * s, 1.0f, 0.74f, 0.76f, "DECLINE");
            } else if (row->kind == ONLINE_ROW_SETTING || row->kind == ONLINE_ROW_FRIEND) {
                float br = selected ? 0.070f : 0.045f;
                float bg = selected ? 0.125f : 0.070f;
                float bb = selected ? 0.145f : 0.095f;
                if (row->kind == ONLINE_ROW_FRIEND && row->id >= 0 && row->id < g_online_friend_count &&
                    g_online_friends[row->id].blocked) {
                    br = selected ? 0.16f : 0.08f;
                    bg = selected ? 0.08f : 0.06f;
                    bb = selected ? 0.09f : 0.07f;
                }
                online_hub_draw_rect(box_x, y, box_w, box_h, br, bg, bb, selected ? 0.98f : 0.92f);
                online_hub_draw_border(box_x, y, box_w, box_h, selected ? 2.0f : 1.0f,
                                       selected ? 0.40f : 0.32f,
                                       selected ? 0.92f : 0.48f,
                                       selected ? 0.82f : 0.58f,
                                       selected ? 1.0f : 0.86f);
                mods_restore_render_state();
                online_hub_text(box_x + 18.0f * s, y + box_h * 0.5f - 11.0f * s,
                                1.00f * s,
                                selected ? 0.92f : 0.78f,
                                selected ? 0.98f : 0.86f,
                                selected ? 1.00f : 0.96f,
                                row->left);
                if (right[0]) {
                    float rr = 0.84f;
                    float rg = 0.90f;
                    float rb = 0.98f;
                    float right_edge = box_x + box_w - 18.0f * s;
                    if (row->kind == ONLINE_ROW_FRIEND && _stricmp(right, "online") == 0) {
                        rr = 0.62f; rg = 0.96f; rb = 0.70f;
                    } else if (row->kind == ONLINE_ROW_FRIEND && _stricmp(right, "offline") == 0) {
                        rr = 0.55f; rg = 0.60f; rb = 0.68f;
                    }
                    if (row->kind == ONLINE_ROW_FRIEND && row->id >= 0 && row->id < g_online_friend_count &&
                        g_online_friends[row->id].online) {
                        int sent = online_sent_challenge_pending(g_online_friends[row->id].name);
                        float bw = 112.0f * s;
                        float bh = box_h - 14.0f * s;
                        float bx = box_x + box_w - 14.0f * s - bw;
                        float by = y + 7.0f * s;
                        right_edge = bx - 14.0f * s;
                        online_hub_draw_rect(bx, by, bw, bh,
                                             sent ? 0.10f : 0.08f,
                                             sent ? 0.12f : 0.21f,
                                             sent ? 0.16f : 0.18f,
                                             selected ? 0.98f : 0.92f);
                        online_hub_draw_border(bx, by, bw, bh, 1.0f, 0.34f, 0.86f, 0.74f, 0.95f);
                        mods_restore_render_state();
                        online_hub_text_center(bx + bw * 0.5f, by + bh * 0.5f - 8.0f * s,
                                               0.66f * s,
                                               sent ? 0.72f : 0.76f,
                                               sent ? 0.82f : 1.0f,
                                               sent ? 0.94f : 0.92f,
                                               sent ? "SENT!" : "CHALLENGE");
                    }
                    online_hub_text_right(right_edge, y + box_h * 0.5f - 10.0f * s,
                                          0.90f * s, rr, rg, rb, right);
                }
            } else {
                online_hub_draw_button_box(box_x, y, box_w, box_h, row->left, right,
                                           selected, online_hub_row_primary(row), s);
            }
        }
    }

    if (g_online_scroll_row > 0) {
        online_hub_text_right(L.right_x, rows_top - 12.0f * s, 0.70f * s, 0.44f, 0.52f, 0.62f, "^");
    }
    if (g_online_scroll_row + cap < g_online_row_count) {
        online_hub_text_right(L.right_x, L.list_bottom - 8.0f * s, 0.70f * s, 0.44f, 0.52f, 0.62f, "v");
    }

    online_hub_text(L.panel_x + 18.0f * s, L.panel_y + L.panel_h - L.footer_h + 12.0f * s,
                    0.88f * s, 0.40f, 0.48f, 0.58f, "ESC");
    online_hub_draw_context_menu();
    online_hub_draw_cursor();
}

static void online_hub_open(void) {
    void* cur;
    if (!p_state_switch) return;
    online_hub_load();
    cur = p_state_current ? p_state_current() : NULL;
    if (cur == (void*)&g_console_state) cur = g_console_return_state;
    if (!cur || cur == (void*)&g_online_hub_state || cur == (void*)&g_online_result_state) {
        cur = (void*)(uintptr_t)ADDR_MAIN_STATE;
    }
    if (is_online_hub_state_active()) {
        g_online_return_state = cur;
        return;
    }
    g_online_pending_return_state = cur;
    g_online_open_pending = 1;
}

static void __cdecl online_hub_enter(void) {
    void* last = p_state_last ? p_state_last() : (void*)(uintptr_t)ADDR_MAIN_STATE;
    online_hub_load();
    if (!last || last == (void*)&g_online_hub_state || last == (void*)&g_console_state) {
        last = (void*)(uintptr_t)ADDR_MAIN_STATE;
    }
    g_online_return_state = last;
    g_online_capture_active = 0;
    online_hub_apply_net_settings();
    online_hub_rebuild_rows();
    if (!g_online_pending_match.active && !g_online_result.active) {
        online_hub_set_status("");
    }
    LOG_INFO("ONLINE HUB: enter");
}

static void __cdecl online_hub_update(void) {
    online_sent_challenge_prune();
    online_server_update();
    online_match_pump_launch();
    lua_manager_on_tick();
    lua_manager_on_tick_post();
    hooks_finish_game_tick();
}

static void online_hub_render_background(void) {
    float w = p_mad_w ? p_mad_w() : BASE_UI_W;
    float h = p_mad_h ? p_mad_h() : BASE_UI_H;
    mods_restore_render_state();
    if (g_console_bg_ready) {
        console_draw_background(w, h);
        online_hub_draw_rect(0.0f, 0.0f, w, h, 0.006f, 0.010f, 0.014f, 0.14f);
        return;
    }
    if (p_main_draw) {
        p_main_draw();
        mods_restore_render_state();
        if (p_menu_common_render) {
            p_menu_common_render();
            mods_restore_render_state();
        }
        if (p_main_sprite_batches_draw) p_main_sprite_batches_draw();
        mods_restore_render_state();
        online_hub_draw_rect(0.0f, 0.0f, w, h, 0.006f, 0.010f, 0.014f, 0.38f);
        return;
    }
    console_draw_background(w, h);
}

static void __cdecl online_hub_render(void) {
    mods_restore_render_state();
    online_hub_render_background();
    mods_restore_render_state();
    online_hub_render_ui();
    mods_restore_render_state();
    if (p_main_sprite_batches_draw) {
        p_main_sprite_batches_draw();
    }
    mods_restore_render_state();
}

static void __cdecl online_hub_leave(void) {
    g_online_capture_active = 0;
    online_hub_save();
    mods_restore_render_state();
    LOG_INFO("ONLINE HUB: leave");
}

static const char* online_result_title(OnlineMatchResult result) {
    if (result == ONLINE_MATCH_RESULT_WIN) return "YOU WON";
    if (result == ONLINE_MATCH_RESULT_LOSS) return "YOU LOST";
    if (result == ONLINE_MATCH_RESULT_DRAW) return "DRAW";
    return "GAME OVER";
}

static const char* online_result_button_label(int index) {
    return index == 0 ? "REQUEUE" : "ONLINE HUB";
}

static void online_result_toast_metrics(float* out_x,
                                        float* out_y,
                                        float* out_w,
                                        float* out_h,
                                        float* out_close_x,
                                        float* out_close_y,
                                        float* out_close_size) {
    float sw = p_mad_w ? p_mad_w() : BASE_UI_W;
    float sh = p_mad_h ? p_mad_h() : BASE_UI_H;
    float s = online_hub_ui_scale();
    float margin = 18.0f * s;
    float w = clampf(410.0f * s, 340.0f, sw - margin * 2.0f);
    float h = 128.0f * s;
    float x = sw - w - margin;
    float y = sh - h - margin;
    float close_size = 24.0f * s;
    float close_x;
    float close_y;
    if (x < margin) x = margin;
    if (y < margin) y = margin;
    close_x = x + w - close_size - 10.0f * s;
    close_y = y + 10.0f * s;
    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    if (out_close_x) *out_close_x = close_x;
    if (out_close_y) *out_close_y = close_y;
    if (out_close_size) *out_close_size = close_size;
}

static void online_result_render_toast(void) {
    float s;
    float x;
    float y;
    float w;
    float h;
    float close_x;
    float close_y;
    float close_size;
    float alpha;
    int hover;
    char line[256];
    if (!g_online_result.active) return;
    s = online_hub_ui_scale();
    if (g_online_result.toast_lifetime <= 0) g_online_result.toast_lifetime = ONLINE_RESULT_TOAST_FRAMES;
    alpha = online_ui_fade_alpha(g_online_result.toast_age, g_online_result.toast_lifetime, 90);
    if (alpha <= 0.01f) return;
    online_result_toast_metrics(&x, &y, &w, &h, &close_x, &close_y, &close_size);
    hover = online_ui_close_hit(g_online_mouse_x, g_online_mouse_y, close_x, close_y, close_size);
    online_ui_panel(x, y, w, h, alpha, 0);
    online_hub_draw_rect(x, y, 5.0f * s, h,
                         g_online_result.result == ONLINE_MATCH_RESULT_WIN ? 0.24f : 0.86f,
                         g_online_result.result == ONLINE_MATCH_RESULT_WIN ? 0.92f : 0.30f,
                         g_online_result.result == ONLINE_MATCH_RESULT_WIN ? 0.54f : 0.34f,
                         0.96f * alpha);
    mods_restore_render_state();
    online_hub_text_alpha(x + 18.0f * s, y + 18.0f * s,
                    1.10f * s,
                    g_online_result.result == ONLINE_MATCH_RESULT_WIN ? 0.72f : 1.00f,
                    g_online_result.result == ONLINE_MATCH_RESULT_WIN ? 1.00f : 0.72f,
                    g_online_result.result == ONLINE_MATCH_RESULT_WIN ? 0.76f : 0.70f,
                    alpha,
                    online_result_title(g_online_result.result));
    snprintf(line, sizeof(line), "%s - %s",
             g_online_result.opponent[0] ? g_online_result.opponent : "opponent",
             g_online_result.map_label[0] ? g_online_result.map_label : "selected map");
    online_hub_text_alpha(x + 18.0f * s, y + 60.0f * s,
                    0.78f * s, 0.86f, 0.91f, 0.98f, alpha, line);
    if (g_online_result.competitive && g_online_result.elo_delta_valid) {
        int delta = g_online_result.elo_after - g_online_result.elo_before;
        snprintf(line, sizeof(line), "Elo %d (%+d)", g_online_result.elo_after, delta);
    } else {
        snprintf(line, sizeof(line), "Match complete");
    }
    online_hub_text_alpha(x + 18.0f * s, y + 91.0f * s,
                    0.76f * s, 0.70f, 0.80f, 0.90f, alpha, line);
    online_ui_close_button(close_x, close_y, close_size, alpha, hover);
}

static void online_result_tick_toast(void) {
    if (!g_online_result.active) return;
    if (g_online_result.toast_lifetime <= 0) g_online_result.toast_lifetime = ONLINE_RESULT_TOAST_FRAMES;
    g_online_result.toast_age++;
    if (g_online_result.toast_age >= g_online_result.toast_lifetime) {
        g_online_result.active = 0;
    }
}

static void online_result_activate(void) {
    if (g_online_result.selected == 0) {
        const char* queue = (g_online_result.queue_mode == 2) ? "competitive" : "casual";
        if (!g_online_result.server_confirmed) {
            safe_copy(g_online_result.status, sizeof(g_online_result.status), "Waiting for server result...");
            return;
        }
        online_server_send_queue(queue);
        g_online_tab = ONLINE_TAB_PLAY;
        g_online_result.active = 0;
        online_hub_open();
    } else {
        g_online_tab = ONLINE_TAB_PLAY;
        g_online_result.active = 0;
        online_hub_open();
    }
}

static int online_result_close_at(float x, float y) {
    float close_x;
    float close_y;
    float close_size;
    if (!g_online_result.active) return 0;
    online_result_toast_metrics(NULL, NULL, NULL, NULL, &close_x, &close_y, &close_size);
    return online_ui_close_hit(x, y, close_x, close_y, close_size);
}

static int online_challenge_index_by_ref(int id, const char* from) {
    if (id > 0) {
        for (int i = 0; i < g_online_challenge_count; i++) {
            if (g_online_challenges[i].id == id) return i;
        }
    }
    if (!from || !from[0]) return -1;
    for (int i = 0; i < g_online_challenge_count; i++) {
        if (_stricmp(g_online_challenges[i].from, from) == 0) return i;
    }
    return -1;
}

static void online_challenge_toast_show(const char* from, int id, int elo, int expires_in) {
    if (!g_online_cfg.challenge_notifications || !from || !from[0]) return;
    memset(&g_online_challenge_toast, 0, sizeof(g_online_challenge_toast));
    g_online_challenge_toast.active = 1;
    g_online_challenge_toast.id = id;
    g_online_challenge_toast.elo = elo;
    if (expires_in <= 0) expires_in = 300;
    g_online_challenge_toast.expires_ms = (uint32_t)GetTickCount() + (uint32_t)expires_in * 1000u;
    safe_copy(g_online_challenge_toast.from, sizeof(g_online_challenge_toast.from), from);
}

static void online_challenge_toast_clear(int id, const char* from) {
    if (!g_online_challenge_toast.active) return;
    if (id > 0 && g_online_challenge_toast.id == id) {
        memset(&g_online_challenge_toast, 0, sizeof(g_online_challenge_toast));
        return;
    }
    if (from && from[0] && _stricmp(g_online_challenge_toast.from, from) == 0) {
        memset(&g_online_challenge_toast, 0, sizeof(g_online_challenge_toast));
    }
}

static void online_challenge_toast_metrics(float* out_x,
                                           float* out_y,
                                           float* out_w,
                                           float* out_h,
                                           float* out_accept_x,
                                           float* out_accept_y,
                                           float* out_accept_w,
                                           float* out_accept_h,
                                           float* out_decline_x,
                                           float* out_decline_y,
                                           float* out_decline_w,
                                           float* out_decline_h) {
    float sw = p_mad_w ? p_mad_w() : BASE_UI_W;
    float s = online_hub_ui_scale();
    float margin = 18.0f * s;
    float w = clampf(430.0f * s, 350.0f, sw - margin * 2.0f);
    float h = 156.0f * s;
    float x = sw - w - margin;
    float y = margin;
    float button_h = 34.0f * s;
    float gap = 10.0f * s;
    float decline_w = 112.0f * s;
    float accept_w = 104.0f * s;
    float decline_x = x + w - 18.0f * s - decline_w;
    float accept_x = decline_x - gap - accept_w;
    float button_y = y + h - button_h - 16.0f * s;
    if (x < margin) x = margin;
    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    if (out_accept_x) *out_accept_x = accept_x;
    if (out_accept_y) *out_accept_y = button_y;
    if (out_accept_w) *out_accept_w = accept_w;
    if (out_accept_h) *out_accept_h = button_h;
    if (out_decline_x) *out_decline_x = decline_x;
    if (out_decline_y) *out_decline_y = button_y;
    if (out_decline_w) *out_decline_w = decline_w;
    if (out_decline_h) *out_decline_h = button_h;
}

static void online_challenge_toast_close_metrics(float* out_x, float* out_y, float* out_size) {
    float x;
    float y;
    float w;
    float h;
    float s = online_hub_ui_scale();
    float size = 24.0f * s;
    online_challenge_toast_metrics(&x, &y, &w, &h, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    (void)h;
    if (out_x) *out_x = x + w - size - 10.0f * s;
    if (out_y) *out_y = y + 10.0f * s;
    if (out_size) *out_size = size;
}

static int online_challenge_toast_seconds_left(void) {
    uint32_t now_ms;
    int32_t left_ms;
    if (!g_online_challenge_toast.active) return 0;
    now_ms = (uint32_t)GetTickCount();
    left_ms = (int32_t)(g_online_challenge_toast.expires_ms - now_ms);
    if (left_ms <= 0) return 0;
    return (left_ms + 999) / 1000;
}

static int online_challenge_toast_action_at(float x, float y) {
    float ax;
    float ay;
    float aw;
    float ah;
    float dx;
    float dy;
    float dw;
    float dh;
    float close_x;
    float close_y;
    float close_size;
    if (!g_online_challenge_toast.active) return 0;
    online_challenge_toast_close_metrics(&close_x, &close_y, &close_size);
    if (online_ui_close_hit(x, y, close_x, close_y, close_size)) return 3;
    online_challenge_toast_metrics(NULL, NULL, NULL, NULL, &ax, &ay, &aw, &ah, &dx, &dy, &dw, &dh);
    if (x >= ax && x <= ax + aw && y >= ay && y <= ay + ah) return 1;
    if (x >= dx && x <= dx + dw && y >= dy && y <= dy + dh) return 2;
    return 0;
}

static int online_challenge_toast_activate(int action) {
    int idx;
    int id;
    char from[48];
    if (!g_online_challenge_toast.active || (action != 1 && action != 2 && action != 3)) return 0;
    if (action == 3) {
        memset(&g_online_challenge_toast, 0, sizeof(g_online_challenge_toast));
        return 1;
    }
    id = g_online_challenge_toast.id;
    safe_copy(from, sizeof(from), g_online_challenge_toast.from);
    idx = online_challenge_index_by_ref(id, from);
    if (idx >= 0) {
        id = g_online_challenges[idx].id;
        safe_copy(from, sizeof(from), g_online_challenges[idx].from);
    }
    online_server_send_challenge_action(action == 1 ? "challenge_accept" : "challenge_decline", id, from);
    if (idx >= 0) online_remove_challenge_index(idx);
    online_challenge_toast_clear(id, from);
    return 1;
}

static void online_challenge_toast_render(void) {
    float s;
    float x;
    float y;
    float w;
    float h;
    float ax;
    float ay;
    float aw;
    float ah;
    float dx;
    float dy;
    float dw;
    float dh;
    float close_x;
    float close_y;
    float close_size;
    float alpha;
    int left;
    int accept_hot;
    int decline_hot;
    int close_hot;
    char line[160];
    if (!g_online_challenge_toast.active) return;
    left = online_challenge_toast_seconds_left();
    if (left <= 0) {
        memset(&g_online_challenge_toast, 0, sizeof(g_online_challenge_toast));
        return;
    }
    s = online_hub_ui_scale();
    alpha = 1.0f;
    if (g_online_challenge_toast.age < ONLINE_CHALLENGE_TOAST_FADE_FRAMES) {
        alpha = clampf((float)g_online_challenge_toast.age / (float)ONLINE_CHALLENGE_TOAST_FADE_FRAMES, 0.20f, 1.0f);
    } else if (left <= 5) {
        alpha = clampf((float)left / 5.0f, 0.24f, 1.0f);
    }
    online_challenge_toast_metrics(&x, &y, &w, &h, &ax, &ay, &aw, &ah, &dx, &dy, &dw, &dh);
    accept_hot = online_challenge_toast_action_at(g_online_mouse_x, g_online_mouse_y) == 1;
    decline_hot = online_challenge_toast_action_at(g_online_mouse_x, g_online_mouse_y) == 2;
    close_hot = online_challenge_toast_action_at(g_online_mouse_x, g_online_mouse_y) == 3;
    online_challenge_toast_close_metrics(&close_x, &close_y, &close_size);

    online_ui_panel(x, y, w, h, alpha, accept_hot || decline_hot || close_hot);
    online_hub_draw_rect(x, y, 5.0f * s, h, 0.96f, 0.74f, 0.24f, 0.96f * alpha);
    online_ui_close_button(close_x, close_y, close_size, alpha, close_hot);
    mods_restore_render_state();
    online_hub_text_alpha(x + 18.0f * s, y + 17.0f * s,
                          1.08f * s, 1.0f, 0.92f, 0.62f, alpha, "CHALLENGE");
    snprintf(line, sizeof(line), "%s wants to play", g_online_challenge_toast.from[0] ? g_online_challenge_toast.from : "A friend");
    online_hub_text_alpha(x + 18.0f * s, y + 55.0f * s,
                          0.82f * s, 0.90f, 0.96f, 1.00f, alpha, line);
    snprintf(line, sizeof(line), "Expires in %ds", left);
    online_hub_text_alpha(x + 18.0f * s, y + 86.0f * s,
                          0.74f * s, 0.70f, 0.80f, 0.90f, alpha, line);

    online_hub_draw_rect(ax, ay, aw, ah,
                         accept_hot ? 0.10f : 0.08f,
                         accept_hot ? 0.30f : 0.22f,
                         accept_hot ? 0.18f : 0.15f,
                         0.94f * alpha);
    online_hub_draw_border(ax, ay, aw, ah, accept_hot ? 2.0f : 1.0f,
                           0.38f, 0.92f, 0.56f, 0.95f * alpha);
    online_hub_draw_rect(dx, dy, dw, dh,
                         decline_hot ? 0.30f : 0.22f,
                         decline_hot ? 0.09f : 0.08f,
                         decline_hot ? 0.11f : 0.10f,
                         0.94f * alpha);
    online_hub_draw_border(dx, dy, dw, dh, decline_hot ? 2.0f : 1.0f,
                           0.92f, 0.42f, 0.44f, 0.95f * alpha);
    mods_restore_render_state();
    online_hub_text_center_alpha(ax + aw * 0.5f, ay + ah * 0.5f - 8.0f * s,
                                 0.68f * s, 0.78f, 1.0f, 0.82f, alpha, "ACCEPT");
    online_hub_text_center_alpha(dx + dw * 0.5f, dy + dh * 0.5f - 8.0f * s,
                                 0.68f * s, 1.0f, 0.74f, 0.76f, alpha, "DECLINE");
}

static void online_challenge_toast_tick(void) {
    if (!g_online_challenge_toast.active) return;
    if (online_challenge_toast_seconds_left() <= 0) {
        memset(&g_online_challenge_toast, 0, sizeof(g_online_challenge_toast));
        return;
    }
    if (g_online_challenge_toast.age < 0x3fffffff) {
        g_online_challenge_toast.age++;
    }
}

static void __cdecl online_result_enter(void) {
    g_online_capture_active = 0;
    if (!g_online_result.active) {
        memset(&g_online_result, 0, sizeof(g_online_result));
        g_online_result.active = 1;
        g_online_result.result = ONLINE_MATCH_RESULT_NONE;
        g_online_result.toast_lifetime = ONLINE_RESULT_TOAST_FRAMES;
        safe_copy(g_online_result.status, sizeof(g_online_result.status), "Match ended.");
    }
    g_online_result.selected = 0;
    LOG_INFO("ONLINE RESULT: enter");
}

static void __cdecl online_result_update(void) {
    online_server_update();
    lua_manager_on_tick();
    lua_manager_on_tick_post();
    hooks_finish_game_tick();
}

static void online_result_render_overlay(const OnlineLayout* L) {
    (void)L;
    online_result_render_toast();
}

static void __cdecl online_result_render(void) {
    OnlineLayout L;
    float s;
    char line[256];
    mods_restore_render_state();
    online_hub_render_background();
    mods_restore_render_state();
    online_calc_layout(&L);
    s = L.ui;
    (void)s;
    (void)line;
    online_result_render_overlay(&L);
    online_hub_draw_cursor();
    mods_restore_render_state();
    if (p_main_sprite_batches_draw) p_main_sprite_batches_draw();
    mods_restore_render_state();
    return;
    online_hub_draw_rect(L.panel_x, L.panel_y, L.panel_w, L.panel_h, 0.02f, 0.025f, 0.035f, 0.94f);
    online_hub_draw_border(L.panel_x, L.panel_y, L.panel_w, L.panel_h, 2.0f,
                           g_online_result.result == ONLINE_MATCH_RESULT_WIN ? 0.54f : 0.72f,
                           g_online_result.result == ONLINE_MATCH_RESULT_WIN ? 0.86f : 0.50f,
                           g_online_result.result == ONLINE_MATCH_RESULT_WIN ? 0.42f : 0.36f,
                           1.0f);
    online_hub_text_center(L.center_x, L.panel_y + 58.0f * s, 1.75f * s,
                           g_online_result.result == ONLINE_MATCH_RESULT_WIN ? 0.72f : 0.98f,
                           g_online_result.result == ONLINE_MATCH_RESULT_WIN ? 0.95f : 0.66f,
                           g_online_result.result == ONLINE_MATCH_RESULT_WIN ? 0.52f : 0.48f,
                           online_result_title(g_online_result.result));

    snprintf(line, sizeof(line), "Opponent: %s",
             g_online_result.opponent[0] ? g_online_result.opponent : "opponent");
    online_hub_text_center(L.center_x, L.panel_y + 126.0f * s, 0.96f * s, 0.82f, 0.88f, 0.98f, line);
    snprintf(line, sizeof(line), "Map: %s",
             g_online_result.map_label[0] ? g_online_result.map_label : "selected map");
    online_hub_text_center(L.center_x, L.panel_y + 166.0f * s, 0.86f * s, 0.66f, 0.72f, 0.82f, line);

    if (g_online_result.competitive) {
        if (g_online_result.elo_delta_valid) {
            int delta = g_online_result.elo_after - g_online_result.elo_before;
            snprintf(line, sizeof(line), "Competitive Elo: %d (%+d)", g_online_result.elo_after, delta);
        } else {
            snprintf(line, sizeof(line), "Competitive result reported");
        }
    } else {
        snprintf(line, sizeof(line), "Casual match");
    }
    online_hub_text_center(L.center_x, L.panel_y + 218.0f * s, 0.92f * s, 0.96f, 0.88f, 0.42f, line);

    online_hub_text_center(L.center_x, L.panel_y + 262.0f * s, 0.80f * s, 0.48f, 0.58f, 0.70f,
                           g_online_result.status[0] ? g_online_result.status : "Result reported.");

    {
        float bw = 220.0f * s;
        float bh = 52.0f * s;
        float gap = 20.0f * s;
        float by = L.panel_y + L.panel_h - L.footer_h - 70.0f * s;
        for (int i = 0; i < 2; i++) {
            float bx = L.center_x - bw - gap * 0.5f + (float)i * (bw + gap);
            online_hub_draw_button_box(bx, by, bw, bh,
                                       online_result_button_label(i),
                                       i == 0 ? ((g_online_result.queue_mode == 2) ? "competitive" : "casual") : "",
                                       i == g_online_result.selected,
                                       i == 0,
                                       s);
        }
    }
    online_hub_draw_cursor();
    mods_restore_render_state();
    if (p_main_sprite_batches_draw) p_main_sprite_batches_draw();
    mods_restore_render_state();
}

static void __cdecl online_result_leave(void) {
    mods_restore_render_state();
    LOG_INFO("ONLINE RESULT: leave");
}

static uintptr_t online_player_ptr(int player_index) {
    if (!p_player_slots || player_index < 0 || player_index > 1) return 0;
    if (IsBadReadPtr((const void*)(p_player_slots + player_index), sizeof(uintptr_t))) return 0;
    return p_player_slots[player_index];
}

static int online_player_room(int player_index, int* out_room) {
    uintptr_t player = online_player_ptr(player_index);
    if (!player) return 0;
    if (IsBadReadPtr((const void*)player, PLAYER_SIZE)) return 0;
    if (out_room) *out_room = (int)*(signed char*)(player + PLAYER_OFS_ROOM);
    return 1;
}

static int online_player_screen_pos(int player_index, float y_offset, float* out_x, float* out_y) {
    uintptr_t player = online_player_ptr(player_index);
    float px;
    float py;
    float sw;
    float sh;
    float cam_x;
    float cam_y;
    float game_w;
    float game_h;
    float scale;
    if (!player || !out_x || !out_y) return 0;
    if (IsBadReadPtr((const void*)player, PLAYER_SIZE)) return 0;
    px = *(float*)(player + PLAYER_OFS_X);
    py = *(float*)(player + PLAYER_OFS_Y) + y_offset;
    sw = p_mad_w ? p_mad_w() : BASE_UI_W;
    sh = p_mad_h ? p_mad_h() : BASE_UI_H;
    cam_x = g_camera_x ? *g_camera_x : 0.0f;
    cam_y = g_camera_y ? *g_camera_y : 0.0f;
    game_w = (g_game_w_native && *g_game_w_native > 1.0f) ? *g_game_w_native : BASE_UI_W;
    game_h = (g_game_h_native && *g_game_h_native > 1.0f) ? *g_game_h_native : BASE_UI_H;
    scale = (sw / game_w < sh / game_h) ? (sw / game_w) : (sh / game_h);
    *out_x = sw * 0.5f + (px - cam_x) * scale;
    *out_y = sh * 0.5f + (py - cam_y) * scale;
    return 1;
}

static void online_draw_nametag(float cx, float cy, const char* text, int opponent) {
    float sw = p_mad_w ? p_mad_w() : BASE_UI_W;
    float s = clampf(0.78f * online_hub_ui_scale(), 0.90f, 1.12f);
    float text_w = approx_text_width(text, s);
    float y;
    if (!text || !text[0]) return;
    cx = clampf(cx, text_w * 0.5f + 6.0f, sw - text_w * 0.5f - 6.0f);
    y = cy - 10.0f * s;
    mods_restore_render_state();
    online_hub_text_center(cx, y + 4.0f * s,
                           s,
                           opponent ? 1.00f : 0.42f,
                           opponent ? 0.58f : 1.00f,
                           opponent ? 0.50f : 0.86f,
                           text);
}

void hooks_online_on_pre_swap(void) {
    void* state_ptr = p_state_current ? p_state_current() : NULL;
    int local_player;
    int remote_player;
    int local_room = 0;
    int remote_room = 0;
    float x;
    float y;
    if (g_online_open_pending) {
        g_online_open_pending = 0;
        g_online_return_state = g_online_pending_return_state;
        if (!g_online_return_state || g_online_return_state == (void*)&g_online_hub_state) {
            g_online_return_state = (void*)(uintptr_t)ADDR_MAIN_STATE;
        }
        (void)console_capture_background_now();
        if (p_state_switch && !is_online_hub_state_active()) {
            p_state_switch((void*)&g_online_hub_state);
            state_ptr = (void*)&g_online_hub_state;
        }
    }

    if (g_online_result.active) {
        online_result_render_toast();
        online_result_tick_toast();
        mods_restore_render_state();
        if (p_main_sprite_batches_draw) p_main_sprite_batches_draw();
        mods_restore_render_state();
    }
    if (g_online_challenge_toast.active) {
        online_challenge_toast_render();
        online_challenge_toast_tick();
        online_hub_draw_cursor();
        mods_restore_render_state();
        if (p_main_sprite_batches_draw) p_main_sprite_batches_draw();
        mods_restore_render_state();
    }

    if (!ggpo_net_active() || !g_online_active_match.active) return;
    if (state_ptr != (void*)(uintptr_t)ADDR_GAME_STATE &&
        state_ptr != (void*)(uintptr_t)ADDR_OPTIONS_STATE_PAUSED) {
        return;
    }
    local_player = ggpo_net_local_player();
    if (local_player < 0 || local_player > 1) local_player = clampi(g_online_active_match.local_player, 0, 1);
    remote_player = local_player ^ 1;

    if (online_player_room(local_player, &local_room) &&
        online_player_room(remote_player, &remote_room) &&
        local_room == remote_room &&
        online_player_screen_pos(remote_player, -16.0f, &x, &y)) {
        online_draw_nametag(x, y, g_online_active_match.opponent[0] ? g_online_active_match.opponent : "opponent", 1);
    }
    if (online_player_screen_pos(local_player, -16.0f, &x, &y)) {
        online_draw_nametag(x, y, "V", 0);
    }
    mods_restore_render_state();
    if (p_main_sprite_batches_draw) p_main_sprite_batches_draw();
    mods_restore_render_state();
}

static int online_result_keydown(int sym) {
    switch (sym) {
        case SDLK_ESCAPE:
            g_online_result.selected = 1;
            online_result_activate();
            return 1;
        case SDLK_LEFT:
        case SDLK_UP:
        case 'a': case 'A':
        case 'w': case 'W':
            g_online_result.selected = 0;
            return 1;
        case SDLK_RIGHT:
        case SDLK_DOWN:
        case 'd': case 'D':
        case 's': case 'S':
            g_online_result.selected = 1;
            return 1;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SPACE:
            online_result_activate();
            return 1;
        default:
            return 1;
    }
}

int hooks_online_hub_active(void) {
    return is_online_hub_state_active() || is_online_result_state_active();
}

int hooks_online_hub_keydown(int sym, int scancode, int mod) {
    (void)scancode;
    (void)mod;
    if (is_online_result_state_active()) return online_result_keydown(sym);
    if (!is_online_hub_state_active()) return 0;
    if (g_online_pending_match.active && g_online_pending_match.launch_countdown_frames > 0) return 1;
    if (g_online_queue_mode) {
        if (sym == SDLK_ESCAPE || sym == SDLK_BACKSPACE || sym == SDLK_DELETE) {
            online_server_leave_queue();
        }
        return 1;
    }

    if (g_online_capture_active) {
        if (sym == SDLK_ESCAPE) {
            online_cancel_capture();
            return 1;
        }
        if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER) {
            online_commit_capture();
            return 1;
        }
        if (sym == SDLK_BACKSPACE) {
            size_t len = strlen(g_online_capture_buf);
            if (len > 0) g_online_capture_buf[len - 1] = '\0';
            return 1;
        }
        if (sym == SDLK_DELETE) {
            g_online_capture_buf[0] = '\0';
            return 1;
        }
        return 1;
    }

    online_hub_rebuild_rows();
    switch (sym) {
        case SDLK_ESCAPE:
            if (p_state_switch) {
                void* target = g_online_return_state ? g_online_return_state : (void*)(uintptr_t)ADDR_MAIN_STATE;
                if (target == (void*)&g_online_hub_state) target = (void*)(uintptr_t)ADDR_MAIN_STATE;
                p_state_switch(target);
            }
            return 1;
        case SDLK_TAB:
        case 'e': case 'E':
            if (!g_online_authed) {
                online_move_selection(1, 1);
                return 1;
            }
            online_switch_tab(1);
            return 1;
        case 'q': case 'Q':
            if (!g_online_authed) {
                online_move_selection(-1, 1);
                return 1;
            }
            online_switch_tab(-1);
            return 1;
        case SDLK_UP:
        case 'w': case 'W':
            online_move_selection(-1, 1);
            return 1;
        case SDLK_DOWN:
        case 's': case 'S':
            online_move_selection(1, 1);
            return 1;
        case SDLK_PAGEUP:
            online_move_selection(-1, online_visible_rows_capacity() - 2);
            return 1;
        case SDLK_PAGEDOWN:
            online_move_selection(1, online_visible_rows_capacity() - 2);
            return 1;
        case SDLK_HOME:
            g_online_selected_row = online_first_selectable();
            online_ensure_scroll_visible();
            return 1;
        case SDLK_END:
            g_online_selected_row = online_last_selectable();
            online_ensure_scroll_visible();
            return 1;
        case SDLK_LEFT:
        case 'a': case 'A':
            online_adjust_selected(-1);
            return 1;
        case SDLK_RIGHT:
        case 'd': case 'D':
            online_adjust_selected(1);
            return 1;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SPACE:
            online_activate_selected();
            return 1;
        case SDLK_BACKSPACE:
        case SDLK_DELETE:
            if (online_selected_decline()) return 1;
            return 1;
        default:
            return 1;
    }
}

int hooks_online_hub_textinput(const char* text) {
    if (is_online_result_state_active()) return 1;
    if (!is_online_hub_state_active()) return 0;
    if (!g_online_capture_active) return 1;
    if (text && text[0]) {
        size_t len = strlen(g_online_capture_buf);
        for (const unsigned char* p = (const unsigned char*)text; *p; p++) {
            if (*p < 0x20) continue;
            if (len + 1 >= sizeof(g_online_capture_buf)) break;
            g_online_capture_buf[len++] = (char)*p;
        }
        g_online_capture_buf[len] = '\0';
    }
    return 1;
}

int hooks_online_hub_mousemotion(int x, int y) {
    g_online_mouse_x = (float)x;
    g_online_mouse_y = (float)y;
    if (!is_online_hub_state_active() && !is_online_result_state_active()) return 0;
    if (is_online_result_state_active()) {
        return 1;
    } else if (g_online_pending_match.active && g_online_pending_match.launch_countdown_frames > 0) {
        return 1;
    } else if (g_online_queue_mode) {
        return 1;
    } else if (!g_online_context_active) {
        int hit = online_row_at_point((float)x, (float)y);
        if (hit >= 0 && hit < g_online_row_count && g_online_rows[hit].selectable) {
            g_online_selected_row = hit;
            online_ensure_scroll_visible();
        }
    }
    return 1;
}

int hooks_online_hub_mousewheel(int y) {
    if (is_online_result_state_active()) return 1;
    if (g_online_pending_match.active && g_online_pending_match.launch_countdown_frames > 0) return 1;
    if (g_online_queue_mode) return 1;
    if (!is_online_hub_state_active()) return 0;
    if (y > 0) online_move_selection(-1, 3);
    else if (y < 0) online_move_selection(1, 3);
    return 1;
}

int hooks_online_hub_mousebutton(int x, int y, int button, int down) {
    int row;
    int tab;
    g_online_mouse_x = (float)x;
    g_online_mouse_y = (float)y;
    if (down && button == 1) {
        int challenge_action = online_challenge_toast_action_at((float)x, (float)y);
        if (challenge_action) {
            online_challenge_toast_activate(challenge_action);
            return 1;
        }
    }
    if (down && button == 1 && online_result_close_at((float)x, (float)y)) {
        g_online_result.active = 0;
        return 1;
    }
    if (!is_online_hub_state_active() && !is_online_result_state_active()) return 0;
    if (!down) return 1;
    if (is_online_result_state_active()) {
        if (button != 1) return 1;
        return 1;
    }
    if (g_online_pending_match.active && g_online_pending_match.launch_countdown_frames > 0) return 1;
    if (g_online_queue_mode) {
        if (button == 1 && online_queue_cancel_button_at((float)x, (float)y)) {
            online_server_leave_queue();
        }
        return 1;
    }
    if (button == 1 && g_online_context_active) {
        int action = online_context_action_at((float)x, (float)y);
        if (action) {
            online_context_activate(action);
            return 1;
        }
        g_online_context_active = 0;
    }
    tab = g_online_authed ? online_tab_at_point((float)x, (float)y) : -1;
    if (button == 1 && tab >= 0 && tab < ONLINE_TAB_COUNT) {
        g_online_tab = (OnlineHubTab)tab;
        g_online_selected_row = -1;
        g_online_scroll_row = 0;
        g_online_capture_active = 0;
        g_online_context_active = 0;
        online_hub_rebuild_rows();
        return 1;
    }
    row = online_row_at_point((float)x, (float)y);
    if (row >= 0 && row < g_online_row_count && g_online_rows[row].selectable) {
        g_online_selected_row = row;
        online_ensure_scroll_visible();
        if (button == 3 && g_online_rows[row].kind == ONLINE_ROW_FRIEND) {
            online_open_friend_context(g_online_rows[row].id, (float)x, (float)y);
            return 1;
        }
        if (button == 1) online_activate_selected_from_mouse((float)x);
    }
    return 1;
}

int hooks_online_hub_control_action(int action) {
    if (is_online_result_state_active()) {
        switch (action) {
            case 1:
            case 3:
                g_online_result.selected = 0;
                return 1;
            case 2:
            case 4:
                g_online_result.selected = 1;
                return 1;
            case 5:
                online_result_activate();
                return 1;
            case 6:
                g_online_result.selected = 1;
                online_result_activate();
                return 1;
            default:
                return 0;
        }
    }
    if (!is_online_hub_state_active()) return 0;
    if (g_online_pending_match.active && g_online_pending_match.launch_countdown_frames > 0) return 1;
    if (g_online_queue_mode) {
        if (action == 6) online_server_leave_queue();
        return 1;
    }
    if (g_online_capture_active) return 1;
    online_hub_rebuild_rows();
    switch (action) {
        case 1: online_move_selection(-1, 1); return 1;
        case 2: online_move_selection(1, 1); return 1;
        case 3: online_adjust_selected(-1); return 1;
        case 4: online_adjust_selected(1); return 1;
        case 5: online_activate_selected(); return 1;
        case 6:
            if (p_state_switch) {
                void* target = g_online_return_state ? g_online_return_state : (void*)(uintptr_t)ADDR_MAIN_STATE;
                if (target == (void*)&g_online_hub_state) target = (void*)(uintptr_t)ADDR_MAIN_STATE;
                p_state_switch(target);
            }
            return 1;
        default:
            return 0;
    }
}

int hooks_console_active(void) {
    return is_console_state_active();
}

void hooks_console_on_pre_swap(void) {
    /*
     * One-shot: apply the saved sound on/off setting at startup. The vanilla
     * engine loads DAT_0055a170 from settings.nogg but force-calls
     * mad_enable_synth(1) right after (app_state_init), so the actual synth flag
     * (ADDR_NATIVE_SYNTH_ENABLED) stays ON regardless of the saved setting until
     * the player toggles sound in Options. We sync the flag from the setting on
     * the first frame the game has a live state (init + settings load complete).
     */
    {
        static int g_startup_sound_applied = 0;
        if (!g_startup_sound_applied && p_state_current && p_state_current() != NULL) {
            const volatile int* sound_setting = (const volatile int*)(uintptr_t)ADDR_SOUND_SETTING;
            int want_on = (*sound_setting != 0);
            hooks_set_native_synth_enabled(want_on);
            g_startup_sound_applied = 1;
            LOG_INFO("hooks: applied startup sound setting (%s)", want_on ? "on" : "off");
        }
    }

    if (!g_console_open_pending) return;

    g_console_bg_ready = 0;
    g_console_bg_w = 0;
    g_console_bg_h = 0;
    (void)console_capture_background_now();

    g_console_open_pending = 0;
    g_console_open_ready = 1;
}

void hooks_console_pump(void) {
    if (!g_console_open_ready) return;
    g_console_open_ready = 0;
    g_console_return_state = g_console_pending_return_state;
    if (!g_console_return_state || g_console_return_state == (void*)&g_console_state) {
        g_console_return_state = (void*)(uintptr_t)ADDR_MAIN_STATE;
    }
    if (p_state_switch && !is_console_state_active()) {
        p_state_switch((void*)&g_console_state);
    }
}

int hooks_console_textinput(const char* text) {
    if (g_console_suppress_next_textinput > 0) {
        g_console_suppress_next_textinput--;
        return 1;
    }
    if (!is_console_state_active()) return 0;
    if (text && text[0]) {
        if (g_console_search_active) console_search_type(text);
        else console_insert_text(text);
    }
    return 1;
}

int hooks_console_control_action(int action) {
    if (!is_console_state_active()) return 0;
    switch (action) {
        case 1: console_history_step(-1); return 1;
        case 2: console_history_step(1);  return 1;
        case 3:
            if (g_console_cursor > 0) g_console_cursor--;
            return 1;
        case 4:
            if ((size_t)g_console_cursor < strlen(g_console_input)) g_console_cursor++;
            return 1;
        case 5: console_execute_input(); return 1;
        case 6: console_close(); return 1;
        default: return 1;
    }
}

int hooks_console_mousewheel(int y) {
    if (!is_console_state_active()) return 0;
    if (y > 0) console_scroll_by(3);
    else if (y < 0) console_scroll_by(-3);
    return 1;
}

int hooks_console_mousebutton(int x, int y, int button, int down) {
    float w;
    float h;
    float ui;
    float margin;
    float panel_x;
    float panel_w;
    float panel_h;
    float panel_y;
    float text_scale;
    float line_h;
    float input_y;
    size_t in_len;

    if (!is_console_state_active()) return 0;
    if (!down) return 1;

    if (button == 3) {
        console_paste_clipboard();
        return 1;
    }
    if (button == 2) {
        console_copy_output_to_clipboard();
        return 1;
    }
    if (button != 1) return 1;

    w = p_mad_w ? p_mad_w() : BASE_UI_W;
    h = p_mad_h ? p_mad_h() : BASE_UI_H;
    ui = calc_ui_scale();
    margin = 36.0f * ui;
    if (w < 640.0f) margin = 12.0f;
    else if (w < 900.0f) margin = 20.0f * ui;
    panel_x = margin;
    panel_w = w - (margin * 2.0f);
    if (panel_w < 260.0f) {
        panel_x = 10.0f;
        panel_w = w - 20.0f;
    }
    panel_h = h * 0.50f;
    if (h < 720.0f) panel_h = h - (44.0f * ui);
    if (h >= 860.0f) panel_h = h * 0.58f;
    if (h >= 1080.0f) panel_h = h * 0.62f;
    if (panel_h > h - (82.0f * ui)) panel_h = h - (82.0f * ui);
    if (panel_h < 220.0f) panel_h = 220.0f;
    panel_y = h - panel_h - (26.0f * ui);
    if (panel_y < 20.0f * ui) panel_y = 20.0f * ui;

    text_scale = clampf(0.92f * ui, 1.00f, 1.25f);
    {
        int text_q = (int)(text_scale * 4.0f + 0.5f);
        if (text_q < 1) text_q = 1;
        text_scale = (float)text_q / 4.0f;
    }
    line_h = (10.0f * text_scale) + (9.0f * ui);
    input_y = panel_y + panel_h - (27.0f * ui);
    if ((float)y < input_y - (8.0f * ui) || (float)y > input_y + line_h) {
        return 1;
    }

    in_len = strlen(g_console_input);
    if ((size_t)g_console_cursor > in_len) g_console_cursor = (int)in_len;
    {
        int avail_chars = (int)((panel_w - (36.0f * ui)) / (6.0f * text_scale));
        int start_idx = 0;
        float text_x = panel_x + (14.0f * ui);
        int clicked_col;
        int cursor;
        if (avail_chars < 12) avail_chars = 12;
        if (g_console_cursor > avail_chars - 4) {
            start_idx = g_console_cursor - (avail_chars - 4);
        }
        clicked_col = (int)(((float)x - text_x - (18.0f * text_scale)) / (9.0f * text_scale) + 0.5f);
        if (clicked_col < 0) clicked_col = 0;
        cursor = start_idx + clicked_col;
        if (cursor < 0) cursor = 0;
        if ((size_t)cursor > in_len) cursor = (int)in_len;
        g_console_cursor = cursor;
        console_detach_from_history();
    }
    return 1;
}

int hooks_console_keydown(int sym, int scancode, int mod) {
    (void)scancode;

    if (sym == SDLK_F5 || sym == SDLK_F6 || sym == SDLK_F7 || sym == SDLK_F9 || sym == SDLK_F10) {
        return 1;
    }

    if (!is_console_state_active() && sym == SDLK_F2) {
        void* cur = p_state_current ? p_state_current() : NULL;
        if (cur == (void*)(uintptr_t)ADDR_GAME_STATE) {
            run_ggpo_roundtrip_check("F2");
            return 1;
        }
    }
    if (!is_console_state_active() && sym == SDLK_F3) {
        void* cur = p_state_current ? p_state_current() : NULL;
        if (cur == (void*)(uintptr_t)ADDR_GAME_STATE) {
            toggle_ggpo_loopback("F3");
            return 1;
        }
    }
    if (!is_console_state_active() && sym == SDLK_F4) {
        void* cur = p_state_current ? p_state_current() : NULL;
        if (cur == (void*)(uintptr_t)ADDR_GAME_STATE) {
            toggle_ggpo_local("F4");
            return 1;
        }
    }
    if (!is_console_state_active() && sym == SDLK_F6) {
        void* cur = p_state_current ? p_state_current() : NULL;
        if (cur == (void*)(uintptr_t)ADDR_GAME_STATE) {
            start_ggpo_net_host(GGPO_NET_DEFAULT_PORT, "F6");
            return 1;
        }
    }
    if (!is_console_state_active() && sym == SDLK_F7) {
        void* cur = p_state_current ? p_state_current() : NULL;
        if (cur == (void*)(uintptr_t)ADDR_GAME_STATE) {
            start_ggpo_net_join("127.0.0.1", GGPO_NET_DEFAULT_PORT, 0, "F7");
            return 1;
        }
    }

    if (sym == '`') {
        if (g_online_active_match.active && !g_online_active_match.result_reported) {
            if (is_console_state_active()) console_close();
            return 1;
        }
        g_console_suppress_next_textinput = 1;
        if (is_console_state_active()) {
            console_close();
        } else if (g_console_open_pending || g_console_open_ready) {
            g_console_open_pending = 0;
            g_console_open_ready = 0;
        } else {
            console_open();
        }
        return 1;
    }

    if (!is_console_state_active()) return 0;

    if (g_console_search_active) {
        if (sym == SDLK_ESCAPE) { console_search_cancel(); return 1; }
        if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER) { console_search_accept(); return 1; }
        if (sym == SDLK_BACKSPACE) { console_search_backspace(); return 1; }
        if ((mod & KMOD_CTRL) && (sym == 'r' || sym == 'R')) { console_search_step_older(); return 1; }
        if (sym == SDLK_TAB) { console_search_accept(); return 1; }
        if (sym == SDLK_LEFT || sym == SDLK_RIGHT || sym == SDLK_HOME || sym == SDLK_END ||
            sym == SDLK_UP || sym == SDLK_DOWN || sym == SDLK_DELETE) {
            console_search_accept();  // accept, then let the navigation key act normally
        } else {
            return 1;                 // other keys (incl. modifiers) ignored; text via textinput
        }
    }

    if (mod & KMOD_CTRL) {
        if (sym == 'v' || sym == 'V') {
            console_paste_clipboard();
            return 1;
        }
        if (sym == 'c' || sym == 'C') {
            if (g_console_input[0]) console_copy_input_to_clipboard();
            else console_copy_output_to_clipboard();
            return 1;
        }
        if (sym == 'x' || sym == 'X') {
            if (g_console_input[0]) {
                console_copy_input_to_clipboard();
                console_set_input("");
                console_detach_from_history();
            }
            return 1;
        }
        if (sym == 'a' || sym == 'A') {
            g_console_cursor = (int)strlen(g_console_input);
            return 1;
        }
        if (sym == 'r' || sym == 'R') {
            console_search_begin();
            return 1;
        }
    }

    switch (sym) {
        case SDLK_ESCAPE:
            console_close();
            return 1;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            console_execute_input();
            return 1;
        case SDLK_TAB:
            console_autocomplete((mod & KMOD_SHIFT) != 0);
            return 1;
        case SDLK_BACKSPACE:
            console_backspace();
            return 1;
        case SDLK_DELETE:
            console_delete();
            return 1;
        case SDLK_LEFT:
            if (g_console_cursor > 0) g_console_cursor--;
            console_compl_reset();
            return 1;
        case SDLK_RIGHT:
            if ((size_t)g_console_cursor < strlen(g_console_input)) g_console_cursor++;
            console_compl_reset();
            return 1;
        case SDLK_HOME:
            g_console_cursor = 0;
            console_compl_reset();
            return 1;
        case SDLK_END:
            g_console_cursor = (int)strlen(g_console_input);
            console_compl_reset();
            return 1;
        case SDLK_UP:
            console_history_step(-1);
            return 1;
        case SDLK_DOWN:
            console_history_step(1);
            return 1;
        case SDLK_PAGEUP:
            console_scroll_by(8);
            return 1;
        case SDLK_PAGEDOWN:
            console_scroll_by(-8);
            return 1;
        case 'l':
        case 'L':
            if (mod & KMOD_CTRL) {
                console_clear_output();
                return 1;
            }
            return 1;
        default:
            return 1;
    }
}

int hooks_text_capture_active(void) {
    return g_capture_active;
}

int hooks_mods_menu_active(void) {
    return is_mods_state_active();
}

void hooks_mods_menu_notify_reload(void) {
    capture_clear();

    if (is_mods_state_active()) {
        rebuild_rows();
        mods_cursor_tick();
    }
}

int hooks_text_capture_keydown(int sym, int scancode, int mod) {
    (void)scancode;

    if (!g_capture_active) return 0;
    if (!is_mods_state_active()) {
        capture_clear();
        return 0;
    }

    if (g_capture_kind == CAPTURE_BIND) {
        if (sym == SDLK_ESCAPE) {
            capture_clear();
            rebuild_rows();
            return 1;
        }
        if (sym == SDLK_BACKSPACE || sym == SDLK_DELETE) {
            lua_manager_clear_mod_bind_value(g_capture_mod, g_capture_cfg);
            capture_clear();
            rebuild_rows();
            return 1;
        }
        if (sym == '`') {
            capture_clear();
            rebuild_rows();
            return 1;
        }
        lua_manager_set_mod_bind_value(g_capture_mod, g_capture_cfg, sym);
        capture_clear();
        rebuild_rows();
        return 1;
    }

    if (sym == SDLK_ESCAPE) {
        capture_clear();
        return 1;
    }

    if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER) {
        lua_manager_config_set_string(g_capture_mod, g_capture_cfg, g_capture_buf);
        capture_clear();
        rebuild_rows();
        return 1;
    }

    if (sym == SDLK_BACKSPACE) {
        size_t len = strlen(g_capture_buf);
        if (len > 0) g_capture_buf[len - 1] = '\0';
        return 1;
    }

    if (sym == SDLK_DELETE) {
        g_capture_buf[0] = '\0';
        return 1;
    }

    {
        char c = keycode_to_char(sym, mod);
        if (c) {
            size_t len = strlen(g_capture_buf);
            if (len + 1 < sizeof(g_capture_buf)) {
                g_capture_buf[len] = c;
                g_capture_buf[len + 1] = '\0';
            }
            return 1;
        }
    }

    return 1;
}

int hooks_mods_menu_keydown(int sym, int scancode, int mod) {
    (void)scancode;
    (void)mod;

    if (!is_mods_state_active()) return 0;
    if (g_capture_active) return 1;

    rebuild_rows();

    switch (sym) {
        case SDLK_ESCAPE:
            mods_go_back();
            return 1;

        case SDLK_UP:
        case 'w':
        case 'W':
            move_selection(-1, 1);
            return 1;

        case SDLK_DOWN:
        case 's':
        case 'S':
            move_selection(1, 1);
            return 1;

        case SDLK_PAGEUP: {
            int step = visible_rows_capacity() - 2;
            if (step < 1) step = 1;
            move_selection(-1, step);
            return 1;
        }

        case SDLK_PAGEDOWN: {
            int step = visible_rows_capacity() - 2;
            if (step < 1) step = 1;
            move_selection(1, step);
            return 1;
        }

        case SDLK_HOME: {
            int prev = g_selected_row;
            int first = first_selectable_index();
            if (first >= 0) g_selected_row = first;
            ensure_scroll_visible();
            if (g_selected_row != prev) mods_cursor_on_selection_changed();
            return 1;
        }

        case SDLK_END: {
            int prev = g_selected_row;
            int last = last_selectable_index();
            if (last >= 0) g_selected_row = last;
            ensure_scroll_visible();
            if (g_selected_row != prev) mods_cursor_on_selection_changed();
            return 1;
        }

        case SDLK_LEFT:
        case 'a':
        case 'A':
            apply_adjustment_on_selected(-1);
            return 1;

        case SDLK_RIGHT:
        case 'd':
        case 'D':
            apply_adjustment_on_selected(1);
            return 1;

        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SPACE:
        case 'z': case 'Z':
        case 'x': case 'X':
        case 'c': case 'C':
        case 'v': case 'V':
        case 'f': case 'F':
        case 'g': case 'G':
        case 'h': case 'H':
        case 'j': case 'J':
        case 'k': case 'K':
        case 'l': case 'L':
        case ';':
        case ',':
        case '.':
        case '/':
            activate_selected();
            return 1;

        default:
            return 0;
    }
}

int hooks_mods_menu_control_action(int action) {
    if (!is_mods_state_active()) return 0;
    if (g_capture_active) return 1;

    rebuild_rows();

    switch (action) {
        case 1: move_selection(-1, 1); return 1;
        case 2: move_selection(1, 1); return 1;
        case 3: apply_adjustment_on_selected(-1); return 1;
        case 4: apply_adjustment_on_selected(1); return 1;
        case 5: activate_selected(); return 1;
        case 6: mods_go_back(); return 1;
        default: return 0;
    }
}

void hooks_set_tick_input(int player_index, uint32_t cmd_mask, int ticks, int replace) {
    int pi = (player_index & 1);
    if (ticks == 0) {
        g_tick_input_mask[pi] = 0;
        g_tick_input_ticks[pi] = 0;
        g_tick_input_replace[pi] = 0;
        return;
    }
    g_tick_input_mask[pi] = cmd_mask;
    g_tick_input_ticks[pi] = ticks;
    g_tick_input_replace[pi] = replace ? 1 : 0;
}

void hooks_clear_tick_input(int player_index) {
    int pi = (player_index & 1);
    g_tick_input_mask[pi] = 0;
    g_tick_input_ticks[pi] = 0;
    g_tick_input_replace[pi] = 0;
}

int hooks_get_tick_input(int player_index, uint32_t* out_mask, int* out_ticks, int* out_replace) {
    int pi = (player_index & 1);
    int ticks = g_tick_input_ticks[pi];
    if (out_mask) *out_mask = g_tick_input_mask[pi];
    if (out_ticks) *out_ticks = ticks;
    if (out_replace) *out_replace = g_tick_input_replace[pi];
    return ticks != 0;
}

void hooks_set_raw_input_blocked(int player_index, int blocked) {
    int pi = (player_index & 1);
    g_raw_input_blocked[pi] = blocked ? 1 : 0;
}

int hooks_get_raw_input_blocked(int player_index) {
    int pi = (player_index & 1);
    return g_raw_input_blocked[pi] != 0;
}

void hooks_set_input_override(int player_index, uint32_t cmd_mask, int frames, int replace) {
    int pi = (player_index & 1);
    if (frames == 0) {
        g_input_override_mask[pi] = 0;
        g_input_override_frames[pi] = 0;
        g_input_override_replace[pi] = 0;
        return;
    }
    g_input_override_mask[pi] = cmd_mask;
    g_input_override_frames[pi] = frames;
    g_input_override_replace[pi] = replace ? 1 : 0;
}

void hooks_clear_input_override(int player_index) {
    int pi = (player_index & 1);
    g_input_override_mask[pi] = 0;
    g_input_override_frames[pi] = 0;
    g_input_override_replace[pi] = 0;
}

int hooks_get_input_override(int player_index, uint32_t* out_mask, int* out_frames, int* out_replace) {
    int pi = (player_index & 1);
    int frames = g_input_override_frames[pi];
    if (out_mask) *out_mask = g_input_override_mask[pi];
    if (out_frames) *out_frames = frames;
    if (out_replace) *out_replace = g_input_override_replace[pi];
    return frames != 0;
}

static uint32_t hooks_apply_effective_overrides(uint32_t player_index, uint32_t cmd, int consume_poll_override) {
    int pi = (int)(player_index & 1u);

    {
        int ticks = g_tick_input_ticks[pi];
        if (ticks != 0) {
            uint32_t mask = g_tick_input_mask[pi];
            if (g_tick_input_replace[pi]) cmd = mask;
            else cmd |= mask;
        }
    }

    {
        int frames = g_input_override_frames[pi];
        if (frames != 0) {
            uint32_t mask = g_input_override_mask[pi];
            if (g_input_override_replace[pi]) cmd = mask;
            else cmd |= mask;

            if (consume_poll_override && frames > 0) {
                frames--;
                g_input_override_frames[pi] = frames;
                if (frames == 0) {
                    g_input_override_mask[pi] = 0;
                    g_input_override_replace[pi] = 0;
                }
            }
        }
    }

    g_last_effective_cmd[pi] = cmd;
    return cmd;
}

static void hooks_finish_game_tick(void) {
    for (int pi = 0; pi < 2; pi++) {
        int ticks = g_tick_input_ticks[pi];
        if (ticks > 0) {
            ticks--;
            g_tick_input_ticks[pi] = ticks;
            if (ticks == 0) {
                g_tick_input_mask[pi] = 0;
                g_tick_input_replace[pi] = 0;
            }
        }
    }
}

static void hooks_prepare_deterministic_game_update(void) {
    uint32_t native_ticks = 0;
    if (g_debug) {
        *g_debug = 0;
    }
    if (g_debug_slowmo) {
        *g_debug_slowmo = 0;
    }
    if (g_mad_ticks && lua_manager_game_native_ticks(&native_ticks)) {
        *g_mad_ticks = native_ticks;
    }
}

void hooks_sync_mad_ticks_to_game_clock(void) {
    uint32_t native_ticks = 0;
    if (g_mad_ticks && lua_manager_game_native_ticks(&native_ticks)) {
        *g_mad_ticks = native_ticks;
    }
}

uint32_t hooks_peek_player_cmds_raw(int player_index, int mode) {
    fn_main_player_poll_cmds_t real_poll = p_main_player_poll_cmds_trampoline
        ? p_main_player_poll_cmds_trampoline
        : p_main_player_poll_cmds;
    uint32_t pi = (uint32_t)(player_index & 1);
    uint32_t raw = real_poll ? real_poll(pi, (uint32_t)mode) : 0u;
    g_last_raw_cmd[pi & 1u] = raw;
    return raw;
}

uint32_t hooks_peek_player_cmds_effective(int player_index, int mode) {
    uint32_t pi = (uint32_t)(player_index & 1);
    uint32_t raw = hooks_peek_player_cmds_raw(player_index, mode);
    return hooks_apply_effective_overrides(pi, raw, 0);
}

void hooks_block_next_game_tick(int block) {
    g_block_game_tick_once = block ? 1 : 0;
}

static int hooks_can_run_simulated_game_update(void* state_ptr) {
    if (state_ptr == (void*)(uintptr_t)ADDR_GAME_STATE) return 1;
    if (g_allow_paused_game_tick &&
        state_ptr == (void*)(uintptr_t)ADDR_OPTIONS_STATE_PAUSED &&
        ggpo_net_active()) {
        return 1;
    }
    return 0;
}

static int hooks_should_clear_native_pause_for_tick(void* state_ptr) {
    return g_allow_paused_game_tick &&
           state_ptr == (void*)(uintptr_t)ADDR_OPTIONS_STATE_PAUSED &&
           ggpo_net_active();
}

int hooks_simulate_game_ticks(int count, int arg0) {
    fn_game_update_t real_update = p_game_update_trampoline
        ? p_game_update_trampoline
        : p_game_update;
    int ran = 0;

    if (count <= 0) return 0;
    if (!real_update) return -1;

    for (int i = 0; i < count; i++) {
        void* state_ptr = p_state_current ? p_state_current() : NULL;
        int restore_paused = 0;
        int old_paused = 0;
        if (!hooks_can_run_simulated_game_update(state_ptr)) {
            return (ran > 0) ? ran : -1;
        }
        if (hooks_should_clear_native_pause_for_tick(state_ptr) && g_native_paused) {
            old_paused = *g_native_paused;
            *g_native_paused = 0;
            restore_paused = 1;
        }
        hooks_prepare_deterministic_game_update();
        real_update(arg0);
        if (restore_paused) {
            *g_native_paused = old_paused;
        }
        hooks_finish_game_tick();
        ran++;
    }

    return ran;
}

int hooks_advance_game_tick(int arg0, int run_framework_tick) {
    fn_game_update_t real_update = p_game_update_trampoline
        ? p_game_update_trampoline
        : p_game_update;
    void* state_ptr = p_state_current ? p_state_current() : NULL;

    if (!real_update) return -1;
    if (state_ptr != (void*)(uintptr_t)ADDR_GAME_STATE) return -1;

    if (run_framework_tick) {
        lua_manager_on_tick();
        state_ptr = p_state_current ? p_state_current() : NULL;
        if (state_ptr != (void*)(uintptr_t)ADDR_GAME_STATE) {
            lua_manager_on_tick_post();
            return -1;
        }
    }

    if (hooks_consume_block_game_tick()) {
        if (run_framework_tick) lua_manager_on_tick_post();
        return 0;
    }

    hooks_prepare_deterministic_game_update();
    real_update(arg0);
    if (run_framework_tick) lua_manager_on_tick_post();
    hooks_finish_game_tick();
    return 1;
}

static void render_rows(void) {
    rebuild_rows();

    ModsLayout L;
    mods_calc_layout(&L);
    g_ui_scale = L.text_scale;
    float ui = L.ui;

    mods_restore_render_state();

    float header_cx = L.center_x;
    float title_y   = 28.0f * ui;
    float help_y    = 60.0f * ui;

    draw_text_centered_scaled(header_cx, title_y, g_ui_scale * 1.16f,
                              0.90f, 0.94f, 0.98f,
                              "MOD MANAGER");
    if (g_capture_active && g_capture_kind == CAPTURE_CONFIG_STRING) {
        draw_text_centered_scaled(header_cx, help_y, g_ui_scale * 0.92f,
                                  0.90f, 0.80f, 0.40f,
                                  "Editing text (Enter = apply, Esc = cancel)");
    } else if (g_capture_active && g_capture_kind == CAPTURE_BIND) {
        draw_text_centered_scaled(header_cx, help_y, g_ui_scale * 0.92f,
                                  0.90f, 0.80f, 0.40f,
                                  "Binding key (press key, Backspace/Delete = clear, Esc = cancel)");
    } else {
        draw_text_centered_scaled(header_cx, help_y, g_ui_scale * 0.88f,
                                  0.58f, 0.64f, 0.72f,
                                  "Enter expand/collapse or edit   Left/Right adjust   Esc back");
    }

    int cap = visible_rows_capacity();
    int start_row = g_scroll_row;
    if (start_row < 0) start_row = 0;
    if (start_row > g_row_count) start_row = g_row_count;
    int end_row = start_row + cap;
    if (end_row > g_row_count) end_row = g_row_count;

    for (int i = start_row; i < end_row; i++) {
        float y = L.list_top + (float)(i - start_row) * L.row_h;
        MenuRow* row = &g_rows[i];
        int selected = (i == g_selected_row);
        float base_r = 0.78f, base_g = 0.82f, base_b = 0.88f;
        float dim_r  = 0.54f, dim_g  = 0.60f, dim_b  = 0.68f;
        /* Without a selection bar, the active row needs a clearly different
           text tint. Use a warm gold instead of subtle near-white. */
        float sel_r  = 0.96f, sel_g  = 0.86f, sel_b  = 0.42f;
        float muted_r = 0.42f, muted_g = 0.48f, muted_b = 0.56f;
        float label_x = L.left + (24.0f * ui);
        float option_x = L.left + (44.0f * ui);
        float right_x = L.right - (20.0f * ui);

        if (row->kind == ROW_INFO && row->left[0] == '\0' && row->right[0] == '\0') continue;

        switch (row->kind) {
            case ROW_MOD_HEADER: {
                float rr = selected ? sel_r : 0.76f;
                float gg = selected ? sel_g : 0.82f;
                float bb = selected ? sel_b : 0.88f;
                draw_text_scaled(label_x, y, g_ui_scale * 1.02f, rr, gg, bb, row->left);
                if (row->right[0]) {
                    float sr = 0.50f, sg = 0.80f, sb = 0.58f;
                    if (_stricmp(row->right, "enabled") != 0) { sr = 0.66f; sg = 0.70f; sb = 0.76f; }
                    if (selected) {
                        sr = clampf(sr + 0.14f, 0.0f, 1.0f);
                        sg = clampf(sg + 0.14f, 0.0f, 1.0f);
                        sb = clampf(sb + 0.14f, 0.0f, 1.0f);
                    }
                    draw_text_right_scaled(right_x, y, g_ui_scale * 0.82f, sr, sg, sb, row->right);
                }
            } break;

            case ROW_DIVIDER: {
                /* No divider bar here; the section spacing already does the job,
                   and the old line read as a purple stripe over the scene. */
            } break;

            case ROW_MOD_TOGGLE:
            case ROW_CONFIG:
            case ROW_BIND: {
                float lr = selected ? sel_r : base_r;
                float lg = selected ? sel_g : base_g;
                float lb = selected ? sel_b : base_b;
                float vr = dim_r, vg = dim_g, vb = dim_b;
                if (row->kind == ROW_MOD_TOGGLE) {
                    if (row->right[0] == 'O' && row->right[1] == 'N') { vr = 0.48f; vg = 0.88f; vb = 0.58f; }
                    else { vr = 0.74f; vg = 0.76f; vb = 0.82f; }
                } else if (row->kind == ROW_BIND) {
                    if (lua_manager_mod_bind_has_conflict(row->mod_index, row->cfg_index)) {
                        vr = 0.92f; vg = 0.72f; vb = 0.44f;
                    } else {
                        vr = 0.62f; vg = 0.72f; vb = 0.84f;
                    }
                } else {
                    int type = lua_manager_get_mod_config_type(row->mod_index, row->cfg_index);
                    if (type == LUA_CFG_BOOL) {
                        if (row->right[0] == 'O' && row->right[1] == 'N') { vr = 0.48f; vg = 0.88f; vb = 0.58f; }
                        else { vr = 0.74f; vg = 0.76f; vb = 0.82f; }
                    } else if (type == LUA_CFG_ACTION) {
                        vr = 0.62f; vg = 0.72f; vb = 0.84f;
                    } else if (type == LUA_CFG_ENUM) {
                        vr = 0.58f; vg = 0.80f; vb = 0.82f;  /* teal: cyclable choice */
                    }
                }
                if (selected) {
                    vr = clampf(vr + 0.04f, 0.0f, 1.0f);
                    vg = clampf(vg + 0.04f, 0.0f, 1.0f);
                    vb = clampf(vb + 0.04f, 0.0f, 1.0f);
                }

                draw_text_scaled(option_x, y, g_ui_scale * 0.96f, lr, lg, lb, row->left);

                if (g_capture_active && row->mod_index == g_capture_mod && row->cfg_index == g_capture_cfg) {
                    if (row->kind == ROW_BIND && g_capture_kind == CAPTURE_BIND) {
                        draw_text_right_scaled(right_x, y, g_ui_scale * 0.92f, vr, vg, vb, "<press key>");
                    } else if (row->kind == ROW_CONFIG && g_capture_kind == CAPTURE_CONFIG_STRING) {
                        char live[CAPTURE_BUF_SIZE + 8];
                        snprintf(live, sizeof(live), "\"%s|\"", g_capture_buf);
                        draw_text_right_scaled(right_x, y, g_ui_scale * 0.92f, vr, vg, vb, live);
                    } else {
                        draw_text_right_scaled(right_x, y, g_ui_scale * 0.92f, vr, vg, vb, row->right);
                    }
                } else {
                    draw_text_right_scaled(right_x, y, g_ui_scale * 0.92f, vr, vg, vb, row->right);
                }
            } break;

            case ROW_FW_HEADER: {
                draw_text_scaled(label_x, y, g_ui_scale * 1.02f, 0.76f, 0.82f, 0.88f, row->left);
            } break;

            case ROW_FW_TOGGLE:
            case ROW_FW_ACTION: {
                float lr = selected ? sel_r : base_r;
                float lg = selected ? sel_g : base_g;
                float lb = selected ? sel_b : base_b;
                float vr, vg, vb;
                if (row->kind == ROW_FW_TOGGLE) {
                    if (row->right[0] == 'O' && row->right[1] == 'N') { vr = 0.48f; vg = 0.88f; vb = 0.58f; }
                    else { vr = 0.74f; vg = 0.76f; vb = 0.82f; }
                } else { /* ROW_FW_ACTION: button tint, matches LUA_CFG_ACTION */
                    vr = 0.62f; vg = 0.72f; vb = 0.84f;
                }
                if (selected) {
                    vr = clampf(vr + 0.04f, 0.0f, 1.0f);
                    vg = clampf(vg + 0.04f, 0.0f, 1.0f);
                    vb = clampf(vb + 0.04f, 0.0f, 1.0f);
                }
                draw_text_scaled(option_x, y, g_ui_scale * 0.96f, lr, lg, lb, row->left);
                draw_text_right_scaled(right_x, y, g_ui_scale * 0.92f, vr, vg, vb, row->right);
            } break;

            case ROW_BACK: {
                float rr = selected ? sel_r : base_r;
                float gg = selected ? sel_g : base_g;
                float bb = selected ? sel_b : base_b;
                draw_text_centered_scaled(L.center_x, y, g_ui_scale * 1.00f, rr, gg, bb, row->left);
            } break;

            case ROW_INFO: {
                if (row->mod_index >= 0) {
                    float ir = dim_r, ig = dim_g, ib = dim_b;
                    float ix = label_x + (18.0f * ui);
                    float scale = g_ui_scale * 0.82f;
                    if (_stricmp(row->left, "Options") == 0) {
                        ir = muted_r; ig = muted_g; ib = muted_b;
                        ix = option_x;
                        scale = g_ui_scale * 0.78f;
                    } else if (console_stristr(row->left, "Status:")) {
                        ir = 0.90f; ig = 0.72f; ib = 0.44f;
                        ix = option_x;
                    }
                    draw_text_scaled(ix, y, scale, ir, ig, ib, row->left);
                    if (row->right[0]) {
                        draw_text_right_scaled(right_x, y, scale, ir, ig, ib, row->right);
                    }
                } else {
                    draw_text_centered_scaled(L.center_x, y, g_ui_scale * 0.90f, dim_r, dim_g, dim_b, row->left);
                }
            } break;

            default:
                break;
        }
    }

    if (g_scroll_row > 0) {
        draw_text_right_scaled(L.right - (16.0f * ui), L.list_top - (10.0f * ui), g_ui_scale * 0.92f, 0.44f, 0.50f, 0.58f, "^");
    }
    if (g_scroll_row + cap < g_row_count) {
        draw_text_right_scaled(L.right - (16.0f * ui), L.list_bottom - (10.0f * ui), g_ui_scale * 0.92f, 0.44f, 0.50f, 0.58f, "v");
    }

    mods_restore_render_state();
}

static void console_draw_rect(float x, float y, float w, float h, float r, float g, float b, float a) {
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();
}

static void console_draw_rect_outline(float x, float y, float w, float h, float line_w, float r, float g, float b, float a) {
    glLineWidth(line_w < 1.0f ? 1.0f : line_w);
    glColor4f(r, g, b, a);
    glBegin(GL_LINE_LOOP);
    glVertex2f(x + 0.5f, y + 0.5f);
    glVertex2f(x + w - 0.5f, y + 0.5f);
    glVertex2f(x + w - 0.5f, y + h - 0.5f);
    glVertex2f(x + 0.5f, y + h - 0.5f);
    glEnd();
    glLineWidth(1.0f);
}

static void console_draw_line(float x1, float y1, float x2, float y2, float line_w, float r, float g, float b, float a) {
    glLineWidth(line_w < 1.0f ? 1.0f : line_w);
    glColor4f(r, g, b, a);
    glBegin(GL_LINES);
    glVertex2f(x1, y1);
    glVertex2f(x2, y2);
    glEnd();
    glLineWidth(1.0f);
}

void hooks_ui_fill_rect(float x, float y, float w, float h,
                        float r, float g, float b, float a) {
    GLint prev_matrix_mode = GL_MODELVIEW;
    float sw = p_mad_w ? p_mad_w() : BASE_UI_W;
    float sh = p_mad_h ? p_mad_h() : BASE_UI_H;
    if (w <= 0.0f || h <= 0.0f) return;

    glGetIntegerv(GL_MATRIX_MODE, &prev_matrix_mode);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, (double)sw, (double)sh, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_TEXTURE);
    glPushMatrix();
    glLoadIdentity();

    console_draw_rect(x, y, w, h, r, g, b, a);

    glMatrixMode(GL_TEXTURE);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(prev_matrix_mode);
    glPopAttrib();
}

void hooks_ui_stroke_rect(float x, float y, float w, float h, float line_w,
                          float r, float g, float b, float a) {
    GLint prev_matrix_mode = GL_MODELVIEW;
    float sw = p_mad_w ? p_mad_w() : BASE_UI_W;
    float sh = p_mad_h ? p_mad_h() : BASE_UI_H;
    if (w <= 0.0f || h <= 0.0f) return;

    glGetIntegerv(GL_MATRIX_MODE, &prev_matrix_mode);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, (double)sw, (double)sh, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_TEXTURE);
    glPushMatrix();
    glLoadIdentity();

    console_draw_rect_outline(x, y, w, h, line_w, r, g, b, a);

    glMatrixMode(GL_TEXTURE);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(prev_matrix_mode);
    glPopAttrib();
}

void hooks_ui_draw_line(float x1, float y1, float x2, float y2, float line_w,
                        float r, float g, float b, float a) {
    GLint prev_matrix_mode = GL_MODELVIEW;
    float sw = p_mad_w ? p_mad_w() : BASE_UI_W;
    float sh = p_mad_h ? p_mad_h() : BASE_UI_H;

    glGetIntegerv(GL_MATRIX_MODE, &prev_matrix_mode);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, (double)sw, (double)sh, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_TEXTURE);
    glPushMatrix();
    glLoadIdentity();

    console_draw_line(x1, y1, x2, y2, line_w, r, g, b, a);

    glMatrixMode(GL_TEXTURE);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(prev_matrix_mode);
    glPopAttrib();
}

static void console_draw_background(float w, float h) {
    GLint prev_matrix_mode = GL_MODELVIEW;
    glGetIntegerv(GL_MATRIX_MODE, &prev_matrix_mode);

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, (double)w, (double)h, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_TEXTURE);
    glPushMatrix();
    glLoadIdentity();

    if (g_console_bg_ready && g_console_bg_tex != 0 && g_console_bg_w > 0 && g_console_bg_h > 0) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, g_console_bg_tex);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f, 0.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex2f(w, 0.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex2f(w, h);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, h);
        glEnd();
    } else {
        glDisable(GL_TEXTURE_2D);
        console_draw_rect(0.0f, 0.0f, w, h, 0.05f, 0.06f, 0.08f, 1.0f);
    }

    glDisable(GL_TEXTURE_2D);
    console_draw_rect(0.0f, 0.0f, w, h, 0.02f, 0.03f, 0.04f, 0.24f);

    glMatrixMode(GL_TEXTURE);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(prev_matrix_mode);
    glPopAttrib();
}

static void mods_draw_background(const ModsLayout* L) {
    GLint prev_matrix_mode = GL_MODELVIEW;
    float ui;
    float x;
    float y;
    float w;
    float h;

    if (!L) return;

    ui = L->ui;
    x = L->left - (52.0f * ui);
    y = L->list_top - (42.0f * ui);
    w = L->content_w + (104.0f * ui);
    h = (L->list_bottom - L->list_top) + (84.0f * ui);

    if (x < 18.0f * ui) x = 18.0f * ui;
    if (y < 18.0f * ui) y = 18.0f * ui;
    if (x + w > L->w - (18.0f * ui)) w = (L->w - (18.0f * ui)) - x;
    if (y + h > L->h - (18.0f * ui)) h = (L->h - (18.0f * ui)) - y;

    glGetIntegerv(GL_MATRIX_MODE, &prev_matrix_mode);

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, (double)L->w, (double)L->h, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_TEXTURE);
    glPushMatrix();
    glLoadIdentity();

    /* Keep the scene visible, but give the menu text its own neutral sheet so
       the swords can remain bright on the sides. */
    console_draw_rect(x, y, w, h, 0.06f, 0.08f, 0.10f, 0.40f);

    glMatrixMode(GL_TEXTURE);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(prev_matrix_mode);
    glPopAttrib();
}

static void console_render_ui(void) {
    float w = p_mad_w ? p_mad_w() : BASE_UI_W;
    float h = p_mad_h ? p_mad_h() : BASE_UI_H;
    float ui = calc_ui_scale();
    float margin = 36.0f * ui;
    float panel_x;
    float panel_w;
    float panel_h = h * 0.50f;
    float panel_y;
    float title_scale;
    float text_scale;
    float line_h;
    float lines_top;
    float input_y;
    int visible_lines;
    int newest;
    int first;
    int line_no;
    int text_q;

    if (w < 640.0f) margin = 12.0f;
    else if (w < 900.0f) margin = 20.0f * ui;
    panel_x = margin;
    panel_w = w - (margin * 2.0f);
    if (panel_w < 260.0f) {
        panel_x = 10.0f;
        panel_w = w - 20.0f;
    }
    // Give taller consoles on larger windows while keeping safe margins.
    if (h < 720.0f) panel_h = h - (44.0f * ui);
    if (h >= 860.0f) panel_h = h * 0.58f;
    if (h >= 1080.0f) panel_h = h * 0.62f;
    if (panel_h > h - (82.0f * ui)) panel_h = h - (82.0f * ui);
    if (panel_h < 220.0f) panel_h = 220.0f;
    panel_y = h - panel_h - (26.0f * ui);
    if (panel_y < 20.0f * ui) panel_y = 20.0f * ui;

    GLint prev_matrix_mode = GL_MODELVIEW;
    glGetIntegerv(GL_MATRIX_MODE, &prev_matrix_mode);

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_TEXTURE_2D);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, (double)w, (double)h, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    console_draw_rect(panel_x, panel_y, panel_w, panel_h, 0.03f, 0.05f, 0.08f, 0.85f);
    /* title bar + accent underline */
    console_draw_rect(panel_x, panel_y, panel_w, 32.0f * ui, 0.08f, 0.12f, 0.20f, 0.94f);
    console_draw_rect(panel_x, panel_y + (32.0f * ui), panel_w, 1.5f * ui, 0.32f, 0.66f, 0.84f, 0.85f);
    /* input bar + accent overline */
    console_draw_rect(panel_x, panel_y + panel_h - (42.0f * ui), panel_w, 42.0f * ui, 0.02f, 0.04f, 0.06f, 0.94f);
    console_draw_rect(panel_x, panel_y + panel_h - (42.0f * ui), panel_w, 1.5f * ui, 0.32f, 0.66f, 0.84f, 0.55f);
    glColor4f(0.34f, 0.46f, 0.62f, 0.9f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(panel_x, panel_y);
    glVertex2f(panel_x + panel_w, panel_y);
    glVertex2f(panel_x + panel_w, panel_y + panel_h);
    glVertex2f(panel_x, panel_y + panel_h);
    glEnd();

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(prev_matrix_mode);
    glPopAttrib();

    title_scale = clampf(1.00f * ui, 0.95f, 1.55f);
    text_scale = clampf(0.92f * ui, 1.00f, 1.25f);
    text_q = (int)(text_scale * 4.0f + 0.5f);
    if (text_q < 1) text_q = 1;
    text_scale = (float)text_q / 4.0f;
    title_scale = text_scale * 1.18f;
    line_h = (10.0f * text_scale) + (9.0f * ui);
    lines_top = panel_y + (48.0f * ui);
    input_y = panel_y + panel_h - (27.0f * ui);
    visible_lines = (int)((input_y - lines_top - (8.0f * ui)) / line_h);
    if (visible_lines < 3) visible_lines = 3;

    draw_text_scaled((float)((int)(panel_x + (14.0f * ui) + 0.5f)), (float)((int)(panel_y + (22.0f * ui) + 0.5f)),
                     title_scale, 0.94f, 0.96f, 0.99f, "DEV CONSOLE");
    {
        char hdr[192];
        snprintf(hdr, sizeof(hdr), "ret=%s  hist=%d  scroll=%d", state_name_from_ptr(g_console_return_state), g_console_history_count, g_console_scroll);
        draw_text_right_scaled((float)((int)(panel_x + panel_w - (14.0f * ui) + 0.5f)), (float)((int)(panel_y + (22.0f * ui) + 0.5f)),
                               text_scale, 0.66f, 0.74f, 0.86f, hdr);
    }

    newest = g_console_line_count - 1 - g_console_scroll;
    if (newest >= 0) {
        first = newest - visible_lines + 1;
        if (first < 0) first = 0;
        line_no = 0;
        for (int i = first; i <= newest; i++) {
            ConsoleLine* line = console_line_at_oldest_index(i);
            float y = (float)((int)(lines_top + (line_no * line_h) + 0.5f));
            if (!line) continue;
            draw_text_scaled((float)((int)(panel_x + (14.0f * ui) + 0.5f)), y, text_scale, line->r, line->g, line->b, line->text);
            line_no++;
        }
    } else {
        draw_text_scaled((float)((int)(panel_x + (14.0f * ui) + 0.5f)), (float)((int)(lines_top + 0.5f)), text_scale, 0.62f, 0.70f, 0.82f,
                         "No output yet. Type 'help'.");
    }

    {
        char input_line[CONSOLE_INPUT_BUF + 8];
        size_t in_len = strlen(g_console_input);
        int avail_chars;
        int start_idx = 0;
        int caret_on = (((unsigned)GetTickCount() / 530u) & 1u) == 0u;
        float ix = (float)((int)(panel_x + (14.0f * ui) + 0.5f));
        float iy = (float)((int)(input_y + 0.5f));

        if (g_console_cursor < 0) g_console_cursor = 0;
        if ((size_t)g_console_cursor > in_len) g_console_cursor = (int)in_len;
        avail_chars = (int)((panel_w - (36.0f * ui)) / (6.0f * text_scale));
        if (avail_chars < 12) avail_chars = 12;

        if (g_console_search_active) {
            char line[CONSOLE_INPUT_BUF + 64];
            const char* match = (g_console_search_index >= 0 && g_console_search_index < g_console_history_count)
                                ? g_console_history[g_console_search_index] : "";
            snprintf(line, sizeof(line), "(reverse-search) '%s': %s", g_console_search_query, match);
            if ((int)strlen(line) > avail_chars + 2) line[avail_chars + 2] = '\0';
            draw_text_scaled(ix, iy, text_scale, 0.60f, 0.88f, 1.00f, line);
        } else {
            char caret = caret_on ? '|' : ' ';
            if (g_console_cursor > avail_chars - 4) start_idx = g_console_cursor - (avail_chars - 4);
            if (start_idx < 0) start_idx = 0;
            snprintf(input_line, sizeof(input_line), "> %.*s%c%s",
                     g_console_cursor - start_idx, g_console_input + start_idx, caret, g_console_input + g_console_cursor);
            if ((int)strlen(input_line) > avail_chars + 2) input_line[avail_chars + 2] = '\0';
            draw_text_scaled(ix, iy, text_scale, 0.95f, 0.88f, 0.40f, input_line);
        }

        if (panel_w > 600.0f) {
            const char* hint;
            char tok0[CONSOLE_COMPL_ITEM];
            if (g_console_search_active) {
                hint = "Enter=use  Ctrl+R=older  Esc=cancel";
            } else {
                const char* usage;
                console_input_token(0, tok0, sizeof(tok0));
                usage = console_usage_for(tok0);
                hint = usage ? usage : "Tab=complete  Ctrl+R=search  Ctrl+C/V=copy  Wheel=scroll";
            }
            draw_text_right_scaled((float)((int)(panel_x + panel_w - (14.0f * ui) + 0.5f)), iy, text_scale,
                                   0.62f, 0.70f, 0.84f, hint);
        }
    }
}

static void __cdecl console_enter(void) {
    capture_clear();
    g_console_scroll = 0;
    console_compl_reset();
    g_console_search_active = 0;
    g_console_search_query[0] = '\0';
    g_console_search_index = -1;
    console_history_load();
    if (g_console_line_count == 0) {
        console_push_line_rgb("Type 'help' for a list of commands.", 0.72f, 0.90f, 1.00f);
    }
}

static void __cdecl console_update(void) {
    // Intentionally no menu-button update path here.
}

static void __cdecl console_render(void) {
    mods_restore_render_state();
    console_draw_background(p_mad_w ? p_mad_w() : BASE_UI_W, p_mad_h ? p_mad_h() : BASE_UI_H);
    console_render_ui();
    mods_restore_render_state();
    if (p_main_sprite_batches_draw) {
        p_main_sprite_batches_draw();
    }
    mods_restore_render_state();
}

static void __cdecl console_leave(void) {
    g_console_open_pending = 0;
    g_console_open_ready = 0;
    g_console_suppress_next_textinput = 0;
}


static void __cdecl mods_enter(void) {
    LOG_INFO("MODS: entering mods menu");
    if (p_main_buttons_start) p_main_buttons_start();

    capture_clear();

    g_mods_return_state = p_state_last ? p_state_last() : (void*)(uintptr_t)ADDR_OPTIONS_STATE;
    if (g_mods_return_state == (void*)&g_console_state) {
        g_mods_return_state = g_console_return_state;
    }
    if (!g_mods_return_state ||
        g_mods_return_state == (void*)&g_mods_state ||
        g_mods_return_state == (void*)&g_mods_entry_state ||
        g_mods_return_state == (void*)&g_console_state) {
        g_mods_return_state = (void*)(uintptr_t)ADDR_OPTIONS_STATE;
    }

    rebuild_rows();
    g_cursor_initialized = 0;
    g_cursor_bump = 0;
    mods_cursor_tick();
}

static void __cdecl mods_update(void) {
    p_main_update_with_buttons(0);
    mods_cursor_tick();
}

static void __cdecl mods_render(void) {
    ModsLayout L;
    mods_calc_layout(&L);

    /* Keep this simple: draw the live scene, let the vanilla menu visuals
       (including the swords) render normally, then draw the mod-manager text
       on top. No extra tint sheet or overlay pass. */
    mods_restore_render_state();
    if (p_main_draw) {
        p_main_draw();
    } else if (p_main_sprite_batches_draw) {
        p_main_sprite_batches_draw();
    }
    mods_restore_render_state();

    if (p_menu_common_render) {
        p_menu_common_render();
    }
    mods_restore_render_state();
    if (p_main_sprite_batches_draw) {
        p_main_sprite_batches_draw();
    }
    mods_restore_render_state();

    render_rows();
    mods_restore_render_state();
    if (p_main_sprite_batches_draw) {
        p_main_sprite_batches_draw();
    }
    mods_restore_render_state();
}

static void __cdecl mods_leave(void) {
    capture_clear();
    mods_restore_render_state();
}

/* ── Generic custom-state framework ─────────────────────────────────── */

static void __cdecl custom_state_enter(void) {
    HookCustomState* slot = find_active_custom_state();
    void* fallback = (void*)(uintptr_t)ADDR_MAIN_STATE;
    void* last = p_state_last ? p_state_last() : fallback;

    if (!slot) return;
    slot->return_state = last;
    if (!slot->return_state || slot->return_state == (void*)&slot->state) {
        slot->return_state = fallback;
    }
    console_capture_background_now();
    LOG_INFO("CUSTOM STATE: enter (%s)", slot->name);
    mods_restore_render_state();
}

static void __cdecl custom_state_update(void) {
    lua_manager_on_tick();
    lua_manager_on_tick_post();
    hooks_finish_game_tick();
}

static void __cdecl custom_state_render(void) {
    mods_restore_render_state();
    console_draw_background(p_mad_w ? p_mad_w() : BASE_UI_W, p_mad_h ? p_mad_h() : BASE_UI_H);
    mods_restore_render_state();
    if (p_main_sprite_batches_draw) {
        p_main_sprite_batches_draw();
    }
    mods_restore_render_state();
}

static void __cdecl custom_state_leave(void) {
    HookCustomState* slot = find_active_custom_state();
    if (slot) {
        LOG_INFO("CUSTOM STATE: leave (%s)", slot->name);
    }
    mods_restore_render_state();
}

int hooks_register_custom_state(const char* name) {
    int i;
    HookCustomState* slot;
    if (!name || !name[0]) return 0;
    if (find_custom_state_by_name(name)) return 1;
    for (i = 0; i < MAX_CUSTOM_STATES; i++) {
        if (!g_custom_states[i].used) {
            slot = &g_custom_states[i];
            memset(slot, 0, sizeof(*slot));
            slot->used = 1;
            safe_copy(slot->name, sizeof(slot->name), name);
            slot->return_state = (void*)(uintptr_t)ADDR_MAIN_STATE;
            slot->state.enter = custom_state_enter;
            slot->state.update = custom_state_update;
            slot->state.render = custom_state_render;
            slot->state.leave = custom_state_leave;
            LOG_INFO("CUSTOM STATE: registered (%s)", slot->name);
            return 1;
        }
    }
    LOG_WARN("CUSTOM STATE: registration failed for '%s' (pool full)", name);
    return 0;
}

int hooks_enter_custom_state(const char* name) {
    HookCustomState* slot;
    if (!p_state_switch) return 0;
    slot = find_custom_state_by_name(name);
    if (!slot) {
        if (!hooks_register_custom_state(name)) return 0;
        slot = find_custom_state_by_name(name);
        if (!slot) return 0;
    }
    p_state_switch((void*)&slot->state);
    return 1;
}

int hooks_leave_custom_state(void) {
    HookCustomState* slot;
    void* target;
    if (!p_state_switch) return 0;
    slot = find_active_custom_state();
    if (!slot) return 0;
    target = slot->return_state ? slot->return_state : (void*)(uintptr_t)ADDR_MAIN_STATE;
    if (target == (void*)&slot->state) {
        target = (void*)(uintptr_t)ADDR_MAIN_STATE;
    }
    p_state_switch(target);
    return 1;
}

const char* hooks_custom_state_name_for_ptr(void* state_ptr) {
    HookCustomState* slot = find_custom_state_by_ptr(state_ptr);
    return slot ? slot->name : NULL;
}

const char* hooks_custom_state_active_name(void) {
    HookCustomState* slot = find_active_custom_state();
    return slot ? slot->name : NULL;
}

static void __cdecl mods_entry_enter(void) {
    p_state_switch((void*)&g_mods_state);
}

static void __cdecl mods_entry_update(void) { }
static void __cdecl mods_entry_render(void) { }
static void __cdecl mods_entry_leave(void) { }

static void* online_find_button_by_action(uintptr_t action_ptr) {
    if (!p_button_count || !p_button_get || action_ptr == 0) return NULL;
    {
        int count = p_button_count();
        if (count <= 0 || count > 3000) return NULL;
        for (int i = 0; i < count; i++) {
            void* btn = p_button_get(i);
            if (!btn) continue;
            if (IsBadReadPtr((uint8_t*)btn + BTN_OFS_ACTION_PTR, (SIZE_T)sizeof(void*))) continue;
            if ((uintptr_t)(*(void**)((uint8_t*)btn + BTN_OFS_ACTION_PTR)) == action_ptr) {
                return btn;
            }
        }
    }
    return NULL;
}

static void* online_find_button_by_link(uintptr_t link_ptr) {
    if (!p_button_count || !p_button_get || link_ptr == 0) return NULL;
    {
        int count = p_button_count();
        if (count <= 0 || count > 3000) return NULL;
        for (int i = 0; i < count; i++) {
            void* btn = p_button_get(i);
            if (!btn) continue;
            if (IsBadReadPtr((uint8_t*)btn + BTN_OFS_LINK_PTR, (SIZE_T)sizeof(void*))) continue;
            if ((uintptr_t)(*(void**)((uint8_t*)btn + BTN_OFS_LINK_PTR)) == link_ptr) {
                return btn;
            }
        }
    }
    return NULL;
}

static int install_detour(Detour* d, void* target, void* hook, size_t length) {
    if (!d || !target || !hook) return 0;
    if (length < 5 || length > sizeof(d->original)) return 0;

    memset(d, 0, sizeof(*d));
    d->target = target;
    d->length = length;

    memcpy(d->original, target, length);

    d->trampoline = VirtualAlloc(NULL, length + 7, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!d->trampoline) return 0;

    memcpy(d->trampoline, d->original, length);

    {
        uint8_t* jump_back = (uint8_t*)d->trampoline + length;
        uintptr_t back_addr = (uintptr_t)target + length;

        // push back_addr; ret  (absolute jump without clobbering EAX)
        jump_back[0] = 0x68; // push imm32
        *(uint32_t*)(jump_back + 1) = (uint32_t)back_addr;
        jump_back[5] = 0xC3; // ret
        jump_back[6] = 0x90; // nop (padding)
    }

    {
        DWORD old_protect = 0;
        if (!VirtualProtect(target, length, PAGE_EXECUTE_READWRITE, &old_protect)) {
            return 0;
        }

        {
            uint8_t* at = (uint8_t*)target;
            uintptr_t rel = (uintptr_t)hook - ((uintptr_t)target + 5);
            at[0] = 0xE9;
            *(uint32_t*)(at + 1) = (uint32_t)rel;
            for (size_t i = 5; i < length; i++) at[i] = 0x90;
        }

        FlushInstructionCache(GetCurrentProcess(), target, length);
        VirtualProtect(target, length, old_protect, &old_protect);
    }

    return 1;
}

// Old-style link filter: allow either player selector to activate the same button.
static int __cdecl mods_entry_player_filter_proxy(void* btn, int event_code) {
    if (!btn || !p_btn_player_filter) return 0;

    // Direct activation path: consume the activation and open custom MODS now.
    if (event_code == 3) {
        uint32_t* tag_ptr = (uint32_t*)((uint8_t*)btn + 4);
        uint32_t old_tag = *tag_ptr;
        int ok = 0;

        *tag_ptr = 0x11;
        ok = p_btn_player_filter(btn, event_code);
        if (!ok) {
            *tag_ptr = 0x12;
            ok = p_btn_player_filter(btn, event_code);
        }
        *tag_ptr = old_tag;

        if (ok) {
            p_state_switch((void*)&g_mods_state);
        }
        return 0;
    }

    uint32_t* tag_ptr = (uint32_t*)((uint8_t*)btn + 4);
    uint32_t old_tag = *tag_ptr;

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

    *tag_ptr = old_tag;
    return 0;
}

static int __cdecl online_hub_player_filter_proxy(void* btn, int event_code) {
    if (!btn || !p_btn_player_filter) return 0;

    if (event_code == 3) {
        uint32_t* tag_ptr = (uint32_t*)((uint8_t*)btn + 4);
        uint32_t old_tag = *tag_ptr;
        int ok = 0;

        *tag_ptr = 0x11;
        ok = p_btn_player_filter(btn, event_code);
        if (!ok) {
            *tag_ptr = 0x12;
            ok = p_btn_player_filter(btn, event_code);
        }
        *tag_ptr = old_tag;

        if (ok) {
            online_hub_open();
        }
        return 0;
    }

    {
        uint32_t* tag_ptr = (uint32_t*)((uint8_t*)btn + 4);
        uint32_t old_tag = *tag_ptr;

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

        *tag_ptr = old_tag;
    }
    return 0;
}

static int online_activate_native_button(void* btn) {
    uint32_t* tag_ptr;
    uint32_t old_tag;
    int ok = 0;
    void* link_ptr = NULL;
    int nolink = 0;
    if (!btn || !p_btn_player_filter) return 0;
    tag_ptr = (uint32_t*)((uint8_t*)btn + 4);
    old_tag = *tag_ptr;
    *tag_ptr = 0x11;
    ok = p_btn_player_filter(btn, 3);
    if (!ok) {
        *tag_ptr = 0x12;
        ok = p_btn_player_filter(btn, 3);
    }
    *tag_ptr = old_tag;
    if (!ok) return 0;
    if (!IsBadReadPtr((uint8_t*)btn + BTN_OFS_LINK_PTR, (SIZE_T)sizeof(void*))) {
        link_ptr = *(void**)((uint8_t*)btn + BTN_OFS_LINK_PTR);
    }
    if (!IsBadReadPtr((uint8_t*)btn + BTN_OFS_NOLINK_FLAG, (SIZE_T)sizeof(unsigned char))) {
        nolink = (*(unsigned char*)((uint8_t*)btn + BTN_OFS_NOLINK_FLAG) != 0);
    }
    if (link_ptr && !nolink && p_state_switch) {
        p_state_switch(link_ptr);
    }
    return 1;
}

static int online_native_winner_player(void) {
    uintptr_t leader;
    uintptr_t loser;
    if (g_game_end_countdown && *g_game_end_countdown > 0 && g_game_leader && p_player_slots) {
        if (g_game_loser && !IsBadReadPtr((const void*)g_game_loser, sizeof(uintptr_t))) {
            loser = *g_game_loser;
            if (loser == (uintptr_t)p_player_slots[0]) return 1;
            if (loser == (uintptr_t)p_player_slots[1]) return 0;
        }
        leader = *g_game_leader;
        if (leader == (uintptr_t)p_player_slots[0]) return 0;
        if (leader == (uintptr_t)p_player_slots[1]) return 1;
    }
    if (g_game_score_target && g_game_score_player0 && g_game_score_player1 && *g_game_score_target > 0) {
        int target = *g_game_score_target;
        int s0 = *g_game_score_player0;
        int s1 = *g_game_score_player1;
        if (s0 >= target && s0 > s1) return 0;
        if (s1 >= target && s1 > s0) return 1;
    }
    return -1;
}

static int online_match_state_allowed(void* state_ptr) {
    return state_ptr == (void*)(uintptr_t)ADDR_GAME_STATE ||
           state_ptr == (void*)(uintptr_t)ADDR_OPTIONS_STATE_PAUSED;
}

static void online_monitor_active_match_state(void* state_ptr) {
    if (!g_online_active_match.active || g_online_active_match.result_reported) return;
    if (!ggpo_net_active()) {
        online_finish_active_match(ONLINE_MATCH_RESULT_LOSS, "P2P disconnected; reported loss.", 1);
        return;
    }
    if (online_match_state_allowed(state_ptr)) {
        g_online_active_match.invalid_state_ticks = 0;
        return;
    }
    g_online_active_match.invalid_state_ticks++;
    if (g_online_active_match.invalid_state_ticks >= ONLINE_ABANDON_GRACE_TICKS) {
        online_finish_active_match(ONLINE_MATCH_RESULT_LOSS, "You left the match; reported loss.", 1);
    }
}

static void online_match_poll_completion(void) {
    int winner;
    int local_player;
    OnlineMatchResult result;
    if (!g_online_active_match.active || g_online_active_match.result_reported) return;
    winner = online_native_winner_player();
    if (winner < 0) return;
    local_player = ggpo_net_local_player();
    if (local_player < 0 || local_player > 1) {
        local_player = clampi(g_online_active_match.local_player, 0, 1);
    }
    result = (winner == local_player) ? ONLINE_MATCH_RESULT_WIN : ONLINE_MATCH_RESULT_LOSS;
    LOG_INFO("online.match: completion match=%d local_player=%d winner=%d result=%s end=%d leader=0x%08X loser=0x%08X score=%d-%d target=%d",
             g_online_active_match.match_id,
             local_player,
             winner,
             result == ONLINE_MATCH_RESULT_WIN ? "win" : "loss",
             g_game_end_countdown ? *g_game_end_countdown : 0,
             (unsigned int)(g_game_leader ? *g_game_leader : 0u),
             (unsigned int)(g_game_loser ? *g_game_loser : 0u),
             g_game_score_player0 ? *g_game_score_player0 : 0,
             g_game_score_player1 ? *g_game_score_player1 : 0,
             g_game_score_target ? *g_game_score_target : 0);
    online_finish_active_match(result, "Reported result to server.", 1);
}

static void online_log_desync_snapshot_if_changed(void) {
    static uint32_t s_seen_desync_count = 0;
    uint32_t count = ggpo_net_desync_count();
    if (count == s_seen_desync_count) return;
    s_seen_desync_count = count;
    if (count == 0) return;
    LOG_WARN("online.match: desync snapshot count=%u frame=%u local_crc=%u remote_crc=%u net_frame=%u remote_frame=%u room=%d old_room=%d resumed=%d waterfall_count=%d waterfall_fx=0x%08X start=%d end=%d round_end=%d score=%d-%d target=%d map=%d seed=%u mrand=%u local_player=%d active_match=%d pending=%d",
             (unsigned int)count,
             (unsigned int)ggpo_net_desync_frame(),
             (unsigned int)ggpo_net_desync_local_checksum(),
             (unsigned int)ggpo_net_desync_remote_checksum(),
             (unsigned int)ggpo_net_frame_count(),
             (unsigned int)ggpo_net_remote_frame_count(),
             g_game_active_room ? *g_game_active_room : -1,
             g_game_old_active_room ? *g_game_old_active_room : -1,
             g_game_resumed ? *g_game_resumed : -1,
             g_game_waterfall_count ? *g_game_waterfall_count : -1,
             (unsigned int)(g_game_waterfall_fx ? *g_game_waterfall_fx : 0u),
             g_game_start_countdown ? *g_game_start_countdown : -1,
             g_game_end_countdown ? *g_game_end_countdown : -1,
             g_game_round_end_any ? *g_game_round_end_any : -1,
             g_game_score_player0 ? *g_game_score_player0 : -1,
             g_game_score_player1 ? *g_game_score_player1 : -1,
             g_game_score_target ? *g_game_score_target : -1,
             g_hook_map_selector ? *g_hook_map_selector : -1,
             g_native_seed ? *g_native_seed : 0u,
             g_native_mrand_seed ? *g_native_mrand_seed : 0u,
             ggpo_net_local_player(),
             g_online_active_match.active,
             g_online_pending_match.active);
}

static void online_match_pump_launch(void) {
    void* state_ptr;
    if (!g_online_pending_match.active) return;
    if (g_online_pending_match.launch_countdown_frames > 0) {
        if (!is_online_hub_state_active()) {
            online_hub_open();
            return;
        }
        g_online_pending_match.launch_countdown_frames--;
        if (g_online_pending_match.launch_countdown_frames > 0) return;
        online_launch_pending_match_to_game();
        return;
    }
    if (g_hook_map_selector) {
        *g_hook_map_selector = g_online_pending_match.selector;
    }
    state_ptr = p_state_current ? p_state_current() : NULL;

    if (state_ptr == (void*)(uintptr_t)ADDR_GAME_STATE && ggpo_net_active()) {
        if (!g_online_pending_match.p2p_started) {
            g_online_pending_match.p2p_started = 1;
            online_active_match_begin_from_pending();
            g_online_pending_match.active = 0;
            online_hub_set_status("");
        }
        return;
    }

    if (g_online_pending_match.launch_stage <= 1) {
        if (state_ptr == (void*)(uintptr_t)ADDR_MAIN_STATE ||
            state_ptr == (void*)(uintptr_t)ADDR_MAIN_STATE_INITIAL) {
            void* start_btn = online_find_button_by_action(MAIN_START_ACTION_PTR);
            if (start_btn && online_activate_native_button(start_btn)) {
                g_online_pending_match.launch_stage = 2;
            }
            return;
        }
        if (state_ptr == (void*)(uintptr_t)ADDR_PREGAME_STATE) {
            g_online_pending_match.launch_stage = 2;
        }
    }

    if (g_online_pending_match.launch_stage == 2) {
        if (state_ptr == (void*)(uintptr_t)ADDR_PREGAME_STATE) {
            void* game_btn = online_find_button_by_link(ADDR_GAME_STATE);
            if (!game_btn) game_btn = online_find_button_by_action(MAIN_START_ACTION_PTR);
            if (game_btn && online_activate_native_button(game_btn)) {
                g_online_pending_match.launch_stage = 3;
            }
            return;
        }
        if (state_ptr == (void*)(uintptr_t)ADDR_MAIN_STATE ||
            state_ptr == (void*)(uintptr_t)ADDR_MAIN_STATE_INITIAL) {
            return;
        }
    }

    if (state_ptr == (void*)(uintptr_t)ADDR_GAME_STATE && !g_online_pending_match.p2p_started) {
        online_ensure_native_game_started();
        if (g_online_pending_match.launch_delay_frames > 0) {
            g_online_pending_match.launch_delay_frames--;
            return;
        }
        g_online_pending_match.p2p_started = 1;
        if (_stricmp(g_online_pending_match.p2p_role, "host") == 0) {
            start_ggpo_net_host((uint16_t)g_online_pending_match.local_port, "online server match");
            if (ggpo_net_active() && g_online_pending_match.peer_host[0] && g_online_pending_match.peer_port > 0) {
                char err[256];
                err[0] = '\0';
                if (!ggpo_net_set_peer(g_online_pending_match.peer_host,
                                       (uint16_t)g_online_pending_match.peer_port,
                                       err,
                                       sizeof(err))) {
                    LOG_WARN("online.p2p: host could not apply early peer endpoint (%s)", err[0] ? err : "unknown error");
                }
            }
        } else if (g_online_pending_match.peer_host[0] && g_online_pending_match.peer_port > 0) {
            start_ggpo_net_join(g_online_pending_match.peer_host,
                                (uint16_t)g_online_pending_match.peer_port,
                                (uint16_t)g_online_pending_match.local_port,
                                "online server match");
        } else {
            start_ggpo_net_join_deferred((uint16_t)g_online_pending_match.local_port, "online server match");
        }
        if (ggpo_net_active()) {
            online_active_match_begin_from_pending();
            g_online_pending_match.active = 0;
            online_hub_set_status("");
        } else {
            online_hub_set_status("P2P session failed to start.");
        }
    }
}

static void add_online_button_to_main(void) {
    void* start_btn;
    void* btn;
    static void* baseline_start_btn = NULL;
    static float baseline_start_y = 0.0f;
    if (!p_button_ex) return;

    btn = online_find_button_by_action((uintptr_t)&online_hub_player_filter_proxy);
    if (!btn) {
        if (p_button_set_layout) {
            p_button_set_layout(3.0f, 6.0f);
        }
        btn = p_button_ex(1.0f, 4.5f, 0u, "ONLINE", (int)(intptr_t)&online_hub_player_filter_proxy);
        if (!btn) return;
    }

    *(void**)((uint8_t*)btn + BTN_OFS_LINK_PTR) = (void*)&g_online_hub_state;
    *(void**)((uint8_t*)btn + BTN_OFS_ACTION_PTR) = (void*)&online_hub_player_filter_proxy;

    start_btn = online_find_button_by_action(MAIN_START_ACTION_PTR);
    if (start_btn &&
        !IsBadReadPtr((uint8_t*)start_btn + BTN_OFS_HEIGHT, (SIZE_T)sizeof(float)) &&
        !IsBadWritePtr((uint8_t*)start_btn + BTN_OFS_HEIGHT, (SIZE_T)sizeof(float)) &&
        !IsBadWritePtr((uint8_t*)btn + BTN_OFS_HEIGHT, (SIZE_T)sizeof(float))) {
        float sx = *(float*)((uint8_t*)start_btn + BTN_OFS_CENTER_X);
        float sy = *(float*)((uint8_t*)start_btn + BTN_OFS_CENTER_Y);
        float sw = *(float*)((uint8_t*)start_btn + BTN_OFS_WIDTH);
        float sh = *(float*)((uint8_t*)start_btn + BTN_OFS_HEIGHT);
        float gap = 8.0f;
        float center_gap = (sh + gap) * 0.5f;
        if (baseline_start_btn != start_btn) {
            baseline_start_btn = start_btn;
            baseline_start_y = sy;
        }
        *(float*)((uint8_t*)start_btn + BTN_OFS_CENTER_Y) = baseline_start_y - center_gap;
        *(float*)((uint8_t*)btn + BTN_OFS_CENTER_X) = sx;
        *(float*)((uint8_t*)btn + BTN_OFS_CENTER_Y) = baseline_start_y + center_gap;
        *(float*)((uint8_t*)btn + BTN_OFS_WIDTH) = sw;
        *(float*)((uint8_t*)btn + BTN_OFS_HEIGHT) = sh;
    }
}

static void add_mods_button_to_options(void) {
    if (!p_menu_button_link) return;
    if (p_button_set_layout) {
        // Match old working entry: back-button-like sizing in the top-right lane.
        p_button_set_layout(5.0f, 5.0f);
    }
    {
        // Old placement: above Player 2 Input.
        void* btn = p_menu_button_link(4.0f, -1.05f, "MODS", (void*)&g_mods_entry_state);
        if (btn) {
            // Force bridge target explicitly so legacy paths cannot land in old menu states.
            *(void**)((uint8_t*)btn + 0xE0) = (void*)&g_mods_entry_state;
            // Use old filter path so both selectors can activate it (not just mouse).
            *(void**)((uint8_t*)btn + 0xE4) = (void*)&mods_entry_player_filter_proxy;
        }
    }
}

static void __cdecl hooked_options_enter(void) {
    fn_void_void_t real_enter = p_options_enter_trampoline ? p_options_enter_trampoline : p_options_enter;
    real_enter();
    add_mods_button_to_options();
}

static void __cdecl hooked_options_enter_paused(void) {
    fn_void_void_t real_enter = p_options_enter_paused_trampoline ? p_options_enter_paused_trampoline : p_options_enter_paused;
    real_enter();
    add_mods_button_to_options();
}

static int online_advance_net_gameplay_tick(int arg0) {
    uint32_t raw0 = hooks_peek_player_cmds_raw(0, 2);
    uint32_t raw1 = hooks_peek_player_cmds_raw(1, 2);
    void* state_ptr = p_state_current ? p_state_current() : NULL;
    uint32_t checksum = 0;
    int advanced_any = 0;
    int steps = 0;
    char err[512];
    char out[CONSOLE_LINE_TEXT];
    if (state_ptr == (void*)(uintptr_t)ADDR_OPTIONS_STATE_PAUSED || state_ptr == (void*)&g_console_state) {
        int local_player = ggpo_net_local_player();
        if (local_player < 0 || local_player > 1) local_player = clampi(g_online_active_match.local_player, 0, 1);
        if (local_player == 0) raw0 = 0u;
        else raw1 = 0u;
    }
    online_pump_p2p_probe();
    if (ggpo_net_build_mismatch()) {
        /* Builds differ (exe/dll file-hash mismatch). Rollback determinism
         * really does require identical simulation code on both peers, so this
         * makes desyncs likely. We used to hard-abort here, but that blocks
         * real-world testing whenever one peer is briefly on an older DLL, so
         * now we warn once and let the match proceed. ggpo.net already logs the
         * exact fingerprints once per match. */
        static int s_warned_build_mismatch = 0;
        if (!s_warned_build_mismatch) {
            s_warned_build_mismatch = 1;
            console_push_line_rgb("ggpo.net: WARNING - opponent is on a different build; desyncs likely",
                                  0.98f, 0.78f, 0.40f);
            online_hub_set_status("Warning: opponent is on a different game build - desyncs likely. Use identical versions for a clean match.");
            LOG_WARN("ggpo.net: build/exe/dll fingerprint mismatch with opponent - continuing anyway (desyncs expected)");
        }
    }
    do {
        int advanced = 0;
        if (!ggpo_net_advance(raw0, raw1, arg0, &checksum, &advanced, err, sizeof(err))) {
            snprintf(out,
                     sizeof(out),
                     "ggpo.net: failed (%s)",
                     err[0] ? err : "see log");
            console_push_line_rgb(out, 0.98f, 0.45f, 0.45f);
            LOG_ERROR("ggpo.net: failed (%s)", err[0] ? err : "unknown error");
            ggpo_net_stop();
            return -1;
        }
        if (!advanced) break;
        advanced_any = 1;
        steps++;
    } while (steps < 4 && ggpo_net_catchup_pending());
    online_match_poll_completion();
    online_log_desync_snapshot_if_changed();
    return advanced_any ? 1 : 0;
}

static int __cdecl hooked_main_update_with_buttons(int arg0) {
    fn_main_update_with_buttons_t real_update = p_main_update_with_buttons_trampoline
        ? p_main_update_with_buttons_trampoline
        : p_main_update_with_buttons;
    void* state_ptr = NULL;
    int is_game_state = 0;

    if (p_state_current) state_ptr = p_state_current();
    is_game_state = (state_ptr == (void*)(uintptr_t)ADDR_GAME_STATE);
    online_monitor_active_match_state(state_ptr);
    if (!is_game_state) {
        lua_manager_on_tick();
    }
    online_server_update();
    {
        int result = real_update ? real_update(arg0) : 0;
        online_match_pump_launch();
        if (!is_game_state) {
            void* after_update = p_state_current ? p_state_current() : NULL;
            int net_tick_attempted = 0;
            int net_tick_result = 0;
            if (after_update == (void*)(uintptr_t)ADDR_MAIN_STATE ||
                after_update == (void*)(uintptr_t)ADDR_MAIN_STATE_INITIAL) {
                add_online_button_to_main();
            }
            online_monitor_active_match_state(after_update);
            if (after_update == (void*)(uintptr_t)ADDR_OPTIONS_STATE_PAUSED && ggpo_net_active()) {
                net_tick_attempted = 1;
                g_allow_paused_game_tick++;
                net_tick_result = online_advance_net_gameplay_tick(arg0);
                g_allow_paused_game_tick--;
            }
            lua_manager_on_tick_post();
            if (!net_tick_attempted || net_tick_result <= 0) {
                hooks_finish_game_tick();
            }
        }
        return result;
    }
}

static void ggpo_selftest_cleanup(void) {
    free(g_ggpo_selftest.initial_blob);
    free(g_ggpo_selftest.work_blob);
    free(g_ggpo_selftest.live_blob);
    free(g_ggpo_selftest.frame_checksums);
    memset(&g_ggpo_selftest, 0, sizeof(g_ggpo_selftest));
}

static int ggpo_selftest_advance_frame_silent(const GgpoFrameInputs* inputs, int arg0, uint32_t* out_checksum, char* err, size_t err_cap) {
    int old_synth_enabled = hooks_set_native_synth_enabled(0);
    int ok = ggpo_ext_advance_frame(inputs, arg0, out_checksum, err, err_cap);
    hooks_set_native_synth_enabled(old_synth_enabled);
    return ok;
}

static int ggpo_selftest_start(char* err, size_t err_cap) {
    int frames = g_ggpo_selftest_frames > 0 ? g_ggpo_selftest_frames : 120;
    size_t state_size = ggpo_ext_game_state_size();

    if (frames <= 0) frames = 120;
    if (frames > 3600) frames = 3600;
    if (state_size == 0) {
        snprintf(err, err_cap, "game state unavailable");
        return 0;
    }

    ggpo_selftest_cleanup();
    g_ggpo_selftest.frames = frames;
    g_ggpo_selftest.state_size = state_size;
    g_ggpo_selftest.initial_blob = (uint8_t*)malloc(state_size);
    g_ggpo_selftest.work_blob = (uint8_t*)malloc(state_size);
    g_ggpo_selftest.live_blob = (uint8_t*)malloc(state_size);
    g_ggpo_selftest.frame_checksums = (uint32_t*)calloc((size_t)frames, sizeof(uint32_t));
    if (!g_ggpo_selftest.initial_blob || !g_ggpo_selftest.work_blob || !g_ggpo_selftest.live_blob || !g_ggpo_selftest.frame_checksums) {
        ggpo_selftest_cleanup();
        snprintf(err, err_cap, "out of memory");
        return 0;
    }

    if (!ggpo_ext_save_game_state(g_ggpo_selftest.initial_blob,
                                  state_size,
                                  &g_ggpo_selftest.initial_len,
                                  &g_ggpo_selftest.base_checksum,
                                  err,
                                  err_cap)) {
        ggpo_selftest_cleanup();
        return 0;
    }
    memcpy(g_ggpo_selftest.work_blob, g_ggpo_selftest.initial_blob, g_ggpo_selftest.initial_len);
    g_ggpo_selftest.work_len = g_ggpo_selftest.initial_len;
    g_ggpo_selftest.active = 1;
    g_ggpo_selftest.pass = 0;
    g_ggpo_selftest.frame_index = 0;
    return 1;
}

static void run_pending_ggpo_selftest(void) {
    char err[256];
    char out[CONSOLE_LINE_TEXT];
    size_t live_len = 0;
    int completed = 0;
    int failed = 0;
    int started = 0;

    if (g_ggpo_selftest_pending && !g_ggpo_selftest.active) {
        g_ggpo_selftest_pending = 0;
        if (ggpo_loopback_active()) {
            snprintf(out, sizeof(out), "ggpo.selftest: skipped (loopback active)");
            console_push_line_rgb(out, 0.98f, 0.76f, 0.40f);
            LOG_WARN("%s", out);
            return;
        }
        if (ggpo_local_active()) {
            snprintf(out, sizeof(out), "ggpo.selftest: skipped (local session active)");
            console_push_line_rgb(out, 0.98f, 0.76f, 0.40f);
            LOG_WARN("%s", out);
            return;
        }
        if (ggpo_net_active()) {
            snprintf(out, sizeof(out), "ggpo.selftest: skipped (net session active)");
            console_push_line_rgb(out, 0.98f, 0.76f, 0.40f);
            LOG_WARN("%s", out);
            return;
        }
        err[0] = '\0';
        if (!ggpo_selftest_start(err, sizeof(err))) {
            snprintf(out, sizeof(out), "ggpo.selftest: failed (%s)", err[0] ? err : "unknown error");
            console_push_line_rgb(out, 0.98f, 0.45f, 0.45f);
            LOG_ERROR("%s", out);
            return;
        }
        started = 1;
        snprintf(out,
                 sizeof(out),
                 "ggpo.selftest: started frames=%d state_size=%u budget=%d",
                 g_ggpo_selftest.frames,
                 (unsigned int)g_ggpo_selftest.state_size,
                 GGPO_SELFTEST_FRAMES_PER_TICK);
        console_push_line_rgb(out, 0.64f, 0.92f, 0.66f);
        LOG_INFO("%s", out);
    }

    if (!g_ggpo_selftest.active) return;
    if (started) return;

    err[0] = '\0';
    if (ggpo_loopback_active()) {
        snprintf(err, sizeof(err), "loopback became active");
        failed = 1;
        goto finish_without_live_restore;
    }
    if (ggpo_local_active()) {
        snprintf(err, sizeof(err), "local session became active");
        failed = 1;
        goto finish_without_live_restore;
    }
    if (ggpo_net_active()) {
        snprintf(err, sizeof(err), "net session became active");
        failed = 1;
        goto finish_without_live_restore;
    }

    if (!ggpo_ext_save_game_state(g_ggpo_selftest.live_blob,
                                  g_ggpo_selftest.state_size,
                                  &live_len,
                                  NULL,
                                  err,
                                  sizeof(err))) {
        failed = 1;
        goto finish_without_live_restore;
    }
    if (!ggpo_ext_load_game_state(g_ggpo_selftest.work_blob, g_ggpo_selftest.work_len, err, sizeof(err))) {
        failed = 1;
        goto finish_with_live_restore;
    }

    for (int step = 0; step < GGPO_SELFTEST_FRAMES_PER_TICK && g_ggpo_selftest.active; step++) {
        int i = g_ggpo_selftest.frame_index;
        int mask_count = (int)(sizeof(k_ggpo_selftest_masks) / sizeof(k_ggpo_selftest_masks[0]));
        uint32_t frame_checksum = 0;
        GgpoFrameInputs inputs;
        inputs.player_cmd[0] = k_ggpo_selftest_masks[i % mask_count];
        inputs.player_cmd[1] = k_ggpo_selftest_masks[((i * 3) + 2) % mask_count];

        if (!ggpo_selftest_advance_frame_silent(&inputs, 0, &frame_checksum, err, sizeof(err))) {
            failed = 1;
            goto finish_with_live_restore;
        }

        if (g_ggpo_selftest.pass == 0) {
            g_ggpo_selftest.frame_checksums[i] = frame_checksum;
        } else if (frame_checksum != g_ggpo_selftest.frame_checksums[i]) {
            snprintf(err,
                     sizeof(err),
                     "replay checksum mismatch frame=%d expected=%u got=%u",
                     i + 1,
                     g_ggpo_selftest.frame_checksums[i],
                     frame_checksum);
            failed = 1;
            goto finish_with_live_restore;
        }

        g_ggpo_selftest.frame_index++;
        if (g_ggpo_selftest.frame_index >= g_ggpo_selftest.frames) {
            uint32_t replay_checksum = 0;
            uint32_t restored_checksum = 0;
            if (!lua_manager_game_state_rollback_checksum(&replay_checksum, err, sizeof(err))) {
                failed = 1;
                goto finish_with_live_restore;
            }

            if (g_ggpo_selftest.pass == 0) {
                g_ggpo_selftest.replay1_checksum = replay_checksum;
                if (!ggpo_ext_load_game_state(g_ggpo_selftest.initial_blob, g_ggpo_selftest.initial_len, err, sizeof(err))) {
                    failed = 1;
                    goto finish_with_live_restore;
                }
                if (!lua_manager_game_state_rollback_checksum(&restored_checksum, err, sizeof(err))) {
                    failed = 1;
                    goto finish_with_live_restore;
                }
                if (restored_checksum != g_ggpo_selftest.base_checksum) {
                    snprintf(err,
                             sizeof(err),
                             "restore checksum mismatch expected=%u got=%u",
                             g_ggpo_selftest.base_checksum,
                             restored_checksum);
                    failed = 1;
                    goto finish_with_live_restore;
                }
                if (!ggpo_ext_save_game_state(g_ggpo_selftest.work_blob,
                                              g_ggpo_selftest.state_size,
                                              &g_ggpo_selftest.work_len,
                                              NULL,
                                              err,
                                              sizeof(err))) {
                    failed = 1;
                    goto finish_with_live_restore;
                }
                g_ggpo_selftest.pass = 1;
                g_ggpo_selftest.frame_index = 0;
                break;
            }

            g_ggpo_selftest.replay2_checksum = replay_checksum;
            if (g_ggpo_selftest.replay1_checksum != g_ggpo_selftest.replay2_checksum) {
                snprintf(err,
                         sizeof(err),
                         "replay checksum mismatch expected=%u got=%u",
                         g_ggpo_selftest.replay1_checksum,
                         g_ggpo_selftest.replay2_checksum);
                failed = 1;
                goto finish_with_live_restore;
            }
            if (!ggpo_ext_load_game_state(g_ggpo_selftest.initial_blob, g_ggpo_selftest.initial_len, err, sizeof(err))) {
                failed = 1;
                goto finish_with_live_restore;
            }
            if (!lua_manager_game_state_rollback_checksum(&restored_checksum, err, sizeof(err))) {
                failed = 1;
                goto finish_with_live_restore;
            }
            if (restored_checksum != g_ggpo_selftest.base_checksum) {
                snprintf(err,
                         sizeof(err),
                         "restore checksum mismatch expected=%u got=%u",
                         g_ggpo_selftest.base_checksum,
                         restored_checksum);
                failed = 1;
                goto finish_with_live_restore;
            }
            completed = 1;
            break;
        }
    }

    if (!completed && !failed) {
        if (!ggpo_ext_save_game_state(g_ggpo_selftest.work_blob,
                                      g_ggpo_selftest.state_size,
                                      &g_ggpo_selftest.work_len,
                                      NULL,
                                      err,
                                      sizeof(err))) {
            failed = 1;
        }
    }

finish_with_live_restore:
    if (live_len > 0) {
        char restore_err[128];
        restore_err[0] = '\0';
        if (!ggpo_ext_load_game_state(g_ggpo_selftest.live_blob, live_len, restore_err, sizeof(restore_err)) && !failed) {
            snprintf(err, sizeof(err), "live restore failed (%s)", restore_err[0] ? restore_err : "unknown error");
            failed = 1;
        }
    }

finish_without_live_restore:
    if (failed) {
        snprintf(out, sizeof(out), "ggpo.selftest: failed (%s)", err[0] ? err : "unknown error");
        console_push_line_rgb(out, 0.98f, 0.45f, 0.45f);
        LOG_ERROR("%s", out);
        ggpo_selftest_cleanup();
    } else if (completed) {
        snprintf(out,
                 sizeof(out),
                 "ggpo.selftest: ok frames=%d initial=%u replay_final=%u",
                 g_ggpo_selftest.frames,
                 (unsigned int)g_ggpo_selftest.base_checksum,
                 (unsigned int)g_ggpo_selftest.replay1_checksum);
        console_push_line_rgb(out, 0.64f, 0.92f, 0.66f);
        LOG_INFO("%s", out);
        ggpo_selftest_cleanup();
    }
}

static void __cdecl hooked_game_update(int arg0) {
    fn_game_update_t real_update = p_game_update_trampoline
        ? p_game_update_trampoline
        : p_game_update;
    void* state_ptr = p_state_current ? p_state_current() : NULL;

    /* Authoritative: game_update IS the sim thread. Used by the RNG hooks to tell
     * sim draws (real gameplay seed) from synth draws on the SDL audio thread
     * (private audio seed). Cheap unconditional store. */
    g_sim_thread_id = GetCurrentThreadId();

    if (state_ptr != (void*)(uintptr_t)ADDR_GAME_STATE) {
        if (real_update) real_update(arg0);
        return;
    }

    lua_manager_on_tick();
    state_ptr = p_state_current ? p_state_current() : NULL;
    if (state_ptr != (void*)(uintptr_t)ADDR_GAME_STATE) {
        lua_manager_on_tick_post();
        hooks_finish_game_tick();
        return;
    }

    if (hooks_consume_block_game_tick()) {
        lua_manager_on_tick_post();
        hooks_finish_game_tick();
        run_pending_ggpo_selftest();
        return;
    }

    if (ggpo_loopback_active()) {
        uint32_t raw0 = hooks_peek_player_cmds_raw(0, 2);
        uint32_t raw1 = hooks_peek_player_cmds_raw(1, 2);
        uint32_t checksum = 0;
        char err[1024];
        char out[CONSOLE_LINE_TEXT];
        if (!ggpo_loopback_advance(raw0, raw1, arg0, &checksum, err, sizeof(err))) {
            snprintf(out, sizeof(out), "ggpo.loopback: failed (see log)");
            console_push_line_rgb(out, 0.98f, 0.45f, 0.45f);
            LOG_ERROR("ggpo.loopback: failed (%s)", err[0] ? err : "unknown error");
            ggpo_loopback_stop();
            lua_manager_on_tick_post();
            hooks_finish_game_tick();
            return;
        }
        lua_manager_on_tick_post();
        return;
    }

    if (ggpo_local_active()) {
        uint32_t raw0 = hooks_peek_player_cmds_raw(0, 2);
        uint32_t raw1 = hooks_peek_player_cmds_raw(1, 2);
        uint32_t checksum = 0;
        char err[512];
        char out[CONSOLE_LINE_TEXT];
        if (!ggpo_local_advance(raw0, raw1, arg0, &checksum, err, sizeof(err))) {
            snprintf(out, sizeof(out), "ggpo.local: failed (see log)");
            console_push_line_rgb(out, 0.98f, 0.45f, 0.45f);
            LOG_ERROR("ggpo.local: failed (%s)", err[0] ? err : "unknown error");
            ggpo_local_stop();
            lua_manager_on_tick_post();
            hooks_finish_game_tick();
            return;
        }
        lua_manager_on_tick_post();
        return;
    }

    if (ggpo_net_active()) {
        int net_tick_result = online_advance_net_gameplay_tick(arg0);
        lua_manager_on_tick_post();
        if (net_tick_result <= 0) {
            hooks_finish_game_tick();
        }
        return;
    }

    if (real_update) real_update(arg0);
    lua_manager_on_tick_post();
    hooks_finish_game_tick();
    run_pending_ggpo_selftest();
}

static uint32_t __cdecl hooked_main_player_poll_cmds(uint32_t player_index, uint32_t mode) {
    uint32_t raw = hooks_get_raw_input_blocked((int)player_index)
        ? 0u
        : hooks_peek_player_cmds_raw((int)player_index, (int)mode);
    return hooks_apply_effective_overrides(player_index, raw, 1);
}


// =============================
//
// counts are left queued at the end of a frame (cross-frame batching).
//
// Logs are rate-limited and written to mods/modframework.log.

// Query-only enums (guarded so we don't redefine what gl.h/glext.h already provides)

static RgbaImage* __cdecl hooked_rgba_load(const char* path) {
    fn_rgba_load_t real = p_rgba_load_trampoline ? p_rgba_load_trampoline : p_rgba_load;
    RgbaImage* img = real ? real(path) : NULL;
    if (img) {
        font_ext_on_rgba_load(path, img);
        texture_ext_on_rgba_load(path, img);
    }
    return img;
}

static int __cdecl hooked_atlas_upload(int atlas, int arg2, int format) {
    fn_atlas_upload_t real = p_atlas_upload_trampoline ? p_atlas_upload_trampoline : p_atlas_upload;
    if (atlas) {
        lua_manager_before_atlas_upload(atlas);
    }
    return real ? real(atlas, arg2, format) : -1;
}

static void __cdecl hooked_mapgen_init(void) {
    fn_void_void_t real = p_mapgen_init_trampoline ? p_mapgen_init_trampoline : p_mapgen_init;
    custom_maps_handle_mapgen_init(real);
}

static int __cdecl hooked_high_water_action(void* tile, int mode, int arg3, int arg4, int arg5) {
    int result = 0;

    if (p_high_water_action_trampoline) {
        // Preserve vanilla deep-water geometry/animation (the tall look).
        result = p_high_water_action_trampoline(tile, mode, arg3, arg4, arg5);
    }

    // During draw mode, replace the fallback textured high-water tile draw with
    // the tintable water sprite path while keeping the high-water transform.
    if (mode == 2) {
        float color[3] = { 0.0f, 0.0f, 0.0f };
        if (p_game_water_hi_colour) p_game_water_hi_colour(color);
        else if (p_game_water_colour) p_game_water_colour(color);

        *(volatile float*)(uintptr_t)ADDR_TURTLE_R = color[0];
        *(volatile float*)(uintptr_t)ADDR_TURTLE_G = color[1];
        *(volatile float*)(uintptr_t)ADDR_TURTLE_B = color[2];
        *(volatile float*)(uintptr_t)ADDR_TURTLE_A = 1.0f;

        if (tile && p_sprite_batch_plot && g_layer) {
            int flip = (int)(signed char)((unsigned char*)tile)[2];
            int sprite = *g_layer + 0x0b0c;
            p_sprite_batch_plot(sprite, flip, 0);
            return 1; // Prevent fallback sprite draw (which has the heavy texture).
        }
    }

    return result;
}

static void* __cdecl hooked_state_switch(void* target) {
    fn_state_switch_t real_switch = p_state_switch_trampoline
        ? p_state_switch_trampoline
        : (fn_state_switch_t)(uintptr_t)ADDR_STATE_SWITCH;
    void* cur = p_state_current ? p_state_current() : NULL;
    int leaving_game = (cur == (void*)(uintptr_t)ADDR_GAME_STATE ||
                        cur == (void*)(uintptr_t)ADDR_OPTIONS_STATE_PAUSED);
    int entering_main = (target == (void*)(uintptr_t)ADDR_MAIN_STATE ||
                         target == (void*)(uintptr_t)ADDR_MAIN_STATE_INITIAL);
    if (leaving_game && entering_main && (g_online_active_match.active || g_online_result.active || ggpo_net_active())) {
        online_reset_native_sound_state("state switch to main");
    }
    return real_switch ? real_switch(target) : target;
}


void hooks_init(void) {
    static int done = 0;
    if (done) return;
    done = 1;

    /* Runs during DLL load on the main thread; seed the sim-thread id so the RNG
     * hooks can tell sim draws from audio-thread synth draws even before the first
     * game_update (which re-affirms it authoritatively). */
    g_sim_thread_id = GetCurrentThreadId();




    custom_maps_init();

    if (!install_detour(&g_options_enter_detour, (void*)(uintptr_t)ADDR_OPTIONS_ENTER, (void*)&hooked_options_enter, 5)) {
        LOG_ERROR("hooks_init: failed to detour options enter");
        return;
    }
    p_options_enter_trampoline = (fn_void_void_t)g_options_enter_detour.trampoline;

    if (!install_detour(&g_options_enter_paused_detour, (void*)(uintptr_t)ADDR_OPTIONS_ENTER_PAUSED, (void*)&hooked_options_enter_paused, 5)) {
        LOG_ERROR("hooks_init: failed to detour paused options enter");
        return;
    }
    p_options_enter_paused_trampoline = (fn_void_void_t)g_options_enter_paused_detour.trampoline;

    if (!install_detour(&g_state_switch_detour, (void*)(uintptr_t)ADDR_STATE_SWITCH, (void*)&hooked_state_switch, 7)) {
        LOG_WARN("hooks_init: failed to detour state_switch (online sound cleanup will be less precise)");
    } else {
        p_state_switch_trampoline = (fn_state_switch_t)g_state_switch_detour.trampoline;
    }

    if (!install_detour(&g_main_update_with_buttons_detour, (void*)(uintptr_t)ADDR_MAIN_UPDATE_WITH_BUTTONS, (void*)&hooked_main_update_with_buttons, 6)) {
        LOG_WARN("hooks_init: failed to detour main_update_with_buttons (game tick API disabled)");
    } else {
        p_main_update_with_buttons_trampoline = (fn_main_update_with_buttons_t)g_main_update_with_buttons_detour.trampoline;
    }

    if (!install_detour(&g_main_player_poll_cmds_detour, (void*)(uintptr_t)ADDR_MAIN_PLAYER_POLL_CMDS, (void*)&hooked_main_player_poll_cmds, 5)) {
        LOG_WARN("hooks_init: failed to detour main_player_poll_cmds (input override API disabled)");
    } else {
        p_main_player_poll_cmds_trampoline = (fn_main_player_poll_cmds_t)g_main_player_poll_cmds_detour.trampoline;
    }

    if (!install_detour(&g_game_update_detour, (void*)(uintptr_t)ADDR_GAME_UPDATE, (void*)&hooked_game_update, 5)) {
        LOG_WARN("hooks_init: failed to detour game_update (GGPO/gameplay tick API disabled)");
    } else {
        p_game_update_trampoline = (fn_game_update_t)g_game_update_detour.trampoline;
    }

    // Detour rgba_load so we can patch data/font8x8.png pixels before it is
    // packed into the engine's glyph atlas.
    // rgba_load has an 8-byte prologue (push ebx; sub esp,0x28; lea ...), so
    // we patch 8 bytes to avoid splitting instructions.
    if (!install_detour(&g_rgba_load_detour, (void*)(uintptr_t)ADDR_RGBA_LOAD, (void*)&hooked_rgba_load, 8)) {
        LOG_ERROR("hooks_init: failed to detour rgba_load (font glyph overlay disabled)");
    } else {
        p_rgba_load_trampoline = (fn_rgba_load_t)g_rgba_load_detour.trampoline;
    }

    if (!install_detour(&g_game_player_colour_index_detour,
                        (void*)(uintptr_t)ADDR_GAME_PLAYER_COLOUR_INDEX,
                        (void*)&hooked_game_player_colour_index,
                        9)) {
        LOG_WARN("hooks_init: failed to detour game_player_colour_index (expanded player colours will not save correctly)");
    } else {
        p_game_player_colour_index_trampoline = (fn_game_player_colour_index_t)g_game_player_colour_index_detour.trampoline;
    }

    if (!install_detour(&g_game_set_player_colour_index_detour,
                        (void*)(uintptr_t)ADDR_GAME_SET_PLAYER_COLOUR_INDEX,
                        (void*)&hooked_game_set_player_colour_index,
                        6)) {
        LOG_WARN("hooks_init: failed to detour game_set_player_colour_index (expanded player colours disabled)");
    } else {
        p_game_set_player_colour_index_trampoline = (fn_game_set_player_colour_index_t)g_game_set_player_colour_index_detour.trampoline;
    }

    if (!install_detour(&g_game_player_colour_detour,
                        (void*)(uintptr_t)ADDR_GAME_PLAYER_COLOUR,
                        (void*)&hooked_game_player_colour,
                        7)) {
        LOG_WARN("hooks_init: failed to detour game_player_colour (expanded player colour rendering disabled)");
    } else {
        p_game_player_colour_trampoline = (fn_game_player_colour_t)g_game_player_colour_detour.trampoline;
    }

    if (!install_detour(&g_game_inc_player_colour_ex_detour,
                        (void*)(uintptr_t)ADDR_GAME_INC_PLAYER_COLOUR_EX,
                        (void*)&hooked_game_inc_player_colour_ex,
                        7)) {
        LOG_WARN("hooks_init: failed to detour game_inc_player_colour_ex (main menu colour buttons keep vanilla wrap)");
    } else {
        p_game_inc_player_colour_ex_trampoline = (fn_game_inc_player_colour_ex_t)g_game_inc_player_colour_ex_detour.trampoline;
    }

    // draw_player_body starts with:
    //   push ebp
    //   mov ecx, 0x18
    // Patch 6 bytes so custom character overlays can hide only the vanilla
    // body layers without making the player's sword inherit transparent tints.
    if (!install_detour(&g_draw_player_body_detour,
                        (void*)(uintptr_t)ADDR_DRAW_PLAYER_BODY,
                        (void*)&hooked_draw_player_body,
                        6)) {
        LOG_WARN("hooks_init: failed to detour draw_player_body (custom characters will draw over vanilla bodies)");
    } else {
        g_draw_player_body_trampoline = g_draw_player_body_detour.trampoline;
    }

    // A few sword/pose paths draw the vanilla arm directly with sprite_batch_plot
    // instead of going through draw_player_body. Filter only those return sites
    // for custom-character slots, leaving the sword's misc sprites visible.
    if (!install_detour(&g_sprite_batch_plot_detour,
                        (void*)(uintptr_t)ADDR_SPRITE_BATCH_PLOT,
                        (void*)&hooked_sprite_batch_plot,
                        6)) {
        LOG_WARN("hooks_init: failed to detour sprite_batch_plot (custom characters may show vanilla arm layers)");
    } else {
        g_sprite_batch_plot_trampoline = g_sprite_batch_plot_detour.trampoline;
    }

    // turtle_trans begins with two 4-byte double loads. For custom characters,
    // replace the vanilla swordfight idle sway with a Lua-supplied per-player
    // offset so the sword can match the custom body animation instead.
    if (!install_detour(&g_turtle_trans_detour,
                        (void*)(uintptr_t)ADDR_TURTLE_TRANS,
                        (void*)&hooked_turtle_trans,
                        8)) {
        LOG_WARN("hooks_init: failed to detour turtle_trans (custom sword idle offsets disabled)");
    } else {
        g_turtle_trans_trampoline = g_turtle_trans_detour.trampoline;
    }

    // mapgen_init starts with `sub esp, 0x2c` (3 bytes) followed by a 6-byte
    // absolute mov. Patch 9 bytes so the trampoline never returns into a split
    // instruction.
    if (!install_detour(&g_mapgen_init_detour, (void*)(uintptr_t)ADDR_MAPGEN_INIT, (void*)&hooked_mapgen_init, 9)) {
        LOG_ERROR("hooks_init: failed to detour mapgen_init");
        return;
    }
    p_mapgen_init_trampoline = (fn_void_void_t)g_mapgen_init_detour.trampoline;

    // high_water_action starts with:
    //   push ebx         (1)
    //   sub esp, 0x28    (3)
    //   cmp [esp+0x34],2 (5)
    // Patch 9 bytes to avoid splitting instructions.
    if (!install_detour(&g_high_water_action_detour, (void*)(uintptr_t)ADDR_HIGH_WATER_ACTION, (void*)&hooked_high_water_action, 9)) {
        LOG_WARN("hooks_init: failed to detour high_water_action (W will keep vanilla rendering)");
    } else {
        p_high_water_action_trampoline = (fn_tile_action_t)g_high_water_action_detour.trampoline;
    }

    // atlas_upload starts with:
    //   push ebp
    //   mov ebp, esp
    //   push edi
    //   push esi
    // Patch exactly 5 bytes so mod asset spritesheets can be packed before upload.
    if (!install_detour(&g_atlas_upload_detour, (void*)(uintptr_t)ADDR_ATLAS_UPLOAD, (void*)&hooked_atlas_upload, 5)) {
        LOG_WARN("hooks_init: failed to detour atlas_upload (mod.assets PNG sheets will stay pending)");
    } else {
        p_atlas_upload_trampoline = (fn_atlas_upload_t)g_atlas_upload_detour.trampoline;
    }

    if (!install_detour(&g_rng_mrand_detour, (void*)(uintptr_t)ADDR_MRAND, (void*)&hooked_mrand, 10)) {
        LOG_WARN("hooks_init: failed to detour mrand (RNG tracing disabled for mrand)");
    } else {
        g_hooks_rng_mrand_trampoline = g_rng_mrand_detour.trampoline;
    }
    if (!install_detour(&g_rng_rnd_detour, (void*)(uintptr_t)ADDR_RND, (void*)&hooked_rnd, 10)) {
        LOG_WARN("hooks_init: failed to detour rnd (RNG tracing disabled for rnd)");
    } else {
        g_hooks_rng_rnd_trampoline = g_rng_rnd_detour.trampoline;
    }
    if (!install_detour(&g_rng_frnd_detour, (void*)(uintptr_t)ADDR_FRND, (void*)&hooked_frnd, 13)) {
        LOG_WARN("hooks_init: failed to detour frnd (RNG tracing disabled for frnd)");
    } else {
        g_hooks_rng_frnd_trampoline = g_rng_frnd_detour.trampoline;
    }
    if (!install_detour(&g_rng_rnd5050_detour, (void*)(uintptr_t)ADDR_RND5050, (void*)&hooked_rnd5050, 10)) {
        LOG_WARN("hooks_init: failed to detour rnd5050 (RNG tracing disabled for rnd5050)");
    } else {
        g_hooks_rng_rnd5050_trampoline = g_rng_rnd5050_detour.trampoline;
    }
    if (!install_detour(&g_rng_rndsign_detour, (void*)(uintptr_t)ADDR_RNDSIGN, (void*)&hooked_rndsign, 13)) {
        LOG_WARN("hooks_init: failed to detour rndsign (RNG tracing disabled for rndsign)");
    } else {
        g_hooks_rng_rndsign_trampoline = g_rng_rndsign_detour.trampoline;
    }

    /*
     * respawn_warble + synth_effect_whistling are synth DSP voice callbacks that
     * run on the SDL audio thread. They are now covered generically by the
     * audio-thread branch in hooked_rnd/frnd/rnd5050/mrand/rndsign (their internal
     * draws are served from the private audio seed), so we no longer wrap them
     * individually. The old wrappers also swapped _mrand_seed from the audio thread,
     * which RACED the sim thread - exactly the bug we are removing. Intentionally
     * not installed. sound_sword_ching below stays: it is a MAIN-thread trigger
     * (player_block_sword_at_ex), so its pitch frnd must be isolated on the sim
     * thread and that wrapper is race-free.
     */
    if (!install_detour(&g_sound_sword_ching_detour, (void*)(uintptr_t)ADDR_SOUND_SWORD_CHING, (void*)&hooked_sound_sword_ching, 6)) {
        LOG_WARN("hooks_init: failed to detour sound_sword_ching (audio RNG may affect gameplay RNG)");
    } else {
        p_sound_sword_ching_trampoline = (fn_sound_sword_ching_t)g_sound_sword_ching_detour.trampoline;
    }





LOG_INFO("hooks_init: custom MODS menu ready");
}
