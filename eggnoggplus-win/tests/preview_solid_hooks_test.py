"""Guarded tests of the actual hooks with native engine boundaries mocked.

Uses real entity geometry and snapshot code. No game is launched. The mocks
reproduce the recovered engine's movement -> logic order, collision flag reset,
previous-position stash, and tile-only corpse respawn gate.
"""
from pathlib import Path
import re
import subprocess
import sys

from prematch_net_test import ROOT, find_gcc, child_environment, assert_runtime_dlls


def extract(source, marker):
    start = source.index(marker)
    brace = source.index("{", start)
    depth = 0
    for end in range(brace, len(source)):
        depth += (source[end] == "{") - (source[end] == "}")
        if depth == 0:
            return source[start:end + 1]
    raise RuntimeError(f"Cannot extract {marker}")


def main():
    source = (ROOT / "hooks.c").read_text(encoding="utf-8")
    constants = "\n".join(re.findall(
        r"^#define (?:THING_(?:SIZE|OFS_\w+|TYPE_\w+)|PLAYER_(?:SIZE|OFS_\w+|STATE_\w+|COLLIDE_\w+)|ADDR_(?:HAZARD_ANIM|GAME_STATE))\b[^\n]*",
        source, re.MULTILINE))
    markers = [
        "int hooks_start_native_match(int selector) {",
        "static void __cdecl hooked_state_update(void) {",
        "static int hooks_map_script_kind_for_body(uintptr_t body) {",
        "static int hooks_map_script_read_contact_radius(uintptr_t body,",
        "static int __attribute__((regparm(3))) hooked_check_map_collide(",
        "static unsigned hooks_resolve_native_solid_body(uintptr_t body,",
        "static void __attribute__((regparm(1))) hooked_player_update_logic(void* player) {",
        "static void __attribute__((regparm(1))) hooked_player_update_movement(void* player) {",
        "static void __cdecl hooked_sword_update_movement(void* sword) {",
        "static void __cdecl hooked_hazard_update_movement(void* hazard) {",
    ]
    code = PRELUDE + constants + BOUNDARIES + "\n".join(
        extract(source, marker) for marker in markers) + TESTS
    gcc = find_gcc()
    assert_runtime_dlls(gcc)
    if not any((directory / "lua51.dll").is_file() for directory in
               (ROOT, Path(r"C:\msys64\mingw32\bin"), Path(r"C:\msys64\usr\bin"))):
        raise RuntimeError("lua51.dll is required; refusing to launch test executable")
    env = child_environment(gcc)
    output = ROOT / "build" / "preview_solid_hooks"
    output.mkdir(parents=True, exist_ok=True)
    harness = output / "harness.c"
    harness.write_text(code, encoding="utf-8")
    exe = output / "preview_solid_hooks_test.exe"
    subprocess.run([gcc, "-m32", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-pedantic", "-I", str(ROOT), str(harness), "map_script.c", "entity_world.c",
        "entity_lua.c", "entity_package.c", "entity_package_json.c",
        "content_registry.c", "mod_json.c", "-lluajit-5.1", "-lbcrypt", "-o", str(exe)],
        cwd=ROOT, env=env, check=True, timeout=45)
    subprocess.run([str(exe), *sys.argv[1:]], cwd=ROOT, env=env, check=True, timeout=20)
    entity_exe = output / "map_entity_runtime_test.exe"
    subprocess.run([gcc, "-m32", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-pedantic", "tests/map_entity_runtime_test.c", "map_script.c", "entity_world.c",
        "entity_lua.c", "entity_package.c", "entity_package_json.c",
        "content_registry.c", "mod_json.c", "-lluajit-5.1", "-lbcrypt", "-o", str(entity_exe)],
        cwd=ROOT, env=env, check=True, timeout=45)
    subprocess.run([str(entity_exe)], cwd=ROOT, env=env, check=True, timeout=20)


PRELUDE = r'''
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include "map_script.h"
#define LOG_INFO(...) ((void)0)
'''

BOUNDARIES = r'''
typedef void (__attribute__((regparm(1))) *fn_player_update_movement_t)(void*);
static DWORD g_sim_thread_id;
typedef int (__attribute__((regparm(3))) *fn_check_map_collide_t)(void*,int,int,int);
static int native_terrain,impacts;
static int __attribute__((regparm(3))) mock_check_map_collide(void* body,int xb,int yb,int vertical) {
    float x,y;memcpy(&x,&xb,4);memcpy(&y,&yb,4);
    if(!native_terrain||x<128||x>=144||y<128||y>=144)return 0;
    uint8_t* p=body;
    *(float*)(p+(vertical?THING_OFS_Y:THING_OFS_X))=*(float*)(p+(vertical?THING_OFS_PREV_Y:THING_OFS_PREV_X));
    return 1;
}
static fn_check_map_collide_t p_check_map_collide_trampoline=mock_check_map_collide;
static int __attribute__((regparm(3))) hooked_check_map_collide(void*,int,int,int);
static int native_ticks, logic_ticks, respawns, jump_requested, landing_seen;
static unsigned native_contacts;
static void mock_movement(void* record) {
    uint8_t* p=record;
    p[0xAC]=p[0xAD]; p[0xAD]=(uint8_t)native_contacts;
    *(float*)(p+0x24)+=*(float*)(p+0x34);
    *(float*)(p+0x28)+=*(float*)(p+0x38);
    *(float*)(p+0x2C)=*(float*)(p+0x24);
    *(float*)(p+0x30)=*(float*)(p+0x28);
    native_ticks++;
}
static void mock_logic(void* record) {
    uint8_t* p=record;logic_ticks++;
    if(*(int*)(p+0x18)>0) {
        --*(int*)(p+0x18);
        if(!p[0x10] && !*(int*)(p+0x18)) *(int*)(p+0x18)=1;
        if(!*(int*)(p+0x18)) {
            respawns++;p[0x78]=0;p[0x10]=0;p[0xAD]=0;
            *(float*)(p+0x24)=10;*(float*)(p+0x28)=10;
        }
    } else {
        landing_seen=(p[0xAD]&1)!=0;
        if(jump_requested && landing_seen) *(float*)(p+0x38)=-3;
    }
}
/* Match the original register calling convention even for mocked boundaries. */
static void __attribute__((regparm(1))) mock_player_movement(void* p) {mock_movement(p);}
static void __attribute__((regparm(1))) mock_player_logic(void* p) {mock_logic(p);}
static fn_player_update_movement_t p_player_update_movement_trampoline=mock_player_movement;
static fn_player_update_movement_t p_player_update_logic_trampoline=mock_player_logic;
/* Recovered sword/K native response contract, intentionally different from
 * player movement. Collision flags feed the native impulse/spin/impact path. */
static int projectile_probe(uint8_t* p,float x,float y,int vertical) {
    int xb,yb;memcpy(&xb,&x,4);memcpy(&yb,&y,4);return hooked_check_map_collide(p,xb,yb,vertical);
}
static void mock_projectile_movement(void* body) {
    uint8_t* p=body;int sword=p[THING_OFS_TYPE]==THING_TYPE_SWORD,bounce=0;
    float restitution=sword?-0.5f:-0.125f;
    if(sword)*(float*)(p+0xC0)+=*(float*)(p+0xC8);
    if(!p[0x13])*(float*)(p+THING_OFS_VY)+=0.075f;
    *(float*)(p+THING_OFS_X)+=*(float*)(p+THING_OFS_VX);
    int colx=projectile_probe(p,*(float*)(p+THING_OFS_X),*(float*)(p+THING_OFS_Y)-6,0);
    *(float*)(p+THING_OFS_Y)+=*(float*)(p+THING_OFS_VY);
    int coly=projectile_probe(p,*(float*)(p+THING_OFS_X),*(float*)(p+THING_OFS_Y)-6,1);
    if(colx){bounce=fabsf(*(float*)(p+THING_OFS_VX))>0.3f;*(float*)(p+THING_OFS_X)=*(float*)(p+THING_OFS_PREV_X);*(float*)(p+THING_OFS_VX)*=restitution;}
    if(coly){
        if(fabsf(*(float*)(p+THING_OFS_VY))>0.3f){*(float*)(p+THING_OFS_VY)*=restitution;bounce=1;}
        else *(float*)(p+THING_OFS_VY)=0;
        *(float*)(p+THING_OFS_Y)=*(float*)(p+THING_OFS_PREV_Y);*(float*)(p+THING_OFS_VX)*=0.75f;
        if(sword){*(float*)(p+0xC8)*=-0.5f;if(p[0x88])*(float*)(p+0xC0)=fmodf(*(float*)(p+0xC0),360)*0.5f;}
    }
    *(float*)(p+THING_OFS_PREV_X)=*(float*)(p+THING_OFS_X);*(float*)(p+THING_OFS_PREV_Y)=*(float*)(p+THING_OFS_Y);
    if(bounce)impacts++;
    native_ticks++;
}
static void (*p_sword_update_movement_trampoline)(void*)=mock_projectile_movement;
static void (*p_hazard_update_movement_trampoline)(void*)=mock_projectile_movement;
static void hooks_remove_thing_below_authored_room(void* p) {(void)p;}

static int selector, variable_room;
static int *g_hook_map_selector=&selector;
static LONG g_variable_room_player_query_room,g_variable_room_local_movement;
static uint32_t g_variable_room_movement_tiles[128*64],tiles[32*16];
static uintptr_t tile_pointer=(uintptr_t)tiles;
static int tile_width=32,tile_height=16,active_room,room_px=256;
static int native_w=32,native_h=16,native_room_w=16;
static uintptr_t *g_tilemap_data_ptr=&tile_pointer,*g_game_leader;
static int *g_tilemap_width=&tile_width,*g_tilemap_height=&tile_height;
static int *g_game_active_room=&active_room,*g_room_pixel_width=&room_px;
static int *g_native_map_w=&native_w,*g_native_map_h=&native_h,*g_native_room_w=&native_room_w;
static int custom_maps_variable_room_at(int s,float x,float y,int* room,int* sx,int* sy,int* w,int* h) {
    (void)s;(void)x;(void)y;
    if(!variable_room)return -1;
    *room=0;*sx=32;*sy=64;*w=256;*h=128;return 1;
}
static int custom_maps_variable_room_bounds_2d_for_index(int s,int r,int* x,int* y,int* w,int* h) {
    (void)s;(void)r;(void)x;(void)y;(void)w;(void)h;return -1;
}
static int custom_maps_variable_room_bounds(int s,float x,int* r,int* sx,int* w,int* h) {
    (void)s;(void)x;(void)r;(void)sx;(void)w;(void)h;return -1;
}
static int custom_maps_resolve_room_transition(int s,int r,float ox,float oy,float x,float y,int* d,int* c) {
    (void)s;(void)r;(void)ox;(void)oy;(void)x;(void)y;(void)d;(void)c;return 0;
}
static int hooks_room_connection_allows(uintptr_t p,int c,int* f) {(void)p;(void)c;(void)f;return 1;}
static int hooks_room_graph_has_go_player(void) {return 0;}

static struct {int active;} g_online_pending_match;
static int net_active,reset_calls,dispatch_calls,state_value,pending_preview,preview_ready;
static int ggpo_net_active(void) {return net_active;}
static void online_clear_waterfall_audio_state(const char* reason) {(void)reason;}
static void reset(void) {reset_calls++;}
static void switch_state(void* p) {state_value=(p==(void*)(uintptr_t)ADDR_GAME_STATE)?2:1;}
static void (*p_game_reset)(void)=reset;
static void (*p_state_switch)(void*)=switch_state;
int hooks_start_native_match(int);
static int online_launch_pump(void) {
    if(!pending_preview||!preview_ready||net_active||g_online_pending_match.active)return 0;
    if(!hooks_start_native_match(21))return 0;
    pending_preview=0;return 1;
}
static void dispatch(void) {dispatch_calls++;}
static void (*p_state_update_trampoline)(void)=dispatch;
'''

TESTS = r'''
static char error[384];
static int high_seas_layout;
static void activate(const char* package,const char* script) {
    MapScriptDefinition definition={0};definition.script_id=123;
    definition.source=script;definition.source_len=strlen(script);
    if (high_seas_layout) {
        const char* rooms[]={"center","outer_1","outer_2","outer_3","outer_4","outer_5","outer_6"};
        definition.entity_layout.count=7;
        for(unsigned i=0;i<7;++i)definition.entity_layout.rooms[i]=rooms[i];
    }
    if (!map_script_activate_content(&definition,NULL,package,strlen(package),error,sizeof(error))) {
        fprintf(stderr,"map activation failed: %s\n",error);abort();
    }
}
static void body_init(uint8_t* p,int kind,float x,float y,float vx,float vy) {
    memset(p,0,THING_SIZE);p[THING_OFS_ACTIVE]=1;
    p[THING_OFS_TYPE]=kind==MAP_SCRIPT_OBJECT_SWORD?THING_TYPE_SWORD:
        kind==MAP_SCRIPT_OBJECT_HAZARD?THING_TYPE_HAZARD:THING_TYPE_PLAYER;
    p[PLAYER_OFS_STATE_ID]=kind==MAP_SCRIPT_OBJECT_DEAD_BODY?PLAYER_STATE_DEAD_BODY:0;
    *(float*)(p+THING_OFS_X)=x;*(float*)(p+THING_OFS_Y)=y;
    *(float*)(p+THING_OFS_PREV_X)=x;*(float*)(p+THING_OFS_PREV_Y)=y;
    *(float*)(p+THING_OFS_VX)=vx;*(float*)(p+THING_OFS_VY)=vy;
    *(float*)(p+THING_OFS_CONTACT_RADIUS)=kind==MAP_SCRIPT_OBJECT_SWORD?4:kind==MAP_SCRIPT_OBJECT_HAZARD?0:6;
    *(uint32_t*)(p+THING_OFS_UPDATE_FN)=ADDR_HAZARD_ANIM;
    jump_requested=0;native_contacts=0;landing_seen=0;
}
static void tick(uint8_t* p) {hooked_player_update_movement(p);hooked_player_update_logic(p);}
static void test_preview(void) {
    for(int old=1;old<=2;++old) {
        state_value=old;pending_preview=1;preview_ready=0;
        int before=dispatch_calls;hooked_state_update();
        assert(dispatch_calls==before+1&&state_value==old&&reset_calls==old-1);
        preview_ready=1;before=dispatch_calls;hooked_state_update();
        assert(state_value==2&&reset_calls==old&&dispatch_calls==before&&selector==21);
        hooked_state_update();assert(dispatch_calls==before+1);
    }
    pending_preview=preview_ready=1;net_active=1;
    int before=reset_calls;hooked_state_update();assert(before==reset_calls);
    assert(!hooks_start_native_match(99));net_active=0;g_online_pending_match.active=1;
    hooked_state_update();assert(before==reset_calls);assert(!hooks_start_native_match(99));
    g_online_pending_match.active=0;pending_preview=0;
    puts("PASS: preview dispatch from menu and warm GAME; no old callback; online guard");
}
static void test_solids(void) {
    const char* package="{\"schema\":1,\"capacity\":8,\"types\":[{\"key\":\"test:plank\",\"regions\":[{\"id\":1,\"role\":\"solid\",\"layer\":1,\"mask\":1,\"x\":-8,\"y\":-8,\"width\":16,\"height\":16}]}],\"placements\":[{\"name\":\"floor\",\"type\":\"test:plank\",\"x\":136,\"y\":136}]}";
    activate(package,"-- solid regression\n");uint8_t p[THING_SIZE],initial[THING_SIZE],first[THING_SIZE];
    for(variable_room=0;variable_room<=1;++variable_room) {
        body_init(p,MAP_SCRIPT_OBJECT_PLAYER,136,100,0,60);tick(p);
        assert(*(float*)(p+THING_OFS_Y)==122&&*(float*)(p+THING_OFS_PREV_Y)==122);
        assert(*(float*)(p+THING_OFS_VY)==0&&landing_seen&&(p[PLAYER_OFS_COLLISION_FLAGS]&1));
        jump_requested=1;tick(p);assert(landing_seen&&*(float*)(p+THING_OFS_VY)==-3);
        jump_requested=0;tick(p);assert(!landing_seen&&*(float*)(p+THING_OFS_Y)==119);
        body_init(p,MAP_SCRIPT_OBJECT_DEAD_BODY,136,122,0,0);
        *(int*)(p+PLAYER_OFS_RESPAWN_TIMER)=2;tick(p);
        assert(*(int*)(p+PLAYER_OFS_RESPAWN_TIMER)==1&&!p[PLAYER_OFS_RESPAWN_UNGROUNDED]);
        int before=respawns;tick(p);assert(respawns==before+1&&p[PLAYER_OFS_STATE_ID]!=PLAYER_STATE_DEAD_BODY);
        assert(!p[PLAYER_OFS_RESPAWN_UNGROUNDED]);
        body_init(p,MAP_SCRIPT_OBJECT_DEAD_BODY,136,100,0,0);
        *(int*)(p+PLAYER_OFS_RESPAWN_TIMER)=1;tick(p);
        assert(*(int*)(p+PLAYER_OFS_RESPAWN_TIMER)==1&&!p[PLAYER_OFS_RESPAWN_UNGROUNDED]);
        body_init(p,MAP_SCRIPT_OBJECT_DEAD_BODY,136,122,0,0);
        p[PLAYER_OFS_RESPAWN_UNGROUNDED]=1;*(int*)(p+PLAYER_OFS_RESPAWN_TIMER)=2;tick(p);
        assert(p[PLAYER_OFS_RESPAWN_UNGROUNDED]==1); // preserve native water/pit allowance
    }
    variable_room=0;
    body_init(p,MAP_SCRIPT_OBJECT_PLAYER,100,136,60,0);native_contacts=2;tick(p);
    assert(*(float*)(p+THING_OFS_X)==122&&*(float*)(p+THING_OFS_VX)==-15&&p[PLAYER_OFS_COLLISION_FLAGS]==6);
    body_init(p,MAP_SCRIPT_OBJECT_PLAYER,136,170,0,-60);tick(p);
    assert(*(float*)(p+THING_OFS_Y)==150&&*(float*)(p+THING_OFS_VY)==30&&p[PLAYER_OFS_COLLISION_FLAGS]==2);
    body_init(p,MAP_SCRIPT_OBJECT_DEAD_BODY,136,100,0,3);
    *(float*)(p+THING_OFS_Y)=140; // native movement crossed a floor
    assert(hooks_resolve_native_solid_body((uintptr_t)p,136,100)==1);
    assert(*(float*)(p+THING_OFS_VY)==-1.5f);
    // An earlier native ceiling response must not be reflected a second time.
    body_init(p,MAP_SCRIPT_OBJECT_PLAYER,136,160,0,1.5f);p[PLAYER_OFS_COLLISION_FLAGS]=2;
    *(float*)(p+THING_OFS_Y)=145;assert(hooks_resolve_native_solid_body((uintptr_t)p,136,160)==2);
    assert(*(float*)(p+THING_OFS_VY)==1.5f);
    const int projectile_kinds[]={MAP_SCRIPT_OBJECT_SWORD,MAP_SCRIPT_OBJECT_HAZARD};
    for(unsigned k=0;k<2;++k) {
        int kind=projectile_kinds[k];
        float solid_radius=kind==MAP_SCRIPT_OBJECT_SWORD?4:6;
        const float cases[][4]={{136,132,2,3},{126,140,3,0},{136,152,0,-3},{136,133.9f,2,0.1f}};
        for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
            body_init(initial,kind,cases[i][0],cases[i][1],cases[i][2],cases[i][3]);
            initial[0x88]=1;*(float*)(initial+0xC0)=45;*(float*)(initial+0xC8)=2;
            memcpy(p,initial,sizeof(p));int before=impacts;native_terrain=1;
            mock_projectile_movement(p);int native_impacts=impacts-before;memcpy(first,p,sizeof(p));
            memcpy(p,initial,sizeof(p));native_terrain=0;before=impacts;
            /* Native terrain uses y-6 and a point. For actual entity geometry,
             * approach the equivalent face with the centered body footprint. */
            if(i==1)*(float*)(p+THING_OFS_X)=*(float*)(p+THING_OFS_PREV_X)=128-solid_radius-2;
            else *(float*)(p+THING_OFS_Y)=*(float*)(p+THING_OFS_PREV_Y)=
                i==2?144+solid_radius+2:128-solid_radius-(i==3?0.1f:2);
            if(kind==MAP_SCRIPT_OBJECT_SWORD)hooked_sword_update_movement(p);else hooked_hazard_update_movement(p);
            assert(*(float*)(first+THING_OFS_VX)==*(float*)(p+THING_OFS_VX));
            assert(*(float*)(first+THING_OFS_VY)==*(float*)(p+THING_OFS_VY));
            assert(*(float*)(first+0xC0)==*(float*)(p+0xC0)&&*(float*)(first+0xC8)==*(float*)(p+0xC8));
            assert(impacts-before==native_impacts);
            assert(*(float*)(p+(i==1?THING_OFS_X:THING_OFS_Y))==
                (i==2?144+solid_radius:128-solid_radius));
        }
        native_terrain=0;
        body_init(p,kind,136,100,0,60);int impact_before=impacts;
        if(kind==MAP_SCRIPT_OBJECT_SWORD)hooked_sword_update_movement(p);else hooked_hazard_update_movement(p);
        assert(*(float*)(p+THING_OFS_Y)==128-solid_radius&&*(float*)(p+THING_OFS_VY)<0&&impacts==impact_before+1);
        body_init(p,kind,136,127,0,0);
        if(kind==MAP_SCRIPT_OBJECT_SWORD)hooked_sword_update_movement(p);else hooked_hazard_update_movement(p);
        assert(*(float*)(p+THING_OFS_Y)==128-solid_radius);
        body_init(p,kind,136,128-solid_radius-0.1f,0,0.1f);
        for(int i=0;i<600;i++) {
            if(kind==MAP_SCRIPT_OBJECT_SWORD)hooked_sword_update_movement(p);else hooked_hazard_update_movement(p);
            assert(*(float*)(p+THING_OFS_Y)+solid_radius<=128);
        }
        assert(*(float*)(p+THING_OFS_Y)==128-solid_radius&&*(float*)(p+THING_OFS_VY)==0);
        // Resting bodies must slide off the edge instead of sticking to a floor.
        *(float*)(p+THING_OFS_VX)=20;
        if(kind==MAP_SCRIPT_OBJECT_SWORD)hooked_sword_update_movement(p);else hooked_hazard_update_movement(p);
        assert(*(float*)(p+THING_OFS_Y)>128-solid_radius&&*(float*)(p+THING_OFS_VY)>0);
        // Replaying the identical impact restores identical native body bytes.
        body_init(initial,kind,136,100,2,60);memcpy(p,initial,sizeof(p));
        if(kind==MAP_SCRIPT_OBJECT_SWORD)hooked_sword_update_movement(p);else hooked_hazard_update_movement(p);
        memcpy(first,p,sizeof(p));memcpy(p,initial,sizeof(p));
        if(kind==MAP_SCRIPT_OBJECT_SWORD)hooked_sword_update_movement(p);else hooked_hazard_update_movement(p);
        assert(!memcmp(first,p,sizeof(p)));
    }
    native_terrain=0;
    // No active custom solid or unrecognized projectile changes native queries.
    body_init(p,MAP_SCRIPT_OBJECT_HAZARD,136,100,0,60);*(uint32_t*)(p+THING_OFS_UPDATE_FN)=0;
    hooked_hazard_update_movement(p);assert(*(float*)(p+THING_OFS_Y)>160);
    body_init(p,MAP_SCRIPT_OBJECT_PLAYER,136,100,0,60);*(float*)(p+THING_OFS_CONTACT_RADIUS)=99;tick(p);
    assert(*(float*)(p+THING_OFS_Y)==160&&!landing_seen);
    body_init(initial,MAP_SCRIPT_OBJECT_PLAYER,136,100,0,60);
    size_t size=map_script_content_snapshot_size();void* snapshot=malloc(size);assert(snapshot);
    assert(map_script_content_snapshot_save(snapshot,size,error,sizeof(error)));
    memcpy(p,initial,sizeof(p));tick(p);memcpy(first,p,sizeof(p));
    assert(map_script_content_snapshot_load(snapshot,size,error,sizeof(error)));
    memcpy(p,initial,sizeof(p));tick(p);assert(!memcmp(first,p,sizeof(p)));free(snapshot);
    map_script_deactivate();body_init(p,MAP_SCRIPT_OBJECT_PLAYER,136,100,0,60);tick(p);
    assert(*(float*)(p+THING_OFS_Y)==160&&!landing_seen);
    puts("PASS: live/variable-room landing, jump, corpse respawn, side/ceiling, replay, native bypass");
    puts("PASS: sword/K native impulses, visible surface contact, 600-tick settling, edge departure, replay and fast impacts");
}
static char* read_file(const char* path) {
    FILE* f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
    assert(size>=0&&size<4*1024*1024);char* bytes=calloc((size_t)size+1,1);assert(bytes);
    assert(fread(bytes,1,(size_t)size,f)==(size_t)size);fclose(f);return bytes;
}
int main(int argc,char** argv) {
    test_preview();test_solids();
    if(argc==3) {
        char* package=read_file(argv[1]);char* script=read_file(argv[2]);high_seas_layout=1;activate(package,script);
        uint8_t p[THING_SIZE];body_init(p,MAP_SCRIPT_OBJECT_PLAYER,4*528+136,100,0,60);tick(p);
        assert(*(float*)(p+THING_OFS_Y)==106&&landing_seen);
        body_init(p,MAP_SCRIPT_OBJECT_DEAD_BODY,4*528+136,100,0,60);*(int*)(p+PLAYER_OFS_RESPAWN_TIMER)=1;
        int before=respawns;tick(p);assert(respawns==before+1);
        for(int kind=MAP_SCRIPT_OBJECT_SWORD;kind<=MAP_SCRIPT_OBJECT_HAZARD;++kind) {
            if(kind!=MAP_SCRIPT_OBJECT_SWORD&&kind!=MAP_SCRIPT_OBJECT_HAZARD)continue;
            float radius=kind==MAP_SCRIPT_OBJECT_SWORD?4:6;
            body_init(p,kind,4*528+136,70,0,60);
            if(kind==MAP_SCRIPT_OBJECT_SWORD)hooked_sword_update_movement(p);else hooked_hazard_update_movement(p);
            assert(*(float*)(p+THING_OFS_Y)+radius==112);
        }
        free(package);free(script);map_script_deactivate();puts("PASS: actual High Seas package floor and corpse respawn");
    }
    assert(native_ticks>0&&logic_ticks>0);return 0;
}
'''

if __name__ == "__main__":
    main()
