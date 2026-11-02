/* Virtual "filesystem_stdio.dll" for PE mods built on the HL25 SDK.
 *
 * Such DLLs call LoadLibraryA("filesystem_stdio.dll"), GetProcAddress
 * ("CreateInterface") and request "VFileSystem009". The engine has already
 * loaded and configured the native filesystem_stdio.so; this module hands the
 * PE an object with the Microsoft C++ ABI whose methods forward to that same
 * native singleton.
 *
 * Microsoft ABI: methods are thiscall (this in ECX, callee pops), the virtual
 * destructor has one slot, overloads are grouped (Size(const char*) precedes
 * Size(FileHandle_t)). FPrintf is variadic, hence cdecl with this on the stack.
 * Itanium ABI (native): this is the first stack argument, two destructor
 * slots, declaration order. tools/check_filesystem_abi.py verifies both orders
 * against public/FileSystem.h and clang-cl's MSVC vtable layout. */
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <link.h>
#include "filesystem.h"

typedef void *FileHandle_t;
/* Native slot = 2 (complete and deleting destructors) + declaration index. */
enum native_slot {
    FS_Mount,FS_Unmount,FS_RemoveAllSearchPaths,FS_AddSearchPath,FS_RemoveSearchPath,FS_RemoveFile,
    FS_CreateDirHierarchy,FS_FileExists,FS_IsDirectory,FS_Open,FS_Close,FS_Seek,FS_Tell,
    FS_SizeHandle,FS_SizeName,FS_GetFileTime,FS_FileTimeToString,FS_IsOk,FS_Flush,FS_EndOfFile,
    FS_Read,FS_Write,FS_ReadLine,FS_FPrintf,FS_GetReadBuffer,FS_ReleaseReadBuffer,FS_FindFirst,
    FS_FindNext,FS_FindIsDirectory,FS_FindClose,FS_GetLocalCopy,FS_GetLocalPath,FS_ParseFile,
    FS_FullPathToRelativePath,FS_GetCurrentDirectory,FS_PrintOpenedFiles,FS_SetWarningFunc,
    FS_SetWarningLevel,FS_LogLevelLoadStarted,FS_LogLevelLoadFinished,FS_HintResourceNeed,
    FS_PauseResourcePreloading,FS_ResumeResourcePreloading,FS_SetVBuf,FS_GetInterfaceVersion,
    FS_IsFileImmediatelyAvailable,FS_WaitForResources,FS_GetWaitForResourcesProgress,
    FS_CancelWaitForResources,FS_IsAppReadyForOfflinePlay,FS_AddPackFile,FS_OpenFromCacheForRead,
    FS_AddSearchPathNoWrite,FS_GetFileModificationTime,FS_METHOD_COUNT
};
_Static_assert(FS_METHOD_COUNT==54,"IFileSystem method count");

static void *native;            /* native IFileSystem* */
static void *const *native_vtable;
#define CALL(method,ret,types,...) ((ret(*)types)native_vtable[2+FS_##method])(native,__VA_ARGS__)
#define CALL0(method,ret) ((ret(*)(void*))native_vtable[2+FS_##method])(native)
/* PE callers guarantee 4-byte stack alignment only. */
#define METHOD __attribute__((thiscall,force_align_arg_pointer,noinline))
struct pe_filesystem {const void *const *vtable;};
typedef struct pe_filesystem self_t;

METHOD static void *fs_Destructor(self_t *self,unsigned flags) {(void)flags;return self; /* static singleton */}
METHOD static void fs_Mount(self_t *s) {(void)s;CALL0(Mount,void);}
METHOD static void fs_Unmount(self_t *s) {(void)s;CALL0(Unmount,void);}
METHOD static void fs_RemoveAllSearchPaths(self_t *s) {(void)s;CALL0(RemoveAllSearchPaths,void);}
METHOD static void fs_AddSearchPath(self_t *s,const char *path,const char *id) {(void)s;CALL(AddSearchPath,void,(void*,const char*,const char*),path,id);}
METHOD static bool fs_RemoveSearchPath(self_t *s,const char *path) {(void)s;return CALL(RemoveSearchPath,bool,(void*,const char*),path);}
METHOD static void fs_RemoveFile(self_t *s,const char *path,const char *id) {(void)s;CALL(RemoveFile,void,(void*,const char*,const char*),path,id);}
METHOD static void fs_CreateDirHierarchy(self_t *s,const char *path,const char *id) {(void)s;CALL(CreateDirHierarchy,void,(void*,const char*,const char*),path,id);}
METHOD static bool fs_FileExists(self_t *s,const char *name) {(void)s;return CALL(FileExists,bool,(void*,const char*),name);}
METHOD static bool fs_IsDirectory(self_t *s,const char *name) {(void)s;return CALL(IsDirectory,bool,(void*,const char*),name);}
METHOD static FileHandle_t fs_Open(self_t *s,const char *name,const char *options,const char *id) {(void)s;return CALL(Open,FileHandle_t,(void*,const char*,const char*,const char*),name,options,id);}
METHOD static void fs_Close(self_t *s,FileHandle_t f) {(void)s;CALL(Close,void,(void*,FileHandle_t),f);}
METHOD static void fs_Seek(self_t *s,FileHandle_t f,int position,int type) {(void)s;CALL(Seek,void,(void*,FileHandle_t,int,int),f,position,type);}
METHOD static unsigned fs_Tell(self_t *s,FileHandle_t f) {(void)s;return CALL(Tell,unsigned,(void*,FileHandle_t),f);}
METHOD static unsigned fs_SizeHandle(self_t *s,FileHandle_t f) {(void)s;return CALL(SizeHandle,unsigned,(void*,FileHandle_t),f);}
METHOD static unsigned fs_SizeName(self_t *s,const char *name) {(void)s;return CALL(SizeName,unsigned,(void*,const char*),name);}
METHOD static long fs_GetFileTime(self_t *s,const char *name) {(void)s;return CALL(GetFileTime,long,(void*,const char*),name);}
METHOD static void fs_FileTimeToString(self_t *s,char *out,int size,long time) {(void)s;CALL(FileTimeToString,void,(void*,char*,int,long),out,size,time);}
METHOD static bool fs_IsOk(self_t *s,FileHandle_t f) {(void)s;return CALL(IsOk,bool,(void*,FileHandle_t),f);}
METHOD static void fs_Flush(self_t *s,FileHandle_t f) {(void)s;CALL(Flush,void,(void*,FileHandle_t),f);}
METHOD static bool fs_EndOfFile(self_t *s,FileHandle_t f) {(void)s;return CALL(EndOfFile,bool,(void*,FileHandle_t),f);}
METHOD static int fs_Read(self_t *s,void *out,int size,FileHandle_t f) {(void)s;return CALL(Read,int,(void*,void*,int,FileHandle_t),out,size,f);}
METHOD static int fs_Write(self_t *s,const void *in,int size,FileHandle_t f) {(void)s;return CALL(Write,int,(void*,const void*,int,FileHandle_t),in,size,f);}
METHOD static char *fs_ReadLine(self_t *s,char *out,int size,FileHandle_t f) {(void)s;return CALL(ReadLine,char*,(void*,char*,int,FileHandle_t),out,size,f);}
__attribute__((force_align_arg_pointer,noinline)) static int fs_FPrintf(self_t *s,FileHandle_t f,char *format,...) {
    (void)s;va_list args;va_start(args,format);
    va_list copy;va_copy(copy,args);int length=vsnprintf(NULL,0,format,copy);va_end(copy);
    if(length<0){va_end(args);return -1;}
    char *text=malloc((size_t)length+1);if(!text){va_end(args);return -1;}
    vsnprintf(text,(size_t)length+1,format,args);va_end(args);
    int result=CALL(FPrintf,int,(void*,FileHandle_t,const char*,...),f,"%s",text);
    free(text);return result;
}
METHOD static void *fs_GetReadBuffer(self_t *s,FileHandle_t f,int *size,bool cached) {(void)s;return CALL(GetReadBuffer,void*,(void*,FileHandle_t,int*,bool),f,size,cached);}
METHOD static void fs_ReleaseReadBuffer(self_t *s,FileHandle_t f,void *buffer) {(void)s;CALL(ReleaseReadBuffer,void,(void*,FileHandle_t,void*),f,buffer);}
METHOD static const char *fs_FindFirst(self_t *s,const char *wildcard,int *handle,const char *id) {(void)s;return CALL(FindFirst,const char*,(void*,const char*,int*,const char*),wildcard,handle,id);}
METHOD static const char *fs_FindNext(self_t *s,int handle) {(void)s;return CALL(FindNext,const char*,(void*,int),handle);}
METHOD static bool fs_FindIsDirectory(self_t *s,int handle) {(void)s;return CALL(FindIsDirectory,bool,(void*,int),handle);}
METHOD static void fs_FindClose(self_t *s,int handle) {(void)s;CALL(FindClose,void,(void*,int),handle);}
METHOD static void fs_GetLocalCopy(self_t *s,const char *name) {(void)s;CALL(GetLocalCopy,void,(void*,const char*),name);}
METHOD static const char *fs_GetLocalPath(self_t *s,const char *name,char *out,int size) {(void)s;return CALL(GetLocalPath,const char*,(void*,const char*,char*,int),name,out,size);}
METHOD static char *fs_ParseFile(self_t *s,char *bytes,char *token,bool *quoted) {(void)s;return CALL(ParseFile,char*,(void*,char*,char*,bool*),bytes,token,quoted);}
METHOD static bool fs_FullPathToRelativePath(self_t *s,const char *full,char *relative) {(void)s;return CALL(FullPathToRelativePath,bool,(void*,const char*,char*),full,relative);}
METHOD static bool fs_GetCurrentDirectory(self_t *s,char *out,int size) {(void)s;return CALL(GetCurrentDirectory,bool,(void*,char*,int),out,size);}
METHOD static void fs_PrintOpenedFiles(self_t *s) {(void)s;CALL0(PrintOpenedFiles,void);}
/* The PE warning callback is cdecl in both ABIs; native callers need no adapter. */
METHOD static void fs_SetWarningFunc(self_t *s,void(*warning)(const char*,...)) {(void)s;CALL(SetWarningFunc,void,(void*,void(*)(const char*,...)),warning);}
METHOD static void fs_SetWarningLevel(self_t *s,int level) {(void)s;CALL(SetWarningLevel,void,(void*,int),level);}
METHOD static void fs_LogLevelLoadStarted(self_t *s,const char *name) {(void)s;CALL(LogLevelLoadStarted,void,(void*,const char*),name);}
METHOD static void fs_LogLevelLoadFinished(self_t *s,const char *name) {(void)s;CALL(LogLevelLoadFinished,void,(void*,const char*),name);}
METHOD static int fs_HintResourceNeed(self_t *s,const char *list,int forget) {(void)s;return CALL(HintResourceNeed,int,(void*,const char*,int),list,forget);}
METHOD static int fs_PauseResourcePreloading(self_t *s) {(void)s;return CALL0(PauseResourcePreloading,int);}
METHOD static int fs_ResumeResourcePreloading(self_t *s) {(void)s;return CALL0(ResumeResourcePreloading,int);}
METHOD static int fs_SetVBuf(self_t *s,FileHandle_t f,char *buffer,int mode,long size) {(void)s;return CALL(SetVBuf,int,(void*,FileHandle_t,char*,int,long),f,buffer,mode,size);}
METHOD static void fs_GetInterfaceVersion(self_t *s,char *out,int size) {(void)s;CALL(GetInterfaceVersion,void,(void*,char*,int),out,size);}
METHOD static bool fs_IsFileImmediatelyAvailable(self_t *s,const char *name) {(void)s;return CALL(IsFileImmediatelyAvailable,bool,(void*,const char*),name);}
METHOD static int fs_WaitForResources(self_t *s,const char *list) {(void)s;return CALL(WaitForResources,int,(void*,const char*),list);}
METHOD static bool fs_GetWaitForResourcesProgress(self_t *s,int handle,float *progress,bool *complete) {(void)s;return CALL(GetWaitForResourcesProgress,bool,(void*,int,float*,bool*),handle,progress,complete);}
METHOD static void fs_CancelWaitForResources(self_t *s,int handle) {(void)s;CALL(CancelWaitForResources,void,(void*,int),handle);}
METHOD static bool fs_IsAppReadyForOfflinePlay(self_t *s,int app) {(void)s;return CALL(IsAppReadyForOfflinePlay,bool,(void*,int),app);}
METHOD static bool fs_AddPackFile(self_t *s,const char *path,const char *id) {(void)s;return CALL(AddPackFile,bool,(void*,const char*,const char*),path,id);}
METHOD static FileHandle_t fs_OpenFromCacheForRead(self_t *s,const char *name,const char *options,const char *id) {(void)s;return CALL(OpenFromCacheForRead,FileHandle_t,(void*,const char*,const char*,const char*),name,options,id);}
METHOD static void fs_AddSearchPathNoWrite(self_t *s,const char *path,const char *id) {(void)s;CALL(AddSearchPathNoWrite,void,(void*,const char*,const char*),path,id);}
METHOD static long fs_GetFileModificationTime(self_t *s,const char *name) {(void)s;return CALL(GetFileModificationTime,long,(void*,const char*),name);}

/* Microsoft vftable order (clang-cl -fdump-vtable-layouts). */
static const void *const msvc_vtable[]={
    fs_Destructor,
    fs_Mount,fs_Unmount,fs_RemoveAllSearchPaths,fs_AddSearchPath,fs_RemoveSearchPath,fs_RemoveFile,
    fs_CreateDirHierarchy,fs_FileExists,fs_IsDirectory,fs_Open,fs_Close,fs_Seek,fs_Tell,
    fs_SizeName,fs_SizeHandle,
    fs_GetFileTime,fs_FileTimeToString,fs_IsOk,fs_Flush,fs_EndOfFile,fs_Read,fs_Write,fs_ReadLine,
    fs_FPrintf,fs_GetReadBuffer,fs_ReleaseReadBuffer,fs_FindFirst,fs_FindNext,fs_FindIsDirectory,
    fs_FindClose,fs_GetLocalCopy,fs_GetLocalPath,fs_ParseFile,fs_FullPathToRelativePath,
    fs_GetCurrentDirectory,fs_PrintOpenedFiles,fs_SetWarningFunc,fs_SetWarningLevel,
    fs_LogLevelLoadStarted,fs_LogLevelLoadFinished,fs_HintResourceNeed,fs_PauseResourcePreloading,
    fs_ResumeResourcePreloading,fs_SetVBuf,fs_GetInterfaceVersion,fs_IsFileImmediatelyAvailable,
    fs_WaitForResources,fs_GetWaitForResourcesProgress,fs_CancelWaitForResources,
    fs_IsAppReadyForOfflinePlay,fs_AddPackFile,fs_OpenFromCacheForRead,fs_AddSearchPathNoWrite,
    fs_GetFileModificationTime
};
_Static_assert(sizeof(msvc_vtable)/sizeof(*msvc_vtable)==FS_METHOD_COUNT+1,"MSVC vftable size");
static struct pe_filesystem pe_object={msvc_vtable};

#define INTERFACE_NAME "VFileSystem009"
static char native_path[4096];
static int find_loaded(struct dl_phdr_info *info,size_t size,void *data) {
    (void)size;(void)data;
    const char *name=info->dlpi_name,*base=strrchr(name,'/');base=base?base+1:name;
    if(strcmp(base,"filesystem_stdio.so"))return 0;
    snprintf(native_path,sizeof(native_path),"%s",name);return 1;
}
/* The engine loads filesystem_stdio.so before any game DLL; reuse that copy
 * so the PE sees the engine's search paths. CB_FILESYSTEM_SO is for tests
 * without an engine; that copy must be configured by the test itself. */
__attribute__((force_align_arg_pointer,noinline)) static int attach_native(void) {
    if(native)return 0;
    void *library=NULL;
    if(dl_iterate_phdr(find_loaded,NULL))library=dlopen(native_path,RTLD_NOW|RTLD_NOLOAD);
    else if(getenv("CB_FILESYSTEM_SO"))library=dlopen(getenv("CB_FILESYSTEM_SO"),RTLD_NOW);
    if(!library){fprintf(stderr,"[bridge] native filesystem_stdio.so is not loaded in this process\n");return -1;}
    void *(*factory)(const char*,int*)=(void*(*)(const char*,int*))dlsym(library,"CreateInterface");
    void *instance=factory?factory(INTERFACE_NAME,NULL):NULL;
    /* The reference is kept: the PE may hold file handles until it unloads. */
    if(!instance){fprintf(stderr,"[bridge] native filesystem lacks %s\n",INTERFACE_NAME);return -1;}
    native=instance;native_vtable=*(void*const*const*)instance;
    fprintf(stderr,"[bridge] filesystem_stdio.dll -> native %s %s\n",INTERFACE_NAME,native_path[0]?native_path:getenv("CB_FILESYSTEM_SO"));
    return 0;
}
__attribute__((force_align_arg_pointer,noinline)) static void *pe_create_interface(const char *name,int *code) {
    bool found=name&&!strcmp(name,INTERFACE_NAME)&&native;
    if(code)*code=found?0:1; /* IFACE_OK, IFACE_FAILED */
    if(!found)fprintf(stderr,"[bridge] filesystem_stdio.dll CreateInterface(%s): not provided\n",name?name:"NULL");
    return found?&pe_object:NULL;
}
static char module_tag;
void *cb_filesystem_module(void) {return attach_native()?NULL:&module_tag;}
int cb_is_filesystem_module(void *handle) {return handle&&handle==&module_tag&&native;}
void *cb_filesystem_symbol(const char *name) {return strcmp(name,"CreateInterface")?NULL:(void*)pe_create_interface;}
