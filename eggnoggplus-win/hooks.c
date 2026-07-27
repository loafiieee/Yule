#include <windows.h>
#include <shellapi.h>

#ifdef __INTELLISENSE__
#define HOOKS_INTELLISENSE 1
#endif
#include <excpt.h>
#include <limits.h>
#include <math.h>
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
#ifndef SDLK_F1
#define SDLK_F1 1073741882
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
#ifndef SDLK_F11
#define SDLK_F11 1073741892
#endif

#include "hooks.h"
#include "ggpo_ext.h"
#include "ggpo_loopback.h"
#include "ggpo_local.h"
#include "ggpo_net.h"
#include "fp_control.h"
#include "lua_manager.h"
#include "font_ext.h"
#include "texture_ext.h"
#include "custom_maps.h"
#include "content_bridge.h"
#include "content_tiles.h"
#include "map_script.h"
#include "log.h"
#include "net_ext.h"
#include "update_ext.h"
#include "credential_ext.h"
#include "online_control.h"
#include "launch_request.h"
#include "launch_ipc.h"
#include "window_policy.h"
#include "cursor_ext.h"
#include "discord_rpc_ext.h"
#include "bytebeat_ext.h"
#include "bytebeat_js.h"
#include "bytebeat_stream.h"

extern char* SDL_GetClipboardText(void);
extern int SDL_SetClipboardText(const char* text);
extern void SDL_free(void* mem);


#define ADDR_STATE_CURRENT            0x405DB0u
#define ADDR_STATE_LAST               0x405DB8u
#define ADDR_STATE_SWITCH             0x405DC0u
#define ADDR_MAD_INIT_AUDIO_STREAM    0x404240u
#define ADDR_MAIN_UPDATE_WITH_BUTTONS 0x4340E0u
#define ADDR_GAME_UPDATE              0x42C590u
#define ADDR_MAIN_TALLY_TUNES         0x430640u
#define ADDR_GAME_PICK_RANDOM_TUNE    0x4306C0u
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
#define ADDR_SPRITE_GET               0x405D20u
#define ADDR_SPRITE_BATCH_DRAW        0x405A90u
#define ADDR_ATLAS_UPLOAD             0x401480u
#define ADDR_MISC_ID                  0x547B7Cu
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
#define ADDR_TURTLE_SET_SCALEX        0x4090A0u
#define ADDR_TURTLE_SET_SCALEY        0x4090B0u
#define ADDR_TURTLE_SET_RGB           0x4091E0u
#define ADDR_TURTLE_SET_RGBA          0x4090C0u
#define ADDR_TURTLE_RESET             0x4092D0u
#define ADDR_MAD_W                    0x404300u
#define ADDR_MAD_H                    0x404320u
#define ADDR_OPTIONS_STATE            0x448398u
#define ADDR_OPTIONS_STATE_PAUSED     0x448388u
#define ADDR_REMAP_STATE2             0x4483B8u
#define ADDR_REMAP_STATE1             0x4483C8u
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
/* Native window control (the game's own F11 fullscreen / F1 window-size logic,
 * which sets up the GL viewport correctly - used to apply -fullscreen/-windowed
 * launch args instead of a raw SDL flag). All __cdecl. */
#define ADDR_MAIN_SET_FULLSCREEN      0x430AA0u
#define ADDR_MAIN_SET_WINDOW          0x4309E0u
#define ADDR_MAIN_IS_FULLSCREEN       0x430AE0u
#define ADDR_MAIN_SAVED_WINDOW_H      0x448360u
#define ADDR_MAIN_SAVED_WINDOW_W      0x448364u
#define ADDR_WRAPPER_DESKTOP_H         0x4EDB70u
#define ADDR_WRAPPER_DESKTOP_W         0x4EDB74u
#define ADDR_MAPGEN_INIT              0x437D30u
#define ADDR_MAPGEN_BUILD_MAP         0x437DB0u
#define ADDR_TILE_ACTION_EX           0x440250u
#define ADDR_HIGH_WATER_ACTION        0x43C730u
#define ADDR_GAME_WATER_HI_COLOUR     0x420100u
#define ADDR_GAME_WATER_COLOUR        0x4201D0u
#define ADDR_GAME_PLAYER_COLOUR_INDEX 0x41FE40u
#define ADDR_GAME_SET_PLAYER_COLOUR_INDEX 0x41FE60u
#define ADDR_GAME_PLAYER_COLOUR       0x4207C0u
#define ADDR_GAME_INC_PLAYER_COLOUR_EX 0x420E30u
#define ADDR_ANGLE_COLOUR             0x4180F0u
#define ADDR_DRAW_PLAYER_BODY         0x41BDD0u
#define ADDR_THING_NEW                0x41FD40u
#define ADDR_PLAYER_ARRAY             0x542058u
#define ADDR_THINGS                   0x542080u
#define ADDR_THING_INFO               0x543640u
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
#define ADDR_MAP_TILE_LAYER           0x54A1E0u
#define ADDR_TILEMAP_DATA_PTR         0x54A1E4u
#define ADDR_TILEMAP_W                0x54A1E8u
#define ADDR_TILEMAP_H                0x54A1ECu
#define ADDR_TILE_W                   0x54A1F0u
#define ADDR_TILE_H                   0x54A1F4u
#define ADDR_TILE_INFO                0x55AB40u
#define ADDR_TURTLE_STATE             0x4480C0u
#define ADDR_NATIVE_SYNTH_ENABLED     0x54C0CAu
#define ADDR_AUDIO_STREAM_INITED      0x54C0C8u   /* DAT_0054c0c8: >0 once mad_init_audio_stream ran */
#define ADDR_SOUND_SETTING            0x55A170u   /* DAT_0055a170: saved "sound on" setting (0=off) */
#define ADDR_MUSIC_SETTING            0x55A16Cu
#define ADDR_GLITCH_CALLBACK          0x54C0D4u
#define ADDR_FORCED_TUNE              0x55A17Cu
#define ADDR_TUNE_COUNT               0x547B6Cu
#define ADDR_SHUFFLE_TUNE             0x54A1A0u
#define ADDR_LAST_TUNE                0x448368u
#define ADDR_SEED                     0x542074u
#define ADDR_MRAND_SEED               0x496DA0u
#define ADDR_MRAND                    0x405080u
#define ADDR_RND                      0x4050B0u
#define ADDR_FRND                     0x405170u
#define ADDR_RND5050                  0x405210u
#define ADDR_RNDSIGN                  0x4052C0u
#define ADDR_ONEIN                    0x4053F0u
#define ADDR_HAZARD_ANIM              0x43C450u
#define ADDR_RESPAWN_WARBLE           0x41BB70u
#define ADDR_SYNTH_EFFECT_WHISTLING   0x41BBD0u
#define ADDR_SOUND_SWORD_CHING        0x425D70u
#define ADDR_SYNTH_EFFECTS_INIT       0x408780u
#define ADDR_SYN_ENABLE_RANGE         0x406F40u
#define ADDR_SYNTH_ENGINE             0x5540E0u

/*
 * Vanilla opens SDL at 22,050 Hz. That is below the native rate of most
 * Dollchan library tracks and aliases their upper harmonics even when `t`
 * advances at the correct source rate. Keep the formula rate independent,
 * but open the actual game mixer at the highest authoring rate accepted by
 * yule:bytebeat so every supported track is upsampled or rendered 1:1.
 */
#define FRAMEWORK_AUDIO_VANILLA_RATE 22050
#define FRAMEWORK_AUDIO_OUTPUT_RATE  48000

#define PLAYER_SIZE                   0x15Cu
#define PLAYER_OFS_X                  0x24u
#define PLAYER_OFS_Y                  0x28u
#define PLAYER_OFS_VX                 0x34u
#define PLAYER_OFS_VY                 0x38u
#define PLAYER_OFS_STATE_ID           0x78u
#define PLAYER_OFS_ROOM               0x9Bu
#define PLAYER_STATE_DEAD_BODY        0x08u

#define THING_SIZE                    0x15Cu
#define THING_SLOT_COUNT              ((ADDR_THING_INFO - ADDR_THINGS) / THING_SIZE)
#define THING_OFS_ACTIVE              0x00u
#define THING_OFS_TYPE                0x01u
#define THING_OFS_X                   0x24u
#define THING_OFS_Y                   0x28u
#define THING_OFS_PREV_X              0x2Cu
#define THING_OFS_PREV_Y              0x30u
#define THING_OFS_VX                  0x34u
#define THING_OFS_VY                  0x38u
#define THING_OFS_CONTACT_RADIUS      0x6Cu
#define THING_OFS_UPDATE_FN           0x158u
#define THING_TYPE_PLAYER             0x01u
#define THING_TYPE_SWORD              0x02u
#define THING_TYPE_HAZARD             0x03u

/* map.sensor permits a tile-local box to reach two cells left/up and three
 * right/down, while a custom object box may reach two cells from its center.
 * Five cells in each direction is therefore the complete bounded search. */
#define MAP_SCRIPT_SENSOR_CELL_RADIUS 5

// Asset load hook used for moddable font glyph overlays.
#define ADDR_RGBA_LOAD                0x4022A0u

#define MAX_MENU_ROWS     2048
#define MAX_MODS_TRACKED   512
#define CAPTURE_BUF_SIZE   256
#define BASE_UI_W        1280.0f
#define BASE_UI_H         720.0f
#define MODS_CURSOR_ROW_Y_FACTOR   0.50f
#define MODS_CURSOR_ROW_Y_NUDGE    0.0f
#define MODS_CURSOR_OUTER_PAD_X   34.0f  /* push swords clear of the inset panel */
#define MODS_FOOTER_Y_OFF         48.0f  /* "Back" footer baseline below list_bottom (panel ends ~+10); low enough the sword tops clear the panel */
#define MODS_BACK_SWORD_Y_OFF      8.0f  /* extra drop for the Back swords only (keeps text put) */

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
/* Per-button style fields (from button_ex decompile: _btns base 0x50e3c0,
 * stride 0x148; verified against tag +0x04 / center +0x10 / action +0xE4). */
#define BTN_OFS_BACKING           0x08   /* backing sprite id (panel art) */
#define BTN_OFS_FG_RGBA           0x30   /* label color, 4 floats */
#define BTN_OFS_BG_RGBA           0x40   /* fill color, 4 floats */
#define BTN_OFS_HI_BG_RGBA        0x50   /* highlighted fill, 4 floats */
#define BTN_OFS_HI_FG_RGBA        0x60   /* highlighted label, 4 floats */

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
#define ONLINE_HUB_CAPTURE_MAX   512
#define ONLINE_HUB_STATUS_MAX    256
#define ONLINE_CHALLENGE_MAP_MAX 4096
#define ONLINE_HUB_CFG_PATH      "mods\\online_hub.cfg"
#define ONLINE_DEFAULT_SERVER_HOST "eggnogg.loafiieee.com"
#define ONLINE_DEFAULT_SERVER_PORT 47778
#define ONLINE_ABANDON_GRACE_TICKS 90
#define ONLINE_MATCH_COUNTDOWN_FRAMES 150
/* Hole punching depends on the NAT mapping chosen for a UDP socket. Retrying with
 * a fresh socket (and therefore a fresh mapping) gives ordinary NATs more chances
 * to line up without changing any post-connect rollback behavior. Wall-clock
 * deadlines keep duplicate UI/update pumps from shortening an attempt. */
#define ONLINE_CONNECT_ATTEMPT_MS       8000u
#define ONLINE_CONNECT_START_FAILURE_MS  250u
#define ONLINE_CONNECT_MAX_ATTEMPTS        3
#define ONLINE_PREMATCH_SETUP_TIMEOUT_MS 45000u
#define ONLINE_SERVER_CONNECT_TIMEOUT_MS 10000u
#define ONLINE_SERVER_AUTH_TIMEOUT_MS    10000u
#define ONLINE_SERVER_HEARTBEAT_MS       30000u
#define ONLINE_SERVER_LINE_CAP            8192u
#define ONLINE_MAP_MANIFEST_MAX_BYTES    (96u * 1024u)
#define ONLINE_CONTROL_PROTOCOL_VERSION      3
#define ONLINE_MATCH_PROTOCOL_VERSION        3
#define ONLINE_RESULT_TOAST_FRAMES 420
#define ONLINE_CHALLENGE_TOAST_FADE_FRAMES 30
#define UPDATE_TOAST_VISIBLE_MS 12000u

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
    ROW_FW_TOGGLE,   // framework-level toggle (cfg_index = FW_SETTING_*)
    ROW_FW_ACTION,   // framework-level action (cfg_index = FW_ACTION_*)
} RowKind;

// Framework-level settings shown at the top of the mods menu (cfg_index of a ROW_FW_TOGGLE row).
#define FW_SETTING_LOG_CONSOLE 0
#define FW_SETTING_AUTO_UPDATE 1
#define FW_SETTING_DISCORD_PRESENCE 2
#define FW_ACTION_UPDATE       0

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
    ONLINE_ROW_CHALLENGE_MAP,
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
    ONLINE_ACTION_SEND_CHALLENGE,
    ONLINE_ACTION_CANCEL_CHALLENGE_MAP,
    ONLINE_ACTION_ACCEPT_FRIEND,
    ONLINE_ACTION_DECLINE_FRIEND,
    ONLINE_ACTION_ACCEPT_CHALLENGE,
    ONLINE_ACTION_DECLINE_CHALLENGE,
    ONLINE_ACTION_REMOVE_FRIEND,
    ONLINE_ACTION_SAVE_SETTINGS,
} OnlineHubAction;

typedef enum OnlineFriendContextAction {
    ONLINE_CONTEXT_NONE = 0,
    ONLINE_CONTEXT_CHALLENGE,
    ONLINE_CONTEXT_MUTE,
    ONLINE_CONTEXT_BLOCK,
    ONLINE_CONTEXT_UNFRIEND,
    ONLINE_CONTEXT_UNBLOCK,
} OnlineFriendContextAction;

typedef enum OnlineHubSetting {
    ONLINE_SETTING_NONE = 0,
    ONLINE_SETTING_USERNAME,
    ONLINE_SETTING_PASSWORD,
    ONLINE_SETTING_REMEMBER_ME,
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
    ONLINE_SETTING_DISCORD_PRESENCE,
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

typedef enum OnlineRematchState {
    ONLINE_REMATCH_NONE = 0,
    ONLINE_REMATCH_AVAILABLE,
    ONLINE_REMATCH_WAITING,
    ONLINE_REMATCH_OFFERED,
    ONLINE_REMATCH_STARTING,
} OnlineRematchState;

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
    char presence[32];
    char host[ONLINE_HUB_TEXT_MAX];
    uint16_t port;
    int blocked;
    int muted;
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
    char map_key[128];
    char map_label[96];
    int elo;
    int expires_in;
    int muted;
} OnlineChallenge;

typedef struct OnlineMapChoice {
    char key[128];
    char label[96];
} OnlineMapChoice;

typedef struct OnlineChallengeMapPicker {
    int active;
    int loading;
    int request_id;
    int expected_count;
    int selected;
    char username[48];
    OnlineMapChoice* choices;
    int choice_count;
    int choice_capacity;
} OnlineChallengeMapPicker;

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
    char map_label[96];
} OnlineChallengeToast;

typedef struct OnlineHubConfig {
    char username[48];
    char password[257];
    int remember_me;
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
typedef void  (__cdecl *fn_mad_init_audio_stream_t)(int, int);
typedef void  (__cdecl *fn_main_cursors_reset_t)(float, float);
typedef void  (__cdecl *fn_main_cursor_spin_t)(int);
typedef void  (__cdecl *fn_main_sprite_batches_draw_t)(void);
typedef void  (__cdecl *fn_sprite_batch_plot_t)(int sprite, int flip, int layer);
typedef void* (__cdecl *fn_sprite_get_t)(uint32_t sprite_id);
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
typedef void  (__cdecl *fn_turtle_set_scalar_t)(double);
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
typedef void* (__cdecl *fn_thing_new_t)(int);
typedef void (__cdecl *fn_glitch_audio_callback_t)(int16_t*, int, int);

static fn_state_current_t            p_state_current = (fn_state_current_t)(uintptr_t)ADDR_STATE_CURRENT;
static fn_state_current_t            p_state_last = (fn_state_current_t)(uintptr_t)ADDR_STATE_LAST;
static fn_state_switch_t             p_state_switch = (fn_state_switch_t)(uintptr_t)ADDR_STATE_SWITCH;
static fn_main_update_with_buttons_t p_main_update_with_buttons = (fn_main_update_with_buttons_t)(uintptr_t)ADDR_MAIN_UPDATE_WITH_BUTTONS;
static fn_game_update_t              p_game_update = (fn_game_update_t)(uintptr_t)ADDR_GAME_UPDATE;
static fn_mad_init_audio_stream_t    p_mad_init_audio_stream_trampoline = NULL;
static fn_void_void_t                p_main_tally_tunes = (fn_void_void_t)(uintptr_t)ADDR_MAIN_TALLY_TUNES;
static fn_void_void_t                p_game_pick_random_tune = (fn_void_void_t)(uintptr_t)ADDR_GAME_PICK_RANDOM_TUNE;
static fn_void_void_t                p_main_draw = (fn_void_void_t)(uintptr_t)ADDR_MAIN_DRAW;
static fn_void_void_t                p_menu_common_render = (fn_void_void_t)(uintptr_t)ADDR_MENU_COMMON_RENDER;
static fn_void_void_t                p_main_buttons_start = (fn_void_void_t)(uintptr_t)ADDR_MAIN_BUTTONS_START;
static fn_void_void_t                p_game_reset = (fn_void_void_t)(uintptr_t)ADDR_GAME_RESET;
static fn_void_void_t                p_game_start = (fn_void_void_t)(uintptr_t)ADDR_GAME_START;
static fn_main_cursors_reset_t       p_main_cursors_reset = (fn_main_cursors_reset_t)(uintptr_t)ADDR_MAIN_CURSORS_RESET;
static fn_main_cursor_spin_t        p_main_cursor_spin = (fn_main_cursor_spin_t)(uintptr_t)ADDR_MAIN_CURSOR_SPIN;
static fn_main_sprite_batches_draw_t p_main_sprite_batches_draw = (fn_main_sprite_batches_draw_t)(uintptr_t)ADDR_MAIN_SPRITE_BATCHES_DRAW;
static fn_sprite_batch_plot_t        p_sprite_batch_plot = (fn_sprite_batch_plot_t)(uintptr_t)ADDR_SPRITE_BATCH_PLOT;
static fn_sprite_get_t               p_sprite_get = (fn_sprite_get_t)(uintptr_t)ADDR_SPRITE_GET;
static volatile int*                 p_misc_id = (volatile int*)(uintptr_t)ADDR_MISC_ID;
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
static fn_turtle_set_scalar_t        p_turtle_set_scalex = (fn_turtle_set_scalar_t)(uintptr_t)ADDR_TURTLE_SET_SCALEX;
static fn_turtle_set_scalar_t        p_turtle_set_scaley = (fn_turtle_set_scalar_t)(uintptr_t)ADDR_TURTLE_SET_SCALEY;
static fn_turtle_set_rgb_t           p_turtle_set_rgb = (fn_turtle_set_rgb_t)(uintptr_t)ADDR_TURTLE_SET_RGB;
static fn_turtle_set_rgba_t          p_turtle_set_rgba = (fn_turtle_set_rgba_t)(uintptr_t)ADDR_TURTLE_SET_RGBA;
static fn_turtle_reset_t             p_turtle_reset = (fn_turtle_reset_t)(uintptr_t)ADDR_TURTLE_RESET;
static fn_mad_dim_t                  p_mad_w = (fn_mad_dim_t)(uintptr_t)ADDR_MAD_W;
static fn_mad_dim_t                  p_mad_h = (fn_mad_dim_t)(uintptr_t)ADDR_MAD_H;
static fn_void_void_t                p_options_enter = (fn_void_void_t)(uintptr_t)ADDR_OPTIONS_ENTER;
static fn_void_void_t                p_options_enter_paused = (fn_void_void_t)(uintptr_t)ADDR_OPTIONS_ENTER_PAUSED;
typedef void (__cdecl *fn_set_fullscreen_t)(int);
typedef void (__cdecl *fn_set_window_t)(int, int);
typedef int  (__cdecl *fn_is_fullscreen_t)(void);
static fn_set_fullscreen_t           p_main_set_fullscreen = (fn_set_fullscreen_t)(uintptr_t)ADDR_MAIN_SET_FULLSCREEN;
static fn_set_window_t               p_main_set_window = (fn_set_window_t)(uintptr_t)ADDR_MAIN_SET_WINDOW;
static fn_is_fullscreen_t            p_main_is_fullscreen = (fn_is_fullscreen_t)(uintptr_t)ADDR_MAIN_IS_FULLSCREEN;
static volatile int*                 p_main_saved_window_h = (volatile int*)(uintptr_t)ADDR_MAIN_SAVED_WINDOW_H;
static volatile int*                 p_main_saved_window_w = (volatile int*)(uintptr_t)ADDR_MAIN_SAVED_WINDOW_W;
static volatile int*                 p_wrapper_desktop_h = (volatile int*)(uintptr_t)ADDR_WRAPPER_DESKTOP_H;
static volatile int*                 p_wrapper_desktop_w = (volatile int*)(uintptr_t)ADDR_WRAPPER_DESKTOP_W;
extern void* g_proxy_sdl_window;
extern void* p_SDL_GetWindowSize;
extern void* p_SDL_GL_GetDrawableSize;
extern void* p_SDL_GetWindowDisplayIndex;
extern void* p_SDL_GetDisplayBounds;
extern void* p_SDL_GetWindowFlags;
extern void* p_SDL_GetError;
extern void* p_SDL_SetWindowFullscreen;
extern volatile LONG g_proxy_sdl_display_override;
static fn_void_void_t                p_mapgen_init = (fn_void_void_t)(uintptr_t)ADDR_MAPGEN_INIT;
static fn_void_void_t                p_mapgen_build_map = (fn_void_void_t)(uintptr_t)ADDR_MAPGEN_BUILD_MAP;
static fn_tile_action_t              p_tile_action_ex = (fn_tile_action_t)(uintptr_t)ADDR_TILE_ACTION_EX;
static fn_thing_new_t                p_thing_new_trampoline = NULL;
static fn_void_void_t                p_options_enter_trampoline = NULL;
static fn_void_void_t                p_options_enter_paused_trampoline = NULL;
static fn_void_void_t                p_mapgen_init_trampoline = NULL;
static fn_void_void_t                p_mapgen_build_map_trampoline = NULL;
static fn_tile_action_t              p_tile_action_ex_trampoline = NULL;
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
static uint8_t*                      p_things = (uint8_t*)(uintptr_t)ADDR_THINGS;
static volatile int* g_layer = (volatile int*)(uintptr_t)ADDR_LAYER;
static volatile int* g_map_tile_layer =
    (volatile int*)(uintptr_t)ADDR_MAP_TILE_LAYER;
static volatile uint32_t* g_mad_ticks = (volatile uint32_t*)(uintptr_t)ADDR_MAD_TICKS;
static volatile uint32_t* g_game_ticks = (volatile uint32_t*)(uintptr_t)ADDR_GAME_TICKS;
static volatile int* g_native_forced_tune = (volatile int*)(uintptr_t)ADDR_FORCED_TUNE;
static volatile int* g_native_tune_count = (volatile int*)(uintptr_t)ADDR_TUNE_COUNT;
static volatile int* g_native_shuffle_tune = (volatile int*)(uintptr_t)ADDR_SHUFFLE_TUNE;
static volatile int* g_native_last_tune = (volatile int*)(uintptr_t)ADDR_LAST_TUNE;
static volatile int* g_native_music_setting = (volatile int*)(uintptr_t)ADDR_MUSIC_SETTING;
static fn_glitch_audio_callback_t volatile* g_native_glitch_callback =
    (fn_glitch_audio_callback_t volatile*)(uintptr_t)ADDR_GLITCH_CALLBACK;
static fn_glitch_audio_callback_t g_framework_previous_audio_callback = NULL;
static volatile uintptr_t* g_tilemap_data_ptr = (volatile uintptr_t*)(uintptr_t)ADDR_TILEMAP_DATA_PTR;
static volatile int* g_tilemap_width = (volatile int*)(uintptr_t)ADDR_TILEMAP_W;
static volatile int* g_tilemap_height = (volatile int*)(uintptr_t)ADDR_TILEMAP_H;
static volatile int* g_tile_width = (volatile int*)(uintptr_t)ADDR_TILE_W;
static volatile int* g_tile_height = (volatile int*)(uintptr_t)ADDR_TILE_H;
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
static volatile signed char* g_audio_stream_inited = (volatile signed char*)(uintptr_t)ADDR_AUDIO_STREAM_INITED;
static volatile int* g_sound_setting = (volatile int*)(uintptr_t)ADDR_SOUND_SETTING;
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
static Detour g_mad_init_audio_stream_detour;
static volatile LONG g_framework_audio_config_rate =
    FRAMEWORK_AUDIO_OUTPUT_RATE;
static volatile LONG g_framework_audio_active_rate = 0;
static Detour g_state_switch_detour;
static Detour g_main_update_with_buttons_detour;
static Detour g_game_update_detour;
static Detour g_thing_new_detour;
static Detour g_main_player_poll_cmds_detour;
static Detour g_rgba_load_detour;
static Detour g_mapgen_init_detour;
static Detour g_mapgen_build_map_detour;
static Detour g_tile_action_ex_detour;
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
static Detour g_onein_detour;
static void*  g_hooks_onein_trampoline = NULL;
static Detour g_respawn_warble_detour;
static Detour g_synth_effect_whistling_detour;
static Detour g_sound_sword_ching_detour;
static volatile LONG g_content_bridge_enabled = 0;
static volatile LONG g_thing_lifecycle_tracking_enabled = 0;
/* A V2 map may provide one complete native-layout tile sheet.  The qualified
 * key is copied at map-build time; atlas ids/pointers are intentionally
 * resolved at draw time because the framework can rebuild the atlas. */
static char g_map_native_tileset_sheet[CONTENT_SHEET_KEY_MAX] = {0};
static int g_map_native_tileset_sprite_count = 0;
static int g_map_native_tileset_enabled = 0;

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

/* The updater starts here, on the render thread with a live application and
 * GL context.  It must never be booted from DllMain's loader lock. */
static volatile LONG g_update_runtime_booted = 0;
static volatile LONG g_update_handoff_started = 0;
static DWORD g_update_handoff_retry_ms = 0u;
static uint32_t g_update_toast_started_ms = 0;
static int g_update_toast_status = -1;

typedef enum WindowLaunchMode {
    WINDOW_LAUNCH_DEFAULT = 0,
    WINDOW_LAUNCH_WINDOWED,
    WINDOW_LAUNCH_FULLSCREEN,
    WINDOW_LAUNCH_BORDERLESS,
} WindowLaunchMode;

typedef enum WindowKeyAction {
    WINDOW_KEY_ACTION_NONE = 0,
    WINDOW_KEY_ACTION_CYCLE_SIZE,
    WINDOW_KEY_ACTION_TOGGLE_FULLSCREEN,
} WindowKeyAction;

typedef struct WindowRuntimeState {
    volatile LONG busy;
    int command_line_parsed;
    WindowLaunchMode launch_mode;
    int launch_done;
    int launch_attempts;
    DWORD first_window_ms;
    DWORD next_launch_attempt_ms;
    DWORD suppress_resize_until_ms;
    DWORD resize_due_ms;
    unsigned int pending_key_actions;
    int pending_key_action_count;
    int pending_resize;
    int pending_maximize;
    int pending_w;
    int pending_h;
    int geometry_dirty;
    int last_window_w;
    int last_window_h;
    int last_drawable_w;
    int last_drawable_h;
    int last_display_index;
    unsigned int last_window_flags;
} WindowRuntimeState;

static WindowRuntimeState g_window_runtime;

// Tick-synchronous input scheduling (applied for an entire gameplay update).
static volatile uint32_t g_tick_input_mask[2] = { 0, 0 };
static volatile int g_tick_input_ticks[2] = { 0, 0 };
static volatile int g_tick_input_replace[2] = { 0, 0 };

// Command-bit overrides applied in the main_player_poll_cmds detour.
static volatile uint32_t g_input_override_mask[2] = { 0, 0 };
static volatile int g_input_override_frames[2] = { 0, 0 };
static volatile int g_input_override_replace[2] = { 0, 0 };

/* --- AI match flag (armed by the main-menu mode button, consumed by Lua bots) --- */
static volatile int g_ai_match_active = 0;
static volatile int g_ai_match_player = 1;
static volatile int g_ai_match_training = 0;

/* --- combat ledger --------------------------------------------------------
 * Authoritative combat counts from a detour on the engine's player_die
 * (0x422830) - the single native kill path. Goal dives ALSO route through
 * player_die (winning a swords map, scoring a point in eggnog/points modes),
 * so the detour classifies at the source by what the call itself changed:
 *   score incremented        -> scoring dive (scores[] event, never a death)
 *   end countdown flipped on -> match end (last_winner)
 *   neither                  -> a real death
 * Counters are monotonic; Lua consumers diff them. */
#define ADDR_PLAYER_DIE 0x422830u
typedef void (__cdecl *fn_player_die_t)(int);
static Detour g_player_die_detour;
static fn_player_die_t p_player_die_trampoline = NULL;
static volatile uint32_t g_ledger_deaths[2] = { 0, 0 };
static volatile uint32_t g_ledger_scores[2] = { 0, 0 };
static volatile uint32_t g_ledger_match_ends = 0;
static volatile int g_ledger_last_winner = -1;

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
static uint32_t hooks_apply_effective_overrides(uint32_t player_index, uint32_t cmd, int consume_poll_override);
static void hooks_finish_game_tick(void);
static void online_sent_challenge_add(const char* username, int id, int expires_in);
static void online_sent_challenge_remove(const char* username, int id);
static void online_sent_challenge_prune(void);
static int online_sent_challenge_pending(const char* username);
static void online_challenge_toast_show(const char* from, int id, int elo, int expires_in, const char* map_label);
static void online_challenge_toast_clear(int id, const char* from);
static void online_challenge_toast_render(void);
static void online_challenge_toast_tick(void);
static int online_challenge_toast_action_at(float x, float y);
static int online_challenge_toast_activate(int action);
static int online_result_has_live_rematch(void);
static void online_result_rematch_clear(const char* status);
static int online_result_rematch_activate(int action);
static void online_result_dismiss(int notify_server);

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
        /* Back is drawn as a footer below the panel; flank it there (swords
           dropped a touch more than the text so their tops clear the panel). */
        row_y = (L.list_bottom + (MODS_FOOTER_Y_OFF * L.ui))
              + (L.row_h * MODS_CURSOR_ROW_Y_FACTOR)
              + (MODS_BACK_SWORD_Y_OFF * L.ui);
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
static int g_online_force_main_return_once = 0;
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
static const char* console_stristr(const char* haystack, const char* needle);
static char* console_parse_token(char** inout_cursor);
static int console_find_mod_index_by_id(const char* id);
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
static void start_ggpo_net_host(uint16_t port, const char* source, int held_start);
static void start_ggpo_net_join(const char* host, uint16_t remote_port, uint16_t local_port, const char* source);
static void start_ggpo_net_join_deferred(uint16_t local_port, const char* source, int held_start);
static void stop_ggpo_net(const char* source);
static void online_hub_open(void);
static void online_hub_close_to_return_state(void);
static void online_hub_set_status(const char* msg);
static void online_clear_match_state(void);

/* --- Main-menu mode-cycling PLAY button ------------------------------------
 * Modes 0 (PLAY) and 1 (ONLINE) are framework built-ins; modes >= 2 map to
 * the lua_manager menu-mode registry (mod.game.register_menu_mode), registry
 * index (mode - 2). Selection persists by mode ID string in modframework.cfg. */
enum {
    MENU_MODE_PLAY = 0,
    MENU_MODE_ONLINE = 1,
    MENU_MODE_CUSTOM0 = 2
};
static int g_menu_mode = MENU_MODE_PLAY;
static int g_menu_mode_loaded = 0;
static char g_menu_mode_pending_id[32] = "";   /* persisted id awaiting registration */
static int __cdecl menu_mode_main_filter_proxy(void* btn, int event_code);
static int __cdecl menu_mode_arrow_up_filter_proxy(void* btn, int event_code);
static int __cdecl menu_mode_arrow_down_filter_proxy(void* btn, int event_code);
static void* online_find_button_by_action(uintptr_t action_ptr);
static void online_hub_load(void);
static void online_hub_save(void);
static void online_hub_apply_net_settings(void);
static void online_hub_rebuild_rows(void);
static void online_adjust_selected(int delta);
static void online_hub_render_ui(void);
static int online_advance_net_gameplay_tick(int arg0);
static int online_state_ticks_via_button_update(void* st);
static int online_state_is_ingame_menu(void* st);
static void online_server_update(void);
static void online_server_disconnect(const char* reason);
static void online_challenge_map_picker_clear(void);
static void online_handle_server_match_disconnect(const char* reason);
static void online_cancel_match_from_console(void);
static void online_match_pump_launch(void);
static void online_match_poll_completion(void);
static int online_native_winner_player(void);
static int online_native_finish_is_presenting(void);
static void online_abort_connect_timeout(void);
static void online_abort_prematch_setup(const char* reason);
static void online_connect_start_attempt(int first);
static void online_connect_retry_tick(void);
static void online_viewport_poll(const char* tag, int force);
static void online_viewport_end(void);
static const char* state_name_from_ptr(void* st);
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
    int launch_countdown_frames;
    int prematch_prepared;
    int prematch_released;
    int prematch_start_prepared;
    int server_start_reported;
    int server_committed;
    DWORD setup_started_ms;
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
    char p2p_auth_token[GGPO_NET_MATCH_TOKEN_HEX_BYTES + 1];
    char opponent[48];
    char map_key[128];
    char map_label[128];
} OnlinePendingMatch;

typedef struct OnlineConnectRetry {
    int attempts;              /* attempts started (1-based while connecting) */
    DWORD attempt_started_ms;
    int connected_once;        /* log/status latch for the current fresh socket */
    int established;           /* set only once synchronized gameplay begins */
    int match_id;
    char p2p_role[12];
    int local_port;            /* configured port for attempt 1; retries bind 0 */
    char peer_host[ONLINE_HUB_TEXT_MAX];
    int peer_port;
    char public_host[ONLINE_HUB_TEXT_MAX];
    int public_port;
    char lan_host[ONLINE_HUB_TEXT_MAX];
    int lan_port;
    char peer_route[32];
    char p2p_token[96];
    char p2p_auth_token[GGPO_NET_MATCH_TOKEN_HEX_BYTES + 1];
    int probe_cooldown;
    int probe_logged;
    int probe_warned;
} OnlineConnectRetry;

typedef struct OnlineActiveMatch {
    int active;
    int match_id;
    int local_player;
    int competitive;
    int queue_mode;
    int result_reported;
    int server_committed;
    int invalid_state_ticks;
    int winner_player;
    int awaiting_native_return;
    OnlineMatchResult result;
    char opponent[48];
    char map_label[128];
    char p2p_token[96];
    char completion_status[192];
} OnlineActiveMatch;

typedef struct OnlineResultToast {
    /* `active` retains the exact match identity while an asynchronous server
     * result is outstanding. `toast_visible` is deliberately separate: a
     * dismissed/expired provisional notification must not make the eventual
     * exact-ID confirmation look stale. */
    int active;
    int toast_visible;
    int match_id;
    int server_confirmed;
    int toast_age;
    int toast_lifetime;
    OnlineMatchResult result;
    int competitive;
    int elo_before;
    int elo_after;
    int elo_delta_valid;
    OnlineRematchState rematch_state;
    DWORD rematch_deadline_ms;
    int rematch_unranked;
    char opponent[48];
    char map_label[128];
    char status[ONLINE_HUB_STATUS_MAX];
} OnlineResultToast;

typedef struct OnlineViewportSnapshot {
    int valid;
    int window_w;
    int window_h;
    int drawable_w;
    int drawable_h;
    int viewport[4];
    int scissor[4];
    int scissor_enabled;
    int fullscreen;
    void* state_ptr;
    float mad_w;
    float mad_h;
    float game_w;
    float game_h;
    float camera_x;
    float camera_y;
} OnlineViewportSnapshot;

static OnlineHubConfig g_online_cfg;
static OnlineFriend g_online_friends[ONLINE_HUB_MAX_FRIENDS];
static int g_online_friend_count = 0;
static OnlineFriendRequest g_online_requests[ONLINE_HUB_MAX_INBOX];
static int g_online_request_count = 0;
static OnlineChallenge g_online_challenges[ONLINE_HUB_MAX_INBOX];
static int g_online_challenge_count = 0;
static OnlineChallengeMapPicker g_online_challenge_map_picker;
static int g_online_challenge_map_request_serial = 0;
static OnlineSentChallenge g_online_sent_challenges[ONLINE_HUB_MAX_INBOX];
static int g_online_sent_challenge_count = 0;
static int g_online_loaded = 0;
static OnlineHubTab g_online_tab = ONLINE_TAB_PLAY;
static OnlineHubRow g_online_rows[ONLINE_HUB_MAX_ROWS];
static int g_online_row_count = 0;
static int g_online_selected_row = -1;
static int g_online_scroll_row = 0;
static char g_online_status[ONLINE_HUB_STATUS_MAX];
/* Deferred hub status after a terminal connect path (1 opponent timeout,
 * 2 matchmaking disconnect, 3 explicit cancellation, 4 setup/sync failure). */
static int g_online_pending_connect_fail_status = 0;
static char g_online_pending_connect_fail_reason[ONLINE_HUB_STATUS_MAX];
static int g_online_capture_active = 0;
static OnlineHubCaptureKind g_online_capture_kind = ONLINE_CAPTURE_NONE;
static int g_online_capture_target = 0;
static char g_online_capture_buf[ONLINE_HUB_CAPTURE_MAX];
static int g_online_password_from_credential = 0;
static OnlineServerState g_online_server_state = ONLINE_SERVER_DISCONNECTED;
static int g_online_server_slot = -1;
static int g_online_authed = 0;
static int g_online_register_after_connect = 0;
static int g_online_auth_pending = 0;
static int g_online_server_info_pending = 0;
static DWORD g_online_server_deadline_ms = 0;
static DWORD g_online_server_heartbeat_deadline_ms = 0;
static int g_online_server_heartbeat_seq = 0;
static int g_online_server_heartbeat_acked_seq = 0;
static char g_online_recv_buf[65536];
static size_t g_online_recv_len = 0;
static int g_online_queue_mode = 0; /* 0 none, 1 casual, 2 competitive */
static int g_online_public_elo = 1000;
static int g_online_queue_casual_count = 0;
static int g_online_queue_competitive_count = 0;
static int g_online_friend_snapshot_complete = 0;
static float g_online_mouse_x = BASE_UI_W * 0.5f;
static float g_online_mouse_y = BASE_UI_H * 0.5f;
static int g_online_context_active = 0;
static int g_online_context_friend = -1;
static int g_online_context_selected = 0;
static float g_online_context_x = 0.0f;
static float g_online_context_y = 0.0f;
static int g_online_open_pending = 0;
static void* g_online_pending_return_state = (void*)(uintptr_t)ADDR_MAIN_STATE;
static OnlinePendingMatch g_online_pending_match;
static OnlineActiveMatch g_online_active_match;
static OnlineConnectRetry g_online_connect;
static OnlineResultToast g_online_result;

enum {
    ONLINE_LAUNCH_TIMEOUT_MS = 120000
};

typedef struct OnlineLaunchRuntime {
    int parsed;
    int hub_open_requested;
    int waiting_status_shown;
    DWORD deadline_ms;
    LaunchRequest request;
} OnlineLaunchRuntime;

static OnlineLaunchRuntime g_online_launch;
static OnlineChallengeToast g_online_challenge_toast;
static int g_online_result_toast_rendered_this_swap = 0;
static int g_online_challenge_toast_rendered_this_swap = 0;
static OnlineViewportSnapshot g_online_viewport;
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

static void online_clear_password_memory(void) {
    credential_ext_secure_zero(g_online_cfg.password, sizeof(g_online_cfg.password));
    g_online_password_from_credential = 0;
}

static void online_clear_capture_state(void) {
    credential_ext_secure_zero(g_online_capture_buf, sizeof(g_online_capture_buf));
    g_online_capture_active = 0;
    g_online_capture_kind = ONLINE_CAPTURE_NONE;
    g_online_capture_target = 0;
}

static void online_p2p_auth_tokens_clear(void) {
    SecureZeroMemory(g_online_pending_match.p2p_auth_token,
                     sizeof(g_online_pending_match.p2p_auth_token));
    SecureZeroMemory(g_online_connect.p2p_auth_token,
                     sizeof(g_online_connect.p2p_auth_token));
    ggpo_net_clear_match_token();
}

static void online_pending_match_reset(void) {
    SecureZeroMemory(g_online_pending_match.p2p_auth_token,
                     sizeof(g_online_pending_match.p2p_auth_token));
    memset(&g_online_pending_match, 0, sizeof(g_online_pending_match));
}

static void online_connect_reset(void) {
    online_viewport_end();
    online_p2p_auth_tokens_clear();
    memset(&g_online_connect, 0, sizeof(g_online_connect));
}

static int online_viewport_geometry_equal(const OnlineViewportSnapshot* a,
                                          const OnlineViewportSnapshot* b) {
    if (!a || !b || !a->valid || !b->valid) return 0;
    return a->window_w == b->window_w &&
           a->window_h == b->window_h &&
           a->drawable_w == b->drawable_w &&
           a->drawable_h == b->drawable_h &&
           a->viewport[0] == b->viewport[0] &&
           a->viewport[1] == b->viewport[1] &&
           a->viewport[2] == b->viewport[2] &&
           a->viewport[3] == b->viewport[3] &&
           a->scissor[0] == b->scissor[0] &&
           a->scissor[1] == b->scissor[1] &&
           a->scissor[2] == b->scissor[2] &&
           a->scissor[3] == b->scissor[3] &&
           a->scissor_enabled == b->scissor_enabled &&
           a->fullscreen == b->fullscreen &&
           a->state_ptr == b->state_ptr &&
           a->mad_w == b->mad_w &&
           a->mad_h == b->mad_h &&
           a->game_w == b->game_w &&
           a->game_h == b->game_h;
}

static void online_viewport_poll(const char* tag, int force) {
    typedef void (__cdecl *fn_get_size_t)(void*, int*, int*);
    OnlineViewportSnapshot now;
    memset(&now, 0, sizeof(now));
    now.valid = 1;
    now.window_w = -1;
    now.window_h = -1;
    now.drawable_w = -1;
    now.drawable_h = -1;
    if (g_proxy_sdl_window && p_SDL_GetWindowSize) {
        ((fn_get_size_t)p_SDL_GetWindowSize)(g_proxy_sdl_window, &now.window_w, &now.window_h);
    }
    if (g_proxy_sdl_window && p_SDL_GL_GetDrawableSize) {
        ((fn_get_size_t)p_SDL_GL_GetDrawableSize)(g_proxy_sdl_window,
                                                  &now.drawable_w,
                                                  &now.drawable_h);
    }
    glGetIntegerv(GL_VIEWPORT, now.viewport);
    glGetIntegerv(GL_SCISSOR_BOX, now.scissor);
    now.scissor_enabled = glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE ? 1 : 0;
    now.fullscreen = (p_main_is_fullscreen && p_main_is_fullscreen()) ? 1 : 0;
    now.state_ptr = p_state_current ? p_state_current() : NULL;
    now.mad_w = p_mad_w ? p_mad_w() : -1.0f;
    now.mad_h = p_mad_h ? p_mad_h() : -1.0f;
    now.game_w = g_game_w_native ? *g_game_w_native : -1.0f;
    now.game_h = g_game_h_native ? *g_game_h_native : -1.0f;
    now.camera_x = g_camera_x ? *g_camera_x : 0.0f;
    now.camera_y = g_camera_y ? *g_camera_y : 0.0f;

    if (!force && online_viewport_geometry_equal(&g_online_viewport, &now)) return;
    LOG_INFO("online.viewport[%s]: window=%dx%d drawable=%dx%d viewport=%d,%d %dx%d scissor=%s:%d,%d %dx%d mad=%.0fx%.0f game=%.1fx%.1f camera=%.2f,%.2f fullscreen=%d state=%s match=%d lp=%d frame=%u/%u",
             tag ? tag : (g_online_viewport.valid ? "changed" : "match-start"),
             now.window_w,
             now.window_h,
             now.drawable_w,
             now.drawable_h,
             now.viewport[0],
             now.viewport[1],
             now.viewport[2],
             now.viewport[3],
             now.scissor_enabled ? "on" : "off",
             now.scissor[0],
             now.scissor[1],
             now.scissor[2],
             now.scissor[3],
             now.mad_w,
             now.mad_h,
             now.game_w,
             now.game_h,
             now.camera_x,
             now.camera_y,
             now.fullscreen,
             state_name_from_ptr(now.state_ptr),
             g_online_active_match.active ? g_online_active_match.match_id
                                          : g_online_pending_match.match_id,
             ggpo_net_local_player(),
             (unsigned int)ggpo_net_frame_count(),
             (unsigned int)ggpo_net_remote_frame_count());
    g_online_viewport = now;
}

static void online_viewport_end(void) {
    if (!g_online_viewport.valid) return;
    online_viewport_poll("match-end", 1);
    memset(&g_online_viewport, 0, sizeof(g_online_viewport));
}

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

/* Snapshot/restore the persistent waterfall ambience around a rollback. The
 * waterfall fx handle (0x541E44), per-room count (0x542038) and the audio
 * voice-slot active flag (*(fx+0x2c)) are PROCESS-LOCAL audio state. A rollback
 * loads an older full-state blob and muted-replays frames; that mutates these
 * handles, so the next live frame drives the waterfall from a stale/cleared
 * voice slot -> the waterfall is heard in rooms with no waterfall. We snapshot
 * the live values before the rollback and restore them after, making the
 * rollback transparent to the ambience; the next live game_update re-derives the
 * correct waterfall from the current room's tiles (as vanilla always does).
 * Safe by construction: these fields are canonicalized OUT of the rollback
 * checksum, so this can neither cause nor hide a desync. */
static uintptr_t g_waterfall_saved_fx = 0u;
static int       g_waterfall_saved_count = 0;
static uint32_t  g_waterfall_saved_slot_flag = 0u;
static int       g_waterfall_saved_slot_valid = 0;
static int       g_waterfall_saved = 0;

void hooks_waterfall_audio_save(void) {
    g_waterfall_saved_fx = 0u;
    g_waterfall_saved_count = 0;
    g_waterfall_saved_slot_flag = 0u;
    g_waterfall_saved_slot_valid = 0;

    if (g_game_waterfall_fx && !IsBadReadPtr((const void*)g_game_waterfall_fx, sizeof(uintptr_t))) {
        g_waterfall_saved_fx = *g_game_waterfall_fx;
    }
    if (g_game_waterfall_count && !IsBadReadPtr((const void*)g_game_waterfall_count, sizeof(int))) {
        g_waterfall_saved_count = *g_game_waterfall_count;
    }
    if (g_waterfall_saved_fx && g_waterfall_saved_fx < UINTPTR_MAX - 0x2cu &&
        !IsBadReadPtr((const void*)(g_waterfall_saved_fx + 0x2cu), sizeof(uint32_t))) {
        g_waterfall_saved_slot_flag = *(volatile uint32_t*)(g_waterfall_saved_fx + 0x2cu);
        g_waterfall_saved_slot_valid = 1;
    }
    g_waterfall_saved = 1;
}

void hooks_waterfall_audio_restore(void) {
    if (!g_waterfall_saved) return;
    g_waterfall_saved = 0;

    if (g_game_waterfall_fx && !IsBadWritePtr((void*)g_game_waterfall_fx, (SIZE_T)sizeof(uintptr_t))) {
        *g_game_waterfall_fx = g_waterfall_saved_fx;
    }
    if (g_game_waterfall_count && !IsBadWritePtr((void*)g_game_waterfall_count, (SIZE_T)sizeof(int))) {
        *g_game_waterfall_count = g_waterfall_saved_count;
    }
    if (g_waterfall_saved_slot_valid && g_waterfall_saved_fx &&
        g_waterfall_saved_fx < UINTPTR_MAX - 0x2cu &&
        !IsBadWritePtr((void*)(g_waterfall_saved_fx + 0x2cu), (SIZE_T)sizeof(uint32_t))) {
        *(volatile uint32_t*)(g_waterfall_saved_fx + 0x2cu) = g_waterfall_saved_slot_flag;
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
    { HOOKS_PLAYER_COLOUR_SOLID, 0.20f, 0.22f, 0.25f, 1.0f, 0, 0, 0, 0, 0, 0 },       // charcoal
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

int hooks_player_colour_count(void) {
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

static void safe_copy_ellipsized(char* dst, size_t dst_sz,
                                 const char* src, size_t max_chars) {
    size_t len;
    size_t keep;
    if (!dst || dst_sz == 0) return;
    if (!src) src = "";
    if (max_chars >= dst_sz) max_chars = dst_sz - 1;
    len = strlen(src);
    if (len <= max_chars) {
        if (dst != src) safe_copy(dst, dst_sz, src);
        return;
    }
    if (max_chars < 4) {
        keep = max_chars;
        if (dst != src) memmove(dst, src, keep);
        dst[keep] = '\0';
        return;
    }
    keep = max_chars - 3;
    if (dst != src) memmove(dst, src, keep);
    dst[keep] = '.';
    dst[keep + 1] = '.';
    dst[keep + 2] = '.';
    dst[keep + 3] = '\0';
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
/* Armed the first time rngtrace turns on (begin called); gates the out-of-tick
 * draw logger below so normal play (rngtrace off) is never touched. */
static volatile int g_hooks_rng_trace_enabled = 0;

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
    g_hooks_rng_trace_enabled = 1;
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

/* Out-of-tick (render-time) gameplay-seed draw logger. Draws that happen between
 * the per-tick trace windows (e.g. particle DRAW callbacks during render) still
 * advance the shared seed; render runs at a NON-synced rate, so any such draw can
 * drift the seed between peers (the last leak diffed to the gap between ticks).
 * We log each unique caller once so the cosmetic ones can be isolated. */
static uintptr_t g_hooks_oot_seen[64];
static uint32_t  g_hooks_oot_seen_count = 0;
static void hooks_rng_oot_note(uint32_t kind, uintptr_t caller) {
    uint32_t i;
    for (i = 0; i < g_hooks_oot_seen_count; i++) {
        if (g_hooks_oot_seen[i] == caller) return;
    }
    if (g_hooks_oot_seen_count < 64u) g_hooks_oot_seen[g_hooks_oot_seen_count++] = caller;
    LOG_INFO("ggpo.rngtrace OUT-OF-TICK gameplay-seed draw kind=%u caller=%08X",
             (unsigned int)kind, (unsigned int)caller);
}

void __cdecl hooks_rng_trace_record_from_hook(uint32_t kind, uintptr_t caller) {
    uint32_t index;
    if (!g_hooks_rng_trace_active) {
        if (g_hooks_rng_trace_enabled) hooks_rng_oot_note(kind, caller);
        return;
    }
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
typedef int         (__cdecl *fn_onein_t)(int);

static uint32_t g_cosmetic_rng_seed = 0x1ED37A11u;
static volatile int g_cosmetic_rng_depth = 0;
/* Declared here (used by hooked_sound_sword_ching below) so the rng-hook trace
 * skips can also exclude draws isolated via the audio-seed swap - otherwise
 * sound_sword_ching's frnd shows up as a false-positive gameplay-seed draw. */
static volatile int g_audio_rng_wrap_depth = 0;

/*
 * Audio-thread RNG isolation (desync fix).
 *
 * The native synth's per-voice DSP callbacks run on SDL's AUDIO thread
 * (mixaudio -> synth_effects_update -> effect callbacks) and draw frnd/rnd/
 * rnd5050, which read+write the SHARED gameplay seed _mrand_seed. That advances
 * the gameplay RNG a non-deterministic number of times (it depends on the live
 * voice count and races the sim thread), so the two peers' seeds drift and
 * gameplay draws (e.g. dropped-sword angle on a kill) diverge -> desync on
 * death/respawn/teleport.
 *
 * Fix: the audio thread must NEVER touch _mrand_seed. We record the sim thread's
 * id and, for any RNG draw NOT on the sim thread, serve it from a private audio
 * seed that reproduces the native mad LCG bit-for-bit (so audio stays identical)
 * without ever reading/writing _mrand_seed. Lock-free: the gameplay seed is
 * touched only by the sim thread, the audio seed only by the audio thread.
 */
static volatile DWORD g_sim_thread_id = 0;
static uint32_t       g_audio_thread_rng_seed = 0x2545F491u;

static int hooks_on_audio_thread(void) {
    DWORD sim = g_sim_thread_id;
    return sim != 0 && GetCurrentThreadId() != sim;
}

/* One step of the native mad RNG LCG (matches mrand/rnd/frnd @0x405080..0x405224)
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
     * Sim-thread cosmetic SOUND draws gated by NON-synced state (wall-clock
     * _mad_ticks debounce timers). They run a different number of times on each
     * peer, so on the shared gameplay seed they drift it. Disasm-verified that
     * these ranges' rnd/frnd results feed only sound synthesis. Camera shake is
     * deliberately NOT in this list: game_update_camera commits its jitter to
     * camera X/Y, and native simulation reads prior-tick camera X in the loser-
     * respawn branch. Shake therefore consumes the rollback-owned gameplay RNG.
     */
    if (c >= 0x004224E0u && c < 0x004227C0u) return 1; /* _do_cheer   (cheer-on-kill sound)   */
    if (c >= 0x0041EF40u && c < 0x0041EFB0u) return 1; /* _do_whistle (whistle sound)         */
    if (c >= 0x0041F060u && c < 0x0041F110u) return 1; /* _slide_sound (sliding-sound pitch)   */
    if (c >= 0x004240FFu && c < 0x00424194u) return 1; /* _sound_creepy (ambience pitch)      */
    /* Cosmetic particle-spawn loop INSIDE player_update_movement (a mixed gameplay
     * fn, so isolated per sub-range, NOT whole). The frnds @0x423EA6/0x423F1A/
     * 0x423F38/0x423F59/0x423F71 all write the spawned particle (disasm: esi = the
     * particle_effect_sprite return @0x423EED, fstp [esi+0xc]/[esi+0x2c]/[esi+0x44]
     * = particle velocity/lifetime), never the player struct. The loop's iteration
     * COUNT is non-synced (cosmetic), so on the gameplay seed it drifted it ->
     * desync with the RNG stream otherwise in sync. The gameplay draws in this fn
     * (landing-bounce rnd @0x424380 feeding player Y, ~0x424300 block) are ABOVE
     * this range and stay on the gameplay seed. (Found via both-peer rngtrace diff:
     * frame 2033 same seed, p1 drew n=164 vs p0 n=163 in this 0x423F.. loop.) */
    if (c >= 0x00423E90u && c < 0x00423F90u) return 1; /* player_update_movement trail particles */
    /* _dust_particle: per-particle update for player skid/landing dust. The dust
     * lives in a NON-checksummed cosmetic particle buffer, so the number of live
     * dust particles legitimately differs between peers (different camera / spawn
     * timing). Each one drew frnd+rndsign from the gameplay seed, so that varying
     * count drifted the seed -> "changed=things" desyncs that surface on
     * respawns/landings. Disasm-verified the range only calls frnd/rndsign/
     * room_particle + map reads (map_coord_tile), never map_tile/thing writes.
     * (Found via rngtrace diff: p0 ran 3x 3@0041DEDB 5@0041DEE4, p1 ran 2x.) */
    if (c >= 0x0041DEB0u && c < 0x0041DF90u) return 1; /* _dust_particle (skid dust)          */
    /* _drip_particle: per-particle update for water drips (the registered drip
     * update cb, sibling of _dust_particle). Like dust, drips live in a
     * NON-checksummed cosmetic buffer so the live count differs between peers
     * (camera/timing), and each drip drew rnd5050+2x frnd from the gameplay seed
     * for splash/pip variation -> that varying count drifted the seed ->
     * "changed=things" desyncs. Disasm-verified [0x41F970,0x41FB50) only calls
     * room_particle + sound_pip + map READS (map_coord_tile/map_pixels_h), never
     * map_tile/thing writes. (Found via rngtrace diff: frame 65 same seed, p1 ran
     * an extra 4@0041FA23 3@0041FB15 3@0041FB38 that p0 ran on a different frame.) */
    if (c >= 0x0041F970u && c < 0x0041FB50u) return 1; /* _drip_particle (water drips)        */
    /* _bug_particle: per-particle update for ambient flying insects (cosmetic
     * decoration, sibling of dust/drip). NB: its frnd draw @0x41EBCB was for a
     * LONG time misattributed to "reset_room" (reset_room is actually the tiny fn
     * @0x41E990; 0x41EBCB is inside _bug_particle @0x41EA40). The "reset_room /
     * room-flip" desync hunt was chasing THIS cosmetic leak all along. Bugs live
     * in a NON-checksummed buffer so their count differs between peers (camera/
     * timing); the per-bug frnd drifted the gameplay seed -> downstream the
     * gameplay tile-deco draws (0x42CA16/0x42CA2C feeding map_tile) then produced
     * different tiles -> "changed=tilemap" desyncs. Disasm-verified [0x41EA40,
     * 0x41ECC0) only calls frnd + math (sign/normalize/calc_angle/pow) +
     * room_particle + map READS (map_pixels_h/tile_h/map_coord_tile) +
     * tile_vert_lighting (render), never map_tile/thing/player writes. (Found via
     * rngtrace diff: frame 587 same seed, p0 ran extra 3@0041EBCB that p1 didn't.) */
    if (c >= 0x0041EA40u && c < 0x0041ECC0u) return 1; /* _bug_particle (flying insects)      */
    /* The rest of the cosmetic particle-effect subsystem. Each draws frnd/rnd/
     * rnd5050 to position/spread particles whose COUNT is camera/timing-gated
     * (non-synced), so on the gameplay seed they drift it -> "changed=things"
     * desyncs that surface in combat. Disasm-verified each range only calls the
     * cosmetic particle spawners (particle_effect_sprite / room_particle) + map
     * READS, never thing_new / map_tile writes, and these particles are NOT in
     * the checksummed particle pool (it still matched after isolating dust).
     * Ranges are bounded to EXCLUDE the gameplay functions sitting between them
     * (is_tip_crossed_hand, update_fistfight_points, try_throw_sword, etc.). */
    if (c >= 0x0041D200u && c < 0x0041DD80u) return 1; /* _room_particle + _game_particle_blood */
    if (c >= 0x0041E130u && c < 0x0041E320u) return 1; /* _sparks_ex                          */
    if (c >= 0x0041F330u && c < 0x0041F420u) return 1; /* _sword_sparks_ex                    */
    if (c >= 0x0041FCB0u && c < 0x0041FD30u) return 1; /* _debug_particle + _game_particle    */
    /* Inline cosmetic particle/sound effect block inside the player-update fn
     * (player_is_win_condition.part.38). Disasm-verified this sub-range only
     * calls particle_effect_sprite + sound_fm/sound_pulse (+ frnd/math), no
     * thing_new/map_tile, so the frnd draws here (0x41F661/0x41F83B/0x41F8C6/
     * 0x41F934) are movement particles/sound, not gameplay. The surrounding
     * gameplay draws are outside this sub-range and stay on the gameplay seed. */
    if (c >= 0x0041F640u && c < 0x0041F940u) return 1; /* player-update cosmetic fx block      */
    /* The two biggest inline cosmetic-effect families, found via frame-0 rngtrace
     * diff: at the FIRST divergence one peer drew an extra `onein` (the gate of a
     * cosmetic spawn) on the gameplay seed because the spawner runs a non-synced
     * number of times (camera-derived visible range / cosmetic anim), even though
     * seed+ticks+room all matched. These two whole functions' RNG draws are ALL
     * cosmetic (disasm + decompiler verified: every rnd/frnd/onein/mrand result
     * feeds particle_effect_sprite / sparks_ex / sound objects, NEVER the player
     * struct, map_tile or thing_new - the interleaved gameplay calls player_die/
     * poll_cmds/sword_update_movement are CALLS whose own draws live elsewhere).
     * Combined with the onein detour (below) this routes the gate AND the body of
     * each effect to the private seed, so a non-synced spawn count can no longer
     * drift the gameplay seed. (player_update_logic 0x42a8e0..0x42b770;
     * game_update ambient-decoration spawner drips/dust/bats/bubbles/leaves
     * 0x42d970..0x42f3a0 - bounded below the tilemap-deco rnd @0x42ca11/0x42ca27
     * which MUST stay on the gameplay seed, and above at 0x42f3a0 where the
     * cosmetic cases end and cursor/win-condition gameplay resumes (0x42f3ac
     * main_cursors_disable). All draws in between feed sound_noise /
     * particle_effect_sprite / water-colour particles only. */
    if (c >= 0x0042A8E0u && c < 0x0042B770u) return 1; /* _player_update_logic (sparks/dust)  */
    if (c >= 0x0042D970u && c < 0x0042F3A0u) return 1; /* _game_update ambient decoration      */
    switch (c) {
        /* Cosmetic RNG draws inside game_update (a mixed gameplay/cosmetic fn,
         * so isolated per call site, not by range). Crowd cheer colour + the
         * waterfall sound pitch/volume stored into the _fx_15991 sound object
         * (ADDR_WATERFALL_FX 0x541E44 +0x3c/+0x44), verified @0x42d185/0x42d206/
         * 0x42d242. NB: 0x42CA16/0x42CA2C were previously (mis)listed here as
         * "crowd ambient sound pan" but disasm shows they are rnd() args feeding
         * map_tile @0x42ca33 = GAMEPLAY tile-deco - they MUST stay on the gameplay
         * seed (both peers compute the same tiles), so they are deliberately NOT
         * isolated. Isolating them leaked the (non-synced) cosmetic seed into the
         * checksummed tilemap. */
        case 0x0042C801u: /* frnd(0,2)   - crowd cheer colour trigger */
        /* The presentation-only F00/crowd/chant state is rollback-masked.
         * Whether the near-win chant starts can therefore differ on replay;
         * this draw writes only the chant sound object's pitch and must not
         * advance the rollback-owned gameplay seed. */
        case 0x0042C9B8u: /* frnd(.9,1)  - near-win chant sound pitch */
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
static int __cdecl hooked_onein(int n) { (void)n; return 0; }
#else
/* C (not naked) so it can take the audio-thread branch like rnd/frnd/rnd5050. */
static uint32_t __cdecl hooked_mrand(void) {
    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    int cosmetic;
    uint32_t result;
    if (hooks_on_audio_thread()) return hooks_audio_rng_step() >> 16;
    /* mrand DOES have cosmetic callers now (ambient decoration @0x42db8a/0x42dbb0,
     * player sparks, case-8/9 particle seeds) - isolate them like rnd/frnd. */
    cosmetic = hooks_rng_caller_is_cosmetic(caller);
    if (!cosmetic && g_cosmetic_rng_depth == 0 && g_audio_rng_wrap_depth == 0) hooks_rng_trace_record_from_hook(1u, caller);
    if (!g_hooks_rng_mrand_trampoline) return 0;
    if (g_native_mrand_seed && g_cosmetic_rng_depth == 0 && cosmetic) {
        uint32_t game_seed = *g_native_mrand_seed;
        g_cosmetic_rng_depth++;
        *g_native_mrand_seed = g_cosmetic_rng_seed;
        result = ((uint32_t (__cdecl*)(void))g_hooks_rng_mrand_trampoline)();
        g_cosmetic_rng_seed = *g_native_mrand_seed;
        *g_native_mrand_seed = game_seed;
        g_cosmetic_rng_depth--;
        return result;
    }
    return ((uint32_t (__cdecl*)(void))g_hooks_rng_mrand_trampoline)();
}

/*
 * rnd/frnd/rnd5050 are C wrappers (not naked tail-jumps) so that, for the cosmetic
 * call sites in hooks_rng_caller_is_cosmetic, we can swap the gameplay seed for a
 * private cosmetic seed around the real draw. Reached via the detour's jmp, so
 * __builtin_return_address(0) is the original caller (the draw site). The trace
 * records only NON-cosmetic draws so a desync dump isn't flooded by cosmetic spray.
 * Non-cosmetic callers pass straight through unchanged. Same isolation pattern as
 * hooked_sound_sword_ching, and as mrand/rndsign/onein (which gained cosmetic
 * callers once the ambient-decoration + player-spark ranges were isolated).
 */
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
     * cosmetic spray; cosmetic draws no longer touch the gameplay seed anyway.
     * Also skip when depth>0 (we're inside a cosmetic seed-swap, e.g. onein's
     * nested rnd) so the trace shows only true gameplay-seed draws. */
    if (!cosmetic && g_cosmetic_rng_depth == 0 && g_audio_rng_wrap_depth == 0) hooks_rng_trace_record_from_hook(2u, caller);
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
    if (!cosmetic && g_cosmetic_rng_depth == 0 && g_audio_rng_wrap_depth == 0) hooks_rng_trace_record_from_hook(3u, caller);
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
    if (!cosmetic && g_cosmetic_rng_depth == 0 && g_audio_rng_wrap_depth == 0) hooks_rng_trace_record_from_hook(4u, caller);
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

/* C (not naked) so it can take the audio-thread branch. */
static long double __cdecl hooked_rndsign(void) {
    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    int cosmetic;
    long double result;
    if (hooks_on_audio_thread()) {
        /* rndsign(): (seed & 0x10000) ? -1 : 1, matching rndsign @0x4052c0 */
        uint32_t s = hooks_audio_rng_step();
        return (s & 0x10000u) ? -1.0L : 1.0L;
    }
    /* rndsign also has cosmetic callers now (ambient case-7 bats, player sparks). */
    cosmetic = hooks_rng_caller_is_cosmetic(caller);
    if (!cosmetic && g_cosmetic_rng_depth == 0 && g_audio_rng_wrap_depth == 0) hooks_rng_trace_record_from_hook(5u, caller);
    if (!g_hooks_rng_rndsign_trampoline) return 0.0L;
    if (g_native_mrand_seed && g_cosmetic_rng_depth == 0 && cosmetic) {
        uint32_t game_seed = *g_native_mrand_seed;
        g_cosmetic_rng_depth++;
        *g_native_mrand_seed = g_cosmetic_rng_seed;
        result = ((long double (__cdecl*)(void))g_hooks_rng_rndsign_trampoline)();
        g_cosmetic_rng_seed = *g_native_mrand_seed;
        *g_native_mrand_seed = game_seed;
        g_cosmetic_rng_depth--;
        return result;
    }
    return ((long double (__cdecl*)(void))g_hooks_rng_rndsign_trampoline)();
}

/*
 * onein(n) = (rnd(1,|n|) == 1). The shared 1-in-N gate helper: it draws ONE rnd
 * from the gameplay seed, and is called by BOTH gameplay (mapgen) and cosmetic
 * code (ambient-decoration spawner, player sparks, blood). Range isolation can't
 * catch it because the rnd it draws always reports caller 0x405419 (inside onein).
 * So we detour onein itself and classify by its REAL caller: cosmetic callers run
 * onein with the gameplay seed swapped out for the private cosmetic seed (same
 * synchronous sim-thread swap as the rnd/frnd hooks; depth>0 makes the nested rnd
 * reuse the swapped seed without re-swapping or tracing). Gameplay callers pass
 * straight through. This is what stops the frame-143 "extra onein" from drifting
 * the gameplay seed when one peer's cosmetic spawner runs an extra time.
 */
static int __cdecl hooked_onein(int n) {
    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    fn_onein_t real = (fn_onein_t)g_hooks_onein_trampoline;
    int cosmetic;
    int result;
    if (!real) return 0;
    cosmetic = hooks_rng_caller_is_cosmetic(caller);
    if (g_native_mrand_seed && g_cosmetic_rng_depth == 0 && cosmetic) {
        uint32_t game_seed = *g_native_mrand_seed;
        g_cosmetic_rng_depth++;
        *g_native_mrand_seed = g_cosmetic_rng_seed;
        result = real(n);
        g_cosmetic_rng_seed = *g_native_mrand_seed;
        *g_native_mrand_seed = game_seed;
        g_cosmetic_rng_depth--;
        return result;
    }
    return real(n);
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
/* g_audio_rng_wrap_depth moved up near g_cosmetic_rng_depth (used by the trace skips). */

/* These synth voice callbacks run on the SDL audio thread. Their RNG draws are
 * now isolated by the audio-thread branch in the RNG hooks (served from the
 * private audio seed, never touching _mrand_seed), so this is a straight
 * pass-through. It used to swap the gameplay seed in/out around the callback,
 * but doing that FROM the audio thread was exactly the race that desynced
 * matches on death/respawn/teleport. */
static int hooks_call_synth_callback_with_audio_rng(fn_synth_callback_t callback, void* effect) {
    if (!callback) return 0;
    return callback(effect);
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

static const char* state_name_from_ptr(void* st) {
    HookCustomState* custom;
    if (!st) return "none";
    if (st == (void*)&g_console_state) return "console";
    if (st == (void*)&g_mods_state) return "mods";
    if (st == (void*)&g_mods_entry_state) return "mods_entry";
    if (st == (void*)&g_online_hub_state) return "online_hub";
    custom = find_custom_state_by_ptr(st);
    if (custom) return custom->name;
    if (st == (void*)(uintptr_t)ADDR_MAIN_STATE) return "main";
    if (st == (void*)(uintptr_t)ADDR_MAIN_STATE_INITIAL) return "main_initial";
    if (st == (void*)(uintptr_t)ADDR_GAME_STATE) return "game";
    if (st == (void*)(uintptr_t)ADDR_OPTIONS_STATE) return "options";
    if (st == (void*)(uintptr_t)ADDR_OPTIONS_STATE_PAUSED) return "options_paused";
    if (st == (void*)(uintptr_t)ADDR_REMAP_STATE1) return "remap1";
    if (st == (void*)(uintptr_t)ADDR_REMAP_STATE2) return "remap2";
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
/* The mods list ends with a divider + a "Back" action. Back is rendered as a
 * footer BELOW the panel (not as a scrolling row), so the scroll/selection math
 * works over just the content rows. Returns the count of scrollable content rows. */
static int mods_content_row_count(void) {
    int n = g_row_count;
    if (n > 0 && g_rows[n - 1].kind == ROW_BACK) {
        n--;                                                   /* drop Back */
        if (n > 0 && g_rows[n - 1].kind == ROW_DIVIDER) n--;   /* and its divider */
    }
    return n;
}

static int mods_back_row_index(void) {
    if (g_row_count > 0 && g_rows[g_row_count - 1].kind == ROW_BACK) return g_row_count - 1;
    return -1;
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
    int content = mods_content_row_count();
    int max_scroll = (content > cap) ? (content - cap) : 0;
    int margin = (cap >= 8) ? 2 : 1;

    if (g_selected_row < 0 || g_selected_row >= g_row_count) {
        g_scroll_row = clampi(g_scroll_row, 0, max_scroll);
        return;
    }

    /* "Back" is a footer below the panel, not a scrolling row - just show the
       bottom of the content when it's selected. */
    if (g_selected_row >= content) {
        g_scroll_row = max_scroll;
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
    } else if (type == LUA_CFG_OPTIONS) {
        ok = lua_manager_config_set_option(idx, cfg_idx, value);
    }
    return ok;
}

static void rebuild_rows(void) {
    RowKey keep = selected_key();
    UpdateStatus update_status;
    const char* update_line;
    const char* latest_version;
    char update_line_display[64];
    char latest_display[40];
    char update_action[192];
    rows_clear();

    // Framework settings (apply to the mod framework itself, not a specific mod).
    rows_add(ROW_INFO, 0, -1, -1, "Framework", "");
    rows_add(ROW_INFO, 0, -1, -1, "  Framework version", FRAMEWORK_VERSION);
    rows_add(ROW_FW_TOGGLE, 1, -1, FW_SETTING_LOG_CONSOLE,
             "  Enable log console", log_console_visible() ? "ON" : "OFF");
    rows_add(ROW_FW_TOGGLE, 1, -1, FW_SETTING_AUTO_UPDATE,
             "  Automatic updates", update_ext_auto() ? "ON" : "OFF");
    rows_add(ROW_FW_TOGGLE, 1, -1, FW_SETTING_DISCORD_PRESENCE,
             "  Discord Rich Presence",
             discord_rpc_ext_available()
                 ? (discord_rpc_ext_enabled() ? "ON" : "OFF")
                 : "UNAVAILABLE");

    update_status = update_ext_status();
    update_line = update_ext_status_line();
    latest_version = update_ext_latest_version();
    if (!update_line || !update_line[0]) {
        update_line = (update_status == UPDATE_IDLE) ? "not checked yet" : "working...";
    }
    safe_copy_ellipsized(update_line_display, sizeof(update_line_display), update_line, 42);
    safe_copy_ellipsized(latest_display, sizeof(latest_display), latest_version, 28);
    rows_add(ROW_INFO, 0, -1, -1, "  Update status", update_line_display);
    if (update_status == UPDATE_AVAILABLE) {
        snprintf(update_action, sizeof(update_action), "Install %s",
                 latest_display[0] ? latest_display : "update");
        rows_add(ROW_FW_ACTION, 1, -1, FW_ACTION_UPDATE,
                 "  Install update", update_action);
    } else if (update_status == UPDATE_IDLE || update_status == UPDATE_UP_TO_DATE ||
               update_status == UPDATE_ERROR) {
        rows_add(ROW_FW_ACTION, 1, -1, FW_ACTION_UPDATE,
                 (update_status == UPDATE_ERROR) ? "  Retry update check" : "  Check for updates",
                 "[Check]");
    } else if (update_status == UPDATE_RESTART_PENDING) {
        rows_add(ROW_INFO, 0, -1, -1,
                 "  Restart required", "Exit and relaunch the game");
    }
    rows_add(ROW_DIVIDER, 0, -1, -1, "", "");

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
        const char* name = lua_manager_get_mod_name(mi);
        const char* id = lua_manager_get_mod_id(mi);
        const char* author = lua_manager_get_mod_author(mi);
        const char* desc = lua_manager_get_mod_description(mi);
        int enabled = lua_manager_get_mod_enabled(mi);
        int suspended = lua_manager_get_mod_suspended(mi);
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
        if (suspended) {
            snprintf(header_right, sizeof(header_right), "SUSPENDED (ONLINE)");
        } else if (error_count > 0) {
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
            rows_add(ROW_MOD_TOGGLE, suspended ? 0 : 1, mi, -1, "  Enabled",
                     suspended ? "SUSPENDED" : (enabled ? "ON" : "OFF"));

            if (suspended) {
                snprintf(warn, sizeof(warn), "  Online safety: %s",
                         lua_manager_get_mod_suspend_reason(mi)[0]
                             ? lua_manager_get_mod_suspend_reason(mi)
                             : "gameplay-affecting mod");
                rows_add(ROW_INFO, 0, mi, -1, warn, "");
            }

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
                        case LUA_CFG_OPTIONS: snprintf(value, sizeof(value), "< %s >", raw_value); break;
                        default: safe_copy(value, sizeof(value), raw_value); break;
                    }
                    rows_add(ROW_CONFIG, suspended ? 0 : 1, mi, ci, label, value);
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
                    rows_add(ROW_BIND, suspended ? 0 : 1, mi, bi, label, value);
                }
            }
        }

        if (mi != mod_count - 1) rows_add(ROW_DIVIDER, 0, -1, -1, "", "");
    }

    rows_add(ROW_DIVIDER, 0, -1, -1, "", "");
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
    } else if (row->kind == ROW_MOD_TOGGLE) {
        if (lua_manager_get_mod_suspended(row->mod_index)) return;
        int enabled = lua_manager_get_mod_enabled(row->mod_index);
        lua_manager_set_mod_enabled(row->mod_index, delta > 0 ? 1 : (delta < 0 ? 0 : !enabled));
    } else if (row->kind == ROW_CONFIG) {
        if (lua_manager_get_mod_suspended(row->mod_index)) return;
        int type = lua_manager_get_mod_config_type(row->mod_index, row->cfg_index);
        if (type == LUA_CFG_BOOL) {
            lua_manager_config_toggle_bool(row->mod_index, row->cfg_index);
        } else if (type == LUA_CFG_INT) {
            lua_manager_config_increment_int(row->mod_index, row->cfg_index, delta);
        } else if (type == LUA_CFG_FLOAT) {
            lua_manager_config_increment_float(row->mod_index, row->cfg_index, (double)delta * 0.1);
        } else if (type == LUA_CFG_OPTIONS) {
            lua_manager_config_cycle_option(row->mod_index, row->cfg_index, (delta < 0) ? -1 : 1);
        }
    } else if (row->kind == ROW_FW_TOGGLE) {
        if (row->cfg_index == FW_SETTING_LOG_CONSOLE) {
            log_set_console_visible(delta > 0 ? 1 : (delta < 0 ? 0 : !log_console_visible()));
        } else if (row->cfg_index == FW_SETTING_AUTO_UPDATE) {
            update_ext_set_auto(delta > 0 ? 1 : (delta < 0 ? 0 : !update_ext_auto()));
        } else if (row->cfg_index == FW_SETTING_DISCORD_PRESENCE &&
                   discord_rpc_ext_available()) {
            int enabled = delta > 0 ? 1 :
                          (delta < 0 ? 0 : !discord_rpc_ext_enabled());
            if (update_ext_config_set("discord_presence",
                                      enabled ? "1" : "0")) {
                discord_rpc_ext_set_enabled(enabled);
            } else {
                LOG_WARN("[discord] could not persist Rich Presence setting");
            }
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

    if (row->kind == ROW_MOD_HEADER) {
        mods_toggle_collapsed(row->mod_index);
    } else if (row->kind == ROW_MOD_TOGGLE) {
        if (lua_manager_get_mod_suspended(row->mod_index)) return;
        lua_manager_set_mod_enabled(row->mod_index, lua_manager_get_mod_enabled(row->mod_index) ? 0 : 1);
    } else if (row->kind == ROW_BIND) {
        if (lua_manager_get_mod_suspended(row->mod_index)) return;
        begin_bind_capture(row->mod_index, row->cfg_index);
    } else if (row->kind == ROW_CONFIG) {
        if (lua_manager_get_mod_suspended(row->mod_index)) return;
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
        } else if (type == LUA_CFG_OPTIONS) {
            lua_manager_config_cycle_option(row->mod_index, row->cfg_index, 1);
        }
    } else if (row->kind == ROW_FW_TOGGLE) {
        if (row->cfg_index == FW_SETTING_LOG_CONSOLE) {
            log_set_console_visible(!log_console_visible());
        } else if (row->cfg_index == FW_SETTING_AUTO_UPDATE) {
            update_ext_set_auto(!update_ext_auto());
        } else if (row->cfg_index == FW_SETTING_DISCORD_PRESENCE &&
                   discord_rpc_ext_available()) {
            int enabled = !discord_rpc_ext_enabled();
            if (update_ext_config_set("discord_presence",
                                      enabled ? "1" : "0")) {
                discord_rpc_ext_set_enabled(enabled);
            } else {
                LOG_WARN("[discord] could not persist Rich Presence setting");
            }
        }
    } else if (row->kind == ROW_FW_ACTION && row->cfg_index == FW_ACTION_UPDATE) {
        UpdateStatus status = update_ext_status();
        if (status == UPDATE_AVAILABLE) {
            update_ext_begin_apply();
        } else if (status == UPDATE_IDLE || status == UPDATE_UP_TO_DATE ||
                   status == UPDATE_ERROR) {
            update_ext_begin_check();
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
    "help", "commands", "clear", "history", "echo", "find", "console.find", "console.stats", "console.copy",
    "state", "state.last", "state.return", "state.switch", "sys.info", "ui.size",
    "time.scale", "framework.api", "discord.app",
    "music.status", "music.scan", "music.rescan", "music.play",
    "music.output_rate",
    "mods.count", "mods.list", "mods.find", "mods.info", "mods.trace", "mods.enable", "mods.disable", "mods.toggle",
    "mods.config", "mods.config.find", "mods.config.get", "mods.config.set", "mods.config.action",
    "binds.list", "binds.find", "binds.set", "binds.clear",
    "reload.mods", "mods.reload", "reload.assets",
    "online.hub",
    "net.diag",
    "ggpo.net",
    "log.level", "log.tail", "input.show", "input.override", "input.clear",
    "lua", "eval", "lua.mod", "eval.mod", "lua.file", "exit", "quit",
    "dev",
};

/* Developer mode: hidden until enabled via the console `dev on` command. The
 * commands below (netcode debug harness incl. rngtrace, forced state switches,
 * input injection, arbitrary Lua/eval, raw log tail) are diagnostic/power tools
 * that a normal build shouldn't expose - unknown-command'd unless dev mode is on,
 * and omitted from `commands`/autocomplete. */
static int g_developer_mode = 0;
static const char* k_console_dev_commands[] = {
    "ggpo.net", "ggpo.roundtrip", "ggpo.selftest", "ggpo.local",
    "state.switch", "input.show", "input.override", "input.clear",
    "lua", "eval", "lua.mod", "eval.mod", "lua.file", "log.tail",
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

#define CONSOLE_CAND_MAX 128
#define CONSOLE_CAND_LEN 96

static int console_cmd_in_list(const char* cmd, const char* const* list, int count) {
    for (int i = 0; i < count; i++) if (_stricmp(cmd, list[i]) == 0) return 1;
    return 0;
}

static int console_cmd_is_developer(const char* cmd) {
    return console_cmd_in_list(cmd, k_console_dev_commands,
                               (int)(sizeof(k_console_dev_commands) / sizeof(k_console_dev_commands[0])));
}

/* Build argument-completion candidates for `cmd` at argument position
 * `arg_index` (1 = first arg). arg1/arg2 are earlier args, for context. */
static int console_build_arg_candidates(char cands[][CONSOLE_CAND_LEN], int max,
                                        const char* cmd, int arg_index,
                                        const char* arg1, const char* arg2) {
    int n = 0;
    #define CAND_ADD(s) do { const char* _s = (s); if (n < max && _s && _s[0]) { safe_copy(cands[n], CONSOLE_CAND_LEN, _s); n++; } } while (0)

    static const char* const mod_id_cmds[] = {
        "mods.info","mods.enable","mods.disable","mods.toggle","mods.trace",
        "mods.config","mods.cfg","mods.config.get","mods.cfg.get",
        "mods.config.set","mods.cfg.set","mods.config.action","mods.cfg.action",
        "binds.list","binds.set","binds.clear","lua.mod","eval.mod"
    };
    static const char* const cfg_key_cmds[] = {
        "mods.config.get","mods.cfg.get","mods.config.set","mods.cfg.set",
        "mods.config.action","mods.cfg.action"
    };
    static const char* const bind_key_cmds[] = { "binds.set", "binds.clear" };
    static const char* const all_cmds[] = { "mods.enable", "mods.disable", "mods.toggle", "mods.trace" };

    if (arg_index == 1 && _stricmp(cmd, "state.switch") == 0) {
        static const char* const states[] = {"main","main_initial","options","options_paused","mods","mods_entry","online","console","return"};
        for (int i = 0; i < (int)(sizeof(states)/sizeof(states[0])); i++) CAND_ADD(states[i]);
        return n;
    }
    if (arg_index == 1 && _stricmp(cmd, "state.return") == 0) {
        static const char* const states[] = {"main","main_initial","options","options_paused","mods","mods_entry"};
        for (int i = 0; i < (int)(sizeof(states)/sizeof(states[0])); i++) CAND_ADD(states[i]);
        return n;
    }
    if (arg_index == 1 && _stricmp(cmd, "log.level") == 0) {
        CAND_ADD("debug"); CAND_ADD("info"); CAND_ADD("warn"); CAND_ADD("error");
        return n;
    }
    if (arg_index == 1 && _stricmp(cmd, "music.play") == 0) {
        CAND_ADD("random");
        return n;
    }
    if (arg_index == 1 && _stricmp(cmd, "music.output_rate") == 0) {
        CAND_ADD("22050"); CAND_ADD("32000");
        CAND_ADD("44100"); CAND_ADD("48000");
        return n;
    }

    if (arg_index == 1 && console_cmd_in_list(cmd, mod_id_cmds, (int)(sizeof(mod_id_cmds)/sizeof(mod_id_cmds[0])))) {
        if (console_cmd_in_list(cmd, all_cmds, (int)(sizeof(all_cmds)/sizeof(all_cmds[0])))) CAND_ADD("all");
        for (int mi = 0; mi < lua_manager_get_mod_count(); mi++) CAND_ADD(lua_manager_get_mod_id(mi));
        return n;
    }

    if (arg_index == 2 && console_cmd_in_list(cmd, cfg_key_cmds, (int)(sizeof(cfg_key_cmds)/sizeof(cfg_key_cmds[0])))) {
        int idx = console_find_mod_index_by_id(arg1);
        if (idx >= 0) for (int ci = 0; ci < lua_manager_get_mod_config_count(idx); ci++) CAND_ADD(lua_manager_get_mod_config_key(idx, ci));
        return n;
    }
    if (arg_index == 2 && console_cmd_in_list(cmd, bind_key_cmds, (int)(sizeof(bind_key_cmds)/sizeof(bind_key_cmds[0])))) {
        int idx = console_find_mod_index_by_id(arg1);
        if (idx >= 0) for (int bi = 0; bi < lua_manager_get_mod_bind_count(idx); bi++) CAND_ADD(lua_manager_get_mod_bind_key(idx, bi));
        return n;
    }

    if (arg_index == 3 && (_stricmp(cmd, "mods.config.set") == 0 || _stricmp(cmd, "mods.cfg.set") == 0)) {
        int idx = console_find_mod_index_by_id(arg1);
        int ci = (idx >= 0) ? lua_manager_find_mod_config_index(idx, arg2) : -1;
        if (ci >= 0) {
            int type = lua_manager_get_mod_config_type(idx, ci);
            if (type == LUA_CFG_BOOL) { CAND_ADD("true"); CAND_ADD("false"); }
            else if (type == LUA_CFG_OPTIONS) {
                int oc = lua_manager_get_mod_config_option_count(idx, ci);
                for (int oi = 0; oi < oc; oi++) CAND_ADD(lua_manager_get_mod_config_option(idx, ci, oi));
            }
        }
        return n;
    }

    #undef CAND_ADD
    return n;
}

static void console_autocomplete(void) {
    char buf[CONSOLE_INPUT_BUF];
    char ctx[CONSOLE_INPUT_BUF];
    char cands[CONSOLE_CAND_MAX][CONSOLE_CAND_LEN];
    int  match_idx[CONSOLE_CAND_MAX];
    char* toks[8];
    int upto = g_console_cursor;
    int tok_start;
    const char* partial;
    int partial_len;
    int ntok = 0;
    int arg_index;
    int ncand = 0;
    const char* first_match = NULL;
    int common_len = 0;
    int match_count = 0;

    if (upto < 0) upto = 0;
    if (upto > (int)sizeof(buf) - 1) upto = (int)sizeof(buf) - 1;
    memcpy(buf, g_console_input, (size_t)upto);
    buf[upto] = '\0';

    /* The token being completed runs from the last whitespace up to the cursor. */
    tok_start = upto;
    while (tok_start > 0 && !isspace((unsigned char)buf[tok_start - 1])) tok_start--;
    partial = buf + tok_start;
    partial_len = upto - tok_start;

    /* Tokenize everything before the current token: token 0 is the command. */
    memcpy(ctx, buf, (size_t)tok_start);
    ctx[tok_start] = '\0';
    {
        char* cur = ctx;
        char* t;
        while (ntok < 8 && (t = console_parse_token(&cur)) != NULL) toks[ntok++] = t;
    }
    arg_index = ntok;  /* 0 = completing the command itself */

    if (arg_index == 0) {
        int total = (int)(sizeof(k_console_commands) / sizeof(k_console_commands[0]));
        for (int i = 0; i < total && ncand < CONSOLE_CAND_MAX; i++) {
            /* Hide developer commands from autocomplete unless dev mode is on. */
            if (!g_developer_mode && console_cmd_is_developer(k_console_commands[i])) continue;
            safe_copy(cands[ncand], CONSOLE_CAND_LEN, k_console_commands[i]);
            ncand++;
        }
    } else {
        ncand = console_build_arg_candidates(cands, CONSOLE_CAND_MAX,
                                             toks[0], arg_index,
                                             (ntok > 1) ? toks[1] : "",
                                             (ntok > 2) ? toks[2] : "");
    }
    if (ncand == 0) return;

    for (int i = 0; i < ncand; i++) {
        if (_strnicmp(cands[i], partial, (size_t)partial_len) != 0) continue;
        if (!first_match) { first_match = cands[i]; common_len = (int)strlen(cands[i]); }
        else { int c = console_common_prefix_len(first_match, cands[i]); if (c < common_len) common_len = c; }
        if (match_count < CONSOLE_CAND_MAX) match_idx[match_count] = i;
        match_count++;
    }
    if (!first_match) return;

    /* Only list when ambiguous; a single match completes silently. */
    if (match_count > 1) {
        int show = (match_count < CONSOLE_MAX_MATCHES) ? match_count : CONSOLE_MAX_MATCHES;
        console_push_line_rgb("Matches:", 0.72f, 0.90f, 1.00f);
        for (int k = 0; k < show; k++) {
            char out[CONSOLE_LINE_TEXT];
            snprintf(out, sizeof(out), "  %s", cands[match_idx[k]]);
            console_push_line_rgb(out, 0.80f, 0.83f, 0.90f);
        }
        if (match_count > show) {
            char out[CONSOLE_LINE_TEXT];
            snprintf(out, sizeof(out), "  ... and %d more", match_count - show);
            console_push_line_rgb(out, 0.55f, 0.60f, 0.70f);
        }
    }

    /* Apply the completion: prefix (before token) + completion + suffix (after cursor). */
    {
        char completion[CONSOLE_CAND_LEN];
        char newbuf[CONSOLE_INPUT_BUF];
        const char* suffix = g_console_input + g_console_cursor;
        int single = (match_count == 1);
        int new_cursor;
        if (single) {
            safe_copy(completion, sizeof(completion), first_match);
        } else if (common_len > partial_len) {
            int cl = common_len;
            if (cl >= (int)sizeof(completion)) cl = (int)sizeof(completion) - 1;
            memcpy(completion, first_match, (size_t)cl);
            completion[cl] = '\0';
        } else {
            return;  /* ambiguous with no further common prefix */
        }
        snprintf(newbuf, sizeof(newbuf), "%.*s%s%s%s",
                 tok_start, buf, completion, single ? " " : "", suffix);
        new_cursor = tok_start + (int)strlen(completion) + (single ? 1 : 0);
        safe_copy(g_console_input, sizeof(g_console_input), newbuf);
        if (new_cursor > (int)strlen(g_console_input)) new_cursor = (int)strlen(g_console_input);
        g_console_cursor = new_cursor;
    }
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
    memmove(g_console_input + g_console_cursor - 1,
            g_console_input + g_console_cursor,
            len - (size_t)g_console_cursor + 1);
    g_console_cursor--;
}

static void console_delete(void) {
    size_t len = strlen(g_console_input);
    if ((size_t)g_console_cursor >= len) return;
    console_detach_from_history();
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
        case LUA_CFG_OPTIONS: return "options";
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
        console_push_line_rgb("  find <text>", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  console.stats", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  console.copy [output|input]", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  state", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  state.last", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  state.return [main|main_initial|options|options_paused|mods|mods_entry]", 0.87f, 0.87f, 0.87f);
        if (g_developer_mode) {
            console_push_line_rgb("  state.switch <main|main_initial|options|options_paused|mods|mods_entry|online|console|return>", 0.87f, 0.87f, 0.87f);
        }
        console_push_line_rgb("  sys.info", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  ui.size", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  time.scale [value|auto]", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  framework.api", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  discord.app [application_id]", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  music.status", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  music.scan", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  music.rescan", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  music.play <index|random>", 0.87f, 0.87f, 0.87f);
        console_push_line_rgb("  music.output_rate [hz]", 0.87f, 0.87f, 0.87f);
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
        if (g_developer_mode) {
            console_push_line_rgb("  ggpo.roundtrip", 0.87f, 0.87f, 0.87f);
            console_push_line_rgb("  ggpo.selftest [frames]", 0.87f, 0.87f, 0.87f);
            console_push_line_rgb("  ggpo.local [toggle|on|off|status]", 0.87f, 0.87f, 0.87f);
            console_push_line_rgb("  ggpo.net <host|join|off|status|delay|advantage|predict|highping|smoothping|correction|sim>", 0.87f, 0.87f, 0.87f);
        }
        console_push_line_rgb("  log.level [debug|info|warn|error]", 0.87f, 0.87f, 0.87f);
        if (g_developer_mode) {
            console_push_line_rgb("  log.tail [lines]", 0.87f, 0.87f, 0.87f);
            console_push_line_rgb("  input.show [player]", 0.87f, 0.87f, 0.87f);
            console_push_line_rgb("  input.override <player> <mask> [frames] [replace]", 0.87f, 0.87f, 0.87f);
            console_push_line_rgb("  input.clear <player>", 0.87f, 0.87f, 0.87f);
            console_push_line_rgb("  lua <code>", 0.87f, 0.87f, 0.87f);
            console_push_line_rgb("  eval <code>", 0.87f, 0.87f, 0.87f);
            console_push_line_rgb("  lua.mod <id> <code>", 0.87f, 0.87f, 0.87f);
            console_push_line_rgb("  eval.mod <id> <code>", 0.87f, 0.87f, 0.87f);
            console_push_line_rgb("  lua.file <path>", 0.87f, 0.87f, 0.87f);
        }
        console_push_line_rgb("  exit", 0.87f, 0.87f, 0.87f);
        if (g_developer_mode) {
            console_push_line_rgb("  dev [on|off]  (developer mode is ON)", 0.72f, 0.90f, 1.00f);
        }
        return;
    }

    if (_stricmp(t, "discord.app") == 0) {
        console_push_line_rgb("discord.app: show the current public Discord Application ID.", 0.72f, 0.90f, 1.00f);
        console_push_line_rgb("discord.app <application_id>: validate, save, and reconnect immediately.", 0.72f, 0.90f, 1.00f);
        return;
    }
    if (_stricmp(t, "music.status") == 0 ||
        _stricmp(t, "music.scan") == 0 ||
        _stricmp(t, "music.rescan") == 0 ||
        _stricmp(t, "music.play") == 0 ||
        _stricmp(t, "music.output_rate") == 0) {
        console_push_line_rgb("music.scan: list data/tune*.txt, titles, native reachability, and obvious format errors.", 0.72f, 0.90f, 1.00f);
        console_push_line_rgb("music.rescan: retally contiguous native tune files without restarting.", 0.72f, 0.90f, 1.00f);
        console_push_line_rgb("music.play <index|random>: select a native-postfix, bounded, or Dollchan-JS tune immediately.", 0.72f, 0.90f, 1.00f);
        console_push_line_rgb("music.output_rate [8000..192000]: show or persist the mixer Hz; a tune marker's output_rate overrides it.", 0.72f, 0.90f, 1.00f);
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
        console_push_line_rgb("ggpo.net key: arm a one-shot shared v17 key from the clipboard, then clear the clipboard.", 0.72f, 0.90f, 1.00f);
        console_push_line_rgb("ggpo.net key clear: discard an armed one-shot key.", 0.72f, 0.90f, 1.00f);
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
        console_push_line_rgb("After arming a key, F6 hosts on 47777 and F7 joins 127.0.0.1:47777.", 0.72f, 0.90f, 1.00f);
        return;
    }
    if (_stricmp(t, "online.hub") == 0 || _stricmp(t, "online") == 0) {
        console_push_line_rgb("online.hub: open the built-in online hub with Play, Friends, and Settings tabs.", 0.72f, 0.90f, 1.00f);
        console_push_line_rgb("Play uses server login plus casual/competitive queues; Friends handles requests and challenges.", 0.72f, 0.90f, 1.00f);
        return;
    }
    if (_stricmp(t, "net.diag") == 0 || _stricmp(t, "net.trouble") == 0) {
        console_push_line_rgb("net.diag: P2P connection troubleshooter. Run it during a stuck/failing", 0.72f, 0.90f, 1.00f);
        console_push_line_rgb("connect to see candidates, packets sent/received, and a plain verdict on", 0.72f, 0.90f, 1.00f);
        console_push_line_rgb("what's blocking the link (NAT/firewall vs version mismatch, etc.).", 0.72f, 0.90f, 1.00f);
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

/* Search the console scrollback for lines containing `query` (case-insensitive)
 * and reprint the matches. Matches are snapshotted before printing because
 * pushing result lines mutates the output ring buffer. */
static void console_find_in_output(const char* query) {
    char hits[64][CONSOLE_LINE_TEXT];
    int hit_count = 0;
    int total = 0;
    int count;
    if (!query || !query[0]) {
        console_push_line_rgb("Usage: find <text>", 0.98f, 0.76f, 0.40f);
        return;
    }
    count = g_console_line_count;
    for (int i = 0; i < count; i++) {
        ConsoleLine* line = console_line_at_oldest_index(i);
        if (!line || !console_stristr(line->text, query)) continue;
        total++;
        if (hit_count < 64) safe_copy(hits[hit_count++], CONSOLE_LINE_TEXT, line->text);
    }
    if (total == 0) {
        char out[CONSOLE_LINE_TEXT];
        snprintf(out, sizeof(out), "find: no lines matching '%s'", query);
        console_push_line_rgb(out, 0.98f, 0.76f, 0.40f);
        return;
    }
    {
        char hdr[CONSOLE_LINE_TEXT];
        snprintf(hdr, sizeof(hdr), "find '%s': %d match(es)", query, total);
        console_push_line_rgb(hdr, 0.72f, 0.90f, 1.00f);
    }
    for (int i = 0; i < hit_count; i++) {
        char out[CONSOLE_LINE_TEXT];
        snprintf(out, sizeof(out), "  %s", hits[i]);
        console_push_line_rgb(out, 0.80f, 0.83f, 0.90f);
    }
    if (total > hit_count) {
        char out[CONSOLE_LINE_TEXT];
        snprintf(out, sizeof(out), "  ... and %d more", total - hit_count);
        console_push_line_rgb(out, 0.55f, 0.60f, 0.70f);
    }
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
        snprintf(out, sizeof(out), "runtime storage=%d audio=%d generated=%d/%uB font_regs=%d texture_regs=%d",
                 diag.storage_entries,
                 diag.audio_chunks,
                 diag.audio_generated_chunks,
                 diag.audio_generated_pcm_bytes,
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
    } else if (type == LUA_CFG_OPTIONS) {
        ok = lua_manager_config_set_option(idx, cfg_idx, value);
        if (!ok) {
            char opts[CONSOLE_LINE_TEXT];
            size_t pos = 0;
            int n = lua_manager_get_mod_config_option_count(idx, cfg_idx);
            opts[0] = '\0';
            for (int i = 0; i < n && pos < sizeof(opts); i++) {
                pos += (size_t)snprintf(opts + pos, sizeof(opts) - pos, "%s%s",
                                        (i > 0) ? ", " : "",
                                        lua_manager_get_mod_config_option(idx, cfg_idx, i));
            }
            snprintf(out, sizeof(out), "Value must be one of: %s", opts);
            console_push_line_rgb(out, 0.98f, 0.76f, 0.40f);
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
    uint32_t rx_confirmed = 0u;
    uint32_t peer_confirmed = 0u;
    uint32_t checksum_confirmed = 0u;
    int has_rx_confirmed;
    int has_peer_confirmed;
    int has_checksum_confirmed;
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
        has_rx_confirmed =
            ggpo_net_remote_input_confirmed_frame(&rx_confirmed);
        has_peer_confirmed =
            ggpo_net_peer_input_confirmed_frame(&peer_confirmed);
        has_checksum_confirmed =
            ggpo_net_checksum_confirmed_frame(&checksum_confirmed);
        snprintf(out,
                 sizeof(out),
                 "ggpo.net.confirm: remote=%s%u peer_ack=%s%u checksum=%s%u",
                 has_rx_confirmed ? "" : "none/",
                 (unsigned int)(has_rx_confirmed ? rx_confirmed : 0u),
                 has_peer_confirmed ? "" : "none/",
                 (unsigned int)(has_peer_confirmed ? peer_confirmed : 0u),
                 has_checksum_confirmed ? "" : "none/",
                 (unsigned int)(has_checksum_confirmed ? checksum_confirmed : 0u));
        console_push_line_rgb(out, 0.72f, 0.90f, 1.00f);
        LOG_INFO("%s", out);
        snprintf(out,
                 sizeof(out),
                 "ggpo.net.stats: tx=%u rx=%u pred=%u rb=%u late=%u drop=%u adv_stall=%u pred_stall=%u silence=%u desync=%u corr_tx=%u corr_rx=%u corr_req=%u stale_req=%u dup_chunk=%u corr_id=%u applied_id=%u",
                 (unsigned int)ggpo_net_packets_sent(),
                 (unsigned int)ggpo_net_packets_received(),
                 (unsigned int)ggpo_net_prediction_count(),
                 (unsigned int)ggpo_net_rollback_count(),
                 (unsigned int)ggpo_net_late_input_count(),
                 (unsigned int)ggpo_net_dropped_input_count(),
                 (unsigned int)ggpo_net_frame_advantage_stall_count(),
                 (unsigned int)ggpo_net_prediction_stall_count(),
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
                 "ggpo.net.socket: would_block=%u send_error=%u deferred=%u",
                 (unsigned int)ggpo_net_socket_would_block_count(),
                 (unsigned int)ggpo_net_socket_send_error_count(),
                 (unsigned int)ggpo_net_socket_send_deferred_count());
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
    if (!ggpo_net_active()) {
        print_ggpo_net_status();
        return;
    }
    ggpo_net_stop();
    snprintf(out,
             sizeof(out),
             "ggpo.net: stopped from %s after %u frame(s), checksum=%u tx=%u rx=%u pred=%u rb=%u adv_stall=%u pred_stall=%u desync=%u",
             (source && source[0]) ? source : "unknown",
             (unsigned int)frames,
             (unsigned int)checksum,
             (unsigned int)tx,
             (unsigned int)rx,
             (unsigned int)pred,
             (unsigned int)rb,
             (unsigned int)stalls,
             (unsigned int)pred_stalls,
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

static int configure_online_palette_for_player(int player, const char* source) {
    int palette_count = hooks_player_colour_count();
    int skin = hooks_player_colour_index(player, 0);
    int clothing = hooks_player_colour_index(player, 1);
    if (palette_count <= 0 ||
        !ggpo_net_set_local_palette_preference((uint32_t)skin,
                                               (uint32_t)clothing,
                                               (uint32_t)palette_count)) {
        char out[CONSOLE_LINE_TEXT];
        snprintf(out,
                 sizeof(out),
                 "ggpo.net: could not capture player %d palette from %s",
                 player + 1,
                 (source && source[0]) ? source : "unknown");
        console_push_line_rgb(out, 0.98f, 0.45f, 0.45f);
        LOG_ERROR("%s", out);
        return 0;
    }
    return 1;
}

static void start_ggpo_net_host(uint16_t port, const char* source, int held_start) {
    char out[CONSOLE_LINE_TEXT];
    char err[256];

    if (ggpo_net_active()) {
        stop_ggpo_net(source);
        return;
    }
    if (!can_start_ggpo_net(source)) return;
    if (!configure_online_palette_for_player(0, source)) return;

    err[0] = '\0';
    if (!(held_start ? ggpo_net_start_host_held(port, err, sizeof(err))
                     : ggpo_net_start_host(port, err, sizeof(err)))) {
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
    if (!configure_online_palette_for_player(1, source)) return;

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

static void start_ggpo_net_join_deferred(uint16_t local_port, const char* source, int held_start) {
    char out[CONSOLE_LINE_TEXT];
    char err[256];

    if (ggpo_net_active()) {
        stop_ggpo_net(source);
        return;
    }
    if (!can_start_ggpo_net(source)) return;
    if (!configure_online_palette_for_player(1, source)) return;

    err[0] = '\0';
    if (!(held_start ? ggpo_net_start_join_deferred_held(local_port, err, sizeof(err))
                     : ggpo_net_start_join_deferred(local_port, err, sizeof(err)))) {
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

/* Build the P2P connection troubleshooter report (hub context + net-layer diag).
 * Writes newline-separated lines into `out`; used by the net.diag console command
 * and logged automatically when a connect attempt times out. */
static void online_build_net_diag(char* out, size_t cap) {
    char net[1536];
    char lan[64];
    int n;
    const char* server_state =
        g_online_server_state == ONLINE_SERVER_CONNECTED  ? "connected" :
        g_online_server_state == ONLINE_SERVER_CONNECTING ? "connecting" : "offline";
    if (!out || cap == 0) return;
    lan[0] = '\0';
    if (!(net_local_ipv4(lan, sizeof(lan)) && lan[0])) safe_copy(lan, sizeof(lan), "(unknown)");
    net[0] = '\0';
    ggpo_net_format_diag(net, sizeof(net));
    n = snprintf(out, cap,
                 "=== P2P connection troubleshooter ===\n"
                 "server:  %s:%u (%s) slot=%d\n"
                 "match:   active=%s id=%d\n"
                 "your LAN ip: %s\n"
                 "%s",
                 g_online_cfg.server_host, (unsigned int)g_online_cfg.server_port, server_state,
                 g_online_server_slot,
                 g_online_active_match.active ? "yes" : "no", g_online_active_match.match_id,
                 lan, net);
    if (n < 0 || (size_t)n >= cap) out[cap - 1] = '\0';
}

static void console_run_net_diag(void) {
    char report[2048];
    char* p;
    online_build_net_diag(report, sizeof(report));
    p = report;
    while (p && *p) {
        char* nl = strchr(p, '\n');
        if (nl) *nl = '\0';
        if (*p) console_push_line_rgb(p, 0.80f, 0.90f, 1.00f);
        if (!nl) break;
        p = nl + 1;
    }
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
        online_cancel_match_from_console();
        console_close();
        return;
    }
    if (_stricmp(action, "key") == 0 || _stricmp(action, "auth") == 0) {
        char* option = console_parse_token(&cursor);
        char* clipboard;
        char* token_begin;
        char* token_end;
        size_t clipboard_len;
        size_t token_len;
        char err[256];
        int armed;
        if (option && option[0]) {
            if (_stricmp(option, "clear") == 0 || _stricmp(option, "off") == 0) {
                ggpo_net_clear_match_token();
                console_push_line_rgb("ggpo.net: one-shot match key cleared", 0.64f, 0.92f, 0.66f);
                return;
            }
            console_push_line_rgb("Usage: copy one 64-hex shared key, then run ggpo.net key", 0.98f, 0.76f, 0.40f);
            return;
        }
        clipboard = SDL_GetClipboardText();
        if (!clipboard) {
            console_push_line_rgb("ggpo.net: clipboard is unavailable", 0.98f, 0.45f, 0.45f);
            return;
        }
        clipboard_len = strlen(clipboard);
        token_begin = clipboard;
        while (*token_begin == ' ' || *token_begin == '\t' ||
               *token_begin == '\r' || *token_begin == '\n') {
            token_begin++;
        }
        token_end = clipboard + clipboard_len;
        while (token_end > token_begin &&
               (token_end[-1] == ' ' || token_end[-1] == '\t' ||
                token_end[-1] == '\r' || token_end[-1] == '\n')) {
            token_end--;
        }
        token_len = (size_t)(token_end - token_begin);
        memmove(clipboard, token_begin, token_len);
        clipboard[token_len] = '\0';

        /* Do not arm a reusable network secret unless the OS clipboard was
         * successfully cleared. The command/history never contains the key. */
        if (SDL_SetClipboardText("") != 0) {
            credential_ext_secure_zero(clipboard, clipboard_len + 1u);
            SDL_free(clipboard);
            ggpo_net_clear_match_token();
            console_push_line_rgb("ggpo.net: could not clear the clipboard; key was not armed", 0.98f, 0.45f, 0.45f);
            return;
        }
        err[0] = '\0';
        armed = ggpo_net_set_match_token(clipboard, err, sizeof(err));
        credential_ext_secure_zero(clipboard, clipboard_len + 1u);
        SDL_free(clipboard);
        if (!armed) {
            console_push_line_rgb(err[0] ? err : "ggpo.net: clipboard key must be exactly 64 hexadecimal characters",
                                  0.98f, 0.45f, 0.45f);
            credential_ext_secure_zero(err, sizeof(err));
            return;
        }
        credential_ext_secure_zero(err, sizeof(err));
        console_push_line_rgb("ggpo.net: one-shot v17 match key armed; start host/join next",
                              0.64f, 0.92f, 0.66f);
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
        start_ggpo_net_host(port, "console", 0);
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

    console_push_line_rgb("Usage: ggpo.net <key|host|join|off|status|delay|advantage|predict|highping|smoothping|correction|sim>", 0.98f, 0.76f, 0.40f);
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

        if (!lua_manager_set_time_scale((float)value)) {
            console_push_line_rgb("time.scale is locked to 1.0 during online rollback.",
                                  0.98f, 0.76f, 0.40f);
            return;
        }
        snprintf(out, sizeof(out), "time.scale set to %.6g",
                 (double)lua_manager_get_time_scale());
        console_push_line_rgb(out, 0.64f, 0.92f, 0.66f);
    }
}

static int console_discord_application_id_valid(const char* value) {
    size_t length;
    size_t i;
    if (!value) return 0;
    length = strlen(value);
    if (length == 0u || length > 20u ||
        value[0] < '1' || value[0] > '9') {
        return 0;
    }
    for (i = 1u; i < length; i++) {
        if (value[i] < '0' || value[i] > '9') return 0;
    }
    return 1;
}

static void console_handle_discord_app(const char* arg) {
    char value[CONSOLE_INPUT_BUF];
    char out[CONSOLE_LINE_TEXT];
    char* application_id;
    if (!arg || !arg[0]) {
        const char* current = discord_rpc_ext_application_id();
        snprintf(out, sizeof(out), "discord.app: %s (%s)",
                 current && current[0] ? current : "not configured",
                 discord_rpc_ext_setting_label());
        console_push_line_rgb(out, 0.72f, 0.90f, 1.00f);
        return;
    }

    safe_copy(value, sizeof(value), arg);
    application_id = trim_ws(value);
    if (!console_discord_application_id_valid(application_id)) {
        console_push_line_rgb(
            "Usage: discord.app <1-20 digit application_id>",
            0.98f, 0.76f, 0.40f);
        return;
    }
    if (!update_ext_config_set("discord_application_id", application_id)) {
        console_push_line_rgb(
            "discord.app: could not save mods/modframework.cfg",
            0.98f, 0.45f, 0.45f);
        return;
    }
    if (!discord_rpc_ext_set_application_id(application_id)) {
        console_push_line_rgb(
            "discord.app: ID was saved but could not be applied",
            0.98f, 0.45f, 0.45f);
        return;
    }
    snprintf(out, sizeof(out),
             "discord.app set to %s; reconnect queued", application_id);
    console_push_line_rgb(out, 0.64f, 0.92f, 0.66f);
}

static int framework_audio_requested_rate(int requested_rate) {
    return requested_rate == FRAMEWORK_AUDIO_VANILLA_RATE
        ? (int)InterlockedCompareExchange(
              &g_framework_audio_config_rate, 0, 0)
        : requested_rate;
}

static int framework_audio_rate_valid(long rate) {
    return rate >= 8000l && rate <= 192000l;
}

static void framework_audio_load_config(void) {
    char value[64];
    char* end = NULL;
    long parsed;
    if (!update_ext_config_get(
            "music_output_rate", value, sizeof(value))) {
        return;
    }
    parsed = strtol(value, &end, 10);
    if (!end || *end != '\0' || !framework_audio_rate_valid(parsed)) {
        LOG_WARN("[music] ignoring invalid music_output_rate=%s "
                 "(expected 8000..192000)", value);
        return;
    }
    InterlockedExchange(&g_framework_audio_config_rate, (LONG)parsed);
}

static int framework_audio_apply_rate(int requested_rate) {
    int previous;
    if (!framework_audio_rate_valid(requested_rate) ||
        !p_mad_init_audio_stream_trampoline) {
        return 0;
    }
    previous = (int)InterlockedCompareExchange(
        &g_framework_audio_active_rate, 0, 0);
    if (previous == 0) {
        /* The initial detour will use the configured rate when the game opens
         * audio; do not initialize SDL early from a console/config pump. */
        return 1;
    }
    if (previous == requested_rate) return 1;
    /*
     * A zero buffer size asks the native wrapper to derive its normal latency
     * for the new rate. It closes and reopens SDL synchronously, so this call
     * stays on Eggnogg's main thread and never runs from the audio callback.
     */
    p_mad_init_audio_stream_trampoline(requested_rate, 0);
    InterlockedExchange(
        &g_framework_audio_active_rate, (LONG)requested_rate);
    LOG_INFO("[music] mixer output changed from %d Hz to %d Hz",
             previous, requested_rate);
    return 1;
}

static void __cdecl hooked_mad_init_audio_stream(int requested_rate,
                                                  int buffer_frames) {
    int selected_rate = framework_audio_requested_rate(requested_rate);
    if (!p_mad_init_audio_stream_trampoline) return;
    p_mad_init_audio_stream_trampoline(selected_rate, buffer_frames);
    InterlockedExchange(
        &g_framework_audio_active_rate, (LONG)selected_rate);
    if (selected_rate != requested_rate) {
        LOG_INFO("[music] mixer output upgraded from %d Hz to %d Hz",
                 requested_rate, selected_rate);
    }
}

typedef struct NativeTuneProbe {
    int exists;
    int has_unsupported_javascript;
    int is_yule_bytebeat;
    int is_dollchan;
    char title[96];
} NativeTuneProbe;

static void native_tune_path(int index, char* out, size_t out_size) {
    if (!out || out_size == 0u) return;
    if (index == 0) {
        snprintf(out, out_size, "data/tune.txt");
    } else {
        snprintf(out, out_size, "data/tune%d.txt", index);
    }
}

static void native_tune_probe(int index, NativeTuneProbe* probe) {
    char path[MAX_PATH];
    char line[2048];
    FILE* file;
    if (!probe) return;
    memset(probe, 0, sizeof(*probe));
    safe_copy(probe->title, sizeof(probe->title), "(untitled)");
    native_tune_path(index, path, sizeof(path));
    file = fopen(path, "rb");
    if (!file) return;
    probe->exists = 1;
    while (fgets(line, sizeof(line), file)) {
        char* title_start;
        if (strstr(line, "=>") || strstr(line, "**") ||
            strstr(line, "random(") || strstr(line, "Math.") ||
            strstr(line, "function(") || strstr(line, "function ")) {
            probe->has_unsupported_javascript = 1;
        }
        if (strstr(line, "yule:bytebeat")) {
            probe->is_yule_bytebeat = 1;
            if (strstr(line, "engine=dollchan") ||
                strstr(line, "engine=javascript")) {
                probe->is_dollchan = 1;
            }
        }
        title_start = strstr(line, "$\"");
        if (title_start && strcmp(probe->title, "(untitled)") == 0) {
            char* title_end;
            size_t length;
            title_start += 2;
            title_end = strchr(title_start, '"');
            if (!title_end) continue;
            length = (size_t)(title_end - title_start);
            if (length >= sizeof(probe->title)) {
                length = sizeof(probe->title) - 1u;
            }
            memcpy(probe->title, title_start, length);
            probe->title[length] = '\0';
        }
    }
    fclose(file);
}

typedef struct FrameworkTuneRuntime {
    CRITICAL_SECTION lock;
    int lock_initialized;
    int active;
    int track_index;
    DWORD file_size_low;
    DWORD file_size_high;
    FILETIME file_time;
    BytebeatProgram program;
    BytebeatStream* js_stream;
    BytebeatPlaylistEngine engine;
    BytebeatPlaylistMode mode;
    uint32_t sample_rate;
    uint32_t output_rate;
    double volume;
    uint64_t source_t;
    uint64_t phase;
    int16_t held_left;
    int16_t held_right;
    uint32_t logged_underruns;
    int logged_track;
    DWORD logged_error;
    int rejected_track;
    DWORD rejected_size_low;
    DWORD rejected_size_high;
    FILETIME rejected_time;
} FrameworkTuneRuntime;

static FrameworkTuneRuntime g_framework_tune;

static int framework_tune_ensure_lock(void) {
    if (g_framework_tune.lock_initialized) return 1;
    InitializeCriticalSection(&g_framework_tune.lock);
    g_framework_tune.lock_initialized = 1;
    g_framework_tune.track_index = -1;
    g_framework_tune.logged_track = -1;
    g_framework_tune.rejected_track = -1;
    return 1;
}

static int framework_tune_file_stamp(int index, FILETIME* out_time,
                                     DWORD* out_size_high,
                                     DWORD* out_size_low) {
    char path[MAX_PATH];
    WIN32_FILE_ATTRIBUTE_DATA data;
    native_tune_path(index, path, sizeof(path));
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data) ||
        (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        return 0;
    }
    if (out_time) *out_time = data.ftLastWriteTime;
    if (out_size_high) *out_size_high = data.nFileSizeHigh;
    if (out_size_low) *out_size_low = data.nFileSizeLow;
    return 1;
}

static int framework_tune_stamp_matches(const FILETIME* time,
                                        DWORD size_high, DWORD size_low) {
    return g_framework_tune.file_time.dwLowDateTime == time->dwLowDateTime &&
           g_framework_tune.file_time.dwHighDateTime == time->dwHighDateTime &&
           g_framework_tune.file_size_high == size_high &&
           g_framework_tune.file_size_low == size_low;
}

static int framework_tune_rejection_matches(int track,
                                            const FILETIME* time,
                                            DWORD size_high,
                                            DWORD size_low) {
    return g_framework_tune.rejected_track == track &&
           g_framework_tune.rejected_time.dwLowDateTime ==
                time->dwLowDateTime &&
           g_framework_tune.rejected_time.dwHighDateTime ==
                time->dwHighDateTime &&
           g_framework_tune.rejected_size_high == size_high &&
           g_framework_tune.rejected_size_low == size_low;
}

static void framework_tune_remember_rejection(int track,
                                               const FILETIME* time,
                                               DWORD size_high,
                                               DWORD size_low) {
    g_framework_tune.rejected_track = track;
    g_framework_tune.rejected_time = *time;
    g_framework_tune.rejected_size_high = size_high;
    g_framework_tune.rejected_size_low = size_low;
}

static BytebeatJsMode framework_tune_js_mode(BytebeatPlaylistMode mode) {
    switch (mode) {
        case BYTEBEAT_PLAYLIST_S8: return BYTEBEAT_JS_MODE_S8;
        case BYTEBEAT_PLAYLIST_FLOAT: return BYTEBEAT_JS_MODE_FLOAT;
        case BYTEBEAT_PLAYLIST_FUNC: return BYTEBEAT_JS_MODE_FUNC;
        case BYTEBEAT_PLAYLIST_U8:
        default: return BYTEBEAT_JS_MODE_U8;
    }
}

static int framework_tune_load(int index, BytebeatProgram* out_program,
                               BytebeatJsRuntime** out_js_runtime,
                               BytebeatPlaylistOptions* out_options,
                               BytebeatDiagnostic* diagnostic,
                               char* error, size_t error_size) {
    enum { FRAMEWORK_TUNE_HEADER_ALLOWANCE = 16384 };
    char path[MAX_PATH];
    char marker[1024];
    unsigned char* file_data = NULL;
    char* source;
    char* source_end;
    size_t source_length;
    long file_size_long;
    size_t file_size;
    FILE* file;
    char* line_start;
    int found_marker = 0;
    BytebeatPlaylistOptions options;
    if (!out_program || !out_js_runtime || !out_options ||
        !error || error_size == 0u) {
        return 0;
    }
    error[0] = '\0';
    *out_js_runtime = NULL;
    native_tune_path(index, path, sizeof(path));
    file = fopen(path, "rb");
    if (!file) {
        snprintf(error, error_size, "could not open %s", path);
        return 0;
    }
    if (fseek(file, 0, SEEK_END) != 0 ||
        (file_size_long = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        snprintf(error, error_size, "could not size %s", path);
        return 0;
    }
    file_size = (size_t)file_size_long;
    if (file_size == 0u ||
        file_size > BYTEBEAT_JS_MAX_SOURCE + FRAMEWORK_TUNE_HEADER_ALLOWANCE) {
        fclose(file);
        snprintf(error, error_size,
                 "playlist file exceeds the %u-byte source limit",
                 (unsigned)BYTEBEAT_JS_MAX_SOURCE);
        return 0;
    }
    file_data = (unsigned char*)malloc(file_size + 1u);
    if (!file_data) {
        fclose(file);
        snprintf(error, error_size, "out of memory reading %s", path);
        return 0;
    }
    if (fread(file_data, 1u, file_size, file) != file_size) {
        fclose(file);
        free(file_data);
        snprintf(error, error_size, "could not read %s", path);
        return 0;
    }
    fclose(file);
    if (memchr(file_data, '\0', file_size) != NULL) {
        free(file_data);
        snprintf(error, error_size, "playlist contains a NUL byte");
        return 0;
    }
    file_data[file_size] = '\0';
    line_start = (char*)file_data;
    source = NULL;
    while ((size_t)(line_start - (char*)file_data) < file_size) {
        char* line_end = strchr(line_start, '\n');
        char* content_end = line_end
            ? line_end : (char*)file_data + file_size;
        char* clean = line_start;
        size_t marker_length;
        while (clean < content_end &&
               isspace((unsigned char)*clean)) clean++;
        while (content_end > clean &&
               (content_end[-1] == '\r' ||
                isspace((unsigned char)content_end[-1]))) {
            content_end--;
        }
        {
            char* tag_position = clean < content_end
                ? strstr(clean, "yule:bytebeat") : NULL;
        if (clean < content_end && *clean == '(' &&
            tag_position && tag_position < content_end) {
            marker_length = (size_t)(content_end - clean);
            if (marker_length >= sizeof(marker)) {
                free(file_data);
                snprintf(error, error_size,
                         "playlist marker exceeds %u bytes",
                         (unsigned)(sizeof(marker) - 1u));
                return 0;
            }
            memcpy(marker, clean, marker_length);
            marker[marker_length] = '\0';
            if (!bytebeat_parse_playlist_options(
                    marker, &options, diagnostic)) {
                free(file_data);
                snprintf(error, error_size, "marker byte %lu: %s",
                         (unsigned long)(diagnostic
                            ? diagnostic->offset : 0u),
                         diagnostic && diagnostic->message[0]
                            ? diagnostic->message
                            : "invalid yule:bytebeat options");
                return 0;
            }
            found_marker = 1;
            source = line_end ? line_end + 1 : (char*)file_data + file_size;
            break;
        }
        }
        if (!line_end) break;
        line_start = line_end + 1;
    }
    if (!found_marker || !source) {
        free(file_data);
        snprintf(error, error_size, "missing yule:bytebeat marker");
        return 0;
    }
    source_end = (char*)file_data + file_size;
    while (source < source_end && isspace((unsigned char)*source)) source++;
    while (source_end > source &&
           isspace((unsigned char)source_end[-1])) source_end--;
    source_length = (size_t)(source_end - source);
    *source_end = '\0';
    if (source_length == 0u) {
        free(file_data);
        snprintf(error, error_size, "missing bytebeat source");
        return 0;
    }
    if (options.engine == BYTEBEAT_PLAYLIST_DOLLCHAN) {
        if (source_length > BYTEBEAT_JS_MAX_SOURCE) {
            free(file_data);
            snprintf(error, error_size,
                     "JavaScript source exceeds %u bytes",
                     (unsigned)BYTEBEAT_JS_MAX_SOURCE);
            return 0;
        }
        *out_js_runtime = bytebeat_js_create(
            source, source_length, framework_tune_js_mode(options.mode),
            options.sample_rate, options.volume, error, error_size);
        if (!*out_js_runtime) {
            free(file_data);
            return 0;
        }
    } else {
        if (options.mode != BYTEBEAT_PLAYLIST_U8) {
            free(file_data);
            snprintf(error, error_size,
                     "bounded playlist engine supports bytebeat mode only");
            return 0;
        }
        if (source_length > BYTEBEAT_MAX_EXPRESSION) {
            free(file_data);
            snprintf(error, error_size,
                     "bounded expression exceeds %u bytes",
                     (unsigned)BYTEBEAT_MAX_EXPRESSION);
            return 0;
        }
        if (!bytebeat_compile(source, BYTEBEAT_MODE_U8,
                              out_program, diagnostic)) {
            free(file_data);
            snprintf(error, error_size, "byte %lu: %s",
                     (unsigned long)(diagnostic ? diagnostic->offset : 0u),
                     diagnostic && diagnostic->message[0]
                        ? diagnostic->message : "compile failed");
            return 0;
        }
        if ((uint64_t)out_program->node_count *
                (uint64_t)options.sample_rate >
            (uint64_t)BYTEBEAT_MAX_STREAM_OPERATIONS_PER_SECOND) {
            free(file_data);
            snprintf(error, error_size,
                     "stream needs more than %u node evaluations per second",
                     (unsigned)BYTEBEAT_MAX_STREAM_OPERATIONS_PER_SECOND);
            return 0;
        }
    }
    *out_options = options;
    free(file_data);
    return 1;
}

static uint32_t framework_tune_to_uint32(double value) {
    double truncated;
    double wrapped;
    if (!isfinite(value) || value == 0.0) return 0u;
    truncated = value < 0.0 ? ceil(value) : floor(value);
    wrapped = fmod(truncated, 4294967296.0);
    if (wrapped < 0.0) wrapped += 4294967296.0;
    return (uint32_t)wrapped;
}

static int16_t framework_tune_sample(double value, double volume) {
    double sample = ((double)(int)(framework_tune_to_uint32(value) & 0xffu) -
                     128.0) * 256.0 * volume;
    if (sample < -32768.0) sample = -32768.0;
    if (sample > 32767.0) sample = 32767.0;
    return (int16_t)(sample < 0.0
        ? ceil(sample - 0.5) : floor(sample + 0.5));
}

static int16_t framework_tune_mix_sample(int16_t existing, int16_t added) {
    int mixed = (int)existing + (int)added;
    if (mixed < -32768) mixed = -32768;
    if (mixed > 32767) mixed = 32767;
    return (int16_t)mixed;
}

static void __cdecl framework_tune_audio_callback(int16_t* samples,
                                                   int frame_count,
                                                   int output_rate) {
    int frame;
    int using_javascript = 0;
    fn_glitch_audio_callback_t previous =
        g_framework_previous_audio_callback;
    if (!samples || frame_count <= 0 || output_rate <= 0) return;

    /* Preserve a native/glitch producer that owned this slot before Yule. */
    if (previous && previous != framework_tune_audio_callback) {
        previous(samples, frame_count, output_rate);
    }

    if (g_framework_tune.lock_initialized &&
        TryEnterCriticalSection(&g_framework_tune.lock)) {
        using_javascript =
            g_framework_tune.active &&
            g_framework_tune.engine == BYTEBEAT_PLAYLIST_DOLLCHAN &&
            g_framework_tune.js_stream != NULL;
        if (g_framework_tune.active &&
            (!using_javascript ||
             bytebeat_stream_ready(g_framework_tune.js_stream))) {
            for (frame = 0; frame < frame_count; frame++) {
                g_framework_tune.phase += g_framework_tune.sample_rate;
                while (g_framework_tune.phase >=
                       (uint64_t)(uint32_t)output_rate) {
                    g_framework_tune.phase -=
                        (uint64_t)(uint32_t)output_rate;
                    if (using_javascript) {
                        if (!bytebeat_stream_read(
                                g_framework_tune.js_stream,
                                &g_framework_tune.held_left,
                                &g_framework_tune.held_right)) {
                            g_framework_tune.held_left = 0;
                            g_framework_tune.held_right = 0;
                        }
                    } else {
                        double value = 128.0;
                        if (bytebeat_evaluate(
                                &g_framework_tune.program,
                                (uint32_t)g_framework_tune.source_t,
                                g_framework_tune.sample_rate, &value)) {
                            g_framework_tune.held_left =
                                framework_tune_sample(
                                    value, g_framework_tune.volume);
                            g_framework_tune.held_right =
                                g_framework_tune.held_left;
                        } else {
                            g_framework_tune.held_left = 0;
                            g_framework_tune.held_right = 0;
                        }
                    }
                    g_framework_tune.source_t++;
                }
                samples[frame * 2] = framework_tune_mix_sample(
                    samples[frame * 2], g_framework_tune.held_left);
                samples[frame * 2 + 1] = framework_tune_mix_sample(
                    samples[frame * 2 + 1],
                    g_framework_tune.held_right);
            }
        }
        LeaveCriticalSection(&g_framework_tune.lock);
    }

    /* Generated mod audio shares Eggnogg's already-open device. */
    lua_manager_audio_mix_generated(samples, frame_count, output_rate);
}

static void framework_audio_update_callback_ownership(void) {
    fn_glitch_audio_callback_t current;
    int needed = g_framework_tune.active ||
                 lua_manager_audio_generated_active();
    if (!g_native_glitch_callback) return;
    current = *g_native_glitch_callback;
    if (needed) {
        if (current != framework_tune_audio_callback) {
            g_framework_previous_audio_callback = current;
            *g_native_glitch_callback = framework_tune_audio_callback;
        }
    } else if (current == framework_tune_audio_callback) {
        *g_native_glitch_callback = g_framework_previous_audio_callback;
        g_framework_previous_audio_callback = NULL;
    }
}

static void framework_tune_deactivate(void) {
    BytebeatStream* old_stream;
    if (!g_framework_tune.lock_initialized) return;
    EnterCriticalSection(&g_framework_tune.lock);
    g_framework_tune.active = 0;
    g_framework_tune.track_index = -1;
    old_stream = g_framework_tune.js_stream;
    g_framework_tune.js_stream = NULL;
    LeaveCriticalSection(&g_framework_tune.lock);
    bytebeat_stream_destroy(old_stream);
}

static void framework_tune_pump(void) {
    int forced;
    int selected;
    NativeTuneProbe probe;
    FILETIME file_time;
    DWORD size_high;
    DWORD size_low;
    BytebeatProgram program;
    BytebeatJsRuntime* js_runtime = NULL;
    BytebeatStream* js_stream = NULL;
    BytebeatStream* old_js_stream = NULL;
    BytebeatPlaylistOptions options;
    BytebeatDiagnostic diagnostic;
    char error[256];
    DWORD error_hash;

    framework_tune_ensure_lock();
    if (!g_native_music_setting || *g_native_music_setting == 0 ||
        !g_native_tune_count || *g_native_tune_count <= 0) {
        (void)framework_audio_apply_rate(
            (int)InterlockedCompareExchange(
                &g_framework_audio_config_rate, 0, 0));
        framework_tune_deactivate();
        framework_audio_update_callback_ownership();
        return;
    }
    forced = g_native_forced_tune ? *g_native_forced_tune : -1;
    selected = forced < 0 && g_native_shuffle_tune
        ? *g_native_shuffle_tune : forced;
    if (selected < 0 || selected >= *g_native_tune_count) {
        framework_tune_deactivate();
        framework_audio_update_callback_ownership();
        return;
    }
    native_tune_probe(selected, &probe);
    if (!probe.exists || !probe.is_yule_bytebeat) {
        (void)framework_audio_apply_rate(
            (int)InterlockedCompareExchange(
                &g_framework_audio_config_rate, 0, 0));
        framework_tune_deactivate();
        framework_audio_update_callback_ownership();
        return;
    }
    if (!framework_tune_file_stamp(selected, &file_time,
                                   &size_high, &size_low)) {
        framework_tune_deactivate();
        framework_audio_update_callback_ownership();
        return;
    }

    if (g_framework_tune.active && g_framework_tune.js_stream &&
        bytebeat_stream_failed(g_framework_tune.js_stream)) {
        EnterCriticalSection(&g_framework_tune.lock);
        bytebeat_stream_error(g_framework_tune.js_stream,
                              error, sizeof(error));
        old_js_stream = g_framework_tune.js_stream;
        g_framework_tune.js_stream = NULL;
        g_framework_tune.active = 0;
        g_framework_tune.track_index = -1;
        framework_tune_remember_rejection(
            selected, &file_time, size_high, size_low);
        LeaveCriticalSection(&g_framework_tune.lock);
        LOG_ERROR("[music][tune%d] Dollchan runtime stopped: %s",
                  selected, error);
        bytebeat_stream_destroy(old_js_stream);
        framework_audio_update_callback_ownership();
        return;
    }
    if (g_framework_tune.active && g_framework_tune.js_stream) {
        uint32_t underruns =
            bytebeat_stream_underruns(g_framework_tune.js_stream);
        if (underruns != g_framework_tune.logged_underruns) {
            LOG_WARN("[music][tune%d] PCM producer underrun count=%u "
                     "(buffered=%u frames)",
                     selected, (unsigned)underruns,
                     (unsigned)bytebeat_stream_buffered_frames(
                         g_framework_tune.js_stream));
            g_framework_tune.logged_underruns = underruns;
        }
    }

    if (framework_tune_rejection_matches(
            selected, &file_time, size_high, size_low)) {
        if (g_framework_tune.active &&
            g_framework_tune.track_index == selected) {
            framework_audio_update_callback_ownership();
        } else if (g_framework_tune.active) {
            framework_tune_deactivate();
            framework_audio_update_callback_ownership();
        } else {
            framework_audio_update_callback_ownership();
        }
        return;
    }

    if (!g_framework_tune.active ||
        g_framework_tune.track_index != selected ||
        !framework_tune_stamp_matches(&file_time, size_high, size_low)) {
        if (!framework_tune_load(selected, &program, &js_runtime, &options,
                                 &diagnostic, error, sizeof(error))) {
            error_hash = 2166136261u;
            {
                const unsigned char* cursor = (const unsigned char*)error;
                while (*cursor) {
                    error_hash ^= *cursor++;
                    error_hash *= 16777619u;
                }
            }
            if (g_framework_tune.logged_track != selected ||
                g_framework_tune.logged_error != error_hash) {
                LOG_ERROR("[music][tune%d] bytebeat track rejected: %s",
                          selected, error);
                g_framework_tune.logged_track = selected;
                g_framework_tune.logged_error = error_hash;
            }
            framework_tune_remember_rejection(
                selected, &file_time, size_high, size_low);
            if (!g_framework_tune.active ||
                g_framework_tune.track_index != selected) {
                framework_tune_deactivate();
            }
            framework_audio_update_callback_ownership();
            return;
        }
        if (js_runtime) {
            js_stream = bytebeat_stream_create(
                js_runtime, options.sample_rate, error, sizeof(error));
            if (!js_stream) {
                bytebeat_js_destroy(js_runtime);
                js_runtime = NULL;
                error_hash = 2166136261u;
                {
                    const unsigned char* cursor =
                        (const unsigned char*)error;
                    while (*cursor) {
                        error_hash ^= *cursor++;
                        error_hash *= 16777619u;
                    }
                }
                if (g_framework_tune.logged_track != selected ||
                    g_framework_tune.logged_error != error_hash) {
                    LOG_ERROR("[music][tune%d] bytebeat stream rejected: %s",
                              selected, error);
                    g_framework_tune.logged_track = selected;
                    g_framework_tune.logged_error = error_hash;
                }
                framework_tune_remember_rejection(
                    selected, &file_time, size_high, size_low);
                if (!g_framework_tune.active ||
                    g_framework_tune.track_index != selected) {
                    framework_tune_deactivate();
                }
                framework_audio_update_callback_ownership();
                return;
            }
            js_runtime = NULL;
        }
        (void)framework_audio_apply_rate(
            options.output_rate != 0u
                ? (int)options.output_rate
                : (int)InterlockedCompareExchange(
                      &g_framework_audio_config_rate, 0, 0));
        EnterCriticalSection(&g_framework_tune.lock);
        old_js_stream = g_framework_tune.js_stream;
        g_framework_tune.program = program;
        g_framework_tune.js_stream = js_stream;
        g_framework_tune.engine = options.engine;
        g_framework_tune.mode = options.mode;
        g_framework_tune.sample_rate = options.sample_rate;
        g_framework_tune.output_rate = options.output_rate;
        g_framework_tune.volume = options.volume;
        g_framework_tune.source_t = 0u;
        g_framework_tune.phase = 0u;
        g_framework_tune.held_left = 0;
        g_framework_tune.held_right = 0;
        g_framework_tune.logged_underruns = 0u;
        g_framework_tune.track_index = selected;
        g_framework_tune.file_time = file_time;
        g_framework_tune.file_size_high = size_high;
        g_framework_tune.file_size_low = size_low;
        g_framework_tune.active = 1;
        g_framework_tune.rejected_track = -1;
        LeaveCriticalSection(&g_framework_tune.lock);
        bytebeat_stream_destroy(old_js_stream);
        g_framework_tune.logged_track = -1;
        g_framework_tune.logged_error = 0u;
        LOG_INFO("[music][tune%d] %s active: %s (%u Hz, %.2f volume)",
                 selected,
                 options.engine == BYTEBEAT_PLAYLIST_DOLLCHAN
                    ? "Dollchan JavaScript" : "bounded bytebeat",
                 probe.title, (unsigned)options.sample_rate, options.volume);
    }
    framework_audio_update_callback_ownership();
}

static void framework_tune_shutdown(void) {
    framework_tune_deactivate();
    if (g_native_glitch_callback &&
        *g_native_glitch_callback == framework_tune_audio_callback) {
        *g_native_glitch_callback = g_framework_previous_audio_callback;
    }
    g_framework_previous_audio_callback = NULL;
}

static void console_music_status(void) {
    int count = g_native_tune_count ? *g_native_tune_count : 0;
    int forced = g_native_forced_tune ? *g_native_forced_tune : -1;
    int shuffled = g_native_shuffle_tune ? *g_native_shuffle_tune : -1;
    int current = g_native_last_tune ? *g_native_last_tune : -1;
    char out[CONSOLE_LINE_TEXT];
    snprintf(out, sizeof(out),
             "music.status: count=%d mode=%s selected=%d loaded=%d",
             count, forced < 0 ? "random" : "forced",
             forced < 0 ? shuffled : forced, current);
    console_push_line_rgb(out, 0.72f, 0.90f, 1.00f);
    snprintf(out, sizeof(out),
             "music.output_rate: configured=%ld active=%ld%s",
             (long)InterlockedCompareExchange(
                 &g_framework_audio_config_rate, 0, 0),
             (long)InterlockedCompareExchange(
                 &g_framework_audio_active_rate, 0, 0),
             g_framework_tune.active &&
                     g_framework_tune.output_rate != 0u
                 ? " (track override)" : "");
    console_push_line_rgb(out, 0.72f, 0.90f, 1.00f);
}

static void console_music_output_rate(const char* arg) {
    char value[CONSOLE_INPUT_BUF];
    char persisted[32];
    char out[CONSOLE_LINE_TEXT];
    char* rate_text;
    char* end = NULL;
    long rate;
    uint32_t track_override =
        g_framework_tune.active ? g_framework_tune.output_rate : 0u;
    if (!arg || !arg[0]) {
        console_music_status();
        return;
    }
    safe_copy(value, sizeof(value), arg);
    rate_text = trim_ws(value);
    rate = strtol(rate_text, &end, 10);
    if (!end || *end != '\0' || !framework_audio_rate_valid(rate)) {
        console_push_line_rgb(
            "Usage: music.output_rate <8000..192000>",
            0.98f, 0.76f, 0.40f);
        return;
    }
    snprintf(persisted, sizeof(persisted), "%ld", rate);
    if (!update_ext_config_set("music_output_rate", persisted)) {
        console_push_line_rgb(
            "music.output_rate: could not save mods/modframework.cfg",
            0.98f, 0.45f, 0.45f);
        return;
    }
    InterlockedExchange(&g_framework_audio_config_rate, (LONG)rate);
    if (track_override == 0u) {
        if (!framework_audio_apply_rate((int)rate)) {
            console_push_line_rgb(
                "music.output_rate: saved, but mixer restart failed",
                0.98f, 0.45f, 0.45f);
            return;
        }
        snprintf(out, sizeof(out),
                 "music.output_rate set to %ld Hz", rate);
    } else {
        snprintf(out, sizeof(out),
                 "music.output_rate saved as %ld Hz; current tune overrides "
                 "the mixer with %u Hz",
                 rate, (unsigned)track_override);
    }
    console_push_line_rgb(out, 0.64f, 0.92f, 0.66f);
}

static void console_music_scan(void) {
    int index;
    int found = 0;
    int first_gap = -1;
    int ignored_after_gap = 0;
    for (index = 0; index < 100; index++) {
        NativeTuneProbe probe;
        char out[CONSOLE_LINE_TEXT];
        native_tune_probe(index, &probe);
        if (!probe.exists) {
            if (first_gap < 0) first_gap = index;
            continue;
        }
        found++;
        if (first_gap >= 0) ignored_after_gap = 1;
        snprintf(out, sizeof(out), "music[%d] %s%s: %s",
                 index,
                 first_gap >= 0 ? "IGNORED_AFTER_GAP " : "",
                 probe.is_yule_bytebeat
                    ? (probe.is_dollchan
                        ? "dollchan-js" : "bounded-bytebeat")
                    : (probe.has_unsupported_javascript
                        ? "UNSUPPORTED_JAVASCRIPT" : "native-postfix"),
                 probe.title);
        console_push_line_rgb(
            out,
            (first_gap >= 0 ||
             (probe.has_unsupported_javascript && !probe.is_yule_bytebeat))
                ? 0.98f : 0.72f,
            (first_gap >= 0 ||
             (probe.has_unsupported_javascript && !probe.is_yule_bytebeat))
                ? 0.66f : 0.90f,
            (first_gap >= 0 ||
             (probe.has_unsupported_javascript && !probe.is_yule_bytebeat))
                ? 0.38f : 1.00f);
    }
    if (found == 0) {
        console_push_line_rgb("music.scan: no data/tune*.txt files found",
                              0.98f, 0.45f, 0.45f);
    } else if (ignored_after_gap) {
        char out[CONSOLE_LINE_TEXT];
        snprintf(out, sizeof(out),
                 "Native discovery stops at the first gap (missing index %d).",
                 first_gap);
        console_push_line_rgb(out, 0.98f, 0.76f, 0.40f);
    }
}

static void console_music_rescan(void) {
    if (!p_main_tally_tunes) {
        console_push_line_rgb("music.rescan unavailable",
                              0.98f, 0.45f, 0.45f);
        return;
    }
    p_main_tally_tunes();
    if (g_native_last_tune) *g_native_last_tune = -1;
    console_music_status();
}

static void console_music_play(const char* arg) {
    char value[CONSOLE_INPUT_BUF];
    char* selection;
    long parsed = -1;
    int count;
    if (!arg || !arg[0]) {
        console_push_line_rgb("Usage: music.play <index|random>",
                              0.98f, 0.76f, 0.40f);
        return;
    }
    safe_copy(value, sizeof(value), arg);
    selection = trim_ws(value);
    if (p_main_tally_tunes) p_main_tally_tunes();
    count = g_native_tune_count ? *g_native_tune_count : 0;
    if (count <= 0) {
        console_push_line_rgb("music.play: no contiguous native tunes found",
                              0.98f, 0.45f, 0.45f);
        return;
    }

    if (_stricmp(selection, "random") == 0) {
        if (g_native_forced_tune) *g_native_forced_tune = -1;
        if (count == 1) {
            if (g_native_shuffle_tune) *g_native_shuffle_tune = 0;
        } else if (p_game_pick_random_tune) {
            p_game_pick_random_tune();
        }
        if (g_native_last_tune) *g_native_last_tune = -1;
        console_music_status();
        return;
    }

    if (!console_try_parse_long(selection, &parsed) ||
        parsed < 0 || parsed >= count) {
        char out[CONSOLE_LINE_TEXT];
        snprintf(out, sizeof(out),
                 "music.play: index must be 0..%d or random", count - 1);
        console_push_line_rgb(out, 0.98f, 0.76f, 0.40f);
        return;
    }
    {
        NativeTuneProbe probe;
        native_tune_probe((int)parsed, &probe);
        if (!probe.exists) {
            console_push_line_rgb("music.play: tune file disappeared during rescan",
                                  0.98f, 0.45f, 0.45f);
            return;
        }
        if (probe.has_unsupported_javascript && !probe.is_yule_bytebeat) {
            char out[CONSOLE_LINE_TEXT];
            snprintf(out, sizeof(out),
                     "music.play: tune %ld uses general JavaScript; data/tune files require Eggnogg postfix syntax",
                     parsed);
            console_push_line_rgb(out, 0.98f, 0.45f, 0.45f);
            console_push_line_rgb(
                "Translate it to a marked bounded expression (or use mod.audio.play_bytebeat); arrow functions, assignments, arrays, and random() are unsupported.",
                0.98f, 0.76f, 0.40f);
            return;
        }
    }
    if (g_native_forced_tune) *g_native_forced_tune = (int)parsed;
    if (g_native_shuffle_tune) *g_native_shuffle_tune = (int)parsed;
    if (g_native_last_tune) *g_native_last_tune = -1;
    console_music_status();
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

    /* Developer-mode gate: hide debug/power commands unless enabled. Reported as
     * an unknown command so their existence isn't advertised. */
    if (console_cmd_is_developer(cmd) && !g_developer_mode) {
        char out[CONSOLE_LINE_TEXT];
        snprintf(out, sizeof(out), "Unknown command: %s (type 'help')", cmd);
        console_push_line_rgb(out, 0.98f, 0.45f, 0.45f);
        console_set_input("");
        return;
    }

    if (_stricmp(cmd, "dev") == 0) {
        int want = g_developer_mode;
        if (_stricmp(arg, "on") == 0 || _stricmp(arg, "1") == 0 || _stricmp(arg, "enable") == 0) want = 1;
        else if (_stricmp(arg, "off") == 0 || _stricmp(arg, "0") == 0 || _stricmp(arg, "disable") == 0) want = 0;
        else if (!arg[0] || _stricmp(arg, "toggle") == 0) want = !g_developer_mode;
        g_developer_mode = want;
        {
            char out[CONSOLE_LINE_TEXT];
            snprintf(out, sizeof(out), "developer mode: %s%s", g_developer_mode ? "ON" : "off",
                     g_developer_mode ? " (debug commands unlocked - type 'commands')" : "");
            console_push_line_rgb(out, 0.72f, 0.90f, 1.00f);
        }
        console_set_input("");
        return;
    }

    if (_stricmp(cmd, "help") == 0 || _stricmp(cmd, "commands") == 0) {
        console_show_help(arg);
    } else if (_stricmp(cmd, "clear") == 0) {
        console_clear_output();
    } else if (_stricmp(cmd, "history") == 0) {
        console_show_history(arg);
    } else if (_stricmp(cmd, "echo") == 0) {
        console_echo(arg);
    } else if (_stricmp(cmd, "find") == 0 || _stricmp(cmd, "console.find") == 0) {
        console_find_in_output(arg);
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
    } else if (_stricmp(cmd, "discord.app") == 0) {
        console_handle_discord_app(arg);
    } else if (_stricmp(cmd, "music.status") == 0) {
        console_music_status();
    } else if (_stricmp(cmd, "music.scan") == 0) {
        console_music_scan();
    } else if (_stricmp(cmd, "music.rescan") == 0) {
        console_music_rescan();
    } else if (_stricmp(cmd, "music.play") == 0) {
        console_music_play(arg);
    } else if (_stricmp(cmd, "music.output_rate") == 0) {
        console_music_output_rate(arg);
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
    } else if (_stricmp(cmd, "net.diag") == 0 || _stricmp(cmd, "net.trouble") == 0) {
        console_run_net_diag();
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
    g_online_cfg.remember_me = 0;
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
    if (g_online_cfg.server_port == 0) g_online_cfg.server_port = ONLINE_DEFAULT_SERVER_PORT;
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
    g_online_cfg.remember_me = g_online_cfg.remember_me ? 1 : 0;
    if (!g_online_cfg.server_host[0]) safe_copy(g_online_cfg.server_host, sizeof(g_online_cfg.server_host), ONLINE_DEFAULT_SERVER_HOST);
    if (!g_online_cfg.peer_host[0]) safe_copy(g_online_cfg.peer_host, sizeof(g_online_cfg.peer_host), "127.0.0.1");
}

static int online_credential_identity_ready(const char* server,
                                            uint16_t port,
                                            const char* username) {
    return server && server[0] && port != 0 && username && username[0];
}

static CredentialExtResult online_delete_remembered_password(const char* server,
                                                              uint16_t port,
                                                              const char* username) {
    char error[256];
    CredentialExtResult result;
    if (!online_credential_identity_ready(server, port, username)) {
        return CREDENTIAL_EXT_INVALID_ARGUMENT;
    }
    error[0] = '\0';
    result = credential_ext_delete_password(server,
                                            (unsigned int)port,
                                            username,
                                            error,
                                            sizeof(error));
    if (result != CREDENTIAL_EXT_OK && result != CREDENTIAL_EXT_NOT_FOUND) {
        LOG_WARN("online.credentials: delete failed server=%s:%u user=%s result=%s (%s)",
                 server,
                 (unsigned int)port,
                 username,
                 credential_ext_result_name(result),
                 error[0] ? error : "unknown error");
    }
    return result;
}

static CredentialExtResult online_load_remembered_password(int announce) {
    char error[256];
    size_t password_len = 0;
    CredentialExtResult result;
    CredentialExtResult delete_result = CREDENTIAL_EXT_NOT_FOUND;
    int invalid_saved_record = 0;

    online_clear_password_memory();
    if (!g_online_cfg.remember_me ||
        !online_credential_identity_ready(g_online_cfg.server_host,
                                          g_online_cfg.server_port,
                                          g_online_cfg.username)) {
        return CREDENTIAL_EXT_NOT_FOUND;
    }

    error[0] = '\0';
    result = credential_ext_read_password(g_online_cfg.server_host,
                                          (unsigned int)g_online_cfg.server_port,
                                          g_online_cfg.username,
                                          g_online_cfg.password,
                                          sizeof(g_online_cfg.password),
                                          &password_len,
                                          error,
                                          sizeof(error));
    if (result == CREDENTIAL_EXT_OK && password_len > 0) {
        g_online_password_from_credential = 1;
        LOG_INFO("online.credentials: loaded remembered login server=%s:%u user=%s",
                 g_online_cfg.server_host,
                 (unsigned int)g_online_cfg.server_port,
                 g_online_cfg.username);
        if (announce) {
            online_hub_set_status("Remember me enabled.");
        }
        return result;
    }

    if (result == CREDENTIAL_EXT_OK) {
        result = CREDENTIAL_EXT_MALFORMED_CREDENTIAL;
        safe_copy(error, sizeof(error), "saved password was empty");
    }
    if (result == CREDENTIAL_EXT_MALFORMED_CREDENTIAL ||
        result == CREDENTIAL_EXT_BUFFER_TOO_SMALL) {
        invalid_saved_record = 1;
        delete_result = online_delete_remembered_password(g_online_cfg.server_host,
                                                           g_online_cfg.server_port,
                                                           g_online_cfg.username);
    }
    online_clear_password_memory();
    if (result != CREDENTIAL_EXT_NOT_FOUND) {
        LOG_WARN("online.credentials: read failed server=%s:%u user=%s result=%s (%s)",
                 g_online_cfg.server_host,
                 (unsigned int)g_online_cfg.server_port,
                 g_online_cfg.username,
                 credential_ext_result_name(result),
                 error[0] ? error : "unknown error");
        if (announce && invalid_saved_record &&
            delete_result != CREDENTIAL_EXT_OK &&
            delete_result != CREDENTIAL_EXT_NOT_FOUND) {
            online_hub_set_status("Remembered login could not be reset; enter your password manually.");
        } else if (announce && invalid_saved_record) {
            online_hub_set_status("Remembered login is unavailable; enter your password manually.");
        } else if (announce) {
            online_hub_set_status("Remembered login is unavailable; enter your password manually.");
        }
    } else if (announce) {
        online_hub_set_status("Remember me enabled.");
    }
    return result;
}

static CredentialExtResult online_store_remembered_password(char* error,
                                                             size_t error_cap) {
    if (error && error_cap > 0) error[0] = '\0';
    if (!g_online_cfg.remember_me || !g_online_cfg.password[0] ||
        !online_credential_identity_ready(g_online_cfg.server_host,
                                          g_online_cfg.server_port,
                                          g_online_cfg.username)) {
        return CREDENTIAL_EXT_INVALID_ARGUMENT;
    }
    return credential_ext_write_password(g_online_cfg.server_host,
                                         (unsigned int)g_online_cfg.server_port,
                                         g_online_cfg.username,
                                         g_online_cfg.password,
                                         error,
                                         error_cap);
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
        else if (_stricmp(key, "remember_me") == 0 && online_parse_long_range(value, 0, 1, &parsed)) g_online_cfg.remember_me = (int)parsed;
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
    if (g_online_cfg.remember_me) {
        (void)online_load_remembered_password(0);
    }
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
    fprintf(f, "# Eggnogg+ built-in online hub config v2\n");
    fprintf(f, "username=%s\n", g_online_cfg.username);
    fprintf(f, "remember_me=%d\n", g_online_cfg.remember_me ? 1 : 0);
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

static int online_json_get_string(const char* json, const char* key, char* out, size_t out_sz) {
    return online_control_json_get_string(json, key, out, out_sz) ==
           ONLINE_CONTROL_JSON_OK;
}

static int online_json_get_int(const char* json, const char* key, int* out) {
    return online_control_json_get_int(json, key, out) == ONLINE_CONTROL_JSON_OK;
}

static int online_server_protocol_compatible(const char* json) {
    int control_protocol = 0;
    int match_protocol = 0;
    int p2p_protocol = 0;
    int packet_auth = 0;
    int social_controls = 0;
    int private_rematch = 0;
    int p2p_relay = 0;
    return online_json_get_int(json, "control_protocol", &control_protocol) &&
           online_json_get_int(json, "match_protocol", &match_protocol) &&
           online_json_get_int(json, "p2p_protocol", &p2p_protocol) &&
           online_json_get_int(json, "cap_p2p_auth", &packet_auth) &&
           online_json_get_int(json, "cap_social_controls", &social_controls) &&
           online_json_get_int(json, "cap_private_rematch", &private_rematch) &&
           online_json_get_int(json, "cap_p2p_relay", &p2p_relay) &&
           control_protocol == ONLINE_CONTROL_PROTOCOL_VERSION &&
           match_protocol == ONLINE_MATCH_PROTOCOL_VERSION &&
           p2p_protocol == (int)GGPO_NET_PROTOCOL_VERSION &&
           packet_auth == 1 &&
           social_controls == 1 &&
           private_rematch == 1 &&
           p2p_relay == 1;
}

static int online_match_protocol_compatible(const char* json) {
    int match_protocol = 0;
    int p2p_protocol = 0;
    return online_json_get_int(json, "match_protocol", &match_protocol) &&
           online_json_get_int(json, "p2p_protocol", &p2p_protocol) &&
           match_protocol == ONLINE_MATCH_PROTOCOL_VERSION &&
           p2p_protocol == (int)GGPO_NET_PROTOCOL_VERSION;
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

static void online_server_heartbeat_reset(void) {
    g_online_server_heartbeat_deadline_ms = 0;
    g_online_server_heartbeat_seq = 0;
    g_online_server_heartbeat_acked_seq = 0;
}

static void online_server_heartbeat_arm(uint32_t now) {
    g_online_server_heartbeat_deadline_ms = (DWORD)online_control_deadline_after(
        now, ONLINE_SERVER_HEARTBEAT_MS);
}

/* Node's control socket has a receive-idle timeout. Keep an authenticated hub
 * or long-running gameplay session alive even when the player has no other
 * control-plane traffic. A heartbeat is considered sent only after net_send
 * atomically accepts the complete line; backpressure retries on the next pump. */
static void online_server_heartbeat_tick(uint32_t now) {
    char line[96];
    int next_seq;
    if (g_online_server_state != ONLINE_SERVER_CONNECTED ||
        g_online_server_slot < 0 || !g_online_authed) {
        return;
    }
    if (g_online_server_heartbeat_deadline_ms == 0u) {
        online_server_heartbeat_arm(now);
        return;
    }
    if (!online_control_deadline_reached(
            now, (uint32_t)g_online_server_heartbeat_deadline_ms)) {
        return;
    }
    next_seq = (g_online_server_heartbeat_seq >= 0x7FFFFFFF)
        ? 1
        : g_online_server_heartbeat_seq + 1;
    snprintf(line, sizeof(line),
             "{\"type\":\"ping\",\"seq\":%d}\n",
             next_seq);
    if (online_server_send_raw(line)) {
        g_online_server_heartbeat_seq = next_seq;
        online_server_heartbeat_arm(now);
    }
}

static int online_server_send_map_manifest(void) {
    char* maps_json = NULL;
    char* line = NULL;
    char lan_host[64];
    char lan_json[96];
    int needed;
    int built;
    int formatted;
    int sent = 0;
    size_t line_cap;

    /* Count first, then allocate exactly. A truncated JSON prefix must never be
     * put on the control stream: net_send accepts a logical message atomically,
     * and this cap leaves ample space in its 128 KiB copied output queue. */
    needed = custom_maps_build_manifest_json(NULL, 0);
    if (needed < 0 || (size_t)needed > ONLINE_MAP_MANIFEST_MAX_BYTES) {
        LOG_ERROR("online.maps: manifest size %d exceeds the %lu-byte control limit",
                  needed,
                  (unsigned long)ONLINE_MAP_MANIFEST_MAX_BYTES);
        online_hub_set_status("Installed map list is too large for online play.");
        return 0;
    }
    maps_json = (char*)malloc((size_t)needed + 1u);
    line_cap = (size_t)needed + 512u;
    line = (char*)malloc(line_cap);
    if (!maps_json || !line) {
        LOG_ERROR("online.maps: could not allocate complete manifest message (%d bytes)", needed);
        online_hub_set_status("Could not prepare the installed map list.");
        goto done;
    }
    built = custom_maps_build_manifest_json(maps_json, (size_t)needed + 1u);
    if (built != needed) {
        LOG_WARN("online.maps: registry changed while building manifest (%d -> %d bytes)",
                 needed,
                 built);
        online_hub_set_status("Installed maps changed; reconnect to try again.");
        goto done;
    }

    lan_host[0] = '\0';
    lan_json[0] = '\0';
    if (net_local_ipv4(lan_host, sizeof(lan_host)) && lan_host[0]) {
        online_json_escape(lan_json, sizeof(lan_json), lan_host);
    }
    formatted = snprintf(line,
                         line_cap,
                         "{\"type\":\"map_manifest\",\"p2p_port\":%u,\"lan_host\":\"%s\",\"route_version\":2,\"maps\":%s}\n",
                         (unsigned int)g_online_cfg.local_port,
                         lan_json,
                         maps_json);
    if (formatted < 0 || (size_t)formatted >= line_cap) {
        LOG_ERROR("online.maps: complete manifest envelope exceeded its checked allocation");
        online_hub_set_status("Could not prepare the installed map list.");
        goto done;
    }
    sent = online_server_send_raw(line);
    if (!sent) {
        LOG_WARN("online.maps: complete manifest message was not accepted by the control queue");
        online_hub_set_status("Could not send the installed map list.");
    }

done:
    free(line);
    free(maps_json);
    return sent;
}

static void online_server_send_auth(int register_account) {
    char user[320];
    char pass[1537];
    char line[2048];
    int sent;
    online_json_escape(user, sizeof(user), g_online_cfg.username);
    online_json_escape(pass, sizeof(pass), g_online_cfg.password);
    snprintf(line, sizeof(line), "{\"type\":\"%s\",\"username\":\"%s\",\"password\":\"%s\"}\n",
             register_account ? "register" : "login", user, pass);
    sent = online_server_send_raw(line);
    credential_ext_secure_zero(pass, sizeof(pass));
    credential_ext_secure_zero(line, sizeof(line));
    credential_ext_secure_zero(user, sizeof(user));
    if (!sent) {
        LOG_WARN("online.credentials: authentication request could not be sent");
        if (g_online_server_state != ONLINE_SERVER_DISCONNECTED) {
            online_server_disconnect("Authentication request could not be sent.");
        }
    }
}

static void online_server_disconnect(const char* reason) {
    int match_interrupted = g_online_pending_match.active ||
                            (g_online_active_match.active && !g_online_active_match.result_reported);
    int discard_auth_secret = g_online_auth_pending ||
                              g_online_server_state == ONLINE_SERVER_CONNECTING;
    if (g_online_server_slot >= 0) net_close(g_online_server_slot);
    g_online_server_slot = -1;
    g_online_server_state = ONLINE_SERVER_DISCONNECTED;
    g_online_authed = 0;
    g_online_auth_pending = 0;
    g_online_server_info_pending = 0;
    g_online_friend_snapshot_complete = 0;
    g_online_server_deadline_ms = 0;
    online_server_heartbeat_reset();
    g_online_recv_len = 0;
    g_online_queue_mode = 0;
    online_challenge_map_picker_clear();
    if (discard_auth_secret || !is_online_hub_state_active()) {
        online_clear_password_memory();
    }
    if (match_interrupted) {
        online_handle_server_match_disconnect(reason);
        return;
    }
    if (g_online_result.active && !g_online_result.server_confirmed) {
        /* No control connection remains that could confirm this exact report.
         * Drop the provisional toast/identity instead of blocking requeue or
         * later mistaking an unrelated result for this match. */
        memset(&g_online_result, 0, sizeof(g_online_result));
    }
    if (online_result_has_live_rematch()) {
        online_result_rematch_clear("Rematch unavailable while disconnected.");
    }
    if (reason && reason[0]) online_hub_set_status(reason);
}

static void online_server_connect(int register_account) {
    online_hub_save();
    online_server_disconnect(NULL);
    if (!register_account && !g_online_cfg.password[0] && g_online_cfg.remember_me) {
        (void)online_load_remembered_password(0);
    }
    if (!g_online_cfg.username[0] || !g_online_cfg.password[0]) {
        online_hub_set_status("Enter username and password on Play.");
        return;
    }
    g_online_register_after_connect = register_account ? 1 : 0;
    g_online_auth_pending = 1;
    g_online_server_slot = net_connect(g_online_cfg.server_host, g_online_cfg.server_port);
    if (g_online_server_slot < 0) {
        online_server_disconnect("Could not start server connection.");
        return;
    }
    g_online_server_state = ONLINE_SERVER_CONNECTING;
    g_online_server_deadline_ms = (DWORD)online_control_deadline_after(
        (uint32_t)GetTickCount(), ONLINE_SERVER_CONNECT_TIMEOUT_MS);
    online_hub_set_status("Connecting to online server...");
}

/* A remembered login is the only path allowed to authenticate without an
 * explicit LOG IN activation. Keep the source flag in the predicate so a
 * manually typed password can never be submitted merely by opening the hub. */
static int online_remembered_login_in_progress(void) {
    return !g_online_authed &&
           g_online_auth_pending &&
           !g_online_register_after_connect &&
           g_online_password_from_credential;
}

static int online_try_remembered_login(void) {
    CredentialExtResult result;
    if (g_online_authed || g_online_auth_pending ||
        g_online_server_state != ONLINE_SERVER_DISCONNECTED ||
        !g_online_cfg.remember_me || !g_online_cfg.username[0]) {
        return 0;
    }
    if (!g_online_cfg.password[0]) {
        result = online_load_remembered_password(0);
        if (result != CREDENTIAL_EXT_OK) return 0;
    }
    if (!g_online_password_from_credential || !g_online_cfg.password[0]) return 0;
    online_server_connect(0);
    return online_remembered_login_in_progress();
}

static int online_result_has_live_rematch(void) {
    return g_online_result.active &&
           g_online_result.server_confirmed &&
           g_online_result.match_id > 0 &&
           g_online_result.rematch_state != ONLINE_REMATCH_NONE;
}

static void online_result_rematch_arm(int expires_in) {
    uint32_t delay_ms;
    if (expires_in < 1) expires_in = 1;
    if (expires_in > 300) expires_in = 300;
    delay_ms = (uint32_t)expires_in * 1000u;
    g_online_result.rematch_deadline_ms = (DWORD)online_control_deadline_after(
        (uint32_t)GetTickCount(), delay_ms);
}

static void online_result_rematch_clear(const char* status) {
    g_online_result.rematch_state = ONLINE_REMATCH_NONE;
    g_online_result.rematch_deadline_ms = 0;
    g_online_result.rematch_unranked = 0;
    if (status && status[0]) {
        safe_copy(g_online_result.status, sizeof(g_online_result.status), status);
        online_hub_set_status(status);
    }
}

static int online_server_send_rematch_request(void) {
    char line[128];
    if (!online_result_has_live_rematch() ||
        (g_online_result.rematch_state != ONLINE_REMATCH_AVAILABLE &&
         g_online_result.rematch_state != ONLINE_REMATCH_OFFERED)) {
        return 0;
    }
    if (!g_online_authed || g_online_server_state != ONLINE_SERVER_CONNECTED) {
        online_result_rematch_clear("Rematch unavailable while disconnected.");
        return 0;
    }
    snprintf(line, sizeof(line),
             "{\"type\":\"rematch_request\",\"match_id\":%d}\n",
             g_online_result.match_id);
    if (!online_server_send_raw(line)) return 0;
    g_online_result.rematch_state = ONLINE_REMATCH_WAITING;
    safe_copy(g_online_result.status, sizeof(g_online_result.status),
              "Rematch requested; waiting for opponent.");
    online_hub_set_status("Rematch requested; waiting for opponent.");
    return 1;
}

static int online_server_send_rematch_decline(void) {
    char line[128];
    if (!online_result_has_live_rematch()) return 0;
    if (!g_online_authed || g_online_server_state != ONLINE_SERVER_CONNECTED) return 0;
    snprintf(line, sizeof(line),
             "{\"type\":\"rematch_decline\",\"match_id\":%d}\n",
             g_online_result.match_id);
    return online_server_send_raw(line);
}

static void online_server_send_queue(const char* queue) {
    char line[128];
    if (!g_online_authed) {
        online_hub_set_status("Log in before joining a queue.");
        return;
    }
    if (g_online_result.active && !g_online_result.server_confirmed) {
        online_hub_set_status("Waiting for the server result before requeueing.");
        return;
    }
    if (g_online_result.rematch_state == ONLINE_REMATCH_STARTING) {
        online_hub_set_status("Private rematch is already starting.");
        return;
    }
    if (online_result_has_live_rematch()) {
        (void)online_server_send_rematch_decline();
        online_result_dismiss(0);
    }
    snprintf(line, sizeof(line), "{\"type\":\"join_queue\",\"queue\":\"%s\"}\n", queue ? queue : "casual");
    if (!online_server_send_raw(line)) return;
    g_online_queue_mode = (queue && _stricmp(queue, "competitive") == 0) ? 2 : 1;
    online_hub_set_status(g_online_queue_mode == 2 ? "Joining competitive queue..." : "Joining casual queue...");
}

static void online_server_leave_queue(void) {
    online_server_send_raw("{\"type\":\"leave_queue\"}\n");
    g_online_queue_mode = 0;
    online_hub_set_status("Left queue.");
}

static void online_challenge_map_picker_clear(void) {
    free(g_online_challenge_map_picker.choices);
    memset(&g_online_challenge_map_picker, 0, sizeof(g_online_challenge_map_picker));
}

static int online_challenge_map_picker_matches(const char* username, int request_id) {
    return g_online_challenge_map_picker.active &&
           request_id == g_online_challenge_map_picker.request_id &&
           username && username[0] &&
           _stricmp(username, g_online_challenge_map_picker.username) == 0;
}

static int online_challenge_map_picker_add(const char* key, const char* label) {
    OnlineChallengeMapPicker* picker = &g_online_challenge_map_picker;
    OnlineMapChoice* grown;
    int next_capacity;
    if (!picker->active || !picker->loading || !key || !key[0]) return 0;
    if (picker->choice_count >= ONLINE_CHALLENGE_MAP_MAX) return 0;
    if (picker->choice_count >= picker->choice_capacity) {
        next_capacity = picker->choice_capacity ? picker->choice_capacity * 2 : 32;
        if (next_capacity > ONLINE_CHALLENGE_MAP_MAX) next_capacity = ONLINE_CHALLENGE_MAP_MAX;
        grown = (OnlineMapChoice*)realloc(
            picker->choices, (size_t)next_capacity * sizeof(*picker->choices));
        if (!grown) return 0;
        picker->choices = grown;
        picker->choice_capacity = next_capacity;
    }
    memset(&picker->choices[picker->choice_count], 0, sizeof(picker->choices[picker->choice_count]));
    safe_copy(picker->choices[picker->choice_count].key,
              sizeof(picker->choices[picker->choice_count].key),
              key);
    safe_copy(picker->choices[picker->choice_count].label,
              sizeof(picker->choices[picker->choice_count].label),
              (label && label[0]) ? label : key);
    picker->choice_count++;
    return 1;
}

static int online_challenge_map_picker_begin(const char* username) {
    char user[128];
    char line[320];
    int request_id;
    if (!g_online_authed || !username || !username[0]) return 0;
    online_challenge_map_picker_clear();
    request_id = g_online_challenge_map_request_serial + 1;
    if (request_id <= 0 || request_id >= 0x7fffffff) request_id = 1;
    g_online_challenge_map_request_serial = request_id;
    g_online_challenge_map_picker.active = 1;
    g_online_challenge_map_picker.loading = 1;
    g_online_challenge_map_picker.request_id = request_id;
    g_online_challenge_map_picker.expected_count = -1;
    safe_copy(g_online_challenge_map_picker.username,
              sizeof(g_online_challenge_map_picker.username),
              username);
    online_json_escape(user, sizeof(user), username);
    snprintf(line, sizeof(line),
             "{\"type\":\"challenge_maps\",\"username\":\"%s\",\"request_id\":%d}\n",
             user,
             request_id);
    if (!online_server_send_raw(line)) {
        online_challenge_map_picker_clear();
        online_hub_set_status("Could not request compatible maps.");
        return 0;
    }
    online_hub_set_status("");
    online_hub_rebuild_rows();
    return 1;
}

static int online_challenge_map_picker_send(void) {
    OnlineChallengeMapPicker* picker = &g_online_challenge_map_picker;
    OnlineMapChoice* choice;
    char user[128];
    char key[256];
    char line[512];
    if (!picker->active || picker->loading ||
        picker->choice_count <= 0 ||
        picker->selected < 0 || picker->selected >= picker->choice_count) {
        online_hub_set_status("Choose a compatible map first.");
        return 0;
    }
    choice = &picker->choices[picker->selected];
    online_json_escape(user, sizeof(user), picker->username);
    online_json_escape(key, sizeof(key), choice->key);
    snprintf(line, sizeof(line),
             "{\"type\":\"challenge\",\"username\":\"%s\",\"map_key\":\"%s\"}\n",
             user,
             key);
    if (!online_server_send_raw(line)) {
        online_hub_set_status("Could not send challenge.");
        return 0;
    }
    online_challenge_map_picker_clear();
    online_hub_set_status("Sending challenge...");
    online_hub_rebuild_rows();
    return 1;
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

static void online_server_send_mute_action(const char* username, int muted) {
    char user[128];
    char line[256];
    if (!g_online_authed) {
        online_hub_set_status("Log in first.");
        return;
    }
    online_json_escape(user, sizeof(user), username ? username : "");
    snprintf(line, sizeof(line),
             "{\"type\":\"friend_mute\",\"username\":\"%s\",\"muted\":%d}\n",
             user,
             muted ? 1 : 0);
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
    int match_id = g_online_active_match.match_id;
    if (result == ONLINE_MATCH_RESULT_WIN) text = "win";
    else if (result == ONLINE_MATCH_RESULT_LOSS) text = "loss";
    if (match_id <= 0 || !g_online_active_match.server_committed) return;
    if (!g_online_authed || g_online_server_state != ONLINE_SERVER_CONNECTED) return;
    if (g_online_active_match.winner_player >= 0 &&
        g_online_active_match.winner_player <= 1) {
        snprintf(line, sizeof(line),
                 "{\"type\":\"match_end\",\"match_id\":%d,\"result\":\"%s\",\"winner_player\":%d}\n",
                 match_id,
                 text,
                 g_online_active_match.winner_player);
    } else {
        snprintf(line, sizeof(line),
                 "{\"type\":\"match_end\",\"match_id\":%d,\"result\":\"%s\"}\n",
                 match_id,
                 text);
    }
    online_server_send_raw(line);
}

static void online_server_send_match_started(void) {
    char line[128];
    int match_id = g_online_pending_match.match_id;
    if (!g_online_pending_match.active || match_id <= 0 ||
        g_online_pending_match.server_start_reported) return;
    if (!g_online_authed || g_online_server_state != ONLINE_SERVER_CONNECTED) return;
    snprintf(line, sizeof(line),
             "{\"type\":\"match_started\",\"match_id\":%d}\n",
             match_id);
    if (online_server_send_raw(line)) {
        g_online_pending_match.server_start_reported = 1;
    }
}

static void online_server_send_match_abort(const char* reason) {
    char escaped[192];
    char line[320];
    int match_id = g_online_pending_match.active
        ? g_online_pending_match.match_id
        : g_online_active_match.match_id;
    if (match_id <= 0) return;
    if (!g_online_authed || g_online_server_state != ONLINE_SERVER_CONNECTED) return;
    online_json_escape(escaped, sizeof(escaped),
                       (reason && reason[0]) ? reason : "match setup aborted");
    snprintf(line, sizeof(line),
             "{\"type\":\"match_abort\",\"match_id\":%d,\"reason\":\"%s\"}\n",
             match_id,
             escaped);
    online_server_send_raw(line);
}

static void online_result_prepare(OnlineMatchResult result, const char* status) {
    memset(&g_online_result, 0, sizeof(g_online_result));
    g_online_result.active = 1;
    g_online_result.toast_visible = 1;
    g_online_result.match_id = g_online_active_match.match_id;
    g_online_result.result = result;
    g_online_result.toast_age = 0;
    g_online_result.toast_lifetime = ONLINE_RESULT_TOAST_FRAMES;
    g_online_result.server_confirmed = 0;
    g_online_result.competitive = g_online_active_match.competitive;
    g_online_result.elo_before = g_online_public_elo;
    g_online_result.elo_after = g_online_public_elo;
    safe_copy(g_online_result.opponent, sizeof(g_online_result.opponent), g_online_active_match.opponent);
    safe_copy(g_online_result.map_label, sizeof(g_online_result.map_label), g_online_active_match.map_label);
    safe_copy(g_online_result.status, sizeof(g_online_result.status), status ? status : "Waiting for server result...");
}

static int online_native_finish_is_presenting(void) {
    void* state;
    if (!g_online_active_match.active ||
        !g_online_active_match.server_committed) {
        return 0;
    }
    state = p_state_current ? p_state_current() : NULL;
    if (state != (void*)(uintptr_t)ADDR_GAME_STATE &&
        !online_state_is_ingame_menu(state)) {
        return 0;
    }
    /* The client result flag can lag the native terminal state by one frame,
     * and the countdown reaches zero inside the same call which switches GAME
     * to MAIN. Either retained signal therefore means the presentation still
     * owns the screen. */
    return g_online_active_match.awaiting_native_return ||
           (g_game_end_countdown && *g_game_end_countdown > 0) ||
           online_native_winner_player() >= 0;
}

static void online_return_to_hub_after_match(const char* status) {
    void* main_state = (void*)(uintptr_t)ADDR_MAIN_STATE;
    int already_in_hub = is_online_hub_state_active();
    if (online_native_finish_is_presenting()) {
        g_online_active_match.awaiting_native_return = 1;
        if (status && status[0]) {
            safe_copy(g_online_active_match.completion_status,
                      sizeof(g_online_active_match.completion_status),
                      status);
        }
        /* A previously queued handoff is just as dangerous as a fresh one:
         * pre-swap would otherwise cover the native win scene with the hub. */
        g_online_open_pending = 0;
        LOG_INFO("online.match: deferred hub handoff until native GAME returns to MAIN");
        return;
    }
    g_online_queue_mode = 0;
    g_online_tab = ONLINE_TAB_PLAY;
    online_clear_capture_state();
    g_online_context_active = 0;
    lua_manager_online_suspend_end();
    if (status && status[0]) online_hub_set_status(status);

    /* A completed game has no live native GAME state to return to. Queue the
     * normal hub handoff, then force its Back owner to main so neither an old
     * gameplay state nor any framework overlay can be resurrected. */
    online_hub_open();
    g_online_return_state = main_state;
    g_online_pending_return_state = main_state;
    g_online_force_main_return_once = already_in_hub ? 0 : 1;
}

static void online_active_match_capture_from_pending(void) {
    memset(&g_online_active_match, 0, sizeof(g_online_active_match));
    g_online_active_match.active = 1;
    g_online_active_match.winner_player = -1;
    g_online_active_match.match_id = g_online_pending_match.match_id;
    g_online_active_match.local_player = ggpo_net_local_player();
    if (g_online_active_match.local_player < 0 || g_online_active_match.local_player > 1) {
        g_online_active_match.local_player = clampi(g_online_pending_match.role, 0, 1);
    }
    g_online_active_match.competitive = g_online_pending_match.competitive;
    g_online_active_match.queue_mode = g_online_pending_match.queue_mode;
    g_online_active_match.server_committed = g_online_pending_match.server_committed;
    safe_copy(g_online_active_match.p2p_token, sizeof(g_online_active_match.p2p_token), g_online_pending_match.p2p_token);
    safe_copy(g_online_active_match.opponent, sizeof(g_online_active_match.opponent), g_online_pending_match.opponent);
    safe_copy(g_online_active_match.map_label, sizeof(g_online_active_match.map_label), g_online_pending_match.map_label);
    /* No retry can occur after a server commit, whether GAME is entered or the
     * server immediately resolves a committed disconnect. Retain only ggpo_net's
     * in-session derived keys long enough for the caller to stop the socket. */
    online_p2p_auth_tokens_clear();
}

static void online_active_match_begin_from_pending(void) {
    online_clear_waterfall_audio_state("match begin");
    online_active_match_capture_from_pending();
    g_online_connect.connected_once = 1;
    g_online_connect.established = 1;
    online_viewport_poll("match-start", 1);
}

static void online_finish_active_match(OnlineMatchResult result, const char* status, int send_report) {
    if (!g_online_active_match.active) return;
    if (!g_online_active_match.server_committed) {
        online_server_send_match_abort(status);
        if (ggpo_net_active()) stop_ggpo_net("uncommitted online match ended");
        online_clear_match_state();
        online_hub_set_status((status && status[0]) ? status : "Match setup ended.");
        online_hub_open();
        return;
    }
    if (online_native_finish_is_presenting()) {
        g_online_active_match.awaiting_native_return = 1;
    }
    g_online_active_match.result = result;
    safe_copy(g_online_active_match.completion_status,
              sizeof(g_online_active_match.completion_status),
              status ? status : "Match complete.");
    if (send_report && !g_online_active_match.result_reported) {
        g_online_active_match.result_reported = 1;
        online_server_send_match_end(result);
    }
    if (g_online_active_match.awaiting_native_return) {
        if (!g_online_result.active) {
            online_result_prepare(result, status);
        }
        return;
    }
    if (ggpo_net_active()) stop_ggpo_net("online match complete");
    online_connect_reset();
    online_pending_match_reset();
    online_result_prepare(result, status);
    online_return_to_hub_after_match(status);
}

/* A deliberate local exit is stronger evidence than an ordinary independently
 * detected win/loss. Report a committed match_abort so the server can award the
 * still-connected opponent immediately. The compact local loss remains
 * provisional until the exact server result arrives. */
static void online_forfeit_active_match(const char* status, const char* reason) {
    if (!g_online_active_match.active) return;
    if (!g_online_active_match.server_committed) {
        online_finish_active_match(ONLINE_MATCH_RESULT_LOSS, status, 0);
        return;
    }
    g_online_active_match.result = ONLINE_MATCH_RESULT_LOSS;
    if (!g_online_active_match.result_reported) {
        g_online_active_match.result_reported = 1;
        online_server_send_match_abort((reason && reason[0])
            ? reason
            : "player left committed match");
    }
    if (ggpo_net_active()) stop_ggpo_net("local online match forfeit");
    online_connect_reset();
    online_pending_match_reset();
    online_result_prepare(ONLINE_MATCH_RESULT_LOSS, status);
    online_return_to_hub_after_match(status);
}

static void online_clear_match_state(void) {
    online_pending_match_reset();
    memset(&g_online_active_match, 0, sizeof(g_online_active_match));
    online_connect_reset();
    lua_manager_online_suspend_end();
}

/* A fresh retry socket is useful only while the matchmaking connection can
 * publish its new endpoint. If that control connection disappears, terminate
 * the server-managed match instead of spending the remaining attempts on
 * unreachable ephemeral ports. */
static void online_handle_server_match_disconnect(const char* reason) {
    const char* status = (reason && reason[0])
        ? reason
        : "Online server disconnected; match ended.";
    int show_result = g_online_active_match.active &&
                      g_online_connect.established &&
                      g_online_active_match.server_committed;

    LOG_WARN("online.match: matchmaking connection lost during match id=%d established=%d (%s)",
             g_online_active_match.active ? g_online_active_match.match_id
                                          : g_online_pending_match.match_id,
             g_online_connect.established,
             status);

    if (online_native_finish_is_presenting()) {
        g_online_active_match.awaiting_native_return = 1;
        if (!g_online_active_match.result_reported) {
            online_match_poll_completion();
        }
        /* A malformed terminal state must still never fall through to the
         * immediate teardown path merely because the control socket vanished. */
        g_online_active_match.result_reported = 1;
        safe_copy(g_online_active_match.completion_status,
                  sizeof(g_online_active_match.completion_status),
                  status);
        g_online_pending_connect_fail_status = 2;
        LOG_INFO("online.match: preserving native finish despite control-server disconnect");
        return;
    }

    if (ggpo_net_active()) {
        stop_ggpo_net(show_result
            ? "online server disconnected during committed match"
            : "online server disconnected before gameplay commit");
    }
    online_clear_match_state();
    g_online_pending_connect_fail_status = 2;
    online_hub_set_status(status);
    online_hub_open();
}

/* `ggpo.net off` is an explicit cancellation, not a failed attempt. Resolve the
 * server match and clear retry state so the next frame cannot resurrect P2P. */
static void online_cancel_match_from_console(void) {
    if (g_online_active_match.active) {
        if (!g_online_active_match.server_committed) {
            online_server_send_match_abort("local match cancelled before server commit");
            if (ggpo_net_active()) stop_ggpo_net("console cancelled uncommitted online match");
            online_clear_match_state();
            online_hub_set_status("Match setup cancelled.");
            online_hub_open();
            return;
        }
        /* A committed console cancellation is a forfeit. Keep the active match
         * identity alive for the asynchronous server result; completion uses
         * the normal hub plus the compact result toast. */
        online_forfeit_active_match("P2P stopped; forfeit reported.",
                                    "player stopped P2P during committed match");
        return;
    }
    if (g_online_pending_match.active) {
        online_server_send_match_abort("match cancelled before gameplay");
        if (ggpo_net_active()) stop_ggpo_net("console cancelled pending online match");
        online_clear_match_state();
        g_online_pending_connect_fail_status = 3;
        online_hub_open();
        return;
    }
    online_connect_reset();
    stop_ggpo_net("console");
}

static int online_current_match_id(void) {
    if (g_online_pending_match.active && g_online_pending_match.match_id > 0) {
        return g_online_pending_match.match_id;
    }
    if (g_online_active_match.active && g_online_active_match.match_id > 0) {
        return g_online_active_match.match_id;
    }
    if (g_online_result.active && g_online_result.match_id > 0) {
        return g_online_result.match_id;
    }
    return 0;
}

static int online_server_message_matches_current_match(const char* line, const char* type) {
    int message_match_id = 0;
    int current_match_id = online_current_match_id();
    if (!online_json_get_int(line, "match_id", &message_match_id) ||
        message_match_id <= 0 || current_match_id <= 0 ||
        message_match_id != current_match_id) {
        LOG_WARN("online.server: ignored stale %s match=%d current=%d",
                 type ? type : "match message",
                 message_match_id,
                 current_match_id);
        return 0;
    }
    return 1;
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

/* Build the complete deterministic frame-0 state while the match-found card is
 * still on screen. The P2P host snapshots this state when the prematch hold is
 * released; neither peer has to expose a frozen GAME frame while connecting or
 * transferring it. */
static void online_set_prematch_error(char* err, size_t err_cap,
                                      const char* message) {
    if (!err || err_cap == 0) return;
    snprintf(err, err_cap, "%s",
             (message && message[0]) ? message : "prematch setup failed");
    err[err_cap - 1] = '\0';
}

/* A held online match must use the exact script pinned alongside the native
 * room definitions. Pre-reset deactivation below makes this a postcondition of
 * the current map build, so neither a failed content bind nor a stale VM from a
 * prior match can silently fall back to unscripted gameplay. */
static int online_validate_pinned_map_script(int selector,
                                             char* err,
                                             size_t err_cap) {
    uint64_t expected_script_id = 0;
    uint64_t active_script_id = map_script_is_active()
        ? map_script_active_id() : 0;
    int pinned = custom_maps_pinned_script_id(selector, &expected_script_id);
    if (pinned < 0) {
        online_set_prematch_error(
            err, err_cap,
            "The selected custom map was not pinned during native setup.");
        LOG_ERROR("online.prematch: selector=%d has no matching pinned custom map",
                  selector);
        return 0;
    }
    if (expected_script_id == 0) {
        if (active_script_id == 0) return 1;
        online_set_prematch_error(
            err, err_cap,
            "An unexpected map.lua remained active during native setup.");
        LOG_ERROR("online.prematch: selector=%d expected no script but active=%016llx",
                  selector, (unsigned long long)active_script_id);
        return 0;
    }
    if (!map_script_is_active() || map_script_is_faulted() ||
        active_script_id != expected_script_id) {
        online_set_prematch_error(
            err, err_cap,
            "The selected map's required map.lua failed to bind or start.");
        LOG_ERROR("online.prematch: selector=%d required script unavailable expected=%016llx active=%016llx faulted=%d",
                  selector,
                  (unsigned long long)expected_script_id,
                  (unsigned long long)active_script_id,
                  map_script_is_faulted());
        return 0;
    }
    return 1;
}

static int online_prepare_pending_match_state(char* err, size_t err_cap) {
    if (err && err_cap) err[0] = '\0';
    if (!g_online_pending_match.active) {
        online_set_prematch_error(err, err_cap, "No online match is pending.");
        return 0;
    }
    if (g_online_pending_match.prematch_prepared) return 1;
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
    if (!p_game_reset) {
        LOG_ERROR("online.prematch: game_reset is unavailable");
        online_set_prematch_error(err, err_cap,
                                  "Native match reset is unavailable.");
        return 0;
    }
    custom_maps_deactivate_script();
    p_game_reset();
    /* Run the native room/start-countdown initialization before the host's
     * authoritative capture. GAME enter will then only resume/layout the exact
     * synchronized state after the hub-to-game switch. */
    {
        int old_synth_enabled = hooks_set_native_synth_enabled(0);
        online_ensure_native_game_started();
        hooks_set_native_synth_enabled(old_synth_enabled);
    }
    if (!online_validate_pinned_map_script(g_online_pending_match.selector,
                                           err, err_cap)) {
        return 0;
    }
    if (!ggpo_net_finalize_state_layout(err, err_cap)) {
        LOG_ERROR("online.prematch: final rollback layout failed (%s)",
                  (err && err[0]) ? err : "unknown error");
        if (!err || !err_cap || !err[0]) {
            online_set_prematch_error(err, err_cap,
                                      "The rollback state layout could not be finalized.");
        }
        return 0;
    }
    g_online_pending_match.prematch_prepared = 1;
    LOG_INFO("online.prematch: deterministic match state prepared in countdown match=%d map=%d seed=%u",
             g_online_pending_match.match_id,
             g_online_pending_match.selector,
             g_online_pending_match.seed);
    return 1;
}

static int online_apply_synchronized_player_palettes(char* err, size_t err_cap) {
    uint32_t local_skin = 0u;
    uint32_t local_clothing = 0u;
    uint32_t local_count = 0u;
    uint32_t remote_skin = 0u;
    uint32_t remote_clothing = 0u;
    uint32_t remote_count = 0u;
    int local_player = ggpo_net_local_player();
    int remote_player = ggpo_net_remote_player();
    if ((local_player != 0 && local_player != 1) ||
        (remote_player != 0 && remote_player != 1) ||
        local_player == remote_player ||
        !ggpo_net_palette_ready() ||
        !ggpo_net_local_palette_preference(&local_skin,
                                           &local_clothing,
                                           &local_count) ||
        !ggpo_net_remote_palette_preference(&remote_skin,
                                            &remote_clothing,
                                            &remote_count) ||
        local_count != remote_count ||
        local_count != (uint32_t)hooks_player_colour_count()) {
        online_set_prematch_error(err, err_cap,
                                  "Player palette synchronization was not ready.");
        return 0;
    }
    if (hooks_set_player_colour_index(local_player, 0, (int)local_skin) !=
            (int)local_skin ||
        hooks_set_player_colour_index(local_player, 1, (int)local_clothing) !=
            (int)local_clothing ||
        hooks_set_player_colour_index(remote_player, 0, (int)remote_skin) !=
            (int)remote_skin ||
        hooks_set_player_colour_index(remote_player, 1, (int)remote_clothing) !=
            (int)remote_clothing) {
        online_set_prematch_error(err, err_cap,
                                  "Player palette choices could not be applied.");
        return 0;
    }
    LOG_INFO("online.prematch: applied presentation palettes p%d=%u/%u p%d=%u/%u entries=%u",
             local_player + 1,
             (unsigned int)local_skin,
             (unsigned int)local_clothing,
             remote_player + 1,
             (unsigned int)remote_skin,
             (unsigned int)remote_clothing,
             (unsigned int)local_count);
    return 1;
}

/* Start (or restart) a native local match, optionally on a specific map
 * selector - the same recipe the online flow uses to launch matches
 * (selector -> game_reset -> switch to GAME). Exposed to Lua as
 * mod.game.start_match for bot/trainer mods that chain matches. */
int hooks_start_native_match(int selector) {
    if (ggpo_net_active()) return 0;              /* never yank an online match */
    if (g_online_pending_match.active) return 0;
    online_clear_waterfall_audio_state("mod match start");
    if (selector >= 0 && g_hook_map_selector) {
        *g_hook_map_selector = selector;
    }
    if (p_game_reset) {
        p_game_reset();
    }
    if (p_state_switch) {
        p_state_switch((void*)(uintptr_t)ADDR_GAME_STATE);
    }
    return 1;
}

static void online_server_begin_pending_match(const char* line) {
    char text[128];
    char queue[32];
    int value = 0;
    OnlineControlJsonResult map_key_result;
    OnlineControlJsonResult auth_token_result;
    online_pending_match_reset();
    online_connect_reset();
    g_online_pending_match.active = 1;
    g_online_pending_match.setup_started_ms = GetTickCount();
    (void)lua_manager_online_suspend_begin();
    g_online_pending_match.selector = 0;
    g_online_pending_match.input_delay = g_online_cfg.input_delay;
    g_online_pending_match.local_port = g_online_cfg.local_port;

    if (!online_match_protocol_compatible(line)) {
        online_abort_prematch_setup("server sent an incompatible match protocol");
        return;
    }

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
    auth_token_result = online_control_json_get_string(
        line,
        "p2p_auth_token",
        g_online_pending_match.p2p_auth_token,
        sizeof(g_online_pending_match.p2p_auth_token));
    online_json_get_string(line, "opponent", g_online_pending_match.opponent, sizeof(g_online_pending_match.opponent));
    map_key_result = online_control_json_get_string(line,
                                                     "map_key",
                                                     g_online_pending_match.map_key,
                                                     sizeof(g_online_pending_match.map_key));
    online_json_get_string(line, "map_label", g_online_pending_match.map_label, sizeof(g_online_pending_match.map_label));
    queue[0] = '\0';
    online_json_get_string(line, "queue", queue, sizeof(queue));
    if (_stricmp(queue, "competitive") == 0) {
        g_online_pending_match.competitive = 1;
        g_online_pending_match.queue_mode = 2;
    } else if (_stricmp(queue, "casual") == 0) {
        g_online_pending_match.queue_mode = 1;
    }

    if (g_online_pending_match.match_id <= 0) {
        online_abort_prematch_setup("server sent an invalid match id");
        return;
    }
    if (auth_token_result != ONLINE_CONTROL_JSON_OK) {
        online_abort_prematch_setup("server sent missing or invalid match authentication data");
        return;
    }

    if (map_key_result < ONLINE_CONTROL_JSON_NOT_FOUND) {
        online_abort_prematch_setup("server sent an invalid map key");
        return;
    }
    if (map_key_result == ONLINE_CONTROL_JSON_OK &&
        g_online_pending_match.map_key[0]) {
        int selector = 0;
        if (!custom_maps_selector_for_key(g_online_pending_match.map_key, &selector)) {
            online_abort_prematch_setup("selected map is not installed or changed since matchmaking");
            return;
        }
        g_online_pending_match.selector = selector;
    }
    /* A fully validated fresh match supersedes the retained identity and UI of
     * the previous result/rematch. Do this only after map validation so a bad
     * server message cannot erase the actionable confirmed result. */
    memset(&g_online_result, 0, sizeof(g_online_result));
    online_normalize_pending_match_ports();
    g_online_connect.match_id = g_online_pending_match.match_id;
    g_online_connect.local_port = g_online_pending_match.local_port;
    g_online_connect.peer_port = g_online_pending_match.peer_port;
    safe_copy(g_online_connect.p2p_role,
              sizeof(g_online_connect.p2p_role),
              g_online_pending_match.p2p_role);
    safe_copy(g_online_connect.peer_host,
              sizeof(g_online_connect.peer_host),
              g_online_pending_match.peer_host);
    safe_copy(g_online_connect.p2p_token,
              sizeof(g_online_connect.p2p_token),
              g_online_pending_match.p2p_token);
    safe_copy(g_online_connect.p2p_auth_token,
              sizeof(g_online_connect.p2p_auth_token),
              g_online_pending_match.p2p_auth_token);
    online_hub_apply_net_settings();
    (void)ggpo_net_set_input_delay((uint32_t)clampi(g_online_pending_match.input_delay, 0, GGPO_NET_MAX_INPUT_DELAY));
    snprintf(text, sizeof(text), "Match found: %s on %s",
             g_online_pending_match.opponent[0] ? g_online_pending_match.opponent : "opponent",
             g_online_pending_match.map_label[0] ? g_online_pending_match.map_label : "selected map");
    online_hub_set_status(text);
    g_online_pending_match.launch_countdown_frames = ONLINE_MATCH_COUNTDOWN_FRAMES;
    /* Open the held UDP session now, not after switching to GAME. Connection,
     * retries, RTT sampling and final state sync all run behind the countdown. */
    online_connect_start_attempt(1);
    if (!is_online_hub_state_active()) {
        online_hub_open();
    }
}

static void online_apply_p2p_peer(const char* line) {
    char host[ONLINE_HUB_TEXT_MAX];
    char public_host[ONLINE_HUB_TEXT_MAX];
    char lan_host[ONLINE_HUB_TEXT_MAX];
    char peer_route[32];
    char err[256];
    int match_id = 0;
    int port = 0;
    int public_port = 0;
    int lan_port = 0;
    int applies = 0;
    if (!online_json_get_int(line, "match_id", &match_id)) return;
    if (!online_json_get_int(line, "peer_port", &port)) return;
    host[0] = '\0';
    public_host[0] = '\0';
    lan_host[0] = '\0';
    peer_route[0] = '\0';
    online_json_get_string(line, "peer_host", host, sizeof(host));
    online_json_get_string(line, "peer_route", peer_route, sizeof(peer_route));
    online_json_get_string(line, "peer_public_host", public_host, sizeof(public_host));
    online_json_get_int(line, "peer_public_port", &public_port);
    online_json_get_string(line, "peer_lan_host", lan_host, sizeof(lan_host));
    online_json_get_int(line, "peer_lan_port", &lan_port);
    if (_stricmp(peer_route, "relay") == 0) {
        /*
         * "relay" is a non-address marker. Reuse the hostname which already
         * passed the control-server configuration and DNS validation; the
         * server-provided port may differ from its TCP listener.
         */
        safe_copy(host, sizeof(host), g_online_cfg.server_host);
    }
    if (match_id <= 0 || port <= 0 || port > 65535 || !host[0]) return;

    if (g_online_pending_match.active && g_online_pending_match.match_id == match_id) {
        safe_copy(g_online_pending_match.peer_host, sizeof(g_online_pending_match.peer_host), host);
        g_online_pending_match.peer_port = port;
        applies = 1;
    }
    if (g_online_active_match.active && g_online_active_match.match_id == match_id) {
        applies = 1;
    }
    if (g_online_connect.match_id == match_id) {
        safe_copy(g_online_connect.peer_host, sizeof(g_online_connect.peer_host), host);
        g_online_connect.peer_port = port;
        safe_copy(g_online_connect.peer_route,
                  sizeof(g_online_connect.peer_route),
                  peer_route[0] ? peer_route : "preferred");
        if (public_host[0] && public_port > 0 && public_port <= 65535) {
            safe_copy(g_online_connect.public_host, sizeof(g_online_connect.public_host), public_host);
            g_online_connect.public_port = public_port;
        }
        if (lan_host[0] && lan_port > 0 && lan_port <= 65535) {
            safe_copy(g_online_connect.lan_host, sizeof(g_online_connect.lan_host), lan_host);
            g_online_connect.lan_port = lan_port;
        }
        applies = 1;
    }
    if (!applies) return;

    LOG_INFO("online.p2p: server selected route=%s peer=%s:%d match=%d",
             peer_route[0] ? peer_route : "preferred",
             host,
             port,
             match_id);

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
        /* Do not race the server-selected path against every advertised address.
         * Two clients on one PC were independently pinning loopback/LAN/public
         * sources, leaving each side's strict endpoint gate pointed elsewhere.
         * Each attempt therefore uses exactly one symmetric route generation:
         * loopback, LAN, or public as selected by the rendezvous server. */
    }
}

static void online_pump_p2p_probe(void) {
    char err[256];
    /* Keep probing until the session connects. This keeps NAT mappings warm and
     * lets the server resend a changed observed endpoint during hole punching. */
    if (g_online_connect.attempts <= 0 || g_online_connect.established) return;
    if (!ggpo_net_active()) return;
    if (ggpo_net_link_ready()) return;
    if (g_online_connect.match_id <= 0 || !g_online_connect.p2p_token[0] || !g_online_cfg.username[0]) return;
    if (g_online_server_state != ONLINE_SERVER_CONNECTED || g_online_server_slot < 0) return;
    if (g_online_connect.probe_cooldown > 0) {
        g_online_connect.probe_cooldown--;
        return;
    }
    g_online_connect.probe_cooldown = 12;
    err[0] = '\0';
    if (ggpo_net_send_server_probe(g_online_cfg.server_host,
                                   g_online_cfg.server_port,
                                   g_online_connect.match_id,
                                   g_online_cfg.username,
                                   g_online_connect.p2p_token,
                                   err,
                                   sizeof(err))) {
        if (!g_online_connect.probe_logged) {
            g_online_connect.probe_logged = 1;
            LOG_INFO("online.p2p: sent UDP discovery probe match=%d server=%s:%u local_udp=%u",
                     g_online_connect.match_id,
                     g_online_cfg.server_host,
                     (unsigned int)g_online_cfg.server_port,
                     (unsigned int)ggpo_net_local_port());
        }
    } else {
        if (!g_online_connect.probe_warned) {
            g_online_connect.probe_warned = 1;
            LOG_WARN("online.p2p: server UDP probe failed match=%d (%s)",
                     g_online_connect.match_id,
                     err[0] ? err : "unknown error");
        }
    }
}

static void online_connect_start_attempt(int first) {
    char err[256];
    int port;

    if (g_online_connect.established || g_online_connect.match_id <= 0) return;

    /* Attempt rollover is not a terminal disconnect. In particular, do not send
     * BYE: the other side may have received our last HELLO just before this side's
     * timeout and would otherwise poison an otherwise viable retry. */
    if (ggpo_net_active()) {
        LOG_INFO("online.p2p: closing attempt %d without BYE for fresh socket",
                 g_online_connect.attempts);
        ggpo_net_stop_for_retry();
    }

    port = first ? g_online_connect.local_port : 0;
    g_online_connect.attempts++;
    g_online_connect.attempt_started_ms = GetTickCount();
    g_online_connect.connected_once = 0;
    g_online_connect.probe_cooldown = 0;
    g_online_connect.probe_logged = 0;
    g_online_connect.probe_warned = 0;

    /* The packet-auth key is consumed by each successful socket start. Arm it
     * again immediately before every first/retry start, and fail the match
     * closed instead of silently opening a legacy unauthenticated session. */
    err[0] = '\0';
    if (!g_online_connect.p2p_auth_token[0] ||
        !ggpo_net_set_match_token(g_online_connect.p2p_auth_token,
                                  err,
                                  sizeof(err))) {
        LOG_ERROR("online.p2p: attempt %d rejected match authentication data (%s)",
                  g_online_connect.attempts,
                  err[0] ? err : "missing authentication data");
        online_abort_prematch_setup("missing or invalid P2P match authentication data");
        return;
    }

    if (_stricmp(g_online_connect.p2p_role, "host") == 0) {
        start_ggpo_net_host((uint16_t)port, "online server match", 1);
    } else {
        start_ggpo_net_join_deferred((uint16_t)port, "online server match", 1);
    }

    if (ggpo_net_active()) {
        err[0] = '\0';
        if (!ggpo_net_set_prematch_hold(1, err, sizeof(err))) {
            LOG_ERROR("online.p2p: attempt %d could not engage prematch hold (%s)",
                      g_online_connect.attempts,
                      err[0] ? err : "unknown error");
            ggpo_net_stop_for_retry();
        }
    }

    if (ggpo_net_active()) {
        /* A fresh socket invalidates any state prepared for the previous
         * attempt. Rebuild and republish it only after this attempt connects. */
        g_online_pending_match.prematch_prepared = 0;
        g_online_pending_match.prematch_released = 0;
        g_online_pending_match.prematch_start_prepared = 0;
        err[0] = '\0';
        if (g_online_connect.peer_host[0] && g_online_connect.peer_port > 0 &&
            !ggpo_net_set_peer(g_online_connect.peer_host,
                               (uint16_t)g_online_connect.peer_port,
                               err,
                               sizeof(err))) {
            LOG_WARN("online.p2p: attempt %d could not apply peer %s:%d (%s)",
                     g_online_connect.attempts,
                     g_online_connect.peer_host,
                     g_online_connect.peer_port,
                     err[0] ? err : "unknown error");
        }
        LOG_INFO("online.p2p: connect attempt %d/%d local_udp=%u%s",
                 g_online_connect.attempts,
                 ONLINE_CONNECT_MAX_ATTEMPTS,
                 (unsigned int)ggpo_net_local_port(),
                 first ? " configured socket" : " fresh socket");
    } else {
        LOG_WARN("online.p2p: connect attempt %d/%d could not start; retrying with a fresh socket",
                 g_online_connect.attempts,
                 ONLINE_CONNECT_MAX_ATTEMPTS);
    }

    if (g_online_connect.attempts > 1) {
        char status[96];
        snprintf(status,
                 sizeof(status),
                 "Connecting... (attempt %d/%d)",
                 g_online_connect.attempts,
                 ONLINE_CONNECT_MAX_ATTEMPTS);
        online_hub_set_status(status);
    }
}

static void online_connect_retry_tick(void) {
    DWORD now;
    DWORD deadline;

    if (g_online_connect.established || g_online_connect.attempts <= 0) return;
    if (!g_online_pending_match.active && !g_online_active_match.active) return;

    if (ggpo_net_active() && ggpo_net_connected()) {
        if (!g_online_connect.connected_once) {
            g_online_connect.connected_once = 1;
            LOG_INFO("online.p2p: connected during countdown on attempt %d/%d local_udp=%u",
                     g_online_connect.attempts,
                     ONLINE_CONNECT_MAX_ATTEMPTS,
                     (unsigned int)ggpo_net_local_port());
        }
        return;
    }

    now = GetTickCount();
    deadline = ggpo_net_active() ? ONLINE_CONNECT_ATTEMPT_MS
                                 : ONLINE_CONNECT_START_FAILURE_MS;
    if ((DWORD)(now - g_online_connect.attempt_started_ms) < deadline) return;

    if (g_online_connect.attempts >= ONLINE_CONNECT_MAX_ATTEMPTS) {
        online_abort_connect_timeout();
        return;
    }

    LOG_WARN("online.p2p: attempt %d/%d did not connect after %lu ms; retrying",
             g_online_connect.attempts,
             ONLINE_CONNECT_MAX_ATTEMPTS,
             (unsigned long)deadline);
    online_connect_start_attempt(0);
}

static void online_server_handle_line(const char* line) {
    char type[64];
    char text[256];
    char json_error[192];
    OnlineControlJsonResult type_result;
    int value = 0;
    json_error[0] = '\0';
    if (!online_control_json_validate(line, json_error, sizeof(json_error))) {
        LOG_WARN("online.server: rejected invalid control message (%s)",
                 json_error[0] ? json_error : "invalid JSON");
        online_server_disconnect("Server sent an invalid message.");
        return;
    }
    type_result = online_control_json_get_string(line, "type", type, sizeof(type));
    if (type_result != ONLINE_CONTROL_JSON_OK) {
        LOG_WARN("online.server: rejected control message with missing, invalid, or oversized type");
        online_server_disconnect("Server sent an invalid message type.");
        return;
    }

    if (_stricmp(type, "auth_ok") == 0) {
        char submitted_username[sizeof(g_online_cfg.username)];
        char credential_error[256];
        CredentialExtResult credential_result = CREDENTIAL_EXT_OK;
        OnlineControlJsonResult username_result;
        if (!online_server_protocol_compatible(line)) {
            LOG_WARN("online.server: rejected incompatible auth protocol advertisement");
            online_server_disconnect("Online server update required; matchmaking protocol is incompatible.");
            return;
        }
        safe_copy(submitted_username, sizeof(submitted_username), g_online_cfg.username);
        username_result = online_control_json_get_string(line,
                                                          "username",
                                                          text,
                                                          sizeof(text));
        if (username_result != ONLINE_CONTROL_JSON_OK ||
            !online_control_username_is_canonical(text)) {
            LOG_WARN("online.server: rejected auth_ok with a non-canonical account name");
            online_server_disconnect("Server returned an invalid account name.");
            return;
        }
        g_online_auth_pending = 0;
        g_online_server_deadline_ms = 0;
        g_online_authed = 1;
        if (g_online_launch.request.action != LAUNCH_REQUEST_NONE) {
            g_online_launch.waiting_status_shown = 0;
        }
        online_server_heartbeat_arm((uint32_t)GetTickCount());
        safe_copy(g_online_cfg.username, sizeof(g_online_cfg.username), text);
        if (online_json_get_int(line, "elo", &value)) g_online_public_elo = value;
        if (!online_server_send_map_manifest()) {
            online_clear_password_memory();
            online_server_disconnect("Could not publish the installed map list.");
            return;
        }
        if (g_online_cfg.remember_me) {
            credential_error[0] = '\0';
            credential_result = online_store_remembered_password(credential_error,
                                                                 sizeof(credential_error));
            if (credential_result == CREDENTIAL_EXT_OK) {
                LOG_INFO("online.credentials: updated remembered login server=%s:%u user=%s",
                         g_online_cfg.server_host,
                         (unsigned int)g_online_cfg.server_port,
                         g_online_cfg.username);
                if (strcmp(submitted_username, g_online_cfg.username) != 0) {
                    (void)online_delete_remembered_password(g_online_cfg.server_host,
                                                            g_online_cfg.server_port,
                                                            submitted_username);
                }
                online_hub_set_status("Logged in.");
            } else {
                LOG_WARN("online.credentials: save failed server=%s:%u user=%s result=%s (%s)",
                         g_online_cfg.server_host,
                         (unsigned int)g_online_cfg.server_port,
                         g_online_cfg.username,
                         credential_ext_result_name(credential_result),
                         credential_error[0] ? credential_error : "unknown error");
                online_hub_set_status("Logged in. Remember me could not be updated.");
            }
        } else {
            online_hub_set_status("Logged in.");
        }
        online_clear_password_memory();
        online_hub_save();
    } else if (_stricmp(type, "server_info") == 0) {
        if (!g_online_server_info_pending || !g_online_auth_pending) return;
        if (!online_server_protocol_compatible(line)) {
            LOG_WARN("online.server: server_info is missing required control/match/P2P capabilities");
            online_server_disconnect("Online server update required; matchmaking protocol is incompatible.");
            return;
        }
        g_online_server_info_pending = 0;
        online_hub_set_status("Signing in...");
        online_server_send_auth(g_online_register_after_connect);
    } else if (_stricmp(type, "auth_fail") == 0) {
        g_online_auth_pending = 0;
        g_online_server_deadline_ms = 0;
        online_json_get_string(line, "reason", text, sizeof(text));
        snprintf(g_online_status, sizeof(g_online_status), "Login failed: %s", text[0] ? text : "?");
        g_online_authed = 0;
        if (!g_online_register_after_connect && g_online_password_from_credential) {
            (void)online_delete_remembered_password(g_online_cfg.server_host,
                                                    g_online_cfg.server_port,
                                                    g_online_cfg.username);
            online_clear_password_memory();
            LOG_WARN("online.credentials: discarded a remembered login rejected by the server");
        }
    } else if (_stricmp(type, "pong") == 0) {
        int pong_seq = 0;
        if (!online_json_get_int(line, "seq", &pong_seq) || pong_seq <= 0) {
            LOG_WARN("online.server: rejected malformed heartbeat response");
            online_server_disconnect("Server sent an invalid heartbeat response.");
            return;
        }
        if (pong_seq == g_online_server_heartbeat_seq) {
            g_online_server_heartbeat_acked_seq = pong_seq;
        } else {
            LOG_WARN("online.server: ignored stale heartbeat response seq=%d expected=%d",
                     pong_seq,
                     g_online_server_heartbeat_seq);
        }
    } else if (_stricmp(type, "error") == 0) {
        if (g_online_server_info_pending && g_online_auth_pending) {
            LOG_WARN("online.server: server rejected the required capability handshake");
            online_server_disconnect("Online server update required; matchmaking protocol is incompatible.");
            return;
        }
        online_json_get_string(line, "message", text, sizeof(text));
        if (g_online_result.active && g_online_result.server_confirmed &&
            _stricmp(text, "invalid or stale match result") == 0) {
            /* Older servers can emit this after first sending the authoritative
             * disconnect win. TCP preserves that order, so never replace a
             * confirmed outcome with the harmless late-report race. */
            LOG_WARN("online.server: ignored late report rejection after confirmed match=%d",
                     g_online_result.match_id);
        } else {
            if (g_online_challenge_map_picker.active) {
                online_challenge_map_picker_clear();
                online_hub_rebuild_rows();
            }
            online_hub_set_status(text[0] ? text : "Server error.");
        }
    } else if (_stricmp(type, "queue_update") == 0) {
        if (online_json_get_int(line, "casual", &value)) g_online_queue_casual_count = value;
        if (online_json_get_int(line, "competitive", &value)) g_online_queue_competitive_count = value;
    } else if (_stricmp(type, "queue_left") == 0) {
        g_online_queue_mode = 0;
    } else if (_stricmp(type, "challenge_maps_begin") == 0) {
        char username[48];
        int request_id = 0;
        int count = -1;
        username[0] = '\0';
        online_json_get_string(line, "username", username, sizeof(username));
        online_json_get_int(line, "request_id", &request_id);
        online_json_get_int(line, "count", &count);
        if (online_challenge_map_picker_matches(username, request_id)) {
            if (count < 0 || count > ONLINE_CHALLENGE_MAP_MAX) {
                online_challenge_map_picker_clear();
                online_hub_set_status("Compatible map list is too large.");
            } else {
                free(g_online_challenge_map_picker.choices);
                g_online_challenge_map_picker.choices = NULL;
                g_online_challenge_map_picker.choice_count = 0;
                g_online_challenge_map_picker.choice_capacity = 0;
                g_online_challenge_map_picker.expected_count = count;
                g_online_challenge_map_picker.loading = 1;
            }
            online_hub_rebuild_rows();
        }
    } else if (_stricmp(type, "challenge_map_choice") == 0) {
        char username[48];
        char key[128];
        char label[96];
        int request_id = 0;
        username[0] = '\0';
        key[0] = '\0';
        label[0] = '\0';
        online_json_get_string(line, "username", username, sizeof(username));
        online_json_get_int(line, "request_id", &request_id);
        online_json_get_string(line, "key", key, sizeof(key));
        online_json_get_string(line, "label", label, sizeof(label));
        if (online_challenge_map_picker_matches(username, request_id) &&
            !online_challenge_map_picker_add(key, label)) {
            online_challenge_map_picker_clear();
            online_hub_set_status("Could not load the compatible map list.");
            online_hub_rebuild_rows();
        }
    } else if (_stricmp(type, "challenge_maps_end") == 0) {
        char username[48];
        int request_id = 0;
        int count = -1;
        username[0] = '\0';
        online_json_get_string(line, "username", username, sizeof(username));
        online_json_get_int(line, "request_id", &request_id);
        online_json_get_int(line, "count", &count);
        if (online_challenge_map_picker_matches(username, request_id)) {
            if (count < 0 ||
                count != g_online_challenge_map_picker.expected_count ||
                count != g_online_challenge_map_picker.choice_count) {
                online_challenge_map_picker_clear();
                online_hub_set_status("Server sent an incomplete compatible map list.");
            } else if (count == 0) {
                online_challenge_map_picker_clear();
                online_hub_set_status("You and that friend have no compatible maps.");
            } else {
                g_online_challenge_map_picker.loading = 0;
                g_online_challenge_map_picker.selected = 0;
                online_hub_set_status("");
            }
            online_hub_rebuild_rows();
        }
    } else if (_stricmp(type, "p2p_peer") == 0) {
        online_apply_p2p_peer(line);
    } else if (_stricmp(type, "friend_snapshot_begin") == 0) {
        g_online_friend_snapshot_complete = 0;
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
            if (!online_json_get_string(line, "presence",
                                        fr->presence, sizeof(fr->presence))) {
                safe_copy(fr->presence, sizeof(fr->presence),
                          fr->online ? "online" : "offline");
            }
            if (online_json_get_int(line, "muted", &value)) fr->muted = value ? 1 : 0;
        }
    } else if (_stricmp(type, "blocked_user") == 0) {
        if (g_online_friend_count < ONLINE_HUB_MAX_FRIENDS &&
            online_json_get_string(line, "username", text, sizeof(text))) {
            OnlineFriend* fr = &g_online_friends[g_online_friend_count++];
            memset(fr, 0, sizeof(*fr));
            safe_copy(fr->name, sizeof(fr->name), text);
            fr->blocked = 1;
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
            if (online_json_get_int(line, "muted", &value)) ch->muted = value ? 1 : 0;
            online_json_get_string(line, "map_key", ch->map_key, sizeof(ch->map_key));
            online_json_get_string(line, "map_label", ch->map_label, sizeof(ch->map_label));
            if (!ch->muted) {
                online_challenge_toast_show(ch->from, ch->id, ch->elo,
                                            ch->expires_in, ch->map_label);
            }
        }
    } else if (_stricmp(type, "friend_snapshot_end") == 0) {
        g_online_friend_snapshot_complete = 1;
    } else if (_stricmp(type, "friend_request_sent") == 0) {
        online_hub_set_status("Friend request sent.");
    } else if (_stricmp(type, "social_update") == 0) {
        char action[32];
        action[0] = '\0';
        text[0] = '\0';
        online_json_get_string(line, "action", action, sizeof(action));
        online_json_get_string(line, "username", text, sizeof(text));
        if (_stricmp(action, "muted") == 0) {
            snprintf(g_online_status, sizeof(g_online_status), "%s muted.", text);
        } else if (_stricmp(action, "unmuted") == 0) {
            snprintf(g_online_status, sizeof(g_online_status), "%s unmuted.", text);
        } else if (_stricmp(action, "blocked") == 0) {
            snprintf(g_online_status, sizeof(g_online_status), "%s blocked.", text);
        } else if (_stricmp(action, "unblocked") == 0) {
            snprintf(g_online_status, sizeof(g_online_status), "%s unblocked.", text);
        }
    } else if (_stricmp(type, "challenge_sent") == 0) {
        int id = 0;
        int expires_in = 300;
        text[0] = '\0';
        online_json_get_string(line, "username", text, sizeof(text));
        online_json_get_int(line, "id", &id);
        online_json_get_int(line, "expires_in", &expires_in);
        online_sent_challenge_add(text, id, expires_in);
        {
            char map_label[96];
            map_label[0] = '\0';
            online_json_get_string(line, "map_label", map_label, sizeof(map_label));
            if (map_label[0]) {
                snprintf(g_online_status, sizeof(g_online_status),
                         "Challenge sent on %s.", map_label);
            } else {
                online_hub_set_status("Challenge sent.");
            }
        }
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
    } else if (_stricmp(type, "rematch_waiting") == 0) {
        int expires_in = 45;
        if (!g_online_result.active ||
            !online_server_message_matches_current_match(line, type)) return;
        online_json_get_int(line, "expires_in", &expires_in);
        g_online_result.rematch_state = ONLINE_REMATCH_WAITING;
        g_online_result.rematch_unranked = 1;
        online_result_rematch_arm(expires_in);
        g_online_result.toast_visible = 1;
        g_online_result.toast_age = 0;
        g_online_result.toast_lifetime = expires_in * 60 + 90;
        safe_copy(g_online_result.status, sizeof(g_online_result.status),
                  "Rematch requested; waiting for opponent.");
        online_hub_set_status("Rematch requested; waiting for opponent.");
    } else if (_stricmp(type, "rematch_offer") == 0) {
        int expires_in = 45;
        char from[48];
        from[0] = '\0';
        if (!g_online_result.active ||
            !online_server_message_matches_current_match(line, type)) return;
        online_json_get_string(line, "from", from, sizeof(from));
        if (!online_control_username_is_canonical(from) ||
            (g_online_result.opponent[0] &&
             _stricmp(from, g_online_result.opponent) != 0)) {
            LOG_WARN("online.server: ignored rematch offer from unexpected user");
            return;
        }
        online_json_get_int(line, "expires_in", &expires_in);
        g_online_result.rematch_state = ONLINE_REMATCH_OFFERED;
        g_online_result.rematch_unranked = 1;
        online_result_rematch_arm(expires_in);
        g_online_result.toast_visible = 1;
        g_online_result.toast_age = 0;
        g_online_result.toast_lifetime = expires_in * 60 + 90;
        safe_copy(g_online_result.status, sizeof(g_online_result.status),
                  "Opponent requested a private rematch.");
        online_hub_set_status("Opponent requested a private rematch.");
    } else if (_stricmp(type, "rematch_starting") == 0) {
        if (!g_online_result.active ||
            !online_server_message_matches_current_match(line, type)) return;
        g_online_result.rematch_state = ONLINE_REMATCH_STARTING;
        g_online_result.toast_visible = 1;
        g_online_result.toast_age = 0;
        safe_copy(g_online_result.status, sizeof(g_online_result.status),
                  "Private rematch is starting...");
        online_hub_set_status("Private rematch is starting...");
    } else if (_stricmp(type, "rematch_declined") == 0 ||
               _stricmp(type, "rematch_unavailable") == 0 ||
               _stricmp(type, "rematch_expired") == 0 ||
               _stricmp(type, "rematch_closed") == 0) {
        const char* status = "Rematch unavailable.";
        if (!g_online_result.active ||
            !online_server_message_matches_current_match(line, type)) return;
        if (_stricmp(type, "rematch_declined") == 0) {
            status = "Opponent declined the rematch.";
        } else if (_stricmp(type, "rematch_expired") == 0) {
            status = "Rematch offer expired.";
        } else if (_stricmp(type, "rematch_closed") == 0) {
            status = "Rematch closed.";
        }
        online_result_rematch_clear(status);
        g_online_result.toast_visible = 1;
        g_online_result.toast_age = 0;
        g_online_result.toast_lifetime = 240;
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
    } else if (_stricmp(type, "match_started") == 0) {
        if (!online_server_message_matches_current_match(line, type)) return;
        if (!g_online_pending_match.active ||
            !online_json_get_int(line, "committed", &value) || !value) {
            LOG_WARN("online.server: ignored uncommitted or out-of-phase match_started");
            return;
        }
        g_online_pending_match.server_committed = 1;
        online_hub_set_status("Both players ready. Starting match...");
        LOG_INFO("online.match: server committed gameplay start match=%d",
                 g_online_pending_match.match_id);
    } else if (_stricmp(type, "match_report_ack") == 0) {
        if (!online_server_message_matches_current_match(line, type)) return;
        if (g_online_result.active) {
            safe_copy(g_online_result.status, sizeof(g_online_result.status), "Result reported; waiting for opponent.");
        }
        online_hub_set_status("Result reported; waiting for opponent.");
    } else if (_stricmp(type, "match_result") == 0) {
        OnlineMatchResult result;
        int preserve_native_finish = online_native_finish_is_presenting();
        OnlineRematchState prior_rematch_state = g_online_result.active
            ? g_online_result.rematch_state
            : ONLINE_REMATCH_NONE;
        DWORD prior_rematch_deadline_ms = g_online_result.active
            ? g_online_result.rematch_deadline_ms
            : 0u;
        int prior_rematch_unranked = g_online_result.active
            ? g_online_result.rematch_unranked
            : 1;
        int got_elo = 0;
        int elo_value = 0;
        int got_elo_before = 0;
        int elo_before_value = 0;
        int rematch_available = 0;
        int rematch_expires_in = 0;
        int rematch_unranked = 1;
        int competitive_value = g_online_result.active
            ? g_online_result.competitive
            : g_online_active_match.competitive;
        if (!online_server_message_matches_current_match(line, type)) return;
        if (preserve_native_finish) {
            g_online_active_match.awaiting_native_return = 1;
        }
        if (!g_online_result.active &&
            (!g_online_active_match.active || !g_online_active_match.server_committed)) {
            /* TCP preserves the server's write order, but one recv pump can
             * contain both the two-READY commit and an immediate committed
             * forfeit/disconnect result. The launch pump has not had a chance
             * to promote pending metadata yet. Resolve that exact committed
             * match directly without flashing or entering a now-deleted GAME. */
            if (g_online_pending_match.active &&
                g_online_pending_match.server_committed) {
                LOG_INFO("online.match: resolving server result at start barrier match=%d",
                         g_online_pending_match.match_id);
                online_active_match_capture_from_pending();
                g_online_pending_match.active = 0;
            } else {
                LOG_WARN("online.server: rejected match_result before committed gameplay");
                return;
            }
        }
        if (!preserve_native_finish && ggpo_net_active()) {
            stop_ggpo_net("online server result");
        }
        text[0] = '\0';
        online_json_get_string(line, "result", text, sizeof(text));
        result = online_match_result_from_text(text);
        if (result == ONLINE_MATCH_RESULT_NONE && g_online_result.active) {
            result = g_online_result.result;
        }
        if (result == ONLINE_MATCH_RESULT_NONE) result = g_online_active_match.result;
        if (result == ONLINE_MATCH_RESULT_NONE) result = ONLINE_MATCH_RESULT_DRAW;
        if (online_json_get_int(line, "competitive", &value)) {
            competitive_value = value ? 1 : 0;
            g_online_active_match.competitive = competitive_value;
        }
        if (online_json_get_int(line, "elo", &value)) {
            got_elo = 1;
            elo_value = value;
        }
        if (online_json_get_int(line, "elo_before", &value)) {
            got_elo_before = 1;
            elo_before_value = value;
        }
        online_json_get_int(line, "rematch_available", &rematch_available);
        online_json_get_int(line, "rematch_expires_in", &rematch_expires_in);
        online_json_get_int(line, "rematch_unranked", &rematch_unranked);
        if (!g_online_result.active) {
            if (!g_online_active_match.active) {
                g_online_active_match.active = 1;
                g_online_active_match.queue_mode = 0;
            }
            online_result_prepare(result, "Match complete.");
            if (!preserve_native_finish) {
                online_return_to_hub_after_match("Match complete.");
            }
        }
        g_online_result.result = result;
        g_online_result.toast_visible = 1;
        g_online_result.toast_age = 0;
        if (g_online_result.toast_lifetime <= 0) g_online_result.toast_lifetime = ONLINE_RESULT_TOAST_FRAMES;
        g_online_result.server_confirmed = 1;
        g_online_result.competitive = competitive_value;
        if (got_elo_before) {
            g_online_result.elo_before = elo_before_value;
            g_online_result.elo_delta_valid = 1;
        }
        if (got_elo) {
            g_online_public_elo = elo_value;
            g_online_result.elo_after = elo_value;
            g_online_result.elo_delta_valid = 1;
        }
        if (rematch_available && rematch_expires_in > 0) {
            if (rematch_expires_in > 300) rematch_expires_in = 300;
            if (prior_rematch_state != ONLINE_REMATCH_NONE) {
                /* Terminal result reports are replayable for idempotent
                 * disconnect races. Do not let that replay rewind an offer
                 * which this client already accepted or is already starting. */
                g_online_result.rematch_state = prior_rematch_state;
                g_online_result.rematch_unranked = prior_rematch_unranked;
                g_online_result.rematch_deadline_ms = prior_rematch_deadline_ms;
            } else {
                g_online_result.rematch_state = ONLINE_REMATCH_AVAILABLE;
                g_online_result.rematch_unranked = rematch_unranked ? 1 : 0;
                online_result_rematch_arm(rematch_expires_in);
            }
            g_online_result.toast_lifetime = rematch_expires_in * 60 + 90;
            if (g_online_result.rematch_state == ONLINE_REMATCH_WAITING) {
                safe_copy(g_online_result.status, sizeof(g_online_result.status),
                          "Rematch requested; waiting for opponent.");
                online_hub_set_status("Rematch requested; waiting for opponent.");
            } else if (g_online_result.rematch_state == ONLINE_REMATCH_OFFERED) {
                safe_copy(g_online_result.status, sizeof(g_online_result.status),
                          "Opponent requested a private rematch.");
                online_hub_set_status("Opponent requested a private rematch.");
            } else if (g_online_result.rematch_state == ONLINE_REMATCH_STARTING) {
                safe_copy(g_online_result.status, sizeof(g_online_result.status),
                          "Private rematch is starting...");
                online_hub_set_status("Private rematch is starting...");
            } else {
                safe_copy(g_online_result.status, sizeof(g_online_result.status),
                          "Private rematch available.");
                online_hub_set_status("Private rematch available.");
            }
        } else if (prior_rematch_state != ONLINE_REMATCH_STARTING) {
            online_result_rematch_clear(NULL);
            safe_copy(g_online_result.status, sizeof(g_online_result.status), "Match complete.");
            online_hub_set_status("Match complete.");
        }
        if (preserve_native_finish) {
            safe_copy(g_online_active_match.completion_status,
                      sizeof(g_online_active_match.completion_status),
                      "Match complete.");
        } else {
            online_pending_match_reset();
            memset(&g_online_active_match, 0, sizeof(g_online_active_match));
            online_connect_reset();
        }
    } else if (_stricmp(type, "match_abort") == 0 ||
               _stricmp(type, "match_end") == 0) {
        int preserve_native_finish = online_native_finish_is_presenting();
        if (!online_server_message_matches_current_match(line, type)) return;
        if (preserve_native_finish) {
            g_online_active_match.awaiting_native_return = 1;
        }
        /* A no-contest/abort supersedes any provisional locally reported
         * outcome. Remove both its toast and retained match identity. */
        memset(&g_online_result, 0, sizeof(g_online_result));
        text[0] = '\0';
        online_json_get_string(line, "reason", text, sizeof(text));
        online_hub_set_status(text[0] ? text : "Online match setup ended.");
        if (preserve_native_finish) {
            safe_copy(g_online_active_match.completion_status,
                      sizeof(g_online_active_match.completion_status),
                      text[0] ? text : "Match ended as a no contest.");
        } else {
            if (ggpo_net_active()) stop_ggpo_net("online server");
            online_clear_match_state();
            online_hub_open();
        }
    }
    if (is_online_hub_state_active()) {
        online_hub_rebuild_rows();
    }
}

static void online_server_update(void) {
    char prematch_err[256];
    uint32_t now;
    online_pump_p2p_probe();
    /* Pending matches are transport-only. Poll the held/released socket here so
     * hole punching, RTT/cosmetics and authoritative state sync happen while the
     * match-found countdown is visible, never on the first GAME frame. */
    if (g_online_pending_match.active) {
        if (ggpo_net_active()) {
            prematch_err[0] = '\0';
            if (!ggpo_net_service(prematch_err, sizeof(prematch_err))) {
                /* prepare_prematch_start restores frame zero and deliberately
                 * closes ggpo_net's retry window. Even if the READY write then
                 * failed (so server_start_reported is still false), reusing this
                 * socket state on a fresh attempt would advertise stale
                 * readiness and deadlock the start barrier. Once either local
                 * preparation or READY happened, cancel the setup atomically. */
                if (g_online_pending_match.prematch_start_prepared ||
                    g_online_pending_match.server_start_reported) {
                    online_abort_prematch_setup(
                        prematch_err[0] ? prematch_err
                                        : "transport failed at gameplay start barrier");
                    goto after_pending_transport;
                }
                LOG_WARN("online.prematch: transport attempt %d failed before gameplay (%s)",
                         g_online_connect.attempts,
                         prematch_err[0] ? prematch_err : "unknown error");
                ggpo_net_stop_for_retry();
                g_online_connect.connected_once = 0;
                g_online_pending_match.prematch_prepared = 0;
                g_online_pending_match.prematch_released = 0;
                g_online_pending_match.prematch_start_prepared = 0;
                g_online_pending_match.server_start_reported = 0;
                g_online_pending_match.server_committed = 0;
                /* The failed transport has already been serviced; let the
                 * bounded retry machine open its next fresh socket now. */
                g_online_connect.attempt_started_ms =
                    GetTickCount() - ONLINE_CONNECT_START_FAILURE_MS;
            }
        }
        online_connect_retry_tick();
    }
after_pending_transport:
    now = (uint32_t)GetTickCount();
    if (g_online_server_state == ONLINE_SERVER_CONNECTING) {
        int r = net_check_connect(g_online_server_slot);
        if (r == 1) {
            g_online_server_state = ONLINE_SERVER_CONNECTED;
            g_online_server_info_pending = 1;
            g_online_server_deadline_ms = (DWORD)online_control_deadline_after(
                now, ONLINE_SERVER_AUTH_TIMEOUT_MS);
            online_hub_set_status("Checking online server...");
            if (!online_server_send_raw("{\"type\":\"server_info\"}\n")) {
                if (g_online_server_state != ONLINE_SERVER_DISCONNECTED) {
                    online_server_disconnect("Could not start the server handshake.");
                }
            }
        } else if (r < 0) {
            online_server_disconnect("Server connection failed.");
        } else if (online_control_deadline_reached(
                       now, (uint32_t)g_online_server_deadline_ms)) {
            online_server_disconnect("Server connection timed out.");
        }
        return;
    }
    if (g_online_server_state != ONLINE_SERVER_CONNECTED || g_online_server_slot < 0) return;
    if (g_online_auth_pending &&
        online_control_deadline_reached(now, (uint32_t)g_online_server_deadline_ms)) {
        online_server_disconnect(g_online_server_info_pending
                                     ? "Online server update required; capability handshake timed out."
                                     : "Authentication timed out.");
        return;
    }

    while (1) {
        char chunk[4096];
        int got = net_recv(g_online_server_slot, chunk, (int)sizeof(chunk));
        if (got < 0) {
            online_server_disconnect("Server disconnected.");
            return;
        }
        if (got == 0) break;
        if (memchr(chunk, '\0', (size_t)got) != NULL) {
            LOG_WARN("online.server: rejected NUL-containing control stream");
            online_server_disconnect("Server sent an invalid message.");
            return;
        }
        if (g_online_recv_len + (size_t)got >= sizeof(g_online_recv_buf)) {
            LOG_WARN("online.server: control receive buffer exceeded %lu bytes",
                     (unsigned long)(sizeof(g_online_recv_buf) - 1u));
            online_server_disconnect("Server message too large; disconnected.");
            return;
        }
        memcpy(g_online_recv_buf + g_online_recv_len, chunk, (size_t)got);
        g_online_recv_len += (size_t)got;
        g_online_recv_buf[g_online_recv_len] = '\0';

        while (1) {
            char* nl = memchr(g_online_recv_buf, '\n', g_online_recv_len);
            size_t line_len;
            size_t consumed;
            char line[ONLINE_SERVER_LINE_CAP];
            if (!nl) break;
            line_len = (size_t)(nl - g_online_recv_buf);
            consumed = line_len + 1;
            if (line_len >= sizeof(line) ||
                memchr(g_online_recv_buf, '\0', line_len) != NULL) {
                LOG_WARN("online.server: rejected oversized or NUL-containing control line");
                online_server_disconnect("Server sent an invalid or oversized message.");
                return;
            }
            memcpy(line, g_online_recv_buf, line_len);
            line[line_len] = '\0';
            console_strip_crlf(line);
            memmove(g_online_recv_buf, g_online_recv_buf + consumed, g_online_recv_len - consumed);
            g_online_recv_len -= consumed;
            g_online_recv_buf[g_online_recv_len] = '\0';
            if (line[0]) {
                online_server_handle_line(line);
                if (g_online_server_state != ONLINE_SERVER_CONNECTED ||
                    g_online_server_slot < 0) {
                    return;
                }
            }
        }
        if (g_online_recv_len >= ONLINE_SERVER_LINE_CAP) {
            LOG_WARN("online.server: unterminated control line exceeded %u bytes",
                     (unsigned int)(ONLINE_SERVER_LINE_CAP - 1u));
            online_server_disconnect("Server message too large; disconnected.");
            return;
        }
    }
    online_server_heartbeat_tick(now);
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
        case ONLINE_SETTING_REMEMBER_ME: online_format_bool(out, out_sz, g_online_cfg.remember_me); break;
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
        case ONLINE_SETTING_DISCORD_PRESENCE:
            safe_copy(out, out_sz, discord_rpc_ext_setting_label());
            break;
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
    /* Keep the hub inside genuinely small F1 windows. A fixed >1.0 minimum made
     * rows and dialogs extend past the viewport at 960x540 and below. */
    return clampf(s, 0.72f, 1.55f);
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

static const char* online_friend_presence_label(const OnlineFriend* fr) {
    if (!fr) return "offline";
    if (fr->blocked) return "blocked";
    if (_stricmp(fr->presence, "queue_casual") == 0) return "casual queue";
    if (_stricmp(fr->presence, "queue_competitive") == 0) return "competitive queue";
    if (_stricmp(fr->presence, "match_setup") == 0) return "setting up match";
    if (_stricmp(fr->presence, "in_match") == 0) return "in match";
    return fr->online ? "online" : "offline";
}

static int online_friend_can_challenge(const OnlineFriend* fr) {
    if (!fr || !fr->online || fr->blocked) return 0;
    return _stricmp(fr->presence, "match_setup") != 0 &&
           _stricmp(fr->presence, "in_match") != 0;
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
            if (online_remembered_login_in_progress()) {
                online_rows_add(ONLINE_ROW_INFO, 0, 0, 0,
                                "Opening online hub", "signing in...");
            } else {
                char value[192];
                online_format_setting_value(ONLINE_SETTING_USERNAME, value, sizeof(value));
                online_rows_add(ONLINE_ROW_SETTING, 1, ONLINE_SETTING_USERNAME, 0, "USERNAME", value);
                online_format_setting_value(ONLINE_SETTING_PASSWORD, value, sizeof(value));
                online_rows_add(ONLINE_ROW_SETTING, 1, ONLINE_SETTING_PASSWORD, 0, "PASSWORD", value);
                online_format_setting_value(ONLINE_SETTING_REMEMBER_ME, value, sizeof(value));
                online_rows_add(ONLINE_ROW_SETTING, 1, ONLINE_SETTING_REMEMBER_ME, 0, "REMEMBER ME", value);
                online_rows_add(ONLINE_ROW_ACTION, 1, ONLINE_ACTION_LOGIN, 0, "LOG IN", "");
                online_rows_add(ONLINE_ROW_ACTION, 1, ONLINE_ACTION_REGISTER, 0, "REGISTER", "");
            }
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
        if (g_online_challenge_map_picker.active) {
            char title[128];
            snprintf(title, sizeof(title), "Challenge %s",
                     g_online_challenge_map_picker.username);
            online_rows_add(ONLINE_ROW_INFO, 0, 0, 0, title,
                            g_online_challenge_map_picker.loading
                                ? "loading compatible maps..."
                                : "choose a shared map");
            if (!g_online_challenge_map_picker.loading &&
                g_online_challenge_map_picker.choice_count > 0) {
                char right[192];
                OnlineMapChoice* choice =
                    &g_online_challenge_map_picker.choices[
                        g_online_challenge_map_picker.selected];
                snprintf(right, sizeof(right), "<  %s  >   %d/%d",
                         choice->label,
                         g_online_challenge_map_picker.selected + 1,
                         g_online_challenge_map_picker.choice_count);
                online_rows_add(ONLINE_ROW_CHALLENGE_MAP, 1, 0, 0, "MAP", right);
                online_rows_add(ONLINE_ROW_ACTION, 1, ONLINE_ACTION_SEND_CHALLENGE, 0,
                                "SEND CHALLENGE", choice->label);
            }
            online_rows_add(ONLINE_ROW_ACTION, 1, ONLINE_ACTION_CANCEL_CHALLENGE_MAP, 0,
                            "CANCEL", "back to friends");
        } else {
            online_rows_add(ONLINE_ROW_ACTION, 1, ONLINE_ACTION_ADD_FRIEND, 0, "SEARCH USER", "username");
            for (int i = 0; i < g_online_request_count; i++) {
                online_rows_add(ONLINE_ROW_FRIEND_REQUEST, 1, i, 0, g_online_requests[i].name, "sent a friend request!");
            }
            for (int i = 0; i < g_online_challenge_count; i++) {
                char right[192];
                snprintf(right, sizeof(right), "%s  |  accept   %ds",
                         g_online_challenges[i].map_label[0]
                             ? g_online_challenges[i].map_label
                             : "Compatible map",
                         g_online_challenges[i].expires_in);
                online_rows_add(ONLINE_ROW_CHALLENGE, 1, i, 0, g_online_challenges[i].from, right);
            }
            for (int i = 0; i < g_online_friend_count; i++) {
                OnlineFriend* fr = &g_online_friends[i];
                char right[96];
                if (fr->blocked || !fr->online) continue;
                snprintf(right, sizeof(right), "%s%s",
                         online_friend_presence_label(fr),
                         fr->muted ? " | muted" : "");
                online_rows_add(ONLINE_ROW_FRIEND, 1, i, 0, fr->name, right);
            }
            for (int i = 0; i < g_online_friend_count; i++) {
                OnlineFriend* fr = &g_online_friends[i];
                char right[96];
                if (fr->blocked || fr->online) continue;
                snprintf(right, sizeof(right), "%s%s",
                         online_friend_presence_label(fr),
                         fr->muted ? " | muted" : "");
                online_rows_add(ONLINE_ROW_FRIEND, 1, i, 0, fr->name, right);
            }
            for (int i = 0; i < g_online_friend_count; i++) {
                OnlineFriend* fr = &g_online_friends[i];
                if (!fr->blocked) continue;
                online_rows_add(ONLINE_ROW_FRIEND, 1, i, 0, fr->name, "blocked");
            }
        }
    } else if (g_online_tab == ONLINE_TAB_SETTINGS) {
        struct SettingRow { OnlineHubSetting id; const char* label; } settings[] = {
            { ONLINE_SETTING_SERVER_HOST, "Server Address" },
            { ONLINE_SETTING_LOCAL_PORT, "P2P UDP Port" },
            { ONLINE_SETTING_CHALLENGE_NOTIFICATIONS, "Challenge Notifications" },
            { ONLINE_SETTING_DISCORD_PRESENCE, "Discord Rich Presence" },
        };
        online_rows_add(ONLINE_ROW_INFO, 0, 0, 0, "Settings", "online and privacy");
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
    if (g_online_challenge_map_picker.active) {
        online_challenge_map_picker_clear();
    }
    g_online_tab = (OnlineHubTab)tab;
    g_online_selected_row = -1;
    g_online_scroll_row = 0;
    online_clear_capture_state();
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
    online_clear_capture_state();
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
    online_clear_capture_state();
    g_online_capture_active = 1;
    g_online_capture_kind = ONLINE_CAPTURE_ADD_FRIEND;
    g_online_capture_target = 0;
    online_hub_set_status("Type a username, then Enter.");
}

static void online_commit_capture(void) {
    OnlineHubSetting setting;
    char old_server[ONLINE_HUB_TEXT_MAX];
    char old_username[sizeof(g_online_cfg.username)];
    uint16_t old_server_port;
    int identity_changed = 0;
    if (!g_online_capture_active) return;
    setting = (g_online_capture_kind == ONLINE_CAPTURE_SETTING)
        ? (OnlineHubSetting)g_online_capture_target
        : ONLINE_SETTING_NONE;
    safe_copy(old_server, sizeof(old_server), g_online_cfg.server_host);
    safe_copy(old_username, sizeof(old_username), g_online_cfg.username);
    old_server_port = g_online_cfg.server_port;
    if (g_online_capture_kind == ONLINE_CAPTURE_SETTING) {
        switch (setting) {
            case ONLINE_SETTING_USERNAME: safe_copy(g_online_cfg.username, sizeof(g_online_cfg.username), trim_ws(g_online_capture_buf)); break;
            case ONLINE_SETTING_PASSWORD: {
                const char* password = g_online_capture_buf;
                if (strlen(password) >= sizeof(g_online_cfg.password)) {
                    online_hub_set_status("Password is too long (256 bytes maximum).");
                    return;
                }
                online_clear_password_memory();
                safe_copy(g_online_cfg.password, sizeof(g_online_cfg.password), password);
            } break;
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
        identity_changed = (strcmp(old_server, g_online_cfg.server_host) != 0 ||
                            old_server_port != g_online_cfg.server_port ||
                            strcmp(old_username, g_online_cfg.username) != 0);
        if (identity_changed &&
            (setting == ONLINE_SETTING_USERNAME || setting == ONLINE_SETTING_SERVER_HOST)) {
            if (g_online_cfg.remember_me) {
                (void)online_delete_remembered_password(old_server,
                                                        old_server_port,
                                                        old_username);
            }
            online_clear_password_memory();
            if (g_online_cfg.remember_me) {
                (void)online_load_remembered_password(0);
            }
        }
        online_hub_save();
        online_hub_set_status("Settings saved.");
    } else if (g_online_capture_kind == ONLINE_CAPTURE_ADD_FRIEND) {
        char* name = trim_ws(g_online_capture_buf);
        if (name && name[0]) online_server_send_username_action("friend_request", name);
        else online_hub_set_status("Enter a username.");
    }
    online_clear_capture_state();
    online_hub_rebuild_rows();
}

static void online_cancel_capture(void) {
    online_clear_capture_state();
    online_hub_rebuild_rows();
}

static void online_adjust_setting(OnlineHubSetting setting, int delta) {
    int online_config_changed = 1;
    if (delta == 0) delta = 1;
    switch (setting) {
        case ONLINE_SETTING_REMEMBER_ME: {
            if (g_online_cfg.remember_me) {
                CredentialExtResult delete_result =
                    online_delete_remembered_password(g_online_cfg.server_host,
                                                       g_online_cfg.server_port,
                                                       g_online_cfg.username);
                g_online_cfg.remember_me = 0;
                if (g_online_password_from_credential) {
                    online_clear_password_memory();
                }
                if (delete_result != CREDENTIAL_EXT_OK &&
                    delete_result != CREDENTIAL_EXT_NOT_FOUND) {
                    online_hub_set_status("Remember me disabled; stored login cleanup failed.");
                } else {
                    online_hub_set_status("Remember me disabled.");
                }
            } else {
                g_online_cfg.remember_me = 1;
                if (!g_online_cfg.password[0]) {
                    (void)online_load_remembered_password(1);
                } else {
                    online_hub_set_status("Remember me enabled.");
                }
            }
            break;
        }
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
        case ONLINE_SETTING_DISCORD_PRESENCE: {
            int enabled;
            if (!discord_rpc_ext_available()) {
                online_hub_set_status(
                    "Set discord_application_id in mods/modframework.cfg first.");
                online_config_changed = 0;
                break;
            }
            enabled = discord_rpc_ext_enabled() ? 0 : 1;
            if (!update_ext_config_set("discord_presence",
                                       enabled ? "1" : "0")) {
                online_hub_set_status("Could not save Discord Rich Presence setting.");
            } else {
                discord_rpc_ext_set_enabled(enabled);
                online_hub_set_status(enabled
                    ? "Discord Rich Presence enabled."
                    : "Discord Rich Presence disabled.");
            }
            online_config_changed = 0;
            break;
        }
        default:
            return;
    }
    if (online_config_changed) {
        online_hub_apply_net_settings();
        online_hub_save();
    }
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
    start_ggpo_net_host(port, "online hub", 0);
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
    if (g_online_friends[idx].blocked) {
        online_hub_set_status("Unblock this user before challenging them.");
        return 0;
    }
    if (!g_online_friends[idx].online) {
        online_hub_set_status("Friend is offline.");
        return 0;
    }
    if (!online_friend_can_challenge(&g_online_friends[idx])) {
        online_hub_set_status("Friend is already in a match.");
        return 0;
    }
    if (online_sent_challenge_pending(g_online_friends[idx].name)) {
        online_hub_set_status("");
        return 0;
    }
    return online_challenge_map_picker_begin(g_online_friends[idx].name);
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
        case ONLINE_ACTION_SEND_CHALLENGE:
            online_challenge_map_picker_send();
            break;
        case ONLINE_ACTION_CANCEL_CHALLENGE_MAP:
            online_challenge_map_picker_clear();
            online_hub_set_status("");
            online_hub_rebuild_rows();
            break;
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
        if (g_online_challenge_map_picker.active) {
            online_challenge_map_picker_clear();
            online_hub_set_status("");
            online_hub_rebuild_rows();
            return;
        }
        online_hub_close_to_return_state();
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
    } else if (row->kind == ONLINE_ROW_CHALLENGE_MAP) {
        online_adjust_selected(1);
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
            online_friend_can_challenge(&g_online_friends[idx]) &&
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
    g_online_context_selected = 0;
    g_online_context_x = x;
    g_online_context_y = y;
}

static int online_context_item_count(const OnlineFriend* fr) {
    if (!fr) return 0;
    if (fr->blocked) return 1;
    return online_friend_can_challenge(fr) ? 4 : 3;
}

static int online_context_action_for_item(const OnlineFriend* fr, int item) {
    if (!fr || item < 0 || item >= online_context_item_count(fr)) {
        return ONLINE_CONTEXT_NONE;
    }
    if (fr->blocked) return ONLINE_CONTEXT_UNBLOCK;
    if (online_friend_can_challenge(fr)) {
        switch (item) {
            case 0: return ONLINE_CONTEXT_CHALLENGE;
            case 1: return ONLINE_CONTEXT_MUTE;
            case 2: return ONLINE_CONTEXT_BLOCK;
            case 3: return ONLINE_CONTEXT_UNFRIEND;
            default: return ONLINE_CONTEXT_NONE;
        }
    }
    switch (item) {
        case 0: return ONLINE_CONTEXT_MUTE;
        case 1: return ONLINE_CONTEXT_BLOCK;
        case 2: return ONLINE_CONTEXT_UNFRIEND;
        default: return ONLINE_CONTEXT_NONE;
    }
}

static int online_context_item_at(float x, float y) {
    OnlineLayout L;
    OnlineFriend* fr;
    float s;
    float menu_w;
    float item_h;
    float mx;
    float my;
    int count;
    int item;
    if (!g_online_context_active || g_online_context_friend < 0 ||
        g_online_context_friend >= g_online_friend_count) return -1;
    fr = &g_online_friends[g_online_context_friend];
    online_calc_layout(&L);
    s = L.ui;
    menu_w = 184.0f * s;
    item_h = 34.0f * s;
    count = online_context_item_count(fr);
    mx = clampf(g_online_context_x, 8.0f * s, L.w - menu_w - 8.0f * s);
    my = clampf(g_online_context_y, 8.0f * s,
                L.h - (float)count * item_h - 8.0f * s);
    if (x < mx || x > mx + menu_w ||
        y < my || y > my + (float)count * item_h) return -1;
    item = (int)((y - my) / item_h);
    return clampi(item, 0, count - 1);
}

static int online_context_action_at(float x, float y) {
    OnlineFriend* fr;
    int item = online_context_item_at(x, y);
    if (item < 0 || g_online_context_friend < 0 ||
        g_online_context_friend >= g_online_friend_count) return ONLINE_CONTEXT_NONE;
    fr = &g_online_friends[g_online_context_friend];
    return online_context_action_for_item(fr, item);
}

static int online_context_activate(int action) {
    int idx = g_online_context_friend;
    g_online_context_active = 0;
    if (idx < 0 || idx >= g_online_friend_count) return 0;
    if (action == ONLINE_CONTEXT_CHALLENGE) {
        online_try_send_friend_challenge(idx);
        return 1;
    }
    if (action == ONLINE_CONTEXT_MUTE) {
        online_server_send_mute_action(g_online_friends[idx].name,
                                       !g_online_friends[idx].muted);
        online_hub_set_status("Updating notification preference...");
        return 1;
    }
    if (action == ONLINE_CONTEXT_BLOCK) {
        online_server_send_username_action("friend_block",
                                           g_online_friends[idx].name);
        online_hub_set_status("Blocking user...");
        return 1;
    }
    if (action == ONLINE_CONTEXT_UNFRIEND) {
        online_server_send_username_action("friend_remove", g_online_friends[idx].name);
        online_hub_set_status("Friend removed.");
        online_remove_friend_index(idx);
        return 1;
    }
    if (action == ONLINE_CONTEXT_UNBLOCK) {
        online_server_send_username_action("friend_unblock",
                                           g_online_friends[idx].name);
        online_hub_set_status("Unblocking user...");
        return 1;
    }
    return 0;
}

static void online_context_move_selection(int delta) {
    OnlineFriend* fr;
    int count;
    if (!g_online_context_active || g_online_context_friend < 0 ||
        g_online_context_friend >= g_online_friend_count) return;
    fr = &g_online_friends[g_online_context_friend];
    count = online_context_item_count(fr);
    if (count <= 0) return;
    g_online_context_selected += delta < 0 ? -1 : 1;
    if (g_online_context_selected < 0) g_online_context_selected = count - 1;
    if (g_online_context_selected >= count) g_online_context_selected = 0;
}

static int online_open_selected_friend_context(void) {
    OnlineLayout L;
    int idx = online_selected_friend_index();
    if (idx < 0 || idx >= g_online_friend_count) {
        online_hub_set_status("Select a friend or blocked user first.");
        return 0;
    }
    online_calc_layout(&L);
    online_open_friend_context(idx, L.center_x - 92.0f * L.ui,
                               L.content_y + 28.0f * L.ui);
    return 1;
}

static void online_adjust_selected(int delta) {
    OnlineHubRow* row;
    if (g_online_selected_row < 0 || g_online_selected_row >= g_online_row_count) return;
    row = &g_online_rows[g_online_selected_row];
    if (row->kind == ONLINE_ROW_SETTING) {
        OnlineHubSetting setting = (OnlineHubSetting)row->id;
        if (online_setting_is_text(setting)) return;
        online_adjust_setting(setting, delta);
    } else if (row->kind == ONLINE_ROW_CHALLENGE_MAP &&
               g_online_challenge_map_picker.active &&
               !g_online_challenge_map_picker.loading &&
               g_online_challenge_map_picker.choice_count > 0) {
        int count = g_online_challenge_map_picker.choice_count;
        int selected = g_online_challenge_map_picker.selected + (delta < 0 ? -1 : 1);
        if (selected < 0) selected = count - 1;
        if (selected >= count) selected = 0;
        g_online_challenge_map_picker.selected = selected;
        online_hub_rebuild_rows();
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
           row->kind == ONLINE_ROW_CHALLENGE_MAP ||
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
    return !g_online_authed &&
           !online_remembered_login_in_progress() &&
           g_online_tab == ONLINE_TAB_PLAY;
}

static float online_login_checkbox_height(const OnlineLayout* L) {
    float s = L ? L->ui : online_hub_ui_scale();
    return clampf(30.0f * s, 22.0f, 38.0f);
}

static void online_login_gateway_metrics(const OnlineLayout* L,
                                         float* out_x,
                                         float* out_y,
                                         float* out_w,
    float* out_row_h,
    float* out_gap) {
    float s = L ? L->ui : online_hub_ui_scale();
    float form_w = 540.0f;
    float row_h = 52.0f * s;
    float gap = 12.0f * s;
    float checkbox_h = online_login_checkbox_height(L);
    float form_y = 0.0f;
    if (L) {
        float max_w = L->panel_w - clampf(96.0f * s, 30.0f, 96.0f);
        float min_w;
        float bottom_limit;
        float reserve;
        if (max_w < 240.0f) max_w = L->panel_w - 20.0f;
        min_w = max_w < 420.0f ? max_w : 420.0f;
        form_w = clampf(500.0f * s, min_w, max_w);
        form_y = L->content_y + 78.0f * s;

        /* Two fields, a compact checkbox, buttons, and server/status copy must remain above the footer.
         * Compact and lift the form only when the current window needs it. */
        bottom_limit = L->panel_y + L->panel_h - L->footer_h - 4.0f * s;
        reserve = 70.0f * s;
        if (form_y + row_h * 3.0f + checkbox_h + gap * 3.0f + reserve > bottom_limit) {
            float available;
            gap = clampf(7.0f * s, 5.0f, 12.0f * s);
            form_y = L->content_y + 8.0f * s;
            available = bottom_limit - reserve - form_y - checkbox_h - gap * 3.0f;
            row_h = clampf(available / 3.0f, 28.0f, row_h);
        }
    }
    if (out_w) *out_w = form_w;
    if (out_row_h) *out_row_h = row_h;
    if (out_gap) *out_gap = gap;
    if (out_x) *out_x = L ? (L->center_x - form_w * 0.5f) : 0.0f;
    if (out_y) *out_y = form_y;
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
    float checkbox_h;
    float button_w;
    online_hub_rebuild_rows();
    online_calc_layout(&L);
    online_login_gateway_metrics(&L, &form_x, &form_y, &form_w, &row_h, &gap);
    checkbox_h = online_login_checkbox_height(&L);
    if (x >= form_x && x <= form_x + form_w && y >= form_y && y <= form_y + row_h) {
        return online_find_row_by_kind_id(ONLINE_ROW_SETTING, ONLINE_SETTING_USERNAME);
    }
    if (x >= form_x && x <= form_x + form_w &&
        y >= form_y + row_h + gap && y <= form_y + row_h * 2.0f + gap) {
        return online_find_row_by_kind_id(ONLINE_ROW_SETTING, ONLINE_SETTING_PASSWORD);
    }
    if (x >= form_x && x <= form_x + form_w &&
        y >= form_y + row_h * 2.0f + gap * 2.0f &&
        y <= form_y + row_h * 2.0f + gap * 2.0f + checkbox_h) {
        return online_find_row_by_kind_id(ONLINE_ROW_SETTING, ONLINE_SETTING_REMEMBER_ME);
    }
    button_w = (form_w - gap) * 0.5f;
    y = y - (form_y + row_h * 2.0f + checkbox_h + gap * 3.0f);
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
    /* Use cursor_draw itself, not just its misc[7] artwork. The native helper
     * supplies global scaling, hotspot, shadow, animated color, and timeout. */
    (void)cursor_ext_draw_vanilla_mouse(g_online_mouse_x, g_online_mouse_y);
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
    menu_w = 184.0f * s;
    item_h = 34.0f * s;
    count = online_context_item_count(fr);
    mx = clampf(g_online_context_x, 8.0f * s, L.w - menu_w - 8.0f * s);
    my = clampf(g_online_context_y, 8.0f * s, L.h - (float)count * item_h - 8.0f * s);
    online_hub_draw_rect(mx, my, menu_w, (float)count * item_h, 0.025f, 0.030f, 0.040f, 0.98f);
    online_hub_draw_border(mx, my, menu_w, (float)count * item_h, 1.0f, 0.58f, 0.68f, 0.78f, 0.98f);
    for (int i = 0; i < count; i++) {
        int action = online_context_action_for_item(fr, i);
        int selected = (i == g_online_context_selected);
        int sent = (action == ONLINE_CONTEXT_CHALLENGE)
            ? online_sent_challenge_pending(fr->name)
            : 0;
        const char* label = "";
        float y = my + (float)i * item_h;
        switch (action) {
            case ONLINE_CONTEXT_CHALLENGE: label = sent ? "Sent!" : "Challenge"; break;
            case ONLINE_CONTEXT_MUTE: label = fr->muted ? "Unmute" : "Mute"; break;
            case ONLINE_CONTEXT_BLOCK: label = "Block"; break;
            case ONLINE_CONTEXT_UNFRIEND: label = "Unfriend"; break;
            case ONLINE_CONTEXT_UNBLOCK: label = "Unblock"; break;
            default: break;
        }
        if (selected) {
            online_hub_draw_rect(mx + 2.0f * s, y + 2.0f * s, menu_w - 4.0f * s, item_h - 4.0f * s,
                                 action == ONLINE_CONTEXT_CHALLENGE ? (sent ? 0.08f : 0.06f) : 0.16f,
                                 action == ONLINE_CONTEXT_CHALLENGE ? (sent ? 0.10f : 0.18f) : 0.06f,
                                 action == ONLINE_CONTEXT_CHALLENGE ? (sent ? 0.14f : 0.15f) : 0.08f,
                                 0.96f);
        }
        mods_restore_render_state();
        online_hub_text(mx + 12.0f * s, y + item_h * 0.5f - 9.0f * s,
                        0.76f * s,
                        action == ONLINE_CONTEXT_CHALLENGE ? (sent ? 0.72f : 0.76f) : 1.0f,
                        action == ONLINE_CONTEXT_CHALLENGE ? (sent ? 0.82f : 1.0f) : 0.70f,
                        action == ONLINE_CONTEXT_CHALLENGE ? (sent ? 0.94f : 0.92f) : 0.72f,
                        label);
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
        if (!g_online_pending_match.active) return;
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
        if (!ggpo_net_active() || !ggpo_net_connected()) {
            snprintf(line, sizeof(line), "Connecting to opponent...");
        } else if (!ggpo_net_link_ready()) {
            snprintf(line, sizeof(line), "Confirming connection...");
        } else if (!g_online_pending_match.prematch_prepared) {
            snprintf(line, sizeof(line), "Preparing match...");
        } else if (!ggpo_net_prematch_ready()) {
            snprintf(line, sizeof(line), "Synchronizing players...");
        } else if (g_online_pending_match.launch_countdown_frames > 0) {
            snprintf(line, sizeof(line), "Starting in %d", seconds);
        } else if (g_online_pending_match.server_start_reported &&
                   !g_online_pending_match.server_committed) {
            snprintf(line, sizeof(line), "Waiting for opponent setup...");
        } else if (!g_online_pending_match.server_committed) {
            snprintf(line, sizeof(line), "Finalizing match setup...");
        } else {
            snprintf(line, sizeof(line), "Starting match...");
        }
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
    float checkbox_h;
    float checkbox_y;
    float checkbox_size;
    float checkbox_x;
    float checkbox_box_y;
    float button_y;
    float stack_end;
    float button_w;
    char username[ONLINE_HUB_TEXT_MAX];
    char password[ONLINE_HUB_TEXT_MAX];
    char server[192];
    int user_selected;
    int pass_selected;
    int remember_selected;
    int login_selected;
    int register_selected;
    if (!L) return;
    s = L->ui;
    online_login_gateway_metrics(L, &form_x, &form_y, &form_w, &row_h, &gap);
    checkbox_h = online_login_checkbox_height(L);
    checkbox_y = form_y + row_h * 2.0f + gap * 2.0f;
    checkbox_size = clampf(22.0f * s, 18.0f, checkbox_h - 2.0f);
    checkbox_x = form_x + 12.0f * s;
    checkbox_box_y = checkbox_y + (checkbox_h - checkbox_size) * 0.5f;
    button_y = form_y + row_h * 2.0f + checkbox_h + gap * 3.0f;
    stack_end = button_y + row_h;
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
    remember_selected = (g_online_selected_row == online_find_row_by_kind_id(ONLINE_ROW_SETTING, ONLINE_SETTING_REMEMBER_ME));
    login_selected = (g_online_selected_row == online_find_row_by_kind_id(ONLINE_ROW_ACTION, ONLINE_ACTION_LOGIN));
    register_selected = (g_online_selected_row == online_find_row_by_kind_id(ONLINE_ROW_ACTION, ONLINE_ACTION_REGISTER));

    mods_restore_render_state();
    online_hub_text_center(L->center_x, L->panel_y + 48.0f * s,
                           2.05f * s, 0.94f, 0.98f, 1.00f, "EGGNOGG+ ONLINE");
    online_hub_text_center(L->center_x, L->panel_y + 91.0f * s,
                           1.00f * s, 0.70f, 0.94f, 0.90f,
                           g_online_server_state == ONLINE_SERVER_CONNECTED ? "Connected. Sign in to continue." : "Sign in to play online.");

    online_hub_draw_rect(form_x - 30.0f * s, form_y - 42.0f * s,
                         form_w + 60.0f * s, stack_end - form_y + 106.0f * s,
                         0.030f, 0.038f, 0.050f, 0.90f);
    online_hub_draw_border(form_x - 30.0f * s, form_y - 42.0f * s,
                           form_w + 60.0f * s, stack_end - form_y + 106.0f * s,
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

    /* Compact, conventional checkbox: the whole label line is clickable, but
     * only the square communicates the boolean state. */
    online_hub_draw_rect(checkbox_x, checkbox_box_y, checkbox_size, checkbox_size,
                         g_online_cfg.remember_me ? 0.055f : 0.030f,
                         g_online_cfg.remember_me ? 0.180f : 0.050f,
                         g_online_cfg.remember_me ? 0.145f : 0.070f,
                         0.98f);
    online_hub_draw_border(checkbox_x, checkbox_box_y, checkbox_size, checkbox_size,
                           remember_selected ? 2.0f : 1.0f,
                           remember_selected ? 0.42f : 0.28f,
                           remember_selected ? 0.92f : 0.52f,
                           remember_selected ? 0.82f : 0.62f,
                           0.92f);
    if (g_online_cfg.remember_me) {
        hooks_ui_draw_line(checkbox_x + checkbox_size * 0.20f,
                           checkbox_box_y + checkbox_size * 0.53f,
                           checkbox_x + checkbox_size * 0.43f,
                           checkbox_box_y + checkbox_size * 0.76f,
                           clampf(2.8f * s, 2.0f, 4.0f),
                           0.62f, 1.0f, 0.78f, 1.0f);
        hooks_ui_draw_line(checkbox_x + checkbox_size * 0.43f,
                           checkbox_box_y + checkbox_size * 0.76f,
                           checkbox_x + checkbox_size * 0.83f,
                           checkbox_box_y + checkbox_size * 0.25f,
                           clampf(2.8f * s, 2.0f, 4.0f),
                           0.62f, 1.0f, 0.78f, 1.0f);
    }
    mods_restore_render_state();
    online_hub_text(checkbox_x + checkbox_size + 10.0f * s,
                    checkbox_y + (checkbox_h - 12.0f * s) * 0.5f,
                    0.92f * s,
                    remember_selected ? 0.88f : 0.70f,
                    remember_selected ? 0.98f : 0.82f,
                    remember_selected ? 0.94f : 0.90f,
                    "Remember me");
    online_hub_draw_button_box(form_x, button_y,
                               button_w, row_h, "LOG IN", "",
                               login_selected, 1, s);
    online_hub_draw_button_box(form_x + button_w + gap, button_y,
                               button_w, row_h, "REGISTER", "",
                               register_selected, 0, s);

    mods_restore_render_state();
    online_hub_text_center(L->center_x, stack_end + 28.0f * s,
                           0.92f * s, 0.66f, 0.76f, 0.86f, server);
    if (g_online_status[0]) {
        online_hub_text_center(L->center_x, stack_end + 58.0f * s,
                               0.98f * s, 0.84f, 0.94f, 1.00f, g_online_status);
    }
    online_hub_text(L->panel_x + 18.0f * s, L->panel_y + L->panel_h - L->footer_h + 12.0f * s,
                    0.92f * s, 0.54f, 0.66f, 0.74f, "ESC");
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

    if (g_online_pending_match.active) {
        online_hub_render_match_countdown(&L);
        return;
    }

    if (g_online_queue_mode && !g_online_pending_match.active) {
        online_hub_render_queue_overlay(&L);
        online_hub_text(L.panel_x + 18.0f * s, L.panel_y + L.panel_h - L.footer_h + 12.0f * s,
                        0.88f * s, 0.40f, 0.48f, 0.58f, "ESC");
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
                    if (row->kind == ONLINE_ROW_FRIEND &&
                        _strnicmp(right, "online", 6) == 0) {
                        rr = 0.62f; rg = 0.96f; rb = 0.70f;
                    } else if (row->kind == ONLINE_ROW_FRIEND &&
                               (_strnicmp(right, "casual queue", 12) == 0 ||
                                _strnicmp(right, "competitive queue", 17) == 0)) {
                        rr = 0.46f; rg = 0.88f; rb = 1.0f;
                    } else if (row->kind == ONLINE_ROW_FRIEND &&
                               _strnicmp(right, "setting up match", 16) == 0) {
                        rr = 1.0f; rg = 0.82f; rb = 0.46f;
                    } else if (row->kind == ONLINE_ROW_FRIEND &&
                               _strnicmp(right, "in match", 8) == 0) {
                        rr = 0.86f; rg = 0.66f; rb = 1.0f;
                    } else if (row->kind == ONLINE_ROW_FRIEND &&
                               _strnicmp(right, "offline", 7) == 0) {
                        rr = 0.55f; rg = 0.60f; rb = 0.68f;
                    } else if (row->kind == ONLINE_ROW_FRIEND &&
                               _stricmp(right, "blocked") == 0) {
                        rr = 1.0f; rg = 0.62f; rb = 0.64f;
                    }
                    if (row->kind == ONLINE_ROW_FRIEND && row->id >= 0 && row->id < g_online_friend_count &&
                        online_friend_can_challenge(&g_online_friends[row->id])) {
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
    if (g_online_tab == ONLINE_TAB_FRIENDS && !g_online_challenge_map_picker.active) {
        online_hub_text_right(L.panel_x + L.panel_w - 18.0f * s,
                              L.panel_y + L.panel_h - L.footer_h + 12.0f * s,
                              0.72f * s, 0.40f, 0.48f, 0.58f,
                              "C / X  SOCIAL MENU");
    }
    online_hub_draw_context_menu();
}

static void online_launch_clear(void) {
    memset(&g_online_launch.request, 0, sizeof(g_online_launch.request));
    g_online_launch.hub_open_requested = 0;
    g_online_launch.waiting_status_shown = 0;
    g_online_launch.deadline_ms = 0;
}

static void online_launch_accept_request(const LaunchRequest* request,
                                         const char* source) {
    if (!request || request->action == LAUNCH_REQUEST_NONE) return;
    if (g_online_launch.request.action != LAUNCH_REQUEST_NONE &&
        (g_online_launch.request.action != request->action ||
         strcmp(g_online_launch.request.target, request->target) != 0)) {
        LOG_INFO("online.launch: replacing pending %s intent with %s intent from %s",
                 launch_request_action_name(g_online_launch.request.action),
                 launch_request_action_name(request->action),
                 source ? source : "external activation");
    }
    g_online_launch.request = *request;
    g_online_launch.hub_open_requested = 0;
    g_online_launch.waiting_status_shown = 0;
    g_online_launch.deadline_ms = (DWORD)online_control_deadline_after(
        (uint32_t)GetTickCount(), ONLINE_LAUNCH_TIMEOUT_MS);
    LOG_INFO("online.launch: accepted %s intent from %s",
             launch_request_action_name(request->action),
             source ? source : "external activation");
}

static void online_launch_poll_forwarded_requests(void) {
    LaunchRequest request;
    while (launch_ipc_poll(&request)) {
        online_launch_accept_request(&request, "running-instance handoff");
    }
}

/*
 * Parse via CommandLineToArgvW at a normal update boundary, never from
 * DllMain. UTF-8 conversion is only an interchange step: the launch parser
 * accepts an intentionally tiny ASCII URI/switch grammar and canonical
 * lowercase account names.
 */
static void online_launch_parse_process_args(void) {
    LPWSTR* wide_args = NULL;
    char** utf8_args = NULL;
    int argc = 0;
    int i;
    int conversion_ok = 1;
    char error[192];
    LaunchRequest request;
    LaunchRequestParseResult parsed;

    if (g_online_launch.parsed) return;
    g_online_launch.parsed = 1;
    wide_args = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!wide_args || argc <= 0 || argc > 128) {
        LOG_WARN("online.launch: command line could not be safely tokenized");
        if (wide_args) LocalFree(wide_args);
        return;
    }
    utf8_args = (char**)calloc((size_t)argc, sizeof(*utf8_args));
    if (!utf8_args) {
        LOG_WARN("online.launch: command-line allocation failed");
        LocalFree(wide_args);
        return;
    }
    for (i = 0; i < argc; i++) {
        int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                        wide_args[i], -1, NULL, 0,
                                        NULL, NULL);
        if (bytes <= 0) {
            conversion_ok = 0;
            break;
        }
        utf8_args[i] = (char*)malloc((size_t)bytes);
        if (!utf8_args[i] ||
            WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                wide_args[i], -1, utf8_args[i], bytes,
                                NULL, NULL) != bytes) {
            conversion_ok = 0;
            break;
        }
    }
    error[0] = '\0';
    memset(&request, 0, sizeof(request));
    parsed = conversion_ok
        ? launch_request_parse_args(argc,
                                    (const char* const*)utf8_args,
                                    &request,
                                    error,
                                    sizeof(error))
        : LAUNCH_REQUEST_PARSE_ERROR;
    for (i = 0; i < argc; i++) free(utf8_args[i]);
    free(utf8_args);
    LocalFree(wide_args);

    if (!conversion_ok) {
        LOG_WARN("online.launch: command line was not valid Unicode");
        return;
    }
    if (parsed == LAUNCH_REQUEST_PARSE_ERROR) {
        LOG_WARN("online.launch: request rejected (%s)",
                 error[0] ? error : "invalid request");
        return;
    }
    if (parsed != LAUNCH_REQUEST_PARSE_OK) return;
    online_launch_accept_request(&request, "process command line");
}

static int online_launch_find_friend(const char* username) {
    int i;
    if (!username || !username[0]) return -1;
    for (i = 0; i < g_online_friend_count; i++) {
        if (_stricmp(g_online_friends[i].name, username) == 0) return i;
    }
    return -1;
}

static void online_launch_select_first_inbox_row(void) {
    int i;
    for (i = 0; i < g_online_row_count; i++) {
        if (g_online_rows[i].kind == ONLINE_ROW_FRIEND_REQUEST ||
            g_online_rows[i].kind == ONLINE_ROW_CHALLENGE) {
            g_online_selected_row = i;
            online_ensure_scroll_visible();
            return;
        }
    }
}

static void online_launch_pump(void) {
    LaunchRequestAction action;
    uint32_t now;
    int desired_queue;
    int friend_index;

    online_launch_parse_process_args();
    online_launch_poll_forwarded_requests();
    action = g_online_launch.request.action;
    if (action == LAUNCH_REQUEST_NONE) return;
    now = (uint32_t)GetTickCount();
    if (online_control_deadline_reached(
            now, (uint32_t)g_online_launch.deadline_ms)) {
        LOG_WARN("online.launch: %s intent expired",
                 launch_request_action_name(action));
        if (is_online_hub_state_active()) {
            online_hub_set_status("Launch request expired.");
        }
        online_launch_clear();
        return;
    }
    /*
     * A link activation is navigation, never an out-of-band forfeit button.
     * Keep the validated intent pending until the current match/setup has
     * reached its normal terminal state.
     */
    if (g_online_pending_match.active || g_online_active_match.active) return;
    if (!g_online_launch.hub_open_requested) {
        g_online_launch.hub_open_requested = 1;
        online_hub_open();
    }
    if (!is_online_hub_state_active()) return;
    if (action == LAUNCH_REQUEST_HUB) {
        LOG_INFO("online.launch: completed hub intent");
        online_launch_clear();
        return;
    }
    if (!g_online_authed) {
        if (!g_online_launch.waiting_status_shown) {
            online_hub_set_status(g_online_auth_pending
                ? "Signing in to continue launch request..."
                : "Sign in to continue launch request.");
            g_online_launch.waiting_status_shown = 1;
        }
        return;
    }

    if (action == LAUNCH_REQUEST_REQUESTS) {
        g_online_tab = ONLINE_TAB_FRIENDS;
        online_hub_rebuild_rows();
        online_launch_select_first_inbox_row();
        LOG_INFO("online.launch: completed requests intent");
        online_launch_clear();
        return;
    }
    if (action == LAUNCH_REQUEST_QUEUE_CASUAL ||
        action == LAUNCH_REQUEST_QUEUE_COMPETITIVE) {
        desired_queue = action == LAUNCH_REQUEST_QUEUE_COMPETITIVE ? 2 : 1;
        g_online_tab = ONLINE_TAB_PLAY;
        if (g_online_queue_mode == desired_queue) {
            online_hub_set_status(desired_queue == 2
                ? "Already in competitive queue."
                : "Already in casual queue.");
            online_launch_clear();
            return;
        }
        online_server_send_queue(desired_queue == 2
            ? "competitive" : "casual");
        if (g_online_queue_mode == desired_queue) {
            online_hub_rebuild_rows();
            LOG_INFO("online.launch: completed %s intent",
                     desired_queue == 2
                        ? "queue/competitive" : "queue/casual");
            online_launch_clear();
        }
        return;
    }
    if (action == LAUNCH_REQUEST_CHALLENGE) {
        g_online_tab = ONLINE_TAB_FRIENDS;
        if (!g_online_friend_snapshot_complete) {
            if (!g_online_launch.waiting_status_shown) {
                online_hub_set_status("Loading friends for launch request...");
                g_online_launch.waiting_status_shown = 1;
            }
            return;
        }
        online_hub_rebuild_rows();
        friend_index = online_launch_find_friend(
            g_online_launch.request.target);
        if (friend_index < 0) {
            char status[128];
            snprintf(status, sizeof(status),
                     "%s is not an available friend.",
                     g_online_launch.request.target);
            online_hub_set_status(status);
            LOG_INFO("online.launch: completed challenge intent; target %s unavailable",
                     g_online_launch.request.target);
            online_launch_clear();
            return;
        }
        (void)online_try_send_friend_challenge(friend_index);
        LOG_INFO("online.launch: completed challenge intent for %s",
                 g_online_launch.request.target);
        online_launch_clear();
    }
}

/* Framework overlays are not stable Back destinations for the online hub. */
static void* online_hub_sanitize_return_state(void* state) {
    if (!state ||
        state == (void*)&g_online_hub_state ||
        state == (void*)&g_console_state) {
        return (void*)(uintptr_t)ADDR_MAIN_STATE;
    }
    return state;
}

static void online_hub_close_to_return_state(void) {
    if (!p_state_switch) return;
    if (g_online_launch.request.action != LAUNCH_REQUEST_NONE) {
        LOG_INFO("online.launch: pending intent cancelled by leaving hub");
        online_launch_clear();
    }
    g_online_return_state = online_hub_sanitize_return_state(g_online_return_state);
    p_state_switch(g_online_return_state);
}

static void online_hub_open(void) {
    void* cur;
    if (!p_state_switch) return;
    online_hub_load();
    cur = p_state_current ? p_state_current() : NULL;
    if (cur == (void*)&g_console_state) cur = g_console_return_state;
    cur = online_hub_sanitize_return_state(cur);
    if (is_online_hub_state_active()) {
        g_online_return_state = cur;
        return;
    }
    g_online_pending_return_state = cur;
    g_online_open_pending = 1;
}

static void __cdecl online_hub_enter(void) {
    void* last = p_state_last ? p_state_last() : (void*)(uintptr_t)ADDR_MAIN_STATE;
    int abandoned = 0;

    /* Entering the hub means we are no longer in a match. Cut any lingering P2P
     * session and clear match state immediately so a fresh queue can't start on
     * top of a stale connection (which caused cross-match weirdness when leaving
     * a game and re-queuing within the ~1.5s abandon grace). If a live match was
     * still unreported, this counts as a forfeit/loss. The launch sequence is
     * exempt: it routes through the hub with g_online_pending_match active and
     * the active match not yet begun, so we must not tear that down. */
    if (!g_online_pending_match.active) {
        if (g_online_active_match.active && !g_online_active_match.result_reported) {
            if (g_online_active_match.server_committed) {
                online_forfeit_active_match("Left the match - counted as a loss.",
                                            "player left committed match");
            } else {
                g_online_active_match.result_reported = 1;
                online_server_send_match_abort("left match before server commit");
            }
            abandoned = 1;
        }
        if (ggpo_net_active()) stop_ggpo_net("left match to hub");
        online_clear_match_state();
    }

    online_hub_load();
    if (g_online_force_main_return_once) {
        /* A completed GAME is never a valid Back owner. The handoff captured
         * main before switching, but p_state_last() still names that dead GAME
         * while this enter callback runs, so consume the explicit override. */
        g_online_force_main_return_once = 0;
        last = (void*)(uintptr_t)ADDR_MAIN_STATE;
    } else if (!last ||
        last == (void*)&g_online_hub_state ||
        last == (void*)&g_console_state) {
        /* online_hub_open()/the pre-swap handoff already captured the real
         * owner. Preserve it instead of making a transient last state the
         * next Back destination. */
        last = g_online_return_state;
    }
    g_online_return_state = online_hub_sanitize_return_state(last);
    online_clear_capture_state();
    online_hub_apply_net_settings();
    if (g_online_pending_connect_fail_status) {
        int failure = g_online_pending_connect_fail_status;
        g_online_pending_connect_fail_status = 0;
        if (failure == 2) {
            online_hub_set_status("Online server disconnected; match ended.");
        } else if (failure == 3) {
            online_hub_set_status("P2P stopped; match cancelled.");
        } else if (failure == 4) {
            online_hub_set_status(g_online_pending_connect_fail_reason[0]
                ? g_online_pending_connect_fail_reason
                : "Online match setup failed; see the framework log.");
        } else {
            online_hub_set_status("Couldn't connect to opponent.");
        }
        g_online_pending_connect_fail_reason[0] = '\0';
    } else if (abandoned) {
        online_hub_set_status("Left the match - counted as a loss.");
    } else if (!g_online_pending_match.active && !g_online_result.active) {
        online_hub_set_status("");
    }
    (void)online_try_remembered_login();
    online_hub_rebuild_rows();
    LOG_INFO("ONLINE HUB: enter%s", abandoned ? " (abandoned live match; P2P closed)" : "");
}

static void __cdecl online_hub_update(void) {
    online_sent_challenge_prune();
    online_server_update();
    /*
     * The native menu update hook parses the process URI and opens this custom
     * state, but custom states do not pass through that hook afterward. Keep
     * pumping the still-owned launch request here so manual or remembered
     * authentication resumes requests/queue/challenge instead of stranding the
     * user at the hub's default Play page. Running after server_update lets an
     * auth_ok complete the requested action in the same hub tick.
     */
    online_launch_pump();
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
    if (p_main_draw && cursor_ext_call_with_vanilla_mouse_hidden(p_main_draw)) {
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
    online_clear_capture_state();
    if (!g_online_authed && !g_online_auth_pending) {
        online_clear_password_memory();
    }
    online_hub_save();
    mods_restore_render_state();
    LOG_INFO("ONLINE HUB: leave");
}

static const char* online_result_title(OnlineMatchResult result) {
    if (result == ONLINE_MATCH_RESULT_WIN) return "YOU WON";
    if (result == ONLINE_MATCH_RESULT_LOSS) return "YOU LOST";
    if (result == ONLINE_MATCH_RESULT_DRAW) return "DRAW";
    return "MATCH COMPLETE";
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
    float h = (g_online_result.rematch_state != ONLINE_REMATCH_NONE
                   ? 190.0f
                   : 128.0f) * s;
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

static int online_result_rematch_seconds_left(void) {
    uint32_t now_ms;
    int32_t left_ms;
    if (!online_result_has_live_rematch() ||
        g_online_result.rematch_state == ONLINE_REMATCH_STARTING ||
        g_online_result.rematch_deadline_ms == 0u) {
        return 0;
    }
    now_ms = (uint32_t)GetTickCount();
    left_ms = (int32_t)((uint32_t)g_online_result.rematch_deadline_ms - now_ms);
    if (left_ms <= 0) return 0;
    return (left_ms + 999) / 1000;
}

static void online_result_rematch_button_metrics(float* out_primary_x,
                                                 float* out_primary_y,
                                                 float* out_primary_w,
                                                 float* out_primary_h,
                                                 float* out_secondary_x,
                                                 float* out_secondary_y,
                                                 float* out_secondary_w,
                                                 float* out_secondary_h) {
    float x;
    float y;
    float w;
    float h;
    float s = online_hub_ui_scale();
    float button_h = 34.0f * s;
    float gap = 10.0f * s;
    float primary_w = 142.0f * s;
    float secondary_w = 126.0f * s;
    float secondary_x;
    float primary_x;
    float button_y;
    online_result_toast_metrics(&x, &y, &w, &h, NULL, NULL, NULL);
    secondary_x = x + w - 18.0f * s - secondary_w;
    primary_x = secondary_x - gap - primary_w;
    button_y = y + h - button_h - 14.0f * s;
    if (out_primary_x) *out_primary_x = primary_x;
    if (out_primary_y) *out_primary_y = button_y;
    if (out_primary_w) *out_primary_w = primary_w;
    if (out_primary_h) *out_primary_h = button_h;
    if (out_secondary_x) *out_secondary_x = secondary_x;
    if (out_secondary_y) *out_secondary_y = button_y;
    if (out_secondary_w) *out_secondary_w = secondary_w;
    if (out_secondary_h) *out_secondary_h = button_h;
}

static int online_result_rematch_action_at(float x, float y) {
    float px;
    float py;
    float pw;
    float ph;
    float sx;
    float sy;
    float sw;
    float sh;
    OnlineRematchState state = g_online_result.rematch_state;
    if (!online_result_has_live_rematch() ||
        !g_online_result.toast_visible ||
        state == ONLINE_REMATCH_STARTING) {
        return 0;
    }
    online_result_rematch_button_metrics(
        &px, &py, &pw, &ph, &sx, &sy, &sw, &sh);
    if ((state == ONLINE_REMATCH_AVAILABLE || state == ONLINE_REMATCH_OFFERED) &&
        x >= px && x <= px + pw && y >= py && y <= py + ph) {
        return 1;
    }
    if (x >= sx && x <= sx + sw && y >= sy && y <= sy + sh) return 2;
    return 0;
}

static int online_result_rematch_activate(int action) {
    OnlineRematchState state = g_online_result.rematch_state;
    if (!online_result_has_live_rematch()) return 0;
    if (action == 1 &&
        (state == ONLINE_REMATCH_AVAILABLE || state == ONLINE_REMATCH_OFFERED)) {
        return online_server_send_rematch_request();
    }
    if (action == 2 && state != ONLINE_REMATCH_STARTING) {
        (void)online_server_send_rematch_decline();
        online_result_rematch_clear(
            state == ONLINE_REMATCH_WAITING ? "Rematch cancelled." : "Rematch declined.");
        g_online_result.toast_age = 0;
        g_online_result.toast_lifetime = 240;
        return 1;
    }
    return 0;
}

static void online_result_dismiss(int notify_server) {
    if (!g_online_result.active) return;
    if (notify_server &&
        online_result_has_live_rematch() &&
        g_online_result.rematch_state != ONLINE_REMATCH_STARTING) {
        (void)online_server_send_rematch_decline();
    }
    g_online_result.toast_visible = 0;
    if (g_online_result.server_confirmed) {
        memset(&g_online_result, 0, sizeof(g_online_result));
    }
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
    float primary_x;
    float primary_y;
    float primary_w;
    float primary_h;
    float secondary_x;
    float secondary_y;
    float secondary_w;
    float secondary_h;
    float alpha;
    int hover;
    int primary_hot = 0;
    int secondary_hot = 0;
    int rematch_left = 0;
    const char* primary_label = NULL;
    const char* secondary_label = NULL;
    char line[256];
    if (!g_online_result.active || !g_online_result.toast_visible) return;
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
                    g_online_result.server_confirmed
                        ? online_result_title(g_online_result.result)
                        : "RESULT REPORTED");
    snprintf(line, sizeof(line), "%s - %s",
             g_online_result.opponent[0] ? g_online_result.opponent : "opponent",
             g_online_result.map_label[0] ? g_online_result.map_label : "selected map");
    online_hub_text_alpha(x + 18.0f * s, y + 60.0f * s,
                    0.78f * s, 0.86f, 0.91f, 0.98f, alpha, line);
    if (g_online_result.server_confirmed &&
        g_online_result.competitive &&
        g_online_result.elo_delta_valid) {
        int delta = g_online_result.elo_after - g_online_result.elo_before;
        snprintf(line, sizeof(line), "Elo %d (%+d)", g_online_result.elo_after, delta);
    } else {
        safe_copy(line, sizeof(line),
                  g_online_result.status[0]
                      ? g_online_result.status
                      : (g_online_result.server_confirmed ? "Match complete." : "Waiting for server result..."));
    }
    online_hub_text_alpha(x + 18.0f * s, y + 91.0f * s,
                    0.76f * s, 0.70f, 0.80f, 0.90f, alpha, line);
    if (g_online_result.rematch_state != ONLINE_REMATCH_NONE) {
        rematch_left = online_result_rematch_seconds_left();
        if (g_online_result.rematch_state == ONLINE_REMATCH_STARTING) {
            safe_copy(line, sizeof(line), "PRIVATE REMATCH  |  STARTING...");
        } else {
            snprintf(line, sizeof(line), "PRIVATE REMATCH%s  |  %ds",
                     g_online_result.rematch_unranked ? "  |  UNRANKED" : "",
                     rematch_left);
        }
        online_hub_text_alpha(x + 18.0f * s, y + 121.0f * s,
                              0.72f * s, 0.78f, 0.90f, 0.98f, alpha, line);
        if (g_online_result.rematch_state != ONLINE_REMATCH_STARTING) {
            online_result_rematch_button_metrics(
                &primary_x, &primary_y, &primary_w, &primary_h,
                &secondary_x, &secondary_y, &secondary_w, &secondary_h);
            primary_hot = online_result_rematch_action_at(
                g_online_mouse_x, g_online_mouse_y) == 1;
            secondary_hot = online_result_rematch_action_at(
                g_online_mouse_x, g_online_mouse_y) == 2;
            if (g_online_result.rematch_state == ONLINE_REMATCH_AVAILABLE) {
                primary_label = "REMATCH  R / X";
                secondary_label = "NO  N / Y";
            } else if (g_online_result.rematch_state == ONLINE_REMATCH_OFFERED) {
                primary_label = "ACCEPT  R / X";
                secondary_label = "DECLINE  N / Y";
            } else {
                primary_label = "WAITING...";
                secondary_label = "CANCEL  N / Y";
            }
            online_hub_draw_rect(primary_x, primary_y, primary_w, primary_h,
                                 primary_hot ? 0.10f : 0.08f,
                                 primary_hot ? 0.30f : 0.20f,
                                 primary_hot ? 0.18f : 0.15f,
                                 0.94f * alpha);
            online_hub_draw_border(primary_x, primary_y, primary_w, primary_h,
                                   primary_hot ? 2.0f : 1.0f,
                                   0.38f, 0.92f, 0.56f, 0.95f * alpha);
            online_hub_draw_rect(secondary_x, secondary_y, secondary_w, secondary_h,
                                 secondary_hot ? 0.30f : 0.22f,
                                 secondary_hot ? 0.09f : 0.08f,
                                 secondary_hot ? 0.11f : 0.10f,
                                 0.94f * alpha);
            online_hub_draw_border(secondary_x, secondary_y, secondary_w, secondary_h,
                                   secondary_hot ? 2.0f : 1.0f,
                                   0.94f, 0.42f, 0.46f, 0.95f * alpha);
            online_hub_text_center_alpha(
                primary_x + primary_w * 0.5f,
                primary_y + primary_h * 0.5f - 8.0f * s,
                0.66f * s, 0.82f, 1.00f, 0.88f, alpha, primary_label);
            online_hub_text_center_alpha(
                secondary_x + secondary_w * 0.5f,
                secondary_y + secondary_h * 0.5f - 8.0f * s,
                0.66f * s, 1.00f, 0.78f, 0.80f, alpha, secondary_label);
        }
    }
    online_ui_close_button(close_x, close_y, close_size, alpha, hover);
}

static void online_result_tick_toast(void) {
    if (!g_online_result.active || !g_online_result.toast_visible) return;
    if (g_online_result.toast_lifetime <= 0) g_online_result.toast_lifetime = ONLINE_RESULT_TOAST_FRAMES;
    if (online_result_has_live_rematch()) {
        if (g_online_result.rematch_state != ONLINE_REMATCH_STARTING &&
            online_result_rematch_seconds_left() <= 0) {
            online_result_rematch_clear("Rematch offer expired.");
            g_online_result.toast_age = 0;
            g_online_result.toast_lifetime = 240;
            return;
        }
        if (g_online_result.toast_age < g_online_result.toast_lifetime - 90) {
            g_online_result.toast_age++;
        }
        return;
    }
    g_online_result.toast_age++;
    if (g_online_result.toast_age >= g_online_result.toast_lifetime) {
        online_result_dismiss(1);
    }
}

static int online_result_close_at(float x, float y) {
    float close_x;
    float close_y;
    float close_size;
    if (!g_online_result.active || !g_online_result.toast_visible) return 0;
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

static void online_challenge_toast_show(const char* from, int id, int elo, int expires_in, const char* map_label) {
    if (!g_online_cfg.challenge_notifications || !from || !from[0]) return;
    memset(&g_online_challenge_toast, 0, sizeof(g_online_challenge_toast));
    g_online_challenge_toast.active = 1;
    g_online_challenge_toast.id = id;
    g_online_challenge_toast.elo = elo;
    if (expires_in <= 0) expires_in = 300;
    g_online_challenge_toast.expires_ms = (uint32_t)GetTickCount() + (uint32_t)expires_in * 1000u;
    safe_copy(g_online_challenge_toast.from, sizeof(g_online_challenge_toast.from), from);
    safe_copy(g_online_challenge_toast.map_label,
              sizeof(g_online_challenge_toast.map_label),
              (map_label && map_label[0]) ? map_label : "Compatible map");
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
    snprintf(line, sizeof(line), "%s  |  expires in %ds",
             g_online_challenge_toast.map_label[0]
                 ? g_online_challenge_toast.map_label
                 : "Compatible map",
             left);
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

static void menu_mode_accent(float* r, float* g, float* b) {
    if (g_menu_mode == MENU_MODE_ONLINE) {
        *r = 0.36f; *g = 0.92f; *b = 0.82f;   /* hub cyan (selected-tab accent) */
        return;
    }
    if (g_menu_mode >= MENU_MODE_CUSTOM0 &&
        lua_manager_menu_mode_info(g_menu_mode - MENU_MODE_CUSTOM0, NULL, NULL, r, g, b)) {
        return;
    }
    *r = 1.00f; *g = 0.22f; *b = 0.26f;       /* PLAY: vibrant red */
}

static void btn_write_rgba(void* btn, int ofs, float r, float g, float b, float a) {
    float* c;
    if (!btn) return;
    if (IsBadWritePtr((uint8_t*)btn + ofs, (SIZE_T)(sizeof(float) * 4))) return;
    c = (float*)((uint8_t*)btn + ofs);
    c[0] = r; c[1] = g; c[2] = b; c[3] = a;
}

void hooks_online_on_pre_swap(void) {
    void* state_ptr = p_state_current ? p_state_current() : NULL;
    int local_player;
    int remote_player;
    int local_room = 0;
    int remote_room = 0;
    float x;
    float y;
    g_online_result_toast_rendered_this_swap = 0;
    g_online_challenge_toast_rendered_this_swap = 0;
    if (g_online_active_match.active && ggpo_net_active()) {
        online_viewport_poll(NULL, 0);
    }
    if (g_online_open_pending && !online_native_finish_is_presenting()) {
        g_online_open_pending = 0;
        g_online_return_state = online_hub_sanitize_return_state(g_online_pending_return_state);
        (void)console_capture_background_now();
        if (p_state_switch && !is_online_hub_state_active()) {
            p_state_switch((void*)&g_online_hub_state);
            state_ptr = (void*)&g_online_hub_state;
        }
    }

    if (g_online_result.active && g_online_result.toast_visible) {
        online_result_render_toast();
        g_online_result_toast_rendered_this_swap = 1;
        online_result_tick_toast();
        mods_restore_render_state();
        if (p_main_sprite_batches_draw) p_main_sprite_batches_draw();
        mods_restore_render_state();
    }
    if (g_online_challenge_toast.active) {
        online_challenge_toast_render();
        if (g_online_challenge_toast.active) {
            g_online_challenge_toast_rendered_this_swap = 1;
        }
        online_challenge_toast_tick();
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

static int online_cursor_over_active_overlay(float x, float y) {
    float bx;
    float by;
    float bw;
    float bh;

    /* Native menus and Lua custom states already drew their own cursor before
     * these late overlays. Queue the final online cursor only where the panel
     * would cover that earlier cursor; outside the panel, drawing again would
     * double the native shadow/antialiased edge for no visual benefit. */
    if ((g_online_result.active && g_online_result.toast_visible) ||
        g_online_result_toast_rendered_this_swap) {
        online_result_toast_metrics(&bx, &by, &bw, &bh, NULL, NULL, NULL);
        if (x >= bx && x <= bx + bw && y >= by && y <= by + bh) return 1;
    }
    if (g_online_challenge_toast.active ||
        g_online_challenge_toast_rendered_this_swap) {
        online_challenge_toast_metrics(&bx, &by, &bw, &bh,
                                       NULL, NULL, NULL, NULL,
                                       NULL, NULL, NULL, NULL);
        if (x >= bx && x <= bx + bw && y >= by && y <= by + bh) return 1;
    }
    return 0;
}

void hooks_online_cursor_on_pre_swap(void) {
    void* state_ptr = p_state_current ? p_state_current() : NULL;
    int draw_online_cursor = state_ptr == (void*)&g_online_hub_state ||
                             online_cursor_over_active_overlay(g_online_mouse_x,
                                                               g_online_mouse_y);
    if (!draw_online_cursor) {
        g_online_result_toast_rendered_this_swap = 0;
        g_online_challenge_toast_rendered_this_swap = 0;
        return;
    }

    /* This hook is called after every other framework pre-swap renderer. Keep
     * the native online cursor here, rather than in an individual screen or
     * toast branch, so nametags, the console, and updater notifications cannot
     * cover it and no branch can draw a duplicate/fallback cursor. */
    online_hub_draw_cursor();
    mods_restore_render_state();
    if (p_main_sprite_batches_draw) p_main_sprite_batches_draw();
    mods_restore_render_state();
    g_online_result_toast_rendered_this_swap = 0;
    g_online_challenge_toast_rendered_this_swap = 0;
}

int hooks_online_hub_active(void) {
    return is_online_hub_state_active();
}

int hooks_online_hub_keydown(int sym, int scancode, int mod) {
    (void)scancode;
    (void)mod;
    if (!is_online_hub_state_active()) return 0;
    if (online_result_has_live_rematch() && g_online_result.toast_visible) {
        if (sym == 'r' || sym == 'R') {
            (void)online_result_rematch_activate(1);
            return 1;
        }
        if (sym == 'n' || sym == 'N') {
            (void)online_result_rematch_activate(2);
            return 1;
        }
    }
    if (g_online_pending_match.active) return 1;
    if (g_online_queue_mode) {
        if (sym == SDLK_ESCAPE || sym == SDLK_BACKSPACE || sym == SDLK_DELETE) {
            online_server_leave_queue();
        }
        return 1;
    }
    if (online_remembered_login_in_progress()) {
        if (sym != SDLK_ESCAPE) return 1;
        online_server_disconnect(NULL);
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
            credential_ext_secure_zero(g_online_capture_buf, sizeof(g_online_capture_buf));
            return 1;
        }
        return 1;
    }

    if (g_online_context_active) {
        OnlineFriend* fr;
        switch (sym) {
            case SDLK_ESCAPE:
            case SDLK_BACKSPACE:
                g_online_context_active = 0;
                return 1;
            case SDLK_UP:
            case 'w': case 'W':
                online_context_move_selection(-1);
                return 1;
            case SDLK_DOWN:
            case 's': case 'S':
                online_context_move_selection(1);
                return 1;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
            case SDLK_SPACE:
                if (g_online_context_friend < 0 ||
                    g_online_context_friend >= g_online_friend_count) {
                    g_online_context_active = 0;
                    return 1;
                }
                fr = &g_online_friends[g_online_context_friend];
                online_context_activate(
                    online_context_action_for_item(fr, g_online_context_selected));
                return 1;
            default:
                return 1;
        }
    }

    online_hub_rebuild_rows();
    switch (sym) {
        case SDLK_ESCAPE:
            if (g_online_challenge_map_picker.active) {
                online_challenge_map_picker_clear();
                online_hub_set_status("");
                online_hub_rebuild_rows();
                return 1;
            }
            online_hub_close_to_return_state();
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
        case 'c': case 'C':
            if (g_online_tab == ONLINE_TAB_FRIENDS) {
                online_open_selected_friend_context();
            }
            return 1;
        default:
            return 1;
    }
}

int hooks_online_hub_textinput(const char* text) {
    size_t max_len = sizeof(g_online_capture_buf) - 1u;
    if (!is_online_hub_state_active()) return 0;
    if (!g_online_capture_active) return 1;
    if (g_online_capture_kind == ONLINE_CAPTURE_SETTING) {
        if (g_online_capture_target == ONLINE_SETTING_USERNAME) {
            max_len = sizeof(g_online_cfg.username) - 1u;
        } else if (g_online_capture_target == ONLINE_SETTING_PASSWORD) {
            max_len = sizeof(g_online_cfg.password) - 1u;
        } else if (g_online_capture_target == ONLINE_SETTING_SERVER_HOST ||
                   g_online_capture_target == ONLINE_SETTING_PEER_HOST) {
            max_len = ONLINE_HUB_TEXT_MAX - 1u;
        }
    } else if (g_online_capture_kind == ONLINE_CAPTURE_ADD_FRIEND) {
        max_len = sizeof(g_online_cfg.username) - 1u;
    }
    if (text && text[0]) {
        size_t len = strlen(g_online_capture_buf);
        for (const unsigned char* p = (const unsigned char*)text; *p; p++) {
            if (*p < 0x20 || *p == 0x7f) continue;
            if (len >= max_len) break;
            g_online_capture_buf[len++] = (char)*p;
        }
        g_online_capture_buf[len] = '\0';
    }
    return 1;
}

int hooks_online_hub_mousemotion(int x, int y) {
    g_online_mouse_x = (float)x;
    g_online_mouse_y = (float)y;
    if (!is_online_hub_state_active()) return 0;
    if (g_online_pending_match.active) {
        return 1;
    } else if (g_online_queue_mode) {
        return 1;
    } else if (g_online_context_active) {
        int item = online_context_item_at((float)x, (float)y);
        if (item >= 0) g_online_context_selected = item;
    } else {
        int hit = online_row_at_point((float)x, (float)y);
        if (hit >= 0 && hit < g_online_row_count && g_online_rows[hit].selectable) {
            g_online_selected_row = hit;
            online_ensure_scroll_visible();
        }
    }
    return 1;
}

int hooks_online_hub_mousewheel(int y) {
    if (g_online_pending_match.active) return 1;
    if (g_online_queue_mode) return 1;
    if (!is_online_hub_state_active()) return 0;
    if (g_online_context_active) {
        if (y > 0) online_context_move_selection(-1);
        else if (y < 0) online_context_move_selection(1);
    } else {
        if (y > 0) online_move_selection(-1, 3);
        else if (y < 0) online_move_selection(1, 3);
    }
    return 1;
}

int hooks_online_hub_mousebutton(int x, int y, int button, int down) {
    int row;
    int tab;
    g_online_mouse_x = (float)x;
    g_online_mouse_y = (float)y;
    if (down && button == 1) {
        int rematch_action = online_result_rematch_action_at((float)x, (float)y);
        if (rematch_action) {
            online_result_rematch_activate(rematch_action);
            return 1;
        }
        int challenge_action = online_challenge_toast_action_at((float)x, (float)y);
        if (challenge_action) {
            online_challenge_toast_activate(challenge_action);
            return 1;
        }
    }
    if (down && button == 1 && online_result_close_at((float)x, (float)y)) {
        online_result_dismiss(1);
        return 1;
    }
    if (!is_online_hub_state_active()) return 0;
    if (!down) return 1;
    if (g_online_pending_match.active) return 1;
    if (g_online_queue_mode) {
        if (button == 1 && online_queue_cancel_button_at((float)x, (float)y)) {
            online_server_leave_queue();
        }
        return 1;
    }
    if (online_remembered_login_in_progress()) return 1;
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
        if (g_online_challenge_map_picker.active) {
            online_challenge_map_picker_clear();
        }
        g_online_tab = (OnlineHubTab)tab;
        g_online_selected_row = -1;
        g_online_scroll_row = 0;
        online_clear_capture_state();
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
    if (!is_online_hub_state_active()) return 0;
    if (online_result_has_live_rematch() && g_online_result.toast_visible) {
        if (action == 7) {
            (void)online_result_rematch_activate(1);
            return 1;
        }
        if (action == 8) {
            (void)online_result_rematch_activate(2);
            return 1;
        }
    }
    if (g_online_pending_match.active) return 1;
    if (g_online_queue_mode) {
        if (action == 6) online_server_leave_queue();
        return 1;
    }
    if (online_remembered_login_in_progress()) {
        if (action != 6) return 1;
        online_server_disconnect(NULL);
    }
    if (g_online_capture_active) return 1;
    online_hub_rebuild_rows();
    if (g_online_context_active) {
        OnlineFriend* fr;
        if (g_online_context_friend < 0 ||
            g_online_context_friend >= g_online_friend_count) {
            g_online_context_active = 0;
            return 1;
        }
        fr = &g_online_friends[g_online_context_friend];
        if (action == 1) online_context_move_selection(-1);
        else if (action == 2) online_context_move_selection(1);
        else if (action == 5) {
            online_context_activate(
                online_context_action_for_item(fr, g_online_context_selected));
        } else if (action == 6) {
            g_online_context_active = 0;
        }
        return 1;
    }
    switch (action) {
        case 1: online_move_selection(-1, 1); return 1;
        case 2: online_move_selection(1, 1); return 1;
        case 3: online_adjust_selected(-1); return 1;
        case 4: online_adjust_selected(1); return 1;
        case 5: online_activate_selected(); return 1;
        case 7:
            if (g_online_tab == ONLINE_TAB_FRIENDS) {
                online_open_selected_friend_context();
                return 1;
            }
            return 1;
        case 6:
            if (g_online_challenge_map_picker.active) {
                online_challenge_map_picker_clear();
                online_hub_set_status("");
                online_hub_rebuild_rows();
                return 1;
            }
            online_hub_close_to_return_state();
            return 1;
        default:
            return 0;
    }
}

int hooks_console_active(void) {
    return is_console_state_active();
}

void hooks_console_on_pre_swap(void) {
    if (!g_console_open_pending) return;

    g_console_bg_ready = 0;
    g_console_bg_w = 0;
    g_console_bg_h = 0;
    (void)console_capture_background_now();

    g_console_open_pending = 0;
    g_console_open_ready = 1;
}

#define UPDATE_HANDOFF_COMMAND_CAP 32767u

static int update_handoff_append(wchar_t* output, size_t capacity,
                                 size_t* used, const wchar_t* text,
                                 size_t length) {
    if (!output || !used || !text ||
        *used + length + 1u > capacity) {
        return 0;
    }
    memcpy(output + *used, text, length * sizeof(*output));
    *used += length;
    output[*used] = L'\0';
    return 1;
}

static int update_handoff_quote(wchar_t* output, size_t capacity,
                                size_t* used, const wchar_t* argument) {
    const wchar_t* cursor = argument ? argument : L"";
    size_t slashes = 0u;
    if (!update_handoff_append(
            output, capacity, used, L"\"", 1u)) return 0;
    for (;;) {
        if (*cursor == L'\\') {
            slashes++;
            cursor++;
            continue;
        }
        if (*cursor == L'"') {
            while (slashes > 0u) {
                if (!update_handoff_append(
                        output, capacity, used, L"\\\\", 2u)) return 0;
                slashes--;
            }
            if (!update_handoff_append(
                    output, capacity, used, L"\\\"", 2u)) return 0;
            cursor++;
            continue;
        }
        if (*cursor == L'\0') {
            while (slashes > 0u) {
                if (!update_handoff_append(
                        output, capacity, used, L"\\\\", 2u)) return 0;
                slashes--;
            }
            return update_handoff_append(
                output, capacity, used, L"\"", 1u);
        }
        while (slashes > 0u) {
            if (!update_handoff_append(
                    output, capacity, used, L"\\", 1u)) return 0;
            slashes--;
        }
        if (!update_handoff_append(
                output, capacity, used, cursor, 1u)) return 0;
        cursor++;
    }
}

static int update_handoff_ephemeral_arg(const wchar_t* argument) {
    static const wchar_t* const prefixes[] = {
        L"yule:", L"--yule-uri", L"-online", L"--online",
        L"-requests", L"--requests", L"-queue", L"--queue",
        L"-challenge", L"--challenge"
    };
    size_t i;
    if (!argument) return 0;
    for (i = 0u; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
        size_t length = wcslen(prefixes[i]);
        if (_wcsnicmp(argument, prefixes[i], length) == 0) return 1;
    }
    return 0;
}

static int update_handoff_safe_state(void) {
    void* state = p_state_current ? p_state_current() : NULL;
    return state == (void*)(uintptr_t)ADDR_MAIN_STATE ||
           state == (void*)(uintptr_t)ADDR_MAIN_STATE_INITIAL ||
           state == (void*)(uintptr_t)ADDR_OPTIONS_STATE ||
           state == (void*)&g_mods_state ||
           state == (void*)&g_mods_entry_state;
}

static int update_handoff_launch(void) {
    HMODULE module = NULL;
    wchar_t module_path[MAX_PATH];
    wchar_t updater_path[MAX_PATH];
    wchar_t root[MAX_PATH];
    wchar_t wait_argument[64];
    wchar_t* command = NULL;
    LPWSTR* arguments = NULL;
    int argument_count = 0;
    size_t used = 0u;
    wchar_t* slash;
    DWORD attributes;
    int i;
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    int ok = 0;

    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCWSTR)(const void*)&g_update_handoff_started, &module) ||
        GetModuleFileNameW(module, module_path, MAX_PATH) == 0u) {
        return 0;
    }
    module_path[MAX_PATH - 1] = L'\0';
    slash = wcsrchr(module_path, L'\\');
    if (!slash) return 0;
    *slash = L'\0';
    if (wcslen(module_path) + wcslen(L"\\YuleUpdater.exe") + 1u >
        MAX_PATH) {
        return 0;
    }
    wcscpy(root, module_path);
    wcscpy(updater_path, module_path);
    wcscat(updater_path, L"\\YuleUpdater.exe");
    attributes = GetFileAttributesW(updater_path);
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & (FILE_ATTRIBUTE_DIRECTORY |
                       FILE_ATTRIBUTE_REPARSE_POINT))) {
        LOG_ERROR("update: YuleUpdater.exe is missing or unsafe");
        return 0;
    }

    command = (wchar_t*)calloc(
        UPDATE_HANDOFF_COMMAND_CAP, sizeof(*command));
    arguments = CommandLineToArgvW(
        GetCommandLineW(), &argument_count);
    if (!command || !arguments || argument_count <= 0 ||
        argument_count > 128) {
        goto done;
    }
    _snwprintf(wait_argument,
               sizeof(wait_argument) / sizeof(wait_argument[0]),
               L"--wait-pid=%lu",
               (unsigned long)GetCurrentProcessId());
    wait_argument[
        sizeof(wait_argument) / sizeof(wait_argument[0]) - 1u] = L'\0';
    if (!update_handoff_quote(
            command, UPDATE_HANDOFF_COMMAND_CAP, &used, updater_path) ||
        !update_handoff_append(
            command, UPDATE_HANDOFF_COMMAND_CAP, &used, L" ", 1u) ||
        !update_handoff_quote(
            command, UPDATE_HANDOFF_COMMAND_CAP, &used, wait_argument)) {
        goto done;
    }
    for (i = 1; i < argument_count; i++) {
        if (update_handoff_ephemeral_arg(arguments[i])) continue;
        if (!update_handoff_append(
                command, UPDATE_HANDOFF_COMMAND_CAP, &used, L" ", 1u) ||
            !update_handoff_quote(
                command, UPDATE_HANDOFF_COMMAND_CAP, &used, arguments[i])) {
            goto done;
        }
    }

    memset(&startup, 0, sizeof(startup));
    memset(&process, 0, sizeof(process));
    startup.cb = sizeof(startup);
    if (!CreateProcessW(updater_path, command, NULL, NULL, FALSE, 0u,
                        NULL, root, &startup, &process)) {
        LOG_ERROR("update: could not start YuleUpdater.exe (winerr=%lu)",
                  (unsigned long)GetLastError());
        goto done;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    LOG_INFO("update: updater handoff started; waiting for orderly close");
    ok = 1;

done:
    if (arguments) LocalFree(arguments);
    free(command);
    return ok;
}

static void update_toast_metrics(float* out_x, float* out_y,
                                 float* out_w, float* out_h) {
    float sw = p_mad_w ? p_mad_w() : BASE_UI_W;
    float sh = p_mad_h ? p_mad_h() : BASE_UI_H;
    float ui = clampf(calc_ui_scale(), 0.72f, 1.45f);
    float margin = clampf(14.0f * ui, 8.0f, 22.0f);
    float avail_w = sw - margin * 2.0f;
    float avail_h = sh - margin * 2.0f;
    float w;
    float h;
    float x;
    float y;

    if (avail_w < 1.0f) avail_w = 1.0f;
    if (avail_h < 1.0f) avail_h = 1.0f;
    w = clampf(410.0f * ui, 286.0f, 510.0f);
    h = clampf(112.0f * ui, 88.0f, 148.0f);
    if (w > avail_w) w = avail_w;
    if (h > avail_h) h = avail_h;
    x = sw - margin - w;
    y = margin;
    if (x < 0.0f) x = 0.0f;
    if (y < 0.0f) y = 0.0f;

    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
}

static void update_toast_render(UpdateStatus status, uint32_t elapsed_ms) {
    float x;
    float y;
    float w;
    float h;
    float ui = clampf(calc_ui_scale(), 0.72f, 1.45f);
    float alpha = 1.0f;
    float title_scale = clampf(0.94f * ui, 0.76f, 1.20f);
    float body_scale = clampf(0.78f * ui, 0.68f, 1.02f);
    char latest[64];
    char status_line[192];
    char body[192];
    const char* title = "FRAMEWORK UPDATE";

    safe_copy(latest, sizeof(latest), update_ext_latest_version());
    safe_copy(status_line, sizeof(status_line), update_ext_status_line());
    if (elapsed_ms < 240u) alpha = clampf((float)elapsed_ms / 240.0f, 0.18f, 1.0f);
    if (elapsed_ms > UPDATE_TOAST_VISIBLE_MS - 700u) {
        float fade = (float)(UPDATE_TOAST_VISIBLE_MS - elapsed_ms) / 700.0f;
        alpha = clampf(fade, 0.0f, alpha);
    }

    switch (status) {
        case UPDATE_AVAILABLE:
            title = "UPDATE AVAILABLE";
            snprintf(body, sizeof(body), "Version %s is ready. Open Mods to install.",
                     latest[0] ? latest : "new");
            break;
        case UPDATE_APPLYING:
            title = "INSTALLING UPDATE";
            safe_copy(body, sizeof(body), status_line[0] ? status_line : "Downloading and verifying files...");
            break;
        case UPDATE_RESTART_PENDING:
            title = InterlockedCompareExchange(
                        &g_update_handoff_started, 0, 0)
                ? "RESTARTING TO UPDATE" : "UPDATE READY";
            if (InterlockedCompareExchange(
                    &g_update_handoff_started, 0, 0)) {
                safe_copy(body, sizeof(body),
                          "Closing Eggnogg, replacing verified files, and "
                          "relaunching...");
            } else if (!update_handoff_safe_state()) {
                snprintf(body, sizeof(body),
                         "Version %s is ready. Return to a menu to update "
                         "safely.",
                         latest[0] ? latest : "new");
            } else {
                snprintf(body, sizeof(body),
                         "Version %s is ready. Starting the updater...",
                         latest[0] ? latest : "new");
            }
            break;
        case UPDATE_ERROR:
            title = "UPDATE NEEDS ATTENTION";
            safe_copy(body, sizeof(body), "Open Mods > Framework to retry. Details are in the log.");
            break;
        default:
            safe_copy(body, sizeof(body), status_line[0] ? status_line : "Open Mods > Framework for details.");
            break;
    }

    update_toast_metrics(&x, &y, &w, &h);
    {
        float text_w = w - 40.0f * ui;
        size_t max_chars = text_w > 0.0f
            ? (size_t)(text_w / (9.0f * body_scale))
            : 1u;
        safe_copy_ellipsized(body, sizeof(body), body, max_chars);
    }
    hooks_ui_fill_rect(x + 4.0f * ui, y + 5.0f * ui, w, h,
                       0.0f, 0.0f, 0.0f, 0.36f * alpha);
    hooks_ui_fill_rect(x, y, w, h, 0.035f, 0.050f, 0.072f, 0.96f * alpha);
    hooks_ui_fill_rect(x, y, 5.0f * ui, h,
                       status == UPDATE_ERROR ? 0.94f : 0.30f,
                       status == UPDATE_ERROR ? 0.34f : 0.84f,
                       status == UPDATE_ERROR ? 0.32f : 0.96f,
                       0.96f * alpha);
    hooks_ui_stroke_rect(x, y, w, h, 1.25f,
                         0.34f, 0.60f, 0.78f, 0.72f * alpha);
    mods_restore_render_state();
    draw_text_scaled_mode_alpha(x + 20.0f * ui, y + 19.0f * ui,
                                title_scale, 0.92f, 0.97f, 1.0f, alpha,
                                title, 0);
    draw_text_scaled_mode_alpha(x + 20.0f * ui, y + 51.0f * ui,
                                body_scale, 0.70f, 0.80f, 0.90f, alpha,
                                body, 0);
    draw_text_scaled_mode_alpha(x + w - 14.0f * ui, y + h - 18.0f * ui,
                                body_scale * 0.82f, 0.48f, 0.58f, 0.68f,
                                alpha, "click to dismiss", 2);
    mods_restore_render_state();
    if (p_main_sprite_batches_draw) p_main_sprite_batches_draw();
    mods_restore_render_state();
}

void hooks_update_on_pre_swap(void) {
    uint32_t now_ms;
    uint32_t elapsed_ms;
    UpdateStatus status;

    if (InterlockedCompareExchange(&g_update_runtime_booted, 1, 0) == 0) {
        update_ext_boot();
    }
    now_ms = (uint32_t)GetTickCount();
    status = update_ext_status();
    if (status == UPDATE_RESTART_PENDING &&
        update_handoff_safe_state() &&
        InterlockedCompareExchange(
            &g_update_handoff_started, 0, 0) == 0 &&
        (g_update_handoff_retry_ms == 0u ||
         (int32_t)(now_ms - g_update_handoff_retry_ms) >= 0)) {
        if (update_handoff_launch()) {
            InterlockedExchange(&g_update_handoff_started, 1);
        } else {
            g_update_handoff_retry_ms = now_ms + 5000u;
        }
    }
    if (!update_ext_notice_active()) {
        g_update_toast_started_ms = 0;
        g_update_toast_status = -1;
        return;
    }

    if (g_update_toast_status != (int)status) {
        g_update_toast_status = (int)status;
        g_update_toast_started_ms = now_ms;
    }
    elapsed_ms = now_ms - g_update_toast_started_ms;
    if (elapsed_ms >= UPDATE_TOAST_VISIBLE_MS) {
        update_ext_dismiss_notice();
        g_update_toast_started_ms = 0;
        g_update_toast_status = -1;
        return;
    }
    update_toast_render(status, elapsed_ms);
}

int hooks_update_mousebutton(int x, int y, int button, int down) {
    float tx;
    float ty;
    float tw;
    float th;
    if (!down || button != 1 || !update_ext_notice_active()) return 0;
    update_toast_metrics(&tx, &ty, &tw, &th);
    if ((float)x < tx || (float)x > tx + tw ||
        (float)y < ty || (float)y > ty + th) {
        return 0;
    }
    update_ext_dismiss_notice();
    g_update_toast_started_ms = 0;
    g_update_toast_status = -1;
    return 1;
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
        console_insert_text(text);
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

    /* During an online match, swallow ALL function keys (F1-F12, syms
     * 0x4000003A..0x40000045) so the vanilla debug hotkeys (and our own dev
     * hotkeys below) cannot fire mid-match and desync or cheat. In normal
     * (offline) play they fall through to the game so its debug keys work as
     * usual - previously F5/F9/F10 were swallowed in every mode, which is what
     * disabled them offline. */
    if (sym >= 0x4000003A && sym <= 0x40000045 && ggpo_net_active()) {
        return 1;
    }

    /* F6/F7 are the framework's reserved local host/join test hotkeys. V16
     * starts fail closed unless `ggpo.net key` armed a one-shot clipboard key;
     * always consume them so vanilla never sees a second action. */
    if (sym == SDLK_F6 || sym == SDLK_F7) {
        if (!is_console_state_active()) {
            void* cur = p_state_current ? p_state_current() : NULL;
            if (cur == (void*)(uintptr_t)ADDR_GAME_STATE) {
                if (sym == SDLK_F6) {
                    start_ggpo_net_host(GGPO_NET_DEFAULT_PORT, "F6", 0);
                } else {
                    start_ggpo_net_join("127.0.0.1", GGPO_NET_DEFAULT_PORT, 0, "F7");
                }
            }
        }
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
            console_autocomplete();
            return 1;
        case SDLK_BACKSPACE:
            console_backspace();
            return 1;
        case SDLK_DELETE:
            console_delete();
            return 1;
        case SDLK_LEFT:
            if (g_console_cursor > 0) g_console_cursor--;
            return 1;
        case SDLK_RIGHT:
            if ((size_t)g_console_cursor < strlen(g_console_input)) g_console_cursor++;
            return 1;
        case SDLK_HOME:
            g_console_cursor = 0;
            return 1;
        case SDLK_END:
            g_console_cursor = (int)strlen(g_console_input);
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

void hooks_arm_ai_match(int ai_player, int training) {
    if (ggpo_net_active()) return;               /* never during online play */
    if (g_online_pending_match.active) return;   /* nor while a match is launching */
    g_ai_match_player = ai_player & 1;
    g_ai_match_training = training ? 1 : 0;
    g_ai_match_active = 1;
    LOG_INFO("ai_match: armed (ai_player=%d training=%d)", g_ai_match_player, g_ai_match_training);
}

void hooks_clear_ai_match(void) {
    if (g_ai_match_active) LOG_INFO("ai_match: cleared");
    g_ai_match_active = 0;
    g_ai_match_training = 0;
}

void hooks_get_ai_match(int* out_active, int* out_ai_player, int* out_training) {
    if (out_active) *out_active = g_ai_match_active;
    if (out_ai_player) *out_ai_player = g_ai_match_player;
    if (out_training) *out_training = g_ai_match_training;
}

static void __cdecl hooked_player_die(int player_ptr) {
    fn_player_die_t real = p_player_die_trampoline;
    volatile int* sc0 = (volatile int*)(uintptr_t)ADDR_SCORE_PLAYER0;
    volatile int* sc1 = (volatile int*)(uintptr_t)ADDR_SCORE_PLAYER1;
    int idx = -1;
    int ec_before = 0, ec_after = 0;
    int s0_before = 0, s1_before = 0, s0_after = 0, s1_after = 0;
    int scored = 0;
    if (p_player_slots) {
        if ((uintptr_t)player_ptr == (uintptr_t)p_player_slots[0]) idx = 0;
        else if ((uintptr_t)player_ptr == (uintptr_t)p_player_slots[1]) idx = 1;
    }
    if (g_game_end_countdown && !IsBadReadPtr((const void*)g_game_end_countdown, sizeof(int))) {
        ec_before = *g_game_end_countdown;
    }
    if (!IsBadReadPtr((const void*)sc0, sizeof(int))) s0_before = *sc0;
    if (!IsBadReadPtr((const void*)sc1, sizeof(int))) s1_before = *sc1;

    if (real) real(player_ptr);

    if (g_game_end_countdown && !IsBadReadPtr((const void*)g_game_end_countdown, sizeof(int))) {
        ec_after = *g_game_end_countdown;
    }
    if (!IsBadReadPtr((const void*)sc0, sizeof(int))) s0_after = *sc0;
    if (!IsBadReadPtr((const void*)sc1, sizeof(int))) s1_after = *sc1;

    if (s0_after > s0_before) { g_ledger_scores[0]++; scored = 1; }
    if (s1_after > s1_before) { g_ledger_scores[1]++; scored = 1; }
    if (ec_before == 0 && ec_after > 0) {
        g_ledger_match_ends++;
        if (s0_after > s0_before) g_ledger_last_winner = 0;
        else if (s1_after > s1_before) g_ledger_last_winner = 1;
        else g_ledger_last_winner = idx;
    } else if (!scored && idx >= 0) {
        g_ledger_deaths[idx]++;
    }
}

void hooks_get_combat_ledger(uint32_t* out_d0, uint32_t* out_d1,
                             uint32_t* out_s0, uint32_t* out_s1,
                             uint32_t* out_match_ends, int* out_last_winner) {
    if (out_d0) *out_d0 = g_ledger_deaths[0];
    if (out_d1) *out_d1 = g_ledger_deaths[1];
    if (out_s0) *out_s0 = g_ledger_scores[0];
    if (out_s1) *out_s1 = g_ledger_scores[1];
    if (out_match_ends) *out_match_ends = g_ledger_match_ends;
    if (out_last_winner) *out_last_winner = g_ledger_last_winner;
}

/* The supported executable's thing_new at 0x41FD40 is the single allocator
 * reached by all four direct native call sites (player_new, sword_new,
 * spawn_thing_action, and mine_action).  It clears and reuses a fixed pool slot
 * but stores no generation. Advance our rollback-owned generation only after a
 * successful allocation so same-frame free/reuse cannot inherit map.lua
 * contact history. */
static void* __cdecl hooked_thing_new(int type) {
    fn_thing_new_t real = p_thing_new_trampoline;
    void* result;
    uintptr_t address;
    uintptr_t base;
    size_t offset;
    uint32_t slot;
    if (!real) return NULL;
    result = real(type);
    if (!result || !p_things) return result;
    address = (uintptr_t)result;
    base = (uintptr_t)p_things;
    if (address < base || address >= (uintptr_t)ADDR_THING_INFO) return result;
    offset = (size_t)(address - base);
    if ((offset % THING_SIZE) != 0u) return result;
    slot = (uint32_t)(offset / THING_SIZE);
    if (slot < THING_SLOT_COUNT) {
        (void)map_script_object_lifecycle_advance(slot);
    }
    return result;
}

/* Apply V2 tile behavior as part of the deterministic native simulation tick.
 * The player movement path restores its center outside a solid floor, so the
 * legacy declarative/contact path needs both its exact center and a half-tile
 * foot boundary. The verified sword movement path (0x42B830) instead calls
 * check_map_collide with its exact x/y center. map.sensor is separate: it uses
 * the native radius at thing+0x6c and a bounded author-defined AABB, while the
 * two legacy points remain the force-field compatibility path. */
static void hooks_map_script_apply_object(void* userdata,
                                          const MapScriptObjectView* object);

/* The bridge exposes only native records whose layout and update owner are
 * verified. Type 3 is not sufficient by itself: effects may reuse that byte,
 * while K's point-mass hazard is identified by its exact updater. */
static int hooks_map_script_kind_for_body(uintptr_t body) {
    const uint8_t* thing = (const uint8_t*)body;
    uint32_t update_fn;
    if (!thing || IsBadReadPtr((const void*)thing, THING_SIZE) ||
        thing[THING_OFS_ACTIVE] == 0) {
        return MAP_SCRIPT_OBJECT_UNKNOWN;
    }
    if (thing[THING_OFS_TYPE] == THING_TYPE_PLAYER) {
        return thing[PLAYER_OFS_STATE_ID] == PLAYER_STATE_DEAD_BODY
                   ? MAP_SCRIPT_OBJECT_DEAD_BODY
                   : MAP_SCRIPT_OBJECT_PLAYER;
    }
    if (thing[THING_OFS_TYPE] == THING_TYPE_SWORD) {
        return MAP_SCRIPT_OBJECT_SWORD;
    }
    if (thing[THING_OFS_TYPE] != THING_TYPE_HAZARD) {
        return MAP_SCRIPT_OBJECT_UNKNOWN;
    }
    update_fn = *(const uint32_t*)(thing + THING_OFS_UPDATE_FN);
    return update_fn == (uint32_t)ADDR_HAZARD_ANIM
               ? MAP_SCRIPT_OBJECT_HAZARD
               : MAP_SCRIPT_OBJECT_UNKNOWN;
}

static int hooks_map_script_read_contact_radius(uintptr_t body,
                                                int object_kind,
                                                float* out_radius) {
    float expected;
    float actual;
    if (!body || !out_radius ||
        IsBadReadPtr((const void*)(body + THING_OFS_CONTACT_RADIUS),
                     sizeof(float))) {
        return 0;
    }
    if (object_kind == MAP_SCRIPT_OBJECT_PLAYER ||
        object_kind == MAP_SCRIPT_OBJECT_DEAD_BODY) {
        expected = object_kind == MAP_SCRIPT_OBJECT_DEAD_BODY
                       ? MAP_SCRIPT_DEAD_BODY_CONTACT_RADIUS
                       : MAP_SCRIPT_PLAYER_CONTACT_RADIUS;
    } else if (object_kind == MAP_SCRIPT_OBJECT_SWORD) {
        expected = MAP_SCRIPT_SWORD_CONTACT_RADIUS;
    } else if (object_kind == MAP_SCRIPT_OBJECT_HAZARD) {
        expected = MAP_SCRIPT_HAZARD_CONTACT_RADIUS;
    } else {
        return 0;
    }
    actual = *(const float*)(body + THING_OFS_CONTACT_RADIUS);
    if (!isfinite(actual) || actual != expected) return 0;
    *out_radius = actual;
    return 1;
}

static void hooks_apply_content_interactions_to_body(uint32_t object_id,
                                                     uint32_t lifecycle_id,
                                                     uintptr_t body,
                                                     int tile_w,
                                                     int tile_h,
                                                     int tile_contacts_enabled,
                                                     int object_kind,
                                                     int include_player_foot_probe) {
    float* vx;
    float* vy;
    float x;
    float y;
    ContentTileInteraction contacts[2];
    MapScriptObjectView script_object;
    float contact_radius = 0.0f;
    float sensor_x;
    float sensor_y;
    int sensor_profile_valid;
    int contact_count = 0;
    int sample;
    if (!body || IsBadReadPtr((const void*)body, THING_SIZE) ||
        IsBadWritePtr((void*)(body + THING_OFS_VX), sizeof(float) * 2u)) return;
    x = *(float*)(body + THING_OFS_X);
    y = *(float*)(body + THING_OFS_Y);
    vx = (float*)(body + THING_OFS_VX);
    vy = (float*)(body + THING_OFS_VY);

    /* Declarative force fields and unsensored legacy contacts predate K
     * support. A verified hazard participates only in explicitly authored
     * map.sensor geometry; merely crossing a force tile must not opt it in. */
    for (sample = 0;
         tile_contacts_enabled && object_kind != MAP_SCRIPT_OBJECT_HAZARD &&
         sample < (include_player_foot_probe ? 2 : 1);
         sample++) {
        ContentTileInteraction interaction;
        int duplicate = 0;
        float sample_y = y + (sample == 1 ? (float)tile_h * 0.5f : 0.0f);
        int i;
        if (!content_tiles_interaction_at_world(x, sample_y, tile_w, tile_h,
                                                &interaction)) {
            continue;
        }
        for (i = 0; i < contact_count; i++) {
            if (contacts[i].cell_index == interaction.cell_index) {
                duplicate = 1;
                break;
            }
        }
        if (!duplicate && contact_count < 2) contacts[contact_count++] = interaction;
    }
    for (sample = 0; sample < contact_count; sample++)
        content_tiles_apply_interaction_velocity(&contacts[sample], vx, vy);

    if (!map_script_is_active() || map_script_is_faulted()) return;
    memset(&script_object, 0, sizeof(script_object));
    script_object.object_id = object_id;
    script_object.lifecycle_id = lifecycle_id;
    script_object.object_kind = (uint8_t)object_kind;
    script_object.x = x;
    script_object.y = y;
    script_object.vx = *vx;
    script_object.vy = *vy;
    if (!map_script_update_object(&script_object, NULL, 0)) return;

    /* Fix the geometry sample before any callback can move the staged object.
     * Every candidate in this tick sees identical physics coordinates, while
     * successful callbacks still compose their x/y/vx/vy changes in stable
     * map order. */
    sensor_x = script_object.x;
    sensor_y = script_object.y;
    sensor_profile_valid = hooks_map_script_read_contact_radius(
        body, object_kind, &contact_radius);

    if (tile_contacts_enabled && isfinite(sensor_x) && isfinite(sensor_y) &&
        sensor_x >= 0.0f && sensor_y >= 0.0f) {
        double grid_x = (double)sensor_x / (double)tile_w;
        double grid_y = (double)sensor_y / (double)tile_h;
        if (grid_x <= (double)(INT_MAX - MAP_SCRIPT_SENSOR_CELL_RADIUS) &&
            grid_y <= (double)(INT_MAX - MAP_SCRIPT_SENSOR_CELL_RADIUS)) {
            int center_x = (int)grid_x;
            int center_y = (int)grid_y;
            int first_x = center_x > MAP_SCRIPT_SENSOR_CELL_RADIUS
                              ? center_x - MAP_SCRIPT_SENSOR_CELL_RADIUS : 0;
            int first_y = center_y > MAP_SCRIPT_SENSOR_CELL_RADIUS
                              ? center_y - MAP_SCRIPT_SENSOR_CELL_RADIUS : 0;
            int last_x = center_x + MAP_SCRIPT_SENSOR_CELL_RADIUS;
            int last_y = center_y + MAP_SCRIPT_SENSOR_CELL_RADIUS;
            int cell_y;
            for (cell_y = first_y;
                 cell_y <= last_y && !map_script_is_faulted();
                 cell_y++) {
                int cell_x;
                for (cell_x = first_x;
                     cell_x <= last_x && !map_script_is_faulted();
                     cell_x++) {
                    ContentTileInteraction interaction;
                    int legacy_index = -1;
                    int i;
                    if (!content_tiles_interaction_at_cell(cell_x, cell_y,
                                                           &interaction)) {
                        continue;
                    }
                    for (i = 0; i < contact_count; i++) {
                        if (contacts[i].cell_index == interaction.cell_index) {
                            legacy_index = i;
                            break;
                        }
                    }
                    if (map_script_binding_has_sensor(interaction.key)) {
                        MapScriptCellCandidateView candidate;
                        if (!sensor_profile_valid) continue;
                        memset(&candidate, 0, sizeof(candidate));
                        candidate.object = &script_object;
                        candidate.object_kind = object_kind;
                        candidate.contact_radius = contact_radius;
                        candidate.sensor_x = sensor_x;
                        candidate.sensor_y = sensor_y;
                        candidate.cell_index = interaction.cell_index;
                        candidate.tile_x = interaction.x;
                        candidate.tile_y = interaction.y;
                        candidate.tile_width = tile_w;
                        candidate.tile_height = tile_h;
                        candidate.room_mirrored = interaction.room_mirrored;
                        candidate.qualified_key = interaction.key;
                        (void)map_script_dispatch_cell_candidate(&candidate,
                                                                 NULL, 0);
                    } else if (legacy_index >= 0) {
                        MapScriptContactView script_contact;
                        memset(&script_contact, 0, sizeof(script_contact));
                        script_contact.object = &script_object;
                        script_contact.cell_index =
                            contacts[legacy_index].cell_index;
                        script_contact.tile_x = contacts[legacy_index].x;
                        script_contact.tile_y = contacts[legacy_index].y;
                        script_contact.room_mirrored =
                            contacts[legacy_index].room_mirrored;
                        script_contact.qualified_key = contacts[legacy_index].key;
                        (void)map_script_dispatch_contact(&script_contact, NULL, 0);
                    }
                }
            }
        }
    }
    hooks_map_script_apply_object(NULL, &script_object);
}

/* Map-script object ids identify host slots: 0/1 are the two native players
 * and 2..17 are the fixed thing pool. Every write reclassifies the current
 * record, including player/dead state and K's exact updater. Pooled writes also
 * require the snapshotted lifecycle generation, so a delayed synthesized leave
 * can never mutate a replacement object in the same slot. */
static uintptr_t hooks_map_script_body_for_object(const MapScriptObjectView* object) {
    uint32_t object_id;
    int current_kind;
    if (!object) return 0;
    object_id = object->object_id;
    if (object_id < 2u) {
        uintptr_t player;
        if (object->lifecycle_id != 0u) return 0;
        if (!p_player_slots ||
            IsBadReadPtr((const void*)(p_player_slots + object_id), sizeof(uintptr_t))) {
            return 0;
        }
        player = p_player_slots[object_id];
        current_kind = hooks_map_script_kind_for_body(player);
        if ((object->object_kind != MAP_SCRIPT_OBJECT_PLAYER &&
             object->object_kind != MAP_SCRIPT_OBJECT_DEAD_BODY) ||
            current_kind != object->object_kind) {
            return 0;
        }
        return player;
    }
    object_id -= 2u;
    if (object_id >= THING_SLOT_COUNT || !p_things) return 0;
    {
        uint8_t* thing = p_things + ((size_t)object_id * THING_SIZE);
        if (object->lifecycle_id != map_script_object_lifecycle_current(object_id)) {
            return 0;
        }
        current_kind = hooks_map_script_kind_for_body((uintptr_t)thing);
        if ((object->object_kind != MAP_SCRIPT_OBJECT_SWORD &&
             object->object_kind != MAP_SCRIPT_OBJECT_HAZARD) ||
            current_kind != object->object_kind) {
            return 0;
        }
        return (uintptr_t)thing;
    }
}

static void hooks_map_script_apply_object(void* userdata,
                                          const MapScriptObjectView* object) {
    uintptr_t body;
    float current_x;
    float current_y;
    float translated_prev_x = 0.0f;
    float translated_prev_y = 0.0f;
    int position_changed;
    (void)userdata;
    if (!object || !isfinite(object->x) || !isfinite(object->y) ||
        !isfinite(object->vx) || !isfinite(object->vy)) {
        return;
    }
    body = hooks_map_script_body_for_object(object);
    if (!body ||
        IsBadReadPtr((const void*)(body + THING_OFS_X), sizeof(float) * 6u) ||
        IsBadWritePtr((void*)(body + THING_OFS_X), sizeof(float) * 6u)) {
        return;
    }
    current_x = *(const float*)(body + THING_OFS_X);
    current_y = *(const float*)(body + THING_OFS_Y);
    if (!isfinite(current_x) || !isfinite(current_y)) return;
    position_changed = object->x != current_x || object->y != current_y;
    if (position_changed) {
        float previous_x = *(const float*)(body + THING_OFS_PREV_X);
        float previous_y = *(const float*)(body + THING_OFS_PREV_Y);
        float delta_x = object->x - current_x;
        float delta_y = object->y - current_y;
        if (!isfinite(previous_x) || !isfinite(previous_y) ||
            !isfinite(delta_x) || !isfinite(delta_y)) {
            return;
        }
        translated_prev_x = previous_x + delta_x;
        translated_prev_y = previous_y + delta_y;
        if (!isfinite(translated_prev_x) || !isfinite(translated_prev_y)) return;
    }

    /* Validate and calculate the entire mutation before the first store. A
     * scripted translation moves both current and previous position by the
     * same delta, preserving native displacement; velocity-only callbacks do
     * not touch either position pair. */
    if (position_changed) {
        *(float*)(body + THING_OFS_PREV_X) = translated_prev_x;
        *(float*)(body + THING_OFS_PREV_Y) = translated_prev_y;
        *(float*)(body + THING_OFS_X) = object->x;
        *(float*)(body + THING_OFS_Y) = object->y;
    }
    *(float*)(body + THING_OFS_VX) = object->vx;
    *(float*)(body + THING_OFS_VY) = object->vy;
}

static void hooks_map_script_log(void* userdata, const char* message) {
    (void)userdata;
    LOG_WARN("map.lua: %s", (message && message[0]) ? message : "runtime fault");
}

static void hooks_apply_content_tile_interactions(void) {
    int tile_w;
    int tile_h;
    int tile_contacts_enabled;
    int script_enabled;
    int player_index;
    unsigned int thing_slot;
    script_enabled = map_script_is_active() && !map_script_is_faulted();
    tile_contacts_enabled =
        InterlockedCompareExchange(&g_content_bridge_enabled, 0, 0) != 0 &&
        content_tiles_map_active() && g_tile_width && g_tile_height;
    tile_w = tile_contacts_enabled ? *g_tile_width : 0;
    tile_h = tile_contacts_enabled ? *g_tile_height : 0;
    if (tile_w <= 0 || tile_w > 512 || tile_h <= 0 || tile_h > 512) {
        tile_contacts_enabled = 0;
        tile_w = 0;
        tile_h = 0;
    }
    if (!tile_contacts_enabled && !script_enabled) return;

    for (player_index = 0; player_index < 2; player_index++) {
        uintptr_t player;
        int object_kind;
        if (!p_player_slots ||
            IsBadReadPtr((const void*)(p_player_slots + player_index), sizeof(uintptr_t))) {
            continue;
        }
        player = p_player_slots[player_index];
        object_kind = hooks_map_script_kind_for_body(player);
        if (object_kind != MAP_SCRIPT_OBJECT_PLAYER &&
            object_kind != MAP_SCRIPT_OBJECT_DEAD_BODY) {
            continue;
        }
        hooks_apply_content_interactions_to_body((uint32_t)player_index,
                                                 0u, player, tile_w, tile_h,
                                                 tile_contacts_enabled,
                                                 object_kind, 1);
    }

    /* The verified native thing pool occupies exactly the memory between
     * ADDR_THINGS and ADDR_THING_INFO (16 slots). Binary dispatch and movement
     * evidence verifies type 2 as a physics sword and type 3 with updater
     * 0x43C450 as K's point-mass hazard. Other native records remain excluded.
     * If the allocator detour is unavailable, skip reusable slots entirely so
     * they cannot inherit contact history. This bridge runs after the native
     * update, so callbacks see the final deterministic position for the tick. */
    if (InterlockedCompareExchange(&g_thing_lifecycle_tracking_enabled, 0, 0) != 0) {
        for (thing_slot = 0; thing_slot < THING_SLOT_COUNT; thing_slot++) {
            uint8_t* thing = p_things + ((size_t)thing_slot * THING_SIZE);
            int object_kind = hooks_map_script_kind_for_body((uintptr_t)thing);
            if (object_kind != MAP_SCRIPT_OBJECT_SWORD &&
                object_kind != MAP_SCRIPT_OBJECT_HAZARD) {
                continue;
            }
            hooks_apply_content_interactions_to_body(
                2u + thing_slot,
                map_script_object_lifecycle_current(thing_slot),
                (uintptr_t)thing, tile_w, tile_h,
                tile_contacts_enabled,
                object_kind, 0);
        }
    }
    if (map_script_is_active() && !map_script_is_faulted()) {
        (void)map_script_dispatch_tick(NULL, 0);
    }
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

typedef struct HooksNativeGameTick {
    fn_game_update_t update;
    int arg0;
    int prepare_deterministic_clock;
} HooksNativeGameTick;

static int hooks_native_game_tick_callback(void* user) {
    HooksNativeGameTick* tick = (HooksNativeGameTick*)user;
    if (!tick || !tick->update) return 0;
    if (tick->prepare_deterministic_clock) {
        hooks_prepare_deterministic_game_update();
    }
    tick->update(tick->arg0);
    hooks_apply_content_tile_interactions();
    return 1;
}

/* Every actual native GAME simulation call, live or replayed, crosses this one
 * boundary.  One scope covers one tick only; catch-up/replay loops therefore
 * cannot leak a peer's inherited x87/MXCSR mode across multiple frames. */
static int hooks_run_native_game_tick(fn_game_update_t update,
                                      int arg0,
                                      int prepare_deterministic_clock) {
    HooksNativeGameTick tick;
    if (!update) return 0;
    tick.update = update;
    tick.arg0 = arg0;
    tick.prepare_deterministic_clock = prepare_deterministic_clock ? 1 : 0;
    return fp_control_run_canonical_tick(hooks_native_game_tick_callback, &tick);
}

void hooks_sync_mad_ticks_to_game_clock(void) {
    uint32_t native_ticks = 0;
    if (g_mad_ticks && lua_manager_game_native_ticks(&native_ticks)) {
        *g_mad_ticks = native_ticks;
    }
}

#ifdef EGGNOGGPLUS_SERIALIZER_TESTING
void hooks_test_bind_mad_ticks(volatile uint32_t* ticks) {
    g_mad_ticks = ticks;
}
#endif

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
        online_state_is_ingame_menu(state_ptr) &&
        ggpo_net_active()) {
        return 1;
    }
    return 0;
}

static int hooks_should_clear_native_pause_for_tick(void* state_ptr) {
    return g_allow_paused_game_tick &&
           online_state_is_ingame_menu(state_ptr) &&
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
        if (!hooks_run_native_game_tick(real_update, arg0, 1)) {
            if (restore_paused) {
                *g_native_paused = old_paused;
            }
            return (ran > 0) ? ran : -1;
        }
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

    if (!hooks_run_native_game_tick(real_update, arg0, 1)) {
        if (run_framework_tick) lua_manager_on_tick_post();
        return -1;
    }
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

    /* Readable backdrop panel behind the content. The sword cursors sit ~22px
     * OUTSIDE [L.left, L.right] with their blades pointing inward, so the panel is
     * INSET horizontally (and has no outward shadow) to stay clear of the swords -
     * they keep drawing bright over the bare scene at the panel's edges. Tunables: */
    {
        const float PANEL_A       = 0.86f;  /* main panel opacity */
        const float HEADER_BAND_A = 0.55f;  /* lighter band behind the title */
        const float PANEL_SIDE_INSET = 16.0f; /* * ui; keep clear of the swords */
        float px = L.left + (PANEL_SIDE_INSET * ui);
        float pw = (L.right - L.left) - (2.0f * PANEL_SIDE_INSET * ui);
        float pt = title_y - (16.0f * ui);
        float pb = L.list_bottom + (10.0f * ui);
        if (pw < 80.0f * ui) { px = L.left; pw = L.right - L.left; } /* safety */
        if (pt < 6.0f * ui) pt = 6.0f * ui;
        hooks_ui_fill_rect(px, pt, pw, pb - pt, 0.030f, 0.038f, 0.052f, PANEL_A);    /* panel   */
        hooks_ui_fill_rect(px, pt, pw, (help_y - title_y) + (30.0f*ui),
                           0.055f, 0.066f, 0.088f, HEADER_BAND_A);                   /* header  */
        hooks_ui_stroke_rect(px, pt, pw, pb - pt, 1.5f, 0.32f, 0.42f, 0.58f, 0.45f); /* border  */
    }
    mods_restore_render_state();

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
    int content = mods_content_row_count();   /* Back is a footer, not a scrolling row */
    int start_row = g_scroll_row;
    if (start_row < 0) start_row = 0;
    if (start_row > content) start_row = content;
    int end_row = start_row + cap;
    if (end_row > content) end_row = content;

    /* Selection highlight bar so the current row is obvious against the panel.
     * Aligned to the inset panel and lifted up to sit on the row text. Tunables: */
    if (g_selected_row >= start_row && g_selected_row < end_row) {
        const float HL_INSET = 16.0f;   /* * ui; match the panel side inset */
        const float HL_Y_OFF = -12.5f;   /* * ui; move the bar up onto the text */
        float sy = L.list_top + (float)(g_selected_row - start_row) * L.row_h;
        float hx = L.left + (HL_INSET * ui);
        float hw = (L.right - L.left) - (2.0f * HL_INSET * ui);
        float hy = sy + (HL_Y_OFF * ui);
        float hh = L.row_h - (10.0f * ui);
        if (hw < 80.0f * ui) { hx = L.left; hw = L.right - L.left; }
        hooks_ui_fill_rect(hx, hy, hw, hh, 0.11f, 0.14f, 0.21f, 0.88f);   /* row lift  */
        hooks_ui_fill_rect(hx, hy, 3.0f*ui, hh, 0.96f, 0.80f, 0.34f, 0.95f); /* gold edge */
        mods_restore_render_state();
    }

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
            case ROW_FW_TOGGLE:
            case ROW_FW_ACTION:
            case ROW_CONFIG:
            case ROW_BIND: {
                float lr = selected ? sel_r : base_r;
                float lg = selected ? sel_g : base_g;
                float lb = selected ? sel_b : base_b;
                float vr = dim_r, vg = dim_g, vb = dim_b;
                if (row->kind == ROW_MOD_TOGGLE || row->kind == ROW_FW_TOGGLE) {
                    if (row->right[0] == 'O' && row->right[1] == 'N') { vr = 0.48f; vg = 0.88f; vb = 0.58f; }
                    else { vr = 0.74f; vg = 0.76f; vb = 0.82f; }
                } else if (row->kind == ROW_FW_ACTION) {
                    vr = 0.48f; vg = 0.82f; vb = 0.96f;
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
                    if (row->right[0]) {
                        draw_text_scaled(option_x, y, g_ui_scale * 0.88f,
                                         dim_r, dim_g, dim_b, row->left);
                        draw_text_right_scaled(right_x, y, g_ui_scale * 0.86f,
                                               dim_r, dim_g, dim_b, row->right);
                    } else {
                        draw_text_centered_scaled(L.center_x, y, g_ui_scale * 0.90f,
                                                  dim_r, dim_g, dim_b, row->left);
                    }
                }
            } break;

            default:
                break;
        }
    }

    /* "Back" footer, drawn BELOW the panel with a drop shadow so it stays readable
       against the bare scene; its swords flank it there (see mods_cursor_tick). */
    {
        int back_i = mods_back_row_index();
        if (back_i >= 0) {
            int back_sel = (g_selected_row == back_i);
            float fy = L.list_bottom + (MODS_FOOTER_Y_OFF * ui);
            float rr = back_sel ? 0.96f : 0.80f;
            float gg = back_sel ? 0.86f : 0.84f;
            float bb = back_sel ? 0.42f : 0.90f;
            const char* label = g_rows[back_i].left;
            draw_text_centered_scaled(L.center_x + (2.0f * ui), fy + (2.0f * ui),
                                      g_ui_scale * 1.00f, 0.0f, 0.0f, 0.0f, label); /* shadow */
            draw_text_centered_scaled(L.center_x, fy, g_ui_scale * 1.00f, rr, gg, bb, label);
        }
    }

    if (g_scroll_row > 0) {
        draw_text_right_scaled(L.right - (16.0f * ui), L.list_top - (10.0f * ui), g_ui_scale * 0.92f, 0.44f, 0.50f, 0.58f, "^");
    }
    if (g_scroll_row + cap < content) {
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

    console_draw_rect(panel_x, panel_y, panel_w, panel_h, 0.03f, 0.05f, 0.08f, 0.82f);
    console_draw_rect(panel_x, panel_y, panel_w, 32.0f * ui, 0.07f, 0.11f, 0.18f, 0.92f);
    console_draw_rect(panel_x, panel_y + panel_h - (42.0f * ui), panel_w, 42.0f * ui, 0.02f, 0.04f, 0.06f, 0.92f);
    glColor4f(0.35f, 0.42f, 0.56f, 0.9f);
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
        if (g_console_cursor < 0) g_console_cursor = 0;
        if ((size_t)g_console_cursor > in_len) g_console_cursor = (int)in_len;
        avail_chars = (int)((panel_w - (36.0f * ui)) / (6.0f * text_scale));
        if (avail_chars < 12) avail_chars = 12;
        if (g_console_cursor > avail_chars - 4) {
            start_idx = g_console_cursor - (avail_chars - 4);
        }
        if (start_idx < 0) start_idx = 0;
        snprintf(input_line, sizeof(input_line), "> %.*s|%s",
                 g_console_cursor - start_idx, g_console_input + start_idx, g_console_input + g_console_cursor);
        if ((int)strlen(input_line) > avail_chars + 2) {
            input_line[avail_chars + 2] = '\0';
        }
        draw_text_scaled((float)((int)(panel_x + (14.0f * ui) + 0.5f)), (float)((int)(input_y + 0.5f)), text_scale,
                         0.95f, 0.88f, 0.40f, input_line);
        if (panel_w > 640.0f) {
            draw_text_right_scaled((float)((int)(panel_x + panel_w - (14.0f * ui) + 0.5f)), (float)((int)(input_y + 0.5f)), text_scale,
                                   0.66f, 0.74f, 0.86f, "Tab=complete  Ctrl+C/V=copy/paste  Wheel=scroll");
        }
    }
}

static void __cdecl console_enter(void) {
    capture_clear();
    g_console_scroll = 0;
    console_history_load();
    if (g_console_line_count == 0) {
        console_push_line_rgb("Type 'help' for a list of commands.", 0.72f, 0.90f, 1.00f);
    }
}

static void __cdecl console_update(void) {
    int net_tick_attempted = 0;
    int net_tick_result = 0;

    /* The console intentionally has no menu-button update path, so it does not
     * pass through hooked_main_update_with_buttons like the native/options and
     * MODS states do. Service the online control channel and exactly one GGPO
     * tick here while the console overlays a match. */
    if (!ggpo_net_active() &&
        !g_online_pending_match.active &&
        !g_online_active_match.active) {
        return;
    }
    lua_manager_on_tick();
    online_server_update();
    if (ggpo_net_active() && !g_online_pending_match.active) {
        net_tick_attempted = 1;
        g_allow_paused_game_tick++;
        net_tick_result = online_advance_net_gameplay_tick(0);
        g_allow_paused_game_tick--;
    }
    lua_manager_on_tick_post();
    if (!net_tick_attempted || net_tick_result <= 0) {
        hooks_finish_game_tick();
    }
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

    /* Opening the framework page is an explicit acknowledgement of the launch
     * notification.  The live status/action rows below remain available. */
    update_ext_dismiss_notice();
    g_update_toast_started_ms = 0;
    g_update_toast_status = -1;

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
    /* p_main_update_with_buttons enters our detour, whose common overlay path
     * services GGPO. Do not tick it again here: that used to advance MODS twice. */
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

static int online_native_winner_player(void) {
    uintptr_t leader;
    uintptr_t loser;
    /* `_leader` changes on ordinary deaths, so it is never sufficient by
     * itself. Native game_update arms `_end_countdown` only for its terminal
     * win paths (score target or map win condition); prefer the paired loser
     * identity there, then use the final score as a defensive fallback. */
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
    /* GAME_STATE plus any in-match menu overlay (pause/options/mods/console) - the
     * sim keeps ticking there, so these are NOT "left the match" and must not count
     * toward the abandon/forfeit timer. */
    return state_ptr == (void*)(uintptr_t)ADDR_GAME_STATE ||
           online_state_is_ingame_menu(state_ptr);
}

/* Abort a match that never established its P2P connection (hostile NAT / peer
 * unreachable). Release the match on the server, tear down the session, and
 * return to the hub with an explanatory message instead of hanging forever. */
static void online_abort_connect_timeout(void) {
    int attempts = g_online_connect.attempts;
    LOG_WARN("online.match: P2P did not connect after %d attempt(s); aborting to hub", attempts);
    {   /* Capture why in the log so failed connects are diagnosable after the fact. */
        char report[2048];
        char* p;
        online_build_net_diag(report, sizeof(report));
        for (p = report; p && *p; ) {
            char* nl = strchr(p, '\n');
            if (nl) *nl = '\0';
            if (*p) LOG_WARN("net.diag: %s", p);
            if (!nl) break;
            p = nl + 1;
        }
    }
    if (g_online_pending_match.active) {
        online_server_send_match_abort("P2P connection timed out before gameplay");
    } else if (g_online_active_match.active &&
               !g_online_active_match.result_reported &&
               g_online_active_match.server_committed) {
        g_online_active_match.result_reported = 1;
        online_server_send_match_end(ONLINE_MATCH_RESULT_LOSS);
    }
    if (ggpo_net_active()) stop_ggpo_net("connect timeout");
    online_clear_match_state();
    g_online_pending_connect_fail_status = 1;
    online_hub_open();
}

static void online_abort_prematch_setup(const char* reason) {
    snprintf(g_online_pending_connect_fail_reason,
             sizeof(g_online_pending_connect_fail_reason),
             "Online match setup failed: %.220s",
             (reason && reason[0]) ? reason : "unknown error");
    g_online_pending_connect_fail_reason[
        sizeof(g_online_pending_connect_fail_reason) - 1u] = '\0';
    LOG_ERROR("online.prematch: setup failed match=%d (%s)",
              g_online_pending_match.match_id,
              (reason && reason[0]) ? reason : "unknown error");
    {
        char report[2048];
        char* p;
        online_build_net_diag(report, sizeof(report));
        for (p = report; p && *p; ) {
            char* nl = strchr(p, '\n');
            if (nl) *nl = '\0';
            if (*p) LOG_WARN("net.diag: %s", p);
            if (!nl) break;
            p = nl + 1;
        }
    }
    if (g_online_pending_match.active) {
        online_server_send_match_abort((reason && reason[0])
            ? reason
            : "prematch setup failed");
    }
    if (ggpo_net_active()) stop_ggpo_net("prematch setup failed");
    online_clear_match_state();
    if (is_online_hub_state_active()) {
        /* Prematch normally runs on the hub itself, so there may be no enter
         * callback to consume the deferred status. Surface the same reason now
         * and leave no stale failure to reappear on a later hub visit. */
        online_hub_set_status(g_online_pending_connect_fail_reason);
        g_online_pending_connect_fail_status = 0;
        g_online_pending_connect_fail_reason[0] = '\0';
    } else {
        g_online_pending_connect_fail_status = 4;
        online_hub_open();
    }
}

static void online_monitor_active_match_state(void* state_ptr) {
    if (!g_online_active_match.active || g_online_active_match.result_reported) return;
    if (!ggpo_net_active()) {
        /* A failed socket start is still inside the bounded pre-connect retry
         * state machine. Once a connection was established, any teardown is a
         * real disconnect and keeps the existing loss behavior. */
        if (g_online_connect.attempts > 0 && !g_online_connect.established) return;
        online_finish_active_match(ONLINE_MATCH_RESULT_LOSS, "P2P disconnected; reported loss.", 1);
        return;
    }
    if (online_match_state_allowed(state_ptr)) {
        g_online_active_match.invalid_state_ticks = 0;
        return;
    }
    g_online_active_match.invalid_state_ticks++;
    if (g_online_active_match.invalid_state_ticks >= ONLINE_ABANDON_GRACE_TICKS) {
        online_forfeit_active_match("You left the match; forfeit reported.",
                                    "player left committed match");
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
    g_online_active_match.winner_player = winner;
    g_online_active_match.awaiting_native_return = 1;
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
    char err[256];
    if (!g_online_pending_match.active) return;
    if (!g_online_pending_match.server_committed &&
        g_online_pending_match.setup_started_ms != 0u &&
        (DWORD)(GetTickCount() - g_online_pending_match.setup_started_ms) >=
            ONLINE_PREMATCH_SETUP_TIMEOUT_MS) {
        online_abort_prematch_setup("prematch synchronization timed out");
        return;
    }
    if (!is_online_hub_state_active()) {
        online_hub_open();
        return;
    }

    if (g_online_pending_match.launch_countdown_frames > 0) {
        g_online_pending_match.launch_countdown_frames--;
    }

    /* The countdown keeps running while the bounded fresh-socket retry machine
     * connects. If it reaches zero first, remain on the match card instead of
     * entering GAME and displaying a frozen frame. */
    if (!ggpo_net_active() || !ggpo_net_link_ready()) return;

    /* A server-managed rollback match must never begin on different game or
     * framework binaries. The transport exchanges these fingerprints in its
     * authenticated HELLO; direct developer sessions retain their diagnostic
     * warning path, while matchmaking fails closed before native map setup. */
    if (ggpo_net_build_mismatch()) {
        online_abort_prematch_setup("opponent is using a different game or framework build");
        return;
    }

    if (!g_online_pending_match.prematch_prepared) {
        err[0] = '\0';
        if (!online_prepare_pending_match_state(err, sizeof(err))) {
            online_abort_prematch_setup(
                err[0] ? err : "could not initialize native match state");
            return;
        }
    }

    if (ggpo_net_state_layout_mismatch()) {
        online_abort_prematch_setup("opponent has an incompatible map/state layout");
        return;
    }
    /* Finalization is local and peers may finish native reset on different hub
     * frames. Keep servicing the countdown until both authenticated HELLOs prove
     * the same frozen schema/capacity; this is normal waiting, not a failure. */
    if (!ggpo_net_state_layout_ready()) {
        online_hub_set_status("Synchronizing map layout...");
        return;
    }

    if (!g_online_pending_match.prematch_released) {
        err[0] = '\0';
        if (!ggpo_net_prematch_hold() ||
            !ggpo_net_set_prematch_hold(0, err, sizeof(err))) {
            online_abort_prematch_setup(err[0] ? err : "could not publish final start state");
            return;
        }
        g_online_pending_match.prematch_released = 1;
        online_hub_set_status("Synchronizing match...");
        LOG_INFO("online.prematch: hold released during countdown match=%d epoch=%u",
                 g_online_pending_match.match_id,
                 (unsigned int)ggpo_net_state_epoch());
    }

    /* State/cosmetic/frame-0 exchange continues during the remaining countdown.
     * At zero, stay in the hub until the readiness proof is complete. */
    if (g_online_pending_match.launch_countdown_frames > 0 ||
        !ggpo_net_prematch_ready()) {
        return;
    }

    if (!p_state_switch) {
        online_abort_prematch_setup("state switch is unavailable");
        return;
    }
    if (!g_online_pending_match.prematch_start_prepared) {
        err[0] = '\0';
        if (!ggpo_net_prepare_prematch_start(err, sizeof(err))) {
            online_abort_prematch_setup(err[0] ? err : "could not restore synchronized start state");
            return;
        }
        if (!online_apply_synchronized_player_palettes(err, sizeof(err))) {
            online_abort_prematch_setup(
                err[0] ? err : "could not apply synchronized player palettes");
            return;
        }
        g_online_pending_match.prematch_start_prepared = 1;
    }

    /* READY is reported only after the synchronized state is locally restorable
     * and the native GAME switch exists. Stay behind the hub barrier until the
     * server has received READY from both clients and commits the match. */
    if (!g_online_pending_match.server_start_reported) {
        online_server_send_match_started();
        online_hub_set_status("Waiting for opponent to finish setup...");
        return;
    }
    if (!g_online_pending_match.server_committed) return;

    /* The authoritative server commit is the final barrier. GAME enter sees an
     * already-started synchronized match, and neither client can enter alone. */
    p_state_switch((void*)(uintptr_t)ADDR_GAME_STATE);
    online_active_match_begin_from_pending();
    g_online_pending_match.active = 0;
    online_hub_set_status("");
}

static int menu_mode_total(void) {
    return 2 + lua_manager_menu_mode_count();
}

static int menu_mode_available(int mode) {
    if (mode == MENU_MODE_PLAY || mode == MENU_MODE_ONLINE) return 1;
    return lua_manager_menu_mode_info(mode - MENU_MODE_CUSTOM0, NULL, NULL, NULL, NULL, NULL);
}

static const char* menu_mode_label(int mode) {
    const char* label = NULL;
    if (mode == MENU_MODE_ONLINE) return "ONLINE";
    if (mode >= MENU_MODE_CUSTOM0 &&
        lua_manager_menu_mode_info(mode - MENU_MODE_CUSTOM0, NULL, &label, NULL, NULL, NULL) &&
        label && label[0]) {
        return label;
    }
    return "PLAY";
}

static const char* menu_mode_id(int mode) {
    const char* id = NULL;
    if (mode == MENU_MODE_ONLINE) return "online";
    if (mode >= MENU_MODE_CUSTOM0 &&
        lua_manager_menu_mode_info(mode - MENU_MODE_CUSTOM0, &id, NULL, NULL, NULL, NULL) &&
        id && id[0]) {
        return id;
    }
    return "play";
}

/* resolve a persisted mode id to the current mode index; -1 if not (yet) present */
static int menu_mode_index_for_id(const char* id) {
    if (!id || !id[0]) return -1;
    if (_stricmp(id, "play") == 0 || strcmp(id, "0") == 0) return MENU_MODE_PLAY;
    if (_stricmp(id, "online") == 0 || strcmp(id, "1") == 0) return MENU_MODE_ONLINE;
    /* legacy numeric ids from the enum era */
    if (strcmp(id, "2") == 0) return menu_mode_index_for_id("vs_ai");
    if (strcmp(id, "3") == 0) return menu_mode_index_for_id("train_ai");
    for (int i = 0; i < lua_manager_menu_mode_count(); i++) {
        const char* mid = NULL;
        if (lua_manager_menu_mode_info(i, &mid, NULL, NULL, NULL, NULL) &&
            mid && _stricmp(mid, id) == 0) {
            return MENU_MODE_CUSTOM0 + i;
        }
    }
    return -1;
}

static void menu_mode_load(void) {
    FILE* f;
    char line[256];
    if (g_menu_mode_loaded) return;
    g_menu_mode_loaded = 1;
    f = fopen("mods/modframework.cfg", "r");
    if (!f) return;
    while (fgets(line, sizeof(line), f)) {
        char* p = line;
        char* e;
        while (*p == ' ' || *p == '\t') p++;
        if (_strnicmp(p, "main_menu_mode", 14) != 0) continue;
        p += 14;
        while (*p == ' ' || *p == '\t' || *p == '=' || *p == ':') p++;
        e = p + strlen(p);
        while (e > p && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ')) e--;
        *e = '\0';
        snprintf(g_menu_mode_pending_id, sizeof(g_menu_mode_pending_id), "%s", p);
        break;
    }
    fclose(f);
}

/* mods may register modes after the cfg was read; retry until the id appears */
static void menu_mode_resolve_pending(void) {
    if (!g_menu_mode_pending_id[0]) return;
    {
        int idx = menu_mode_index_for_id(g_menu_mode_pending_id);
        if (idx >= 0) {
            g_menu_mode = idx;
            g_menu_mode_pending_id[0] = '\0';
        }
    }
}

static void menu_mode_save(void) {
    const char* id = menu_mode_id(g_menu_mode);
    if (!update_ext_config_set("main_menu_mode", id)) {
        LOG_WARN("menu: could not persist main_menu_mode=%s", id);
    }
}

static void menu_mode_cycle(int dir) {
    int total = menu_mode_total();
    int step;
    for (step = 0; step < total; step++) {
        g_menu_mode = (g_menu_mode + dir + total) % total;
        if (menu_mode_available(g_menu_mode)) break;
    }
    menu_mode_save();
    LOG_INFO("menu: mode -> %s", menu_mode_label(g_menu_mode));
}

/* captured from the native START button the first time we take it over */
static void* g_main_start_orig_action = NULL;
static void* g_main_start_orig_link = NULL;

/* returns -1 (not activated), 0 (P1), 1 (P2). Mirrors online_hub_player_filter_proxy. */
static int menu_mode_tag_dance_activated(void* btn) {
    uint32_t* tag_ptr = (uint32_t*)((uint8_t*)btn + 4);
    uint32_t old_tag = *tag_ptr;
    int who = -1;
    *tag_ptr = 0x11;
    if (p_btn_player_filter(btn, 3)) who = 0;
    if (who < 0) {
        *tag_ptr = 0x12;
        if (p_btn_player_filter(btn, 3)) who = 1;
    }
    *tag_ptr = old_tag;
    return who;
}

/* non-activation events: same dual-selector pass-through the other proxies use */
static int menu_mode_forward_nav(void* btn, int event_code) {
    uint32_t* tag_ptr = (uint32_t*)((uint8_t*)btn + 4);
    uint32_t old_tag = *tag_ptr;
    *tag_ptr = 0x11;
    if (p_btn_player_filter(btn, event_code)) { *tag_ptr = old_tag; return 1; }
    *tag_ptr = 0x12;
    if (p_btn_player_filter(btn, event_code)) { *tag_ptr = old_tag; return 1; }
    *tag_ptr = old_tag;
    return 0;
}

static int __cdecl menu_mode_arrow_up_filter_proxy(void* btn, int event_code) {
    if (!btn || !p_btn_player_filter) return 0;
    if (event_code == 3) {
        if (menu_mode_tag_dance_activated(btn) >= 0) menu_mode_cycle(-1);
        return 0;  /* consume; no state switch */
    }
    return menu_mode_forward_nav(btn, event_code);
}

static int __cdecl menu_mode_arrow_down_filter_proxy(void* btn, int event_code) {
    if (!btn || !p_btn_player_filter) return 0;
    if (event_code == 3) {
        if (menu_mode_tag_dance_activated(btn) >= 0) menu_mode_cycle(1);
        return 0;
    }
    return menu_mode_forward_nav(btn, event_code);
}

static int __cdecl menu_mode_main_filter_proxy(void* btn, int event_code) {
    if (!btn || !p_btn_player_filter) return 0;
    if (event_code == 3) {
        int who = menu_mode_tag_dance_activated(btn);
        if (who < 0) return 0;
        if (g_menu_mode == MENU_MODE_ONLINE) {
            online_hub_open();
            return 0;
        }
        if (g_menu_mode >= MENU_MODE_CUSTOM0) {
            /* registered mode: run its Lua on_activate(who); only proceed with
             * the native START flow if the callback returns truthy */
            if (ggpo_net_active() || g_online_pending_match.active) return 0;
            if (!lua_manager_menu_mode_activate(g_menu_mode - MENU_MODE_CUSTOM0, who)) {
                return 0;
            }
        }
        /* PLAY / VS AI / TRAIN: run the original native START activation */
        if (g_main_start_orig_action) {
            fn_btn_player_filter_t orig = (fn_btn_player_filter_t)g_main_start_orig_action;
            if (orig(btn, 3)) {
                int nolink = 0;
                if (!IsBadReadPtr((uint8_t*)btn + BTN_OFS_NOLINK_FLAG, (SIZE_T)sizeof(unsigned char)))
                    nolink = (*(unsigned char*)((uint8_t*)btn + BTN_OFS_NOLINK_FLAG) != 0);
                if (g_main_start_orig_link && !nolink && p_state_switch)
                    p_state_switch(g_main_start_orig_link);
            }
        }
        return 0;
    }
    return menu_mode_forward_nav(btn, event_code);
}

static void apply_main_menu_mode_button(void) {
    void* start_btn;
    void* up_btn;
    void* down_btn;
    float sx, sy, sw, sh;
    menu_mode_load();
    menu_mode_resolve_pending();
    if (!p_button_ex) return;
    if (!menu_mode_available(g_menu_mode)) g_menu_mode = MENU_MODE_PLAY;

    start_btn = online_find_button_by_action(MAIN_START_ACTION_PTR);
    if (start_btn) {
        /* fresh native button: capture originals, take it over */
        g_main_start_orig_action = *(void**)((uint8_t*)start_btn + BTN_OFS_ACTION_PTR);
        g_main_start_orig_link   = *(void**)((uint8_t*)start_btn + BTN_OFS_LINK_PTR);
        *(void**)((uint8_t*)start_btn + BTN_OFS_ACTION_PTR) = (void*)&menu_mode_main_filter_proxy;
    } else {
        start_btn = online_find_button_by_action((uintptr_t)&menu_mode_main_filter_proxy);
    }
    if (!start_btn) return;
    if (IsBadWritePtr((uint8_t*)start_btn + BTN_OFS_HEIGHT, (SIZE_T)sizeof(float))) return;

    /* per-mode label; ONLINE opens the hub through the proxy (no native link change) */
    *(const char**)((uint8_t*)start_btn + BTN_OFS_LABEL_PTR) = menu_mode_label(g_menu_mode);

    sx = *(float*)((uint8_t*)start_btn + BTN_OFS_CENTER_X);
    sy = *(float*)((uint8_t*)start_btn + BTN_OFS_CENTER_Y);
    sw = *(float*)((uint8_t*)start_btn + BTN_OFS_WIDTH);
    sh = *(float*)((uint8_t*)start_btn + BTN_OFS_HEIGHT);
    (void)sw;

    up_btn = online_find_button_by_action((uintptr_t)&menu_mode_arrow_up_filter_proxy);
    if (!up_btn) {
        if (p_button_set_layout) p_button_set_layout(3.0f, 6.0f);
        up_btn = p_button_ex(1.0f, 4.0f, 0u, FONT_EXT_GLYPH_TRI_UP_STR,
                             (int)(intptr_t)&menu_mode_arrow_up_filter_proxy);
        if (up_btn) *(void**)((uint8_t*)up_btn + BTN_OFS_ACTION_PTR) = (void*)&menu_mode_arrow_up_filter_proxy;
    }
    down_btn = online_find_button_by_action((uintptr_t)&menu_mode_arrow_down_filter_proxy);
    if (!down_btn) {
        if (p_button_set_layout) p_button_set_layout(3.0f, 6.0f);
        down_btn = p_button_ex(1.0f, 5.0f, 0u, FONT_EXT_GLYPH_TRI_DOWN_STR,
                               (int)(intptr_t)&menu_mode_arrow_down_filter_proxy);
        if (down_btn) *(void**)((uint8_t*)down_btn + BTN_OFS_ACTION_PTR) = (void*)&menu_mode_arrow_down_filter_proxy;
    }

    /* per-mode accent: tint the main button itself (fill + highlight fill) */
    {
        float r, g, b;
        menu_mode_accent(&r, &g, &b);
        btn_write_rgba(start_btn, BTN_OFS_BG_RGBA,    r * 0.55f, g * 0.55f, b * 0.55f, 0.92f);
        btn_write_rgba(start_btn, BTN_OFS_HI_BG_RGBA, r, g, b, 0.95f);
        btn_write_rgba(start_btn, BTN_OFS_FG_RGBA,    0.55f + r * 0.45f, 0.55f + g * 0.45f, 0.55f + b * 0.45f, 1.00f);
        btn_write_rgba(start_btn, BTN_OFS_HI_FG_RGBA, 1.00f, 1.00f, 1.00f, 1.00f);

        /* arrows: bare glyphs — no fill, no panel backing, accent-tinted labels */
        if (up_btn && !IsBadWritePtr((uint8_t*)up_btn + BTN_OFS_HEIGHT, (SIZE_T)sizeof(float))) {
            *(float*)((uint8_t*)up_btn + BTN_OFS_CENTER_X) = sx;
            *(float*)((uint8_t*)up_btn + BTN_OFS_CENTER_Y) = sy - sh * 0.85f;
            *(float*)((uint8_t*)up_btn + BTN_OFS_WIDTH)  = sh * 0.45f;
            *(float*)((uint8_t*)up_btn + BTN_OFS_HEIGHT) = sh * 0.50f;
            if (!IsBadWritePtr((uint8_t*)up_btn + BTN_OFS_BACKING, (SIZE_T)sizeof(uint32_t)))
                *(uint32_t*)((uint8_t*)up_btn + BTN_OFS_BACKING) = 0;
            btn_write_rgba(up_btn, BTN_OFS_BG_RGBA,    0.0f, 0.0f, 0.0f, 0.0f);
            btn_write_rgba(up_btn, BTN_OFS_HI_BG_RGBA, 0.0f, 0.0f, 0.0f, 0.0f);
            btn_write_rgba(up_btn, BTN_OFS_FG_RGBA,    r * 0.75f, g * 0.75f, b * 0.75f, 0.9f);
            btn_write_rgba(up_btn, BTN_OFS_HI_FG_RGBA, 1.0f, 1.0f, 1.0f, 1.0f);
        }
        if (down_btn && !IsBadWritePtr((uint8_t*)down_btn + BTN_OFS_HEIGHT, (SIZE_T)sizeof(float))) {
            *(float*)((uint8_t*)down_btn + BTN_OFS_CENTER_X) = sx;
            *(float*)((uint8_t*)down_btn + BTN_OFS_CENTER_Y) = sy + sh * 0.85f;
            *(float*)((uint8_t*)down_btn + BTN_OFS_WIDTH)  = sh * 0.45f;
            *(float*)((uint8_t*)down_btn + BTN_OFS_HEIGHT) = sh * 0.50f;
            if (!IsBadWritePtr((uint8_t*)down_btn + BTN_OFS_BACKING, (SIZE_T)sizeof(uint32_t)))
                *(uint32_t*)((uint8_t*)down_btn + BTN_OFS_BACKING) = 0;
            btn_write_rgba(down_btn, BTN_OFS_BG_RGBA,    0.0f, 0.0f, 0.0f, 0.0f);
            btn_write_rgba(down_btn, BTN_OFS_HI_BG_RGBA, 0.0f, 0.0f, 0.0f, 0.0f);
            btn_write_rgba(down_btn, BTN_OFS_FG_RGBA,    r * 0.75f, g * 0.75f, b * 0.75f, 0.9f);
            btn_write_rgba(down_btn, BTN_OFS_HI_FG_RGBA, 1.0f, 1.0f, 1.0f, 1.0f);
        }
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

/* True for menu/overlay states that can sit on top of a live online match. While
 * in one of these the sim must keep advancing (online can't pause), but the local
 * player's inputs must be neutralized so menu navigation doesn't drive the fight. */
static int online_state_ticks_via_button_update(void* st) {
    /* Both native input-remapping pages share the menu_common_update path, which
     * reaches hooked_main_update_with_buttons exactly once. Keep them here rather
     * than adding remap-specific update detours; doing both would double-tick. */
    return st == (void*)(uintptr_t)ADDR_OPTIONS_STATE_PAUSED ||
           st == (void*)(uintptr_t)ADDR_OPTIONS_STATE ||
           st == (void*)(uintptr_t)ADDR_REMAP_STATE1 ||
           st == (void*)(uintptr_t)ADDR_REMAP_STATE2 ||
           st == (void*)&g_mods_state ||
           st == (void*)&g_mods_entry_state;
}

static int online_state_is_ingame_menu(void* st) {
    return online_state_ticks_via_button_update(st) ||
           st == (void*)&g_console_state;
}

static DiscordRpcActivity online_discord_activity(void) {
    void* state_ptr = p_state_current ? p_state_current() : NULL;
    if (g_online_pending_match.active) {
        return DISCORD_RPC_ACTIVITY_MATCH_SETUP;
    }
    if (g_online_active_match.active || ggpo_net_active()) {
        if (g_online_active_match.competitive ||
            g_online_active_match.queue_mode == 2) {
            return DISCORD_RPC_ACTIVITY_MATCH_COMPETITIVE;
        }
        if (g_online_active_match.queue_mode == 1) {
            return DISCORD_RPC_ACTIVITY_MATCH_CASUAL;
        }
        return DISCORD_RPC_ACTIVITY_MATCH_PRIVATE;
    }
    if (g_online_queue_mode == 2) {
        return DISCORD_RPC_ACTIVITY_QUEUE_COMPETITIVE;
    }
    if (g_online_queue_mode == 1) {
        return DISCORD_RPC_ACTIVITY_QUEUE_CASUAL;
    }
    if (state_ptr == (void*)&g_online_hub_state) {
        if (online_remembered_login_in_progress() || g_online_auth_pending) {
            return DISCORD_RPC_ACTIVITY_SIGNING_IN;
        }
        if (g_online_tab == ONLINE_TAB_FRIENDS) {
            return DISCORD_RPC_ACTIVITY_SOCIAL;
        }
        return DISCORD_RPC_ACTIVITY_ONLINE_HUB;
    }
    if (state_ptr == (void*)(uintptr_t)ADDR_GAME_STATE) {
        return DISCORD_RPC_ACTIVITY_LOCAL_MATCH;
    }
    return DISCORD_RPC_ACTIVITY_MENUS;
}

static int online_advance_net_gameplay_tick(int arg0) {
    uint32_t raw0;
    uint32_t raw1;
    void* state_ptr = p_state_current ? p_state_current() : NULL;
    int local_player = ggpo_net_local_player();
    uint32_t checksum = 0;
    int advanced_any = 0;
    int steps = 0;
    char err[512];
    char out[CONSOLE_LINE_TEXT];

    if (g_online_pending_match.active) return 0;

    /* This is one tentative wall-tick snapshot, not an input-ring assignment.
     * ggpo_net_advance commits it only after the current logical frame clears
     * every no-advance gate. Repeated stalled wall ticks therefore repoll and
     * discard stale values. Catch-up steps intentionally reuse this snapshot:
     * no newer physical event exists inside one native update. */
    raw0 = hooks_peek_player_cmds_raw(0, 2);
    raw1 = hooks_peek_player_cmds_raw(1, 2);

    /* Both locally configured control sets drive this client's one authoritative
     * online character. The other character still receives only the peer's input
     * through GGPO, so local player-two bindings can never leak across the wire as
     * control of the opponent. */
    if (local_player < 0 || local_player > 1) {
        local_player = clampi(g_online_active_match.local_player, 0, 1);
    }
    if (local_player == 0) raw0 |= raw1;
    else raw1 |= raw0;

    if (online_state_is_ingame_menu(state_ptr)) {
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
            if (!g_online_connect.established && !ggpo_net_connected() &&
                g_online_connect.attempts > 0) {
                LOG_WARN("online.p2p: attempt %d/%d failed before confirmation (%s); retrying",
                         g_online_connect.attempts,
                         ONLINE_CONNECT_MAX_ATTEMPTS,
                         err[0] ? err : "unknown error");
                if (ggpo_net_active()) ggpo_net_stop_for_retry();
                online_connect_retry_tick();
                return 0;
            }
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
    /* Polling happens inside ggpo_net_advance. Evaluate the wall-clock deadline
     * only afterward so a final queued handshake packet gets its chance. */
    online_connect_retry_tick();
    online_match_poll_completion();
    online_log_desync_snapshot_if_changed();
    return advanced_any ? 1 : 0;
}

static int hooks_cmdline_has_flag(const char* flag) {
    const char* cl = GetCommandLineA();
    const char* p;
    size_t flen = 0;
    if (!cl || !flag) return 0;
    while (flag[flen]) flen++;
    for (p = cl; *p; p++) {
        size_t i = 0;
        if (p != cl && p[-1] != ' ' && p[-1] != '\t') continue; /* token start only */
        while (i < flen && p[i] && ((p[i] | 0x20) == (flag[i] | 0x20))) i++;
        if (i == flen && (p[i] == 0 || p[i] == ' ' || p[i] == '\t')) return 1;
    }
    return 0;
}

enum {
    SDL_WINDOW_FULLSCREEN_FLAG = 0x00000001u,
    SDL_WINDOW_FULLSCREEN_DESKTOP_FLAG = 0x00001001u,
    WINDOW_EVENT_MOVED = 4,
    WINDOW_EVENT_RESIZED = 5,
    WINDOW_EVENT_SIZE_CHANGED = 6,
    WINDOW_EVENT_MAXIMIZED = 8,
    WINDOW_EVENT_DISPLAY_CHANGED = 18,
    WINDOW_KEY_ACTION_QUEUE_CAP = 8,
    WINDOW_LAUNCH_SETTLE_MS = 500,
    WINDOW_LAUNCH_RETRY_MS = 300,
    WINDOW_RESIZE_DEBOUNCE_MS = 180,
    WINDOW_TRANSITION_SUPPRESS_MS = 700,
};

typedef struct SdlRectCompat {
    int x;
    int y;
    int w;
    int h;
} SdlRectCompat;

static int hooks_window_time_reached(DWORD now, DWORD deadline) {
    return (LONG)(now - deadline) >= 0;
}

static void hooks_window_parse_launch_mode(void) {
    int borderless;
    int fullscreen;
    int windowed;
    if (g_window_runtime.command_line_parsed) return;
    g_window_runtime.command_line_parsed = 1;
    borderless = hooks_cmdline_has_flag("-borderless") || hooks_cmdline_has_flag("--borderless");
    fullscreen = hooks_cmdline_has_flag("-fullscreen") || hooks_cmdline_has_flag("--fullscreen");
    windowed = hooks_cmdline_has_flag("-windowed") || hooks_cmdline_has_flag("--windowed");
    if (borderless) g_window_runtime.launch_mode = WINDOW_LAUNCH_BORDERLESS;
    else if (fullscreen) g_window_runtime.launch_mode = WINDOW_LAUNCH_FULLSCREEN;
    else if (windowed) g_window_runtime.launch_mode = WINDOW_LAUNCH_WINDOWED;
    else g_window_runtime.launch_mode = WINDOW_LAUNCH_DEFAULT;
    if ((borderless + fullscreen + windowed) > 1) {
        LOG_WARN("window: conflicting launch modes; precedence is borderless, fullscreen, windowed");
    }
    if (g_window_runtime.launch_mode == WINDOW_LAUNCH_DEFAULT) {
        g_window_runtime.launch_done = 1;
    }
}

static int hooks_window_active_display(void) {
    typedef int (__cdecl *fn_get_display_t)(void*);
    int display = -1;
    if (g_proxy_sdl_window && p_SDL_GetWindowDisplayIndex) {
        display = ((fn_get_display_t)p_SDL_GetWindowDisplayIndex)(g_proxy_sdl_window);
    }
    if (display < 0) display = g_window_runtime.last_display_index;
    if (display < 0) display = 0;
    return display;
}

static int hooks_window_display_bounds(int display, int* out_w, int* out_h) {
    typedef int (__cdecl *fn_get_bounds_t)(int, void*);
    SdlRectCompat bounds;
    int w = WINDOW_POLICY_MAX_W + WINDOW_POLICY_FRAME_MARGIN_W;
    int h = WINDOW_POLICY_MAX_H + WINDOW_POLICY_FRAME_MARGIN_H;
    int valid = 0;
    memset(&bounds, 0, sizeof(bounds));
    if (p_SDL_GetDisplayBounds &&
        ((fn_get_bounds_t)p_SDL_GetDisplayBounds)(display, &bounds) == 0 &&
        bounds.w > 0 && bounds.h > 0) {
        w = bounds.w;
        h = bounds.h;
        valid = 1;
    }
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    return valid;
}

static int hooks_window_sync_native_desktop_bounds(int display, int* out_w, int* out_h) {
    int display_w;
    int display_h;
    if (!hooks_window_display_bounds(display, &display_w, &display_h)) {
        LOG_WARN("window: active display %d bounds unavailable; fullscreen transition deferred", display);
        return 0;
    }
    *p_wrapper_desktop_w = display_w;
    *p_wrapper_desktop_h = display_h;
    if (out_w) *out_w = display_w;
    if (out_h) *out_h = display_h;
    LOG_INFO("window: native desktop cache set to %dx%d for display %d",
             display_w, display_h, display);
    return 1;
}

static unsigned int hooks_window_flags(void) {
    typedef unsigned int (__cdecl *fn_get_flags_t)(void*);
    if (!g_proxy_sdl_window || !p_SDL_GetWindowFlags) return 0;
    return ((fn_get_flags_t)p_SDL_GetWindowFlags)(g_proxy_sdl_window);
}

static int hooks_window_mode_matches(WindowLaunchMode mode) {
    unsigned int flags = hooks_window_flags();
    int engine_fullscreen = p_main_is_fullscreen ? (p_main_is_fullscreen() != 0) :
                            ((flags & SDL_WINDOW_FULLSCREEN_FLAG) != 0);
    if (mode == WINDOW_LAUNCH_WINDOWED) {
        return !engine_fullscreen && !(flags & SDL_WINDOW_FULLSCREEN_FLAG);
    }
    if (mode == WINDOW_LAUNCH_FULLSCREEN) {
        return engine_fullscreen && (flags & SDL_WINDOW_FULLSCREEN_FLAG);
    }
    if (mode == WINDOW_LAUNCH_BORDERLESS) {
        return engine_fullscreen &&
               (flags & SDL_WINDOW_FULLSCREEN_DESKTOP_FLAG) == SDL_WINDOW_FULLSCREEN_DESKTOP_FLAG;
    }
    return 1;
}

static void hooks_window_refresh_geometry(const char* reason, int force_viewport) {
    typedef void (__cdecl *fn_get_size_t)(void*, int*, int*);
    int ww = -1, wh = -1, dw = -1, dh = -1;
    int display;
    unsigned int flags;
    int changed;
    GLint viewport[4] = { 0, 0, 0, 0 };
    GLint scissor[4] = { 0, 0, 0, 0 };
    int scissor_was_full = 0;
    if (!g_proxy_sdl_window) return;
    if (p_SDL_GetWindowSize) {
        ((fn_get_size_t)p_SDL_GetWindowSize)(g_proxy_sdl_window, &ww, &wh);
    }
    if (p_SDL_GL_GetDrawableSize) {
        ((fn_get_size_t)p_SDL_GL_GetDrawableSize)(g_proxy_sdl_window, &dw, &dh);
    }
    if (dw <= 0 || dh <= 0) {
        dw = ww;
        dh = wh;
    }
    display = hooks_window_active_display();
    flags = hooks_window_flags();
    changed = ww != g_window_runtime.last_window_w ||
              wh != g_window_runtime.last_window_h ||
              dw != g_window_runtime.last_drawable_w ||
              dh != g_window_runtime.last_drawable_h ||
              display != g_window_runtime.last_display_index ||
              flags != g_window_runtime.last_window_flags;
    if (dw > 0 && dh > 0 && (force_viewport || changed)) {
        glGetIntegerv(GL_VIEWPORT, viewport);
        if (force_viewport || viewport[0] != 0 || viewport[1] != 0 ||
            viewport[2] != dw || viewport[3] != dh) {
            if (glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE) {
                glGetIntegerv(GL_SCISSOR_BOX, scissor);
                scissor_was_full = (scissor[0] == viewport[0] && scissor[1] == viewport[1] &&
                                     scissor[2] == viewport[2] && scissor[3] == viewport[3]);
            }
            glViewport(0, 0, dw, dh);
            if (scissor_was_full) glScissor(0, 0, dw, dh);
        }
    }
    if (changed) {
        LOG_INFO("window[%s]: display=%d window=%dx%d drawable=%dx%d flags=0x%08X viewport=%d,%d %dx%d dpi-scale=%.3fx%.3f",
                 reason ? reason : "changed", display, ww, wh, dw, dh, flags,
                 viewport[0], viewport[1], viewport[2], viewport[3],
                 ww > 0 ? (double)dw / (double)ww : 0.0,
                 wh > 0 ? (double)dh / (double)wh : 0.0);
    }
    g_window_runtime.last_window_w = ww;
    g_window_runtime.last_window_h = wh;
    g_window_runtime.last_drawable_w = dw;
    g_window_runtime.last_drawable_h = dh;
    g_window_runtime.last_display_index = display;
    g_window_runtime.last_window_flags = flags;
    g_window_runtime.geometry_dirty = 0;
}

static int hooks_window_apply_mode(WindowLaunchMode mode, const char* reason) {
    typedef int (__cdecl *fn_sdl_set_window_fullscreen_t)(void*, unsigned int);
    int display = hooks_window_active_display();
    unsigned int flags = hooks_window_flags();
    int engine_fullscreen = p_main_is_fullscreen ? (p_main_is_fullscreen() != 0) :
                            ((flags & SDL_WINDOW_FULLSCREEN_FLAG) != 0);
    int rc = 0;
    DWORD now = GetTickCount();
    if (!g_proxy_sdl_window || !p_main_set_fullscreen) return 0;
    if (mode == WINDOW_LAUNCH_FULLSCREEN || mode == WINDOW_LAUNCH_BORDERLESS) {
        /* main_set_fullscreen reads wrapper_desktop_w/h before its graphics
         * recreation reaches our display override. Refresh those native caches
         * first so a window moved to a differently sized monitor does not enter
         * fullscreen using the previous monitor's dimensions. */
        if (!hooks_window_sync_native_desktop_bounds(display, NULL, NULL)) return 0;
    }
    g_window_runtime.suppress_resize_until_ms = now + WINDOW_TRANSITION_SUPPRESS_MS;
    g_window_runtime.pending_resize = 0;
    g_window_runtime.pending_maximize = 0;
    InterlockedExchange(&g_proxy_sdl_display_override, (LONG)display);
    if (mode == WINDOW_LAUNCH_WINDOWED) {
        if (engine_fullscreen || (flags & SDL_WINDOW_FULLSCREEN_FLAG)) {
            int display_w, display_h;
            int saved_w = p_main_saved_window_w ? *p_main_saved_window_w : WINDOW_POLICY_MID_W;
            int saved_h = p_main_saved_window_h ? *p_main_saved_window_h : WINDOW_POLICY_MID_H;
            WindowPolicySize safe;
            hooks_window_display_bounds(display, &display_w, &display_h);
            safe = window_policy_fit_3_2(saved_w, saved_h, display_w, display_h);
            if (p_main_saved_window_w) *p_main_saved_window_w = safe.w;
            if (p_main_saved_window_h) *p_main_saved_window_h = safe.h;
            p_main_set_fullscreen(0);
        }
    } else if (!engine_fullscreen || !(flags & SDL_WINDOW_FULLSCREEN_FLAG)) {
        p_main_set_fullscreen(1);
    }
    InterlockedExchange(&g_proxy_sdl_display_override, -1);
    if (mode == WINDOW_LAUNCH_BORDERLESS) {
        if (!g_proxy_sdl_window || !p_SDL_SetWindowFullscreen) return 0;
        rc = ((fn_sdl_set_window_fullscreen_t)p_SDL_SetWindowFullscreen)(
            g_proxy_sdl_window, SDL_WINDOW_FULLSCREEN_DESKTOP_FLAG);
        if (rc != 0) {
            const char* err = p_SDL_GetError ? ((const char* (__cdecl *)(void))p_SDL_GetError)() : "unknown SDL error";
            LOG_WARN("window[%s]: desktop fullscreen failed rc=%d (%s)", reason ? reason : "mode", rc, err ? err : "unknown");
        }
    }
    g_window_runtime.geometry_dirty = 1;
    hooks_window_refresh_geometry(reason, 1);
    return rc == 0 && hooks_window_mode_matches(mode);
}

static int hooks_window_set_native_size(int requested_w, int requested_h, const char* reason) {
    int display = hooks_window_active_display();
    int display_w, display_h;
    WindowPolicySize size;
    DWORD now = GetTickCount();
    if (!p_main_set_window || !g_proxy_sdl_window) return 0;
    hooks_window_display_bounds(display, &display_w, &display_h);
    size = window_policy_fit_3_2(requested_w, requested_h, display_w, display_h);
    g_window_runtime.suppress_resize_until_ms = now + WINDOW_TRANSITION_SUPPRESS_MS;
    g_window_runtime.pending_resize = 0;
    g_window_runtime.pending_maximize = 0;
    InterlockedExchange(&g_proxy_sdl_display_override, (LONG)display);
    p_main_set_window(size.w, size.h);
    InterlockedExchange(&g_proxy_sdl_display_override, -1);
    g_window_runtime.geometry_dirty = 1;
    hooks_window_refresh_geometry(reason, 1);
    LOG_INFO("window[%s]: native 3:2 size %dx%d on display %d", reason ? reason : "resize", size.w, size.h, display);
    return 1;
}

static int hooks_window_cycle_native_size(void) {
    int display = hooks_window_active_display();
    int display_w, display_h;
    int current_w = p_main_saved_window_w ? *p_main_saved_window_w : g_window_runtime.last_window_w;
    WindowPolicySize next;
    hooks_window_display_bounds(display, &display_w, &display_h);
    next = window_policy_next_preset(current_w, display_w, display_h);
    return hooks_window_set_native_size(next.w, next.h, "F1");
}

static int hooks_window_queue_key_action(WindowKeyAction action) {
    unsigned int shift;
    if (action == WINDOW_KEY_ACTION_NONE) return 0;
    if (g_window_runtime.pending_key_action_count >= WINDOW_KEY_ACTION_QUEUE_CAP) {
        LOG_WARN("window: key action queue full; dropping action %d", (int)action);
        return 0;
    }
    shift = (unsigned int)g_window_runtime.pending_key_action_count * 2u;
    g_window_runtime.pending_key_actions |= ((unsigned int)action & 3u) << shift;
    g_window_runtime.pending_key_action_count++;
    return 1;
}

static WindowKeyAction hooks_window_pop_key_action(void) {
    WindowKeyAction action;
    if (g_window_runtime.pending_key_action_count <= 0) return WINDOW_KEY_ACTION_NONE;
    action = (WindowKeyAction)(g_window_runtime.pending_key_actions & 3u);
    g_window_runtime.pending_key_actions >>= 2;
    g_window_runtime.pending_key_action_count--;
    return action;
}

void hooks_window_event(int event_code, int data1, int data2) {
    DWORD now = GetTickCount();
    g_window_runtime.geometry_dirty = 1;
    if (event_code == WINDOW_EVENT_MOVED || event_code == WINDOW_EVENT_DISPLAY_CHANGED) {
        g_window_runtime.last_display_index = -1;
    }
    if (event_code == WINDOW_EVENT_MAXIMIZED &&
        hooks_window_time_reached(now, g_window_runtime.suppress_resize_until_ms)) {
        /* Maximized state is intentionally transient: after the resize event
         * burst settles, recreate once at the largest native 3:2 preset that
         * fits this display. Generated recreation events are suppress-filtered. */
        g_window_runtime.pending_resize = 1;
        g_window_runtime.pending_maximize = 1;
        g_window_runtime.resize_due_ms = now + WINDOW_RESIZE_DEBOUNCE_MS;
    }
    if ((event_code == WINDOW_EVENT_RESIZED || event_code == WINDOW_EVENT_SIZE_CHANGED) &&
        data1 > 0 && data2 > 0 &&
        hooks_window_time_reached(now, g_window_runtime.suppress_resize_until_ms)) {
        g_window_runtime.pending_resize = 1;
        if (!g_window_runtime.pending_maximize) {
            g_window_runtime.pending_w = data1;
            g_window_runtime.pending_h = data2;
        }
        g_window_runtime.resize_due_ms = now + WINDOW_RESIZE_DEBOUNCE_MS;
    }
}

int hooks_window_keydown(int sym, int repeat) {
    WindowKeyAction action;
    if (sym != SDLK_F1 && sym != SDLK_F11) return 0;
    if (repeat) return 1;
    hooks_window_parse_launch_mode();
    g_window_runtime.launch_done = 1; /* explicit user input wins over a delayed launch arg */
    action = sym == SDLK_F1 ? WINDOW_KEY_ACTION_CYCLE_SIZE
                            : WINDOW_KEY_ACTION_TOGGLE_FULLSCREEN;
    (void)hooks_window_queue_key_action(action);
    return 1;
}

static void hooks_window_pump(void) {
    DWORD now = GetTickCount();
    if (InterlockedCompareExchange(&g_window_runtime.busy, 1, 0) != 0) return;
    hooks_window_parse_launch_mode();
    if (g_proxy_sdl_window && g_window_runtime.first_window_ms == 0) {
        g_window_runtime.first_window_ms = now;
        g_window_runtime.next_launch_attempt_ms = now + WINDOW_LAUNCH_SETTLE_MS;
        g_window_runtime.geometry_dirty = 1;
    }
    if (g_window_runtime.geometry_dirty && g_proxy_sdl_window) {
        hooks_window_refresh_geometry("event", 0);
    }
    if (g_window_runtime.pending_key_action_count > 0 && g_proxy_sdl_window) {
        WindowKeyAction action = hooks_window_pop_key_action();
        if (action == WINDOW_KEY_ACTION_CYCLE_SIZE) {
            (void)hooks_window_cycle_native_size();
        } else if (action == WINDOW_KEY_ACTION_TOGGLE_FULLSCREEN) {
            WindowLaunchMode target = (p_main_is_fullscreen && p_main_is_fullscreen())
                ? WINDOW_LAUNCH_WINDOWED
                : (g_window_runtime.launch_mode == WINDOW_LAUNCH_BORDERLESS
                    ? WINDOW_LAUNCH_BORDERLESS
                    : WINDOW_LAUNCH_FULLSCREEN);
            (void)hooks_window_apply_mode(target, "F11");
        }
    }
    if (!g_window_runtime.launch_done && g_proxy_sdl_window &&
        hooks_window_time_reached(now, g_window_runtime.next_launch_attempt_ms)) {
        g_window_runtime.launch_attempts++;
        if (hooks_window_apply_mode(g_window_runtime.launch_mode, "launch")) {
            g_window_runtime.launch_done = 1;
            LOG_INFO("window: launch mode applied after %d attempt(s)", g_window_runtime.launch_attempts);
        } else if (g_window_runtime.launch_attempts >= 5) {
            g_window_runtime.launch_done = 1;
            LOG_WARN("window: launch mode could not be verified after %d attempts", g_window_runtime.launch_attempts);
        } else {
            g_window_runtime.next_launch_attempt_ms = now + WINDOW_LAUNCH_RETRY_MS;
        }
    }
    if (g_window_runtime.pending_resize &&
        hooks_window_time_reached(now, g_window_runtime.resize_due_ms)) {
        unsigned int flags = hooks_window_flags();
        if (!(flags & SDL_WINDOW_FULLSCREEN_FLAG) &&
            (!p_main_is_fullscreen || !p_main_is_fullscreen())) {
            if (g_window_runtime.pending_maximize) {
                int display = hooks_window_active_display();
                int display_w;
                int display_h;
                WindowPolicySize largest;
                hooks_window_display_bounds(display, &display_w, &display_h);
                largest = window_policy_largest_preset(display_w, display_h);
                (void)hooks_window_set_native_size(largest.w, largest.h, "maximize");
            } else {
                (void)hooks_window_set_native_size(g_window_runtime.pending_w,
                                                   g_window_runtime.pending_h,
                                                   "resize");
            }
        } else {
            g_window_runtime.pending_resize = 0;
            g_window_runtime.pending_maximize = 0;
        }
    }
    InterlockedExchange(&g_window_runtime.busy, 0);
}

void hooks_window_on_pre_swap(void) {
    if (!g_proxy_sdl_window) return;
    if (InterlockedCompareExchange(&g_window_runtime.busy, 1, 0) != 0) return;
    hooks_window_refresh_geometry("pre-swap", 0);
    InterlockedExchange(&g_window_runtime.busy, 0);
}

static int __cdecl hooked_main_update_with_buttons(int arg0) {
    fn_main_update_with_buttons_t real_update = p_main_update_with_buttons_trampoline
        ? p_main_update_with_buttons_trampoline
        : p_main_update_with_buttons;
    void* state_ptr = NULL;
    int is_game_state = 0;

    /* Mark the sim thread for audio-thread RNG isolation (see hooks_on_audio_thread). */
    g_sim_thread_id = GetCurrentThreadId();
    /* mad_run drains wrapper_handle_events before state_update reaches this
     * hook. This is the sole destructive window-policy pump, so queued key,
     * launch, and resize transitions cannot recreate a window mid-PollEvent. */
    hooks_window_pump();
    online_launch_pump();

    if (p_state_current) state_ptr = p_state_current();
    is_game_state = (state_ptr == (void*)(uintptr_t)ADDR_GAME_STATE);
    online_monitor_active_match_state(state_ptr);
    if (!is_game_state) {
        lua_manager_on_tick();
    }
    online_server_update();
    discord_rpc_ext_pump(online_discord_activity());
    {
        int result = real_update ? real_update(arg0) : 0;
        framework_tune_pump();
        online_match_pump_launch();
        if (!is_game_state) {
            void* after_update = p_state_current ? p_state_current() : NULL;
            int net_tick_attempted = 0;
            int net_tick_result = 0;
            if (after_update == (void*)(uintptr_t)ADDR_MAIN_STATE ||
                after_update == (void*)(uintptr_t)ADDR_MAIN_STATE_INITIAL) {
                apply_main_menu_mode_button();
                hooks_clear_ai_match();
            }
            online_monitor_active_match_state(after_update);
            /* Keep the online sim advancing while ANY menu is open mid-match (pause,
             * options/controls, etc.), not just the pause screen - an online match
             * can't actually pause, so both peers must keep ticking or the session
             * stalls and drops. ggpo_net_active() is only true during a live match,
             * and GAME_STATE advances the netcode itself, so this covers the menu
             * overlays without double-ticking. */
            if (ggpo_net_active() &&
                !g_online_pending_match.active &&
                online_state_ticks_via_button_update(after_update)) {
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

void hooks_runtime_shutdown(void) {
    framework_tune_shutdown();
    discord_rpc_ext_shutdown();
    update_ext_shutdown();
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
    /* Record the simulation thread so the RNG hooks can tell audio-thread draws
     * apart and keep them off the gameplay seed (audio-thread RNG isolation). */
    g_sim_thread_id = GetCurrentThreadId();

    /* Launch-mute fix: vanilla app_state_init force-calls mad_enable_synth(1)
     * and defaults DAT_0055a170=1, THEN loads settings.nogg (which overwrites
     * the saved "sound" value into DAT_0055a170) but never re-applies it to the
     * synth-enabled flag (DAT_0054c0ca). So launching with sound saved OFF still
     * plays audio until main_update_settings runs on an options toggle (the
     * "enable then disable to mute" bug). Sync the synth flag to the saved
     * setting exactly once, after the audio stream is initialized. */
    static int g_launch_synth_synced = 0;
    if (!g_launch_synth_synced && g_audio_stream_inited && *g_audio_stream_inited > 0) {
        if (g_sound_setting) hooks_set_native_synth_enabled(*g_sound_setting != 0);
        g_launch_synth_synced = 1;
    }

    fn_game_update_t real_update = p_game_update_trampoline
        ? p_game_update_trampoline
        : p_game_update;
    void* state_ptr = p_state_current ? p_state_current() : NULL;

    if (state_ptr != (void*)(uintptr_t)ADDR_GAME_STATE) {
        if (real_update) real_update(arg0);
        return;
    }

    /* Defensive invariant: a server-managed pending match is prepared and
     * synchronized entirely in the hub. If another path enters GAME early,
     * never expose native simulation while the prematch barrier is active. */
    if (g_online_pending_match.active) {
        hooks_finish_game_tick();
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

    if (!ggpo_net_active() &&
        g_online_connect.attempts > 0 &&
        !g_online_connect.established &&
        (g_online_pending_match.active || g_online_active_match.active)) {
        /* A failed bind/restart leaves a short no-socket gap. Keep the native game
         * frozen while the retry state machine opens the next attempt. */
        online_connect_retry_tick();
        if (!ggpo_net_active()) {
            lua_manager_on_tick_post();
            hooks_finish_game_tick();
            return;
        }
    }

    if (ggpo_net_active()) {
        int net_tick_result = online_advance_net_gameplay_tick(arg0);
        lua_manager_on_tick_post();
        if (net_tick_result <= 0) {
            hooks_finish_game_tick();
        }
        return;
    }

    if (real_update) {
        (void)hooks_run_native_game_tick(real_update, arg0, 0);
    }
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

static int content_bridge_resolve_sprite(void* user,
                                         const char* sheet_key,
                                         int sheet_index,
                                         int* out_sprite_id) {
    (void)user;
    return lua_manager_content_resolve_sprite(sheet_key, sheet_index,
                                              out_sprite_id);
}

static void* content_bridge_sprite_get(void* user, int sprite_id) {
    (void)user;
    if (!p_sprite_get || sprite_id < 0) return NULL;
    return p_sprite_get((uint32_t)sprite_id);
}

static int content_bridge_turtle_trans(void* user, double x, double y) {
    fn_turtle_trans_t real = g_turtle_trans_trampoline
        ? (fn_turtle_trans_t)g_turtle_trans_trampoline
        : p_turtle_trans;
    (void)user;
    if (!real) return 0;
    real(x, y);
    return 1;
}

static int content_bridge_turtle_set_angle(void* user, double value) {
    (void)user;
    if (!p_turtle_set_angle) return 0;
    p_turtle_set_angle(value);
    return 1;
}

static int content_bridge_turtle_set_scalex(void* user, double value) {
    (void)user;
    if (!p_turtle_set_scalex) return 0;
    p_turtle_set_scalex(value);
    return 1;
}

static int content_bridge_turtle_set_scaley(void* user, double value) {
    (void)user;
    if (!p_turtle_set_scaley) return 0;
    p_turtle_set_scaley(value);
    return 1;
}

static int content_bridge_turtle_set_rgba(void* user,
                                          float r,
                                          float g,
                                          float b,
                                          float a) {
    (void)user;
    if (!p_turtle_set_rgba) return 0;
    p_turtle_set_rgba(r, g, b, a);
    return 1;
}

static int content_bridge_sprite_batch_plot(void* user,
                                            void* sprite,
                                            int flip_x,
                                            int layer) {
    fn_sprite_batch_plot_t real = g_sprite_batch_plot_trampoline
        ? (fn_sprite_batch_plot_t)g_sprite_batch_plot_trampoline
        : p_sprite_batch_plot;
    (void)user;
    if (!real || !sprite || layer < 0 || layer > 1) return 0;
    real((int)(intptr_t)sprite, flip_x ? 1 : 0, layer);
    return 1;
}

static int content_bridge_map_script_visual_override(
    void* user,
    uint32_t cell_index,
    const char* tile_key,
    int current_sprite_index,
    ContentBridgeVisualOverride* out_override) {
    MapScriptVisualOverride visual;
    (void)user;
    (void)tile_key;
    (void)current_sprite_index;
    if (!out_override || !map_script_is_active() || map_script_is_faulted() ||
        !map_script_visual_override(cell_index, &visual)) {
        return 0;
    }
    out_override->sprite_index = visual.sprite_index;
    out_override->offset_x = visual.offset_x;
    out_override->offset_y = visual.offset_y;
    return 1;
}

static void hooks_map_native_tileset_clear(void) {
    g_map_native_tileset_sheet[0] = '\0';
    g_map_native_tileset_sprite_count = 0;
    g_map_native_tileset_enabled = 0;
}

static void hooks_map_native_tileset_configure(int selector) {
    CustomMapContentView view;
    hooks_map_native_tileset_clear();
    if (custom_maps_pinned_content_view(selector, &view) != 1 ||
        !view.native_layout || !view.default_sheet_key[0] ||
        view.default_sheet_sprite_count < 128 ||
        strlen(view.default_sheet_key) >= sizeof(g_map_native_tileset_sheet)) {
        return;
    }
    snprintf(g_map_native_tileset_sheet,
             sizeof(g_map_native_tileset_sheet), "%s",
             view.default_sheet_key);
    g_map_native_tileset_sprite_count = view.default_sheet_sprite_count;
    g_map_native_tileset_enabled = 1;
}

/* Native tile actions address their sprites as byte offsets from the `_tiles`
 * pointer (one 0x1c-byte sprite record per atlas cell).  A validated
 * native-layout map sheet supplies at least the 128-record native prefix;
 * additional cells remain available to explicit custom tiles. Replacing that
 * base only for the synchronous draw call reskins every ordinary native tile.
 * Physics and update actions continue to use the original native glyph logic. */
static int hooks_map_native_tileset_begin_draw(int* out_saved_tiles) {
    int sprite_id;
    int last_sprite_id;
    void* sprite;
    if (!out_saved_tiles || !g_map_native_tileset_enabled ||
        !g_map_native_tileset_sheet[0] ||
        g_map_native_tileset_sprite_count < 128 || !g_layer || !p_sprite_get ||
        !lua_manager_content_resolve_sprite(g_map_native_tileset_sheet, 0,
                                            &sprite_id) ||
        !lua_manager_content_resolve_sprite(g_map_native_tileset_sheet, 127,
                                            &last_sprite_id) ||
        last_sprite_id != sprite_id + 127 ||
        sprite_id < 0) {
        return 0;
    }
    sprite = p_sprite_get((uint32_t)sprite_id);
    if (!sprite) return 0;
    *out_saved_tiles = *g_layer;
    *g_layer = (int)(intptr_t)sprite;
    return 1;
}

static void hooks_map_native_tileset_end_draw(int saved_tiles) {
    if (g_layer) *g_layer = saved_tiles;
}

/* map_draw at 0x434BD0 calls tile_action_ex at 0x434FD1, then treats a zero
 * result as a request for its generic sprite fallback (0x434FEE..0x435026).
 * That fallback uses:
 *
 *   tile_info[tile[0]].signed_sprite_base + unsigned tile[1]
 *   signed tile[2] as flip, sprite-batch layer 0
 *
 * and plots one 0x1c-byte sprite record from the map tile layer.  A per-map
 * native sheet only swaps `_tiles`, while map_draw's `_layer` remains the
 * vanilla base, so reproduce the fallback synchronously with the caller's
 * selected base before `_tiles` or the live turtle action state is restored. */
static int hooks_plot_native_tile_fallback(const void* tile,
                                            int native_sprite_base) {
    enum {
        NATIVE_TILE_INFO_COUNT = 33,
        NATIVE_TILE_INFO_STRIDE = 0x2c,
        NATIVE_TILE_INFO_SPRITE_BASE_OFFSET = 4,
        NATIVE_TILE_SPRITE_COUNT = 128,
        NATIVE_TILE_SPRITE_STRIDE = 0x1c
    };
    fn_sprite_batch_plot_t plot = g_sprite_batch_plot_trampoline
        ? (fn_sprite_batch_plot_t)g_sprite_batch_plot_trampoline
        : p_sprite_batch_plot;
    const unsigned char* tile_bytes = (const unsigned char*)tile;
    const signed char* native_base_ptr;
    uintptr_t sprite_address;
    int tile_type;
    int sprite_index;
    int flip;

    if (!tile_bytes || native_sprite_base == 0 || !plot ||
        IsBadReadPtr(tile_bytes, 3u)) {
        return 0;
    }
    tile_type = (int)tile_bytes[0];
    if (tile_type <= 0 || tile_type >= NATIVE_TILE_INFO_COUNT) return 0;
    native_base_ptr = (const signed char*)(uintptr_t)(
        ADDR_TILE_INFO +
        (uintptr_t)tile_type * (uintptr_t)NATIVE_TILE_INFO_STRIDE +
        NATIVE_TILE_INFO_SPRITE_BASE_OFFSET);
    sprite_index = (int)(*native_base_ptr) + (int)tile_bytes[1];
    if (sprite_index < 0 || sprite_index >= NATIVE_TILE_SPRITE_COUNT) return 0;

    sprite_address = (uintptr_t)(uint32_t)native_sprite_base +
        (uintptr_t)sprite_index * (uintptr_t)NATIVE_TILE_SPRITE_STRIDE;
    flip = (int)(signed char)tile_bytes[2];
    plot((int)(intptr_t)sprite_address, flip, 0);
    return 1;
}

/* Draw an opt-in native base without letting its turtle mutations alter the
 * custom sprite that follows. The supplied function is always the original
 * trampoline, never hooked_tile_action_ex, so this cannot recurse. A zero
 * action result is completed with map_draw's generic fallback while both the
 * selected atlas and the action-mutated turtle state are still live. */
static int hooks_draw_native_tile_underlay(fn_tile_action_t real,
                                            void* tile,
                                            int mode,
                                            int x,
                                            int y,
                                            int arg5) {
    unsigned char saved_turtle[CONTENT_BRIDGE_TURTLE_STATE_SIZE];
    void* turtle = (void*)(uintptr_t)ADDR_TURTLE_STATE;
    int saved_tiles = 0;
    int swapped_tiles;
    int native_sprite_base;
    int result;
    if (!real || mode != 2) return 0;
    memcpy(saved_turtle, turtle, sizeof(saved_turtle));
    swapped_tiles = hooks_map_native_tileset_begin_draw(&saved_tiles);
    native_sprite_base = swapped_tiles && g_layer
        ? *g_layer
        : (g_map_tile_layer ? *g_map_tile_layer : 0);
    result = real(tile, mode, x, y, arg5);
    if (result == 0 &&
        hooks_plot_native_tile_fallback(tile, native_sprite_base)) {
        result = 1;
    }
    if (swapped_tiles) hooks_map_native_tileset_end_draw(saved_tiles);
    memcpy(turtle, saved_turtle, sizeof(saved_turtle));
    return result;
}

static void __cdecl hooked_mapgen_build_map(void) {
    fn_void_void_t real = p_mapgen_build_map_trampoline
        ? p_mapgen_build_map_trampoline
        : p_mapgen_build_map;
    ContentBridgeBindSummary summary;
    char err[256];
    void* tilemap_base;
    int tilemap_width;
    int tilemap_height;
    int selector;
    MapScriptHost script_host;

    hooks_map_native_tileset_clear();
    if (!real) {
        content_tiles_map_end();
        custom_maps_deactivate_script();
        return;
    }
    real();
    if (InterlockedCompareExchange(&g_content_bridge_enabled, 0, 0) == 0) {
        content_tiles_map_end();
        custom_maps_deactivate_script();
        return;
    }
    tilemap_base = g_tilemap_data_ptr
        ? (void*)(uintptr_t)(*g_tilemap_data_ptr) : NULL;
    tilemap_width = g_tilemap_width ? *g_tilemap_width : 0;
    tilemap_height = g_tilemap_height ? *g_tilemap_height : 0;
    selector = g_hook_map_selector ? *g_hook_map_selector : -1;
    err[0] = '\0';
    if (!content_bridge_bind_selector(tilemap_base, tilemap_width,
                                      tilemap_height, selector,
                                      &summary, err, sizeof(err))) {
        LOG_WARN("content bridge: selector=%d live-map bind failed; using native visuals: %s",
                 selector, err[0] ? err : "unknown binding failure");
        custom_maps_deactivate_script();
        return;
    }
    hooks_map_native_tileset_configure(selector);
    memset(&script_host, 0, sizeof(script_host));
    script_host.rng_seed = g_native_seed ? *g_native_seed : 0u;
    script_host.log_fn = hooks_map_script_log;
    script_host.apply_object_fn = hooks_map_script_apply_object;
    err[0] = '\0';
    if (!custom_maps_activate_script_for_selector(selector, &script_host,
                                                   err, sizeof(err))) {
        LOG_WARN("map script: selector=%d activation failed; scripted behavior disabled: %s",
                 selector, err[0] ? err : "unknown activation failure");
    } else if (map_script_is_active()) {
        LOG_INFO("map script: selector=%d active id=%016llx",
                 selector, (unsigned long long)map_script_active_id());
    }
    if (summary.bound_content_cells > 0) {
        LOG_INFO("content bridge: selector=%d source_cells=%u bound_cells=%u definitions=%u generation=%llu",
                 selector,
                 (unsigned)summary.source_content_cells,
                 (unsigned)summary.bound_content_cells,
                 (unsigned)summary.unique_definitions,
                 (unsigned long long)summary.custom_maps_generation);
    }
}

static int __cdecl hooked_tile_action_ex(void* tile,
                                         int mode,
                                         int x,
                                         int y,
                                         int arg5) {
    static const ContentBridgeDrawOps ops = {
        NULL,
        (void*)(uintptr_t)ADDR_TURTLE_STATE,
        CONTENT_BRIDGE_TURTLE_STATE_SIZE,
        content_bridge_resolve_sprite,
        content_bridge_sprite_get,
        content_bridge_turtle_trans,
        content_bridge_turtle_set_angle,
        content_bridge_turtle_set_scalex,
        content_bridge_turtle_set_scaley,
        content_bridge_turtle_set_rgba,
        content_bridge_sprite_batch_plot,
        content_bridge_map_script_visual_override
    };
    fn_tile_action_t real = p_tile_action_ex_trampoline
        ? p_tile_action_ex_trampoline
        : p_tile_action_ex;
    int saved_tiles = 0;
    int swapped_tiles = 0;
    int result = 0;
    int native_underlay_invoked = 0;
    if (InterlockedCompareExchange(&g_content_bridge_enabled, 0, 0) != 0 &&
        mode == 2) {
        if (real && content_tiles_native_visual_underlay_for_action(tile, mode,
                                                                    x, y)) {
            result = hooks_draw_native_tile_underlay(real, tile, mode,
                                                      x, y, arg5);
            native_underlay_invoked = 1;
        }
        if (content_bridge_draw_action(tile, mode, x, y,
                                       g_game_ticks ? (uint64_t)(*g_game_ticks) : 0u,
                                       &ops)) {
            return 1;
        }
        /* The underlay is also the complete native fallback when custom sprite
         * resolution fails. Never invoke the same action a second time. */
        if (native_underlay_invoked) return result;
    }
    if (!real) return 0;
    if (mode == 2) {
        swapped_tiles = hooks_map_native_tileset_begin_draw(&saved_tiles);
    }
    result = real(tile, mode, x, y, arg5);
    if (swapped_tiles && result == 0 && g_layer &&
        hooks_plot_native_tile_fallback(tile, *g_layer)) {
        /* The fallback was queued from the per-map sheet. Report handled so
         * map_draw cannot queue the same sprite again from vanilla `_layer`. */
        result = 1;
    }
    if (swapped_tiles) hooks_map_native_tileset_end_draw(saved_tiles);
    return result;
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
    int finish_online_after_switch =
        leaving_game && entering_main &&
        online_native_finish_is_presenting();
    void* switched;
    if (finish_online_after_switch &&
        !g_online_active_match.result_reported) {
        /* The zero-countdown branch switches synchronously. Capture and report
         * the winner before native GAME tears itself down. */
        online_match_poll_completion();
    }
    if (leaving_game && entering_main && (g_online_active_match.active || g_online_result.active || ggpo_net_active())) {
        online_reset_native_sound_state("state switch to main");
    }
    switched = real_switch ? real_switch(target) : target;
    if (finish_online_after_switch) {
        char status[192];
        safe_copy(status, sizeof(status),
                  g_online_active_match.completion_status[0]
                      ? g_online_active_match.completion_status
                      : "Match complete.");
        LOG_INFO("online.match: native win sequence returned to main; opening hub");
        if (ggpo_net_active()) stop_ggpo_net("native win sequence complete");
        online_pending_match_reset();
        memset(&g_online_active_match, 0, sizeof(g_online_active_match));
        online_connect_reset();
        online_return_to_hub_after_match(status);
    }
    return switched;
}


void hooks_init(void) {
    static int done = 0;
    if (done) return;
    done = 1;
    framework_audio_load_config();

    /*
     * mad_init_audio_stream starts with `sub esp,0x1c` followed by a complete
     * four-byte argument load. Hook seven bytes before app_state_init asks
     * SDL for vanilla's 22,050-Hz stream. Failure leaves the original rate
     * path intact instead of preventing the game from starting.
     */
    if (!install_detour(&g_mad_init_audio_stream_detour,
                        (void*)(uintptr_t)ADDR_MAD_INIT_AUDIO_STREAM,
                        (void*)&hooked_mad_init_audio_stream, 7)) {
        LOG_WARN("hooks_init: failed to upgrade mixer output rate "
                 "(Dollchan tracks may alias at 22050 Hz)");
    } else {
        p_mad_init_audio_stream_trampoline =
            (fn_mad_init_audio_stream_t)
                g_mad_init_audio_stream_detour.trampoline;
    }

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

    /* thing_new 0x41FD40 begins: push edi (1), xor ecx,ecx (2), push esi
     * (1), mov esi,[0x54204c] (6). Ten bytes is the first complete boundary
     * large enough for the detour. The post-call hook advances a generation
     * only when the allocator returns a verified pool record. */
    if (!install_detour(&g_thing_new_detour,
                        (void*)(uintptr_t)ADDR_THING_NEW,
                        (void*)&hooked_thing_new,
                        10)) {
        LOG_WARN("hooks_init: failed to detour thing_new (map.lua sword contacts disabled)");
    } else {
        p_thing_new_trampoline = (fn_thing_new_t)g_thing_new_detour.trampoline;
        InterlockedExchange(&g_thing_lifecycle_tracking_enabled, 1);
    }

    /* player_die prologue: push esi / push ebx / sub esp,0x34 = exactly 5 bytes */
    if (!install_detour(&g_player_die_detour, (void*)(uintptr_t)ADDR_PLAYER_DIE, (void*)&hooked_player_die, 5)) {
        LOG_WARN("hooks_init: failed to detour player_die (combat ledger disabled)");
    } else {
        p_player_die_trampoline = (fn_player_die_t)g_player_die_detour.trampoline;
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

    // mapgen_build_map starts with:
    //   push ebx        (1)
    //   xor ebx, ebx    (2)
    //   sub esp, 0x28   (3)
    // Patch exactly 6 bytes. The post-call hook binds source-room metadata to
    // the already-generated center/left/right native cells.
    if (!install_detour(&g_mapgen_build_map_detour,
                        (void*)(uintptr_t)ADDR_MAPGEN_BUILD_MAP,
                        (void*)&hooked_mapgen_build_map,
                        6)) {
        LOG_WARN("hooks_init: failed to detour mapgen_build_map (custom tile visuals disabled)");
    } else {
        p_mapgen_build_map_trampoline =
            (fn_void_void_t)g_mapgen_build_map_detour.trampoline;

        // tile_action_ex starts with push edi / push esi / push ebx followed by
        // mov eax,[esp+0x10]. Seven bytes is the first complete boundary. A
        // handled return suppresses only the vanilla sprite fallback.
        if (!install_detour(&g_tile_action_ex_detour,
                            (void*)(uintptr_t)ADDR_TILE_ACTION_EX,
                            (void*)&hooked_tile_action_ex,
                            7)) {
            LOG_WARN("hooks_init: failed to detour tile_action_ex (custom tile visuals disabled)");
            content_tiles_map_end();
        } else {
            p_tile_action_ex_trampoline =
                (fn_tile_action_t)g_tile_action_ex_detour.trampoline;
            InterlockedExchange(&g_content_bridge_enabled, 1);
        }
    }

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
    /* onein: relocate 8 bytes (sub esp,8 + mov eax,1) - clean instruction boundary
     * at 0x4053F8. Routes cosmetic onein gates to the private seed (see hooked_onein). */
    if (!install_detour(&g_onein_detour, (void*)(uintptr_t)ADDR_ONEIN, (void*)&hooked_onein, 8)) {
        LOG_WARN("hooks_init: failed to detour onein (cosmetic gate RNG may drift the gameplay seed)");
    } else {
        g_hooks_onein_trampoline = g_onein_detour.trampoline;
    }

    if (!install_detour(&g_respawn_warble_detour, (void*)(uintptr_t)ADDR_RESPAWN_WARBLE, (void*)&hooked_respawn_warble, 12)) {
        LOG_WARN("hooks_init: failed to detour respawn_warble (audio RNG may affect gameplay RNG)");
    } else {
        p_respawn_warble_trampoline = (fn_synth_callback_t)g_respawn_warble_detour.trampoline;
    }
    if (!install_detour(&g_synth_effect_whistling_detour, (void*)(uintptr_t)ADDR_SYNTH_EFFECT_WHISTLING, (void*)&hooked_synth_effect_whistling, 6)) {
        LOG_WARN("hooks_init: failed to detour synth_effect_whistling (audio RNG may affect gameplay RNG)");
    } else {
        p_synth_effect_whistling_trampoline = (fn_synth_callback_t)g_synth_effect_whistling_detour.trampoline;
    }
    if (!install_detour(&g_sound_sword_ching_detour, (void*)(uintptr_t)ADDR_SOUND_SWORD_CHING, (void*)&hooked_sound_sword_ching, 6)) {
        LOG_WARN("hooks_init: failed to detour sound_sword_ching (audio RNG may affect gameplay RNG)");
    } else {
        p_sound_sword_ching_trampoline = (fn_sound_sword_ching_t)g_sound_sword_ching_detour.trampoline;
    }





LOG_INFO("hooks_init: custom MODS menu ready");
}
