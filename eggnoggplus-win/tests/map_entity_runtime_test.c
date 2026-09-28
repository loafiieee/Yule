#include "../map_script.h"
#include "../entity_package.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static const char package[] = "{\"schema\":1,\"capacity\":8,\"types\":[{\"key\":\"demo:orb\",\"regions\":[]}],\"placements\":[{\"name\":\"orb\",\"type\":\"demo:orb\",\"vx\":1}]}";
static char error[384];
static uint32_t object_state_crc(const MapScriptObjectStateSnapshot* snapshot) {
    const unsigned char* bytes=(const unsigned char*)snapshot;
    size_t begin=offsetof(MapScriptObjectStateSnapshot,checksum),end=begin+4;
    uint32_t crc=UINT32_MAX;
    for(size_t i=0;i<sizeof(*snapshot);++i) {
        uint8_t value=(i>=begin&&i<end)?0:bytes[i];crc^=value;
        for(int bit=0;bit<8;++bit){uint32_t mask=(uint32_t)-(int32_t)(crc&1u);crc=(crc>>1)^(UINT32_C(0xedb88320)&mask);}
    }
    return ~crc;
}
static uint32_t map_state_crc(const MapScriptSnapshot* snapshot) {
    const unsigned char* bytes=(const unsigned char*)snapshot;
    size_t begin=offsetof(MapScriptSnapshot,checksum),end=begin+4;
    uint32_t crc=UINT32_MAX;
    for(size_t i=0;i<sizeof(*snapshot);++i) {
        uint8_t value=(i>=begin&&i<end)?0:bytes[i];crc^=value;
        for(int bit=0;bit<8;++bit){uint32_t mask=(uint32_t)-(int32_t)(crc&1u);crc=(crc>>1)^(UINT32_C(0xedb88320)&mask);}
    }
    return ~crc;
}
static MapScriptDefinition definition(const char* source) {
    MapScriptDefinition d;memset(&d,0,sizeof(d));d.script_id=987;
    d.source=source;d.source_len=strlen(source);return d;
}
static void read_entity(const unsigned char* bytes,size_t size,int32_t x,uint32_t count) {
    EntityPackage* p=entity_package_decode(package,strlen(package),error,sizeof(error));
    EntityValue value;assert(p);
    (void)size;
    assert(entity_package_load(p,bytes+MAP_SCRIPT_CONTENT_HEADER_BYTES+sizeof(MapScriptSnapshot),entity_package_snapshot_size(p)));
    assert(entity_world_read(entity_package_world(p),entity_package_placement(p,"orb"),&value));
    assert(value.x==x && entity_world_count(entity_package_world(p))==count);
    entity_package_free(p);
}
typedef struct PlayerHost { int mode,observation_mode;unsigned reads;MapScriptPlayerObservation observation; } PlayerHost;
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
static int read_player_observation(void* user,uint32_t slot,MapScriptPlayerObservation* out) {
    PlayerHost* host=user;(void)slot;if(host->observation_mode==1)return 0;*out=host->observation;
    if(host->observation_mode==2)out->grounded=2;
    else if(host->observation_mode==3)out->previously_grounded=2;
    else if(host->observation_mode==4)out->has_sword=2;
    else if(host->observation_mode==5)out->facing=2;
    return 1;
}
typedef struct VelocityHost { MapScriptObjectView players[2];MapScriptPlayerObservation observations[2];int8_t rooms[2];uint32_t available;int reject,commits; } VelocityHost;
typedef struct MineHost { unsigned calls,count;MapScriptMineTrigger triggers[MAP_SCRIPT_MAX_MINE_TRIGGERS]; } MineHost;
static void trigger_mines(void* user,const MapScriptMineTrigger* triggers,uint32_t count) {
    MineHost* host=user;host->calls++;host->count=count;assert(count<=MAP_SCRIPT_MAX_MINE_TRIGGERS);memcpy(host->triggers,triggers,count*sizeof(*triggers));
}
static int velocity_read(void* user,uint32_t slot,MapScriptObjectView* out) {
    VelocityHost* host=user;if(!(host->available&(1u<<slot))) return 0;*out=host->players[slot];return 1;
}
static int velocity_observation(void* user,uint32_t slot,MapScriptPlayerObservation* out) {
    VelocityHost* host=user;if(!(host->available&(1u<<slot)))return 0;*out=host->observations[slot];return 1;
}
static int velocity_commit(void* user,uint32_t mask,const MapScriptObjectView players[2]) {
    VelocityHost* host=user;host->commits++;
    if(host->reject || (mask&host->available)!=mask) return 0;
    for(uint32_t i=0;i<2;i++) if(mask&(1u<<i)) {host->players[i].vx=players[i].vx;host->players[i].vy=players[i].vy;}
    return 1;
}
static int player_commit(void* user,uint32_t mask,uint32_t defeat,uint32_t room_mask,
                         const int8_t rooms[2],const MapScriptObjectView players[2]) {
    VelocityHost* host=user;
    host->commits++;
    if(host->reject)return 0;
    if(((mask|defeat|room_mask)&host->available)!=(mask|defeat|room_mask))return 0;
    for(uint32_t i=0;i<2;i++)if(mask&(1u<<i))host->players[i]=players[i];
    for(uint32_t i=0;i<2;i++)if(room_mask&(1u<<i))host->rooms[i]=rooms[i];
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
    assert(entity_package_load(decoded,after+offset,entity_package_snapshot_size(decoded)));EntityValue value;
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
    EntityPackage* decoded=entity_package_decode(package,strlen(package),error,sizeof(error));assert(decoded);assert(entity_package_load(decoded,end+offset,entity_package_snapshot_size(decoded)));EntityValue value;assert(entity_world_read(entity_package_world(decoded),entity_package_placement(decoded,"orb"),&value));assert(value.vx==8&&value.vy==256&&value.x==248&&value.y==896);entity_package_free(decoded);
    assert(map_script_content_snapshot_load(start,size,error,sizeof(error)));for(int i=0;i<5;i++)assert(map_script_dispatch_tick(error,sizeof(error)));assert(map_script_content_snapshot_save(replay,size,error,sizeof(error))&&!memcmp(end,replay,size));free(start);free(end);free(replay);map_script_deactivate();
}
static int terrain_fixture(void* user,double x,double y,double width,double height){
    (void)user;return x+width*0.5>64||y+height*0.5>48||y-height*0.5<0;
}
static void test_solid_regions(void){
    const char* json="{\"schema\":1,\"capacity\":8,\"types\":[{\"key\":\"demo:wall\",\"regions\":[{\"id\":1,\"role\":\"solid\",\"layer\":1,\"mask\":1,\"x\":0,\"y\":0,\"width\":16,\"height\":16}]}],\"placements\":[{\"name\":\"wall\",\"type\":\"demo:wall\",\"x\":32,\"y\":32,\"visible\":false}]}";
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
    const char* source="entity.on_spawn('demo:orb',function(h) entity.set(h,{automatic_motion=false}) entity.set_value(h,'direction',0) end) entity.on_update('demo:orb',function(h) local direction=entity.change_value(h,'direction',1) local b=entity.get(h) entity.set(h,{x=b.x+direction}) if map.tick()==2 then entity.remove(h) end end)";
    MapScriptDefinition d=definition(source);assert(map_script_activate_content(&d,NULL,package,strlen(package),error,sizeof(error)));
    size_t size=map_script_content_snapshot_size(),offset=MAP_SCRIPT_CONTENT_HEADER_BYTES+sizeof(MapScriptSnapshot);unsigned char *start=malloc(size),*end=malloc(size),*replay=malloc(size);assert(start&&end&&replay);
    assert(map_script_content_snapshot_save(start,size,error,sizeof(error)));
    for(int pass=0;pass<2;pass++){
        if(pass)assert(map_script_content_snapshot_load(start,size,error,sizeof(error)));
        for(int tick=0;tick<2;tick++)assert(map_script_dispatch_tick(error,sizeof(error)));
        assert(map_script_content_snapshot_save(replay,size,error,sizeof(error)));
        EntityPackage* decoded=entity_package_decode(package,strlen(package),error,sizeof(error));assert(decoded);size_t entity_size=entity_package_snapshot_size(decoded);assert(entity_package_load(decoded,replay+offset,entity_size));EntityValue value;assert(entity_world_read(entity_package_world(decoded),entity_package_placement(decoded,"orb"),&value));assert(value.x==3*256&&value.vx==256);
        const MapScriptObjectStateSnapshot* object_state=(const MapScriptObjectStateSnapshot*)(replay+offset+entity_size);assert(object_state->count==1);assert(object_state->entries[0].handle==entity_package_placement(decoded,"orb"));assert(object_state->entries[0].key_len==9&&!memcmp(object_state->entries[0].key,"direction",9));assert(object_state->entries[0].number_value==2);entity_package_free(decoded);
        assert(map_script_dispatch_tick(error,sizeof(error)));assert(map_script_content_snapshot_save(pass?replay:end,size,error,sizeof(error)));
        MapScriptSnapshot state;memcpy(&state,(pass?replay:end)+MAP_SCRIPT_CONTENT_HEADER_BYTES,sizeof(state));assert(state.state_count==0);object_state=(const MapScriptObjectStateSnapshot*)((pass?replay:end)+size-sizeof(MapScriptObjectStateSnapshot));assert(object_state->count==0);
    }
    assert(!memcmp(end,replay,size));free(start);free(end);free(replay);map_script_deactivate();
}
static void test_spawn_initial_values(void){
    const char* source=
        "entity.on_spawn('demo:orb',function(h) if entity.has_value(h,'damage') then "
        "assert(entity.value(h,'damage')==3 and entity.value(h,'owner')=='player_1' and entity.value(h,'armed')==false) "
        "entity.set_value(h,'initialized_before_spawn',true) end end) "
        "map.on_tick(function() if map.tick()==0 then local h=entity.spawn('demo:orb',{x=4,values={damage=3,owner='player_1',armed=false}}) "
        "assert(entity.value(h,'initialized_before_spawn')==true) end end)";
    MapScriptDefinition d=definition(source);assert(map_script_activate_content(&d,NULL,package,strlen(package),error,sizeof(error)));
    size_t bytes=map_script_content_snapshot_size();unsigned char *start=malloc(bytes),*end=malloc(bytes),*replay=malloc(bytes);assert(start&&end&&replay);
    assert(map_script_content_snapshot_save(start,bytes,error,sizeof(error)));
    assert(map_script_dispatch_tick(error,sizeof(error)));assert(map_script_content_snapshot_save(end,bytes,error,sizeof(error)));
    const MapScriptObjectStateSnapshot* state=(const MapScriptObjectStateSnapshot*)(end+bytes-sizeof(MapScriptObjectStateSnapshot));
    assert(state->count==4);
    assert(!strcmp(state->entries[0].key,"armed")&&!strcmp(state->entries[1].key,"damage")&&
           !strcmp(state->entries[2].key,"owner")&&!strcmp(state->entries[3].key,"initialized_before_spawn"));
    assert(map_script_content_snapshot_load(start,bytes,error,sizeof(error)));assert(map_script_dispatch_tick(error,sizeof(error)));
    assert(map_script_content_snapshot_save(replay,bytes,error,sizeof(error))&&!memcmp(end,replay,bytes));
    d=definition("map.on_tick(function() entity.spawn('demo:orb',{values={good=1,bad={}}}) end)");
    assert(map_script_activate_content(&d,NULL,package,strlen(package),error,sizeof(error)));
    assert(map_script_content_snapshot_save(start,bytes,error,sizeof(error)));
    assert(!map_script_dispatch_tick(error,sizeof(error))&&strstr(error,"object variables accept"));
    assert(map_script_content_snapshot_save(replay,bytes,error,sizeof(error)));
    assert(!memcmp(start+MAP_SCRIPT_CONTENT_HEADER_BYTES+sizeof(MapScriptSnapshot),
                   replay+MAP_SCRIPT_CONTENT_HEADER_BYTES+sizeof(MapScriptSnapshot),
                   bytes-MAP_SCRIPT_CONTENT_HEADER_BYTES-sizeof(MapScriptSnapshot)));
    d=definition("map.on_tick(function() local values={} for i=1,33 do values['v'..i]=i end entity.spawn('demo:orb',{values=values}) end)");
    assert(map_script_activate_content(&d,NULL,package,strlen(package),error,sizeof(error)));
    assert(!map_script_dispatch_tick(error,sizeof(error))&&strstr(error,"at most 32"));
    free(start);free(end);free(replay);map_script_deactivate();
}
static void test_object_variable_capacity_and_isolation(void){
    const char* many_package="{\"schema\":1,\"capacity\":2,\"types\":[{\"key\":\"demo:orb\",\"regions\":[]}],\"placements\":[{\"name\":\"a\",\"type\":\"demo:orb\",\"x\":1},{\"name\":\"b\",\"type\":\"demo:orb\",\"x\":2}]}";
    const char* source="entity.on_spawn('demo:orb',function(h,v) for i=1,80 do entity.set_value(h,'value_'..tostring(i),i+v.x) end end) entity.on_update('demo:orb',function(h) local v=entity.get(h) assert(entity.value(h,'value_80')==80+v.x and #entity.value_keys(h)==80) end)";
    MapScriptDefinition d=definition(source);assert(map_script_activate_content(&d,NULL,many_package,strlen(many_package),error,sizeof(error)));
    size_t size=map_script_content_snapshot_size();unsigned char *start=malloc(size),*after=malloc(size),*replay=malloc(size);assert(start&&after&&replay);
    assert(map_script_content_snapshot_save(start,size,error,sizeof(error)));
    const MapScriptObjectStateSnapshot* state=(const MapScriptObjectStateSnapshot*)(start+size-sizeof(MapScriptObjectStateSnapshot));assert(state->count==160);
    for(int tick=0;tick<3;tick++)assert(map_script_dispatch_tick(error,sizeof(error)));
    assert(map_script_content_snapshot_save(after,size,error,sizeof(error)));
    assert(map_script_content_snapshot_load(start,size,error,sizeof(error)));
    for(int tick=0;tick<3;tick++)assert(map_script_dispatch_tick(error,sizeof(error)));
    assert(map_script_content_snapshot_save(replay,size,error,sizeof(error))&&!memcmp(after,replay,size));
    d=definition("entity.on_spawn('demo:orb',function(h) for i=1,257 do entity.set_value(h,'v'..tostring(i),i) end end)");
    assert(!map_script_activate_content(&d,NULL,many_package,strlen(many_package),error,sizeof(error))&&strstr(error,"limit reached"));
    assert(map_script_content_snapshot_save(replay,size,error,sizeof(error))&&!memcmp(after,replay,size));
    free(start);free(after);free(replay);map_script_deactivate();
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
    assert(!memcmp(end,replay,size));EntityPackage* decoded=entity_package_decode(json,strlen(json),error,sizeof(error));assert(decoded);assert(entity_package_load(decoded,end+offset,entity_package_snapshot_size(decoded)));EntityValue value;assert(entity_world_read(entity_package_world(decoded),entity_package_placement(decoded,"orb"),&value));assert(value.x==44*256&&value.vx==-2*256);entity_package_free(decoded);
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
    EntityPackage* decoded=entity_package_decode(terrain_package,strlen(terrain_package),error,sizeof(error));assert(decoded);assert(entity_package_load(decoded,end+offset,entity_package_snapshot_size(decoded)));EntityValue value;assert(entity_world_read(entity_package_world(decoded),entity_package_placement(decoded,"orb"),&value));assert(value.x==56*256&&value.y==40*256&&value.vx==0&&value.vy==0);entity_package_free(decoded);
    assert(map_script_content_snapshot_load(start,size,error,sizeof(error)));for(int i=0;i<100;i++)assert(map_script_dispatch_tick(error,sizeof(error)));assert(map_script_content_snapshot_save(replay,size,error,sizeof(error))&&!memcmp(end,replay,size));free(start);free(end);free(replay);map_script_deactivate();
}
static void test_double_jump_example(void) {
    char source[8192],json[8192];FILE* file=fopen("docs/examples/entity_double_jump.lua","rb");assert(file);
    size_t n=fread(source,1,sizeof(source)-1,file);assert(!ferror(file)&&feof(file));fclose(file);source[n]=0;
    file=fopen("docs/examples/entity_double_jump.json","rb");assert(file);n=fread(json,1,sizeof(json)-1,file);assert(!ferror(file)&&feof(file));fclose(file);json[n]=0;
    VelocityHost native={0};MapScriptHost host={0};MapScriptDefinition d=definition(source);
    native.available=1;native.players[0].object_id=0;native.players[0].object_kind=MAP_SCRIPT_OBJECT_PLAYER;
    native.players[0].x=32;native.players[0].y=64;host.userdata=&native;host.read_player_fn=velocity_read;
    host.read_player_observation_fn=velocity_observation;host.apply_player_velocities_fn=velocity_commit;
    assert(map_script_activate_content(&d,&host,json,strlen(json),error,sizeof(error)));
    size_t bytes=map_script_content_snapshot_size();unsigned char *grounded=malloc(bytes),*end=malloc(bytes),*replay=malloc(bytes);assert(grounded&&end&&replay);
    native.observations[0].grounded=1;native.observations[0].previously_grounded=1;
    assert(map_script_dispatch_tick(error,sizeof(error)));assert(map_script_content_snapshot_save(grounded,bytes,error,sizeof(error)));
    uint32_t cursor=0,visible=0;EntityRenderView view;while(map_script_entity_render_next(&cursor,&view))visible++;assert(visible==2);
    native.players[0].vy=-3;native.observations[0].grounded=0;native.observations[0].previously_grounded=1;
    native.observations[0].command_bits=MAP_SCRIPT_COMMAND_JUMP;native.observations[0].previous_command_bits=0;
    assert(map_script_dispatch_tick(error,sizeof(error))&&native.commits==0);
    native.observations[0].previously_grounded=0;native.observations[0].command_bits=0;native.observations[0].previous_command_bits=MAP_SCRIPT_COMMAND_JUMP;
    assert(map_script_dispatch_tick(error,sizeof(error))&&native.commits==0);
    native.observations[0].command_bits=MAP_SCRIPT_COMMAND_JUMP;native.observations[0].previous_command_bits=0;
    assert(map_script_dispatch_tick(error,sizeof(error))&&native.commits==1&&native.players[0].vy==-4);
    assert(map_script_content_snapshot_save(end,bytes,error,sizeof(error)));cursor=visible=0;while(map_script_entity_render_next(&cursor,&view))visible++;assert(visible==1);
    assert(map_script_content_snapshot_load(grounded,bytes,error,sizeof(error)));native.commits=0;native.players[0].vy=-3;
    native.observations[0].grounded=0;native.observations[0].previously_grounded=1;native.observations[0].command_bits=MAP_SCRIPT_COMMAND_JUMP;native.observations[0].previous_command_bits=0;
    assert(map_script_dispatch_tick(error,sizeof(error)));native.observations[0].previously_grounded=0;native.observations[0].command_bits=0;native.observations[0].previous_command_bits=MAP_SCRIPT_COMMAND_JUMP;
    assert(map_script_dispatch_tick(error,sizeof(error)));native.observations[0].command_bits=MAP_SCRIPT_COMMAND_JUMP;native.observations[0].previous_command_bits=0;
    assert(map_script_dispatch_tick(error,sizeof(error))&&native.commits==1&&native.players[0].vy==-4);
    assert(map_script_content_snapshot_save(replay,bytes,error,sizeof(error))&&!memcmp(end,replay,bytes));
    free(grounded);free(end);free(replay);map_script_deactivate();
}
static void test_entity_contact_callbacks(void) {
    const char* json="{\"schema\":1,\"capacity\":8,\"types\":["
        "{\"key\":\"demo:blade\",\"regions\":["
        "{\"id\":1,\"name\":\"attack\",\"role\":\"hitbox\",\"layer\":1,\"mask\":1,\"width\":10,\"height\":10},"
        "{\"id\":2,\"name\":\"pickup\",\"role\":\"sensor\",\"layer\":2,\"mask\":2,\"width\":4,\"height\":4}]},"
        "{\"key\":\"demo:target\",\"regions\":["
        "{\"id\":7,\"name\":\"damage\",\"role\":\"hurtbox\",\"layer\":1,\"mask\":1,\"width\":10,\"height\":10}]}],"
        "\"placements\":[{\"name\":\"blade\",\"type\":\"demo:blade\",\"x\":10,\"y\":10},"
        "{\"name\":\"target\",\"type\":\"demo:target\",\"x\":15,\"y\":10}]}";
    const char* source=
        "map.on_tick(function() "
        "local h=entity.find('blade') local all=entity.player_contacts(h) "
        "assert(#all==2 and all[1].region_name=='attack' and all[1].role=='hitbox' and all[1].player==1) "
        "assert(#entity.player_contacts(h,'hitbox')==1 and #entity.player_contacts(h,'name:pickup')==1 and #entity.player_contacts(h,2)==1) "
        "all[1].region=99 assert(entity.player_contacts(h,'hitbox')[1].region==1) end) "
        "entity.on_contact('demo:blade',function(h,c) "
        "assert(c.other==entity.find('target') and c.self_region_name=='attack' and c.other_region_name=='damage' and c.self_role=='hitbox' and c.other_role=='hurtbox' and c.other_type=='demo:target') "
        "entity.change_value(h,'entity_contacts',1) assert(entity.damage(c.other,3,h)) end) "
        "entity.on_spawn('demo:target',function(h) entity.set_value(h,'health',10) end) "
        "entity.on_damage('demo:target',function(h,event) "
        "assert(event.amount==3 and event.source==entity.find('blade') and event.source_type=='demo:blade') "
        "entity.change_value(h,'health',-event.amount) end) "
        "entity.on_player_contact('demo:blade',function(h,c) "
        "assert(c.entity==h and c.player==1 and c.x==12 and c.y==12 and c.input.attack and c.pressed.attack) "
        "entity.change_value(h,'player_contacts',1) if c.role=='hitbox' then map.defeat_player(c.player) end end)";
    VelocityHost native={0};MapScriptHost host={0};MapScriptDefinition d=definition(source);
    native.available=3;
    for(uint32_t i=0;i<2;i++){native.players[i].object_id=i;native.players[i].object_kind=MAP_SCRIPT_OBJECT_PLAYER;native.players[i].x=i?100:12;native.players[i].y=i?100:12;}
    native.observations[0].command_bits=MAP_SCRIPT_COMMAND_ATTACK;
    host.userdata=&native;host.read_player_fn=velocity_read;host.read_player_observation_fn=velocity_observation;host.commit_players_fn=player_commit;
    assert(map_script_activate_content(&d,&host,json,strlen(json),error,sizeof(error)));
    size_t bytes=map_script_content_snapshot_size();unsigned char *start=malloc(bytes),*end=malloc(bytes),*replay=malloc(bytes);assert(start&&end&&replay);
    assert(map_script_content_snapshot_save(start,bytes,error,sizeof(error)));
    assert(map_script_dispatch_tick(error,sizeof(error))&&native.available==2&&native.commits==1);
    assert(map_script_content_snapshot_save(end,bytes,error,sizeof(error)));
    {
        const MapScriptObjectStateSnapshot* state=(const MapScriptObjectStateSnapshot*)(end+bytes-sizeof(MapScriptObjectStateSnapshot));
        int entity_contacts=0,player_contacts=0,health=0;
        for(uint32_t i=0;i<MAP_SCRIPT_MAX_OBJECT_STATE_ENTRIES;i++)if(state->entries[i].in_use){
            if(state->entries[i].key_len==15&&!memcmp(state->entries[i].key,"entity_contacts",15)){assert(state->entries[i].number_value==1);entity_contacts++;}
            if(state->entries[i].key_len==15&&!memcmp(state->entries[i].key,"player_contacts",15)){assert(state->entries[i].number_value==2);player_contacts++;}
            if(state->entries[i].key_len==6&&!memcmp(state->entries[i].key,"health",6)){assert(state->entries[i].number_value==7);health++;}
        }
        assert(entity_contacts==1&&player_contacts==1&&health==1);
    }
    assert(map_script_content_snapshot_load(start,bytes,error,sizeof(error)));native.available=3;native.commits=0;
    assert(map_script_dispatch_tick(error,sizeof(error))&&native.available==2&&native.commits==1);
    assert(map_script_content_snapshot_save(replay,bytes,error,sizeof(error))&&!memcmp(end,replay,bytes));
    d=definition("entity.on_player_contact('demo:blade',function(h,c) entity.set_value(h,'bad',1) map.defeat_player(c.player) error('contact abort') end)");
    native.available=3;native.commits=0;assert(map_script_activate_content(&d,&host,json,strlen(json),error,sizeof(error)));
    assert(map_script_content_snapshot_save(start,bytes,error,sizeof(error)));
    assert(!map_script_dispatch_tick(error,sizeof(error))&&strstr(error,"contact abort")&&native.available==3&&native.commits==0);
    assert(map_script_content_snapshot_save(replay,bytes,error,sizeof(error)));
    assert(!memcmp(start+MAP_SCRIPT_CONTENT_HEADER_BYTES+sizeof(MapScriptSnapshot),
                   replay+MAP_SCRIPT_CONTENT_HEADER_BYTES+sizeof(MapScriptSnapshot),
                   bytes-MAP_SCRIPT_CONTENT_HEADER_BYTES-sizeof(MapScriptSnapshot)));
    d=definition("entity.on_contact('demo:blade',function() end) entity.on_contact('demo:blade',function() end)");
    assert(!map_script_activate_content(&d,&host,json,strlen(json),error,sizeof(error))&&strstr(error,"duplicate"));
    d=definition("entity.on_damage('demo:target',function() end) entity.on_damage('demo:target',function() end)");
    assert(!map_script_activate_content(&d,&host,json,strlen(json),error,sizeof(error))&&strstr(error,"duplicate"));
    d=definition("entity.on_damage('demo:target',function(h,event) entity.set_value(h,'bad',event.amount) error('damage abort') end) map.on_tick(function() entity.damage(entity.find('target'),2,entity.find('blade')) end)");
    assert(map_script_activate_content(&d,&host,json,strlen(json),error,sizeof(error)));
    assert(map_script_content_snapshot_save(start,bytes,error,sizeof(error)));
    assert(!map_script_dispatch_tick(error,sizeof(error))&&strstr(error,"damage abort"));
    assert(map_script_content_snapshot_save(replay,bytes,error,sizeof(error)));
    assert(!memcmp(start+MAP_SCRIPT_CONTENT_HEADER_BYTES+sizeof(MapScriptSnapshot),
                   replay+MAP_SCRIPT_CONTENT_HEADER_BYTES+sizeof(MapScriptSnapshot),
                   bytes-MAP_SCRIPT_CONTENT_HEADER_BYTES-sizeof(MapScriptSnapshot)));
    d=definition("entity.damage('bad',1)");
    assert(!map_script_activate_content(&d,&host,json,strlen(json),error,sizeof(error))&&strstr(error,"gameplay callbacks"));
    d=definition("entity.on_signal('demo:target',function(h,event) assert(event.name=='equip' and event.value=='sword' and event.source==entity.find('blade') and event.source_type=='demo:blade') entity.set_value(h,'message',event.value) end) map.on_tick(function() assert(entity.signal(entity.find('target'),'equip','sword',entity.find('blade'))) assert(not entity.signal(entity.find('blade'),'ignored')) end)");
    assert(map_script_activate_content(&d,&host,json,strlen(json),error,sizeof(error)));
    assert(map_script_content_snapshot_save(start,bytes,error,sizeof(error)));assert(map_script_dispatch_tick(error,sizeof(error)));assert(map_script_content_snapshot_save(end,bytes,error,sizeof(error)));
    {
        const MapScriptObjectStateSnapshot* state=(const MapScriptObjectStateSnapshot*)(end+bytes-sizeof(MapScriptObjectStateSnapshot));int message=0;
        for(uint32_t i=0;i<MAP_SCRIPT_MAX_OBJECT_STATE_ENTRIES;i++)if(state->entries[i].in_use&&!strcmp(state->entries[i].key,"message")){assert(state->entries[i].type==MAP_SCRIPT_STATE_STRING&&!strcmp(state->entries[i].string_value,"sword"));message++;}
        assert(message==1);
    }
    assert(map_script_content_snapshot_load(start,bytes,error,sizeof(error)));assert(map_script_dispatch_tick(error,sizeof(error)));assert(map_script_content_snapshot_save(replay,bytes,error,sizeof(error))&&!memcmp(end,replay,bytes));
    d=definition("entity.on_signal('demo:target',function(h,event) entity.set_value(h,'bad',true) error('signal abort') end) map.on_tick(function() entity.signal(entity.find('target'),'fail',3) end)");
    assert(map_script_activate_content(&d,&host,json,strlen(json),error,sizeof(error)));assert(map_script_content_snapshot_save(start,bytes,error,sizeof(error)));
    assert(!map_script_dispatch_tick(error,sizeof(error))&&strstr(error,"signal abort"));assert(map_script_content_snapshot_save(replay,bytes,error,sizeof(error)));
    assert(!memcmp(start+MAP_SCRIPT_CONTENT_HEADER_BYTES+sizeof(MapScriptSnapshot),replay+MAP_SCRIPT_CONTENT_HEADER_BYTES+sizeof(MapScriptSnapshot),bytes-MAP_SCRIPT_CONTENT_HEADER_BYTES-sizeof(MapScriptSnapshot)));
    d=definition("entity.on_signal('demo:target',function() end) entity.on_signal('demo:target',function() end)");
    assert(!map_script_activate_content(&d,&host,json,strlen(json),error,sizeof(error))&&strstr(error,"duplicate"));
    d=definition("entity.player_contacts()");
    assert(!map_script_activate_content(&d,&host,json,strlen(json),error,sizeof(error))&&strstr(error,"gameplay callbacks"));
    free(start);free(end);free(replay);map_script_deactivate();
}
static void test_damage_example(void) {
    char source[8192],json[8192];size_t source_length,json_length;
    FILE* file=fopen("docs/examples/entity_damage.lua","rb");assert(file);
    source_length=fread(source,1,sizeof(source)-1,file);assert(!ferror(file)&&feof(file));fclose(file);source[source_length]=0;
    file=fopen("docs/examples/entity_damage.json","rb");assert(file);
    json_length=fread(json,1,sizeof(json)-1,file);assert(!ferror(file)&&feof(file));fclose(file);json[json_length]=0;
    MapScriptDefinition d=definition(source);d.entity_layout=(EntityPackageLayout){.count=1,.rooms={"center"}};
    assert(map_script_activate_content(&d,NULL,json,json_length,error,sizeof(error)));
    size_t bytes=map_script_content_snapshot_size(),offset=MAP_SCRIPT_CONTENT_HEADER_BYTES+sizeof(MapScriptSnapshot);
    unsigned char *start=malloc(bytes),*end=malloc(bytes),*replay=malloc(bytes);assert(start&&end&&replay);
    assert(map_script_content_snapshot_save(start,bytes,error,sizeof(error)));
    for(int pass=0;pass<2;pass++) {
        if(pass)assert(map_script_content_snapshot_load(start,bytes,error,sizeof(error)));
        for(int tick=0;tick<40;tick++)assert(map_script_dispatch_tick(error,sizeof(error)));
        assert(map_script_content_snapshot_save(pass?replay:end,bytes,error,sizeof(error)));
        assert(map_script_content_snapshot_validate(pass?replay:end,bytes,error,sizeof(error)));
    }
    assert(!memcmp(end,replay,bytes));
    EntityPackage* decoded=entity_package_decode_layout(json,json_length,&d.entity_layout,error,sizeof(error));assert(decoded);
    assert(entity_package_load(decoded,end+offset,entity_package_snapshot_size(decoded)));
    assert(entity_world_count(entity_package_world(decoded))==1);
    EntityValue value;assert(entity_world_read(entity_package_world(decoded),entity_package_placement(decoded,"blade"),&value));
    assert(!entity_world_read(entity_package_world(decoded),entity_package_placement(decoded,"crate"),&value));
    entity_package_free(decoded);free(start);free(end);free(replay);map_script_deactivate();
}
static void test_projectile_example(void) {
    char source[8192],json[8192];size_t source_length,json_length;FILE* file=fopen("docs/examples/entity_projectile.lua","rb");assert(file);
    source_length=fread(source,1,sizeof(source)-1,file);assert(!ferror(file)&&feof(file));fclose(file);source[source_length]=0;
    file=fopen("docs/examples/entity_projectile.json","rb");assert(file);json_length=fread(json,1,sizeof(json)-1,file);assert(!ferror(file)&&feof(file));fclose(file);json[json_length]=0;
    MapScriptDefinition d=definition(source);d.entity_layout=(EntityPackageLayout){.count=1,.rooms={"center"}};
    assert(map_script_activate_content(&d,NULL,json,json_length,error,sizeof(error)));
    size_t bytes=map_script_content_snapshot_size(),offset=MAP_SCRIPT_CONTENT_HEADER_BYTES+sizeof(MapScriptSnapshot);unsigned char *start=malloc(bytes),*end=malloc(bytes),*replay=malloc(bytes);assert(start&&end&&replay);
    assert(map_script_content_snapshot_save(start,bytes,error,sizeof(error)));
    for(int pass=0;pass<2;pass++){
        if(pass)assert(map_script_content_snapshot_load(start,bytes,error,sizeof(error)));
        for(int tick=0;tick<80;tick++)assert(map_script_dispatch_tick(error,sizeof(error)));
        assert(map_script_content_snapshot_save(pass?replay:end,bytes,error,sizeof(error)));
    }
    assert(!memcmp(end,replay,bytes));
    EntityPackage* decoded=entity_package_decode_layout(json,json_length,&d.entity_layout,error,sizeof(error));assert(decoded);assert(entity_package_load(decoded,end+offset,entity_package_snapshot_size(decoded)));
    assert(entity_world_count(entity_package_world(decoded))==2);
    EntityHandle launcher=entity_package_placement(decoded,"launcher"),target=entity_package_placement(decoded,"target");EntityValue value;
    assert(entity_world_read(entity_package_world(decoded),launcher,&value)&&entity_world_read(entity_package_world(decoded),target,&value));
    const MapScriptObjectStateSnapshot* state=(const MapScriptObjectStateSnapshot*)(end+bytes-sizeof(MapScriptObjectStateSnapshot));int fired=0,health=0,projectile_value=0;
    for(uint32_t i=0;i<MAP_SCRIPT_MAX_OBJECT_STATE_ENTRIES;i++)if(state->entries[i].in_use){
        if(state->entries[i].handle==launcher&&!strcmp(state->entries[i].key,"fired")){assert(state->entries[i].type==MAP_SCRIPT_STATE_BOOL&&state->entries[i].bool_value);fired++;}
        else if(state->entries[i].handle==target&&!strcmp(state->entries[i].key,"health")){assert(state->entries[i].number_value==6);health++;}
        else if(state->entries[i].handle!=launcher&&state->entries[i].handle!=target)projectile_value++;
    }
    assert(fired==1&&health==1&&projectile_value==0);
    entity_package_free(decoded);free(start);free(end);free(replay);map_script_deactivate();
}
static void test_signal_example(void) {
    char source[8192],json[8192];size_t source_length,json_length;FILE* file=fopen("docs/examples/entity_signal.lua","rb");assert(file);
    source_length=fread(source,1,sizeof(source)-1,file);assert(!ferror(file)&&feof(file));fclose(file);source[source_length]=0;
    file=fopen("docs/examples/entity_signal.json","rb");assert(file);json_length=fread(json,1,sizeof(json)-1,file);assert(!ferror(file)&&feof(file));fclose(file);json[json_length]=0;
    MapScriptDefinition d=definition(source);d.entity_layout=(EntityPackageLayout){.count=1,.rooms={"center"}};
    assert(map_script_activate_content(&d,NULL,json,json_length,error,sizeof(error)));
    size_t bytes=map_script_content_snapshot_size(),offset=MAP_SCRIPT_CONTENT_HEADER_BYTES+sizeof(MapScriptSnapshot);unsigned char *start=malloc(bytes),*end=malloc(bytes),*replay=malloc(bytes);assert(start&&end&&replay);
    assert(map_script_content_snapshot_save(start,bytes,error,sizeof(error)));assert(map_script_dispatch_tick(error,sizeof(error)));assert(map_script_content_snapshot_save(end,bytes,error,sizeof(error)));
    assert(map_script_content_snapshot_load(start,bytes,error,sizeof(error)));assert(map_script_dispatch_tick(error,sizeof(error)));assert(map_script_content_snapshot_save(replay,bytes,error,sizeof(error)));assert(!memcmp(end,replay,bytes));
    EntityPackage* decoded=entity_package_decode_layout(json,json_length,&d.entity_layout,error,sizeof(error));assert(decoded);assert(entity_package_load(decoded,end+offset,entity_package_snapshot_size(decoded)));
    EntityHandle switch_handle=entity_package_placement(decoded,"switch"),door_handle=entity_package_placement(decoded,"door");EntityValue value;assert(entity_world_read(entity_package_world(decoded),door_handle,&value)&&(value.flags&ENTITY_FLAG_HIDDEN));
    const MapScriptObjectStateSnapshot* state=(const MapScriptObjectStateSnapshot*)(end+bytes-sizeof(MapScriptObjectStateSnapshot));int used=0,amount=0;
    for(uint32_t i=0;i<MAP_SCRIPT_MAX_OBJECT_STATE_ENTRIES;i++)if(state->entries[i].in_use){
        if(state->entries[i].handle==switch_handle&&!strcmp(state->entries[i].key,"used")){assert(state->entries[i].type==MAP_SCRIPT_STATE_BOOL&&state->entries[i].bool_value);used++;}
        if(state->entries[i].handle==door_handle&&!strcmp(state->entries[i].key,"open_amount")){assert(state->entries[i].type==MAP_SCRIPT_STATE_NUMBER&&state->entries[i].number_value==2);amount++;}
    }
    assert(used==1&&amount==1);entity_package_free(decoded);free(start);free(end);free(replay);map_script_deactivate();
}
static void test_named_animations(void) {
    const char* json="{\"schema\":1,\"capacity\":4,\"types\":[{\"key\":\"demo:door\",\"regions\":[],\"visual\":{\"sheet\":\"builtin:tiles\",\"sprite\":4},\"animations\":[{\"name\":\"open\",\"sprite\":12,\"frames\":3,\"frame_ticks\":2,\"mode\":\"once\"}]}],\"placements\":[{\"name\":\"door\",\"type\":\"demo:door\"}]}";
    MapScriptDefinition d=definition("entity.on_animation_finish('demo:door',function(h,s) assert(s.name=='open' and s.frame==2 and s.finished and s.tick==6) entity.change_value(h,'completed',1) entity.play_animation(h,'default') end) map.on_tick(function() local h=entity.find('door') if map.tick()==0 then assert(entity.animation(h)=='default') entity.play_animation(h,'open') elseif map.tick()==1 then local s=entity.animation_status(h) assert(s.name=='open' and s.frame==0 and s.frames==3 and s.mode=='once' and not s.finished) entity.play_animation(h,'open',false) elseif map.tick()==6 then assert(entity.animation(h)=='default' and entity.value(h,'completed')==1) end end)");
    assert(map_script_activate_content(&d,NULL,json,strlen(json),error,sizeof(error)));size_t bytes=map_script_content_snapshot_size(),offset=MAP_SCRIPT_CONTENT_HEADER_BYTES+sizeof(MapScriptSnapshot);unsigned char *start=malloc(bytes),*end=malloc(bytes),*replay=malloc(bytes);assert(start&&end&&replay);
    assert(map_script_content_snapshot_save(start,bytes,error,sizeof(error)));for(int tick=0;tick<7;++tick)assert(map_script_dispatch_tick(error,sizeof(error)));assert(map_script_content_snapshot_save(end,bytes,error,sizeof(error)));
    EntityPackage* decoded=entity_package_decode(json,strlen(json),error,sizeof(error));assert(decoded&&entity_package_load(decoded,end+offset,entity_package_snapshot_size(decoded)));EntityValue value;assert(entity_world_read(entity_package_world(decoded),entity_package_placement(decoded,"door"),&value)&&value.animation_id==0&&value.animation_tick==1);
    assert(map_script_content_snapshot_load(start,bytes,error,sizeof(error)));for(int tick=0;tick<7;++tick)assert(map_script_dispatch_tick(error,sizeof(error)));assert(map_script_content_snapshot_save(replay,bytes,error,sizeof(error))&&!memcmp(end,replay,bytes));
    entity_package_free(decoded);free(start);free(end);free(replay);map_script_deactivate();
    d=definition("map.on_tick(function() entity.play_animation(entity.find('door'),'missing') end)");assert(map_script_activate_content(&d,NULL,json,strlen(json),error,sizeof(error)));assert(!map_script_dispatch_tick(error,sizeof(error))&&strstr(error,"unknown animation"));map_script_deactivate();
    d=definition("entity.on_animation_finish('demo:door',function() error('finish abort') end) map.on_tick(function() if map.tick()==0 then entity.play_animation(entity.find('door'),'open') end end)");assert(map_script_activate_content(&d,NULL,json,strlen(json),error,sizeof(error)));for(int tick=0;tick<5;++tick)assert(map_script_dispatch_tick(error,sizeof(error)));assert(!map_script_dispatch_tick(error,sizeof(error))&&strstr(error,"finish abort"));map_script_deactivate();
}
static void test_managed_object_health(void) {
    const char* source=
        "entity.on_spawn('demo:orb',function(h) entity.enable_health(h,10,8) assert(entity.health(h)==8,'spawn health') end) "
        "entity.on_damage_filter('demo:orb',function(h,e) assert(e.amount>0 and e.source==nil and e.source_type==nil) return map.tick()~=1 end) "
        "entity.on_damage('demo:orb',function(h,e) if map.tick()==0 then assert(e.old_health==8 and e.health==5 and e.applied==3 and not e.blocked and e.max_health==10 and not e.defeated,'first damage') elseif map.tick()==1 then assert(e.health==7 and e.applied==0 and e.blocked and not e.invulnerable,'filtered damage') else assert(e.old_health==1 and e.health==0 and e.max_health==6 and e.defeated,'lethal damage') end entity.set_value(h,'damage_seen',e.amount) end) "
        "entity.on_defeated('demo:orb',function(h,e) assert(e.health==0 and e.max_health==6,'defeat event') entity.set_value(h,'defeated_seen',true) end) "
        "map.on_tick(function() local h=entity.find('orb') if map.tick()==0 then assert(entity.damage(h,3),'damage handled') assert(entity.heal(h,2)==2,'heal') assert(entity.health(h)==7,'post heal') elseif map.tick()==1 then assert(entity.damage(h,100) and entity.health(h)==7) entity.set_max_health(h,6) assert(entity.health(h)==6,'max clamp') entity.set_health(h,1) elseif map.tick()==2 then assert(entity.damage(h,4),'lethal handled') assert(entity.value(h,'defeated_seen'),'defeat seen') local keys=entity.value_keys(h) assert(#keys==2,'hidden keys') elseif map.tick()==3 then entity.disable_health(h) assert(entity.health(h)==nil and entity.invulnerable(h)==nil) end end)";
    MapScriptDefinition d=definition(source);assert(map_script_activate_content(&d,NULL,package,strlen(package),error,sizeof(error)));
    size_t bytes=map_script_content_snapshot_size();unsigned char *start=malloc(bytes),*end=malloc(bytes),*replay=malloc(bytes);assert(start&&end&&replay);
    assert(map_script_content_snapshot_save(start,bytes,error,sizeof(error)));
    for(int pass=0;pass<2;pass++) {
        if(pass)assert(map_script_content_snapshot_load(start,bytes,error,sizeof(error)));
        for(int tick=0;tick<4;tick++)assert(map_script_dispatch_tick(error,sizeof(error)));
        assert(map_script_content_snapshot_save(pass?replay:end,bytes,error,sizeof(error)));
    }
    assert(!memcmp(end,replay,bytes));free(start);free(end);free(replay);map_script_deactivate();
    d=definition("entity.on_spawn('demo:orb',function(h) entity.enable_health(h,2) end) entity.on_damage_filter('demo:orb',function() return 1 end) map.on_tick(function() entity.damage(entity.find('orb'),1) end)");
    assert(map_script_activate_content(&d,NULL,package,strlen(package),error,sizeof(error)));
    assert(!map_script_dispatch_tick(error,sizeof(error))&&strstr(error,"damage filter must return true"));map_script_deactivate();
}

static void test_native_attack_damage_bridge(void) {
    const char* combat_package="{\"schema\":1,\"capacity\":4,\"types\":[{\"key\":\"demo:target\",\"regions\":[{\"id\":1,\"name\":\"body\",\"role\":\"body\",\"layer\":1,\"mask\":1,\"x\":-8,\"y\":-8,\"width\":16,\"height\":16}]}],\"placements\":[{\"name\":\"target\",\"type\":\"demo:target\",\"x\":32,\"y\":32}]}";
    const char* source=
        "entity.on_spawn('demo:target',function(h) entity.enable_health(h,200) end) "
        "entity.on_damage_filter('demo:target',function(h,e) assert(e.source==nil and string.sub(e.source_type,1,7)=='native:') if e.source_type=='native:mine' then assert(e.source_player==nil,'mine owner') else assert(e.source_player==1,'player owner '..e.source_type) end return e.source_type~='native:mine' end) "
        "entity.on_damage('demo:target',function(h,e) entity.set_value(h,'last_amount',e.amount) entity.set_value(h,'last_source',e.source_type) end) "
        "entity.on_update('demo:target',function(h) local hp=entity.health(h) if map.tick()==0 then assert(hp==188 and entity.was_hit_by_player(h,1)) elseif map.tick()==1 then assert(hp==188 and not entity.was_hit_by_player(h,1)) elseif map.tick()==2 then assert(hp==188) elseif map.tick()==3 then assert(hp==163 and entity.was_hit_by_player(h,1) and entity.value(h,'last_source')=='native:kick') elseif map.tick()==4 then assert(hp==163 and not entity.was_hit_by_player(h,1)) end end)";
    MapScriptDefinition d=definition(source);assert(map_script_activate_content(&d,NULL,combat_package,strlen(combat_package),error,sizeof(error)));
    size_t bytes=map_script_content_snapshot_size();unsigned char *initial=malloc(bytes),*end=malloc(bytes),*replay=malloc(bytes);assert(initial&&end&&replay);
    assert(map_script_content_snapshot_save(initial,bytes,error,sizeof(error)));
    for(int pass=0;pass<2;pass++) {
        if(pass)assert(map_script_content_snapshot_load(initial,bytes,error,sizeof(error)));
        MapScriptNativeAttackProbe probe={32,32,4,0,MAP_SCRIPT_NATIVE_DAMAGE_PUNCH,{0,0}};
        assert(map_script_submit_native_attack_probe(&probe));assert(map_script_dispatch_tick(error,sizeof(error)));
        assert(map_script_submit_native_attack_probe(&probe));assert(map_script_dispatch_tick(error,sizeof(error)));
        assert(map_script_dispatch_tick(error,sizeof(error)));
        probe.kind=MAP_SCRIPT_NATIVE_DAMAGE_KICK;assert(map_script_submit_native_attack_probe(&probe));assert(map_script_dispatch_tick(error,sizeof(error)));
        probe.kind=MAP_SCRIPT_NATIVE_DAMAGE_MINE;probe.player_slot=0xff;assert(map_script_submit_native_attack_probe(&probe));assert(map_script_dispatch_tick(error,sizeof(error)));
        assert(map_script_content_snapshot_save(pass?replay:end,bytes,error,sizeof(error)));
    }
    assert(!memcmp(end,replay,bytes));free(initial);free(end);free(replay);map_script_deactivate();
}
static void test_native_mine_knockback(void) {
    const char* combat_package="{\"schema\":1,\"capacity\":4,\"types\":[{\"key\":\"demo:target\",\"regions\":[]}],\"placements\":[{\"name\":\"target\",\"type\":\"demo:target\",\"x\":32,\"y\":32}]}";
    MapScriptDefinition d=definition(
        "entity.on_spawn('demo:target',function(h) entity.set(h,{automatic_motion=false}) entity.set_mine_knockback(h,true) assert(entity.mine_knockback(h)) end) "
        "map.on_tick(function() local h=entity.find('target') if map.tick()==0 then local o=entity.get(h) assert(o.vx==0 and o.vy==-1533/256,'native mine force') entity.set_mine_knockback(h,false) assert(not entity.mine_knockback(h)) end end)");
    assert(map_script_activate_content(&d,NULL,combat_package,strlen(combat_package),error,sizeof(error)));
    size_t bytes=map_script_content_snapshot_size();unsigned char *initial=malloc(bytes),*end=malloc(bytes),*replay=malloc(bytes);assert(initial&&end&&replay);
    assert(map_script_content_snapshot_save(initial,bytes,error,sizeof(error)));
    for(int pass=0;pass<2;pass++) {
        if(pass)assert(map_script_content_snapshot_load(initial,bytes,error,sizeof(error)));
        MapScriptNativeAttackProbe probe={32,32,24,0xff,MAP_SCRIPT_NATIVE_DAMAGE_MINE,{0,0}};
        assert(map_script_submit_native_attack_probe(&probe));assert(map_script_dispatch_tick(error,sizeof(error)));
        assert(map_script_content_snapshot_save(pass?replay:end,bytes,error,sizeof(error)));
    }
    assert(!memcmp(end,replay,bytes));free(initial);free(end);free(replay);map_script_deactivate();
}
static void test_managed_player_health(void) {
    const char* source=
        "map.on_player_damage(function(p,e) assert(p==1 and e.reason=='damage' and e.amount>0 and e.applied>=0) if map.tick()==1 then assert(e.blocked and e.invulnerable and e.applied==0) end map.state.damage=(map.state.damage or 0)+1 end) "
        "map.on_player_health_changed(function(p,e) assert(p==1 and e.max_health>0) map.state.changed=(map.state.changed or 0)+1 end) "
        "map.on_player_defeated(function(p,e) assert(p==1 and e.health==0 and e.defeated) map.state.defeated=(map.state.defeated or 0)+1 end) "
        "map.on_tick(function() if map.tick()==0 then map.enable_player_health(1,10) assert(map.damage_player(1,3)==3) local h,m=map.player_health(1) assert(h==7 and m==10 and map.players()[1].health==7 and not map.players()[1].invulnerable) elseif map.tick()==1 then map.set_player_invulnerable(1,true) assert(map.player_invulnerable(1) and map.damage_player(1,99)==0 and map.player_health(1)==7) map.set_player_invulnerable(1,false) assert(map.heal_player(1,1)==1) elseif map.tick()==2 then map.set_player_max_health(1,6) elseif map.tick()==3 then assert(map.damage_player(1,20)==6) end end)";
    VelocityHost native={0};MapScriptHost host={0};native.available=1;
    native.players[0].object_id=0;native.players[0].object_kind=MAP_SCRIPT_OBJECT_PLAYER;
    host.userdata=&native;host.read_player_fn=velocity_read;host.commit_players_fn=player_commit;
    MapScriptDefinition d=definition(source);assert(map_script_activate_content(&d,&host,package,strlen(package),error,sizeof(error)));
    size_t bytes=map_script_content_snapshot_size();unsigned char *start=malloc(bytes),*end=malloc(bytes),*replay=malloc(bytes);assert(start&&end&&replay);
    assert(map_script_content_snapshot_save(start,bytes,error,sizeof(error)));
    for(int pass=0;pass<2;pass++) {
        if(pass){assert(map_script_content_snapshot_load(start,bytes,error,sizeof(error)));native.available=1;native.commits=0;}
        for(int tick=0;tick<4;tick++)assert(map_script_dispatch_tick(error,sizeof(error)));
        assert(native.available==0&&native.commits==1);
        assert(map_script_content_snapshot_save(pass?replay:end,bytes,error,sizeof(error)));
    }
    assert(!memcmp(end,replay,bytes));
    { MapScriptSnapshot snapshot;memcpy(&snapshot,end+MAP_SCRIPT_CONTENT_HEADER_BYTES,sizeof(snapshot));
      assert(snapshot.player_health[0].enabled&&snapshot.player_health[0].health==0&&snapshot.player_health[0].max_health==6); }
    native.available=1;assert(map_script_dispatch_tick(error,sizeof(error)));{
      MapScriptSnapshot snapshot;assert(map_script_content_snapshot_save(replay,bytes,error,sizeof(error)));memcpy(&snapshot,replay+MAP_SCRIPT_CONTENT_HEADER_BYTES,sizeof(snapshot));
      assert(snapshot.player_health[0].health==6&&!snapshot.player_health[0].awaiting_respawn);}
    free(start);free(end);free(replay);map_script_deactivate();
}
static void test_player_sprite_override(void) {
    const char* json="{\"schema\":1,\"capacity\":4,\"types\":[{\"key\":\"demo:hero\",\"regions\":[],\"visual\":{\"sheet\":\"builtin:tiles\",\"sprite\":4},\"animations\":[{\"name\":\"jump\",\"sprite\":10,\"frames\":2,\"frame_ticks\":1,\"mode\":\"once\"}]}],\"placements\":[]}";
    const char* source="map.on_tick(function() if map.tick()<3 then map.set_player_sprite(1,'demo:hero','jump',false) if map.tick()==0 then map.set_player_sprite_transform(1,{scale_x=2,scale_y=.5,offset_x=3,offset_y=-4,rotation=90,tint='#80FFFFFF',draw_layer='front',animation_speed=2,mirrored=true}) elseif map.tick()==1 then map.set_player_sprite_transform(1,{visible=false}) end local picture=map.player_sprite(1) local player=map.players()[1] assert(picture.type=='demo:hero' and picture.animation=='jump' and picture.tick==map.tick()*2 and picture.scale_x==-2 and picture.scale_y==.5 and picture.offset_x==3 and picture.offset_y==-4 and picture.rotation==-90 and picture.tint=='#80ffffff' and picture.layer==1 and picture.animation_speed==2 and picture.mirrored and picture.visible==(map.tick()==0) and not picture.restore and picture.mode=='once' and picture.frames==2 and picture.frame==(map.tick()==0 and 0 or 1) and picture.finished==(map.tick()>0) and picture.just_finished==(map.tick()==1) and player.custom_sprite_type=='demo:hero' and player.custom_sprite_animation=='jump' and player.custom_sprite_cell==picture.sprite and player.custom_sprite_tick==picture.tick and player.custom_sprite_frame==picture.frame and player.custom_sprite_frames==picture.frames and player.custom_sprite_finished==picture.finished and player.custom_sprite_just_finished==picture.just_finished and not player.custom_sprite_restore and #map.state_keys()==0) elseif map.tick()==3 then map.clear_player_sprite(1) assert(map.player_sprite(1)==nil and map.players()[1].custom_sprite_type==nil) elseif map.tick()==4 then map.set_player_sprite(1,'demo:hero','jump',true,true) assert(not map.player_sprite(1).finished and map.player_sprite(1).restore and map.players()[1].custom_sprite_restore) elseif map.tick()==5 then assert(not map.player_sprite(1).finished) elseif map.tick()==6 then assert(map.player_sprite(1).just_finished) end end)";
    VelocityHost native={0};MapScriptHost host={0};native.available=1;native.players[0].object_id=0;native.players[0].object_kind=MAP_SCRIPT_OBJECT_PLAYER;native.players[0].x=40;native.players[0].y=60;
    host.userdata=&native;host.read_player_fn=velocity_read;host.commit_players_fn=player_commit;MapScriptDefinition d=definition(source);
    if(!map_script_activate_content(&d,&host,json,strlen(json),error,sizeof(error))){fprintf(stderr,"player sprite activation: %s\n",error);assert(0);}
    size_t bytes=map_script_content_snapshot_size();unsigned char *start=malloc(bytes),*end=malloc(bytes),*replay=malloc(bytes),*bad=malloc(bytes);assert(start&&end&&replay&&bad);assert(map_script_content_snapshot_save(start,bytes,error,sizeof(error)));
    assert(map_script_dispatch_tick(error,sizeof(error)));{MapScriptPlayerSprite sprite;assert(map_script_player_sprite(0,&sprite)&&!strcmp(sprite.sheet,"builtin:tiles")&&sprite.sprite==11&&sprite.x==40&&sprite.y==60&&sprite.scale_x==-512&&sprite.scale_y==128&&sprite.offset_x==768&&sprite.offset_y==-1024&&sprite.rotation==-23040&&sprite.layer==1&&sprite.rgba==UINT32_C(0x80ffffff)&&sprite.visible);}
    assert(map_script_dispatch_tick(error,sizeof(error)));{MapScriptPlayerSprite sprite;assert(map_script_player_sprite(0,&sprite)&&sprite.sprite==11&&!sprite.visible);}
    assert(map_script_dispatch_tick(error,sizeof(error)));assert(map_script_content_snapshot_save(end,bytes,error,sizeof(error)));
    memcpy(bad,end,bytes);{
      MapScriptSnapshot* snapshot=(MapScriptSnapshot*)(bad+MAP_SCRIPT_CONTENT_HEADER_BYTES);int changed=0;
      for(uint32_t i=0;i<MAP_SCRIPT_MAX_STATE_ENTRIES;i++)if(snapshot->state[i].in_use&&snapshot->state[i].key_len==21&&!memcmp(snapshot->state[i].key,"__yule_p1_sprite_type",21)){snapshot->state[i].number_value=UINT32_MAX;snapshot->checksum=map_state_crc(snapshot);changed=1;break;}
      assert(changed&&!map_script_content_snapshot_validate(bad,bytes,error,sizeof(error))&&strstr(error,"pinned entity catalog"));
      assert(!map_script_content_snapshot_load(bad,bytes,error,sizeof(error)));assert(map_script_content_snapshot_save(replay,bytes,error,sizeof(error))&&!memcmp(end,replay,bytes));
    }
    assert(map_script_content_snapshot_load(start,bytes,error,sizeof(error)));assert(map_script_dispatch_tick(error,sizeof(error)));assert(map_script_dispatch_tick(error,sizeof(error)));assert(map_script_dispatch_tick(error,sizeof(error)));assert(map_script_content_snapshot_save(replay,bytes,error,sizeof(error))&&!memcmp(end,replay,bytes));
    assert(map_script_dispatch_tick(error,sizeof(error)));{MapScriptPlayerSprite sprite;MapScriptPlayerPresentation presentation;assert(!map_script_player_sprite(0,&sprite));assert(!map_script_player_presentation(0,&presentation));}
    assert(map_script_dispatch_tick(error,sizeof(error)));{MapScriptPlayerSprite sprite;assert(map_script_player_sprite(0,&sprite));}
    assert(map_script_dispatch_tick(error,sizeof(error)));{MapScriptPlayerSprite sprite;assert(map_script_player_sprite(0,&sprite));}assert(map_script_content_snapshot_save(start,bytes,error,sizeof(error)));
    assert(map_script_dispatch_tick(error,sizeof(error)));{MapScriptPlayerSprite sprite;assert(!map_script_player_sprite(0,&sprite));}assert(map_script_content_snapshot_save(end,bytes,error,sizeof(error)));
    assert(map_script_content_snapshot_load(start,bytes,error,sizeof(error)));assert(map_script_dispatch_tick(error,sizeof(error)));assert(map_script_content_snapshot_save(replay,bytes,error,sizeof(error))&&!memcmp(end,replay,bytes));
    free(start);free(end);free(replay);free(bad);map_script_deactivate();
    d=definition("map.on_tick(function() map.set_player_sprite(1,'demo:hero','default',false,true) end)");
    assert(map_script_activate_content(&d,&host,json,strlen(json),error,sizeof(error)));
    assert(!map_script_dispatch_tick(error,sizeof(error))&&strstr(error,"requires a once animation"));map_script_deactivate();
    d=definition("map.on_tick(function() map.set_player_sprite(1,'demo:hero','jump',false,'yes') end)");
    assert(map_script_activate_content(&d,&host,json,strlen(json),error,sizeof(error)));
    assert(!map_script_dispatch_tick(error,sizeof(error))&&strstr(error,"restore must be boolean"));map_script_deactivate();
}
static void test_player_lifecycle_observations(void) {
    const char* source="map.on_tick(function() local p=map.players()[1] if map.tick()==0 then assert(p.spawned and not p.respawned and not p.room_changed and p.previous_room==p.room) elseif map.tick()==1 then assert(not p.spawned and not p.respawned and not p.room_changed) elseif map.tick()==2 then assert(not p.spawned and p.respawned and p.room_changed and p.previous_room==0 and p.room==1) end end)";
    VelocityHost native={0};MapScriptHost host={0};native.available=1;native.players[0].object_id=0;native.players[0].object_kind=MAP_SCRIPT_OBJECT_PLAYER;native.observations[0].native_state=1;native.observations[0].room=0;
    host.userdata=&native;host.read_player_fn=velocity_read;host.read_player_observation_fn=velocity_observation;host.commit_players_fn=player_commit;
    MapScriptDefinition d=definition(source);assert(map_script_activate_content(&d,&host,package,strlen(package),error,sizeof(error)));size_t bytes=map_script_content_snapshot_size();unsigned char *before=malloc(bytes),*end=malloc(bytes),*replay=malloc(bytes);assert(before&&end&&replay);
    assert(map_script_dispatch_tick(error,sizeof(error)));native.observations[0].native_state=9;assert(map_script_dispatch_tick(error,sizeof(error)));assert(map_script_content_snapshot_save(before,bytes,error,sizeof(error)));
    native.observations[0].native_state=1;native.observations[0].room=1;assert(map_script_dispatch_tick(error,sizeof(error)));assert(map_script_content_snapshot_save(end,bytes,error,sizeof(error)));
    assert(map_script_content_snapshot_load(before,bytes,error,sizeof(error)));native.observations[0].native_state=1;native.observations[0].room=1;assert(map_script_dispatch_tick(error,sizeof(error)));assert(map_script_content_snapshot_save(replay,bytes,error,sizeof(error))&&!memcmp(end,replay,bytes));
    free(before);free(end);free(replay);map_script_deactivate();
}
static void test_native_mine_trigger_queue(void) {
    MineHost mine={0};MapScriptHost host={0};host.userdata=&mine;host.trigger_mines_fn=trigger_mines;
    MapScriptDefinition d=definition("map.on_tick(function() assert(map.trigger_mine_at(80,96)) assert(not map.trigger_mine_at(80,96)) assert(map.trigger_mine_at(-16.5,32.25)) end)");
    assert(map_script_activate_content(&d,&host,package,strlen(package),error,sizeof(error)));
    assert(map_script_dispatch_tick(error,sizeof(error)));assert(mine.calls==1&&mine.count==2);assert(mine.triggers[0].x==80&&mine.triggers[0].y==96);assert(mine.triggers[1].x==-16.5&&mine.triggers[1].y==32.25);map_script_deactivate();
    mine=(MineHost){0};d=definition("map.on_tick(function() map.trigger_mine_at(80,96) error('after mine request') end)");
    assert(map_script_activate_content(&d,&host,package,strlen(package),error,sizeof(error)));assert(!map_script_dispatch_tick(error,sizeof(error))&&strstr(error,"after mine request"));assert(mine.calls==0);map_script_deactivate();
    d=definition("map.on_tick(function() map.trigger_mine_at(0,0) end)");assert(map_script_activate_content(&d,NULL,package,strlen(package),error,sizeof(error)));assert(!map_script_dispatch_tick(error,sizeof(error))&&strstr(error,"mine triggering is unavailable"));map_script_deactivate();
}
int main(void) {
    test_solid_regions();
    test_instance_variables_and_manual_motion();
    test_spawn_initial_values();
    test_object_variable_capacity_and_isolation();
    test_patrol_blocks();
    test_player_variables();
    test_terrain_motion(0);
    test_terrain_motion(1);
    test_motion_controls();
    test_generated_blocks();
    test_generated_player_blocks();
    test_double_jump_example();
    test_entity_contact_callbacks();
    test_damage_example();
    test_projectile_example();
    test_signal_example();
    test_named_animations();
    test_managed_object_health();
    test_native_attack_damage_bridge();
    test_native_mine_knockback();
    test_managed_player_health();
    test_player_sprite_override();
    test_player_lifecycle_observations();
    test_native_mine_trigger_queue();
    MapScriptDefinition d=definition("map.state.orb=entity.list()[1] map.on_tick(function() map.state.x=entity.get(map.state.orb).x end)");
    MapScriptSnapshot plain;unsigned char *initial,*after,*copy,*bad;size_t size,i;
    assert(map_script_activate_content(&d,NULL,package,strlen(package),error,sizeof(error)));
    assert(map_script_network_admissible(error,sizeof(error)) && !error[0]);
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
    memcpy(bad,initial,size);bad[size-sizeof(MapScriptObjectStateSnapshot)+offsetof(MapScriptObjectStateSnapshot,checksum)]^=1;
    assert(!map_script_content_snapshot_validate(bad,size,error,sizeof(error)) && strstr(error,"object variable"));
    assert(!map_script_content_snapshot_load(bad,size,error,sizeof(error)));
    assert(map_script_content_snapshot_save(copy,size,error,sizeof(error)) && !memcmp(copy,after,size));
    memcpy(bad,initial,size);bad[36]^=1;
    assert(!map_script_content_snapshot_validate(bad,size,error,sizeof(error)));
    memcpy(bad,initial,size);{
        MapScriptObjectStateSnapshot* object_state=(MapScriptObjectStateSnapshot*)(bad+size-sizeof(MapScriptObjectStateSnapshot));
        MapScriptObjectStateEntry* entry=&object_state->entries[0];
        entry->in_use=1;entry->type=MAP_SCRIPT_STATE_NUMBER;entry->key_len=1;entry->handle=UINT64_C(0x0000000100000008);entry->number_value=1;entry->key[0]='x';object_state->count=1;object_state->checksum=object_state_crc(object_state);
    }
    assert(!map_script_content_snapshot_validate(bad,size,error,sizeof(error)) && strstr(error,"missing entity"));
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
    d=definition("map.state.spawns=0 map.state.removes=0 entity.on_spawn('demo:orb',function(h,v) map.state.spawns=map.state.spawns+1 entity.set(h,{vy=2}) entity.set_value(h,'token',7) if not entity.has_value(h,'token') or #entity.value_keys(h)~=1 then error('spawn value') end end) entity.on_remove('demo:orb',function(h,v) if entity.exists(h) or v.vy~=2 or entity.value(h,'token')~=7 then error('removal view') end map.state.removes=map.state.removes+1 end) map.on_tick(function() if map.tick()==0 then local h=entity.spawn('demo:orb',{}) entity.remove(h) if entity.remove(h) then error('double remove') end end end)");
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
    assert(((MapScriptObjectStateSnapshot*)(after+size-sizeof(MapScriptObjectStateSnapshot)))->count==1);
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
    d=definition("entity.on_remove('demo:orb',function(h,v) assert(entity.value(h,'temporary')==9) map.state.bad=true error('remove failure') end) map.on_tick(function() local h=entity.list()[1] entity.set_value(h,'temporary',9) entity.remove(h) end)");
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
        const char* rooms="{\"schema\":2,\"capacity\":8,\"types\":[{\"key\":\"demo:orb\",\"regions\":[{\"id\":1,\"name\":\"trigger\",\"role\":\"sensor\",\"layer\":1,\"mask\":1,\"x\":2,\"width\":4,\"height\":6}]}],\"placements\":[{\"name\":\"pad\",\"type\":\"demo:orb\",\"room\":\"side\",\"x\":8,\"vx\":2}]}";
        d=definition("entity.on_spawn('demo:orb',function(h,v) local r=entity.regions(h)[1] if r.name~='trigger' then error('region name') end if v.mirrored then if v.x~=2104 or v.vx~=-2 or r.x~=-6 then error('mirror') end else if v.x~=536 or v.vx~=2 or r.x~=2 then error('source') end end end) entity.on_update('demo:orb',function(h) local v=entity.get(h) entity.set(h,{mirrored=not v.mirrored}) end)");
        d.entity_layout=(EntityPackageLayout){.count=3,.rooms={"center","side","end"}};
        assert(map_script_activate_content(&d,NULL,rooms,strlen(rooms),error,sizeof(error)));
        size_t bytes=map_script_content_snapshot_size();unsigned char* start=malloc(bytes);unsigned char* end=malloc(bytes);unsigned char* replay=malloc(bytes);assert(start && end && replay);
        assert(map_script_content_snapshot_save(start,bytes,error,sizeof(error)));
        for(int tick=0;tick<7;tick++)assert(map_script_dispatch_tick(error,sizeof(error)));
        assert(map_script_content_snapshot_save(end,bytes,error,sizeof(error)));
        assert(map_script_content_snapshot_load(start,bytes,error,sizeof(error)));
        for(int tick=0;tick<7;tick++)assert(map_script_dispatch_tick(error,sizeof(error)));
        assert(map_script_content_snapshot_save(replay,bytes,error,sizeof(error)) && !memcmp(end,replay,bytes));
        d=definition("");d.entity_layout=(EntityPackageLayout){.count=2,.rooms={"center","side"}};
        assert(map_script_activate_content(&d,NULL,rooms,strlen(rooms),error,sizeof(error)));
        assert(!map_script_content_snapshot_load(start,bytes,error,sizeof(error)));
        free(start);free(end);free(replay);
    }
    {
        PlayerHost player_host={0};MapScriptHost host={0};host.userdata=&player_host;host.read_player_fn=read_player;host.read_player_observation_fn=read_player_observation;
        d=definition("map.players()");
        assert(!map_script_activate_content(&d,&host,package,strlen(package),error,sizeof(error)) && strstr(error,"gameplay callbacks"));
        assert(player_host.reads==0);
        player_host.observation.command_bits=MAP_SCRIPT_COMMAND_JUMP|MAP_SCRIPT_COMMAND_RIGHT;
        player_host.observation.previous_command_bits=MAP_SCRIPT_COMMAND_RIGHT|MAP_SCRIPT_COMMAND_ATTACK;
        player_host.observation.previously_grounded=1;
        player_host.observation.native_state=4;player_host.observation.room=-2;player_host.observation.facing=-1;player_host.observation.has_sword=1;
        player_host.observation.collision_flags=5;player_host.observation.previous_collision_flags=1;
        player_host.observation.skin_palette=7;player_host.observation.clothing_palette=9;
        d=definition("map.on_tick(function() local p=map.players() if #p~=1 or p[1].contact_radius~=6 or p[1].grounded or not p[1].previously_grounded or p[1].native_state~=4 or p[1].room~=-2 or p[1].facing~=-1 or not p[1].has_sword or p[1].collision_flags~=5 or p[1].previous_collision_flags~=1 or p[1].skin_palette~=7 or p[1].clothing_palette~=9 or not p[1].input.jump or not p[1].input.right or p[1].input.attack or not p[1].pressed.jump or p[1].pressed.right or p[1].released.jump or not p[1].released.attack then error('player observation') end map.state.player=p[1].player local x=p[1].x p[1].x=999 p[1].input.jump=false if not map.players()[1].input.jump or map.players()[1].x~=x then error('write through') end entity.set(entity.find('orb'),{x=x}) end)");
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
        player_host.mode=0;player_host.observation.command_bits=UINT32_C(0x80000000);
        d=definition("map.on_tick(function() map.players() end)");
        assert(map_script_activate_content(&d,&host,package,strlen(package),error,sizeof(error)));
        assert(!map_script_dispatch_tick(error,sizeof(error)) && strstr(error,"invalid observation"));
        player_host.observation.command_bits=0;player_host.observation.previous_command_bits=UINT32_C(0x80000000);
        assert(map_script_activate_content(&d,&host,package,strlen(package),error,sizeof(error)));
        assert(!map_script_dispatch_tick(error,sizeof(error)) && strstr(error,"invalid observation"));
        player_host.observation.previous_command_bits=0;
        for(player_host.observation_mode=1;player_host.observation_mode<=5;player_host.observation_mode++) {
            assert(map_script_activate_content(&d,&host,package,strlen(package),error,sizeof(error)));
            assert(!map_script_dispatch_tick(error,sizeof(error)) && strstr(error,"invalid observation"));
        }
        player_host.observation_mode=0;
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
        d=definition(source);d.entity_layout=(EntityPackageLayout){.count=1,.rooms={"center"}};assert(map_script_activate_content(&d,&host,json,n,error,sizeof(error)));
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
        d=definition(source);d.entity_layout=(EntityPackageLayout){.count=1,.rooms={"center"}};assert(map_script_activate_content(&d,&host,json,n,error,sizeof(error)));
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
        memcpy(original,native.players,sizeof(original));host.userdata=&native;host.read_player_fn=velocity_read;host.apply_player_velocities_fn=velocity_commit;host.commit_players_fn=player_commit;
        d=definition("map.set_player_velocity(1,1,2)");
        assert(!map_script_activate_content(&d,&host,package,strlen(package),error,sizeof(error)) && strstr(error,"tick/timer"));
        d=definition("map.on_tick(function() map.set_player_position(1,544,96) map.set_player_velocity(1,3,4) map.set_player_velocity(2,-5,6) local p=map.players()[1] if p.vx~=3 or p.x~=544 or p.y~=96 then error('queued read') end end) entity.on_update('demo:orb',function(h) map.set_player_velocity(1,7,8) end)");
        assert(map_script_activate_content(&d,&host,package,strlen(package),error,sizeof(error)));
        assert(map_script_content_snapshot_save(initial,size,error,sizeof(error)));
        for(i=0;i<8;i++) assert(map_script_dispatch_tick(error,sizeof(error)));
        assert(native.commits==8 && native.players[0].vx==7 && native.players[0].vy==8 && native.players[1].vx==-5 && native.players[1].vy==6);
        assert(native.players[0].x==544 && native.players[0].y==96 && native.players[1].y==original[1].y);
        memcpy(future,native.players,sizeof(future));
        assert(map_script_content_snapshot_save(after,size,error,sizeof(error)));
        assert(map_script_content_snapshot_load(initial,size,error,sizeof(error)));memcpy(native.players,original,sizeof(original));native.commits=0;
        for(i=0;i<8;i++) assert(map_script_dispatch_tick(error,sizeof(error)));
        assert(!memcmp(future,native.players,sizeof(future)));
        assert(map_script_content_snapshot_save(copy,size,error,sizeof(error)) && !memcmp(copy,after,size));
        const char* failures[]={
            "map.on_tick(function() map.set_player_position(1,600,80) map.set_player_velocity(1,9,9) map.set_player_velocity(2,8,8) entity.set(entity.find('orb'),{vx=9}) map.state.bad=true error('late failure') end)",
            "map.on_tick(function() map.set_player_position(1,600,80) map.set_player_velocity(1,9,9) map.set_player_velocity(2,8,8) end)"};
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
            const char* room_move="map.on_tick(function() if map.tick()==0 then map.move_player_to_room(1,1,24,80) local p=map.players()[1] assert(p.x==664 and p.y==272 and p.room==1) end end)";
            memcpy(native.players,original,sizeof(original));memset(native.rooms,0,sizeof(native.rooms));native.commits=0;
            d=definition(room_move);d.entity_layout.count=1;d.entity_layout.rooms[0]="center";
            d.entity_layout.instance_count=2;d.entity_layout.start_room=0;
            d.entity_layout.instances[0]=(EntityPackageRoomInstance){.id="start",.source_room=0,.width=528,.height=192};
            d.entity_layout.instances[1]=(EntityPackageRoomInstance){.id="upper",.source_room=0,.x=640,.y=192,.width=320,.height=160};
            assert(map_script_activate(&d,&host,error,sizeof(error)));
            assert(map_script_dispatch_tick(error,sizeof(error)));
            assert(native.commits==1&&native.rooms[0]==1&&native.players[0].x==664&&native.players[0].y==272);
            native.reject=1;native.commits=0;memcpy(native.players,original,sizeof(original));native.rooms[0]=0;
            assert(map_script_activate(&d,&host,error,sizeof(error)));
            assert(!map_script_dispatch_tick(error,sizeof(error))&&native.commits==1&&native.rooms[0]==0&&native.players[0].x==original[0].x);
            native.reject=0;
        }

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
