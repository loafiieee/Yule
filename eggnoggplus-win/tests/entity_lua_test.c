#include "../entity_lua.h"
#include <luajit-2.1/lauxlib.h>
#include <luajit-2.1/lualib.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t resolve(void* user,const char* name,size_t length) {
    (void)user;return length==8 && !memcmp(name,"demo:orb",8) ? 1 : 0;
}
static int run(lua_State* L,const char* source) {
    if(luaL_dostring(L,source)) { fprintf(stderr,"%s\n",lua_tostring(L,-1));lua_pop(L,1);return 0; }
    return 1;
}
static int update(EntityWorld* world,EntityHandle handle,void* user) {
    (void)world;(void)handle;
    return run(user,"entity.set(orb,{vx=9}) entity.spawn('demo:orb',{}) error('intentional rollback')");
}
int main(void) {
    EntityWorld* world=entity_world_create(3);
    EntityType type={0};EntityLuaBinding b={.world=world,.resolve_type=resolve};
    lua_State* L=luaL_newstate();size_t size;unsigned char *before,*after;
    assert(world && L);type.id=1;type.region_count=1;
    type.regions[0]=(EntityRegion){7,ENTITY_REGION_SENSOR,1,1,0,0,256,256};assert(entity_world_define_types(world,&type,1));
    luaL_openlibs(L);entity_lua_push_api(L,&b);lua_setglobal(L,"entity");
    assert(run(L,
      "orb=entity.spawn('demo:orb',{x=1.5,y=-2.25,vx=0.25}) "
      "assert(type(orb)=='string' and #orb==16 and entity.exists(orb)) "
      "local v=entity.get(orb) assert(v.x==1.5 and v.y==-2.25) "
      "v.x=77 assert(entity.get(orb).x==1.5) "
      "local r=entity.regions(orb) assert(#r==1 and r[1].id==7 and r[1].role=='sensor') "
      "assert(r[1].x==0 and r[1].y==0 and r[1].width==1 and r[1].height==1 and r[1].layer==1 and r[1].mask==1) "
      "assert(r[1].world_x==1.5 and r[1].world_y==-2.25) r[1].x=999 assert(entity.regions(orb)[1].x==0) "
      "entity.set(orb,{mirrored=true}) assert(entity.get(orb).mirrored and entity.regions(orb)[1].x==-1) "
      "assert(entity.regions(orb)[1].world_x==0.5) assert(not pcall(entity.set,orb,{mirrored=1})) "
      "entity.set(orb,{mirrored=false}) assert(not entity.get(orb).mirrored) "
      "assert(not pcall(entity.regions)) assert(not pcall(entity.regions,orb,orb)) "
      "entity.set(orb,{vy=-0.5}) assert(entity.get(orb).vx==0.25) "
      "assert(not pcall(entity.set,orb,{x=9,bad=1})) assert(entity.get(orb).x==1.5) "
      "assert(not pcall(entity.set,orb,{x=0/0})) "
      "assert(not pcall(entity.spawn,'unknown',{})) assert(#entity.list()==1) "
      "local old=entity.spawn('demo:orb',{}) assert(entity.remove(old)) "
      "local fresh=entity.spawn('demo:orb',{}) assert(fresh~=old and not entity.exists(old)) "
      "assert(not pcall(entity.get,old)) assert(not pcall(entity.regions,old)) assert(not entity.remove(old)) "
      "assert(not pcall(entity.exists,1)) assert(not pcall(entity.exists,string.rep('z',16))) "
      "assert(entity.remove(fresh))"));
    assert(run(L,
      "assert(#entity.contacts()==0) "
      "peer=entity.spawn('demo:orb',{x=1.5,y=-2.25}) "
      "local c=entity.contacts() assert(#c==1 and c[1].a==orb and c[1].b==peer) "
      "assert(c[1].region_a==7 and c[1].region_b==7 and c[1].role_a=='sensor' and c[1].role_b=='sensor') "
      "c[1].region_a=99 assert(entity.contacts()[1].region_a==7) "
      "entity.set(peer,{x=2.5}) assert(#entity.contacts()==0) "
      "entity.set(peer,{x=2.49609375}) assert(#entity.contacts()==1) "
      "assert(not pcall(entity.contacts,orb)) assert(entity.remove(peer)) assert(#entity.contacts()==0)"));
    {
        uint32_t budget=71;b.work_budget=&budget;
        assert(run(L,"assert(not pcall(entity.regions,orb))"));assert(budget==0);
        budget=72;assert(run(L,"assert(#entity.regions(orb)==1)"));assert(budget==0);b.work_budget=NULL;
        EntityType copy,untouched;memset(&copy,0x5a,sizeof(copy));untouched=copy;
        assert(!entity_world_read_type(world,99,&copy) && !memcmp(&copy,&untouched,sizeof(copy)));
        assert(entity_world_read_type(world,1,&copy));copy.regions[0].width=999;
        assert(entity_world_read_type(world,1,&copy) && copy.regions[0].width==256);
    }
    assert(run(L,"local p=entity.get(orb) assert(entity.at(p.x,p.y)[1]==orb) assert(#entity.at(p.x+1,p.y)==0) assert(#entity.at(p.x,p.y+1)==0)"));
    assert(run(L,"assert(not pcall(entity.at,0/0,0)) assert(not pcall(entity.at,'0',0)) assert(not pcall(entity.at,0)) local p=entity.get(orb) entity.set(orb,{mirrored=true}) assert(entity.at(p.x-1,p.y)[1]==orb) assert(#entity.at(p.x,p.y)==0) entity.set(orb,{mirrored=false})"));
    {uint32_t budget=39;b.work_budget=&budget;assert(run(L,"assert(not pcall(entity.at,0,0))"));assert(budget==0);b.work_budget=NULL;}
    size=entity_world_snapshot_size(world);before=malloc(size);after=malloc(size);assert(before && after);
    assert(entity_world_save(world,before,size));
    assert(!entity_world_update(world,update,L));
    assert(entity_world_save(world,after,size) && !memcmp(before,after,size));
    assert(run(L,"assert(#entity.list()==1 and entity.get(orb).vx==0.25)"));
    {
        EntityWorld* dense=entity_world_create(92);EntityValue v={0};
        assert(dense && entity_world_define_types(dense,&type,1));v.type_id=1;
        for(int i=0;i<92;i++) assert(entity_world_spawn(dense,&v));
        b.world=dense;
        assert(run(L,"local ok,err=pcall(entity.contacts) assert(not ok and string.find(err,'4096'))"));
        assert(entity_world_count(dense)==92 && entity_world_tick(dense)==0);
        {
            EntityContact output[4186],saved[4186];uint32_t budget=1;
            memset(output,0x5a,sizeof(output));memcpy(saved,output,sizeof(output));
            assert(entity_world_contacts_budgeted(dense,output,4186,&budget)==SIZE_MAX && budget==0);
            assert(!memcmp(output,saved,sizeof(output)));
            budget=9000; /* Enough for counting, insufficient for the write pass. */
            assert(entity_world_contacts_budgeted(dense,output,4186,&budget)==SIZE_MAX && budget==0);
            assert(!memcmp(output,saved,sizeof(output)));
            budget=20000;
            assert(entity_world_contacts_budgeted(dense,output,4186,&budget)==4186 && budget<20000);
            budget=5;b.work_budget=&budget;
            assert(run(L,"local ok,err=pcall(entity.contacts) assert(not ok and string.find(err,'budget'))"));
            assert(budget==0);b.work_budget=NULL;
        }
        b.world=world;entity_world_free(dense);
    }
    assert(run(L,"entity.set(orb,{animation_tick=12,animation_paused=true}) local v=entity.get(orb) assert(v.animation_tick==12 and v.animation_paused) assert(not pcall(entity.set,orb,{animation_tick=-1})) assert(not pcall(entity.set,orb,{animation_tick=0.5})) assert(not pcall(entity.set,orb,{animation_paused=1})) entity.set(orb,{animation_tick=0,animation_paused=false})"));
    lua_close(L);entity_world_free(world);free(before);free(after);
    puts("entity Lua adapter: named types, exact handles, validation and transactional callbacks passed");
    return 0;
}
