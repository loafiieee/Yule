#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    ONLINE_SETTING_MATCH_HUD,
    ONLINE_SETTING_DISCORD_PRESENCE,
} OnlineHubSetting;

typedef enum OnlineHubCaptureKind {
    ONLINE_CAPTURE_NONE = 0,
    ONLINE_CAPTURE_SETTING,
    ONLINE_CAPTURE_ADD_FRIEND,
} OnlineHubCaptureKind;
#define ONLINE_HUB_TEXT_MAX 128
#define ONLINE_DEFAULT_SERVER_PORT 47778
#define ONLINE_TAB_COUNT 4
#define ONLINE_ROW_SETTING 1
#define ONLINE_ROW_ACTION 2
#define ONLINE_ROW_FRIEND 3
#define ONLINE_ACTION_ADD_FRIEND 1
#define ONLINE_ACTION_LOGIN 2
typedef int OnlineHubTab;
static struct { char username[32], password[257], server_host[128], peer_host[128]; uint16_t server_port,local_port; int remember_me; } g_online_cfg;
static struct { int active; } g_online_pending_match,g_online_challenge_map_picker;
static struct { int selectable,kind,id; } g_online_rows[4];
static int g_online_capture_active,g_online_capture_target,g_online_row_count=3;
static OnlineHubCaptureKind g_online_capture_kind;
static char g_online_capture_buf[512],status[256],login_password[257];
static float g_online_mouse_x,g_online_mouse_y;
static int g_online_queue_mode,g_online_context_active,g_online_authed;
static OnlineHubTab g_online_tab;
static int g_online_selected_row,g_online_scroll_row,saves,logins,requests;
static void text_copy(char* dest,size_t n,const char* src) { snprintf(dest,n,"%s",src); }
static void credential_ext_secure_zero(void* data,size_t n) { memset(data,0,n); }
static char* trim_ws(char* value) { while(isspace((unsigned char)*value)) value++; size_t n=strlen(value); while(n && isspace((unsigned char)value[n-1])) value[--n]=0; return value; }
static void online_clear_password_memory(void) { memset(g_online_cfg.password,0,sizeof(g_online_cfg.password)); }
static void online_hub_set_status(const char* text) { text_copy(status,sizeof(status),text); }
static void online_hub_rebuild_rows(void) {}
static void online_hub_clamp_config(void) {}
static void online_hub_save(void) { saves++; }
static int online_delete_remembered_password(const char*a,uint16_t b,const char*c) { (void)a;(void)b;(void)c;assert(0);return 0; }
static int online_load_remembered_password(int a) { (void)a;assert(0);return 0; }
static void online_server_send_username_action(const char*a,const char*b) { assert(!strcmp(a,"friend_request") && b[0]);requests++; }
static int online_hub_parse_host_port(const char*a,char*b,size_t n,uint16_t*p,uint16_t d) { (void)d; if(!a[0])return 0;text_copy(b,n,a);*p=47778;return 1; }
static void online_format_setting_value(OnlineHubSetting s,char*b,size_t n) { if(s==ONLINE_SETTING_LOCAL_PORT)snprintf(b,n,"%u",g_online_cfg.local_port);else text_copy(b,n,g_online_cfg.server_host); }
static int is_online_hub_state_active(void) { return 1; }
static int online_result_rematch_action_at(float x,float y) { (void)x;(void)y;return 0; }
static int online_challenge_toast_action_at(float x,float y) { (void)x;(void)y;return 0; }
static int online_result_close_at(float x,float y) { (void)x;(void)y;return 0; }
static int online_queue_cancel_button_at(float x,float y) { (void)x;(void)y;return 0; }
static int online_context_action_at(float x,float y) { (void)x;(void)y;return 0; }
static int online_tab_at_point(float x,float y) { (void)x;(void)y;return -1; }
static int online_row_at_point(float x,float y) { (void)y;return (int)x; }
static int online_remembered_login_in_progress(void) { return 0; }
static void online_result_rematch_activate(int a) { (void)a;assert(0); }
static void online_challenge_toast_activate(int a) { (void)a;assert(0); }
static void online_result_dismiss(int a) { (void)a;assert(0); }
static void online_server_leave_queue(void) { assert(0); }
static void online_context_activate(int a) { (void)a;assert(0); }
static void online_challenge_map_picker_clear(void) {}
static void online_ensure_scroll_visible(void) {}
static void online_open_friend_context(int a,float x,float y) { (void)a;(void)x;(void)y;assert(0); }
#define ONLINE_TAB_FRIENDS 1
typedef struct { int unused; } OnlineFriend;
static OnlineFriend g_online_friends[4];
static int g_online_context_friend,g_online_friend_count,g_online_context_selected;
static struct { int toast_visible; } g_online_result;
static int online_result_has_live_rematch(void) { return 0; }
static void online_server_disconnect(const char* why) { (void)why;assert(0); }
static void online_context_move_selection(int direction) { (void)direction;assert(0); }
static int online_context_action_for_item(OnlineFriend* f,int item) { (void)f;(void)item;return 0; }
static void online_switch_tab(int direction) { (void)direction;assert(0); }
static int online_visible_rows_capacity(void) { return 3; }
static int online_first_selectable(void) { return 0; }
static int online_last_selectable(void) { return 2; }
static void online_adjust_selected(int direction) { (void)direction;assert(0); }
static void online_activate_selected(void) { assert(0); }
static int online_selected_decline(void) { return 0; }
static void online_open_selected_friend_context(void) { assert(0); }
static void online_hub_close_to_return_state(void) { assert(0); }
static void online_activate_selected_from_mouse(float x);
#include "../build/online_capture_under_test.h"
static void online_activate_selected_from_mouse(float x) {
    (void)x;
    if(g_online_rows[g_online_selected_row].kind==ONLINE_ROW_SETTING) online_focus_selected_text_setting();
    else if(g_online_rows[g_online_selected_row].id==ONLINE_ACTION_LOGIN) { logins++;text_copy(login_password,sizeof(login_password),g_online_cfg.password); }
}
static void reset(void) {
    memset(&g_online_cfg,0,sizeof(g_online_cfg));
    text_copy(g_online_cfg.username,sizeof(g_online_cfg.username),"old");
    text_copy(g_online_cfg.server_host,sizeof(g_online_cfg.server_host),"localhost");
    g_online_cfg.server_port=47778;
    online_clear_capture_state(); saves=logins=requests=0;
    g_online_selected_row=0;
    for(int i=0;i<3;i++)g_online_rows[i].selectable=1;
    g_online_rows[0].kind=g_online_rows[1].kind=ONLINE_ROW_SETTING;
    g_online_rows[0].id=ONLINE_SETTING_USERNAME;g_online_rows[1].id=ONLINE_SETTING_PASSWORD;
    g_online_rows[2].kind=ONLINE_ROW_ACTION;g_online_rows[2].id=ONLINE_ACTION_LOGIN;
}
int main(void) {
    reset(); online_begin_setting_capture(ONLINE_SETTING_USERNAME);
    strcpy(g_online_capture_buf,"new_user");
    assert(hooks_online_hub_mousebutton(1,0,1,1));
    assert(!strcmp(g_online_cfg.username,"new_user"));
    assert(g_online_capture_active && g_online_capture_target==ONLINE_SETTING_PASSWORD);
    strcpy(g_online_capture_buf," secret with spaces ");
    assert(hooks_online_hub_mousebutton(2,0,1,1));
    assert(logins==1 && !strcmp(login_password," secret with spaces ") && saves==2);
    assert(!g_online_capture_active);
    for(size_t i=0;i<sizeof(g_online_capture_buf);i++)assert(g_online_capture_buf[i]==0);
    reset(); online_begin_setting_capture(ONLINE_SETTING_USERNAME);strcpy(g_online_capture_buf,"draft");
    hooks_online_hub_mousebutton(0,0,1,1);
    assert(g_online_capture_active && !strcmp(g_online_capture_buf,"draft") && saves==0);
    online_cancel_capture();assert(!strcmp(g_online_cfg.username,"old") && !g_online_capture_active);
    reset();g_online_rows[0].id=ONLINE_SETTING_LOCAL_PORT;online_begin_setting_capture(ONLINE_SETTING_LOCAL_PORT);
    strcpy(g_online_capture_buf,"65536");hooks_online_hub_mousebutton(2,0,1,1);
    assert(g_online_capture_active && !strcmp(g_online_capture_buf,"65536") && saves==0 && logins==0);
    assert(strstr(status,"UDP port"));
    strcpy(g_online_capture_buf,"1234");hooks_online_hub_mousebutton(-1,0,1,1);
    assert(!g_online_capture_active && g_online_cfg.local_port==1234 && saves==1);
    reset();g_online_capture_active=1;g_online_capture_kind=ONLINE_CAPTURE_ADD_FRIEND;strcpy(g_online_capture_buf,"friend");
    hooks_online_hub_mousebutton(-1,0,1,1);assert(requests==0 && !g_online_capture_active);
    g_online_rows[2].id=ONLINE_ACTION_ADD_FRIEND;g_online_capture_active=1;g_online_capture_kind=ONLINE_CAPTURE_ADD_FRIEND;
    strcpy(g_online_capture_buf,"friend");hooks_online_hub_mousebutton(2,0,1,1);assert(requests==1 && !g_online_capture_active);
    reset();online_begin_setting_capture(ONLINE_SETTING_USERNAME);strcpy(g_online_capture_buf,"tab_user");
    assert(test_capture_keydown(SDLK_TAB,0));
    assert(!strcmp(g_online_cfg.username,"tab_user") && g_online_selected_row==1 && g_online_capture_target==ONLINE_SETTING_PASSWORD);
    strcpy(g_online_capture_buf,"tab_password");assert(test_capture_keydown(SDLK_TAB,KMOD_SHIFT));
    assert(!strcmp(g_online_cfg.password,"tab_password") && g_online_selected_row==0 && g_online_capture_target==ONLINE_SETTING_USERNAME);
    strcpy(g_online_capture_buf,"cancelled");assert(test_capture_keydown(SDLK_ESCAPE,0));
    assert(!strcmp(g_online_cfg.username,"tab_user") && !g_online_capture_active);
    reset();g_online_rows[0].id=ONLINE_SETTING_LOCAL_PORT;online_begin_setting_capture(ONLINE_SETTING_LOCAL_PORT);
    strcpy(g_online_capture_buf,"not_a_port");assert(test_capture_keydown(SDLK_TAB,0));
    assert(g_online_selected_row==0 && g_online_capture_active && saves==0);
    reset();g_online_selected_row=2;g_online_rows[2].kind=ONLINE_ROW_SETTING;g_online_rows[2].id=ONLINE_SETTING_REMEMBER_ME;
    assert(test_capture_keydown(SDLK_TAB,KMOD_SHIFT));
    assert(g_online_selected_row==1 && g_online_capture_active && g_online_capture_target==ONLINE_SETTING_PASSWORD);
    puts("online hub commit-on-blur production handler tests: OK");return 0;
}
