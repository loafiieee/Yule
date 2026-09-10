#include "../preview_http.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
    const char* valid="POST /preview/0123456789abcdef0123456789abcdef HTTP/1.1\r\nHost: 127.0.0.1:31785\r\nContent-Length: 8\r\nContent-Type: application/octet-stream\r\n\r\n";
    PreviewHttpRequest out,before;char buffer[1024];size_t n=strlen(valid);memset(&out,0x5a,sizeof(out));before=out;
    for(size_t i=0;i<n;i++){assert(preview_http_parse(valid,i,31785,&out)==0);assert(!memcmp(&before,&out,sizeof(out)));}
    assert(preview_http_parse(valid,n,31785,&out)==1&&out.length==8&&out.body_offset==n);before=out;
    const char* invalid[]={"Host: attacker.example\r\n","Host: 127.0.0.1:31785\r\nHost: 127.0.0.1:31785\r\n","Host: 127.0.0.1:31785\r\nTransfer-Encoding: chunked\r\n","Host: 127.0.0.1:31785\r\nContent-Length: 8\r\n","Host: 127.0.0.1:31785\r\n bad: fold\r\n","Host: 127.0.0.1:31785\r\nExpect: 100-continue\r\n"};
    for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++){
        snprintf(buffer,sizeof(buffer),"POST /preview/0123456789abcdef0123456789abcdef HTTP/1.1\r\n%sContent-Length: 8\r\nContent-Type: application/octet-stream\r\n\r\n",invalid[i]);
        assert(preview_http_parse(buffer,strlen(buffer),31785,&out)==-1);assert(!memcmp(&before,&out,sizeof(out)));
    }
    snprintf(buffer,sizeof(buffer),"GET /preview/0123456789abcdef0123456789abcdef HTTP/1.1\r\nHost: 127.0.0.1:31785\r\n\r\n");assert(preview_http_parse(buffer,strlen(buffer),31785,&out)==1&&out.method==PREVIEW_HTTP_GET);
    memset(buffer,'x',sizeof(buffer));assert(preview_http_parse(buffer,sizeof(buffer),31785,&out)==0);
    puts("preview HTTP: incremental headers, loopback authority, framing and atomic rejection passed");return 0;
}
