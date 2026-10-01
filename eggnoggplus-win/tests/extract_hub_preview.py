"""Render actual custom UI widget code in the guarded hidden-window test."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "hooks.c").read_text(encoding="utf-8")


def block(marker, suffix=""):
    start = SOURCE.index(marker)
    brace = SOURCE.index("{", start)
    depth = 0
    for end in range(brace, len(SOURCE)):
        depth += (SOURCE[end] == "{") - (SOURCE[end] == "}")
        if depth == 0:
            return SOURCE[start:end + 1] + suffix
    raise ValueError(marker)


types = "\n".join(block("typedef " + name, ";") for name in (
    "enum OnlineHubTab", "enum OnlineHubRowKind", "enum OnlineHubAction",
    "enum OnlineHubSetting", "enum OnlineHubCaptureKind", "struct OnlineHubRow", "struct OnlineLayout"
))
# Include the typedef alias after each closing brace.
for name in ("OnlineHubTab", "OnlineHubRowKind", "OnlineHubAction", "OnlineHubSetting",
             "OnlineHubCaptureKind", "OnlineHubRow", "OnlineLayout"):
    start = SOURCE.index("typedef " + ("struct " if name in ("OnlineHubRow", "OnlineLayout") else "enum ") + name)
    raw = block(SOURCE[start:SOURCE.index("{", start)].strip())
    types = types.replace(raw + ";", raw + " " + name + ";")

prelude = r'''
#define BASE_UI_W 1280.0f
#define BASE_UI_H 720.0f
#define ONLINE_HUB_TEXT_MAX 192
#define ONLINE_MATCH_COUNTDOWN_FRAMES 180
#define ONLINE_SERVER_CONNECTED 2
static float preview_width=1280, preview_height=720;
static float preview_w(void) { return preview_width; }
static float preview_h(void) { return preview_height; }
static float (*p_mad_w)(void)=preview_w, (*p_mad_h)(void)=preview_h;
static float g_ui_scale;
static float clampf(float v,float lo,float hi) { return v<lo?lo:v>hi?hi:v; }
static int clampi(int v,int lo,int hi) { return v<lo?lo:v>hi?hi:v; }
static void text_copy(char* to,size_t cap,const char* from) { snprintf(to,cap,"%s",from); }
static void mods_restore_render_state(void) {}
static void draw_text_scaled_mode_alpha(float x,float y,float s,float r,float g,float b,float a,const char* t,int align) {
    assert(ui_text_queue(x,y,s,r,g,b,a,t,align));
}
static void online_hub_draw_rect(float x,float y,float w,float h,float r,float g,float b,float a) {
    glColor4f(r,g,b,a); glBegin(GL_QUADS);
    glVertex2f(x,y);glVertex2f(x+w,y);glVertex2f(x+w,y+h);glVertex2f(x,y+h);glEnd();
}
static void online_hub_draw_border(float x,float y,float w,float h,float thick,float r,float g,float b,float a) {
    glLineWidth(thick);glColor4f(r,g,b,a);glBegin(GL_LINE_LOOP);
    glVertex2f(x,y);glVertex2f(x+w,y);glVertex2f(x+w,y+h);glVertex2f(x,y+h);glEnd();
}
static struct { char username[32]; int remember_me; } g_online_cfg={"Loafiieee",1};
static int g_online_public_elo=1200, g_online_authed=1, g_online_queue_mode;
static int g_online_server_state=ONLINE_SERVER_CONNECTED;
static int g_online_queue_casual_count=2,g_online_queue_competitive_count=4;
static float g_online_mouse_x,g_online_mouse_y;
static int g_online_capture_active,g_online_capture_kind,g_online_capture_target;
static char g_online_capture_buf[192], g_online_status[256]="Ready to play.";
static struct { int active,launch_countdown_frames,prematch_prepared,server_start_reported,server_committed;
    char opponent[32],map_label[64]; } g_online_pending_match;
static struct { int active; } g_online_challenge_map_picker;
static struct { int blocked; char name[32]; } g_online_friends[1];
static int g_online_friend_count,g_online_request_count,g_online_challenge_count;
static OnlineHubTab g_online_tab;
static OnlineHubRow g_online_rows[8];
static int g_online_row_count,g_online_selected_row,g_online_scroll_row;
static void online_hub_rebuild_rows(void) {}
static const char* online_server_state_text(void) { return "Signed in"; }
static int online_friend_can_challenge(void* f) { (void)f;return 0; }
static int online_sent_challenge_pending(const char* f) { (void)f;return 0; }
static int online_login_gateway_active(void) { return !g_online_authed; }
static int online_find_row_by_kind_id(OnlineHubRowKind kind,int id) {
    (void)kind;
    return id==ONLINE_SETTING_USERNAME?0:id==ONLINE_SETTING_PASSWORD?1:id==ONLINE_SETTING_REMEMBER_ME?2:3;
}
static void online_format_setting_value(OnlineHubSetting setting,char* out,size_t size) {
    text_copy(out,size,setting==ONLINE_SETTING_USERNAME?"Loafiieee":"********");
}
static void hooks_ui_draw_line(float x,float y,float x1,float y1,float thickness,float r,float g,float b,float a) {
    glLineWidth(thickness);glColor4f(r,g,b,a);glBegin(GL_LINES);glVertex2f(x,y);glVertex2f(x1,y1);glEnd();
}
static uint32_t hooks_player_colour_tick(void) { return 200; }
static float hooks_triangle01(uint32_t value,uint32_t period) {
    float f=(float)(value%period)/(float)period;return f<.5f?f*2:(1-f)*2;
}
static int ggpo_net_active(void) { return 1; }
static int ggpo_net_connected(void) { return 1; }
static int ggpo_net_link_ready(void) { return 1; }
static int ggpo_net_prematch_ready(void) { return 1; }
static void online_hub_draw_context_menu(void) {}
'''
functions = "\n".join(block(marker) for marker in (
    "static float online_hub_ui_scale(void) {", "static void online_calc_layout(OnlineLayout* L) {",
    "static int online_hub_status_visible(void) {", "static float online_hub_rows_top(",
    "static const char* online_tab_name(OnlineHubTab tab) {",
    "static void online_hub_text_alpha(", "static void online_hub_text(float",
    "static void online_hub_text_center_alpha(", "static void online_hub_text_center(float",
    "static void online_hub_text_right_alpha(", "static void online_hub_text_right(float",
    "static void online_hub_text_emphasis(",
    "static int online_hub_row_primary(", "static int online_hub_row_boxed(",
    "static const char* online_hub_display_label(", "static void online_hub_draw_button_box(",
    "static void online_hub_draw_panel(", "static void online_hub_draw_tabs(",
    "static float online_login_checkbox_height(", "static void online_login_gateway_metrics(",
    "static void online_hub_render_login_gateway(",
    "static void online_hub_draw_wait_dots(", "static float online_ui_pulse01(",
    "static void online_ui_panel(", "static void online_queue_panel_metrics(",
    "static int online_queue_cancel_button_at(", "static void online_hub_render_matchmaking_panel(",
    "static void online_hub_render_match_countdown(", "static void online_hub_render_queue_overlay(",
    "static void online_hub_render_ui(void) {"
))
footer = r'''
static void preview_hub(int width,int height,int tab) {
    preview_width=(float)width;preview_height=(float)height;g_online_tab=(OnlineHubTab)tab;
    memset(g_online_rows,0,sizeof(g_online_rows));
    if (tab==ONLINE_TAB_PLAY) {
        g_online_row_count=4;
        const char* labels[]={"CASUAL QUEUE","COMPETITIVE QUEUE","DISCONNECT","BACK"};
        int ids[]={ONLINE_ACTION_QUEUE_CASUAL,ONLINE_ACTION_QUEUE_COMPETITIVE,ONLINE_ACTION_DISCONNECT_SERVER,0};
        for(int i=0;i<4;i++) {
            g_online_rows[i].kind=i==3?ONLINE_ROW_BACK:ONLINE_ROW_ACTION;
            g_online_rows[i].id=ids[i];strcpy(g_online_rows[i].left,labels[i]);
        }
        strcpy(g_online_rows[0].right,"2 waiting");strcpy(g_online_rows[1].right,"4 waiting");
    } else {
        g_online_row_count=5;
        g_online_selected_row=0;
        const char* labels[]={"Challenge notifications","Discord presence","Advanced settings","Save settings","BACK"};
        const char* values[]={"ON","ON","Show","",""};
        for(int i=0;i<5;i++) {
            g_online_rows[i].kind=i<2?ONLINE_ROW_SETTING:i==4?ONLINE_ROW_BACK:ONLINE_ROW_ACTION;
            strcpy(g_online_rows[i].left,labels[i]);strcpy(g_online_rows[i].right,values[i]);
        }
    }
    online_hub_render_ui();
}
static void preview_online_state(int width,int height,int which) {
    if (which==5) { g_online_authed=0;g_online_status[0]=0; }
    else {
        g_online_queue_mode=which==6?1:0;g_online_pending_match.active=which==7;
        g_online_pending_match.launch_countdown_frames=120;
        g_online_pending_match.prematch_prepared=1;g_online_pending_match.server_committed=1;
        strcpy(g_online_pending_match.opponent,"Opponent");strcpy(g_online_pending_match.map_label,"Classic map");
    }
    preview_hub(width,height,0);
}
'''
output = ROOT / "build" / "ui_text_test" / "hub_preview.h"
output.parent.mkdir(parents=True, exist_ok=True)
extra_types = "\n".join(block("typedef " + kind + " " + name, " " + name + ";")
                        for kind, name in (("enum", "RowKind"), ("enum", "CaptureKind"),
                                           ("struct", "MenuRow"), ("struct", "ModsLayout"),
                                           ("struct", "ConsoleLine")))
extra_prelude = r'''
#define CONSOLE_LINE_TEXT 384
#define CONSOLE_INPUT_BUF 512
#define CAPTURE_BUF_SIZE 512
#define MODS_FOOTER_Y_OFF 48.0f
#define LUA_CFG_BOOL 1
#define LUA_CFG_ACTION 2
#define hooks_ui_fill_rect online_hub_draw_rect
#define hooks_ui_stroke_rect online_hub_draw_border
static int g_capture_active,g_capture_kind,g_capture_mod=-1,g_capture_cfg=-1;
static char g_capture_buf[CAPTURE_BUF_SIZE];
static MenuRow g_rows[8];
static int g_row_count=6,g_selected_row=2,g_scroll_row;
static void rebuild_rows(void) {}
static int lua_manager_mod_bind_has_conflict(int mod,int cfg) { (void)mod;(void)cfg;return 0; }
static int lua_manager_get_mod_config_type(int mod,int cfg) { (void)mod;(void)cfg;return LUA_CFG_BOOL; }
static const char* console_stristr(const char* a,const char* b) { return strstr(a,b); }
static ConsoleLine g_preview_console[4];
static int g_console_line_count=4,g_console_scroll,g_console_cursor;
static int g_console_history;
static void* g_console_return_state;
static char g_console_input[CONSOLE_INPUT_BUF];
static size_t command_history_count(int* history) { (void)history;return 3; }
static const char* state_name_from_ptr(void* state) { (void)state;return "Online"; }
static ConsoleLine* console_line_at_oldest_index(int index) { return &g_preview_console[index]; }
static void draw_text_scaled(float x,float y,float scale,float r,float g,float b,const char* text) {
    assert(ui_text_queue(x,y,scale,r,g,b,1,text,0));
}
static void draw_text_centered_scaled(float x,float y,float scale,float r,float g,float b,const char* text) {
    assert(ui_text_queue(x,y,scale,r,g,b,1,text,1));
}
static void draw_text_right_scaled(float x,float y,float scale,float r,float g,float b,const char* text) {
    assert(ui_text_queue(x,y,scale,r,g,b,1,text,2));
}
static void draw_text_emphasis_mode(float x,float y,float scale,float r,float g,float b,const char* text,int align) {
    assert(ui_text_queue_style(x,y,scale,r,g,b,1,text,align,UI_TEXT_EMPHASIS));
}
'''
extra_functions = "\n".join(block(marker) for marker in (
    "static float calc_ui_scale(void) {", "static void mods_calc_layout(ModsLayout* L) {",
    "static int mods_content_row_count(void) {", "static int mods_back_row_index(void) {",
    "static int visible_rows_capacity(void) {", "static void render_rows(void) {",
    "static void console_draw_rect(float x, float y, float w, float h, float r, float g, float b, float a) {",
    "static void draw_console_text_scaled(float",
    "static void console_render_ui(void) {"
))
extra_footer = r'''
static void preview_custom(int width,int height,int which) {
    preview_width=(float)width;preview_height=(float)height;
    if (which==3) {
        const char* labels[]={"Framework","Console logging","Automatic updates","Music volume","Sound effects","Back"};
        const char* values[]={"","ON","ON","80%","100%",""};
        memset(g_rows,0,sizeof(g_rows));
        for(int i=0;i<6;i++) {
            g_rows[i].kind=i==0?ROW_MOD_HEADER:i==5?ROW_BACK:i<3?ROW_FW_TOGGLE:ROW_FW_VALUE;
            g_rows[i].mod_index=-1;
            strcpy(g_rows[i].left,labels[i]);strcpy(g_rows[i].right,values[i]);
        }
        render_rows();
    } else {
        const char* messages[]={"Yule development console","Transport: EOS / Route: Relay",
            "Session ready. Gameplay RTT: 73 ms.",
            "Long messages wrap inside the panel. Inputs, checksums, packet authentication and replay protection remain in Yule's existing gameplay protocol."};
        for(int i=0;i<4;i++) {
            strcpy(g_preview_console[i].text,messages[i]);
            g_preview_console[i].r=.74f;g_preview_console[i].g=.82f;g_preview_console[i].b=.90f;
        }
        strcpy(g_console_input,"ggpo.net diag");g_console_cursor=(int)strlen(g_console_input);
        console_render_ui();
    }
}
'''
output.write_text(types + prelude + functions + footer +
                  "\n#define CONSOLE_LINE_TEXT 384\n" + extra_types + extra_prelude +
                  extra_functions + extra_footer, encoding="utf-8")
print("Actual hub, mod manager and console renderers extracted for visual verification.")
