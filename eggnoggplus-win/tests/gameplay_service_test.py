"""Execute the actual gameplay hook with mocked native boundaries.

Proves service continues during play/stalls and callbacks cannot resume GAME
after a hub transition. Does not run the game or touch native addresses.
"""
import subprocess
from prematch_net_test import ROOT, find_gcc, child_environment, assert_runtime_dlls

source = (ROOT / "hooks.c").read_text(encoding="utf-8")
marker = "static void __cdecl hooked_game_update(int arg0) {"
start = source.index(marker)
brace = source.index("{", start)
depth = 0
for end in range(brace, len(source)):
    depth += (source[end] == "{") - (source[end] == "}")
    if depth == 0:
        body = source[start:end + 1]
        break
else:
    raise RuntimeError("Gameplay hook extraction failed")
prelude = r'''
#include <windows.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#define ADDR_GAME_STATE 1u
#define CONSOLE_LINE_TEXT 384
#define LOG_ERROR(...) ((void)0)
typedef void (__cdecl *fn_game_update_t)(int);
static DWORD g_sim_thread_id;
static int* g_audio_stream_inited; static int* g_sound_setting;
static int services, advances, native_ticks, finishes, blocked, switch_on_service;
static void* state=(void*)1;
static void mock_native(int arg) { (void)arg;native_ticks++; }
static fn_game_update_t p_game_update_trampoline=mock_native,p_game_update=mock_native;
static void* current(void) { return state; }
static void* (*p_state_current)(void)=current;
static struct { int active; } g_online_pending_match,g_online_active_match;
static struct { int attempts,established; } g_online_connect;
static int hooks_set_native_synth_enabled(int enabled) { return enabled; }
static void online_server_update(void) { services++;if(switch_on_service)state=(void*)2; }
static void hooks_finish_game_tick(void) { finishes++; }
static void lua_manager_on_tick(void) {}
static void lua_manager_on_tick_post(void) {}
static int hooks_consume_block_game_tick(void) { return blocked; }
static void run_pending_ggpo_selftest(void) {}
static int ggpo_loopback_active(void) { return 0; }
static int ggpo_local_active(void) { return 0; }
static uint32_t hooks_peek_player_cmds_raw(int player,int mode) { (void)player;(void)mode;return 0; }
static int mock_advance(uint32_t a,uint32_t b,int c,uint32_t* d,char* e,size_t f) {
    (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return 1;
}
#define ggpo_loopback_advance mock_advance
#define ggpo_local_advance mock_advance
static void console_push_line_rgb(const char* text,float r,float g,float b) { (void)text;(void)r;(void)g;(void)b; }
static void ggpo_loopback_stop(void) {}
static void ggpo_local_stop(void) {}
static int ggpo_net_active(void) { return 1; }
static void online_connect_retry_tick(void) {}
static int online_advance_net_gameplay_tick(int arg) { (void)arg;advances++;return 1; }
static int hooks_run_native_game_tick(fn_game_update_t update,int arg,int replay) { (void)replay;update(arg);return 1; }
'''
checks = r'''
int main(void) {
    for(int i=0;i<3600;i++) hooked_game_update(0);
    assert(services==3600 && advances==3600 && native_ticks==0);
    blocked=1;hooked_game_update(0);
    assert(services==3601 && advances==3600 && finishes==1);
    blocked=0;g_online_pending_match.active=1;hooked_game_update(0);
    assert(services==3602 && advances==3600 && finishes==2);
    g_online_pending_match.active=0;switch_on_service=1;hooked_game_update(0);
    assert(services==3603 && advances==3600 && native_ticks==0 && finishes==3);
    puts("Actual gameplay hook: service during play/stalls, prematch and state-switch callback safety PASS");
    return 0;
}
'''
gcc = find_gcc()
assert_runtime_dlls(gcc)
env = child_environment(gcc)
output = ROOT / "build" / "eos_gameplay_service"
output.mkdir(parents=True, exist_ok=True)
fixture = output / "gameplay_service.c"
fixture.write_text(prelude + body + checks, encoding="utf-8")
exe = output / "gameplay_service.exe"
subprocess.run([gcc, "-m32", "-std=c11", "-Wall", "-Wextra", "-Werror", str(fixture), "-o", str(exe)],
               env=env, check=True, timeout=30)
subprocess.run([str(exe)], env=env, check=True, timeout=10)
