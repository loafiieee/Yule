#include "preview_package.h"
#include <string.h>
#include <stdio.h>
static uint32_t u32(const unsigned char* p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static unsigned char lower(unsigned char c){return c>='A'&&c<='Z'?c+32:c;}
static int same(const char* a,const char* b){while(*a&&*b){if(lower((unsigned char)*a++)!=lower((unsigned char)*b++))return 0;}return *a==*b;}
int preview_package_filename_valid(const char* name){
    size_t n;char stem[PREVIEW_PACKAGE_NAME_BYTES];size_t i=0;
    if(!name||!(n=strlen(name))||n>=PREVIEW_PACKAGE_NAME_BYTES)return 0;
    if(!strcmp(name,"data.json")||!strcmp(name,"data.map")||!strcmp(name,"map.lua")||!strcmp(name,"entities.json"))return 1;
    if(n<5||!same(name+n-4,".png")||name[0]=='.')return 0;
    for(size_t j=0;j<n;j++){unsigned char c=(unsigned char)name[j];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))return 0;}
    while(i<n&&name[i]!='.'){stem[i]=(char)lower((unsigned char)name[i]);i++;}stem[i]=0;
    if(!strcmp(stem,"con")||!strcmp(stem,"prn")||!strcmp(stem,"aux")||!strcmp(stem,"nul"))return 0;
    if(i==4&&(!memcmp(stem,"com",3)||!memcmp(stem,"lpt",3))&&stem[3]>='1'&&stem[3]<='9')return 0;
    return 1;
}
static int fail(char* error,size_t capacity,const char* message){if(error&&capacity)snprintf(error,capacity,"%s",message);return 0;}
int preview_package_decode(const void* data,size_t size,PreviewPackage* out,char* error,size_t capacity){
    const unsigned char* bytes=data;PreviewPackage candidate;size_t offset=8;int json=0,map=0;
    if(!bytes||!out||size<8||size>PREVIEW_PACKAGE_MAX_BYTES||memcmp(bytes,"GGP2",4))return fail(error,capacity,"Invalid preview package header or size.");
    memset(&candidate,0,sizeof(candidate));candidate.count=u32(bytes+4);
    if(!candidate.count||candidate.count>PREVIEW_PACKAGE_MAX_FILES)return fail(error,capacity,"Invalid preview file count.");
    for(uint32_t i=0;i<candidate.count;i++){
        PreviewFile* file=&candidate.files[i];uint32_t name_size,limit;
        if(size-offset<8)return fail(error,capacity,"Truncated preview entry.");
        name_size=(uint32_t)bytes[offset]|((uint32_t)bytes[offset+1]<<8);file->size=u32(bytes+offset+4);
        if(bytes[offset+2]||bytes[offset+3]||!name_size||name_size>=PREVIEW_PACKAGE_NAME_BYTES)return fail(error,capacity,"Invalid preview filename header.");
        offset+=8;
        if(name_size>size-offset)return fail(error,capacity,"Truncated preview filename.");
        memcpy(file->name,bytes+offset,name_size);offset+=name_size;
        if(memchr(file->name,0,name_size)||!preview_package_filename_valid(file->name))return fail(error,capacity,"Unsafe or unsupported preview filename.");
        for(uint32_t j=0;j<i;j++)if(same(file->name,candidate.files[j].name))return fail(error,capacity,"Duplicate preview filename.");
        limit=64u*1024u*1024u;
        if(!strcmp(file->name,"data.json")){limit=4u*1024u*1024u;json=1;}
        else if(!strcmp(file->name,"data.map")){limit=4u*1024u*1024u;map=1;}
        else if(!strcmp(file->name,"map.lua"))limit=262144u;
        else if(!strcmp(file->name,"entities.json"))limit=1048576u;
        if(file->size>limit||file->size>size-offset)return fail(error,capacity,"Preview file exceeds its limit or is truncated.");
        file->bytes=bytes+offset;offset+=file->size;
        if(limit!=64u*1024u*1024u&&memchr(file->bytes,0,file->size))return fail(error,capacity,"NUL byte in preview text.");
        if((!strcmp(file->name,"data.json")||!strcmp(file->name,"data.map"))&&!file->size)return fail(error,capacity,"Empty required preview file.");
    }
    if(offset!=size||!json||!map)return fail(error,capacity,"Preview needs data.json and data.map with no trailing bytes.");
    *out=candidate;if(error&&capacity)error[0]=0;return 1;
}
