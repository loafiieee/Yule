#include "preview_http.h"
#include "preview_package.h"
#include "launch_request.h"
#include <stdio.h>
#include <string.h>
static int equal_ci(const char* a,const char* b){while(*a&&*b){unsigned char x=(unsigned char)*a++,y=(unsigned char)*b++;if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;if(x!=y)return 0;}return *a==*b;}
static int decimal(const char* s,uint32_t* out){uint32_t n=0;if(!*s)return 0;for(;*s;s++){if(*s<'0'||*s>'9'||n>(PREVIEW_PACKAGE_MAX_BYTES-(uint32_t)(*s-'0'))/10u)return 0;n=n*10u+(uint32_t)(*s-'0');}*out=n;return 1;}
int preview_http_parse(const void* data,size_t size,unsigned port,PreviewHttpRequest* out){
    const unsigned char* bytes=data;char text[PREVIEW_HTTP_HEADER_MAX+1],host[64],*line,*next,*path,*version;size_t end=0;
    PreviewHttpRequest candidate;int have_host=0,have_length=0,have_type=0;
    if(!bytes||!out||!port||port>65535)return -1;
    for(size_t i=0;i+3<size&&i+3<PREVIEW_HTTP_HEADER_MAX;i++)if(!memcmp(bytes+i,"\r\n\r\n",4)){end=i+4;break;}
    if(!end)return size>=PREVIEW_HTTP_HEADER_MAX?-1:0;
    if(memchr(bytes,0,end))return -1;
    memset(&candidate,0,sizeof(candidate));candidate.body_offset=end;memcpy(text,bytes,end);text[end]=0;
    line=text;next=strstr(line,"\r\n");if(!next)return -1;*next=0;next+=2;
    path=strchr(line,' ');if(!path)return -1;*path++=0;version=strchr(path,' ');if(!version)return -1;*version++=0;
    if(strcmp(version,"HTTP/1.1"))return -1;
    if(!strcmp(line,"GET"))candidate.method=PREVIEW_HTTP_GET;
    else if(!strcmp(line,"POST"))candidate.method=PREVIEW_HTTP_POST;
    else if(!strcmp(line,"OPTIONS"))candidate.method=PREVIEW_HTTP_OPTIONS;else return -1;
    if(strncmp(path,"/preview/",9)||!launch_request_preview_session_valid(path+9))return -1;
    memcpy(candidate.token,path+9,33);snprintf(host,sizeof(host),"127.0.0.1:%u",port);
    while(*next){
        char* colon;char* value;char* last;line=next;next=strstr(line,"\r\n");if(!next)return -1;*next=0;next+=2;if(!*line)break;
        colon=strchr(line,':');if(!colon||colon==line)return -1;*colon=0;
        for(char* c=line;*c;c++)if(!((*c>='a'&&*c<='z')||(*c>='A'&&*c<='Z')||(*c>='0'&&*c<='9')||strchr("!#$%&'*+-.^_`|~",*c)))return -1;
        value=colon+1;while(*value==' '||*value=='\t')value++;last=value+strlen(value);while(last>value&&(last[-1]==' '||last[-1]=='\t'))*--last=0;
        for(char* c=value;*c;c++)if(((unsigned char)*c<32&&*c!='\t')||(unsigned char)*c==127)return -1;
        if(equal_ci(line,"Host")){if(have_host++||strcmp(value,host))return -1;}
        else if(equal_ci(line,"Content-Length")){if(have_length++||!decimal(value,&candidate.length))return -1;}
        else if(equal_ci(line,"Transfer-Encoding")||equal_ci(line,"Expect"))return -1;
        else if(equal_ci(line,"Content-Type")){if(have_type++||!equal_ci(value,"application/octet-stream"))return -1;}
    }
    if(!have_host)return -1;
    if(candidate.method==PREVIEW_HTTP_POST){if(!have_length||!have_type||candidate.length<8)return -1;}
    else if(candidate.length)return -1;
    if(size-end>candidate.length)return -1;
    *out=candidate;return 1;
}
