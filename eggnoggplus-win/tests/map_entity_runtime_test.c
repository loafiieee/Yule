#include "../map_script.h"
#include "../entity_package.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static const char package[] = "{\"schema\":1,\"capacity\":8,\"types\":[{\"key\":\"demo:orb\",\"regions\":[]}],\"placements\":[{\"name\":\"orb\",\"type\":\"demo:orb\",\"vx\":1}]}";
static char error[384];
static MapScriptDefinition definition(const char* source) {
    MapScriptDefinition d;memset(&d,0,sizeof(d));d.script_id=987;
    d.source=source;d.source_len=strlen(source);return d;
}
static void read_entity(const unsigned char* bytes,size_t size,int32_t x,uint32_t count) {
    EntityPackage* p=entity_package_decode(package,strlen(package),error,sizeof(error));
    EntityValue value;assert(p);
    assert(entity_package_load(p,bytes+MAP_SCRIPT_CONTENT_HEADER_BYTES+sizeof(MapScriptSnapshot),size-MAP_SCRIPT_CONTENT_HEADER_BYTES-sizeof(MapScriptSnapshot)));
    assert(entity_world_read(entity_package_world(p),entity_package_placement(p,"orb"),&value));
    assert(value.x==x && entity_world_count(entity_package_world(p))==count);
    entity_package_free(p);
}
typedef struct PlayerHost { int mode;unsigned reads; } PlayerHost;
static int read_player(void* user,uint32_t slot,MapScriptObjectView* out) {
    PlayerHost* host=user;host->reads++;
    if(host->mode==4) return -1;
    if(slot!=(host->mode==2 ? 1u : 0u)) return 0;
    memset(out,0,sizeof(*out));out->object_id=slot;out->object_kind=MAP_SCRIPT_OBJECT_PLAYER;
    out->x=32;out->y=64;out->vx=0.5f;
    if(host->mode==1) out->object_kind=MAP_SCRIPT_OBJECT_SWORD;
    if(host->mode==3) out->x=NAN;
    return 1;
}
typedef struct VelocityHost { MapScriptObjectView players[2];uint32_t available;int reject,commits; } VelocityHost;
static int velocity_read(void* user,uint32_t slot,MapScriptObjectView* out) {
    VelocityHost* host=user;if(!(host->available&(1u<<slot))) return 0;*out=host->players[slot];return 1;
}
static int velocity_commit(void* user,uint32_t mask,const MapScriptObjectView players[2]) {
    VelocityHost* host=user;host->commits++;
    if(host->reject || (mask&host->available)!=mask) return 0;
    for(uint32_t i=0;i<2;i++) if(mask&(1u<<i)) {host->players[i].vx=players[i].vx;host->players[i].vy=players[i].vy;}
    return 1;
}
static int player_commit(void* user,uint32_t mask,uint32_t defeat,const MapScriptObjectView players[2]) {
    VelocityHost* host=user;
    if(((mask|defeat)&host->available)!=(mask|defeat))return 0;
    if(!velocity_commit(user,mask,players))return 0;
    host->available&=~defeat;return 1;
}
static void velocity_legacy_apply(void* user,const MapScriptObjectView* view) {
    VelocityHost* host=user;if(view->object_id<2) host->players[view->object_id]=*view;
}
static void test_generated_blocks(void) {
    for(int remove=0;remove<3;remove++){
    char source[4096];FILE* file=fopen(remove==2?"tests/fixtures/blocks-object-group.lua":remove?"tests/fixtures/blocks-object-remove.lua":"tests/fixtures/blocks-object-motion.lua","rb");assert(file);
    size_t length=fread(source,1,sizeof(source)-1,file);assert(!ferror(file)&&feof(file));fclose(file);source[length]=0;
    const char* active_package=remove==2?"{\"schema\":1,\"capacity\":8,\"types\":[{\"key\":\"demo:orb\",\"regions\":[]},{\"key\":\"demo:other\",\"regions\":[]}],\"placements\":[{\"name\":\"orb\",\"type\":\"demo:orb\",\"vx\":1},{\"name\":\"other\",\"type\":\"demo:other\",\"vx\":7}]}":package;
    MapScriptDefinition d=definition(source);
    assert(map_script_activate_content(&d,NULL,active_package,strlen(active_package),error,sizeof(error)));
    size_t size=map_script_content_snapshot_size();unsigned char *start=malloc(size),*after=malloc(size),*replay=malloc(size);assert(start&&after&&replay);
    assert(map_script_content_snapshot_save(start,size,error,sizeof(error)));
    for(int i=0;i<5;i++)assert(map_script_dispatch_tick(error,sizeof(error)));
    assert(map_script_content_snapshot_save(after,size,error,sizeof(error)));
    EntityPackage* decoded=entity_package_decode(active_package,strlen(active_package),error,sizeof(error));assert(decoded);
    size_t offset=MAP_SCRIPT_CONTENT_HEADER_BYTES+sizeof(MapScriptSnapshot);
    assert(entity_package_load(decoded,after+offset,size-offset));EntityValue value;
    if(remove==1){assert(entity_world_count(entity_package_world(decoded))==0);}
    else{
    assert(entity_world_read(entity_package_world(decoded),entity_package_placement(decoded,"orb"),&value));
    assert(entity_world_count(entity_package_world(decoded))==(remove==2?2u:6u));
    assert(value.vx==2*256&&(value.flags&ENTITY_FLAG_ANIMATION_PAUSED));
    assert(value.x>0&&value.animation_tick<=1);
    }
    if(remove==2){EntityValue other;assert(entity_world_read(entity_package_world(decoded),entity_package_placement(decoded,"other"),&other));assert(other.vx==7*256&&!(other.flags&ENTITY_FLAG_ANIMATION_PAUSED));}
    entity_package_free(decoded);
    assert(map_script_content_snapshot_load(start,size,error,sizeof(error)));
    for(int i=0;i<5;i++)assert(map_script_dispatch_tick(error,sizeof(error)));
    assert(map_script_content_snapshot_save(replay,size,error,sizeof(error))&&!memcmp(after,replay,size));
    free(start);free(after);free(replay);map_script_deactivate();
    }
}
static void test_generated_player_blocks(void){
    char source[4096];FILE* file=fopen("tests/fixtures/blocks-players.lua","rb");assert(file);
    size_t length=fread(source,1,sizeof(source)-1,file);assert(!ferror(file)&&feof(file));fclose(file);source[length]=0;
    VelocityHost native={0};MapScriptHost host={0};native.available=3;
    for(unsigned i=0;i<2;i++){native.players[i].object_id=i;native.players[i].object_kind=MAP_SCRIPT_OBJECT_PLAYER;native.players[i].vx=(float)i+1;native.players[i].vy=5;}
    host.userdata=&native;host.read_player_fn=velocity_read;host.commit_players_fn=player_commit;
    MapScriptDefinition d=definition(source);assert(map_script_activate_content(&d,&host,package,strlen(package),error,sizeof(error)));
    assert(map_script_dispatch_tick(error,sizeof(error)));
    assert(native.commits==1&&native.players[0].vx==1&&native.players[1].vx==2&&native.players[0].vy==0&&native.players[1].vy==0);
    native.available=2;native.players[1].vy=5;assert(map_script_dispatch_tick(error,sizeof(error)));assert(native.commits==2&&native.players[1].vy==0);
    native.available=0;assert(map_script_dispatch_tick(error,sizeof(error)));assert(native.commits==2);
    map_script_deactivate();
}
static void test_player_variables(void){
    char source[4096];FILE* file=fopen("tests/fixtures/blocks-player-variables.lua","rb");assert(file);
    size_t length=fread(source,1,sizeof(source)-1,file);assert(!ferror(file)&&feof(file));fclose(file);source[length]=0;
    VelocityHost native={0};MapScriptHost host={0};native.available=3;
    for(unsigned i=0;i<2;i++){native.players[i].object_id=i;native.players[i].object_kind=MAP_SCRIPT_OBJECT_PLAYER;}
    host.userdata=&native;host.read_player_fn=velocity_read;host.commit_players_fn=player_commit;
    MapScriptDefinition d=definition(source);assert(map_script_activate_content(&d,&host,package,strlen(package),error,sizeof(error)));
    size_t size=map_script_content_snapshot_size();unsigned char *start=malloc(size),*end=malloc(size),*replay=malloc(size);assert(start&&end&&replay);
    assert(map_script_content_snapshot_save(start,size,error,sizeof(error)));
    for(int pass=0;pass<2;pass++){
        if(pass){assert(map_script_content_snapshot_load(start,size,error,sizeof(error)));native.available=3;for(int i=0;i<2;i++)native.players[i].vy=0;}
        for(int tick=0;tick<5;tick++)assert(map_script_dispatch_tick(error,sizeof(error)));
        assert(native.players[0].vy==5&&native.players[1].vy==10);
        native.available=2;assert(map_script_dispatch_tick(error,sizeof(error)));assert(native.players[0].vy==5&&native.players[1].vy==12);
        assert(map_script_content_snapshot_save(pass?replay:end,size,error,sizeof(error)));
    }
    assert(!memcmp(end,replay,size));free(start);free(end);free(replay);map_script_deactivate();
}
static void test_motion_controls(void){
    char source[4096];FILE* file=fopen("tests/fixtures/object-motion-controls.lua","rb");assert(file);size_t length=fread(source,1,sizeof(source)-1,file);assert(!ferror(file)&&feof(file));fclose(file);source[length]=0;
    MapScriptDefinition d=definition(source);assert(map_script_activate_content(&d,NULL,package,strlen(package),error,sizeof(error)));
    size_t size=map_script_content_snapshot_size(),offset=MAP_SCRIPT_CONTENT_HEADER_BYTES+sizeof(MapScriptSnapshot);unsigned char* start=malloc(size),*end=malloc(size),*replay=malloc(size);assert(start&&end&&replay);
    assert(map_script_content_snapshot_save(start,size,error,sizeof(error)));for(int i=0;i<5;i++)assert(map_script_dispatch_tick(error,sizeof(error)));assert(map_script_content_snapshot_save(end,size,error,sizeof(error)));
    EntityPackage* decoded=entity_package_decode(package,strlen(package),error,sizeof(error));assert(decoded);assert(entity_package_load(decoded,end+offset,size-offset));EntityValue value;assert(entity_world_read(entity_package_world(decoded),entity_package_placement(decoded,"orb"),&value));assert(value.vx==8&&value.vy==256&&value.x==248&&value.y==896);entity_package_free(decoded);
    assert(map_script_content_snapshot_load(start,size,error,sizeof(error)));for(int i=0;i<5;i++)assert(map_script_dispatch_tick(error,sizeof(error)));assert(map_script_content_snapshot_save(replay,size,error,sizeof(error))&&!memcmp(end,replay,size));free(start);free(end);free(replay);map_script_deactivate();
}
static int terrain_fixture(void* user,double x,double y,double width,double height){
    (void)user;return x+width*0.5>64||y+height*0.5>48||y-height*0.5<0;
}
static void test_solid_regions(void){
    const char* json="{\"schema\":1,\"capacity\":8,\"types\":[{\"key\":\"demo:wall\",\"regions\":[{\"id\":1,\"role\":\"solid\",\"layer\":1,\"mask\":1,\"x\":0,\"y\":0,\"width\":16,\"height\":16}]}],\"placements\":[{\"name\":\"wall\",\"type\":\"demo:wall\",\"x\":32,\"y\":32}]}";
    MapScriptDefinition d=definition("map.on_tick(function() assert(#entity.at(32,32)==1) assert(#entity.at(48,32)==0) assert(entity.type(entity.at(40,40)[1])=='demo:wall') assert(entity.solid_box(40,40,1,1)) assert(not entity.solid_box(40,40,1,1,entity.at(40,40)[1])) assert(not entity.solid_box(24,40,16,16)) end)");assert(map_script_activate_content(&d,NULL,json,strlen(json),error,sizeof(error)));
    size_t size=map_script_content_snapshot_size();unsigned char* saved=malloc(size);assert(saved);assert(map_script_content_snapshot_save(saved,size,error,sizeof(error)));
    for(int replay=0;replay<2;replay++){
        if(replay)assert(map_script_content_snapshot_load(saved,size,error,sizeof(error)));
        assert(map_script_dispatch_tick(error,sizeof(error)));
        double x=100,y=40;assert(map_script_sweep_solids(0,40,6,&x,&y)==4&&x==26&&y==40);
        x=0;y=40;assert(map_script_sweep_solids(100,40,6,&x,&y)==8&&x==54);
        x=40;y=100;assert(map_script_sweep_solids(40,0,6,&x,&y)==1&&y==26);
        x=40;y=0;assert(map_script_sweep_solids(40,100,6,&x,&y)==2&&y==54);
        x=100;y=100;assert(map_script_sweep_solids(0,0,6,&x,&y)==1&&x==100&&y==26);
        x=100;y=10;assert(map_script_sweep_solids(0,10,6,&x,&y)==0&&x==100);
        x=40;y=40;assert(map_script_sweep_solids(40,40,6,&x,&y)==1&&y==26);
        x=40;y=26;assert(map_script_sweep_solids(40,26,6,&x,&y)==1&&y==26);
    }
    free(saved);map_script_deactivate();
}
static void test_instance_variables_and_manual_motion(void){
    const char* source="entity.on_spawn('demo:orb',function(h) entity.set(h,{automatic_motion=false}) end) entity.on_update('demo:orb',function(h) local key='o:'..h..':direction' map.state[key]=(map.state[key] or 0)+1 local b=entity.get(h) entity.set(h,{x=b.x+map.state[key]}) if map.tick()==2 then entity.remove(h) end end)";
    MapScriptDefinition d=definition(source);assert(map_script_activate_content(&d,NULL,package,strlen(package),error,sizeof(error)));
    size_t size=map_script_content_snapshot_size(),offset=MAP_SCRIPT_CONTENT_HEADER_BYTES+sizeof(MapScriptSnapshot);unsigned char *start=malloc(size),*end=malloc(size),*replay=malloc(size);assert(start&&end&&replay);
    assert(map_script_content_snapshot_save(start,size,error,sizeof(error)));
    for(int pass=0;pass<2;pass++){
        if(pass)assert(map_script_content_snapshot_load(start,size,error,sizeof(error)));
        for(int tick=0;tick<2;tick++)assert(map_script_dispatch_tick(error,sizeof(error)));
        assert(map_script_content_snapshot_save(replay,size,error,sizeof(error)));
        EntityPackage* decoded=entity_package_decode(package,strlen(package),error,sizeof(error));assert(decoded);assert(entity_package_load(decoded,replay+offset,size-offset));EntityValue value;assert(entity_world_read(entity_package_world(decoded),entity_package_placement(decoded,"orb"),&value));assert(value.x==3*256&&value.vx==256);entity_package_free(decoded);
        assert(map_script_dispatch_tick(error,sizeof(error)));assert(map_script_content_snapshot_save(pass?replay:end,size,error,sizeof(error)));
        MapScriptSnapshot state;memcpy(&state,(pass?replay:end)+MAP_SCRIPT_CONTENT_HEADER_BYTES,sizeof(state));assert(state.state_count==0);
    }
    assert(!memcmp(end,replay,size));free(start);free(end);free(replay);map_script_deactivate();
}
static void test_patrol_blocks(void){
    char source[4096];FILE* file=fopen("tests/fixtures/blocks-patrol.lua","rb");assert(file);size_t length=fread(source,1,sizeof(source)-1,file);assert(!ferror(file)&&feof(file));fclose(file);source[length]=0;
    const char* json="{\"schema\":1,\"capacity\":8,\"types\":[{\"key\":\"demo:orb\",\"regions\":[]}],\"placements\":[{\"name\":\"orb\",\"type\":\"demo:orb\",\"x\":48,\"y\":16,\"vx\":2}]}";
    MapScriptDefinition d=definition(source);MapScriptHost host={0};host.solid_box_fn=terrain_fixture;
    assert(map_script_activate_content(&d,&host,json,strlen(json),error,sizeof(error)));
    size_t size=map_script_content_snapshot_size(),offset=MAP_SCRIPT_CONTENT_HEADER_BYTES+sizeof(MapScriptSnapshot);unsigned char *start=malloc(size),*end=malloc(size),*replay=malloc(size);assert(start&&end&&replay);
    assert(map_script_content_snapshot_save(start,size,error,sizeof(error)));
    for(int pass=0;pass<2;pass++){
        if(pass)assert(map_script_content_snapshot_load(start,size,error,sizeof(error)));
        for(int tick=0;tick<10;tick++)assert(map_script_dispatch_tick(error,sizeof(error)));
        assert(map_script_content_snapshot_save(pass?replay:end,size,error,sizeof(error)));
    }
    assert(!memcmp(end,replay,size));EntityPackage* decoded=entity_package_decode(json,strlen(json),error,sizeof(error));assert(decoded);assert(entity_package_load(decoded,end+offset,size-offset));EntityValue value;assert(entity_world_read(entity_package_world(decoded),entity_package_placement(decoded,"orb"),&value));assert(value.x==44*256&&value.vx==-2*256);entity_package_free(decoded);
    free(start);free(end);free(replay);map_script_deactivate();
}
static int empty_terrain(void* user,double x,double y,double width,double height){(void)user;(void)x;(void)y;(void)width;(void)height;return 0;}
static void test_terrain_motion(int custom){
    char source[8192];FILE* file=fopen("tests/fixtures/object-terrain-motion.lua","rb");assert(file);size_t length=fread(source,1,sizeof(source)-1,file);assert(!ferror(file)&&feof(file));fclose(file);source[length]=0;
    MapScriptDefinition d=definition(source);MapScriptHost host={0};host.solid_box_fn=terrain_fixture;
    const char* terrain_package="{\"schema\":1,\"capacity\":8,\"types\":[{\"key\":\"demo:orb\",\"regions\":[]}],\"placements\":[{\"name\":\"orb\",\"type\":\"demo:orb\",\"x\":16,\"y\":16,\"vx\":32}]}";
    char custom_json[4096];
    if(custom){FILE* json=fopen("tests/fixtures/object-solid-motion.json","rb");assert(json);size_t n=fread(custom_json,1,sizeof(custom_json)-1,json);assert(!ferror(json)&&feof(json));fclose(json);custom_json[n]=0;terrain_package=custom_json;host.solid_box_fn=empty_terrain;}
    assert(map_script_activate_content(&d,&host,terrain_package,strlen(terrain_package),error,sizeof(error)));
    size_t size=map_script_content_snapshot_size(),offset=MAP_SCRIPT_CONTENT_HEADER_BYTES+sizeof(MapScriptSnapshot);unsigned char* start=malloc(size),*end=malloc(size),*replay=malloc(size);assert(start&&end&&replay);
    assert(map_script_content_snapshot_save(start,size,error,sizeof(error)));for(int i=0;i<100;i++)assert(map_script_dispatch_tick(error,sizeof(error)));assert(map_script_content_snapshot_save(end,size,error,sizeof(error)));
    EntityPackage* decoded=entity_package_decode(terrain_package,strlen(terrain_package),error,sizeof(error));assert(decoded);assert(entity_package_load(decoded,end+offset,size-offset));EntityValue value;assert(entity_world_read(entity_package_world(decoded),entity_package_placement(decoded,"orb"),&value));assert(value.x==56*256&&value.y==40*256&&value.vx==0&&value.vy==0);entity_package_free(decoded);
    assert(map_script_content_snapshot_load(start,size,error,sizeof(error)));for(int i=0;i<100;i++)assert(map_script_dispatch_tick(error,sizeof(error)));assert(map_script_content_snapshot_save(replay,size,error,sizeof(error))&&!memcmp(end,replay,size));free(start);free(end);free(replay);map_script_deactivate();
}
int main(void) {
    test_solid_regions();
    test_instance_variables_and_manual_motion();
    test_patrol_blocks();
    test_player_variables();
    test_terrain_motion(0);
    test_terrain_motion(1);
    test_motion_controls();
    test_generated_blocks();
    test_generated_player_blocks();
    MapScriptDefinition d=definition("map.state.orb=entity.list()[1] map.on_tick(function() map.state.x=entity.get(map.state.orb).x end)");
    MapScriptSnapshot plain;unsigned char *initial,*after,*copy,*bad;size_t size,i;
    assert(map_script_activate_content(&d,NULL,package,strlen(package),error,sizeof(error)));
    assert(!map_script_network_admissible(error,sizeof(error)) && strstr(error,"offline-only"));
    assert(!map_script_snapshot_save(&plain,error,sizeof(error)) && strstr(error,"combined"));
    size=map_script_content_snapshot_size();initial=malloc(size);after=malloc(size);copy=malloc(size);bad=malloc(size);
    assert(initial && after && copy && bad);
    assert(map_script_content_snapshot_save(initial,size,error,sizeof(error)));
    read_entity(initial,size,0,1);
    {
        MapScriptDefinition candidate=definition("map.state.private=entity.spawn('demo:orb',{x=5}) entity.on_update('demo:orb',function(h) end)");
        assert(map_script_validate_content(&candidate,package,strlen(package),error,sizeof(error)));
        assert(map_script_content_snapshot_save(copy,size,error,sizeof(error)) && !memcmp(copy,initial,size));
        candidate=definition("entity.spawn('demo:orb',{x=9}) error('validation failure')");
        assert(!map_script_validate_content(&candidate,package,strlen(package),error,sizeof(error)));
        assert(!map_script_validate_content(&d,"{}",2,error,sizeof(error)));
        assert(!map_script_validate_content(&d,NULL,0,error,sizeof(error)));
        assert(map_script_content_snapshot_save(copy,size,error,sizeof(error)) && !memcmp(copy,initial,size));
    }

    assert(map_script_content_snapshot_validate(initial,size,error,sizeof(error)));

    {
        MapScriptDefinition changed=d;
        MapScriptHost host={0};
        MapScriptTileBinding binding={'a',"demo:tile"};
        changed.source="map.state.orb=entity.list()[1]";changed.source_len=strlen(changed.source);
        assert(map_script_activate_content(&changed,NULL,package,strlen(package),error,sizeof(error)));
        assert(map_script_content_snapshot_save(copy,size,error,sizeof(error)));
        assert(!map_script_content_snapshot_load(initial,size,error,sizeof(error)) && strstr(error,"identity"));
        assert(map_script_content_snapshot_save(bad,size,error,sizeof(error)) && !memcmp(copy,bad,size));
        changed=d;changed.instruction_budget=MAP_SCRIPT_DEFAULT_INSTRUCTIONS+1000;
        assert(map_script_activate_content(&changed,NULL,package,strlen(package),error,sizeof(error)));
        assert(!map_script_content_snapshot_load(initial,size,error,sizeof(error)) && strstr(error,"identity"));
        changed=d;changed.memory_limit_bytes=MAP_SCRIPT_DEFAULT_MEMORY_BYTES+1024;
        assert(map_script_activate_content(&changed,NULL,package,strlen(package),error,sizeof(error)));
        assert(!map_script_content_snapshot_load(initial,size,error,sizeof(error)) && strstr(error,"identity"));
        changed=d;changed.bindings=&binding;changed.binding_count=1;
        assert(map_script_activate_content(&changed,NULL,package,strlen(package),error,sizeof(error)));
        assert(!map_script_content_snapshot_load(initial,size,error,sizeof(error)) && strstr(error,"identity"));
        host.rng_seed=123;
        assert(map_script_activate_content(&d,&host,package,strlen(package),error,sizeof(error)));
        assert(!map_script_content_snapshot_load(initial,size,error,sizeof(error)) && strstr(error,"identity"));
        changed=d;changed.memory_limit_bytes=MAP_SCRIPT_DEFAULT_MEMORY_BYTES;
        changed.instruction_budget=MAP_SCRIPT_DEFAULT_INSTRUCTIONS;
        assert(map_script_activate_content(&changed,NULL,package,strlen(package),error,sizeof(error)));
        assert(map_script_content_snapshot_load(initial,size,error,sizeof(error)));
        assert(map_script_content_snapshot_save(copy,size,error,sizeof(error)) && !memcmp(copy,initial,size));
    }
    for(i=0;i<256;i++) assert(map_script_dispatch_tick(error,sizeof(error)));
    assert(map_script_content_snapshot_save(after,size,error,sizeof(error)));
    read_entity(after,size,256*256,1);
    assert(map_script_content_snapshot_load(initial,size,error,sizeof(error)));
    for(i=0;i<256;i++) assert(map_script_dispatch_tick(error,sizeof(error)));
    assert(map_script_content_snapshot_save(copy,size,error,sizeof(error)) && !memcmp(copy,after,size));
    memcpy(bad,initial,size);bad[MAP_SCRIPT_CONTENT_HEADER_BYTES+sizeof(MapScriptSnapshot)+4]^=1;
    assert(!map_script_content_snapshot_validate(bad,size,error,sizeof(error)));
    assert(!map_script_content_snapshot_load(bad,size,error,sizeof(error)));
    assert(map_script_content_snapshot_save(copy,size,error,sizeof(error)) && !memcmp(copy,after,size));
    memcpy(bad,initial,size);bad[MAP_SCRIPT_CONTENT_HEADER_BYTES+sizeof(MapScriptSnapshot)+72+16]=1;
    assert(!map_script_content_snapshot_validate(bad,size,error,sizeof(error)));
    assert(!map_script_content_snapshot_load(bad,size,error,sizeof(error)));
    assert(map_script_content_snapshot_save(copy,size,error,sizeof(error)) && !memcmp(copy,after,size));
    for(i=0;i<size;i++) assert(!map_script_content_snapshot_load(initial,i,error,sizeof(error)));
    assert(!map_script_activate_content(&d,NULL,"{}",2,error,sizeof(error)));
    assert(map_script_content_snapshot_save(copy,size,error,sizeof(error)) && !memcmp(copy,after,size));
    d=definition("map.state.orb=entity.list()[1] map.on_tick(function() entity.set(map.state.orb,{vx=9}) entity.spawn('demo:orb',{x=3}) map.state.changed=true error('rollback both') end)");
    assert(map_script_activate_content(&d,NULL,package,strlen(package),error,sizeof(error)));
    assert(map_script_content_snapshot_save(initial,size,error,sizeof(error)));
    assert(!map_script_dispatch_tick(error,sizeof(error)) && map_script_is_faulted());
    assert(map_script_content_snapshot_save(after,size,error,sizeof(error)));
    assert(!memcmp(initial+MAP_SCRIPT_CONTENT_HEADER_BYTES+sizeof(plain),after+MAP_SCRIPT_CONTENT_HEADER_BYTES+sizeof(plain),size-MAP_SCRIPT_CONTENT_HEADER_BYTES-sizeof(plain)));
    memcpy(&plain,after+MAP_SCRIPT_CONTENT_HEADER_BYTES,sizeof(plain));assert(plain.state_count==1 && plain.tick==0);
    assert(map_script_content_snapshot_load(initial,size,error,sizeof(error)) && !map_script_is_faulted());
    assert(map_script_content_snapshot_save(copy,size,error,sizeof(error)) && !memcmp(copy,initial,size));
    d=definition("entity={} ");
    assert(!map_script_activate_content(&d,NULL,package,strlen(package),error,sizeof(error)) && strstr(error,"reserved table"));
    d=definition("map.on_tick(function() entity.spawn=function() end end)");
    assert(map_script_activate_content(&d,NULL,package,strlen(package),error,sizeof(error)));
    assert(!map_script_dispatch_tick(error,sizeof(error)));
    d=definition("map.state.calls=0 entity.on_update('demo:orb',function(h) map.state.calls=map.state.calls+1 if map.tick()==0 then entity.spawn('demo:orb',{vx=2}) end end)");
    assert(map_script_activate_content(&d,NULL,package,strlen(package),error,sizeof(error)));
    assert(map_script_dispatch_tick(error,sizeof(error)));
    assert(map_script_content_snapshot_save(after,size,error,sizeof(error)));
    memcpy(&plain,after+MAP_SCRIPT_CONTENT_HEADER_BYTES,sizeof(plain));assert(plain.state[0].number_value==1);
    read_entity(after,size,256,2);
    assert(map_script_dispatch_tick(error,sizeof(error)));
    assert(map_script_content_snapshot_save(after,size,error,sizeof(error)));
    memcpy(&plain,after+MAP_SCRIPT_CONTENT_HEADER_BYTES,sizeof(plain));assert(plain.state[0].number_value==3);
    d=definition("entity.on_update('demo:orb',function(h) local t=map.tick() local v=entity.get(h) if t==1 then entity.set(h,{animation_tick=12,animation_paused=true}) elseif t==2 then assert(v.animation_tick==12 and v.animation_paused) entity.set(h,{animation_tick=0,animation_paused=false}) elseif t==3 then assert(v.animation_tick==1 and not v.animation_paused) end end)");
    assert(map_script_activate_content(&d,NULL,package,strlen(package),error,sizeof(error)));
    assert(map_script_content_snapshot_save(initial,size,error,sizeof(error)));
    for(i=0;i<8;i++)assert(map_script_dispatch_tick(error,sizeof(error)));
    assert(map_script_content_snapshot_save(after,size,error,sizeof(error)));
    assert(map_script_content_snapshot_load(initial,size,error,sizeof(error)));
    for(i=0;i<8;i++)assert(map_script_dispatch_tick(error,sizeof(error)));
    assert(map_script_content_snapshot_save(copy,size,error,sizeof(error)) && !memcmp(after,copy,size));
    d=definition("entity.on_update('demo:orb',function(h) entity.set(h,{animation_tick=99,animation_paused=true}) entity.remove(h) map.state.bad=true error('entity failure') end)");
    assert(map_script_activate_content(&d,NULL,package,strlen(package),error,sizeof(error)));
    assert(map_script_content_snapshot_save(initial,size,error,sizeof(error)));
    assert(!map_script_dispatch_tick(error,sizeof(error)));
    assert(map_script_content_snapshot_save(after,size,error,sizeof(error)));
    assert(!memcmp(initial+MAP_SCRIPT_CONTENT_HEADER_BYTES+sizeof(plain),after+MAP_SCRIPT_CONTENT_HEADER_BYTES+sizeof(plain),size-MAP_SCRIPT_CONTENT_HEADER_BYTES-sizeof(plain)));
    memcpy(&plain,after+MAP_SCRIPT_CONTENT_HEADER_BYTES,sizeof(plain));assert(plain.state_count==0);
    d=definition("entity.on_update('demo:orb',function(h) while true do end end)");
    assert(map_script_activate_content(&d,NULL,package,strlen(package),error,sizeof(error)));
    assert(!map_script_dispatch_tick(error,sizeof(error)) && strstr(error,"instruction budget"));
    d=definition("local n=1 entity.on_update('demo:orb',function(h) return n end)");
    assert(!map_script_activate_content(&d,NULL,package,strlen(package),error,sizeof(error)));
    d=definition("entity.on_update('demo:orb',function(h) leaked=1 end)");
    assert(map_script_activate_content(&d,NULL,package,strlen(package),error,sizeof(error)));
    assert(!map_script_dispatch_tick(error,sizeof(error)) && strstr(error,"read-only"));
    {
        const char contact_package[]="{\"schema\":1,\"capacity\":8,\"types\":[{\"key\":\"demo:orb\",\"regions\":[{\"id\":1,\"role\":\"sensor\",\"layer\":1,\"mask\":1,\"width\":2,\"height\":2}]}],\"placements\":[{\"name\":\"a\",\"type\":\"demo:orb\"},{\"name\":\"b\",\"type\":\"demo:orb\",\"x\":1}]}";
        d=definition("map.state.hits=0 map.on_tick(function() local contacts=entity.contacts() for i=1,#contacts do local c=contacts[i] if c.role_a=='sensor' then map.state.hits=map.state.hits+1 entity.set(c.b,{vx=1}) end end end)");
        assert(map_script_activate_content(&d,NULL,contact_package,strlen(contact_package),error,sizeof(error)));
        assert(map_script_content_snapshot_size()==size);
        assert(map_script_content_snapshot_save(initial,size,error,sizeof(error)));
        for(i=0;i<8;i++) assert(map_script_dispatch_tick(error,sizeof(error)));
        assert(map_script_content_snapshot_save(after,size,error,sizeof(error)));
        memcpy(&plain,after+MAP_SCRIPT_CONTENT_HEADER_BYTES,sizeof(plain));
        assert(plain.state_count==1 && plain.state[0].number_value==1);
        assert(map_script_content_snapshot_load(initial,size,error,sizeof(error)));
        for(i=0;i<8;i++) assert(map_script_dispatch_tick(error,sizeof(error)));
        assert(map_script_content_snapshot_save(copy,size,error,sizeof(error)) && !memcmp(copy,after,size));
    }
    d=definition("map.on_tick(function() map.state.changed=true for i=1,10000 do entity.contacts() end end)");
    assert(map_script_activate_content(&d,NULL,package,strlen(package),error,sizeof(error)));
    assert(map_script_content_snapshot_save(initial,size,error,sizeof(error)));
    assert(!map_script_dispatch_tick(error,sizeof(error)) && strstr(error,"budget"));
    assert(map_script_content_snapshot_save(after,size,error,sizeof(error)));
    memcpy(&plain,after+MAP_SCRIPT_CONTENT_HEADER_BYTES,sizeof(plain));assert(plain.state_count==0 && plain.tick==0);
    assert(!memcmp(initial+MAP_SCRIPT_CONTENT_HEADER_BYTES+sizeof(plain),after+MAP_SCRIPT_CONTENT_HEADER_BYTES+sizeof(plain),size-MAP_SCRIPT_CONTENT_HEADER_BYTES-sizeof(plain)));
    d=definition("map.state.spawns=0 map.state.removes=0 entity.on_spawn('demo:orb',function(h,v) map.state.spawns=map.state.spawns+1 entity.set(h,{vy=2}) end) entity.on_remove('demo:orb',function(h,v) if entity.exists(h) or v.vy~=2 then error('removal view') end map.state.removes=map.state.removes+1 end) map.on_tick(function() if map.tick()==0 then local h=entity.spawn('demo:orb',{}) entity.remove(h) if entity.remove(h) then error('double remove') end end end)");
    assert(map_script_activate_content(&d,NULL,package,strlen(package),error,sizeof(error)));
    assert(map_script_content_snapshot_save(initial,size,error,sizeof(error)));
    assert(map_script_dispatch_tick(error,sizeof(error)));
    assert(map_script_content_snapshot_save(after,size,error,sizeof(error)));
    memcpy(&plain,after+MAP_SCRIPT_CONTENT_HEADER_BYTES,sizeof(plain));
    assert(plain.state_count==2);
    for(i=0;i<plain.state_count;i++) {
        if(!strcmp(plain.state[i].key,"spawns")) assert(plain.state[i].number_value==2);
        else { assert(!strcmp(plain.state[i].key,"removes"));assert(plain.state[i].number_value==1); }
    }
    assert(map_script_content_snapshot_load(initial,size,error,sizeof(error)));
    assert(map_script_dispatch_tick(error,sizeof(error)));
    assert(map_script_content_snapshot_save(copy,size,error,sizeof(error)) && !memcmp(copy,after,size));
    {
        MapScriptDefinition invalid=definition("entity.on_spawn('demo:orb',function(h,v) entity.spawn('demo:orb',{}) end)");
        assert(!map_script_activate_content(&invalid,NULL,package,strlen(package),error,sizeof(error)));
        assert(map_script_content_snapshot_save(copy,size,error,sizeof(error)) && !memcmp(copy,after,size));
        invalid=definition("entity.on_spawn('demo:orb',function(h,v) entity.remove(h) entity.spawn('demo:orb',{}) end)");
        assert(!map_script_activate_content(&invalid,NULL,package,strlen(package),error,sizeof(error)) && strstr(error,"32 callbacks"));
        invalid=definition("entity.on_spawn('demo:orb',function(h,v) leaked=1 end)");
        assert(!map_script_activate_content(&invalid,NULL,package,strlen(package),error,sizeof(error)) && strstr(error,"read-only"));
        invalid=definition("local x=1 entity.on_remove('demo:orb',function(h,v) return x end)");
        assert(!map_script_activate_content(&invalid,NULL,package,strlen(package),error,sizeof(error)));
        invalid=definition("entity.on_spawn('demo:orb',function() end) entity.on_spawn('demo:orb',function() end)");
        assert(!map_script_activate_content(&invalid,NULL,package,strlen(package),error,sizeof(error)) && strstr(error,"duplicate"));
    }
    d=definition("entity.on_remove('demo:orb',function(h,v) map.state.bad=true error('remove failure') end) map.on_tick(function() entity.remove(entity.list()[1]) end)");
    assert(map_script_activate_content(&d,NULL,package,strlen(package),error,sizeof(error)));
    assert(map_script_content_snapshot_save(initial,size,error,sizeof(error)));
    assert(!map_script_dispatch_tick(error,sizeof(error)) && strstr(error,"remove failure"));
    assert(map_script_content_snapshot_save(after,size,error,sizeof(error)));
    memcpy(&plain,after+MAP_SCRIPT_CONTENT_HEADER_BYTES,sizeof(plain));assert(plain.state_count==0);
    assert(!memcmp(initial+MAP_SCRIPT_CONTENT_HEADER_BYTES+sizeof(plain),after+MAP_SCRIPT_CONTENT_HEADER_BYTES+sizeof(plain),size-MAP_SCRIPT_CONTENT_HEADER_BYTES-sizeof(plain)));
    d=definition("map.state.original=entity.find('orb') if entity.find('missing')~=nil or entity.type(map.state.original)~='demo:orb' then error('lookup') end map.on_tick(function() if map.tick()==0 then entity.remove(map.state.original) entity.spawn('demo:orb',{}) if entity.find('orb')~=nil then error('reused slot resolved as old placement') end end end)");
    assert(map_script_activate_content(&d,NULL,package,strlen(package),error,sizeof(error)));
    assert(map_script_content_snapshot_save(initial,size,error,sizeof(error)));
    assert(map_script_dispatch_tick(error,sizeof(error)));
    assert(map_script_content_snapshot_save(after,size,error,sizeof(error)));
    assert(map_script_content_snapshot_load(initial,size,error,sizeof(error)));
    assert(map_script_dispatch_tick(error,sizeof(error)));
    assert(map_script_content_snapshot_save(copy,size,error,sizeof(error)) && !memcmp(copy,after,size));
    d=definition("entity.type('0000000100000008')");
    assert(!map_script_activate_content(&d,NULL,package,strlen(package),error,sizeof(error)) && strstr(error,"no longer exists"));
    d=definition("entity.find(1)");
    assert(!map_script_activate_content(&d,NULL,package,strlen(package),error,sizeof(error)) && strstr(error,"placement name"));
    {
        const char* rooms="{\"schema\":2,\"capacity\":8,\"types\":[{\"key\":\"demo:orb\",\"regions\":[{\"id\":1,\"role\":\"sensor\",\"layer\":1,\"mask\":1,\"x\":2,\"width\":4,\"height\":6}]}],\"placements\":[{\"name\":\"pad\",\"type\":\"demo:orb\",\"room\":\"side\",\"x\":8,\"vx\":2}]}";
        d=definition("entity.on_spawn('demo:orb',function(h,v) local r=entity.regions(h)[1] if v.mirrored then if v.x~=2104 or v.vx~=-2 or r.x~=-6 then error('mirror') end else if v.x~=536 or v.vx~=2 or r.x~=2 then error('source') end end end) entity.on_update('demo:orb',function(h) local v=entity.get(h) entity.set(h,{mirrored=not v.mirrored}) end)");
        d.entity_layout=(EntityPackageLayout){3,{"center","side","end"}};
        assert(map_script_activate_content(&d,NULL,rooms,strlen(rooms),error,sizeof(error)));
        size_t bytes=map_script_content_snapshot_size();unsigned char* start=malloc(bytes);unsigned char* end=malloc(bytes);unsigned char* replay=malloc(bytes);assert(start && end && replay);
        assert(map_script_content_snapshot_save(start,bytes,error,sizeof(error)));
        for(int tick=0;tick<7;tick++)assert(map_script_dispatch_tick(error,sizeof(error)));
        assert(map_script_content_snapshot_save(end,bytes,error,sizeof(error)));
        assert(map_script_content_snapshot_load(start,bytes,error,sizeof(error)));
        for(int tick=0;tick<7;tick++)assert(map_script_dispatch_tick(error,sizeof(error)));
        assert(map_script_content_snapshot_save(replay,bytes,error,sizeof(error)) && !memcmp(end,replay,bytes));
        d=definition("");d.entity_layout=(EntityPackageLayout){2,{"center","side"}};
        assert(map_script_activate_content(&d,NULL,rooms,strlen(rooms),error,sizeof(error)));
        assert(!map_script_content_snapshot_load(start,bytes,error,sizeof(error)));
        free(start);free(end);free(replay);
    }
    {
        PlayerHost player_host={0};MapScriptHost host={0};host.userdata=&player_host;host.read_player_fn=read_player;
        d=definition("map.players()");
        assert(!map_script_activate_content(&d,&host,package,strlen(package),error,sizeof(error)) && strstr(error,"gameplay callbacks"));
        assert(player_host.reads==0);
        d=definition("map.on_tick(function() local p=map.players() if #p~=1 or p[1].contact_radius~=6 then error('count/radius') end map.state.player=p[1].player local x=p[1].x p[1].x=999 if map.players()[1].x~=x then error('write through') end entity.set(entity.find('orb'),{x=x}) end)");
        assert(map_script_activate_content(&d,&host,package,strlen(package),error,sizeof(error)));
        assert(map_script_content_snapshot_save(initial,size,error,sizeof(error)));
        for(i=0;i<16;i++) assert(map_script_dispatch_tick(error,sizeof(error)));
        assert(map_script_content_snapshot_save(after,size,error,sizeof(error)));read_entity(after,size,33*256,1);
        assert(map_script_content_snapshot_load(initial,size,error,sizeof(error)));
        for(i=0;i<16;i++) assert(map_script_dispatch_tick(error,sizeof(error)));
        assert(map_script_content_snapshot_save(copy,size,error,sizeof(error)) && !memcmp(copy,after,size));
        player_host.mode=2;
        assert(map_script_dispatch_tick(error,sizeof(error)));
        assert(map_script_content_snapshot_save(copy,size,error,sizeof(error)));
        memcpy(&plain,copy+MAP_SCRIPT_CONTENT_HEADER_BYTES,sizeof(plain));assert(plain.state[0].number_value==2);
        for(int mode=1;mode<=4;mode++) if(mode!=2) {
            player_host.mode=mode;d=definition("map.on_tick(function() map.state.bad=true map.players() end)");
            assert(map_script_activate_content(&d,&host,package,strlen(package),error,sizeof(error)));
            assert(!map_script_dispatch_tick(error,sizeof(error)) && strstr(error,"invalid view"));
            assert(map_script_content_snapshot_save(copy,size,error,sizeof(error)));
            memcpy(&plain,copy+MAP_SCRIPT_CONTENT_HEADER_BYTES,sizeof(plain));assert(plain.state_count==0 && plain.tick==0);
        }
        assert(map_script_activate_content(&d,NULL,package,strlen(package),error,sizeof(error)));
        assert(!map_script_dispatch_tick(error,sizeof(error)) && strstr(error,"unavailable"));
    }
    {
        char sample_source[4096],sample_json[4096];size_t source_length,json_length;
        FILE* file=fopen("docs/examples/entity_follow_player.lua","rb");assert(file);
        source_length=fread(sample_source,1,sizeof(sample_source)-1,file);assert(!ferror(file));fclose(file);sample_source[source_length]=0;
        file=fopen("docs/examples/entity_visual_package.json","rb");assert(file);
        json_length=fread(sample_json,1,sizeof(sample_json)-1,file);assert(!ferror(file));fclose(file);sample_json[json_length]=0;
        PlayerHost player_host={0};MapScriptHost host={0};host.userdata=&player_host;host.read_player_fn=read_player;
        d=definition(sample_source);
        assert(map_script_activate_content(&d,&host,sample_json,json_length,error,sizeof(error)));
        size_t sample_size=map_script_content_snapshot_size();unsigned char* sample_initial=malloc(sample_size);unsigned char* sample_after=malloc(sample_size);unsigned char* sample_replay=malloc(sample_size);
        assert(sample_initial && sample_after && sample_replay);
        assert(map_script_content_snapshot_save(sample_initial,sample_size,error,sizeof(error)));
        for(i=0;i<100;i++) assert(map_script_dispatch_tick(error,sizeof(error)));
        uint32_t cursor=0;EntityRenderView view;
        assert(map_script_entity_render_next(&cursor,&view) && view.x==32*256 && view.y==64*256 && view.sprite==5);
        assert(map_script_content_snapshot_save(sample_after,sample_size,error,sizeof(error)));
        assert(map_script_content_snapshot_load(sample_initial,sample_size,error,sizeof(error)));
        for(i=0;i<100;i++) assert(map_script_dispatch_tick(error,sizeof(error)));
        assert(map_script_content_snapshot_save(sample_replay,sample_size,error,sizeof(error)) && !memcmp(sample_after,sample_replay,sample_size));
        free(sample_initial);free(sample_after);free(sample_replay);
    }
    {
        char source[4096],json[4096];FILE* file=fopen("docs/examples/entity_moving_hazard.lua","rb");assert(file);
        size_t n=fread(source,1,sizeof(source)-1,file);assert(!ferror(file) && feof(file));fclose(file);source[n]=0;
        file=fopen("docs/examples/entity_moving_hazard.json","rb");assert(file);
        n=fread(json,1,sizeof(json)-1,file);assert(!ferror(file) && feof(file));fclose(file);json[n]=0;
        VelocityHost native={0};MapScriptHost host={0};native.available=3;
        for(uint32_t j=0;j<2;j++){native.players[j].object_id=j;native.players[j].object_kind=MAP_SCRIPT_OBJECT_PLAYER;native.players[j].x=j?120:48;native.players[j].y=64;}
        host.userdata=&native;host.read_player_fn=velocity_read;host.commit_players_fn=player_commit;
        d=definition(source);d.entity_layout=(EntityPackageLayout){1,{"center"}};assert(map_script_activate_content(&d,&host,json,n,error,sizeof(error)));
        size_t bytes=map_script_content_snapshot_size();unsigned char *start=malloc(bytes),*end=malloc(bytes),*replay=malloc(bytes);assert(start && end && replay);
        assert(map_script_content_snapshot_save(start,bytes,error,sizeof(error)));
        for(int tick=0;tick<100;tick++)assert(map_script_dispatch_tick(error,sizeof(error)));
        assert(native.available==2 && native.commits==1);
        assert(map_script_content_snapshot_save(end,bytes,error,sizeof(error)));
        assert(map_script_content_snapshot_load(start,bytes,error,sizeof(error)));native.available=3;native.commits=0;
        for(int tick=0;tick<100;tick++)assert(map_script_dispatch_tick(error,sizeof(error)));
        assert(native.available==2 && native.commits==1);
        assert(map_script_content_snapshot_save(replay,bytes,error,sizeof(error)) && !memcmp(end,replay,bytes));
        assert(map_script_content_snapshot_load(start,bytes,error,sizeof(error)));native.available=3;native.commits=0;native.reject=1;
        assert(!map_script_dispatch_tick(error,sizeof(error)) && native.available==3);
        free(start);free(end);free(replay);
    }
    for(int variant=0;variant<3;variant++) {
        char source[4096],json[4096];FILE* file=fopen(variant==2?"tests/fixtures/blocks-player-touching.lua":variant==1?"tests/fixtures/workshop-launch-pad.lua":"docs/examples/entity_launch_pad.lua","rb");assert(file);
        size_t n=fread(source,1,sizeof(source)-1,file);assert(!ferror(file) && feof(file));fclose(file);source[n]=0;
        file=fopen(variant==1?"tests/fixtures/workshop-launch-pad.json":"docs/examples/entity_launch_pad.json","rb");assert(file);
        n=fread(json,1,sizeof(json)-1,file);assert(!ferror(file) && feof(file));fclose(file);json[n]=0;
        VelocityHost native={0};MapScriptHost host={0};MapScriptObjectView original[2];
        native.available=3;
        for(uint32_t j=0;j<2;j++){native.players[j].object_id=j;native.players[j].object_kind=MAP_SCRIPT_OBJECT_PLAYER;native.players[j].x=48+12*j;native.players[j].y=58;}
        memcpy(original,native.players,sizeof(original));host.userdata=&native;host.read_player_fn=velocity_read;host.apply_player_velocities_fn=velocity_commit;
        d=definition(source);d.entity_layout=(EntityPackageLayout){1,{"center"}};assert(map_script_activate_content(&d,&host,json,n,error,sizeof(error)));
        size_t bytes=map_script_content_snapshot_size();unsigned char* start=malloc(bytes);unsigned char* end=malloc(bytes);unsigned char* replay=malloc(bytes);assert(start && end && replay);
        assert(map_script_content_snapshot_save(start,bytes,error,sizeof(error)));
        for(int tick=0;tick<8;tick++)assert(map_script_dispatch_tick(error,sizeof(error)));
        assert(native.commits==(variant==2?8:1) && native.players[0].vy==-4 && native.players[1].vy==-4);
        assert(map_script_content_snapshot_save(end,bytes,error,sizeof(error)));
        assert(map_script_content_snapshot_load(start,bytes,error,sizeof(error)));memcpy(native.players,original,sizeof(original));native.commits=0;
        for(int tick=0;tick<8;tick++)assert(map_script_dispatch_tick(error,sizeof(error)));
        assert(native.commits==(variant==2?8:1) && native.players[0].vy==-4 && native.players[1].vy==-4);
        assert(map_script_content_snapshot_save(replay,bytes,error,sizeof(error)) && !memcmp(end,replay,bytes));
        assert(map_script_content_snapshot_load(start,bytes,error,sizeof(error)));memcpy(native.players,original,sizeof(original));native.commits=0;native.reject=1;
        assert(!map_script_dispatch_tick(error,sizeof(error)) && native.commits==1 && !memcmp(native.players,original,sizeof(original)));
        if(variant==2){
            for(int corner=0;corner<2;corner++){
                assert(map_script_content_snapshot_load(start,bytes,error,sizeof(error)));memcpy(native.players,original,sizeof(original));native.commits=0;native.reject=0;
                native.players[0].x=70;native.players[0].y=corner?70:64;native.players[1].x=200;
                assert(map_script_dispatch_tick(error,sizeof(error)));
                assert(native.commits==(corner?0:1)&&native.players[0].vy==(corner?0:-4)&&native.players[1].vy==0);
            }
        }
        free(start);free(end);free(replay);
    }
    {
        VelocityHost native={0};MapScriptHost host={0};MapScriptObjectView original[2],future[2];
        native.available=3;for(uint32_t j=0;j<2;j++) {native.players[j].object_id=j;native.players[j].object_kind=MAP_SCRIPT_OBJECT_PLAYER;native.players[j].x=32+(float)j;native.players[j].y=64;}
        memcpy(original,native.players,sizeof(original));host.userdata=&native;host.read_player_fn=velocity_read;host.apply_player_velocities_fn=velocity_commit;
        d=definition("map.set_player_velocity(1,1,2)");
        assert(!map_script_activate_content(&d,&host,package,strlen(package),error,sizeof(error)) && strstr(error,"tick/timer"));
        d=definition("map.on_tick(function() map.set_player_velocity(1,3,4) map.set_player_velocity(2,-5,6) if map.players()[1].vx~=3 then error('queued read') end end) entity.on_update('demo:orb',function(h) map.set_player_velocity(1,7,8) end)");
        assert(map_script_activate_content(&d,&host,package,strlen(package),error,sizeof(error)));
        assert(map_script_content_snapshot_save(initial,size,error,sizeof(error)));
        for(i=0;i<8;i++) assert(map_script_dispatch_tick(error,sizeof(error)));
        assert(native.commits==8 && native.players[0].vx==7 && native.players[0].vy==8 && native.players[1].vx==-5 && native.players[1].vy==6);
        assert(native.players[0].x==original[0].x && native.players[1].y==original[1].y);
        memcpy(future,native.players,sizeof(future));
        assert(map_script_content_snapshot_save(after,size,error,sizeof(error)));
        assert(map_script_content_snapshot_load(initial,size,error,sizeof(error)));memcpy(native.players,original,sizeof(original));native.commits=0;
        for(i=0;i<8;i++) assert(map_script_dispatch_tick(error,sizeof(error)));
        assert(!memcmp(future,native.players,sizeof(future)));
        assert(map_script_content_snapshot_save(copy,size,error,sizeof(error)) && !memcmp(copy,after,size));
        const char* failures[]={
            "map.on_tick(function() map.set_player_velocity(1,9,9) map.set_player_velocity(2,8,8) entity.set(entity.find('orb'),{vx=9}) map.state.bad=true error('late failure') end)",
            "map.on_tick(function() map.set_player_velocity(1,9,9) map.set_player_velocity(2,8,8) end)"};
        for(int scenario=0;scenario<3;scenario++) {
            memcpy(native.players,original,sizeof(original));native.commits=0;native.reject=scenario==1;native.available=scenario==2?1:3;
            d=definition(failures[scenario==0?0:1]);
            assert(map_script_activate_content(&d,&host,package,strlen(package),error,sizeof(error)));
            assert(map_script_content_snapshot_save(initial,size,error,sizeof(error)));
            assert(!map_script_dispatch_tick(error,sizeof(error)));
            assert(native.commits==(scenario==1?1:0) && !memcmp(original,native.players,sizeof(original)));
            assert(map_script_content_snapshot_save(after,size,error,sizeof(error)));
            memcpy(&plain,after+MAP_SCRIPT_CONTENT_HEADER_BYTES,sizeof(plain));assert(plain.state_count==0 && plain.tick==0);
            assert(!memcmp(initial+MAP_SCRIPT_CONTENT_HEADER_BYTES+sizeof(plain),after+MAP_SCRIPT_CONTENT_HEADER_BYTES+sizeof(plain),size-MAP_SCRIPT_CONTENT_HEADER_BYTES-sizeof(plain)));
        }
        native.available=3;native.reject=0;
        assert(map_script_content_snapshot_load(initial,size,error,sizeof(error)));
        assert(map_script_dispatch_tick(error,sizeof(error)) && native.players[0].vx==9 && native.players[1].vx==8);
        {
            MapScriptHost combined=host;combined.commit_players_fn=player_commit;
            const char* source="map.on_tick(function() if map.tick()==0 then map.set_player_velocity(2,6,7) assert(map.defeat_player(1)) assert(not map.defeat_player(1)) assert(#map.players()==1 and map.players()[1].player==2) end end)";
            for(int reject=0;reject<2;reject++) {
                memcpy(native.players,original,sizeof(original));native.available=3;native.reject=reject;native.commits=0;
                d=definition(source);assert(map_script_activate_content(&d,&combined,package,strlen(package),error,sizeof(error)));
                assert(map_script_content_snapshot_save(initial,size,error,sizeof(error)));
                assert(map_script_dispatch_tick(error,sizeof(error))==!reject);
                if(reject){assert(native.available==3 && !memcmp(original,native.players,sizeof(original)));}
                else {assert(native.available==2 && native.players[1].vx==6);assert(map_script_content_snapshot_save(after,size,error,sizeof(error)));
                    assert(map_script_content_snapshot_load(initial,size,error,sizeof(error)));memcpy(native.players,original,sizeof(original));native.available=3;
                    assert(map_script_dispatch_tick(error,sizeof(error)));assert(native.available==2 && native.players[1].vx==6);
                    assert(map_script_content_snapshot_save(copy,size,error,sizeof(error)) && !memcmp(copy,after,size));}
            }
            native.available=3;native.reject=0;native.commits=0;memcpy(native.players,original,sizeof(original));
            const char* defeat_failures[]={
                "map.on_tick(function() map.defeat_player(1) map.set_player_velocity(2,9,9) error('abort') end)",
                "map.on_tick(function() map.defeat_player(1) map.set_player_velocity(1,9,9) end)",
                "map.on_tick(function() map.defeat_player(3) end)",
                "map.defeat_player(1)"};
            for(int failure=0;failure<4;failure++) {
                d=definition(defeat_failures[failure]);
                if(failure==3)assert(!map_script_activate_content(&d,&combined,package,strlen(package),error,sizeof(error)));
                else {assert(map_script_activate_content(&d,&combined,package,strlen(package),error,sizeof(error)));assert(!map_script_dispatch_tick(error,sizeof(error)));}
                assert(native.commits==0 && native.available==3 && !memcmp(original,native.players,sizeof(original)));
            }
            combined.commit_players_fn=NULL;d=definition("map.on_tick(function() map.defeat_player(1) end)");
            assert(map_script_activate_content(&d,&combined,package,strlen(package),error,sizeof(error)));
            assert(!map_script_dispatch_tick(error,sizeof(error)) && strstr(error,"unavailable"));

        }
        native.available=3;native.reject=0;

        {
            MapScriptTileBinding binding={'B',"demo:tile"};MapScriptContactView contact={0};
            memcpy(native.players,original,sizeof(original));native.commits=0;host.apply_object_fn=velocity_legacy_apply;
            d=definition("map.on_enter('B',function(object,tile) object:set_velocity_limits({max_vx=2},4) end) map.on_leave('B',function(object,tile) object:set_velocity(-9,-10) end) map.on_tick(function() if map.tick()==1 then map.set_player_velocity(1,7,8) end end)");
            d.bindings=&binding;d.binding_count=1;
            assert(map_script_activate_content(&d,&host,package,strlen(package),error,sizeof(error)));
            contact.object=&native.players[0];contact.symbol='B';contact.qualified_key="demo:tile";
            assert(map_script_dispatch_contact(&contact,error,sizeof(error)));
            assert(map_script_dispatch_tick(error,sizeof(error)));
            assert(map_script_dispatch_tick(error,sizeof(error)));
            assert(native.commits==1 && native.players[0].vx==2 && native.players[0].vy==8);
        }

    }
    map_script_deactivate();assert(map_script_network_admissible(error,sizeof(error)));free(initial);free(after);free(copy);free(bad);
    puts("managed entities: sandbox, combined snapshots, replay, atomic rejection and callback rollback passed");return 0;
}
