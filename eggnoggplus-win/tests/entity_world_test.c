#include "../entity_world.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct UpdateFixture { EntityHandle first, second, replacement; int calls, mode; } UpdateFixture;
static int update_fixture(EntityWorld* w,EntityHandle h,void* user) {
    UpdateFixture* f=user;
    EntityValue v={1,0,0,2,0,0,0};
    f->calls++;
    if(h==f->first) {
        if(f->mode==2) { (void)entity_world_step(w);return 1; }
        assert(entity_world_remove(w,f->second));
        f->replacement=entity_world_spawn(w,&v);assert(f->replacement && f->replacement!=f->second);
        assert(entity_world_read(w,h,&v));v.vx=7;assert(entity_world_write(w,h,&v));
        if(f->mode==1) return 0;
    }
    return 1;
}
static void test_update(void) {
    EntityWorld* w=entity_world_create(3);
    EntityValue v={1,0,0,1,0,0,0},read;
    UpdateFixture f={0};
    unsigned char *before,*after;size_t size;
    f.first=entity_world_spawn(w,&v);f.second=entity_world_spawn(w,&v);
    size=entity_world_snapshot_size(w);before=malloc(size);after=malloc(size);assert(before && after);
    assert(entity_world_save(w,before,size));
    f.mode=1;assert(!entity_world_update(w,update_fixture,&f));
    assert(entity_world_save(w,after,size) && !memcmp(before,after,size));
    f.mode=2;assert(!entity_world_update(w,update_fixture,&f));
    assert(entity_world_save(w,after,size) && !memcmp(before,after,size));
    f.mode=0;f.calls=0;assert(entity_world_update(w,update_fixture,&f));
    assert(f.calls==1 && entity_world_tick(w)==1);
    assert(!entity_world_read(w,f.second,&read));
    assert(entity_world_read(w,f.replacement,&read) && read.x==2);
    assert(entity_world_read(w,f.first,&read) && read.x==7);
    free(before);free(after);entity_world_free(w);
}
static void test_regions(void) {
    EntityWorld* a=entity_world_create(4), *b=entity_world_create(4), *different=entity_world_create(4);
    EntityType types[2]={0};
    EntityValue v={1,0,0,0,0,0,0}; EntityHandle first,second;
    EntityContact contacts[2], sentinel[2];
    unsigned char *snapshot,*after; size_t size;
    types[0].id=1;types[0].region_count=2;
    types[0].regions[0]=(EntityRegion){1,ENTITY_REGION_BODY,1,2,0,0,256,256};
    types[0].regions[1]=(EntityRegion){2,ENTITY_REGION_HITBOX,4,8,-128,0,512,256};
    types[1].id=2;types[1].region_count=2;
    types[1].regions[0]=(EntityRegion){1,ENTITY_REGION_BODY,2,1,0,0,256,256};
    types[1].regions[1]=(EntityRegion){2,ENTITY_REGION_HURTBOX,8,4,0,0,256,256};
    types[0].regions[0].width=0;assert(!entity_world_define_types(a,types,2));
    types[0].regions[0].width=256;
    types[1].id=1;assert(!entity_world_define_types(a,types,2));types[1].id=2;
    types[0].regions[1].id=1;assert(!entity_world_define_types(a,types,2));types[0].regions[1].id=2;
    assert(entity_world_define_types(a,types,2));
    { EntityType reversed[2]={types[1],types[0]};
      assert(entity_world_define_types(b,reversed,2)); }

    types[1].regions[0].width=512;assert(entity_world_define_types(different,types,2));
    assert(!entity_world_define_types(a,types,2));
    first=entity_world_spawn(a,&v);v.type_id=2;v.x=128;second=entity_world_spawn(a,&v);
    assert(first && second && entity_world_contacts(a,NULL,0)==2);
    memset(contacts,0x5a,sizeof(contacts));memcpy(sentinel,contacts,sizeof(contacts));
    assert(entity_world_contacts(a,contacts,1)==2 && !memcmp(contacts,sentinel,sizeof(contacts)));
    assert(entity_world_contacts(a,contacts,2)==2 && contacts[0].a==first && contacts[0].b==second);
    assert(contacts[0].role_a==ENTITY_REGION_BODY && contacts[1].role_a==ENTITY_REGION_HITBOX);
    size=entity_world_snapshot_size(a); snapshot=malloc(size);after=malloc(size);assert(snapshot && after);
    assert(entity_world_save(a,snapshot,size));
    assert(!entity_world_load(different,snapshot,size));
    assert(entity_world_load(b,snapshot,size));
    assert(entity_world_save(b,after,size) && !memcmp(snapshot,after,size));
    v.x=256;assert(entity_world_write(a,second,&v));
    assert(entity_world_contacts(a,contacts,2)==1 && contacts[0].role_a==ENTITY_REGION_HITBOX);
    v.x=384;assert(entity_world_write(a,second,&v));assert(entity_world_contacts(a,NULL,0)==0);
    v.type_id=99;assert(!entity_world_spawn(a,&v));
    free(snapshot);free(after);entity_world_free(a);entity_world_free(b);entity_world_free(different);
}
static void test_animation(void) {
    EntityWorld* w=entity_world_create(2);EntityValue v={1,0,0,1,0,0,0},read;
    EntityHandle first=entity_world_spawn(w,&v);assert(entity_world_step(w));
    EntityHandle second=entity_world_spawn(w,&v);
    assert(entity_world_read(w,first,&read) && read.animation_tick==1);
    assert(entity_world_read(w,second,&read) && read.animation_tick==0);
    read.flags|=ENTITY_FLAG_ANIMATION_PAUSED;assert(entity_world_write(w,second,&read));
    size_t size=entity_world_snapshot_size(w);unsigned char *before=malloc(size),*after=malloc(size);
    assert(entity_world_save(w,before,size));assert(entity_world_step(w));
    assert(entity_world_read(w,second,&read) && read.animation_tick==0 && read.x==1);
    assert(entity_world_load(w,before,size));assert(entity_world_step(w));assert(entity_world_save(w,after,size));
    assert(entity_world_load(w,before,size));assert(entity_world_step(w));assert(entity_world_save(w,before,size));assert(!memcmp(before,after,size));
    assert(entity_world_read(w,first,&read));read.animation_tick=ENTITY_ANIMATION_TICK_MAX;
    assert(entity_world_write(w,first,&read));assert(entity_world_save(w,before,size));
    assert(!entity_world_step(w));assert(entity_world_save(w,after,size));assert(!memcmp(before,after,size));
    read.animation_tick++;assert(!entity_world_write(w,first,&read));
    before[4]=1;assert(!entity_world_load(w,before,size));
    free(before);free(after);entity_world_free(w);
}
int main(void) {
    test_animation();
    EntityWorld* a=entity_world_create(4), *b=entity_world_create(4);
    test_regions();
    test_update();
    EntityValue value={1,256,512,3,-2,0,0}, read={0};
    EntityHandle h, reused, other;
    size_t size=entity_world_snapshot_size(a);
    unsigned char *baseline=malloc(size), *future=malloc(size), *replay=malloc(size), *bad=malloc(size);
    uint32_t cursor=0;
    assert(a && b && baseline && future && replay && bad);
    assert(!entity_world_create(0) && !entity_world_create(ENTITY_WORLD_LIMIT+1));
    h=entity_world_spawn(a,&value); other=entity_world_spawn(a,&value);
    assert(h && other && h!=other && entity_world_count(a)==2);
    assert(entity_world_next(a,&cursor)==h && entity_world_next(a,&cursor)==other);
    assert(!entity_world_next(a,&cursor));
    assert(entity_world_remove(a,h) && !entity_world_remove(a,h));
    reused=entity_world_spawn(a,&value);
    assert(reused!=h && (uint32_t)reused==(uint32_t)h);
    assert(!entity_world_read(a,h,&read) && !entity_world_write(a,h,&value));
    assert(entity_world_read(a,reused,&read) && read.x==256);
    assert(entity_world_save(a,baseline,size));
    for(int i=0;i<2048;++i) assert(entity_world_step(a));
    assert(entity_world_save(a,future,size));
    assert(entity_world_load(b,baseline,size));
    for(int i=0;i<2048;++i) assert(entity_world_step(b));
    assert(entity_world_save(b,replay,size) && memcmp(future,replay,size)==0);
    assert(entity_world_tick(b)==2048);
    /* Every truncated buffer is rejected without changing the destination. */
    for(size_t i=0;i<size;++i) assert(!entity_world_load(b,baseline,i));
    assert(entity_world_save(b,replay,size) && memcmp(future,replay,size)==0);
    memcpy(bad,baseline,size); bad[24]=1; assert(!entity_world_load(b,bad,size));
    memcpy(bad,baseline,size); bad[36]=2; assert(!entity_world_load(b,bad,size));
    memcpy(bad,baseline,size); bad[12]=4; assert(!entity_world_load(b,bad,size));
    memcpy(bad,baseline,size); memset(bad+32,0,4); assert(!entity_world_load(b,bad,size));
    assert(entity_world_save(b,replay,size) && memcmp(future,replay,size)==0);
    /* Failure in a later entity must not integrate an earlier entity. */
    value.x=ENTITY_WORLD_COORD_LIMIT; value.vx=1;
    assert(entity_world_write(a,other,&value));
    assert(entity_world_save(a,baseline,size));
    assert(!entity_world_step(a));
    assert(entity_world_save(a,replay,size) && memcmp(baseline,replay,size)==0);
    value.x=INT32_MAX; assert(!entity_world_write(a,other,&value));
    value.x=0; value.type_id=2; assert(!entity_world_write(a,other,&value));
    assert(entity_world_save(a,replay,size) && memcmp(baseline,replay,size)==0);
    /* Serialized exhausted generations retire slots; never wrap a handle. */
    assert(entity_world_remove(a,reused));
    assert(entity_world_save(a,baseline,size));
    memset(baseline+32,255,4);
    assert(entity_world_load(a,baseline,size));
    value.type_id=1; value.vx=0;
    h=entity_world_spawn(a,&value); assert(h && (uint32_t)h==3);
    assert(entity_world_spawn(a,&value)); assert(!entity_world_spawn(a,&value));
    free(baseline);free(future);free(replay);free(bad);entity_world_free(a);entity_world_free(b);
    puts("entity world: lifecycle, overflow atomicity, 2048-tick replay and strict snapshots passed");
    return 0;
}
