#include "entity_lua.h"
#include <luajit-2.1/lauxlib.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

static EntityLuaBinding* binding(lua_State* L) {
    EntityLuaBinding* b=lua_touserdata(L,lua_upvalueindex(1));
    if(!b || !b->world || !b->resolve_type) luaL_error(L,"entity runtime unavailable");
    return b;
}
EntityHandle entity_lua_check_handle(lua_State* L,int arg) {
    size_t length; const char* s; EntityHandle h=0;
    if(lua_type(L,arg)!=LUA_TSTRING) luaL_error(L,"entity handle must be an opaque string");
    s=lua_tolstring(L,arg,&length);
    if(length!=16) luaL_error(L,"invalid entity handle");
    for(size_t i=0;i<length;++i) {
        unsigned char c=(unsigned char)s[i]; unsigned int digit;
        if(c>='0' && c<='9') digit=c-'0'; else if(c>='a' && c<='f') digit=c-'a'+10;
        else return (EntityHandle)luaL_error(L,"invalid entity handle");
        h=(h<<4)|digit;
    }
    if(!h) luaL_error(L,"invalid entity handle");
    return h;
}
void entity_lua_push_handle(lua_State* L,EntityHandle h) {
    char text[17];
    snprintf(text,sizeof(text),"%08x%08x",(unsigned int)(h>>32),(unsigned int)h);
    lua_pushlstring(L,text,16);
}
static int32_t coordinate(lua_State* L,int arg) {
    double v;
    if(lua_type(L,arg)!=LUA_TNUMBER) luaL_error(L,"entity coordinates must be numbers");
    v=lua_tonumber(L,arg)*256.0;
    if(!isfinite(v) || v < -ENTITY_WORLD_COORD_LIMIT || v > ENTITY_WORLD_COORD_LIMIT)
        luaL_error(L,"entity coordinate is outside the fixed-point range");
    return (int32_t)(v<0 ? ceil(v-0.5) : floor(v+0.5));
}
static int32_t visual_scale(lua_State* L,int arg,const char* name) {
    double value,scaled;int32_t result;
    if(lua_type(L,arg)!=LUA_TNUMBER)luaL_error(L,"%s must be a number",name);
    value=lua_tonumber(L,arg);scaled=value*256.0;
    if(!isfinite(value)||value==0||scaled < -65536.0||scaled > 65536.0)
        luaL_error(L,"%s must be finite, nonzero and within -256..256",name);
    result=(int32_t)(scaled<0?ceil(scaled-0.5):floor(scaled+0.5));
    if(!result)luaL_error(L,"%s is too close to zero for 1/256 scale precision",name);
    return result;
}
static int32_t visual_rotation(lua_State* L,int arg) {
    double value,scaled;
    if(lua_type(L,arg)!=LUA_TNUMBER)luaL_error(L,"visual_rotation must be a number");
    value=lua_tonumber(L,arg);scaled=value*256.0;
    if(!isfinite(value)||scaled < -92160000.0||scaled > 92160000.0)
        luaL_error(L,"visual_rotation must be finite and within -360000..360000 degrees");
    return (int32_t)(scaled<0?ceil(scaled-0.5):floor(scaled+0.5));
}
static uint32_t visual_tint(lua_State* L,int arg) {
    size_t length;const char* text;uint32_t rgba=0;
    if(lua_type(L,arg)!=LUA_TSTRING)luaL_error(L,"visual_tint must be #RRGGBBAA or default");
    text=lua_tolstring(L,arg,&length);
    if(length==7&&!memcmp(text,"default",7))return 0;
    if(length!=9||text[0]!='#')luaL_error(L,"visual_tint must be #RRGGBBAA or default");
    for(size_t i=1;i<9;++i){unsigned char c=(unsigned char)text[i];unsigned digit;
        if(c>='0'&&c<='9')digit=c-'0';else if(c>='a'&&c<='f')digit=c-'a'+10;
        else if(c>='A'&&c<='F')digit=c-'A'+10;else return (uint32_t)luaL_error(L,"visual_tint has an invalid hex digit");
        rgba=(rgba<<4)|digit;
    }
    return rgba;
}
static void properties(lua_State* L,int arg,EntityValue* v,int allow_values) {
    if(lua_type(L,arg)!=LUA_TTABLE) luaL_error(L,"entity properties must be a table");
    lua_pushnil(L);
    while(lua_next(L,arg)) {
        const char* key; size_t length;
        if(lua_type(L,-2)!=LUA_TSTRING) luaL_error(L,"entity property names must be strings");
        key=lua_tolstring(L,-2,&length);
        if(length==1 && key[0]=='x') v->x=coordinate(L,-1);
        else if(length==1 && key[0]=='y') v->y=coordinate(L,-1);
        else if(length==2 && !memcmp(key,"vx",2)) v->vx=coordinate(L,-1);
        else if(length==2 && !memcmp(key,"vy",2)) v->vy=coordinate(L,-1);
        else if(length==7 && !memcmp(key,"scale_x",7)) v->visual_scale_x=visual_scale(L,-1,"scale_x");
        else if(length==7 && !memcmp(key,"scale_y",7)) v->visual_scale_y=visual_scale(L,-1,"scale_y");
        else if(length==15 && !memcmp(key,"visual_offset_x",15)) v->visual_offset_x=coordinate(L,-1);
        else if(length==15 && !memcmp(key,"visual_offset_y",15)) v->visual_offset_y=coordinate(L,-1);
        else if(length==15 && !memcmp(key,"visual_rotation",15)) v->visual_rotation=visual_rotation(L,-1);
        else if(length==11 && !memcmp(key,"visual_tint",11)) {
            uint32_t rgba=visual_tint(L,-1);v->visual_tint=rgba;
            v->flags=(v->flags&~ENTITY_FLAG_TINT_OVERRIDE)|(rgba||strcmp(lua_tostring(L,-1),"default")?ENTITY_FLAG_TINT_OVERRIDE:0);
        }
        else if(length==10 && !memcmp(key,"draw_layer",10)) {
            size_t n;const char* layer;
            if(lua_type(L,-1)!=LUA_TSTRING)luaL_error(L,"draw_layer must be authored, behind, or front");
            layer=lua_tolstring(L,-1,&n);
            if(n==8&&!memcmp(layer,"authored",8)){v->flags&=~ENTITY_FLAG_LAYER_OVERRIDE;v->visual_layer=0;}
            else if(n==6&&!memcmp(layer,"behind",6)){v->flags|=ENTITY_FLAG_LAYER_OVERRIDE;v->visual_layer=0;}
            else if(n==5&&!memcmp(layer,"front",5)){v->flags|=ENTITY_FLAG_LAYER_OVERRIDE;v->visual_layer=1;}
            else luaL_error(L,"draw_layer must be authored, behind, or front");
        }
        else if(length==8 && !memcmp(key,"mirrored",8)) {
            if(lua_type(L,-1)!=LUA_TBOOLEAN) luaL_error(L,"mirrored must be boolean");
            v->flags=(v->flags & ~ENTITY_FLAG_MIRROR_X) | (lua_toboolean(L,-1)?ENTITY_FLAG_MIRROR_X:0);
        }
        else if(length==14 && !memcmp(key,"animation_tick",14)) {
            double tick=lua_tonumber(L,-1);
            if(lua_type(L,-1)!=LUA_TNUMBER || !isfinite(tick) || tick<0 || tick>(double)ENTITY_ANIMATION_TICK_MAX || floor(tick)!=tick)
                luaL_error(L,"animation_tick must be a nonnegative exact integer");
            v->animation_tick=(uint64_t)tick;v->animation_subtick=0;
        }
        else if(length==15 && !memcmp(key,"animation_speed",15)) {
            double speed,scaled;int32_t result;
            if(lua_type(L,-1)!=LUA_TNUMBER)luaL_error(L,"animation_speed must be a number");
            speed=lua_tonumber(L,-1);scaled=speed*256.0;
            if(!isfinite(speed)||speed<=0||scaled>65536.0)luaL_error(L,"animation_speed must be finite and within 1/256..256");
            result=(int32_t)floor(scaled+0.5);
            if(result<1)luaL_error(L,"animation_speed is too small for 1/256 precision");
            v->animation_rate=result;
        }
        else if(length==16 && !memcmp(key,"automatic_motion",16)) {
            if(lua_type(L,-1)!=LUA_TBOOLEAN)luaL_error(L,"automatic_motion must be boolean");
            v->flags=(v->flags & ~ENTITY_FLAG_MANUAL_MOTION)|(lua_toboolean(L,-1)?0:ENTITY_FLAG_MANUAL_MOTION);
        }
        else if(length==16 && !memcmp(key,"animation_paused",16)) {
            if(lua_type(L,-1)!=LUA_TBOOLEAN)luaL_error(L,"animation_paused must be boolean");
            v->flags=(v->flags & ~ENTITY_FLAG_ANIMATION_PAUSED)|(lua_toboolean(L,-1)?ENTITY_FLAG_ANIMATION_PAUSED:0);
        }
        else if(length==7 && !memcmp(key,"visible",7)) {
            if(lua_type(L,-1)!=LUA_TBOOLEAN)luaL_error(L,"visible must be boolean");
            v->flags=(v->flags & ~ENTITY_FLAG_HIDDEN)|(lua_toboolean(L,-1)?0:ENTITY_FLAG_HIDDEN);
        }
        else if(allow_values && length==6 && !memcmp(key,"values",6)) {
            if(lua_type(L,-1)!=LUA_TTABLE)luaL_error(L,"spawn values must be a table");
        }
        else luaL_error(L,"unknown entity property; expected x, y, vx, vy, scale_x, scale_y, visual_offset_x, visual_offset_y, visual_rotation, visual_tint, draw_layer, mirrored, animation_tick, animation_speed, animation_paused, automatic_motion or visible");
        lua_pop(L,1);
    }
}
static int spawn(lua_State* L) {
    EntityLuaBinding* b=binding(L); EntityValue v={0}; EntityHandle h;
    const char* key; size_t length;
    if(lua_gettop(L)!=2 || lua_type(L,1)!=LUA_TSTRING) return luaL_error(L,"spawn expects type name and properties");
    key=lua_tolstring(L,1,&length);
    if(!length || length>96 || memchr(key,0,length)) return luaL_error(L,"invalid entity type name");
    v.type_id=b->resolve_type(b->user,key,length);
    if(!v.type_id) return luaL_error(L,"unknown entity type");
    properties(L,2,&v,1);
    lua_getfield(L,2,"values");
    {
        int has_values=!lua_isnil(L,-1);
        lua_pop(L,1);
        if(has_values&&!b->initialize_values)return luaL_error(L,"spawn values are unavailable in this host");
    }
    h=entity_world_spawn(b->world,&v);
    if(!h) return luaL_error(L,"entity spawn rejected: capacity exhausted or type unavailable");
    if(b->initialize_values)b->initialize_values(L,h,2);
    if(b->lifecycle) b->lifecycle(L,1,h,&v);
    entity_lua_push_handle(L,h);return 1;
}
static int exists(lua_State* L) {
    EntityLuaBinding* b=binding(L);EntityValue v;
    if(lua_gettop(L)!=1) return luaL_error(L,"exists expects one handle");
    lua_pushboolean(L,entity_world_read(b->world,entity_lua_check_handle(L,1),&v));return 1;
}
static int remove_entity(lua_State* L) {
    EntityLuaBinding* b=binding(L);EntityValue v;EntityHandle h;
    if(lua_gettop(L)!=1) return luaL_error(L,"remove expects one handle");
    h=entity_lua_check_handle(L,1);
    if(!entity_world_read(b->world,h,&v)) { lua_pushboolean(L,0);return 1; }
    if(!entity_world_remove(b->world,h)) return luaL_error(L,"entity removal rejected");
    if(b->lifecycle) b->lifecycle(L,2,h,&v);
    lua_pushboolean(L,1);return 1;
}
void entity_lua_push_value(lua_State* L,const EntityValue* v) {
    char tint[10];
    lua_createtable(L,0,17);
    lua_pushnumber(L,(lua_Number)v->animation_tick);lua_setfield(L,-2,"animation_tick");
    lua_pushnumber(L,v->animation_rate?v->animation_rate/256.0:1.0);lua_setfield(L,-2,"animation_speed");
    lua_pushboolean(L,(v->flags & ENTITY_FLAG_ANIMATION_PAUSED)!=0);lua_setfield(L,-2,"animation_paused");
    lua_pushboolean(L,(v->flags & ENTITY_FLAG_MANUAL_MOTION)==0);lua_setfield(L,-2,"automatic_motion");
    lua_pushboolean(L,(v->flags & ENTITY_FLAG_MIRROR_X)!=0);lua_setfield(L,-2,"mirrored");
    lua_pushboolean(L,(v->flags & ENTITY_FLAG_HIDDEN)==0);lua_setfield(L,-2,"visible");
    lua_pushnumber(L,v->x/256.0);lua_setfield(L,-2,"x");
    lua_pushnumber(L,v->y/256.0);lua_setfield(L,-2,"y");
    lua_pushnumber(L,v->vx/256.0);lua_setfield(L,-2,"vx");
    lua_pushnumber(L,v->vy/256.0);lua_setfield(L,-2,"vy");
    lua_pushnumber(L,v->visual_scale_x?v->visual_scale_x/256.0:1.0);lua_setfield(L,-2,"scale_x");
    lua_pushnumber(L,v->visual_scale_y?v->visual_scale_y/256.0:1.0);lua_setfield(L,-2,"scale_y");
    lua_pushnumber(L,v->visual_offset_x/256.0);lua_setfield(L,-2,"visual_offset_x");
    lua_pushnumber(L,v->visual_offset_y/256.0);lua_setfield(L,-2,"visual_offset_y");
    lua_pushnumber(L,v->visual_rotation/256.0);lua_setfield(L,-2,"visual_rotation");
    if(v->flags&ENTITY_FLAG_TINT_OVERRIDE){snprintf(tint,sizeof(tint),"#%08x",v->visual_tint);lua_pushstring(L,tint);}else lua_pushstring(L,"default");lua_setfield(L,-2,"visual_tint");
    lua_pushstring(L,(v->flags&ENTITY_FLAG_LAYER_OVERRIDE)?(v->visual_layer?"front":"behind"):"authored");lua_setfield(L,-2,"draw_layer");
}
static int read_entity(lua_State* L) {
    EntityLuaBinding* b=binding(L);EntityValue v;
    if(lua_gettop(L)!=1) return luaL_error(L,"get expects one handle");
    if(!entity_world_read(b->world,entity_lua_check_handle(L,1),&v)) return luaL_error(L,"entity no longer exists");
    entity_lua_push_value(L,&v);return 1;
}
static int set_entity(lua_State* L) {
    EntityLuaBinding* b=binding(L);EntityValue v;EntityHandle h;
    if(lua_gettop(L)!=2) return luaL_error(L,"set expects handle and properties");
    h=entity_lua_check_handle(L,1);
    if(!entity_world_read(b->world,h,&v)) return luaL_error(L,"entity no longer exists");
    properties(L,2,&v,0);
    if(!entity_world_write(b->world,h,&v)) return luaL_error(L,"entity update rejected");
    return 0;
}
static int find_entity(lua_State* L) {
    EntityLuaBinding* b=binding(L);EntityHandle h;EntityValue value;size_t n;const char* key;
    if(lua_gettop(L)!=1 || lua_type(L,1)!=LUA_TSTRING) return luaL_error(L,"find expects one placement name");
    key=lua_tolstring(L,1,&n);
    if(!n || n>96 || memchr(key,0,n)) return luaL_error(L,"invalid placement name");
    if(!b->find_placement) return luaL_error(L,"entity placement lookup unavailable");
    h=b->find_placement(b->user,key);
    if(!h || !entity_world_read(b->world,h,&value)) { lua_pushnil(L);return 1; }
    entity_lua_push_handle(L,h);return 1;
}
static int entity_type(lua_State* L) {
    EntityLuaBinding* b=binding(L);EntityValue value;const char* key;
    if(lua_gettop(L)!=1) return luaL_error(L,"type expects one handle");
    if(!entity_world_read(b->world,entity_lua_check_handle(L,1),&value)) return luaL_error(L,"entity no longer exists");
    if(!b->type_key || !(key=b->type_key(b->user,value.type_id))) return luaL_error(L,"entity type lookup unavailable");
    lua_pushstring(L,key);return 1;
}
static int list_entities(lua_State* L) {
    EntityLuaBinding* b=binding(L);EntityHandle h;uint32_t cursor=0;int index=0;
    if(lua_gettop(L)) return luaL_error(L,"list expects no arguments");
    lua_createtable(L,(int)entity_world_count(b->world),0);
    while((h=entity_world_next(b->world,&cursor))) { entity_lua_push_handle(L,h);lua_rawseti(L,-2,++index); }
    return 1;
}
static int regions(lua_State* L) {
    EntityLuaBinding* b=binding(L);EntityValue value;EntityType type;uint32_t cost;
    static const char* roles[]={NULL,"body","sensor","hitbox","hurtbox","solid"};
    if(lua_gettop(L)!=1) return luaL_error(L,"regions expects one handle");
    if(!entity_world_read(b->world,entity_lua_check_handle(L,1),&value)) return luaL_error(L,"entity no longer exists");
    if(!entity_world_read_type(b->world,value.type_id,&type)) return luaL_error(L,"entity regions unavailable");
    cost=64u+type.region_count*8u;
    if(b->work_budget) {
        if(*b->work_budget<cost) {*b->work_budget=0;return luaL_error(L,"entity region query exceeded its execution budget");}
        *b->work_budget-=cost;
    }
    lua_createtable(L,(int)type.region_count,0);
    for(uint32_t i=0;i<type.region_count;i++) {
        const EntityRegion* r=&type.regions[i];const char* name=b->region_name?b->region_name(b->user,value.type_id,r->id):NULL;lua_createtable(L,0,11);
        lua_pushnumber(L,r->id);lua_setfield(L,-2,"id");
        if(name){lua_pushstring(L,name);lua_setfield(L,-2,"name");}
        lua_pushstring(L,roles[r->role]);lua_setfield(L,-2,"role");
        lua_pushnumber(L,r->layer);lua_setfield(L,-2,"layer");
        lua_pushnumber(L,r->mask);lua_setfield(L,-2,"mask");
        lua_pushnumber(L,entity_region_local_x(&value,r)/256.0);lua_setfield(L,-2,"x");
        lua_pushnumber(L,r->y/256.0);lua_setfield(L,-2,"y");
        lua_pushnumber(L,r->width/256.0);lua_setfield(L,-2,"width");
        lua_pushnumber(L,r->height/256.0);lua_setfield(L,-2,"height");
        lua_pushnumber(L,((int64_t)value.x+entity_region_local_x(&value,r))/256.0);lua_setfield(L,-2,"world_x");
        lua_pushnumber(L,((int64_t)value.y+r->y)/256.0);lua_setfield(L,-2,"world_y");
        lua_rawseti(L,-2,(int)i+1);
    }
    return 1;
}
static int solid_box(lua_State* L){
    EntityLuaBinding* b=binding(L);int argc=lua_gettop(L);EntityHandle ignore=0,h;uint32_t cursor=0;
    if(argc!=4&&argc!=5)return luaL_error(L,"solid_box expects center x, y, width, height and optional ignored handle");
    int32_t x=coordinate(L,1),y=coordinate(L,2),width=coordinate(L,3),height=coordinate(L,4);
    if(width<=0||height<=0||width>128*256||height>128*256)return luaL_error(L,"solid_box dimensions must be 1/256 through 128 pixels");
    if(argc==5){EntityValue ignored;ignore=entity_lua_check_handle(L,5);if(!entity_world_read(b->world,ignore,&ignored))return luaL_error(L,"ignored entity no longer exists");}
    /* Double fixed-point coordinates preserve half-unit centered-box edges. */
    int64_t left=(int64_t)x*2-width,top=(int64_t)y*2-height,right=left+width*2,bottom=top+height*2;
    while((h=entity_world_next(b->world,&cursor))){
        if(h==ignore)continue;
        EntityValue value;EntityType type;
        if(!entity_world_read(b->world,h,&value)||!entity_world_read_type(b->world,value.type_id,&type))return luaL_error(L,"solid query encountered invalid entity");
        uint32_t cost=32+type.region_count*8;
        if(b->work_budget){if(*b->work_budget<cost){*b->work_budget=0;return luaL_error(L,"entity solid query exceeded its execution budget");}*b->work_budget-=cost;}
        for(uint32_t i=0;i<type.region_count;i++){
            const EntityRegion* r=&type.regions[i];if(r->role!=ENTITY_REGION_SOLID)continue;
            int64_t rx=((int64_t)value.x+entity_region_local_x(&value,r))*2,ry=((int64_t)value.y+r->y)*2;
            if(left<rx+(int64_t)r->width*2&&right>rx&&top<ry+(int64_t)r->height*2&&bottom>ry){lua_pushboolean(L,1);return 1;}
        }
    }
    lua_pushboolean(L,0);return 1;
}
/* Detached, stable point-query results; overlapping regions never duplicate an instance. */
static int entities_at(lua_State* L){
    EntityLuaBinding* b=binding(L);uint32_t cursor=0,count=0;EntityHandle h;
    if(lua_gettop(L)!=2)return luaL_error(L,"at expects x and y");
    int32_t x=coordinate(L,1),y=coordinate(L,2);lua_newtable(L);
    while((h=entity_world_next(b->world,&cursor))){
        EntityValue value;EntityType type;
        if(!entity_world_read(b->world,h,&value)||!entity_world_read_type(b->world,value.type_id,&type))return luaL_error(L,"point query encountered invalid entity");
        uint32_t cost=32+type.region_count*8;
        if(b->work_budget){if(*b->work_budget<cost){*b->work_budget=0;return luaL_error(L,"entity point query exceeded its execution budget");}*b->work_budget-=cost;}
        for(uint32_t i=0;i<type.region_count;i++){
            const EntityRegion* r=&type.regions[i];int64_t left=(int64_t)value.x+entity_region_local_x(&value,r),top=(int64_t)value.y+r->y;
            if(x>=left&&x<left+r->width&&y>=top&&y<top+r->height){entity_lua_push_handle(L,h);lua_rawseti(L,-2,(int)++count);break;}
        }
    }
    return 1;
}
/* Lua-owned scratch remains collectable even if table allocation raises an
 * out-of-memory error. Never hold malloc storage across a Lua allocation. */
static int contacts(lua_State* L) {
    EntityLuaBinding* b=binding(L);size_t count;EntityContact* values;
    static const char* roles[]={NULL,"body","sensor","hitbox","hurtbox","solid"};
    if(lua_gettop(L)) return luaL_error(L,"contacts expects no arguments");
    count=entity_world_contacts_budgeted(b->world,NULL,0,b->work_budget);
    if(count==SIZE_MAX) return luaL_error(L,"entity contact query exceeded its comparison budget");
    if(count>4096u) return luaL_error(L,"entity contact query exceeds 4096 results");
    values=lua_newuserdata(L,count*sizeof(*values));
    if(count && entity_world_contacts_budgeted(b->world,values,count,b->work_budget)!=count)
        return luaL_error(L,"entity contact query exceeded its execution budget");
    lua_createtable(L,(int)count,0);
    for(size_t i=0;i<count;++i) {
        const EntityContact* c=&values[i];
        lua_createtable(L,0,6);
        entity_lua_push_handle(L,c->a);lua_setfield(L,-2,"a");
        entity_lua_push_handle(L,c->b);lua_setfield(L,-2,"b");
        lua_pushnumber(L,c->region_a);lua_setfield(L,-2,"region_a");
        lua_pushnumber(L,c->region_b);lua_setfield(L,-2,"region_b");
        lua_pushstring(L,roles[c->role_a]);lua_setfield(L,-2,"role_a");
        lua_pushstring(L,roles[c->role_b]);lua_setfield(L,-2,"role_b");
        lua_rawseti(L,-2,(int)i+1);
    }
    return 1;
}
void entity_lua_push_api(lua_State* L,EntityLuaBinding* b) {
    static const luaL_Reg api[]={ {"spawn",spawn},{"exists",exists},{"remove",remove_entity},
        {"get",read_entity},{"set",set_entity},{"list",list_entities},{"at",entities_at},{"solid_box",solid_box},{"contacts",contacts},{"find",find_entity},{"type",entity_type},{"regions",regions},{NULL,NULL} };
    lua_newtable(L);
    for(const luaL_Reg* p=api;p->name;++p) {
        lua_pushlightuserdata(L,b);lua_pushcclosure(L,p->func,1);lua_setfield(L,-2,p->name);
    }
}
