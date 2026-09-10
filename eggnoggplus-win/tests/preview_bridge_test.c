#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include "../preview_bridge.h"
#include "../preview_stage.h"
#include "../preview_package.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static void request(const char* method,const char* token,const void* bytes,size_t size,int expected,const char* body){
    SOCKET socket=WSASocketA(AF_INET,SOCK_STREAM,IPPROTO_TCP,NULL,0,0);struct sockaddr_in address;char header[512],response[2048];size_t used=0;DWORD timeout=2000;
    assert(socket!=INVALID_SOCKET);memset(&address,0,sizeof(address));address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons((u_short)preview_bridge_port());
    setsockopt(socket,SOL_SOCKET,SO_RCVTIMEO,(const char*)&timeout,sizeof(timeout));assert(!connect(socket,(struct sockaddr*)&address,sizeof(address)));
    int n=snprintf(header,sizeof(header),"%s /preview/%s HTTP/1.1\r\nHost: 127.0.0.1:%u\r\nContent-Length: %u\r\n%s\r\n",method,token,preview_bridge_port(),(unsigned)size,size?"Content-Type: application/octet-stream\r\n":"");
    assert(send(socket,header,n,0)==n);if(size)assert(send(socket,bytes,(int)size,0)==(int)size);
    while(used+1<sizeof(response)){n=recv(socket,response+used,(int)(sizeof(response)-used-1),0);if(n<=0)break;used+=(size_t)n;}response[used]=0;closesocket(socket);
    char status[32];snprintf(status,sizeof(status),"HTTP/1.1 %d ",expected);if(strncmp(response,status,strlen(status))) fprintf(stderr,"%s expected %d, received [%s]\n",method,expected,response);assert(!strncmp(response,status,strlen(status)));if(body)assert(strstr(response,body));
}
static int keep_preview(const char* token){(void)token;return 1;}
static int retire_preview(const char* token){(void)token;return 0;}
static void staging(const char* token,const unsigned char* packet,size_t length){
    char temporary[MAX_PATH],root[MAX_PATH],cache[MAX_PATH],folder[MAX_PATH],path[MAX_PATH],error[128];
    assert(GetTempPathA(sizeof(temporary),temporary));
    assert(GetTempFileNameA(temporary,"ggp",0,root));assert(DeleteFileA(root));
    assert(preview_stage_package(root,token,packet,length,error,sizeof(error)));
    assert(!preview_stage_package(root,token,packet,length,error,sizeof(error)));
    PreviewPackage decoded;assert(preview_package_decode(packet,length,&decoded,error,sizeof(error)));
    assert(snprintf(cache,sizeof(cache),"%s\\_greggnogg_previews",root)<(int)sizeof(cache));
    assert(snprintf(folder,sizeof(folder),"%s\\%s",cache,token)<(int)sizeof(folder));
    for(unsigned i=0;i<decoded.count;i++){
        unsigned char data[512];
        assert(snprintf(path,sizeof(path),"%s\\%s",folder,decoded.files[i].name)<(int)sizeof(path));
        FILE* file=fopen(path,"rb");assert(file);size_t n=fread(data,1,sizeof(data),file);
        assert(!ferror(file)&&n==decoded.files[i].size&&!memcmp(data,decoded.files[i].bytes,n));fclose(file);
    }
    preview_stage_collect(keep_preview);
    assert(GetFileAttributesA(path)!=INVALID_FILE_ATTRIBUTES);
    char unrelated[MAX_PATH];assert(snprintf(unrelated,sizeof(unrelated),"%s\\notes.txt",folder)<(int)sizeof(unrelated));
    FILE* note=fopen(unrelated,"wb");assert(note);fclose(note);
    char changed[MAX_PATH];assert(snprintf(changed,sizeof(changed),"%s\\data.json",folder)<(int)sizeof(changed));
    FILE* edited=fopen(changed,"ab");assert(edited);assert(fputs("user edit",edited)>=0);fclose(edited);
    preview_stage_collect(retire_preview);
    assert(GetFileAttributesA(changed)!=INVALID_FILE_ATTRIBUTES);
    assert(DeleteFileA(changed));
    assert(GetFileAttributesA(path)==INVALID_FILE_ATTRIBUTES);
    assert(GetFileAttributesA(unrelated)!=INVALID_FILE_ATTRIBUTES);
    assert(DeleteFileA(unrelated));
    assert(RemoveDirectoryA(folder));assert(RemoveDirectoryA(cache));assert(RemoveDirectoryA(root));
    assert(!preview_stage_package(root,"../escape",packet,length,error,sizeof(error)));
    assert(GetFileAttributesA(root)==INVALID_FILE_ATTRIBUTES);
    assert(!preview_stage_package(root,token,packet,length-1,error,sizeof(error)));
    assert(GetFileAttributesA(root)==INVALID_FILE_ATTRIBUTES);
    assert(CreateDirectoryA(root,NULL));
    FILE* blocker=fopen(cache,"wb");assert(blocker);assert(fputs("preserve",blocker)>=0);fclose(blocker);
    assert(!preview_stage_package(root,token,packet,length,error,sizeof(error)));
    blocker=fopen(cache,"rb");assert(blocker);char kept[9]={0};assert(fread(kept,1,8,blocker)==8);fclose(blocker);
    assert(!strcmp(kept,"preserve"));assert(DeleteFileA(cache));assert(RemoveDirectoryA(root));
}
int main(void){
    const char* token="0123456789abcdef0123456789abcdef";char error[128],received[33];unsigned char packet[512],*bytes=NULL;size_t size=0;
    FILE* file=fopen("tests/fixtures/preview-package.bin","rb");assert(file);size_t length=fread(packet,1,sizeof(packet),file);assert(feof(file)&&!ferror(file));fclose(file);
    staging(token,packet,length);
    assert(preview_bridge_begin(token,error,sizeof(error)));assert(preview_bridge_port()!=0);
    request("GET","1123456789abcdef0123456789abcdef",NULL,0,403,"inactive session");
    request("OPTIONS",token,NULL,0,200,"Access-Control-Allow-Origin");request("GET",token,NULL,0,200,"waiting");
    request("POST",token,packet,length,202,"ready");assert(preview_bridge_take(received,&bytes,&size));assert(!strcmp(received,token)&&size==length&&!memcmp(bytes,packet,size));free(bytes);
    request("POST",token,packet,length,409,"already submitted");preview_bridge_finish(token,1);request("GET",token,NULL,0,200,"done");
    for(unsigned attempt=0;attempt<16;attempt++){
        assert(preview_bridge_begin(token,error,sizeof(error)));
        request("POST",token,packet,length,202,"ready");
        request("POST",token,packet,length,409,"already submitted");
        assert(preview_bridge_take(received,&bytes,&size));free(bytes);
        preview_bridge_finish(token,1);
    }
    assert(preview_bridge_begin(token,error,sizeof(error)));packet[0]='X';request("POST",token,packet,length,400,"invalid package");assert(!preview_bridge_take(received,&bytes,&size));request("GET",token,NULL,0,200,"error");
    preview_bridge_shutdown();assert(preview_bridge_begin(token,error,sizeof(error)));request("GET",token,NULL,0,200,"waiting");preview_bridge_shutdown();
    puts("preview bridge: staging, loopback upload, token admission, ownership transfer, repeated rejection and restart passed");return 0;
}
