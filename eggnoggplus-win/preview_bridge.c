#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include "preview_bridge.h"
#include "preview_http.h"
#include "preview_package.h"
#include "launch_request.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static CRITICAL_SECTION lock;
static int initialized,state;
static SOCKET listener=INVALID_SOCKET;
static HANDLE thread;
static LONG stopping;
static char session[33];
static DWORD expires;
static unsigned port;
static unsigned char* pending;
static size_t pending_size;
static int current(const char* token){unsigned mismatch=0;for(size_t i=0;i<32;i++)mismatch|=(unsigned char)token[i]^(unsigned char)session[i];return !mismatch&&state&&((LONG)(GetTickCount()-expires)<0);}
static int active(const char* token){int result;EnterCriticalSection(&lock);result=current(token);LeaveCriticalSection(&lock);return result;}
static int send_all(SOCKET socket,const char* bytes,size_t size){while(size){int n=send(socket,bytes,(int)size,0);if(n<=0)return 0;bytes+=n;size-=(size_t)n;}return 1;}
static void respond(SOCKET socket,int code,const char* body){
    char header[768];size_t size=strlen(body);int n=snprintf(header,sizeof(header),"HTTP/1.1 %d Preview\r\nContent-Type: application/json\r\nContent-Length: %u\r\nConnection: close\r\nCache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type\r\nAccess-Control-Allow-Private-Network: true\r\n\r\n",code,(unsigned)size);
    if(n>0&&(size_t)n<sizeof(header)&&send_all(socket,header,(size_t)n))send_all(socket,body,size);
}
static void receive_request(SOCKET socket){
    unsigned char header[PREVIEW_HTTP_HEADER_MAX];size_t used=0;PreviewHttpRequest request;int result=0;DWORD started=GetTickCount(),timeout=1000;
    setsockopt(socket,SOL_SOCKET,SO_RCVTIMEO,(const char*)&timeout,sizeof(timeout));setsockopt(socket,SOL_SOCKET,SO_SNDTIMEO,(const char*)&timeout,sizeof(timeout));
    while(!result&&used<sizeof(header)&&!InterlockedCompareExchange(&stopping,0,0)){
        int n=recv(socket,(char*)header+used,(int)(sizeof(header)-used),0);if(n<=0)return;used+=(size_t)n;
        result=preview_http_parse(header,used,port,&request);if(GetTickCount()-started>3000u)return;
    }
    if(result!=1){respond(socket,400,"{\"error\":\"invalid request\"}");return;}
    EnterCriticalSection(&lock);int accepted=current(request.token),snapshot=state;
    if(accepted&&request.method==PREVIEW_HTTP_POST&&state==1)state=2;
    LeaveCriticalSection(&lock);
    if(!accepted){respond(socket,403,"{\"error\":\"inactive session\"}");return;}
    if(request.method==PREVIEW_HTTP_OPTIONS){respond(socket,200,"{}");return;}
    if(request.method==PREVIEW_HTTP_GET){
        static const char* names[]={"inactive","waiting","receiving","ready","processing","done","error"};char body[64];
        snprintf(body,sizeof(body),"{\"state\":\"%s\"}",snapshot>=0&&snapshot<=6?names[snapshot]:"error");respond(socket,200,body);return;
    }
    if(snapshot!=1){respond(socket,409,"{\"error\":\"package already submitted\"}");return;}
    unsigned char* bytes=malloc(request.length);size_t count=used-request.body_offset;int valid=bytes!=NULL;
    if(bytes)memcpy(bytes,header+request.body_offset,count);
    while(valid&&count<request.length){
        if(InterlockedCompareExchange(&stopping,0,0)||!active(request.token)||GetTickCount()-started>60000u){valid=0;break;}
        size_t remaining=request.length-count;int n=recv(socket,(char*)bytes+count,(int)(remaining>65536u?65536u:remaining),0);
        if(n<=0){valid=0;break;}count+=(size_t)n;
    }
    PreviewPackage package;char error[128];if(valid)valid=preview_package_decode(bytes,count,&package,error,sizeof(error));
    EnterCriticalSection(&lock);
    if(current(request.token)&&state==2){if(valid){pending=bytes;pending_size=count;bytes=NULL;state=3;}else state=6;}
    else valid=0;
    LeaveCriticalSection(&lock);free(bytes);
    respond(socket,valid?202:400,valid?"{\"state\":\"ready\"}":"{\"error\":\"invalid package\"}");
}
static DWORD WINAPI serve(void* unused){
    (void)unused;
    while(!InterlockedCompareExchange(&stopping,0,0)){
        fd_set reads;struct timeval wait={0,100000};FD_ZERO(&reads);FD_SET(listener,&reads);
        if(select(0,&reads,NULL,NULL,&wait)>0){SOCKET client=accept(listener,NULL,NULL);if(client!=INVALID_SOCKET){receive_request(client);
            /* Deliver the response before closing a socket with unread request
               bytes. An abortive close can otherwise discard the HTTP response. */
            shutdown(client,SD_SEND);
            {
                char discard[4096];
                DWORD drain_started=GetTickCount();
                while(!InterlockedCompareExchange(&stopping,0,0)&&
                      GetTickCount()-drain_started<1000u&&
                      recv(client,discard,sizeof(discard),0)>0) {}
            }
            closesocket(client);}}
    }
    return 0;
}
int preview_bridge_begin(const char* token,char* error,size_t capacity){
    if(!launch_request_preview_session_valid(token)){if(error&&capacity)snprintf(error,capacity,"Invalid preview token.");return 0;}
    if(!initialized){
        WSADATA wsa;struct sockaddr_in address;int address_size=sizeof(address);BOOL exclusive=TRUE;
        if(WSAStartup(MAKEWORD(2,2),&wsa)){if(error&&capacity)snprintf(error,capacity,"Preview networking unavailable.");return 0;}
        listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);memset(&address,0,sizeof(address));address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons(PREVIEW_BRIDGE_PORT);
        if(listener==INVALID_SOCKET||setsockopt(listener,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,(const char*)&exclusive,sizeof(exclusive))||bind(listener,(struct sockaddr*)&address,sizeof(address))||listen(listener,4)||getsockname(listener,(struct sockaddr*)&address,&address_size)){
            if(listener!=INVALID_SOCKET) closesocket(listener);
            listener=INVALID_SOCKET;
            WSACleanup();
            if(error&&capacity) snprintf(error,capacity,"Preview loopback port is unavailable.");
            return 0;
        }
        port=ntohs(address.sin_port);InitializeCriticalSection(&lock);initialized=1;InterlockedExchange(&stopping,0);
        thread=CreateThread(NULL,0,serve,NULL,0,NULL);
        if(!thread){closesocket(listener);listener=INVALID_SOCKET;DeleteCriticalSection(&lock);initialized=0;WSACleanup();if(error&&capacity)snprintf(error,capacity,"Preview worker could not start.");return 0;}
    }
    EnterCriticalSection(&lock);free(pending);pending=NULL;pending_size=0;memcpy(session,token,33);expires=GetTickCount()+120000u;state=1;LeaveCriticalSection(&lock);
    if(error&&capacity) error[0]=0;
    return 1;
}
int preview_bridge_take(char token[33],unsigned char** bytes,size_t* size){
    int ready;
    if(!initialized||!token||!bytes||!size) return 0;
    EnterCriticalSection(&lock);ready=state==3&&current(session);
    if(ready){memcpy(token,session,33);*bytes=pending;*size=pending_size;pending=NULL;pending_size=0;state=4;}
    LeaveCriticalSection(&lock);return ready;
}
void preview_bridge_finish(const char* token,int success){if(!initialized||!launch_request_preview_session_valid(token))return;EnterCriticalSection(&lock);if(current(token)&&state==4)state=success?5:6;LeaveCriticalSection(&lock);}
void preview_bridge_cancel(void){
    if(!initialized)return;
    EnterCriticalSection(&lock);
    if(state>=1&&state<=4){state=6;free(pending);pending=NULL;pending_size=0;}
    LeaveCriticalSection(&lock);
}
unsigned preview_bridge_port(void){return port;}
void preview_bridge_shutdown(void){
    if(!initialized) return;
    InterlockedExchange(&stopping,1);
    WaitForSingleObject(thread,INFINITE);
    CloseHandle(thread);
    thread=NULL;
    closesocket(listener);listener=INVALID_SOCKET;EnterCriticalSection(&lock);free(pending);pending=NULL;pending_size=0;state=0;session[0]=0;LeaveCriticalSection(&lock);
    DeleteCriticalSection(&lock);initialized=0;WSACleanup();
}
