#include "entity_package.h"
#include "content_registry.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define PACKAGE_HEADER 72u
struct EntityPackage {
    EntityNamedType* types; uint32_t type_count;
    EntityPlacement* placements; EntityHandle* handles; uint32_t placement_count;
    EntityWorld* world; char fingerprint[65];
};
static int valid_name(const char* name,int qualified) {
    size_t i;int colons=0;
    for(i=0;i<ENTITY_PACKAGE_KEY_MAX && name[i];++i) {
        unsigned char c=(unsigned char)name[i];
        if(c==':') { if(!i || i+1>=ENTITY_PACKAGE_KEY_MAX || !name[i+1] || ++colons>1) return 0; }
        else if(!((c>='a' && c<='z') || (c>='0' && c<='9') || c=='_' || c=='-' || c=='.')) return 0;
    }
    return i>0 && i<ENTITY_PACKAGE_KEY_MAX && colons==(qualified ? 1 : 0);
}
static int type_order(const void* a,const void* b) { return strcmp(((const EntityNamedType*)a)->key,((const EntityNamedType*)b)->key); }
static int placement_order(const void* a,const void* b) { return strcmp(((const EntityPlacement*)a)->name,((const EntityPlacement*)b)->name); }
void entity_package_free(EntityPackage* p) {
    if(p) { entity_world_free(p->world);free(p->types);free(p->placements);free(p->handles);free(p); }
}
EntityWorld* entity_package_world(EntityPackage* p) { return p ? p->world : NULL; }
uint32_t entity_package_resolve_type(void* package,const char* key,size_t length) {
    EntityPackage* p=package;char name[ENTITY_PACKAGE_KEY_MAX];uint32_t lo=0,hi;
    if(!p || !key || !length || length>=sizeof(name) || memchr(key,0,length)) return 0;
    memcpy(name,key,length);name[length]=0;hi=p->type_count;
    while(lo<hi) { uint32_t mid=lo+(hi-lo)/2; if(strcmp(p->types[mid].key,name)<0) lo=mid+1;else hi=mid; }
    return lo<p->type_count && !strcmp(p->types[lo].key,name) ? lo+1 : 0;
}
EntityHandle entity_package_placement(const EntityPackage* p,const char* name) {
    if(!p || !name) return 0;
    for(uint32_t i=0;i<p->placement_count;++i) if(!strcmp(p->placements[i].name,name)) return p->handles[i];
    return 0;
}
const char* entity_package_fingerprint(const EntityPackage* p) { return p ? p->fingerprint : NULL; }
int entity_package_visual(const EntityPackage* p,uint32_t id,EntityVisual* out) {
    if(!p || !out || !id || id>p->type_count) return 0;
    *out=p->types[id-1].visual;return out->sheet[0]!=0;
}
uint32_t entity_visual_frame(const EntityVisual* v,uint64_t tick) {
    if(!v || !v->frames || !v->frame_ticks) return 0;
    return v->sprite+(uint32_t)((tick/v->frame_ticks)%v->frames);
}
static int valid_visual(const EntityVisual* v) {
    size_t n=0;
    while(n<sizeof(v->sheet) && v->sheet[n]) n++;
    if(!n) return !v->sprite && !v->frames && !v->frame_ticks && !v->offset_x && !v->offset_y && !v->scale_x && !v->scale_y && !v->rgba && !v->layer;
    if(n>=sizeof(v->sheet) || v->sheet[0]=='.' || (n==8 && !memcmp(v->sheet,"builtin:",8))) return 0;
    for(size_t i=0;i<n;i++) {
        unsigned char c=(unsigned char)v->sheet[i];
        if(!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-' || c=='.' || c==':')) return 0;
        if(c==':' && (i!=7 || memcmp(v->sheet,"builtin",7))) return 0;
    }
    if(strncmp(v->sheet,"builtin:",8) && (n<5 || strcmp(v->sheet+n-4,".png"))) return 0;
    return v->frames && v->frames<=65536 && v->frame_ticks && v->frame_ticks<=1000000 &&
        (uint64_t)v->sprite+v->frames-1<=INT32_MAX && v->layer<=1 &&
        v->offset_x>=-ENTITY_WORLD_COORD_LIMIT && v->offset_x<=ENTITY_WORLD_COORD_LIMIT &&
        v->offset_y>=-ENTITY_WORLD_COORD_LIMIT && v->offset_y<=ENTITY_WORLD_COORD_LIMIT &&
        v->scale_x && v->scale_x>=-65536 && v->scale_x<=65536 &&
        v->scale_y && v->scale_y>=-65536 && v->scale_y<=65536;
}
static void visual_u32(unsigned char* p,uint32_t v) {
    for(unsigned i=0;i<4;i++) p[i]=(unsigned char)(v>>(8*i));
}
EntityPackage* entity_package_create(const EntityNamedType* types,uint32_t count,
    const EntityPlacement* placements,uint32_t placed,uint32_t capacity,char* error,size_t error_size) {
    EntityPackage* p=NULL;EntityType* definitions=NULL;unsigned char* identity=NULL;size_t identity_size,offset;int has_visual=0;
    uint8_t hash[32]; const char* failure="allocation failed";
    if(error && error_size) error[0]=0;
    if(!types || !count || count>ENTITY_WORLD_LIMIT || placed>capacity || (placed && !placements) ||
       !capacity || capacity>ENTITY_WORLD_LIMIT) { failure="invalid entity package count or capacity";goto fail; }
    for(uint32_t i=0;i<count;++i) if(!valid_name(types[i].key,1)) { failure="type key must be a bounded owner:name identifier";goto fail; }
    for(uint32_t i=0;i<count;++i) {
        if(!valid_visual(&types[i].visual)) { failure="invalid entity visual definition";goto fail; }
        if(types[i].visual.sheet[0]) has_visual=1;
    }
    for(uint32_t i=0;i<placed;++i) if(!valid_name(placements[i].name,0) || !valid_name(placements[i].type,1)) { failure="placement requires a local name and qualified type key";goto fail; }
    p=calloc(1,sizeof(*p));if(!p) goto fail;
    p->types=calloc(count,sizeof(*p->types));p->placements=calloc(placed ? placed : 1,sizeof(*p->placements));
    p->handles=calloc(placed ? placed : 1,sizeof(*p->handles));definitions=calloc(count,sizeof(*definitions));
    if(!p->types || !p->placements || !p->handles || !definitions) goto fail;
    p->type_count=count;p->placement_count=placed;
    for(uint32_t i=0;i<count;++i) { strcpy(p->types[i].key,types[i].key);p->types[i].definition=types[i].definition;p->types[i].visual=types[i].visual; }
    qsort(p->types,count,sizeof(*p->types),type_order);
    for(uint32_t i=0;i<count;++i) {
        if(i && !strcmp(p->types[i-1].key,p->types[i].key)) { failure="duplicate entity type key";goto fail; }
        definitions[i]=p->types[i].definition;definitions[i].id=i+1;
    }
    p->world=entity_world_create(capacity);
    if(!p->world) goto fail;
    if(!entity_world_define_types(p->world,definitions,count)) { failure="invalid entity region definitions";goto fail; }
    for(uint32_t i=0;i<placed;++i) {
        strcpy(p->placements[i].name,placements[i].name);strcpy(p->placements[i].type,placements[i].type);
        p->placements[i].value=placements[i].value;
    }
    qsort(p->placements,placed,sizeof(*p->placements),placement_order);
    for(uint32_t i=0;i<placed;++i) {
        EntityPlacement* item=&p->placements[i];
        if(i && !strcmp(p->placements[i-1].name,item->name)) { failure="duplicate placement name";goto fail; }
        item->value.type_id=entity_package_resolve_type(p,item->type,strlen(item->type));
        if(!item->value.type_id) { failure="placement references an unknown entity type";goto fail; }
        p->handles[i]=entity_world_spawn(p->world,&item->value);
        if(!p->handles[i]) { failure="placement position/velocity is invalid or capacity exhausted";goto fail; }
    }
    /* Explicit fixed-size names plus the canonical initial world binds capacity,
     * sorted type IDs, geometry, placement values, and placement names. */
    identity_size=8u+(size_t)(count+placed)*ENTITY_PACKAGE_KEY_MAX+entity_world_snapshot_size(p->world);
    if(has_visual) identity_size+=(size_t)count*(ENTITY_VISUAL_SHEET_MAX+36u);
    identity=calloc(1,identity_size);if(!identity) goto fail;
    memcpy(identity,has_visual ? "YEPM0002" : "YEPM0001",8);offset=8;
    for(uint32_t i=0;i<count;++i) { memcpy(identity+offset,p->types[i].key,strlen(p->types[i].key));offset+=ENTITY_PACKAGE_KEY_MAX; }
    for(uint32_t i=0;i<placed;++i) { memcpy(identity+offset,p->placements[i].name,strlen(p->placements[i].name));offset+=ENTITY_PACKAGE_KEY_MAX; }
    if(has_visual) for(uint32_t i=0;i<count;i++) {
        const EntityVisual* v=&p->types[i].visual;
        uint32_t fields[]={v->sprite,v->frames,v->frame_ticks,(uint32_t)v->offset_x,(uint32_t)v->offset_y,(uint32_t)v->scale_x,(uint32_t)v->scale_y,v->rgba,v->layer};
        memcpy(identity+offset,v->sheet,strlen(v->sheet));offset+=ENTITY_VISUAL_SHEET_MAX;
        for(unsigned j=0;j<9;j++) { visual_u32(identity+offset,fields[j]);offset+=4; }
    }
    if(!entity_world_save(p->world,identity+offset,identity_size-offset)) goto fail;
    if(!content_registry_sha256_bytes(identity,identity_size,hash,p->fingerprint,error,error_size)) { failure="entity package identity hashing failed";goto fail; }
    free(identity);free(definitions);return p;
fail:
    if(error && error_size) snprintf(error,error_size,"%s",failure);
    free(identity);free(definitions);entity_package_free(p);return NULL;
}
size_t entity_package_snapshot_size(const EntityPackage* p) { return p ? PACKAGE_HEADER+entity_world_snapshot_size(p->world) : 0; }
int entity_package_save(const EntityPackage* p,void* bytes,size_t size) {
    unsigned char* out=bytes;
    if(!p || !out || size!=entity_package_snapshot_size(p)) return 0;
    memset(out,0,PACKAGE_HEADER);memcpy(out,"YEP1",4);memcpy(out+4,p->fingerprint,64);
    return entity_world_save(p->world,out+PACKAGE_HEADER,size-PACKAGE_HEADER);
}
int entity_package_load(EntityPackage* p,const void* bytes,size_t size) {
    const unsigned char* in=bytes;
    if(!p || !in || size!=entity_package_snapshot_size(p) || memcmp(in,"YEP1",4) ||
       memcmp(in+4,p->fingerprint,64) || in[68] || in[69] || in[70] || in[71]) return 0;
    return entity_world_load(p->world,in+PACKAGE_HEADER,size-PACKAGE_HEADER);
}

int entity_package_validate_for_tick(const EntityPackage* p,const void* bytes,size_t size,uint64_t tick) {
    const unsigned char* in=bytes;
    uint64_t encoded=0;
    if(!p || !in || size!=entity_package_snapshot_size(p) || size<PACKAGE_HEADER+24u) return 0;
    /* YEW1 tick is an explicit little-endian u64 at world offset 16. */
    for(unsigned i=0;i<8;i++) encoded|=(uint64_t)in[PACKAGE_HEADER+16u+i]<<(8u*i);
    return encoded==tick && !memcmp(in,"YEP1",4) && !memcmp(in+4,p->fingerprint,64) &&
        !in[68] && !in[69] && !in[70] && !in[71] &&
        entity_world_validate_snapshot(p->world,in+PACKAGE_HEADER,size-PACKAGE_HEADER);
}

int entity_package_load_for_tick(EntityPackage* p,const void* bytes,size_t size,uint64_t tick) {
    return entity_package_validate_for_tick(p,bytes,size,tick) && entity_package_load(p,bytes,size);
}

int entity_package_render_next(const EntityPackage* p,uint32_t* cursor,EntityRenderView* out) {
    EntityHandle h;EntityValue value;
    if(!p || !cursor || !out) return 0;
    while((h=entity_world_next(p->world,cursor))) {
        if(!entity_world_read(p->world,h,&value)) return 0;
        if(!entity_package_visual(p,value.type_id,&out->visual)) continue;
        if(value.flags & ENTITY_FLAG_MIRROR_X) {
            out->visual.offset_x=-out->visual.offset_x;
            out->visual.scale_x=-out->visual.scale_x;
        }
        out->handle=h;out->x=value.x;out->y=value.y;
        out->sprite=entity_visual_frame(&out->visual,value.animation_tick);return 1;
    }
    return 0;
}

uint32_t entity_package_type_count(const EntityPackage* p) { return p ? p->type_count : 0; }
const char* entity_package_type_key(const EntityPackage* p,uint32_t id) {
    return p && id && id<=p->type_count ? p->types[id-1].key : NULL;
}
