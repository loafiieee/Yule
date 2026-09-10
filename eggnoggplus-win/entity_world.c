#include "entity_world.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

typedef struct EntitySlot { uint32_t generation, active; EntityValue value; } EntitySlot;
struct EntityWorld { uint32_t capacity, count; uint64_t tick; EntitySlot* slots; EntityType* types; uint32_t type_count; int spawned; int updating, update_failed; int has_solids; };
#define EW_HEADER 32u
#define EW_RECORD 40u
#define EW_TYPE_BYTES (8u + ENTITY_TYPE_REGIONS_MAX * 32u)
static int valid_value(const EntityValue* v) {
    return v && v->animation_tick<=ENTITY_ANIMATION_TICK_MAX && v->type_id && v->x >= -ENTITY_WORLD_COORD_LIMIT && v->x <= ENTITY_WORLD_COORD_LIMIT &&
        v->y >= -ENTITY_WORLD_COORD_LIMIT && v->y <= ENTITY_WORLD_COORD_LIMIT &&
        v->vx >= -ENTITY_WORLD_COORD_LIMIT && v->vx <= ENTITY_WORLD_COORD_LIMIT &&
        v->vy >= -ENTITY_WORLD_COORD_LIMIT && v->vy <= ENTITY_WORLD_COORD_LIMIT;
}
EntityWorld* entity_world_create(uint32_t capacity) {
    EntityWorld* w;
    if (!capacity || capacity > ENTITY_WORLD_LIMIT) return NULL;
    w = calloc(1, sizeof(*w));
    if (!w) return NULL;
    w->slots = calloc(capacity, sizeof(*w->slots));
    if (!w->slots) { free(w); return NULL; }
    w->capacity = capacity;
    return w;
}
void entity_world_free(EntityWorld* w) { if (w) { free(w->types); free(w->slots); free(w); } }
uint32_t entity_world_count(const EntityWorld* w) { return w ? w->count : 0; }
uint64_t entity_world_tick(const EntityWorld* w) { return w ? w->tick : 0; }
static EntitySlot* find_slot(const EntityWorld* w, EntityHandle h) {
    uint32_t index = (uint32_t)h;
    EntitySlot* slot;
    if (!w || !index || index > w->capacity) return NULL;
    slot = &w->slots[index - 1];
    return slot->active && slot->generation == (uint32_t)(h >> 32) ? slot : NULL;
}
static const EntityType* find_type(const EntityWorld* w, uint32_t id) {
    uint32_t lo=0, hi=w->type_count;
    while(lo<hi) {
        uint32_t mid=lo+(hi-lo)/2;
        if(w->types[mid].id<id) lo=mid+1;
        else hi=mid;
    }
    return lo<w->type_count && w->types[lo].id==id ? &w->types[lo] : NULL;
}
int entity_world_read_type(const EntityWorld* w,uint32_t id,EntityType* out) {
    const EntityType* type;
    if(!w || !out || !(type=find_type(w,id))) return 0;
    *out=*type;return 1;
}
EntityHandle entity_world_spawn(EntityWorld* w, const EntityValue* value) {
    uint32_t i;
    if (!w || !valid_value(value) || (w->types && !find_type(w,value->type_id))) return 0;
    for (i=0; i<w->capacity; ++i) {
        EntitySlot* slot = &w->slots[i];
        /* Exhausted generations retire a slot instead of reviving stale handles. */
        if (slot->active || slot->generation == UINT32_MAX) continue;
        w->spawned=1; slot->generation++; slot->active=1; slot->value=*value; w->count++;
        return ((uint64_t)slot->generation << 32) | (i+1u);
    }
    return 0;
}
int entity_world_remove(EntityWorld* w, EntityHandle h) {
    EntitySlot* slot=find_slot(w,h);
    if (!slot) return 0;
    slot->active=0; memset(&slot->value,0,sizeof(slot->value)); w->count--; return 1;
}
int entity_world_read(const EntityWorld* w, EntityHandle h, EntityValue* out) {
    EntitySlot* slot=find_slot(w,h);
    if (!slot || !out) return 0;
    *out=slot->value; return 1;
}
int entity_world_write(EntityWorld* w, EntityHandle h, const EntityValue* value) {
    EntitySlot* slot=find_slot(w,h);
    if (!slot || !valid_value(value) || value->type_id != slot->value.type_id) return 0;
    slot->value=*value; return 1;
}
EntityHandle entity_world_next(const EntityWorld* w, uint32_t* cursor) {
    if (!w || !cursor) return 0;
    while (*cursor < w->capacity) {
        uint32_t i=(*cursor)++;
        if (w->slots[i].active) return ((uint64_t)w->slots[i].generation<<32)|(i+1u);
    }
    return 0;
}
int entity_world_step(EntityWorld* w) {
    uint32_t i;
    if (!w || w->tick==UINT64_MAX) return 0;
    if (w->updating) { w->update_failed=1; return 0; }
    for (i=0; i<w->capacity; ++i) if (w->slots[i].active) {
        const EntityValue* v=&w->slots[i].value;
        int64_t x=(int64_t)v->x+((v->flags & ENTITY_FLAG_MANUAL_MOTION)?0:v->vx), y=(int64_t)v->y+((v->flags & ENTITY_FLAG_MANUAL_MOTION)?0:v->vy);
        if(!(v->flags & ENTITY_FLAG_ANIMATION_PAUSED) && v->animation_tick==ENTITY_ANIMATION_TICK_MAX)return 0;
        if (x < -ENTITY_WORLD_COORD_LIMIT || x > ENTITY_WORLD_COORD_LIMIT ||
            y < -ENTITY_WORLD_COORD_LIMIT || y > ENTITY_WORLD_COORD_LIMIT) return 0;
    }
    for (i=0; i<w->capacity; ++i) if (w->slots[i].active) {
        if(!(w->slots[i].value.flags & ENTITY_FLAG_MANUAL_MOTION)){
            w->slots[i].value.x+=w->slots[i].value.vx;
            w->slots[i].value.y+=w->slots[i].value.vy;
        }
        if(!(w->slots[i].value.flags & ENTITY_FLAG_ANIMATION_PAUSED))w->slots[i].value.animation_tick++;
    }
    w->tick++; return 1;
}
static void put32(unsigned char* p,uint32_t v) { for (int i=0;i<4;++i) p[i]=(unsigned char)(v>>(i*8)); }
static uint32_t get32(const unsigned char* p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static int32_t signed32(uint32_t v) { return v<=INT32_MAX ? (int32_t)v : -1-(int32_t)(UINT32_MAX-v); }
static void encode_type(unsigned char* p,const EntityType* t) {
    memset(p,0,EW_TYPE_BYTES); put32(p,t->id); put32(p+4,t->region_count);
    for(uint32_t i=0;i<t->region_count;++i) {
        const EntityRegion* r=&t->regions[i]; unsigned char* q=p+8+i*32;
        put32(q,r->id);put32(q+4,r->role);put32(q+8,r->layer);put32(q+12,r->mask);
        put32(q+16,(uint32_t)r->x);put32(q+20,(uint32_t)r->y);
        put32(q+24,(uint32_t)r->width);put32(q+28,(uint32_t)r->height);
    }
}
size_t entity_world_snapshot_size(const EntityWorld* w) { return w ? EW_HEADER+(size_t)w->capacity*EW_RECORD+(size_t)w->type_count*EW_TYPE_BYTES : 0; }
int entity_world_save(const EntityWorld* w,void* bytes,size_t size) {
    unsigned char* p=bytes;
    uint32_t i;
    if (!w || !p || size != entity_world_snapshot_size(w)) return 0;
    memset(p,0,size); memcpy(p,"YEW1",4); put32(p+4,2); put32(p+8,w->capacity); put32(p+12,w->count);
    put32(p+16,(uint32_t)w->tick); put32(p+20,(uint32_t)(w->tick>>32)); put32(p+24,w->type_count);
    for(uint32_t t=0;t<w->type_count;++t) encode_type(p+EW_HEADER+w->capacity*EW_RECORD+t*EW_TYPE_BYTES,&w->types[t]);
    for(i=0;i<w->capacity;++i) {
        const EntitySlot* s=&w->slots[i]; unsigned char* r=p+EW_HEADER+i*EW_RECORD;
        put32(r,s->generation); put32(r+4,s->active); put32(r+8,s->value.type_id);
        put32(r+12,(uint32_t)s->value.x); put32(r+16,(uint32_t)s->value.y);
        put32(r+20,(uint32_t)s->value.vx); put32(r+24,(uint32_t)s->value.vy); put32(r+28,s->value.flags);
        put32(r+32,(uint32_t)s->value.animation_tick);put32(r+36,(uint32_t)(s->value.animation_tick>>32));
    }
    return 1;
}
static EntitySlot decode_slot(const unsigned char* r) {
    EntitySlot s;
    memset(&s,0,sizeof(s)); s.generation=get32(r); s.active=get32(r+4);
    s.value.type_id=get32(r+8); s.value.x=signed32(get32(r+12)); s.value.y=signed32(get32(r+16));
    s.value.vx=signed32(get32(r+20)); s.value.vy=signed32(get32(r+24)); s.value.flags=get32(r+28);
    s.value.animation_tick=(uint64_t)get32(r+32)|((uint64_t)get32(r+36)<<32);
    return s;
}
int entity_world_validate_snapshot(const EntityWorld* w,const void* bytes,size_t size) {
    const unsigned char* p=bytes; uint32_t count=0,i;
    if (!w || !p || size!=entity_world_snapshot_size(w) || memcmp(p,"YEW1",4) ||
        get32(p+4)!=2 || get32(p+8)!=w->capacity || get32(p+12)>w->capacity || get32(p+24)!=w->type_count || get32(p+28)) return 0;
    for(uint32_t t=0;t<w->type_count;++t) {
        unsigned char expected[EW_TYPE_BYTES]; encode_type(expected,&w->types[t]);
        if(memcmp(expected,p+EW_HEADER+w->capacity*EW_RECORD+t*EW_TYPE_BYTES,EW_TYPE_BYTES)) return 0;
    }
    for(i=0;i<w->capacity;++i) {
        const unsigned char* r=p+EW_HEADER+i*EW_RECORD; EntitySlot s=decode_slot(r);
        if (s.active>1) return 0;
        if (s.active) { if (!s.generation || !valid_value(&s.value) || (w->types && !find_type(w,s.value.type_id))) return 0; count++; }
        else { for (uint32_t j=8;j<EW_RECORD;++j) if (r[j]) return 0; }
    }
    if (count!=get32(p+12)) return 0;
    return 1;
}
int entity_world_load(EntityWorld* w,const void* bytes,size_t size) {
    const unsigned char* p=bytes;
    if (w && w->updating) { w->update_failed=1; return 0; }
    if (!entity_world_validate_snapshot(w,bytes,size)) return 0;
    for(uint32_t i=0;i<w->capacity;++i) w->slots[i]=decode_slot(p+EW_HEADER+i*EW_RECORD);
    w->spawned=1; w->count=get32(p+12); w->tick=(uint64_t)get32(p+16)|((uint64_t)get32(p+20)<<32); return 1;
}

static int type_order(const void* lhs,const void* rhs) {
    uint32_t a=((const EntityType*)lhs)->id, b=((const EntityType*)rhs)->id;
    return a<b ? -1 : a>b;
}
int entity_world_define_types(EntityWorld* w,const EntityType* types,uint32_t count) {
    EntityType* copy;
    if (!w || w->updating || w->types || w->spawned || w->tick || !types || !count || count>ENTITY_WORLD_LIMIT) return 0;
    for(uint32_t i=0;i<count;++i) {
        const EntityType* t=&types[i];
        if (!t->id || t->region_count>ENTITY_TYPE_REGIONS_MAX) return 0;
        for(uint32_t j=0;j<i;++j) if(types[j].id==t->id) return 0;
        for(uint32_t j=0;j<t->region_count;++j) {
            const EntityRegion* r=&t->regions[j];
            if (!r->id || r->role<ENTITY_REGION_BODY || r->role>ENTITY_REGION_SOLID ||
                r->width<=0 || r->height<=0 || r->width>ENTITY_WORLD_COORD_LIMIT || r->height>ENTITY_WORLD_COORD_LIMIT ||
                r->x < -ENTITY_WORLD_COORD_LIMIT || r->x>ENTITY_WORLD_COORD_LIMIT ||
                r->y < -ENTITY_WORLD_COORD_LIMIT || r->y>ENTITY_WORLD_COORD_LIMIT) return 0;
            for(uint32_t k=0;k<j;++k) if(t->regions[k].id==r->id) return 0;
        }
    }
    copy=calloc(count,sizeof(*copy)); if(!copy) return 0;
    for(uint32_t i=0;i<count;++i) {
        copy[i].id=types[i].id; copy[i].region_count=types[i].region_count;
        memcpy(copy[i].regions,types[i].regions,types[i].region_count*sizeof(EntityRegion));
    }
    qsort(copy,count,sizeof(*copy),type_order);
    w->types=copy; w->type_count=count;
    for(uint32_t i=0;i<count;i++)for(uint32_t j=0;j<copy[i].region_count;j++)if(copy[i].regions[j].role==ENTITY_REGION_SOLID)w->has_solids=1;
    return 1;
}
int64_t entity_region_local_x(const EntityValue* v,const EntityRegion* r) {
    return v->flags & ENTITY_FLAG_MIRROR_X ? -(int64_t)r->x-r->width : r->x;
}
static int region_overlap(const EntityValue* a,const EntityRegion* ar,const EntityValue* b,const EntityRegion* br) {
    int64_t ax=(int64_t)a->x+entity_region_local_x(a,ar),ay=(int64_t)a->y+ar->y;
    int64_t bx=(int64_t)b->x+entity_region_local_x(b,br),by=(int64_t)b->y+br->y;
    return (ar->mask & br->layer) && (br->mask & ar->layer) &&
        ax<bx+br->width && bx<ax+ar->width && ay<by+br->height && by<ay+ar->height;
}
static int contact_work(size_t* comparisons,uint32_t* budget) {
    if(++*comparisons>1000000u) return 0;
    if(budget) { if(!*budget) return 0; --*budget; }
    return 1;
}
size_t entity_world_contacts_budgeted(const EntityWorld* w,EntityContact* out,size_t capacity,uint32_t* budget) {
    size_t required=0,comparisons=0;
    if(!w || !w->types || (!out && capacity)) return SIZE_MAX;
    /* Count and reserve all work before touching caller output. The world cannot
     * mutate during this synchronous query, so the write pass costs the same. */
    for(int pass=0;pass<2;++pass) {
        size_t n=0;
        for(uint32_t i=0;i<w->capacity;++i) {
            if(!pass && !contact_work(&comparisons,budget)) return SIZE_MAX;
            if(!w->slots[i].active) continue;
            const EntityType* a=find_type(w,w->slots[i].value.type_id);
            for(uint32_t j=i+1;j<w->capacity;++j) {
                if(!pass && !contact_work(&comparisons,budget)) return SIZE_MAX;
                if(!w->slots[j].active) continue;
                const EntityType* b=find_type(w,w->slots[j].value.type_id);
                for(uint32_t ai=0;ai<a->region_count;++ai) for(uint32_t bi=0;bi<b->region_count;++bi) {
                    if(!pass && !contact_work(&comparisons,budget)) return SIZE_MAX;
                    if(!region_overlap(&w->slots[i].value,&a->regions[ai],&w->slots[j].value,&b->regions[bi])) continue;
                    if(pass) {
                        out[n].a=((uint64_t)w->slots[i].generation<<32)|(i+1u);
                        out[n].b=((uint64_t)w->slots[j].generation<<32)|(j+1u);
                        out[n].region_a=a->regions[ai].id; out[n].region_b=b->regions[bi].id;
                        out[n].role_a=a->regions[ai].role; out[n].role_b=b->regions[bi].role;
                    }
                    n++;
                }
            }
        }
        if(!pass) {
            required=n;if(!out || capacity<required || !required) return required;
            if(budget) {
                if(*budget<comparisons) { *budget=0;return SIZE_MAX; }
                *budget-=(uint32_t)comparisons;
            }
        }
    }
    return required;
}
size_t entity_world_contacts(const EntityWorld* w,EntityContact* out,size_t capacity) {
    return entity_world_contacts_budgeted(w,out,capacity,NULL);
}

int entity_world_update(EntityWorld* w,EntityUpdateFn callback,void* user) {
    unsigned char* before;
    EntityHandle* handles;
    uint32_t cursor=0,count=0;
    size_t size;
    int ok=1,spawned;
    EntityHandle h;
    if(!w || !callback) return 0;
    if(w->updating) { w->update_failed=1; return 0; }
    size=entity_world_snapshot_size(w);
    before=malloc(size); handles=malloc((size_t)w->capacity*sizeof(*handles));
    if(!before || !handles) { free(before);free(handles);return 0; }
    if(!entity_world_save(w,before,size)) { free(before);free(handles);return 0; }
    spawned=w->spawned;
    while((h=entity_world_next(w,&cursor))!=0) handles[count++]=h;
    w->updating=1; w->update_failed=0;
    for(uint32_t i=0;i<count;++i) {
        if(!find_slot(w,handles[i])) continue;
        if(!callback(w,handles[i],user) || w->update_failed) { ok=0;break; }
    }
    w->updating=0;
    if(ok) ok=entity_world_step(w);
    if(!ok) {
        /* Owned, freshly encoded bytes and immutable definitions: load cannot
         * fail here. No callback runs during the restoring copy. */
        (void)entity_world_load(w,before,size); w->spawned=spawned;
    }
    w->update_failed=0;
    free(before);free(handles);return ok;
}

unsigned entity_world_sweep_solids(const EntityWorld* w,double old_x,double old_y,double radius,double* x,double* y){
    unsigned flags=0;if(!w||!w->has_solids||!x||!y)return 0;
    double target_x=*x,target_y=*y,resolved_x=target_x,resolved_y=target_y;
    double px=old_x,py=old_y,dx=target_x-old_x,dy=target_y-old_y;
    for(int iteration=0;iteration<3;iteration++){
        double earliest=2;unsigned hit=0;
        for(uint32_t slot=0;slot<w->capacity;slot++)if(w->slots[slot].active){
            const EntityValue* value=&w->slots[slot].value;const EntityType* type=find_type(w,value->type_id);
            if(!type)continue;
            for(uint32_t j=0;j<type->region_count;j++){
                const EntityRegion* r=&type->regions[j];if(r->role!=ENTITY_REGION_SOLID)continue;
                double left=((double)value->x+entity_region_local_x(value,r))/256.0-radius,right=left+r->width/256.0+radius*2;
                double top=((double)value->y+r->y)/256.0-radius,bottom=top+r->height/256.0+radius*2;
                double enter_x=-1e300,exit_x=1e300,enter_y=-1e300,exit_y=1e300;
                if(dx==0){if(px<=left||px>=right)continue;}else if(dx>0){enter_x=(left-px)/dx;exit_x=(right-px)/dx;}else{enter_x=(right-px)/dx;exit_x=(left-px)/dx;}
                if(dy==0){if(py<=top||py>=bottom)continue;}else if(dy>0){enter_y=(top-py)/dy;exit_y=(bottom-py)/dy;}else{enter_y=(bottom-py)/dy;exit_y=(top-py)/dy;}
                double enter=enter_x>enter_y?enter_x:enter_y,leave=exit_x<exit_y?exit_x:exit_y;
                if(enter<0||enter>1||enter>leave||enter>=earliest)continue;
                earliest=enter;hit=enter_x>enter_y?(dx>0?4u:8u):(dy>0?1u:2u);
            }
        }
        if(!hit){px+=dx;py+=dy;break;}
        px+=dx*earliest;py+=dy*earliest;dx*=1-earliest;dy*=1-earliest;
        if(hit&12u)dx=0;else dy=0;flags|=hit;
    }
    resolved_x=px;resolved_y=py;
    /* Resolve initial penetration (for example a spawned body) to the nearest
     * face. Stable slot/region order makes equal-distance ties deterministic. */
    for(uint32_t slot=0;slot<w->capacity;slot++)if(w->slots[slot].active){
        const EntityValue* value=&w->slots[slot].value;const EntityType* type=find_type(w,value->type_id);
        if(!type)continue;
        for(uint32_t j=0;j<type->region_count;j++){
            const EntityRegion* r=&type->regions[j];if(r->role!=ENTITY_REGION_SOLID)continue;
            double left=((double)value->x+entity_region_local_x(value,r))/256.0-radius,right=left+r->width/256.0+radius*2;
            double top=((double)value->y+r->y)/256.0-radius,bottom=top+r->height/256.0+radius*2;
            if(resolved_x>left&&resolved_x<right&&resolved_y==top&&target_y>=old_y)flags|=1;
            if(resolved_x>left&&resolved_x<right&&resolved_y>top&&resolved_y<bottom){
                double distance=resolved_y-top;unsigned face=1;
                if(bottom-resolved_y<distance){distance=bottom-resolved_y;face=2;}
                if(resolved_x-left<distance){distance=resolved_x-left;face=4;}
                if(right-resolved_x<distance)face=8;
                if(face==1)resolved_y=top;else if(face==2)resolved_y=bottom;else if(face==4)resolved_x=left;else resolved_x=right;
                flags|=face;
            }
        }
    }
    *x=resolved_x;*y=resolved_y;return flags;
}
