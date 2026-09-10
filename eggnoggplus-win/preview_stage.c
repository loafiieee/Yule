#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "preview_stage.h"
#include "preview_package.h"
#include "launch_request.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static HANDLE hold_directory(const char* path,int create){
    BY_HANDLE_FILE_INFORMATION info;
    if(create&&!CreateDirectoryA(path,NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS)
        return INVALID_HANDLE_VALUE;
    HANDLE handle=CreateFileA(path,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,
        NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if(handle==INVALID_HANDLE_VALUE) return handle;
    if(!GetFileInformationByHandle(handle,&info)||
       !(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||
       (info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)){
        CloseHandle(handle);return INVALID_HANDLE_VALUE;
    }
    return handle;
}
static int join(char* out,size_t capacity,const char* parent,const char* name){
    int n=snprintf(out,capacity,"%s\\%s",parent,name);
    return n>0&&(size_t)n<capacity;
}
typedef struct StagedSession {
    struct StagedSession* next;
    char token[33],folder[MAX_PATH];
    HANDLE directories[3];
    unsigned count;
    struct {char name[PREVIEW_PACKAGE_NAME_BYTES];BY_HANDLE_FILE_INFORMATION identity;} files[PREVIEW_PACKAGE_MAX_FILES];
} StagedSession;
static StagedSession* staged;
static int same_file(const BY_HANDLE_FILE_INFORMATION* a,const BY_HANDLE_FILE_INFORMATION* b){
    return a->dwVolumeSerialNumber==b->dwVolumeSerialNumber&&
        a->nFileIndexHigh==b->nFileIndexHigh&&a->nFileIndexLow==b->nFileIndexLow&&
        a->nFileSizeHigh==b->nFileSizeHigh&&a->nFileSizeLow==b->nFileSizeLow&&
        CompareFileTime(&a->ftLastWriteTime,&b->ftLastWriteTime)==0;
}
void preview_stage_collect(PreviewStageInUse in_use){
    if(!in_use)return;
    StagedSession** link=&staged;
    while(*link){
        StagedSession* item=*link;
        if(in_use(item->token)){link=&item->next;continue;}
        for(unsigned i=0;i<item->count;i++){
            char path[MAX_PATH];BY_HANDLE_FILE_INFORMATION identity;
            if(!join(path,sizeof(path),item->folder,item->files[i].name))continue;
            HANDLE file=CreateFileA(path,DELETE|FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,
                FILE_FLAG_OPEN_REPARSE_POINT,NULL);
            if(file==INVALID_HANDLE_VALUE)continue;
            if(GetFileInformationByHandle(file,&identity)&&
               !(identity.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))&&
               same_file(&identity,&item->files[i].identity)){
                FILE_DISPOSITION_INFO disposition={TRUE};
                SetFileInformationByHandle(file,FileDispositionInfo,&disposition,sizeof(disposition));
            }
            CloseHandle(file);
        }
        CloseHandle(item->directories[2]);
        RemoveDirectoryA(item->folder); /* Only removes an empty session folder. */
        CloseHandle(item->directories[1]);CloseHandle(item->directories[0]);
        *link=item->next;free(item);
    }
}
int preview_stage_package(const char* maps_root,const char* token,
                          const void* bytes,size_t size,char* error,size_t capacity){
    PreviewPackage package;
    StagedSession* record=NULL;
    char root[MAX_PATH],cache[MAX_PATH],folder[MAX_PATH],file[MAX_PATH];
    HANDLE roots[3]={INVALID_HANDLE_VALUE,INVALID_HANDLE_VALUE,INVALID_HANDLE_VALUE};
    unsigned created=0;int success=0,created_folder=0;
    const char* failure="Could not create preview folder.";
    if(!maps_root||!launch_request_preview_session_valid(token)){
        if(error&&capacity)snprintf(error,capacity,"Invalid preview destination.");
        return 0;
    }
    if(!preview_package_decode(bytes,size,&package,error,capacity))return 0;
    DWORD length=GetFullPathNameA(maps_root,sizeof(root),root,NULL);
    if(!length||length>=sizeof(root)||!join(cache,sizeof(cache),root,"_greggnogg_previews")||
       !join(folder,sizeof(folder),cache,token))goto done;
    /* Hold every directory we create/use without delete sharing so it cannot
       be replaced by a junction between checking it and writing its children. */
    record=calloc(1,sizeof(*record));if(!record)goto done;
    roots[0]=hold_directory(root,1);if(roots[0]==INVALID_HANDLE_VALUE)goto done;
    roots[1]=hold_directory(cache,1);if(roots[1]==INVALID_HANDLE_VALUE)goto done;
    failure="Preview session already exists or cannot be created.";
    if(!CreateDirectoryA(folder,NULL))goto done;
    created_folder=1;
    roots[2]=hold_directory(folder,0);if(roots[2]==INVALID_HANDLE_VALUE)goto done;
    failure="Could not write preview files.";
    for(unsigned i=0;i<package.count;i++){
        const PreviewFile* entry=&package.files[i];
        if(!join(file,sizeof(file),folder,entry->name))goto done;
        HANDLE output=CreateFileA(file,GENERIC_WRITE,0,NULL,CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
        if(output==INVALID_HANDLE_VALUE)goto done;
        created=i+1;
        DWORD written=0;
        int ok=WriteFile(output,entry->bytes,entry->size,&written,NULL)&&written==entry->size;
        if(!CloseHandle(output))ok=0;
        if(!ok)goto done;
        output=CreateFileA(file,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,
                           NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
        if(output==INVALID_HANDLE_VALUE)goto done;
        ok=GetFileInformationByHandle(output,&record->files[i].identity);
        CloseHandle(output);if(!ok)goto done;
        snprintf(record->files[i].name,sizeof(record->files[i].name),"%s",entry->name);
    }
    snprintf(record->token,sizeof(record->token),"%s",token);
    snprintf(record->folder,sizeof(record->folder),"%s",folder);
    record->count=package.count;
    for(unsigned i=0;i<3;i++){record->directories[i]=roots[i];roots[i]=INVALID_HANDLE_VALUE;}
    record->next=staged;staged=record;record=NULL;
    success=1;
done:
    free(record);
    if(!success&&created_folder){
        for(unsigned i=0;i<created;i++)
            if(join(file,sizeof(file),folder,package.files[i].name))DeleteFileA(file);
    }
    if(roots[2]!=INVALID_HANDLE_VALUE)CloseHandle(roots[2]);
    if(!success&&created_folder)RemoveDirectoryA(folder);
    if(roots[1]!=INVALID_HANDLE_VALUE)CloseHandle(roots[1]);
    if(roots[0]!=INVALID_HANDLE_VALUE)CloseHandle(roots[0]);
    if(error&&capacity)snprintf(error,capacity,"%s",success?"":failure);
    return success;
}
