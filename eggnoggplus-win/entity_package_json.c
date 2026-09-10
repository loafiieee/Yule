#include "entity_package.h"
#include "mod_json.h"
#include <luajit-2.1/lauxlib.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct PackageDecode {
    const char* json;
    size_t length;
    EntityNamedType* types;
    EntityPlacement* placements;
    EntityPackage* result;
    const EntityPackageLayout* layout;
    char error[256];
} PackageDecode;

static int fail(lua_State* L,const char* path,const char* message) {
    return luaL_error(L,"%s: %s",path,message);
}
static void object(lua_State* L,const char* path,const char* const* keys) {
    if(mod_json_table_kind(L,-1)!=2) fail(L,path,"expected object");
    lua_pushnil(L);
    while(lua_next(L,-2)) {
        size_t n;const char* key=lua_tolstring(L,-2,&n);size_t i;
        for(i=0;keys[i];i++) if(strlen(keys[i])==n && !memcmp(key,keys[i],n)) break;
        if(!keys[i]) luaL_error(L,"%s: unknown field '%s'",path,key);
        lua_pop(L,1);
    }
}
static uint32_t array(lua_State* L,const char* path,uint32_t limit) {
    size_t count;
    if(mod_json_table_kind(L,-1)!=1) fail(L,path,"expected array");
    count=lua_objlen(L,-1);
    if(count>limit) fail(L,path,"array exceeds limit");
    return (uint32_t)count;
}
static double number(lua_State* L,const char* key,const char* path,double min,double max,int optional) {
    double value;
    lua_getfield(L,-1,key);
    if(optional && lua_isnil(L,-1)) {lua_pop(L,1);return 0;}
    if(lua_type(L,-1)!=LUA_TNUMBER) luaL_error(L,"%s.%s: expected number",path,key);
    value=lua_tonumber(L,-1);
    if(!isfinite(value) || value<min || value>max) luaL_error(L,"%s.%s: number outside range",path,key);
    lua_pop(L,1);return value;
}
static uint32_t integer(lua_State* L,const char* key,const char* path,uint32_t min,uint32_t max) {
    double v=number(L,key,path,min,max,0);
    if(floor(v)!=v) luaL_error(L,"%s.%s: expected integer",path,key);
    return (uint32_t)v;
}
static int32_t coordinate(lua_State* L,const char* key,const char* path,int positive,int optional) {
    double bound=ENTITY_WORLD_COORD_LIMIT/256.0;
    double v=number(L,key,path,positive?1.0/256.0:-bound,bound,optional)*256.0;
    return (int32_t)(v>=0?floor(v+0.5):ceil(v-0.5));
}
static void name(lua_State* L,const char* key,const char* path,char* out,int qualified) {
    size_t n,i;const char* text;unsigned colons=0;
    lua_getfield(L,-1,key);
    if(lua_type(L,-1)!=LUA_TSTRING) luaL_error(L,"%s.%s: expected identifier",path,key);
    text=lua_tolstring(L,-1,&n);
    if(!n || n>=ENTITY_PACKAGE_KEY_MAX) luaL_error(L,"%s.%s: identifier length must be 1..96",path,key);
    for(i=0;i<n;i++) {
        unsigned char c=(unsigned char)text[i];
        if(c==':' && qualified && i && i+1<n) colons++;
        else if(!((c>='a' && c<='z') || (c>='0' && c<='9') || c=='_' || c=='-' || c=='.'))
            luaL_error(L,"%s.%s: invalid identifier",path,key);
    }
    if(colons!=(unsigned)qualified) luaL_error(L,"%s.%s: expected namespace:name",path,key);
    memcpy(out,text,n);out[n]=0;lua_pop(L,1);
}
static double visual_number(lua_State* L,const char* key,const char* path,double lo,double hi,double fallback,int whole) {
    double v;lua_getfield(L,-1,key);
    if(lua_isnil(L,-1)) {lua_pop(L,1);return fallback;}
    lua_pop(L,1);v=number(L,key,path,lo,hi,0);
    if(whole && floor(v)!=v) luaL_error(L,"%s.%s: expected integer",path,key);
    return v;
}
static void visual(lua_State* L,EntityVisual* v,const char* path) {
    static const char* const keys[]={"sheet","sprite","frames","frame_ticks","offset_x","offset_y","scale_x","scale_y","tint","layer",NULL};
    size_t n;const char* text;double x,y;
    object(L,path,keys);lua_getfield(L,-1,"sheet");
    if(lua_type(L,-1)!=LUA_TSTRING) fail(L,path,"visual sheet must be a string");
    text=lua_tolstring(L,-1,&n);
    if(!n || n>=sizeof(v->sheet) || memchr(text,0,n)) fail(L,path,"invalid visual sheet length");
    memcpy(v->sheet,text,n);lua_pop(L,1);
    v->sprite=integer(L,"sprite",path,0,INT32_MAX);
    v->frames=(uint32_t)visual_number(L,"frames",path,1,65536,1,1);
    v->frame_ticks=(uint32_t)visual_number(L,"frame_ticks",path,1,1000000,1,1);
    v->offset_x=coordinate(L,"offset_x",path,0,1);v->offset_y=coordinate(L,"offset_y",path,0,1);
    x=visual_number(L,"scale_x",path,-256,256,1,0)*256;
    y=visual_number(L,"scale_y",path,-256,256,1,0)*256;
    v->scale_x=(int32_t)(x<0?ceil(x-0.5):floor(x+0.5));
    v->scale_y=(int32_t)(y<0?ceil(y-0.5):floor(y+0.5));
    if(!v->scale_x || !v->scale_y) fail(L,path,"visual scale rounds to zero");
    v->layer=(uint32_t)visual_number(L,"layer",path,0,1,1,1);v->rgba=UINT32_MAX;
    lua_getfield(L,-1,"tint");
    if(!lua_isnil(L,-1)) {
        if(lua_type(L,-1)!=LUA_TSTRING) fail(L,path,"tint must be #RRGGBBAA");
        text=lua_tolstring(L,-1,&n);
        if(n!=9 || text[0]!='#') fail(L,path,"tint must be #RRGGBBAA");
        v->rgba=0;
        for(size_t i=1;i<9;i++) {
            unsigned char c=(unsigned char)text[i];unsigned digit;
            if(c>='0' && c<='9') digit=c-'0';else if(c>='a' && c<='f') digit=c-'a'+10;
            else if(c>='A' && c<='F') digit=c-'A'+10;else { fail(L,path,"invalid tint hex digit");return; }
            v->rgba=(v->rgba<<4)|digit;
        }
    }
    lua_pop(L,1);
}
static int decode(lua_State* L) {
    static const char* const root_keys[]={"schema","capacity","types","placements",NULL};
    static const char* const type_keys[]={"key","regions","visual",NULL};
    static const char* const region_keys[]={"id","role","layer","mask","x","y","width","height",NULL};
    static const char* const placement_keys[]={"name","type","x","y","vx","vy","room","side",NULL};
    static const char* const roles[]={"body","sensor","hitbox","hurtbox","solid"};
    PackageDecode* d=(PackageDecode*)lua_touserdata(L,1);
    uint32_t capacity,count,placed,expanded=0,schema,i,j,k;char path[160];
    lua_pushcfunction(L,mod_json_lua_decode);lua_pushlstring(L,d->json,d->length);lua_call(L,1,2);
    if(lua_isnil(L,-2)) return luaL_error(L,"%s",lua_tostring(L,-1));
    lua_pop(L,1);object(L,"$",root_keys);
    schema=integer(L,"schema","$",1,2);capacity=integer(L,"capacity","$",1,ENTITY_WORLD_LIMIT);
    lua_getfield(L,-1,"types");count=array(L,"$.types",ENTITY_WORLD_LIMIT);
    if(!count) return fail(L,"$.types","at least one type is required");
    d->types=(EntityNamedType*)calloc(count,sizeof(*d->types));
    if(!d->types) return fail(L,"$","out of memory");
    for(i=0;i<count;i++) {
        EntityType* type=&d->types[i].definition;
        lua_rawgeti(L,-1,(int)i+1);snprintf(path,sizeof(path),"$.types[%u]",i+1);object(L,path,type_keys);
        name(L,"key",path,d->types[i].key,1);
        for(k=0;k<i;k++) if(!strcmp(d->types[k].key,d->types[i].key)) return fail(L,path,"duplicate type key");
        lua_getfield(L,-1,"visual");
        if(!lua_isnil(L,-1)) {snprintf(path,sizeof(path),"$.types[%u].visual",i+1);visual(L,&d->types[i].visual,path);}
        lua_pop(L,1);
        lua_getfield(L,-1,"regions");snprintf(path,sizeof(path),"$.types[%u].regions",i+1);
        type->region_count=array(L,path,ENTITY_TYPE_REGIONS_MAX);
        for(j=0;j<type->region_count;j++) {
            EntityRegion* r=&type->regions[j];size_t n;const char* role;
            lua_rawgeti(L,-1,(int)j+1);snprintf(path,sizeof(path),"$.types[%u].regions[%u]",i+1,j+1);object(L,path,region_keys);
            r->id=integer(L,"id",path,1,UINT32_MAX);
            for(k=0;k<j;k++) if(type->regions[k].id==r->id) return fail(L,path,"duplicate region id");
            lua_getfield(L,-1,"role");
            if(lua_type(L,-1)!=LUA_TSTRING) return fail(L,path,"role must be body, sensor, hitbox or hurtbox");
            role=lua_tolstring(L,-1,&n);
            for(k=0;k<5;k++) if(strlen(roles[k])==n && !memcmp(role,roles[k],n)) break;
            if(k==5) return fail(L,path,"unknown region role");
            r->role=k+1;lua_pop(L,1);
            r->layer=integer(L,"layer",path,0,UINT32_MAX);r->mask=integer(L,"mask",path,0,UINT32_MAX);
            r->x=coordinate(L,"x",path,0,1);r->y=coordinate(L,"y",path,0,1);
            r->width=coordinate(L,"width",path,1,0);r->height=coordinate(L,"height",path,1,0);
            lua_pop(L,1);
        }
        lua_pop(L,2);
    }
    lua_pop(L,1);lua_getfield(L,-1,"placements");placed=array(L,"$.placements",capacity);
    if(placed) {d->placements=(EntityPlacement*)calloc(capacity,sizeof(*d->placements));if(!d->placements) return fail(L,"$","out of memory");}
    for(i=0;i<placed;i++) {
        EntityPlacement base={0};EntityPlacement* p=&base;int room=-1,side=2;
        lua_rawgeti(L,-1,(int)i+1);snprintf(path,sizeof(path),"$.placements[%u]",i+1);object(L,path,placement_keys);
        name(L,"name",path,p->name,0);name(L,"type",path,p->type,1);
        for(k=0;k<i;k++) {
            lua_rawgeti(L,-2,(int)k+1);lua_getfield(L,-1,"name");
            int duplicate=!strcmp(lua_tostring(L,-1),p->name);lua_pop(L,2);
            if(duplicate) return fail(L,path,"duplicate placement name");
        }

        for(k=0;k<count;k++) if(!strcmp(d->types[k].key,p->type)) break;
        if(k==count) return fail(L,path,"unknown placement type");
        p->value.x=coordinate(L,"x",path,0,1);p->value.y=coordinate(L,"y",path,0,1);
        p->value.vx=coordinate(L,"vx",path,0,1);p->value.vy=coordinate(L,"vy",path,0,1);
        lua_getfield(L,-1,"room");
        if(!lua_isnil(L,-1)) {
            size_t n;const char* key;
            if(schema!=2) return fail(L,path,"room placement requires schema 2");
            if(lua_type(L,-1)!=LUA_TSTRING) return fail(L,path,"room must name a source room");
            key=lua_tolstring(L,-1,&n);
            if(!n || n>96 || memchr(key,0,n)) return fail(L,path,"invalid room name");
            if(!d->layout || !d->layout->count) return fail(L,path,"room placement requires a map layout");
            for(uint32_t r=0;r<d->layout->count;r++) if(strlen(d->layout->rooms[r])==n && !memcmp(key,d->layout->rooms[r],n)) {room=(int)r;break;}
            if(room<0) return fail(L,path,"unknown source room");
        }
        lua_pop(L,1);lua_getfield(L,-1,"side");
        if(!lua_isnil(L,-1)) {
            size_t n;const char* key;
            if(schema!=2 || room<0 || lua_type(L,-1)!=LUA_TSTRING) return fail(L,path,"side requires a schema 2 room placement");
            key=lua_tolstring(L,-1,&n);
            if(n==6 && !memcmp(key,"source",6)) side=0;
            else if(n==8 && !memcmp(key,"mirrored",8)) side=1;
            else if(n==4 && !memcmp(key,"both",4)) side=2;
            else return fail(L,path,"side must be source, mirrored or both");
        }
        lua_pop(L,1);
        if(room==0 && side==1) return fail(L,path,"the center room has no mirrored copy");
        if(room>=0 && (p->value.x<0 || p->value.x>528*256 || p->value.y<0 || p->value.y>192*256))
            return fail(L,path,"room coordinates must be within 528 by 192 pixels");
        for(int mirror=0;mirror<2;mirror++) {
            EntityPlacement result=*p;
            if(room<0 && mirror) continue;
            if(room>=0 && ((mirror && (!room || side==0)) || (!mirror && side==1))) continue;
            if(room>=0) {
                int final=(int)d->layout->count-1+(mirror?room:-room);
                result.value.x=final*528*256+(mirror?528*256-p->value.x:p->value.x);
                if(mirror) {
                    size_t length=strlen(p->name);
                    if(length+7>=sizeof(result.name)) return fail(L,path,"mirrored placement name exceeds 96 characters");
                    memcpy(result.name+length,".mirror",8);
                    result.value.vx=-result.value.vx;result.value.flags=ENTITY_FLAG_MIRROR_X;
                }
            }
            if(expanded>=capacity) return fail(L,path,"expanded placements exceed capacity");
            for(k=0;k<expanded;k++) if(!strcmp(d->placements[k].name,result.name)) return fail(L,path,"duplicate expanded placement name");
            d->placements[expanded++]=result;
        }
        lua_pop(L,1);
    }
    d->result=entity_package_create(d->types,count,d->placements,expanded,capacity,d->error,sizeof(d->error));
    return 0;
}
EntityPackage* entity_package_decode_layout(const char* json,size_t length,const EntityPackageLayout* layout,char* error,size_t error_size) {
    PackageDecode d;lua_State* L;
    memset(&d,0,sizeof(d));d.json=json;d.length=length;d.layout=layout;
    if(layout) {
        if(layout->count>9) {if(error && error_size)snprintf(error,error_size,"invalid source room count");return NULL;}
        for(uint32_t i=0;i<layout->count;i++) {
            if(!layout->rooms[i] || !layout->rooms[i][0] || strlen(layout->rooms[i])>96) {if(error && error_size)snprintf(error,error_size,"invalid source room name");return NULL;}
            for(uint32_t j=0;j<i;j++) if(!strcmp(layout->rooms[i],layout->rooms[j])) {if(error && error_size)snprintf(error,error_size,"duplicate source room name");return NULL;}
        }
    }
    if(error && error_size) error[0]=0;
    if(!json || length>MOD_JSON_MAX_INPUT_BYTES) {snprintf(d.error,sizeof(d.error),"$: missing input or input exceeds 1048576 bytes");L=NULL;}
    else {
        L=luaL_newstate();
        if(!L) snprintf(d.error,sizeof(d.error),"$: could not allocate JSON decoder");
        else {
            lua_pushcfunction(L,decode);lua_pushlightuserdata(L,&d);
            if(lua_pcall(L,1,0,0)!=0) snprintf(d.error,sizeof(d.error),"%s",lua_tostring(L,-1));
        }
    }
    if(L) lua_close(L);
    free(d.types);free(d.placements);
    if(error && error_size) snprintf(error,error_size,"%s",d.error);
    return d.result;
}

EntityPackage* entity_package_decode(const char* json,size_t length,char* error,size_t error_size) {
    return entity_package_decode_layout(json,length,NULL,error,error_size);
}
