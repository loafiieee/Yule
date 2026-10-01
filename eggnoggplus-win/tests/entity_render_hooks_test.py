"""Check actual entity/player draw hooks, including native batch selection.

Native sprite_batch_draw renders batch 1 with GL_ONE, GL_ONE. Actor-relative
front/behind placement must use call order while both use alpha batch 0.
"""
import subprocess
from prematch_net_test import ROOT, find_gcc, child_environment, assert_runtime_dlls


def extract(source, marker):
    start = source.index(marker)
    brace = source.index("{", start)
    depth = 0
    for end in range(brace, len(source)):
        depth += (source[end] == "{") - (source[end] == "}")
        if not depth:
            return source[start:end + 1]
    raise RuntimeError("Cannot extract rendering hook")


PRELUDE = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "map_script.h"
#include "content_bridge.h"
#include "content_tiles.h"
static float camera_x=12,camera_y=24;
static float* g_camera_x=&camera_x;
static float* g_camera_y=&camera_y;
static int g_render_hide_players_drawing;
static ContentBridgeDrawOps g_content_draw_ops;
static EntityRenderView entities[4];
static unsigned entity_count=2;
static MapScriptPlayerSprite players[2];
static ContentTileRender recorded[8];
static unsigned count;
static unsigned native_queue[16],custom_queue[16],painted[32];
static unsigned native_count,custom_count,painted_count;
#define THING_OFS_TYPE 1u
#define THING_OFS_ACTIVE 0u
#define THING_TYPE_PLAYER 1u
static uint32_t* p_player_slots;
static int hooks_ptr_accessible(const void* p,size_t size,int write) {
    (void)size;(void)write;return p!=NULL;
}
static void mock_flush(void) {
    for(unsigned i=0;i<native_count;++i)painted[painted_count++]=native_queue[i];
    for(unsigned i=0;i<custom_count;++i)painted[painted_count++]=custom_queue[i];
    native_count=custom_count=0;
}
static void (*p_main_sprite_batches_draw)(void)=mock_flush;
static void __attribute__((regparm(1))) mock_native_draw_things(int layer) {
    native_queue[native_count++]=(unsigned)(100+layer);
}
static void (__attribute__((regparm(1))) *p_entity_draw_things_trampoline)(int)=mock_native_draw_things;
int map_script_entity_render_next(uint32_t* cursor,struct EntityRenderView* out) {
    if(*cursor>=entity_count)return 0;
    *out=entities[(*cursor)++];return 1;
}
int map_script_player_sprite(uint32_t slot,MapScriptPlayerSprite* out) {
    if(slot>=2)return 0;
    *out=players[slot];return 1;
}
int content_bridge_draw_visual(const struct ContentTileRender* view,const ContentBridgeDrawOps* ops) {
    (void)ops;assert(count<8);recorded[count++]=*view;
    if(!strncmp(view->sprite_sheet,"builtin:",8))native_queue[native_count++]=(unsigned)view->sprite_index;
    else custom_queue[custom_count++]=(unsigned)view->sprite_index;
    return 1;
}
'''

TESTS = r'''
int main(void) {
    for(unsigned i=0;i<2;++i) {
        strcpy(entities[i].visual.sheet,"builtin:tiles");entities[i].visual.layer=i;
        entities[i].visual.rgba=0x874521FF;entities[i].visual.scale_x=entities[i].visual.scale_y=256;
        entities[i].x=136*256;entities[i].y=120*256;
        strcpy(players[i].sheet,"builtin:sprites");players[i].layer=i;players[i].visible=1;
        players[i].rgba=i?0xFFFFFFFF:0xFFFFFF80;players[i].scale_x=players[i].scale_y=256;
    }
    draw_custom_entities_for_order(0);assert(count==1);
    draw_custom_player_sprites_for_order(0);assert(count==2);
    draw_custom_entities_for_order(1);assert(count==3);
    draw_custom_player_sprites_for_order(1);assert(count==4);
    for(unsigned i=0;i<count;++i)assert(recorded[i].layer==0);
    assert(recorded[0].tint[3]==1.0f&&recorded[2].tint[3]==1.0f&&recorded[3].tint[3]==1.0f);
    assert(fabsf(recorded[1].tint[3]-128/255.0f)<0.000001f);
    assert(recorded[0].offset_x==124&&recorded[0].offset_y==-96);
    g_render_hide_players_drawing=1;draw_custom_player_sprites_for_order(1);assert(count==4);
    puts("PASS: actual entity/player hooks use alpha batch for both draw orders and retain authored opacity");
    count=native_count=custom_count=painted_count=0;entity_count=4;
    players[0].visible=players[1].visible=0;g_render_hide_players_drawing=0;
    for(unsigned i=0;i<4;++i) {
        entities[i]=entities[0];entities[i].visual.layer=i/2;
        entities[i].sprite=10+i;
        strcpy(entities[i].visual.sheet,(i%2)?"map.png":"builtin:tiles");
    }
    const int layers[]={1,0,-1,-2};
    for(unsigned i=0;i<4;++i)hooked_entity_draw_things(layers[i]);
    mock_flush();
    const unsigned expected[]={10,11,101,100,99,98,12,13};
    assert(painted_count==8&&!memcmp(painted,expected,sizeof(expected)));
    puts("PASS: both native and custom atlas behind sprites paint before actors, foreground sprites after actors");
    return 0;
}
'''


def main():
    source = (ROOT / "hooks.c").read_text(encoding="utf-8")
    code = PRELUDE + extract(source, "static void draw_custom_entities_for_order(")
    code += extract(source, "static void draw_custom_player_sprites_for_order(")
    code += extract(source, "static void __attribute__((regparm(1))) hooked_entity_draw_things(") + TESTS
    gcc = find_gcc()
    assert_runtime_dlls(gcc)
    env = child_environment(gcc)
    build = ROOT / "build" / "entity_render_hooks"
    build.mkdir(parents=True, exist_ok=True)
    harness = build / "harness.c"
    harness.write_text(code, encoding="utf-8")
    exe = build / "entity_render_hooks_test.exe"
    subprocess.run([gcc, "-m32", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-pedantic", "-I", str(ROOT), str(harness), "-o", str(exe)],
        cwd=ROOT, env=env, check=True, timeout=30)
    subprocess.run([str(exe)], cwd=ROOT, env=env, check=True, timeout=10)


if __name__ == "__main__":
    main()
