#include "../preview_package.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
    unsigned char bytes[512],bad[513];char error[128];PreviewPackage parsed,before;
    FILE* file=fopen("tests/fixtures/preview-package.bin","rb");assert(file);size_t size=fread(bytes,1,sizeof(bytes),file);assert(feof(file)&&!ferror(file));fclose(file);
    assert(preview_package_decode(bytes,size,&parsed,error,sizeof(error)));
    assert(parsed.count==3&&!strcmp(parsed.files[0].name,"data.json")&&!strcmp(parsed.files[1].name,"data.map"));
    assert(parsed.files[0].size==2&&!memcmp(parsed.files[0].bytes,"{}",2));
    before=parsed;
    for(size_t n=0;n<size;n++){assert(!preview_package_decode(bytes,n,&parsed,error,sizeof(error)));assert(!memcmp(&parsed,&before,sizeof(parsed)));}
    memcpy(bad,bytes,size);bad[size]=0;assert(!preview_package_decode(bad,size+1,&parsed,error,sizeof(error)));
    memcpy(bad,bytes,size);bad[4]=65;assert(!preview_package_decode(bad,size,&parsed,error,sizeof(error)));
    memcpy(bad,bytes,size);bad[10]=1;assert(!preview_package_decode(bad,size,&parsed,error,sizeof(error)));
    memcpy(bad,bytes,size);bad[16]='/';assert(!preview_package_decode(bad,size,&parsed,error,sizeof(error)));
    memcpy(bad,bytes,size);bad[(size_t)(before.files[1].bytes-bytes)]=0;assert(!preview_package_decode(bad,size,&parsed,error,sizeof(error)));
    assert(!memcmp(&parsed,&before,sizeof(parsed)));
    const char* invalid[]={"../a.png","CON.png","com1.extra.png","a/b.png","map.LUA","objects.greggnogg.json",".png","x.png:stream","a\\b.png"};
    for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++)assert(!preview_package_filename_valid(invalid[i]));
    assert(preview_package_filename_valid("_sprite.PNG"));
    puts("preview package: shared browser fixture, bounded framing, filenames and atomic decode passed");return 0;
}
