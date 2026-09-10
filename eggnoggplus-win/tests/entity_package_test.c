#include "../entity_package.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static void json_tests(void) {
    const char* valid="{\"schema\":1,\"capacity\":8,\"types\":[{\"key\":\"demo:orb\",\"regions\":[{\"id\":1,\"role\":\"sensor\",\"layer\":1,\"mask\":4294967295,\"width\":2,\"height\":3}]}],\"placements\":[{\"name\":\"start\",\"type\":\"demo:orb\",\"x\":-1.25}]}";
    const char* invalid[]={
        "[]", "null", "{}",
        "{\"schema\":1,\"schema\":1}",
        "{\"schema\":1,\"capacity\":8,\"types\":{},\"placements\":[]}",
        "{\"schema\":1,\"capacity\":8,\"types\":[{\"key\":\"demo:orb\",\"regions\":[],\"typo\":true}],\"placements\":[]}",
        "{\"schema\":1,\"capacity\":8,\"types\":[{\"key\":\"demo:orb\",\"regions\":[]}],\"placements\":[{\"name\":\"start\",\"type\":\"demo:unknown\"}]}",
        "{\"schema\":1,\"capacity\":8,\"types\":[{\"key\":\"demo:orb\\u0000\",\"regions\":[]}],\"placements\":[]}",
        "{\"schema\":1,\"capacity\":1.5,\"types\":[],\"placements\":[]}",
        "{\"schema\":1,\"capacity\":8,\"types\":[{\"key\":\"demo:orb\",\"regions\":[]}],\"placements\":[{\"name\":\"start\",\"type\":\"demo:orb\",\"x\":null}]}"
    };
    EntityNamedType type={0};EntityPlacement placement={0};
    EntityPackage *a,*b;EntityValue v;char error[256];size_t i;
    strcpy(type.key,"demo:orb");type.definition.region_count=1;
    type.definition.regions[0]=(EntityRegion){1,ENTITY_REGION_SENSOR,1,UINT32_MAX,0,0,512,768};
    strcpy(placement.name,"start");strcpy(placement.type,"demo:orb");placement.value.x=-320;
    a=entity_package_decode(valid,strlen(valid),error,sizeof(error));assert(a && !error[0]);
    b=entity_package_create(&type,1,&placement,1,8,error,sizeof(error));assert(b);
    assert(!strcmp(entity_package_fingerprint(a),entity_package_fingerprint(b)));
    assert(entity_world_read(entity_package_world(a),entity_package_placement(a,"start"),&v) && v.x==-320);
    for(i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        assert(!entity_package_decode(invalid[i],strlen(invalid[i]),error,sizeof(error)));
        assert(strchr(error,'$'));
    }
    for(i=0;i<strlen(valid);i++) assert(!entity_package_decode(valid,i,error,sizeof(error)));
    assert(!entity_package_decode(NULL,0,error,sizeof(error)));
    assert(!entity_package_decode(valid,1048577,error,sizeof(error)));
    entity_package_free(a);entity_package_free(b);
}
static void visual_tests(void) {
    char error[256],json[1024];EntityVisual v;EntityPackage *a,*b;EntityNamedType type={0};
    const char* format="{\"schema\":1,\"capacity\":8,\"types\":[{\"key\":\"demo:orb\",\"regions\":[],\"visual\":%s}],\"placements\":[]}";
    const char* good="{\"sheet\":\"builtin:tiles\",\"sprite\":4,\"frames\":3,\"frame_ticks\":2,\"offset_x\":-1.25,\"scale_y\":-2,\"tint\":\"#12Ab34ff\"}";
    const char* invalid[]={"{}","[]","null","{\"sheet\":\"../x.png\",\"sprite\":0}","{\"sheet\":\"builtin:\",\"sprite\":0}","{\"sheet\":\"x.png\",\"sprite\":0,\"frames\":0}","{\"sheet\":\"x.png\",\"sprite\":2147483647,\"frames\":2}","{\"sheet\":\"x.png\",\"sprite\":0,\"scale_x\":0.0001}","{\"sheet\":\"x.png\",\"sprite\":0,\"tint\":\"#ggffffff\"}","{\"sheet\":\"x.png\",\"sprite\":0,\"layer\":2}"};
    snprintf(json,sizeof(json),format,good);a=entity_package_decode(json,strlen(json),error,sizeof(error));assert(a);
    assert(entity_package_visual(a,1,&v));assert(!strcmp(v.sheet,"builtin:tiles") && v.offset_x==-320 && v.scale_x==256 && v.scale_y==-512 && v.rgba==0x12ab34ff && v.layer==1);
    assert(entity_visual_frame(&v,0)==4 && entity_visual_frame(&v,2)==5 && entity_visual_frame(&v,6)==4);
    assert(entity_visual_frame(&v,UINT64_MAX)==4+(UINT64_MAX/2)%3);
    strcpy(type.key,"demo:orb");type.visual=v;
    b=entity_package_create(&type,1,NULL,0,8,error,sizeof(error));assert(b);
    assert(!strcmp(entity_package_fingerprint(a),entity_package_fingerprint(b)));entity_package_free(b);
    type.visual.sprite++;
    b=entity_package_create(&type,1,NULL,0,8,error,sizeof(error));assert(b);
    assert(strcmp(entity_package_fingerprint(a),entity_package_fingerprint(b)));entity_package_free(b);
    assert(entity_package_visual(a,1,&v) && v.sprite==4); /* Detached native copy. */
    for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        snprintf(json,sizeof(json),format,invalid[i]);b=entity_package_decode(json,strlen(json),error,sizeof(error));assert(!b && error[0]);
    }
    {
        EntityValue value={0};EntityRenderView view;uint32_t cursor=0;
        size_t size=entity_package_snapshot_size(a);unsigned char* before=malloc(size);unsigned char* after=malloc(size);
        assert(before && after);value.type_id=1;value.x=384;value.vx=256;
        EntityHandle h=entity_world_spawn(entity_package_world(a),&value);assert(h);
        assert(entity_package_save(a,before,size));
        assert(entity_package_render_next(a,&cursor,&view) && view.handle==h && view.x==384 && view.sprite==4);
        assert(!entity_package_render_next(a,&cursor,&view));
        assert(entity_package_save(a,after,size) && !memcmp(before,after,size));
        assert(entity_world_step(entity_package_world(a)) && entity_world_step(entity_package_world(a)));
        cursor=0;assert(entity_package_render_next(a,&cursor,&view) && view.x==896 && view.sprite==5);
        assert(entity_package_load(a,before,size));cursor=0;
        assert(entity_package_render_next(a,&cursor,&view) && view.x==384 && view.sprite==4);
        assert(entity_world_read(entity_package_world(a),h,&value));value.animation_tick=4;value.flags|=ENTITY_FLAG_ANIMATION_PAUSED;
        assert(entity_world_write(entity_package_world(a),h,&value));
        assert(entity_world_step(entity_package_world(a)));cursor=0;
        assert(entity_package_render_next(a,&cursor,&view) && view.sprite==6 && view.x==640);
        assert(entity_package_save(a,before,size));
        value.animation_tick=0;value.flags&=~ENTITY_FLAG_ANIMATION_PAUSED;assert(entity_world_write(entity_package_world(a),h,&value));
        cursor=0;assert(entity_package_render_next(a,&cursor,&view) && view.sprite==4);
        assert(entity_package_load(a,before,size));cursor=0;
        assert(entity_package_render_next(a,&cursor,&view) && view.sprite==6);
        free(before);free(after);
    }
    entity_package_free(a);
}
static void editor_export_test(void) {
    char json[8192],error[256];EntityPackage* package;EntityVisual visual;EntityValue value;
    FILE* file=fopen("tests/fixtures/workshop-entities.json","rb");assert(file);
    size_t length=fread(json,1,sizeof(json),file);assert(length>0 && length<sizeof(json) && !ferror(file));fclose(file);
    package=entity_package_decode(json,length,error,sizeof(error));assert(package);
    assert(entity_package_type_count(package)==1 && !strcmp(entity_package_type_key(package,1),"demo:orb"));
    assert(entity_package_visual(package,1,&visual) && visual.frames==3 && visual.frame_ticks==6);
    assert(visual.offset_y==-320 && visual.scale_x==-128 && visual.rgba==0x80c0ffff);
    assert(entity_world_read(entity_package_world(package),entity_package_placement(package,"first_orb"),&value));
    assert(value.x==48*256 && value.y==64*256 && value.vx==128);
    entity_package_free(package);
}
static void room_placement_tests(void) {
    const char* format="{\"schema\":2,\"capacity\":%d,\"types\":[{\"key\":\"demo:orb\",\"regions\":[{\"id\":1,\"role\":\"sensor\",\"layer\":1,\"mask\":1,\"x\":2,\"width\":4,\"height\":6}],\"visual\":{\"sheet\":\"builtin:tiles\",\"sprite\":4,\"offset_x\":3}}],\"placements\":[{\"name\":\"pad\",\"type\":\"demo:orb\",\"room\":\"%s\",\"side\":\"%s\",\"x\":8,\"y\":12,\"vx\":2}]}";
    EntityPackageLayout layout={3,{"center","side","end"}};char json[2048],error[256];EntityValue value;EntityRenderView view;uint32_t cursor=0;
    snprintf(json,sizeof(json),format,8,"side","both");
    assert(!entity_package_decode(json,strlen(json),error,sizeof(error)) && strstr(error,"map layout"));
    EntityPackage* p=entity_package_decode_layout(json,strlen(json),&layout,error,sizeof(error));assert(p);
    EntityWorld* w=entity_package_world(p);assert(entity_world_count(w)==2);
    assert(entity_world_read(w,entity_package_placement(p,"pad"),&value) && value.x==(528+8)*256 && value.vx==512 && !value.flags);
    assert(entity_world_read(w,entity_package_placement(p,"pad.mirror"),&value) && value.x==(4*528-8)*256 && value.vx==-512 && value.flags==ENTITY_FLAG_MIRROR_X);
    assert(entity_package_render_next(p,&cursor,&view) && view.visual.offset_x==768 && view.visual.scale_x==256);
    assert(entity_package_render_next(p,&cursor,&view) && view.visual.offset_x==-768 && view.visual.scale_x==-256);
    EntityType type;assert(entity_world_read_type(w,1,&type));assert(entity_region_local_x(&value,&type.regions[0])==-6*256);
    EntityValue peer=value;peer.flags=0;peer.x-=8*256;peer.vx=0;EntityHandle h=entity_world_spawn(w,&peer);assert(h);
    assert(entity_world_contacts(w,NULL,0)==1);peer.x=value.x;assert(entity_world_write(w,h,&peer));assert(entity_world_contacts(w,NULL,0)==0);
    entity_package_free(p);
    const char* rooms[]={"center","center","missing","side","side"};const char* sides[]={"both","mirrored","both","bad","both"};
    for(int i=0;i<5;i++) {
        snprintf(json,sizeof(json),format,i==4?1:8,rooms[i],sides[i]);p=entity_package_decode_layout(json,strlen(json),&layout,error,sizeof(error));
        assert((p!=NULL)==(i==0));if(p){assert(entity_world_count(entity_package_world(p))==1);entity_package_free(p);}
    }
}
int main(void) {
    json_tests();visual_tests();editor_export_test();room_placement_tests();
    EntityNamedType types[2]={0},reversed[2];EntityPlacement placements[2]={0},reverse_places[2];
    EntityPackage *a,*b,*changed;char error[256];EntityValue value;size_t size;unsigned char *before,*after;
    strcpy(types[0].key,"demo:orb");strcpy(types[1].key,"demo:crate");
    types[0].definition.region_count=1;
    types[0].definition.regions[0]=(EntityRegion){1,ENTITY_REGION_HITBOX,1,1,0,0,256,256};
    strcpy(placements[0].name,"right");strcpy(placements[0].type,"demo:orb");placements[0].value.x=512;
    strcpy(placements[1].name,"left");strcpy(placements[1].type,"demo:crate");placements[1].value.x=-256;
    a=entity_package_create(types,2,placements,2,8,error,sizeof(error));assert(a && !error[0]);
    reversed[0]=types[1];reversed[1]=types[0];reverse_places[0]=placements[1];reverse_places[1]=placements[0];
    b=entity_package_create(reversed,2,reverse_places,2,8,error,sizeof(error));assert(b);
    assert(!strcmp(entity_package_fingerprint(a),entity_package_fingerprint(b)));
    assert(entity_package_resolve_type(a,"demo:crate",10)==1);
    assert(entity_world_read(entity_package_world(a),entity_package_placement(a,"left"),&value) && value.x==-256);
    size=entity_package_snapshot_size(a);before=malloc(size);after=malloc(size);assert(before && after);
    assert(entity_package_save(a,before,size));assert(entity_package_load(b,before,size));
    assert(entity_package_save(b,after,size) && !memcmp(before,after,size));
    strcpy(types[1].key,"demo:box");strcpy(placements[1].type,"demo:box");
    changed=entity_package_create(types,2,placements,2,8,error,sizeof(error));assert(changed);
    assert(strcmp(entity_package_fingerprint(a),entity_package_fingerprint(changed)));
    assert(!entity_package_load(changed,before,size));entity_package_free(changed);
    placements[0].value.x++;
    changed=entity_package_create(types,2,placements,2,8,error,sizeof(error));assert(changed);
    assert(strcmp(entity_package_fingerprint(a),entity_package_fingerprint(changed)));entity_package_free(changed);
    strcpy(placements[0].type,"demo:unknown");assert(!entity_package_create(types,2,placements,2,8,error,sizeof(error)) && strstr(error,"unknown"));
    strcpy(types[1].key,types[0].key);assert(!entity_package_create(types,2,NULL,0,8,error,sizeof(error)) && strstr(error,"duplicate"));
    memset(types[0].key,'a',sizeof(types[0].key));types[0].key[96]=':';
    assert(!entity_package_create(types,1,NULL,0,8,error,sizeof(error)));
    assert(entity_package_save(a,after,size) && !memcmp(before,after,size));
    free(before);free(after);entity_package_free(a);entity_package_free(b);
    puts("entity packages: canonical ownership, placement, identities and atomic restore passed");return 0;
}
