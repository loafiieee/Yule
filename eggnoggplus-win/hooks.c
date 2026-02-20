#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "hooks.h"
#include "lua_manager.h"
#include "log.h"

#define ADDR_STATE_CURRENT            0x405DB0u
#define ADDR_STATE_LAST               0x405DB8u
#define ADDR_STATE_SWITCH             0x405DC0u
#define ADDR_MAIN_UPDATE_WITH_BUTTONS 0x4340E0u
#define ADDR_MENU_COMMON_RENDER       0x4334E0u
#define ADDR_MAIN_BUTTONS_START       0x432FD0u
#define ADDR_MAIN_CURSORS_RESET       0x431200u
#define ADDR_MAIN_CURSOR_SPIN        0x4311E0u
#define ADDR_MAIN_CURSOR_DATA        0x549140u
#define ADDR_MAIN_SPRITE_BATCHES_DRAW 0x431890u
#define ADDR_MENU_BUTTON_LINK         0x433950u
#define ADDR_BUTTON_SET_LAYOUT        0x415F80u
#define ADDR_BTN_PLAYER_FILTER        0x4325C0u
#define ADDR_PLOT_TEXT                0x4304E0u
#define ADDR_PLOT_TEXT_SET_SHADOW     0x430390u
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
#define ADDR_OPTIONS_ENTER            0x4381F0u
#define ADDR_OPTIONS_ENTER_PAUSED     0x438200u

#define MAX_MENU_ROWS     2048
#define CAPTURE_BUF_SIZE   256
#define BASE_UI_W        1280.0f
#define BASE_UI_H         720.0f

#define MODS_BTN_GRID_X      1.0f
#define MODS_BTN_GRID_Y      0.0f

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

typedef struct GameState {
    void (__cdecl *enter)(void);
    void (__cdecl *update)(void);
    void (__cdecl *render)(void);
    void (__cdecl *leave)(void);
} GameState;

typedef enum RowKind {
    ROW_NONE = 0,
    ROW_MOD_HEADER,
    ROW_DIVIDER,
    ROW_CONFIG,
    ROW_BACK,
    ROW_INFO,
} RowKind;

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

typedef struct Detour {
    void* target;
    uint8_t original[16];
    size_t length;
    void* trampoline;
} Detour;

typedef void* (__cdecl *fn_state_current_t)(void);
typedef void* (__cdecl *fn_state_switch_t)(void*);
typedef int   (__cdecl *fn_main_update_with_buttons_t)(int);
typedef void  (__cdecl *fn_void_void_t)(void);
typedef void  (__cdecl *fn_main_cursors_reset_t)(float, float);
typedef void  (__cdecl *fn_main_cursor_spin_t)(int);
typedef void  (__cdecl *fn_main_sprite_batches_draw_t)(void);
typedef void* (__cdecl *fn_menu_button_link_t)(float, float, const char*, void*);
typedef void  (__cdecl *fn_button_set_layout_t)(float, float);
typedef int   (__cdecl *fn_btn_player_filter_t)(void* btn, int event_code);
typedef void  (__cdecl *fn_plot_text_t)(const char*, int);
typedef void  (__cdecl *fn_plot_text_set_shadow_t)(float, float, float, float);
typedef void  (__cdecl *fn_turtle_set_angle_t)(double);
typedef void  (__cdecl *fn_turtle_set_pos_unscaled_t)(double, double);
typedef void  (__cdecl *fn_turtle_set_scale_t)(double, double);
typedef void  (__cdecl *fn_turtle_set_rgb_t)(float, float, float);
typedef void  (__cdecl *fn_turtle_set_rgba_t)(float, float, float, float);
typedef void  (__cdecl *fn_turtle_reset_t)(void);
typedef float (__cdecl *fn_mad_dim_t)(void);

static fn_state_current_t            p_state_current = (fn_state_current_t)(uintptr_t)ADDR_STATE_CURRENT;
static fn_state_current_t            p_state_last = (fn_state_current_t)(uintptr_t)ADDR_STATE_LAST;
static fn_state_switch_t             p_state_switch = (fn_state_switch_t)(uintptr_t)ADDR_STATE_SWITCH;
static fn_main_update_with_buttons_t p_main_update_with_buttons = (fn_main_update_with_buttons_t)(uintptr_t)ADDR_MAIN_UPDATE_WITH_BUTTONS;
static fn_void_void_t                p_menu_common_render = (fn_void_void_t)(uintptr_t)ADDR_MENU_COMMON_RENDER;
static fn_void_void_t                p_main_buttons_start = (fn_void_void_t)(uintptr_t)ADDR_MAIN_BUTTONS_START;
static fn_main_cursors_reset_t       p_main_cursors_reset = (fn_main_cursors_reset_t)(uintptr_t)ADDR_MAIN_CURSORS_RESET;
static fn_main_cursor_spin_t        p_main_cursor_spin = (fn_main_cursor_spin_t)(uintptr_t)ADDR_MAIN_CURSOR_SPIN;
static fn_main_sprite_batches_draw_t p_main_sprite_batches_draw = (fn_main_sprite_batches_draw_t)(uintptr_t)ADDR_MAIN_SPRITE_BATCHES_DRAW;
static fn_menu_button_link_t         p_menu_button_link = (fn_menu_button_link_t)(uintptr_t)ADDR_MENU_BUTTON_LINK;
static fn_button_set_layout_t        p_button_set_layout = (fn_button_set_layout_t)(uintptr_t)ADDR_BUTTON_SET_LAYOUT;
static fn_btn_player_filter_t        p_btn_player_filter = (fn_btn_player_filter_t)(uintptr_t)ADDR_BTN_PLAYER_FILTER;
static fn_plot_text_t                p_plot_text = (fn_plot_text_t)(uintptr_t)ADDR_PLOT_TEXT;
static fn_plot_text_set_shadow_t     p_plot_text_set_shadow = (fn_plot_text_set_shadow_t)(uintptr_t)ADDR_PLOT_TEXT_SET_SHADOW;
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
static fn_void_void_t                p_options_enter_trampoline = NULL;
static fn_void_void_t                p_options_enter_paused_trampoline = NULL;

static Detour g_options_enter_detour;
static Detour g_options_enter_paused_detour;

static MenuRow g_rows[MAX_MENU_ROWS];
static int g_row_count = 0;
static int g_selected_row = -1;
static int g_scroll_row = 0;

static int g_capture_active = 0;
static int g_capture_mod = -1;
static int g_capture_cfg = -1;
static char g_capture_buf[CAPTURE_BUF_SIZE];
static float g_ui_scale = 1.0f;

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
static void mods_calc_layout(ModsLayout* L);

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
    if (idx < 0 || idx > 1 || !g_main_cursors) return;
    volatile MainCursor* c = (volatile MainCursor*)((uintptr_t)g_main_cursors + (uintptr_t)(idx * (int)sizeof(MainCursor)));
    c->x = x;
    c->y = y;
    c->tx = x;
    c->ty = y;
}

static void mods_cursor_on_selection_changed(void) {
    g_cursor_bump = 7;
    if (p_main_cursor_spin) {
        p_main_cursor_spin(0);
        p_main_cursor_spin(1);
    }
}

static void mods_cursor_tick(void) {
    if (!is_mods_state_active()) return;
    if (g_selected_row < 0 || g_selected_row >= g_row_count) return;

    ModsLayout L;
    mods_calc_layout(&L);
    g_ui_scale = L.text_scale;

    // Determine target position from selected row.
    float row_y = L.list_top + ((float)(g_selected_row - g_scroll_row) * L.row_h) + (L.row_h * 0.55f);

    // Place swords just outside the content region so they don't clip into text.
    float left_x  = L.left  + (44.0f * L.ui);
    float right_x = L.right + (30.0f * L.ui);
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

    // Small outward "pop" when selection changes.
    if (g_cursor_bump > 0) {
        float pop = (float)g_cursor_bump * (1.2f * L.ui);
        write_main_cursor_pos(0, g_cursor_x[0] - pop, g_cursor_y[0]);
        write_main_cursor_pos(1, g_cursor_x[1] + pop, g_cursor_y[1]);
        g_cursor_bump--;
    } else {
        write_main_cursor_pos(0, g_cursor_x[0], g_cursor_y[0]);
        write_main_cursor_pos(1, g_cursor_x[1], g_cursor_y[1]);
    }
}


static void* g_mods_return_state = (void*)(uintptr_t)ADDR_OPTIONS_STATE;

static void __cdecl mods_enter(void);
static void __cdecl mods_update(void);
static void __cdecl mods_render(void);
static void __cdecl mods_leave(void);
static void __cdecl mods_entry_enter(void);
static void __cdecl mods_entry_update(void);
static void __cdecl mods_entry_render(void);
static void __cdecl mods_entry_leave(void);

static GameState g_mods_state = {
    mods_enter,
    mods_update,
    mods_render,
    mods_leave,
};

static GameState g_mods_entry_state = {
    mods_entry_enter,
    mods_entry_update,
    mods_entry_render,
    mods_entry_leave,
};

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

static int str_bool_true(const char* s) {
    if (!s) return 0;
    if (_stricmp(s, "1") == 0) return 1;
    if (_stricmp(s, "true") == 0) return 1;
    if (_stricmp(s, "yes") == 0) return 1;
    if (_stricmp(s, "on") == 0) return 1;
    return 0;
}

static int is_mods_state_active(void) {
    return p_state_current && (p_state_current() == (void*)&g_mods_state);
}

static float approx_text_width(const char* text, float scale) {
    if (!text) return 0.0f;
    // font8x8 atlas is 145x145 (16x16 cells with 1px gutters):
    // effective advance is 9px per glyph at scale=1.
    return (float)strlen(text) * 9.0f * scale;
}

static void mods_restore_render_state(void) {
    // Reset full turtle state (including alpha) to avoid leaking text render
    // state into sprite/glow drawing used by the rest of the UI.
    if (p_turtle_reset) {
        p_turtle_reset();
        return;
    }
    p_turtle_set_angle(0.0);
    p_turtle_set_scale(1.0, 1.0);
    if (p_turtle_set_rgba) {
        p_turtle_set_rgba(1.0f, 1.0f, 1.0f, 1.0f);
    } else {
        p_turtle_set_rgb(1.0f, 1.0f, 1.0f);
    }
}

static void draw_text_scaled_mode(float x, float y, float scale, float r, float g, float b, const char* text, int mode) {
    if (!text || !p_plot_text) return;
    p_turtle_set_angle(0.0);
    p_turtle_set_scale((double)scale, (double)scale);
    p_turtle_set_rgb(r, g, b);
    p_turtle_set_pos_unscaled((double)x, (double)y);
    p_plot_text(text, mode);
}

static void draw_text_scaled(float x, float y, float scale, float r, float g, float b, const char* text) {
    draw_text_scaled_mode(x, y, scale, r, g, b, text, 1);
}

static void draw_text(float x, float y, float r, float g, float b, const char* text) {
    draw_text_scaled(x, y, g_ui_scale, r, g, b, text);
}

static void draw_text_centered_scaled(float cx, float y, float scale, float r, float g, float b, const char* text) {
    // plot_text mode=1 is already centered around turtle x in the base UI.
    draw_text_scaled_mode(cx, y, scale, r, g, b, text, 1);
}

static void draw_text_right_scaled(float right_x, float y, float scale, float r, float g, float b, const char* text) {
    float w = approx_text_width(text, scale);
    draw_text_scaled(right_x - w, y, scale, r, g, b, text);
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

static void rebuild_rows(void) {
    RowKey keep = selected_key();
    rows_clear();

    int mod_count = lua_manager_get_mod_count();
    if (mod_count <= 0) {
        rows_add(ROW_INFO, 0, -1, -1, "No mods detected in mods/", "");
        rows_add(ROW_DIVIDER, 0, -1, -1, "", "");
    }

    for (int mi = 0; mi < mod_count; mi++) {
        char header[128];
        char version[64];
        const char* name = lua_manager_get_mod_name(mi);
        const char* id = lua_manager_get_mod_id(mi);
        const char* ver = lua_manager_get_mod_version(mi);

        if (!name || !name[0]) name = (id && id[0]) ? id : "(unnamed mod)";
        if (!ver) ver = "";

        safe_copy(header, sizeof(header), name);
        if (ver[0]) {
            snprintf(version, sizeof(version), "v%s", ver);
        } else {
            version[0] = '\0';
        }

        rows_add(ROW_MOD_HEADER, 0, mi, -1, header, version);
        rows_add(ROW_DIVIDER, 0, mi, -1, "", "");

        int cfg_count = lua_manager_get_mod_config_count(mi);
        if (cfg_count <= 0) {
            rows_add(ROW_INFO, 0, mi, -1, "(no options)", "");
        }

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

            safe_copy(label, sizeof(label), raw_label);

            switch (type) {
                case LUA_CFG_BOOL:
                    safe_copy(value, sizeof(value), str_bool_true(raw_value) ? "ON" : "OFF");
                    break;
                case LUA_CFG_ACTION:
                    safe_copy(value, sizeof(value), "[Run]");
                    break;
                case LUA_CFG_STRING:
                    snprintf(value, sizeof(value), "\"%s\"", raw_value);
                    break;
                default:
                    safe_copy(value, sizeof(value), raw_value);
                    break;
            }

            rows_add(ROW_CONFIG, 1, mi, ci, label, value);
        }

        if (mi != mod_count - 1) {
            rows_add(ROW_INFO, 0, -1, -1, "", "");
        }
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
    g_capture_active = 0;
    g_capture_mod = -1;
    g_capture_cfg = -1;

    void* target = g_mods_return_state;
    if (!target || target == (void*)&g_mods_state) {
        target = (void*)(uintptr_t)ADDR_OPTIONS_STATE;
    }
    p_state_switch(target);
}

static void begin_string_capture(int mod_index, int cfg_index) {
    const char* v = lua_manager_get_mod_config_value_str(mod_index, cfg_index);
    g_capture_active = 1;
    g_capture_mod = mod_index;
    g_capture_cfg = cfg_index;
    safe_copy(g_capture_buf, sizeof(g_capture_buf), v ? v : "");
}

static void apply_adjustment_on_selected(int delta) {
    if (g_selected_row < 0 || g_selected_row >= g_row_count) return;

    MenuRow* row = &g_rows[g_selected_row];
    if (row->kind != ROW_CONFIG) return;

    int type = lua_manager_get_mod_config_type(row->mod_index, row->cfg_index);
    if (type == LUA_CFG_BOOL) {
        lua_manager_config_toggle_bool(row->mod_index, row->cfg_index);
    } else if (type == LUA_CFG_INT) {
        lua_manager_config_increment_int(row->mod_index, row->cfg_index, delta);
    } else if (type == LUA_CFG_FLOAT) {
        lua_manager_config_increment_float(row->mod_index, row->cfg_index, (double)delta * 0.1);
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

    if (row->kind != ROW_CONFIG) return;

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
    }

    rebuild_rows();
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

int hooks_text_capture_active(void) {
    return g_capture_active;
}

int hooks_text_capture_keydown(int sym, int scancode, int mod) {
    (void)scancode;

    if (!g_capture_active) return 0;
    if (!is_mods_state_active()) {
        g_capture_active = 0;
        return 0;
    }

    if (sym == SDLK_ESCAPE) {
        g_capture_active = 0;
        g_capture_mod = -1;
        g_capture_cfg = -1;
        return 1;
    }

    if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER) {
        lua_manager_config_set_string(g_capture_mod, g_capture_cfg, g_capture_buf);
        g_capture_active = 0;
        g_capture_mod = -1;
        g_capture_cfg = -1;
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

static void render_rows(void) {
    rebuild_rows();

    ModsLayout L;
    mods_calc_layout(&L);
    g_ui_scale = L.text_scale;

    float ui = L.ui;

    // Header + help: center to the *content* region, and position them relative
    // to list_top so it scales with window height.
    float header_cx = (L.left + L.right) * 0.5f;
    float header_y = L.list_top - (70.0f * ui);
    float help_y   = L.list_top - (44.0f * ui);
    if (header_y < 18.0f * ui) header_y = 18.0f * ui;
    if (help_y   < 42.0f * ui) help_y   = 42.0f * ui;

    draw_text_centered_scaled(header_cx, header_y, g_ui_scale * 1.34f,
                              0.95f, 0.95f, 0.95f, "MOD OPTIONS");

    if (g_capture_active) {
        draw_text_centered_scaled(header_cx, help_y, g_ui_scale * 1.00f,
                                  1.00f, 0.85f, 0.35f,
                                  "Editing text (Enter = apply, Esc = cancel)");
    } else {
        draw_text_centered_scaled(header_cx, help_y, g_ui_scale * 0.92f,
                                  0.60f, 0.70f, 0.82f,
                                  "Up/Down to navigate, Enter to toggle/run, Left/Right to adjust, Esc to go back");
    }

    int cap = visible_rows_capacity();
    int start = g_scroll_row;
    if (start < 0) start = 0;
    if (start > g_row_count) start = g_row_count;

    int end = start + cap;
    if (end > g_row_count) end = g_row_count;

    // Dynamic divider string sized to our content width.
    char divider[256];
    {
        float scale = g_ui_scale * 0.86f;
        int count = (int)((L.content_w - (28.0f * ui)) / (4.2f * scale));
        if (count < 8) count = 8;
        if (count > (int)sizeof(divider) - 1) count = (int)sizeof(divider) - 1;
        build_repeat(divider, sizeof(divider), '-', count);
    }

    for (int i = start; i < end; i++) {
        float y = L.list_top + (float)(i - start) * L.row_h;
        MenuRow* row = &g_rows[i];
        int selected = (i == g_selected_row);

        // Spacer rows (blank info rows) - keep them as vertical padding only.
        if (row->kind == ROW_INFO && row->left[0] == '\0' && row->right[0] == '\0') {
            continue;
        }

        // Palette
        float base_r = 0.92f, base_g = 0.92f, base_b = 0.92f;
        float dim_r  = 0.58f, dim_g  = 0.66f, dim_b  = 0.76f;
        float acc_r  = 1.00f, acc_g  = 0.85f, acc_b  = 0.35f;

        switch (row->kind) {
            case ROW_MOD_HEADER: {
                float name_scale = g_ui_scale * 1.22f;
                float ver_scale  = g_ui_scale * 0.98f;

                draw_text_scaled(L.left + (26.0f * ui), y, name_scale,
                                 0.80f, 0.86f, 0.94f,
                                 row->left);

                if (row->right[0]) {
                    draw_text_right_scaled(L.right - (16.0f * ui), y, ver_scale,
                                           dim_r, dim_g, dim_b,
                                           row->right);
                }
                // (no ASCII underline; spacing comes from divider rows)
            } break;

            case ROW_DIVIDER: {
                // Pure spacing row (no ASCII divider)
            } break;

            case ROW_CONFIG: {
                float label_x = L.label_x + (18.0f * ui);
                float right_x = L.right - (16.0f * ui);

                // Label
                if (selected) {
                    draw_text_scaled(label_x, y, g_ui_scale * 1.08f, acc_r, acc_g, acc_b, row->left);
                } else {
                    draw_text_scaled(label_x, y, g_ui_scale * 1.08f, base_r, base_g, base_b, row->left);
                }

                // Value (right aligned)
                float vr = dim_r, vg = dim_g, vb = dim_b;
                int type = lua_manager_get_mod_config_type(row->mod_index, row->cfg_index);

                if (type == LUA_CFG_BOOL) {
                    if (row->right[0] == 'O' && row->right[1] == 'N') { vr = 0.45f; vg = 0.92f; vb = 0.55f; }
                    else { vr = 0.95f; vg = 0.45f; vb = 0.45f; }
                } else if (type == LUA_CFG_ACTION) {
                    vr = 0.78f; vg = 0.84f; vb = 0.95f;
                }

                // If we're capturing text on this row, show live buffer with a caret.
                if (g_capture_active &&
                    row->mod_index == g_capture_mod &&
                    row->cfg_index == g_capture_cfg &&
                    type == LUA_CFG_STRING)
                {
                    char live[CAPTURE_BUF_SIZE + 8];
                    snprintf(live, sizeof(live), "\"%s|\"", g_capture_buf);
                    draw_text_right_scaled(right_x, y, g_ui_scale * 1.00f,
                                           selected ? acc_r : vr,
                                           selected ? acc_g : vg,
                                           selected ? acc_b : vb,
                                           live);
                } else {
                    draw_text_right_scaled(right_x, y, g_ui_scale * 1.00f,
                                           selected ? acc_r : vr,
                                           selected ? acc_g : vg,
                                           selected ? acc_b : vb,
                                           row->right);
                }
            } break;

            case ROW_BACK: {
                float back_scale = g_ui_scale * 1.12f;
                draw_text_centered_scaled(L.center_x, y, back_scale,
                                          selected ? acc_r : base_r,
                                          selected ? acc_g : base_g,
                                          selected ? acc_b : base_b,
                                          row->left);
            } break;

            case ROW_INFO: {
                draw_text_centered_scaled(L.center_x, y, g_ui_scale * 0.96f,
                                          dim_r, dim_g, dim_b,
                                          row->left);
            } break;

            default:
                break;
        }
    }

    // Scroll affordances (subtle)
    if (g_scroll_row > 0) {
        draw_text_right_scaled(L.right - (16.0f * ui), L.list_top - (10.0f * ui),
                               g_ui_scale * 0.96f,
                               0.55f, 0.64f, 0.74f, "^");
    }
    if (g_scroll_row + cap < g_row_count) {
        draw_text_right_scaled(L.right - (16.0f * ui), L.list_bottom - (10.0f * ui),
                               g_ui_scale * 0.96f,
                               0.55f, 0.64f, 0.74f, "v");
    }

    // Position indicator
    if (g_row_count > 0 && g_selected_row >= 0) {
        char pos[64];
        snprintf(pos, sizeof(pos), "%d/%d", g_selected_row + 1, g_row_count);
        draw_text_right_scaled(L.right - (16.0f * ui), 44.0f * ui,
                               g_ui_scale * 0.82f,
                               0.46f, 0.54f, 0.64f, pos);
    }

    mods_restore_render_state();
}


static void __cdecl mods_enter(void) {
    LOG_INFO("MODS: entering custom mods state");
    if (p_main_buttons_start) p_main_buttons_start();

    g_capture_active = 0;
    g_capture_mod = -1;
    g_capture_cfg = -1;

    g_mods_return_state = p_state_last ? p_state_last() : (void*)(uintptr_t)ADDR_OPTIONS_STATE;
    if (!g_mods_return_state || g_mods_return_state == (void*)&g_mods_state) {
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
    p_menu_common_render();
    render_rows();
    // main_draw() handles sprite batch flush; avoid a second flush here.
    mods_restore_render_state();
}

static void __cdecl mods_leave(void) {
    g_capture_active = 0;
    g_capture_mod = -1;
    g_capture_cfg = -1;
    mods_restore_render_state();
}

static void __cdecl mods_entry_enter(void) {
    p_state_switch((void*)&g_mods_state);
}

static void __cdecl mods_entry_update(void) { }
static void __cdecl mods_entry_render(void) { }
static void __cdecl mods_entry_leave(void) { }

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
        jump_back[0] = 0xB8;
        *(uint32_t*)(jump_back + 1) = (uint32_t)back_addr;
        jump_back[5] = 0xFF;
        jump_back[6] = 0xE0;
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

static void add_mods_button_to_options(void) {
    if (!p_menu_button_link) return;
    if (p_button_set_layout) {
        // Match old working entry: back-button-like sizing in the top-right lane.
        p_button_set_layout(5.0f, 5.0f);
    }
    {
        // Old placement: above Player 2 Input.
        void* btn = p_menu_button_link(4.0f, -1.05f, "Mods", (void*)&g_mods_entry_state);
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

void hooks_init(void) {
    static int done = 0;
    if (done) return;
    done = 1;

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

    LOG_INFO("hooks_init: custom MODS menu ready");
}
