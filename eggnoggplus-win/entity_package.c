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
static int valid_region_name(const char* name) {
    size_t i;
    if(!name || !name[0]) return 1;
    for(i=0;i<ENTITY_REGION_NAME_MAX && name[i];++i) {
        unsigned char c=(unsigned char)name[i];
        if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.')) return 0;
    }
    return i>0 && i<ENTITY_REGION_NAME_MAX;
}
static int valid_animation_name(const char* name) {
    return valid_region_name(name)&&name&&name[0]&&strcmp(name,"default");
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
uint32_t entity_package_resolve_animation(const EntityPackage* p,uint32_t type_id,const char* name,size_t length) {
    if(!p||!type_id||type_id>p->type_count||!name||!length||length>=ENTITY_ANIMATION_NAME_MAX||memchr(name,0,length))return 0;
    if(length==7&&!memcmp(name,"default",7))return UINT32_MAX;
    const EntityNamedType* type=&p->types[type_id-1];
    for(uint32_t i=0;i<type->animation_count;++i)if(strlen(type->animations[i].name)==length&&!memcmp(type->animations[i].name,name,length))return i+1u;
    return 0;
}
const char* entity_package_animation_name(const EntityPackage* p,uint32_t type_id,uint32_t animation_id) {
    if(!p||!type_id||type_id>p->type_count)return NULL;
    if(!animation_id)return "default";
    const EntityNamedType* type=&p->types[type_id-1];
    return animation_id<=type->animation_count?type->animations[animation_id-1u].name:NULL;
}
uint32_t entity_package_animation_count(const EntityPackage* p,uint32_t type_id) {
    return p&&type_id&&type_id<=p->type_count?p->types[type_id-1u].animation_count:0;
}
int entity_package_animation_visual(const EntityPackage* p,uint32_t type_id,uint32_t animation_id,EntityVisual* out) {
    const EntityNamedAnimation* animation;
    if(!entity_package_visual(p,type_id,out))return 0;
    if(!animation_id)return 1;
    if(animation_id>p->types[type_id-1u].animation_count)return 0;
    animation=&p->types[type_id-1u].animations[animation_id-1u];
    out->sprite=animation->sprite;out->frames=animation->frames;
    out->frame_ticks=animation->frame_ticks;out->animation_mode=animation->animation_mode;
    return 1;
}
uint32_t entity_visual_frame(const EntityVisual* v,uint64_t tick) {
    uint64_t index,period;
    if(!v || !v->frames || !v->frame_ticks) return 0;
    index=tick/v->frame_ticks;
    if(v->animation_mode==ENTITY_ANIMATION_ONCE) {
        if(index>=v->frames)index=v->frames-1u;
    } else if(v->animation_mode==ENTITY_ANIMATION_PING_PONG && v->frames>1u) {
        period=(uint64_t)v->frames*2u-2u;index%=period;
        if(index>=v->frames)index=period-index;
    } else index%=v->frames;
    return v->sprite+(uint32_t)index;
}
int entity_package_animation_status(const EntityPackage* p,const EntityValue* value,
    const char** name,uint32_t* frame,uint32_t* frames,uint32_t* mode,int* finished) {
    EntityVisual visual;
    if(!p||!value||!entity_package_animation_visual(p,value->type_id,value->animation_id,&visual))return 0;
    if(name)*name=entity_package_animation_name(p,value->type_id,value->animation_id);
    if(frame)*frame=entity_visual_frame(&visual,value->animation_tick)-visual.sprite;
    if(frames)*frames=visual.frames;
    if(mode)*mode=visual.animation_mode;
    if(finished)*finished=visual.animation_mode==ENTITY_ANIMATION_ONCE &&
        value->animation_tick/visual.frame_ticks>=visual.frames;
    return 1;
}
static int valid_visual(const EntityVisual* v) {
    size_t n=0;
    while(n<sizeof(v->sheet) && v->sheet[n]) n++;
    if(!n) return !v->sprite && !v->frames && !v->frame_ticks && !v->animation_mode && !v->offset_x && !v->offset_y && !v->scale_x && !v->scale_y && !v->rotation && !v->rgba && !v->layer;
    if(n>=sizeof(v->sheet) || v->sheet[0]=='.' || (n==8 && !memcmp(v->sheet,"builtin:",8))) return 0;
    for(size_t i=0;i<n;i++) {
        unsigned char c=(unsigned char)v->sheet[i];
        if(!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-' || c=='.' || c==':')) return 0;
        if(c==':' && (i!=7 || memcmp(v->sheet,"builtin",7))) return 0;
    }
    if(strncmp(v->sheet,"builtin:",8) && (n<5 || strcmp(v->sheet+n-4,".png"))) return 0;
    return v->frames && v->frames<=65536 && v->frame_ticks && v->frame_ticks<=1000000 &&
        (uint64_t)v->sprite+v->frames-1<=INT32_MAX && v->animation_mode<=ENTITY_ANIMATION_PING_PONG && v->layer<=1 &&
        v->offset_x>=-ENTITY_WORLD_COORD_LIMIT && v->offset_x<=ENTITY_WORLD_COORD_LIMIT &&
        v->offset_y>=-ENTITY_WORLD_COORD_LIMIT && v->offset_y<=ENTITY_WORLD_COORD_LIMIT &&
        v->scale_x && v->scale_x>=-65536 && v->scale_x<=65536 &&
        v->scale_y && v->scale_y>=-65536 && v->scale_y<=65536 &&
        v->rotation>=-INT32_C(92160000) && v->rotation<=INT32_C(92160000);
}
static void visual_u32(unsigned char* p,uint32_t v) {
    for(unsigned i=0;i<4;i++) p[i]=(unsigned char)(v>>(8*i));
}
EntityPackage* entity_package_create(const EntityNamedType* types,uint32_t count,
    const EntityPlacement* placements,uint32_t placed,uint32_t capacity,char* error,size_t error_size) {
    EntityPackage* p=NULL;EntityType* definitions=NULL;unsigned char* identity=NULL;size_t identity_size,offset;int has_visual=0,has_region_names=0,has_animations=0;
    uint8_t hash[32]; const char* failure="allocation failed";
    if(error && error_size) error[0]=0;
    if(!types || !count || count>ENTITY_WORLD_LIMIT || placed>capacity || (placed && !placements) ||
       !capacity || capacity>ENTITY_WORLD_LIMIT) { failure="invalid entity package count or capacity";goto fail; }
    for(uint32_t i=0;i<count;++i) if(!valid_name(types[i].key,1)) { failure="type key must be a bounded owner:name identifier";goto fail; }
    for(uint32_t i=0;i<count;++i) {
        if(!valid_visual(&types[i].visual)) { failure="invalid entity visual definition";goto fail; }
        if(types[i].visual.sheet[0]) has_visual=1;
        if(types[i].animation_count&&!types[i].visual.sheet[0]){failure="named animations require a base visual";goto fail;}
        if(types[i].animation_count>ENTITY_ANIMATION_MAX){failure="too many named animations";goto fail;}
        for(uint32_t j=0;j<types[i].animation_count;++j){
            const EntityNamedAnimation* animation=&types[i].animations[j];
            if(!valid_animation_name(animation->name)||!animation->frames||!animation->frame_ticks||animation->frames>65536u||animation->frame_ticks>1000000u||animation->animation_mode>ENTITY_ANIMATION_PING_PONG||(uint64_t)animation->sprite+animation->frames-1u>INT32_MAX){failure="invalid named animation";goto fail;}
            for(uint32_t k=0;k<j;++k)if(!strcmp(types[i].animations[k].name,animation->name)){failure="duplicate named animation";goto fail;}
        }
        if(types[i].animation_count)has_animations=1;
        for(uint32_t j=0;j<ENTITY_TYPE_REGIONS_MAX;++j) {
            const char* name=types[i].region_names[j];
            if(j>=types[i].definition.region_count && name[0]) {
                failure="unused entity region names must be empty";goto fail;
            }
            if(j<types[i].definition.region_count && !valid_region_name(name)) {
                failure="region name must be a 1-32 character local identifier";goto fail;
            }
            if(name[0]) {
                has_region_names=1;
                for(uint32_t k=0;k<j;++k) if(!strcmp(types[i].region_names[k],name)) {
                    failure="duplicate entity region name";goto fail;
                }
            }
        }
    }
    for(uint32_t i=0;i<placed;++i) if(!valid_name(placements[i].name,0) || !valid_name(placements[i].type,1)) { failure="placement requires a local name and qualified type key";goto fail; }
    p=calloc(1,sizeof(*p));if(!p) goto fail;
    p->types=calloc(count,sizeof(*p->types));p->placements=calloc(placed ? placed : 1,sizeof(*p->placements));
    p->handles=calloc(placed ? placed : 1,sizeof(*p->handles));definitions=calloc(count,sizeof(*definitions));
    if(!p->types || !p->placements || !p->handles || !definitions) goto fail;
    p->type_count=count;p->placement_count=placed;
    for(uint32_t i=0;i<count;++i) { strcpy(p->types[i].key,types[i].key);p->types[i].definition=types[i].definition;p->types[i].visual=types[i].visual;p->types[i].animation_count=types[i].animation_count;memcpy(p->types[i].animations,types[i].animations,sizeof(p->types[i].animations));memcpy(p->types[i].region_names,types[i].region_names,sizeof(p->types[i].region_names)); }
    qsort(p->types,count,sizeof(*p->types),type_order);
    for(uint32_t i=0;i<count;++i) {
        if(i && !strcmp(p->types[i-1].key,p->types[i].key)) { failure="duplicate entity type key";goto fail; }
        definitions[i]=p->types[i].definition;definitions[i].id=i+1;definitions[i].animation_count=p->types[i].animation_count;
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
    if(has_region_names) identity_size+=(size_t)count*(ENTITY_VISUAL_SHEET_MAX+44u+ENTITY_TYPE_REGIONS_MAX*ENTITY_REGION_NAME_MAX);
    else if(has_visual) identity_size+=(size_t)count*(ENTITY_VISUAL_SHEET_MAX+44u);
    if(has_animations)identity_size+=(size_t)count*(4u+ENTITY_ANIMATION_MAX*(ENTITY_ANIMATION_NAME_MAX+16u));
    identity=calloc(1,identity_size);if(!identity) goto fail;
    memcpy(identity,has_animations?"YEPM0006":has_region_names ? "YEPM0006" : has_visual ? "YEPM0006" : "YEPM0001",8);offset=8;
    for(uint32_t i=0;i<count;++i) { memcpy(identity+offset,p->types[i].key,strlen(p->types[i].key));offset+=ENTITY_PACKAGE_KEY_MAX; }
    for(uint32_t i=0;i<placed;++i) { memcpy(identity+offset,p->placements[i].name,strlen(p->placements[i].name));offset+=ENTITY_PACKAGE_KEY_MAX; }
    if(has_visual || has_region_names) for(uint32_t i=0;i<count;i++) {
        const EntityVisual* v=&p->types[i].visual;
        uint32_t fields[]={v->sprite,v->frames,v->frame_ticks,v->animation_mode,(uint32_t)v->offset_x,(uint32_t)v->offset_y,(uint32_t)v->scale_x,(uint32_t)v->scale_y,v->rgba,v->layer,(uint32_t)v->rotation};
        memcpy(identity+offset,v->sheet,strlen(v->sheet));offset+=ENTITY_VISUAL_SHEET_MAX;
        for(unsigned j=0;j<11;j++) { visual_u32(identity+offset,fields[j]);offset+=4; }
    }
    if(has_region_names) for(uint32_t i=0;i<count;++i) {
        memcpy(identity+offset,p->types[i].region_names,sizeof(p->types[i].region_names));
        offset+=sizeof(p->types[i].region_names);
    }
    if(has_animations)for(uint32_t i=0;i<count;++i){
        visual_u32(identity+offset,p->types[i].animation_count);offset+=4;
        for(uint32_t j=0;j<ENTITY_ANIMATION_MAX;++j){
            const EntityNamedAnimation* animation=&p->types[i].animations[j];
            memcpy(identity+offset,animation->name,strlen(animation->name));offset+=ENTITY_ANIMATION_NAME_MAX;
            visual_u32(identity+offset,animation->sprite);visual_u32(identity+offset+4,animation->frames);visual_u32(identity+offset+8,animation->frame_ticks);visual_u32(identity+offset+12,animation->animation_mode);offset+=16;
        }
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

int entity_package_snapshot_contains_handles(const EntityPackage* p,const void* bytes,size_t size,
                                             const EntityHandle* handles,size_t handle_count) {
    const unsigned char* in=bytes;
    if(!p || !in || size!=entity_package_snapshot_size(p) || memcmp(in,"YEP1",4) ||
       memcmp(in+4,p->fingerprint,64) || in[68] || in[69] || in[70] || in[71]) return 0;
    return entity_world_snapshot_contains_handles(p->world,in+PACKAGE_HEADER,size-PACKAGE_HEADER,
                                                  handles,handle_count);
}

int entity_package_load_for_tick(EntityPackage* p,const void* bytes,size_t size,uint64_t tick) {
    return entity_package_validate_for_tick(p,bytes,size,tick) && entity_package_load(p,bytes,size);
}

int entity_package_render_next(const EntityPackage* p,uint32_t* cursor,EntityRenderView* out) {
    EntityHandle h;EntityValue value;
    if(!p || !cursor || !out) return 0;
    while((h=entity_world_next(p->world,cursor))) {
        if(!entity_world_read(p->world,h,&value)) return 0;
        if(value.flags & ENTITY_FLAG_HIDDEN) continue;
        if(!entity_package_animation_visual(p,value.type_id,value.animation_id,&out->visual)) continue;
        if(value.flags & ENTITY_FLAG_MIRROR_X) {
            out->visual.offset_x=-out->visual.offset_x;
            out->visual.scale_x=-out->visual.scale_x;
            out->visual.rotation=-out->visual.rotation;
        }
        out->visual.scale_x=(int32_t)(((int64_t)out->visual.scale_x*(value.visual_scale_x?value.visual_scale_x:256))/256);
        out->visual.scale_y=(int32_t)(((int64_t)out->visual.scale_y*(value.visual_scale_y?value.visual_scale_y:256))/256);
        out->visual.offset_x+=value.visual_offset_x;out->visual.offset_y+=value.visual_offset_y;
        out->visual.rotation+=(value.flags&ENTITY_FLAG_MIRROR_X)
            ? -value.visual_rotation : value.visual_rotation;
        if(value.flags&ENTITY_FLAG_TINT_OVERRIDE){uint32_t composed=0;for(unsigned channel=0;channel<4;++channel){unsigned shift=24u-channel*8u;unsigned a=(out->visual.rgba>>shift)&255u,b=(value.visual_tint>>shift)&255u;composed|=((a*b+127u)/255u)<<shift;}out->visual.rgba=composed;}
        if(value.flags&ENTITY_FLAG_LAYER_OVERRIDE)out->visual.layer=value.visual_layer;
        out->handle=h;out->x=value.x;out->y=value.y;
        out->sprite=entity_visual_frame(&out->visual,value.animation_tick);return 1;
    }
    return 0;
}

uint32_t entity_package_type_count(const EntityPackage* p) { return p ? p->type_count : 0; }
const char* entity_package_type_key(const EntityPackage* p,uint32_t id) {
    return p && id && id<=p->type_count ? p->types[id-1].key : NULL;
}
const char* entity_package_region_name(const EntityPackage* p,uint32_t type_id,uint32_t region_id) {
    if(!p || !type_id || type_id>p->type_count || !region_id) return NULL;
    const EntityNamedType* type=&p->types[type_id-1];
    for(uint32_t i=0;i<type->definition.region_count;++i)
        if(type->definition.regions[i].id==region_id)
            return type->region_names[i][0]?type->region_names[i]:NULL;
    return NULL;
}
