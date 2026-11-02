/* Experimental, single-thread PE32 host. Unknown WinAPI calls fail explicitly. */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <search.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <pthread.h>
#include <iconv.h>
#include <locale.h>
#include <wctype.h>
#include <fcntl.h>
#include <time.h>
#include <sched.h>
#include <sys/resource.h>
#include <cpuid.h>
#include <ctype.h>
#include <dirent.h>
#include "winnt_types.h"
#include "pe_linker.h"
#include "ntoskernel.h"
#include "winexports.h"
#include "runtime.h"
#include "filesystem.h"
#include "sdl.h"
#include "fmod.h"
/* Win32 BOOL is a 32-bit int; the loader headers declare it as uint8_t,
 * which would leave the upper bits of EAX and of BOOL* outputs undefined. */
typedef int WIN_BOOL;

extern uintptr_t LocalStorage[1024];
static struct pe_image image;
static struct pe_image server_image;
static bool runtime_started,server_attached;
static char server_path[4096];
static struct pe_image vgui_image;
static struct pe_image host_image;
static char vgui_path[4096];
static DWORD last_error;
static bool tls_used[1024] = {true};
static pid_t owner_thread;
static uint16_t original_fs;
static bool attached;
extern void release_nt_threadinfo(void);
extern PKUSER_SHARED_DATA SharedUserData;
extern void *cb_gdi_symbol(const char *name);
extern void cb_gdi_unload(void);
struct binding {const char *name; void *target; unsigned calls; void *thunk;};
/* All PE modules of the process share one table: an HL25 client, VGUI, the
 * VGUI helper and the server import about 1060 functions (Gunman: 1018). */
#define MAX_BINDINGS 8192u
#define THUNK_BYTES 32u
static struct binding bindings[MAX_BINDINGS];
static size_t binding_count;
static void *thunk_page;
static const size_t thunk_size=MAX_BINDINGS*THUNK_BYTES;
static void put_u32(unsigned char *dest,uint32_t value) {memcpy(dest,&value,sizeof(value));}

__attribute__((noreturn)) static void unsupported(const char *name) {
    fprintf(stderr,"[bridge] UNSUPPORTED WinAPI: %s\n",name);
    gb_report();
    fflush(stderr); abort();
}
static void check_thread(void) {
    if ((pid_t)syscall(SYS_gettid)!=owner_thread) unsupported("PE runtime on another thread");
}
static void *implemented_api(const char *name);
static void *find_implemented(const char *name) {
    for (size_t i=0;i<binding_count;i++)
        if (bindings[i].target && !strcmp(bindings[i].name,name)) return bindings[i].thunk;
    return implemented_api(name);
}

/* Private heap allocation ownership, including HeapDestroy and size queries.
 * Modern UCRT sends every malloc through HeapAlloc(GetProcessHeap()), so a
 * block carries its owner in a header: free/size/realloc are O(1), HeapDestroy
 * walks only that heap, and a pointer from another allocator is still trapped. */
struct heap;
struct allocation {uint32_t magic; struct heap *heap; size_t size; struct allocation *prev,*next;};
struct heap {struct allocation *allocations;};
#define ALLOCATION_MAGIC 0x50454850u /* "PHEP" */
#define ALLOCATION_HEADER 32u /* keeps malloc's 16-byte alignment for the PE */
_Static_assert(sizeof(struct allocation)<=ALLOCATION_HEADER,"heap header");
static struct heap process_heap;
static HANDLE WINAPI w_HeapCreate(DWORD flags, SIZE_T initial, SIZE_T maximum) {
    if (flags & ~1u) unsupported("HeapCreate flags");
    if (maximum) unsupported("HeapCreate fixed-size heap");
    (void)initial; return calloc(1,sizeof(struct heap));
}
static HANDLE WINAPI w_GetProcessHeap(void) {return &process_heap;}
static struct allocation *allocation_for(struct heap *h, void *ptr, const char *operation) {
    struct allocation *a=(void*)((char*)ptr-ALLOCATION_HEADER);
    if (a->magic!=ALLOCATION_MAGIC || a->heap!=h) unsupported(operation);
    return a;
}
static void link_allocation(struct heap *h,struct allocation *a) {
    a->magic=ALLOCATION_MAGIC;a->heap=h;a->prev=NULL;a->next=h->allocations;
    if (a->next) a->next->prev=a;
    h->allocations=a;
}
static void unlink_allocation(struct allocation *a) {
    if (a->prev) a->prev->next=a->next; else a->heap->allocations=a->next;
    if (a->next) a->next->prev=a->prev;
    a->magic=0;
}
static PVOID WINAPI w_HeapAlloc(HANDLE handle,DWORD flags,SIZE_T size) {
    if (flags & ~(1u|8u)) unsupported("HeapAlloc flags");
    if (size>SIZE_MAX-ALLOCATION_HEADER) return NULL;
    struct allocation *a=(flags&8)?calloc(1,ALLOCATION_HEADER+size):malloc(ALLOCATION_HEADER+size);
    if (!a) return NULL;
    a->size=size; link_allocation(handle,a); return (char*)a+ALLOCATION_HEADER;
}
static WIN_BOOL WINAPI w_HeapFree(HANDLE handle,DWORD flags,PVOID ptr) {
    if (flags & ~1u) unsupported("HeapFree flags");
    if (!ptr) return 1;
    struct allocation *a=allocation_for(handle,ptr,"HeapFree foreign allocation");
    unlink_allocation(a); free(a); return 1;
}
static PVOID WINAPI w_HeapReAlloc(HANDLE handle,DWORD flags,PVOID ptr,SIZE_T size) {
    if (flags & ~(1u|8u)) unsupported("HeapReAlloc flags");
    if (!ptr) return NULL;
    if (size>SIZE_MAX-ALLOCATION_HEADER) {last_error=8;return NULL;}
    struct allocation *a=allocation_for(handle,ptr,"HeapReAlloc foreign allocation");
    size_t old=a->size;
    unlink_allocation(a);
    struct allocation *result=realloc(a,ALLOCATION_HEADER+size);
    if (!result) {link_allocation(handle,a);last_error=8;return NULL;}
    result->size=size; link_allocation(handle,result);
    if ((flags&8)&&size>old) memset((char*)result+ALLOCATION_HEADER+old,0,size-old);
    return (char*)result+ALLOCATION_HEADER;
}
static WIN_BOOL WINAPI w_HeapDestroy(HANDLE handle) {
    struct heap *h=handle;
    if (h==&process_heap) {last_error=5;return 0;}
    while (h->allocations) {struct allocation *a=h->allocations;unlink_allocation(a);free(a);}
    free(h);return 1;
}

struct region {void *base;size_t size;struct region *next;};
static void forget_mappings(void);
static struct region *regions;
static size_t page_up(size_t n) {return (n+4095)&~(size_t)4095;}
static int win_protection(DWORD protect) {
    switch(protect) {case 1:return PROT_NONE;case 2:return PROT_READ;
        case 4:return PROT_READ|PROT_WRITE;case 0x20:return PROT_READ|PROT_EXEC;
        case 0x40:return PROT_READ|PROT_WRITE|PROT_EXEC;
        default:unsupported("VirtualAlloc protection");}
}
static PVOID WINAPI w_VirtualAlloc(PVOID address,SIZE_T size,DWORD type,DWORD protect) {
    if (!size || (type & ~0x3000u)) unsupported("VirtualAlloc type/size");
    if (type & 0x2000) {
        if (address) unsupported("VirtualAlloc specified reservation address");
        size=page_up(size);void *base=mmap(NULL,size,(type&0x1000)?win_protection(protect):PROT_NONE,
            MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if (base==MAP_FAILED) {last_error=8;return NULL;}
        struct region *r=malloc(sizeof(*r));
        if (!r) {munmap(base,size);return NULL;}
        *r=(struct region){base,size,regions};regions=r;forget_mappings();return base;
    }
    if (type==0x1000 && address) {
        uintptr_t start=(uintptr_t)address&~4095u;
        size=page_up((uintptr_t)address-start+size);
        for(struct region *r=regions;r;r=r->next)
            if(start>=(uintptr_t)r->base && start+size<=(uintptr_t)r->base+r->size) {
                if(mprotect((void*)start,size,win_protection(protect))) {last_error=487;return NULL;}
                forget_mappings();return (void*)start;
            }
        last_error=487;return NULL;
    }
    if (type==0x1000 && !address) return w_VirtualAlloc(NULL,size,0x3000,protect);
    unsupported("VirtualAlloc request");
}
static WIN_BOOL WINAPI w_VirtualFree(PVOID address,SIZE_T size,DWORD type) {
    struct region **at=&regions;
    if (type==0x8000 && !size) {
        while(*at && (*at)->base!=address) at=&(*at)->next;
        if (!*at) {last_error=487;return 0;}
        struct region *r=*at;*at=r->next;munmap(r->base,r->size);free(r);forget_mappings();return 1;
    }
    if (type==0x4000) {
        for(struct region *r=regions;r;r=r->next) {
            if(address==r->base && !size) size=r->size;
            uintptr_t start=(uintptr_t)address&~4095u;
            size_t length=page_up((uintptr_t)address-start+size);
            if(start>=(uintptr_t)r->base && length && start+length<=(uintptr_t)r->base+r->size) {
                if(madvise((void*)start,length,MADV_DONTNEED)||mprotect((void*)start,length,PROT_NONE)) return 0;
                forget_mappings();return 1;
            }
        }
        last_error=487;return 0;
    }
    unsupported("VirtualFree request");
}

static DWORD WINAPI w_TlsAlloc(void) {
    for(DWORD i=1;i<1024;i++) if(!tls_used[i]) {tls_used[i]=true;LocalStorage[i]=0;return i;}
    last_error=8;return UINT32_MAX;
}
static WIN_BOOL WINAPI w_TlsSetValue(DWORD index,PVOID value) {
    if(index>=1024 || !tls_used[index]) {last_error=87;return 0;}
    LocalStorage[index]=(uintptr_t)value;return 1;
}
static PVOID WINAPI w_TlsGetValue(DWORD index) {
    if(index>=1024 || !tls_used[index]) {last_error=87;return NULL;}
    last_error=0;return (void*)LocalStorage[index];
}
static WIN_BOOL WINAPI w_TlsFree(DWORD index) {
    if(!index || index>=1024 || !tls_used[index]) {last_error=87;return 0;}
    tls_used[index]=false;LocalStorage[index]=0;return 1;
}
static void WINAPI w_InitializeCriticalSection(void *object) {
    pthread_mutex_t *m=malloc(sizeof(*m));
    if(!m) unsupported("InitializeCriticalSection allocation failure");
    pthread_mutexattr_t attr;pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE);
    int error=pthread_mutex_init(m,&attr);pthread_mutexattr_destroy(&attr);
    if(error) unsupported("InitializeCriticalSection pthread failure");
    memset(object,0,24);memcpy(object,&m,sizeof(m));
}
static pthread_mutex_t *critical_mutex(void *object) {pthread_mutex_t *m;memcpy(&m,object,4);return m;}
static void WINAPI w_EnterCriticalSection(void *object) {
    if(pthread_mutex_lock(critical_mutex(object))) unsupported("EnterCriticalSection failure");
}
static void WINAPI w_LeaveCriticalSection(void *object) {
    if(pthread_mutex_unlock(critical_mutex(object))) unsupported("LeaveCriticalSection failure");
}
static void WINAPI w_DeleteCriticalSection(void *object) {
    pthread_mutex_t *m=critical_mutex(object);
    if(pthread_mutex_destroy(m)) unsupported("DeleteCriticalSection failure");
    free(m);memset(object,0,24);
}
static long WINAPI w_InterlockedIncrement(long *p) {return __atomic_add_fetch(p,1,__ATOMIC_SEQ_CST);}
static long WINAPI w_InterlockedDecrement(long *p) {return __atomic_sub_fetch(p,1,__ATOMIC_SEQ_CST);}
static DWORD WINAPI w_GetVersion(void) {return 0x0a280105; /* Windows XP, build 2600 */}
static void *exception_filter;
static void * WINAPI w_SetUnhandledExceptionFilter(void *filter) {
    void *previous=exception_filter;exception_filter=filter;return previous;
}
static WIN_BOOL WINAPI w_GetVersionExA(void *info) {
    if(!info){last_error=87;return 0;}
    uint32_t *fields=info;uint32_t size=fields[0];
    if(size!=148 && size!=156){last_error=87;return 0;}
    memset((char*)info+4,0,size-4);
    fields[1]=5;fields[2]=1;fields[3]=2600;fields[4]=2;
    if(size==156)((unsigned char*)info)[154]=1; /* VER_NT_WORKSTATION */
    return 1;
}
static DWORD WINAPI w_GetLastError(void) {return last_error;}
static void WINAPI w_SetLastError(DWORD value) {last_error=value;}
static DWORD WINAPI w_GetCurrentThreadId(void) {return syscall(SYS_gettid);}
/* Obsolete on Windows NT: the API returns the requested handle count. */
static DWORD WINAPI w_SetHandleCount(DWORD count) {return count;}
static HANDLE WINAPI w_GetCurrentThread(void) {return (void*)-2;}
static HANDLE WINAPI w_GetCurrentProcess(void) {return (void*)-1;}
static char command_line[4096];
static char dll_path[4096];
static char * WINAPI w_GetCommandLineA(void) {return command_line;}
static HANDLE std_handles[3]={(void*)0,(void*)1,(void*)2};
static HANDLE WINAPI w_GetStdHandle(DWORD type) {
    if(type<(DWORD)-12 || type>(DWORD)-10) {last_error=87;return (void*)-1;}
    return std_handles[(DWORD)-10-type];
}
static WIN_BOOL WINAPI w_SetStdHandle(DWORD type,HANDLE handle) {
    if(type<(DWORD)-12 || type>(DWORD)-10) {last_error=87;return 0;}
    std_handles[(DWORD)-10-type]=handle;return 1;
}
static DWORD WINAPI w_GetFileType(HANDLE handle) {
    extern int cb_is_file(HANDLE);if(cb_is_file(handle))return 1;
    int fd=(intptr_t)handle;struct stat st;
    if(fstat(fd,&st)) {last_error=6;return 0;}
    if(S_ISCHR(st.st_mode)) return 2;
    if(S_ISFIFO(st.st_mode)||S_ISSOCK(st.st_mode)) return 3;
    return 1;
}
static void WINAPI w_GetStartupInfoA(void *info) {
    memset(info,0,68);*(uint32_t*)info=68;
    uint32_t *fields=info;fields[11]=0x100;
    fields[14]=(uintptr_t)std_handles[0];fields[15]=(uintptr_t)std_handles[1];fields[16]=(uintptr_t)std_handles[2];
}
extern char **environ;
static char * WINAPI w_GetEnvironmentStrings(void) {
    size_t size=1;for(char **p=environ;*p;p++)size+=strlen(*p)+1;
    char *block=malloc(size+1);if(!block)return NULL;char *out=block;
    for(char **p=environ;*p;p++){size_t n=strlen(*p)+1;memcpy(out,*p,n);out+=n;}
    *out=0;if(out==block)out[1]=0;return block;
}
static WIN_BOOL WINAPI w_FreeEnvironmentStringsA(void *block) {free(block);return 1;}
static uint16_t * WINAPI w_GetEnvironmentStringsW(void) {
    char *block=w_GetEnvironmentStrings();if(!block)return NULL;
    size_t bytes=1;for(char *p=block;*p;p+=strlen(p)+1)bytes+=strlen(p)+1;
    size_t capacity=bytes*4+4,remaining=capacity;
    uint16_t *wide=calloc(1,capacity);if(!wide){free(block);return NULL;}
    char *input=block,*output=(char*)wide;
    iconv_t converter=iconv_open("UTF-16LE","UTF-8");
    if(converter==(iconv_t)-1)unsupported("native UTF-8 environment conversion unavailable");
    size_t result=iconv(converter,&input,&bytes,&output,&remaining);
    iconv_close(converter);free(block);
    if(result==(size_t)-1){free(wide);last_error=1113;return NULL;}
    return wide;
}
static WIN_BOOL WINAPI w_FreeEnvironmentStringsW(void *block) {free(block);return 1;}
static const char *encoding(unsigned cp) {
    switch(cp){case 0:case 1252:return "WINDOWS-1252";case 1:case 437:return "CP437";
        case 65001:return "UTF-8";default:unsupported("unsupported character codepage");}
}
static size_t wide_length(const uint16_t *s) {size_t n=0;while(s[n])n++;return n;}
static int WINAPI w_WideCharToMultiByte(unsigned cp,DWORD flags,const uint16_t *src,int src_count,
    char *dest,int dest_count,const char *default_char,WIN_BOOL *used_default) {
    if(src_count==0||src_count< -1||dest_count<0){last_error=87;return 0;}
    size_t bytes=(src_count==-1?wide_length(src)+1:(size_t)src_count)*2;
    if(flags) {
        if(flags & ~0x220u)unsupported("WideCharToMultiByte flags");
        /* WC_COMPOSITECHECK|WC_SEPCHARS has no effect on ASCII timezone names. */
        for(size_t i=0;i<bytes/2;i++)if(src[i]>127)unsupported("WideCharToMultiByte non-ASCII composition");
    }
    size_t capacity=bytes*4+16,remaining=capacity;
    char *buffer=malloc(capacity),*out=buffer,*input=(char*)src;
    if(!buffer)return 0;
    if(used_default)*used_default=0;
    iconv_t converter=iconv_open(encoding(cp),"UTF-16LE");
    if(converter==(iconv_t)-1)unsupported("character converter unavailable");
    while(bytes) {
        if(iconv(converter,&input,&bytes,&out,&remaining)!=(size_t)-1)break;
        if(errno!=EILSEQ || bytes<2){iconv_close(converter);free(buffer);last_error=1113;return 0;}
        uint16_t ch;memcpy(&ch,input,2);
        size_t skip=(ch>=0xd800&&ch<=0xdbff&&bytes>=4)?4:2;
        input+=skip;bytes-=skip;*out++=default_char?*default_char:'?';remaining--;
        if(used_default)*used_default=1;
    }
    iconv_close(converter);size_t length=out-buffer;
    if(dest_count){if(length>(size_t)dest_count){free(buffer);last_error=122;return 0;}memcpy(dest,buffer,length);}
    free(buffer);return length;
}
static int WINAPI w_MultiByteToWideChar(unsigned cp,DWORD flags,const char *src,int src_count,
    uint16_t *dest,int dest_count) {
    if(flags & ~9u)unsupported("MultiByteToWideChar flags");
    if(src_count==0||src_count< -1||dest_count<0){last_error=87;return 0;}
    size_t bytes=src_count==-1?strlen(src)+1:(size_t)src_count;
    size_t capacity=bytes*4+16,remaining=capacity;
    char *buffer=malloc(capacity),*out=buffer,*input=(char*)src;if(!buffer)return 0;
    iconv_t converter=iconv_open("UTF-16LE",encoding(cp));
    if(converter==(iconv_t)-1)unsupported("character converter unavailable");
    size_t result=iconv(converter,&input,&bytes,&out,&remaining);iconv_close(converter);
    if(result==(size_t)-1){free(buffer);last_error=1113;return 0;}
    size_t length=(out-buffer)/2;
    if(dest_count){if(length>(size_t)dest_count){free(buffer);last_error=122;return 0;}memcpy(dest,buffer,length*2);}
    free(buffer);return length;
}
static WIN_BOOL WINAPI w_GetStringTypeW(DWORD type,const uint16_t *src,int count,uint16_t *types) {
    if(type!=1)unsupported("GetStringTypeW classification other than CT_CTYPE1");
    if(count== -1)count=wide_length(src)+1;
    if(count<0){last_error=87;return 0;}
    static locale_t unicode;
    if(!unicode)unicode=newlocale(LC_CTYPE_MASK,"C.UTF-8",(locale_t)0);
    if(!unicode)unsupported("C.UTF-8 locale unavailable");
    for(int i=0;i<count;i++) {
        wint_t c=src[i];unsigned value=0;
        if(iswupper_l(c,unicode))value|=1;
        if(iswlower_l(c,unicode))value|=2;
        if(iswdigit_l(c,unicode))value|=4;
        if(iswspace_l(c,unicode))value|=8;
        if(iswpunct_l(c,unicode))value|=16;
        if(iswcntrl_l(c,unicode))value|=32;
        if(iswblank_l(c,unicode))value|=64;
        if(iswxdigit_l(c,unicode))value|=128;
        if(iswalpha_l(c,unicode))value|=256;
        if(iswprint_l(c,unicode)||iswcntrl_l(c,unicode))value|=512;
        types[i]=value;
    }
    return 1;
}
static WIN_BOOL WINAPI w_GetStringTypeA(DWORD locale,DWORD type,const char *src,int count,uint16_t *types) {
    if(locale!=0 && locale!=0x409)unsupported("GetStringTypeA locale");
    if(count== -1)count=strlen(src)+1;
    if(count<0){last_error=87;return 0;}
    uint16_t *wide=malloc((size_t)count*2);if(!wide)return 0;
    int converted=w_MultiByteToWideChar(1252,0,src,count,wide,count);
    WIN_BOOL result=converted==count?w_GetStringTypeW(type,wide,count,types):0;
    free(wide);return result;
}
static int WINAPI w_LCMapStringW(DWORD locale,DWORD flags,const uint16_t *src,int count,uint16_t *dest,int dest_count) {
    if(locale!=0 && locale!=0x409 && locale!=0x400 && locale!=0x800)unsupported("LCMapStringW locale");
    if(flags!=0x100 && flags!=0x200)unsupported("LCMapStringW flags other than lower/upper");
    if(count== -1)count=wide_length(src)+1;
    if(count<=0||dest_count<0){last_error=87;return 0;}
    if(!dest_count)return count;
    if(dest_count<count){last_error=122;return 0;}
    locale_t unicode=newlocale(LC_CTYPE_MASK,"C.UTF-8",(locale_t)0);
    if(!unicode)unsupported("case mapping locale unavailable");
    for(int i=0;i<count;i++)dest[i]=flags==0x100?towlower_l(src[i],unicode):towupper_l(src[i],unicode);
    freelocale(unicode);return count;
}
static int WINAPI w_LCMapStringA(DWORD locale,DWORD flags,const char *src,int count,char *dest,int dest_count) {
    if(count== -1)count=strlen(src)+1;
    if(count<=0||dest_count<0){last_error=87;return 0;}
    uint16_t *wide=malloc((size_t)count*2);if(!wide)return 0;
    int result=0;
    if(w_MultiByteToWideChar(1252,0,src,count,wide,count)==count &&
       w_LCMapStringW(locale,flags,wide,count,wide,count)==count)
        result=w_WideCharToMultiByte(1252,0,wide,count,dest,dest_count,NULL,NULL);
    free(wide);return result;
}
void *cb_module_symbol(struct pe_image*,const char*,bool*);
#define KERNEL32_HANDLE ((void*)0x4b45524e)
#define USER32_HANDLE ((void*)0x55534552)
/* Windows matches a module by its base name, case-insensitively, with or
 * without ".dll"; callers pass bare names, relative and absolute paths. */
static bool module_name_is(const char *name,const char *dll) {
    const char *base=name;
    for(const char *p=name;*p;p++)if(*p=='/'||*p=='\\')base=p+1;
    size_t length=strlen(base),dll_length=strlen(dll);
    if(length>=4&&!strcasecmp(base+length-4,".dll"))length-=4;
    if(dll_length>=4&&!strcasecmp(dll+dll_length-4,".dll"))dll_length-=4;
    return length==dll_length&&!strncasecmp(base,dll,length);
}
static HANDLE WINAPI w_GetModuleHandleA(const char *name) {
    if(!name)return image.image?image.image:server_image.image;
    if(image.image&&module_name_is(name,"client.dll"))return image.image;
    if(server_image.image&&module_name_is(name,server_image.name))return server_image.image;
    if(vgui_image.image&&module_name_is(name,"vgui.dll"))return vgui_image.image;
    if(module_name_is(name,"kernel32.dll"))return KERNEL32_HANDLE;
    if(module_name_is(name,"user32.dll"))return USER32_HANDLE;
    if(module_name_is(name,"filesystem_stdio.dll")) {
        void *module=cb_filesystem_module();if(cb_is_filesystem_module(module))return module;
    }
    if(module_name_is(name,"fmod.dll")) {void *module=cb_fmod_module();if(module)return module;}
    last_error=126;return NULL;
}
static HANDLE WINAPI w_LoadLibraryA(const char *name) {
    /* Virtual modules are provided on demand, as LoadLibrary maps a file. */
    HANDLE result=!name?NULL:module_name_is(name,"filesystem_stdio.dll")?cb_filesystem_module():
        module_name_is(name,"fmod.dll")?cb_fmod_module():w_GetModuleHandleA(name);
    fprintf(stderr,"[bridge] LoadLibraryA(%s)%s\n",name?name:"NULL",result?"":": module not provided");
    if(!result)last_error=126;return result;
}
static WIN_BOOL WINAPI w_FreeLibrary(HANDLE handle) {
    if(handle&&(handle==image.image||handle==server_image.image||handle==vgui_image.image||handle==KERNEL32_HANDLE||handle==USER32_HANDLE||
       cb_is_filesystem_module(handle)||cb_is_fmod_module(handle)))return 1;
    last_error=6;return 0;
}
static void * WINAPI w_GetProcAddress(HANDLE module,const char *name) {
    if((uintptr_t)name<65536) unsupported("GetProcAddress ordinal");
    if(module==image.image) return gb_symbol(name);
    if(module==server_image.image)return cb_module_symbol(&server_image,name,NULL);
    if(module==vgui_image.image) {
        extern void *cb_module_symbol(struct pe_image*,const char*,bool*);
        return cb_module_symbol(&vgui_image,name,NULL);
    }
    /* An import that is bound to a trap must not be handed out here: modern
     * CRTs probe optional APIs and fall back when GetProcAddress fails. */
    if(cb_is_filesystem_module(module)) {void *p=cb_filesystem_symbol(name);if(p)return p;last_error=127;return NULL;}
    if(cb_is_fmod_module(module)) {void *p=cb_fmod_symbol(name);if(p)return p;last_error=127;return NULL;}
    if(module==KERNEL32_HANDLE||module==USER32_HANDLE) {void *p=find_implemented(name);if(p)return p;last_error=127;return NULL;}
    last_error=6;return NULL;
}
static DWORD WINAPI w_GetModuleFileNameA(HANDLE module,char *dest,DWORD count) {
    if(!count){last_error=87;return 0;}
    char filename[4096];const char *source;
    if(!module) {
        ssize_t n=readlink("/proc/self/exe",filename,sizeof(filename)-1);
        if(n<0){last_error=2;return 0;}filename[n]=0;source=filename;
    } else if(module==image.image)source=dll_path;
    else if(module==server_image.image)source=server_path;
    else if(module==vgui_image.image)source=vgui_path;
    else{last_error=6;return 0;}
    /* Windows reports '\\' separators; HL25 code splits the game root at the
     * last backslash. File APIs map them back to Linux paths (windows_path). */
    size_t length=strlen(source);
    size_t copied=length>=count?count-1:length;
    for(size_t i=0;i<copied;i++)dest[i]=source[i]=='/'?'\\':source[i];
    dest[copied]=0;
    if(length>=count){last_error=122;return count;}
    return length;
}
static unsigned WINAPI w_GetACP(void) {return 1252;}
struct cpinfo {unsigned maxchar;unsigned char defaultchar[2],leadbytes[12];};
static WIN_BOOL WINAPI w_GetCPInfo(unsigned cp,struct cpinfo *info) {
    if(cp!=0 && cp!=1252) unsupported("GetCPInfo unsupported codepage");
    memset(info,0,sizeof(*info));info->maxchar=1;info->defaultchar[0]='?';return 1;
}
static SIZE_T WINAPI w_HeapSize(HANDLE heap,DWORD flags,void *ptr) {
    if(flags & ~1u)unsupported("HeapSize flags");
    if(!ptr){last_error=87;return (SIZE_T)-1;}
    return allocation_for(heap,ptr,"HeapSize foreign allocation")->size;
}
static DWORD WINAPI w_GetTickCount(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return (DWORD)((uint64_t)t.tv_sec*1000+t.tv_nsec/1000000);
}
static uint16_t WINAPI w_htons(uint16_t value) {return __builtin_bswap16(value);}
struct cb_file {unsigned char *data;size_t size,position;int fd;struct cb_file *next;};
/* fd of a metadata-only handle (FILE_READ_ATTRIBUTES) is O_PATH: fstat works, I/O fails. */
static struct cb_file *files;
static unsigned char *(*asset_load)(char*,int,int*);
static void (*asset_free)(void*);
static char game_directory[256];
void cb_set_file_loader(unsigned char *(*load)(char*,int,int*),void(*release)(void*),const char *directory) {
    asset_load=load;asset_free=release;
    const char *base=directory?strrchr(directory,'/'):NULL;
    snprintf(game_directory,sizeof(game_directory),"%s",base?base+1:directory?directory:"");
}
static struct cb_file *file_for(HANDLE handle) {for(struct cb_file *f=files;f;f=f->next)if(f==handle)return f;return NULL;}
int cb_is_file(HANDLE handle) {return file_for(handle)!=NULL;}
static WIN_BOOL windows_path(const char *name,char path[4096]) {
    size_t length=strlen(name);if(length>=4096){last_error=206;return 0;}
    for(size_t i=0;i<=length;i++)path[i]=name[i]=='\\'?'/':name[i];
    size_t prefix=strlen(game_directory);
    if(prefix&&!strncasecmp(path,game_directory,prefix)&&path[prefix]=='/') {
        memcpy(path,game_directory,prefix);
        for(size_t i=prefix+1;i<length;i++)if(path[i]>='A'&&path[i]<='Z')path[i]+='a'-'A';
    }
    return 1;
}
static WIN_BOOL WINAPI w_CreateDirectoryA(const char *name,void *security) {
    (void)security;char path[4096];if(!windows_path(name,path))return 0;
    fprintf(stderr,"[bridge] CreateDirectoryA(%s)\n",path);
    if(!mkdir(path,0755))return 1;
    last_error=errno==EEXIST?183:errno==ENOENT?3:5;return 0;
}
static HANDLE WINAPI w_CreateFileA(const char *name,DWORD access,DWORD share,void *security,DWORD creation,DWORD flags,HANDLE template) {
    (void)share;(void)security;(void)template;
    fprintf(stderr,"[bridge] CreateFileA(%s), access=%#x creation=%u\n",name,access,creation);
    /* Rights that only describe a handle; the CRT's stat() asks for FILE_READ_ATTRIBUTES. */
    const DWORD metadata_rights=0x80u|0x8u|0x20000u|0x100000u; /* READ_ATTRIBUTES, READ_EA, READ_CONTROL, SYNCHRONIZE */
    DWORD data_access=access&0xc0000000u;
    if((access&~(0xc0000000u|metadata_rights))||creation<1||creation>5||flags&0x40000000u)unsupported("CreateFileA access/creation/overlapped request");
    if(!data_access) {
        if(creation!=3)unsupported("CreateFileA metadata handle with creation other than OPEN_EXISTING");
        char path[4096];if(!windows_path(name,path))return (void*)-1;
        int fd=open(path,O_PATH|O_CLOEXEC);struct stat st;
        if(fd<0){last_error=errno==ENOTDIR?3:2;return (void*)-1;}
        if(fstat(fd,&st)){close(fd);last_error=5;return (void*)-1;}
        /* Windows opens directories only with FILE_FLAG_BACKUP_SEMANTICS. */
        if(S_ISDIR(st.st_mode)&&!(flags&0x02000000u)){close(fd);last_error=5;return (void*)-1;}
        struct cb_file *f=calloc(1,sizeof(*f));if(!f){close(fd);last_error=8;return (void*)-1;}
        f->fd=fd;f->size=st.st_size;f->next=files;files=f;return f;
    }
    access=data_access;
    struct cb_file *f=calloc(1,sizeof(*f));if(!f){last_error=8;return (void*)-1;}f->fd=-1;
    char path[4096];if(!windows_path(name,path)){free(f);return (void*)-1;}
    int mode=access==0x80000000u?O_RDONLY:access==0x40000000u?O_WRONLY:O_RDWR;
    if(creation==1)mode|=O_CREAT|O_EXCL;
    if(creation==2)mode|=O_CREAT|O_TRUNC;
    if(creation==4)mode|=O_CREAT;
    if(creation==5)mode|=O_TRUNC;
    f->fd=open(path,mode,0644);
    if(f->fd>=0) {struct stat st;if(fstat(f->fd,&st)){close(f->fd);free(f);last_error=2;return (void*)-1;}f->size=st.st_size;}
    else if(asset_load&&access==0x80000000u&&creation==3) {
        int size=0;char *relative=path;size_t prefix=strlen(game_directory);
        if(prefix&&!strncasecmp(path,game_directory,prefix)&&path[prefix]=='/')relative=path+prefix+1;
        f->data=asset_load(relative,5,&size);if(f->data)f->size=size;
        if(!f->data) {
            // The mod's loose resources were normalized to lower case on Linux.
            char folded[4096];size_t n=strlen(relative);
            for(size_t i=0;i<=n;i++)folded[i]=relative[i]>='A'&&relative[i]<='Z'?relative[i]+('a'-'A'):relative[i];
            f->data=asset_load(folded,5,&size);if(f->data)f->size=size;
        }
    }
    if(f->fd<0&&!f->data){free(f);last_error=2;return (void*)-1;}
    f->next=files;files=f;return f;
}
static WIN_BOOL WINAPI w_ReadFile(HANDLE handle,void *out,DWORD requested,DWORD *received,void *overlapped) {
    if(overlapped)unsupported("ReadFile overlapped");if(received)*received=0;
    struct cb_file *f=file_for(handle);
    if(f&&f->data){size_t count=f->position<f->size?f->size-f->position:0;if(count>requested)count=requested;
        if(count)memcpy(out,f->data+f->position,count);f->position+=count;if(received)*received=count;return 1;}
    int fd=f?f->fd:(int)(intptr_t)handle;if(!f&&(fd<0||fd>2)){last_error=6;return 0;}
    ssize_t count;do{count=read(fd,out,requested);}while(count<0&&errno==EINTR);
    if(count<0){last_error=5;return 0;}if(received)*received=count;if(f)f->position+=count;return 1;
}
static WIN_BOOL WINAPI w_WriteFile(HANDLE handle,const void *data,DWORD requested,DWORD *written,void *overlapped) {
    if(overlapped)unsupported("WriteFile overlapped");if(written)*written=0;
    struct cb_file *f=file_for(handle);
    int fd=f?f->fd:(int)(intptr_t)handle;if(fd<0||(!f&&fd>2)){last_error=5;return 0;}
    ssize_t count;do{count=write(fd,data,requested);}while(count<0&&errno==EINTR);
    if(count<0){last_error=5;return 0;}if(written)*written=count;
    if(f){f->position+=count;if(f->position>f->size)f->size=f->position;}return 1;
}
static DWORD WINAPI w_SetFilePointer(HANDLE handle,int32_t low,int32_t *high,DWORD method) {
    struct cb_file *f=file_for(handle);if(!f||method>2){last_error=87;return UINT32_MAX;}
    int64_t distance=high?((int64_t)*high<<32)|(uint32_t)low:low;
    int64_t start=method==0?0:method==1?f->position:f->size;int64_t position=start+distance;
    if(position<0){last_error=131;return UINT32_MAX;}
    if(f->fd>=0&&lseek(f->fd,position,SEEK_SET)<0){last_error=5;return UINT32_MAX;}
    f->position=position;if(high)*high=position>>32;last_error=0;return position;
}
static WIN_BOOL WINAPI w_CloseHandle(HANDLE handle) {
    struct cb_file **at=&files;while(*at&&*at!=handle)at=&(*at)->next;
    if(!*at){last_error=6;return 0;}struct cb_file *f=*at;*at=f->next;
    if(f->fd>=0)close(f->fd);if(f->data)asset_free(f->data);free(f);return 1;
}
static WIN_BOOL WINAPI w_FlushFileBuffers(HANDLE handle) {
    struct cb_file *f=file_for(handle);if(f)return f->fd<0?1:fsync(f->fd)==0;
    int fd=(int)(intptr_t)handle;if(fd<0||fd>2){last_error=6;return 0;}
    /* Unbuffered writes to terminal/pipe handles require no disk flush. */
    struct stat st;if(fstat(fd,&st)){last_error=6;return 0;}if(!S_ISREG(st.st_mode))return 1;return fsync(fd)==0;
}
static void system_time(uint16_t *out,bool local) {
    struct timespec ts;clock_gettime(CLOCK_REALTIME,&ts);struct tm tm;
    if(local)localtime_r(&ts.tv_sec,&tm);else gmtime_r(&ts.tv_sec,&tm);
    out[0]=tm.tm_year+1900;out[1]=tm.tm_mon+1;out[2]=tm.tm_wday;out[3]=tm.tm_mday;
    out[4]=tm.tm_hour;out[5]=tm.tm_min;out[6]=tm.tm_sec;out[7]=ts.tv_nsec/1000000;
}
static void WINAPI w_GetLocalTime(uint16_t *out) {system_time(out,true);}
static void WINAPI w_GetSystemTime(uint16_t *out) {system_time(out,false);}
static DWORD WINAPI w_GetTimeZoneInformation(void *info) {
    tzset();time_t now=time(NULL);struct tm local;localtime_r(&now,&local);
    memset(info,0,172);*(int32_t*)info=timezone/60;
    uint16_t *standard=(void*)((char*)info+4),*summer=(void*)((char*)info+88);
    for(size_t i=0;i<31&&tzname[0][i];i++)standard[i]=(unsigned char)tzname[0][i];
    for(size_t i=0;i<31&&tzname[1][i];i++)summer[i]=(unsigned char)tzname[1][i];
    *(int32_t*)((char*)info+168)=-60;
    return local.tm_isdst>0?2:1;
}
/* The old client temporarily changes Windows pointer acceleration. Keep that
 * emulated state private; the Linux desktop's acceleration is not modified. */
static int mouse_parameters[3]={0,0,1};
static void (*mouse_get)(int*,int*),(*mouse_set)(int,int);
void cb_set_mouse(void(*get)(int*,int*),void(*set)(int,int)) {mouse_get=get;mouse_set=set;}
static WIN_BOOL WINAPI w_GetCursorPos(int *position) {
    if(!mouse_get)unsupported("GetCursorPos input adapter missing");mouse_get(&position[0],&position[1]);return 1;
}
static WIN_BOOL WINAPI w_SetCursorPos(int x,int y) {
    if(!mouse_set)unsupported("SetCursorPos input adapter missing");mouse_set(x,y);return 1;
}
static unsigned WINAPI w_joyGetNumDevs(void) {
    fprintf(stderr,"[client] WinMM joystick devices: 0 (controller adapter pending)\n");return 0;
}
static WIN_BOOL WINAPI w_SystemParametersInfoA(unsigned action,unsigned param,void *value,unsigned flags) {
    (void)param;(void)flags;
    if(!value){last_error=87;return 0;}
    if(action==3){memcpy(value,mouse_parameters,sizeof(mouse_parameters));return 1;}
    if(action==4){memcpy(mouse_parameters,value,sizeof(mouse_parameters));return 1;}
    unsupported("SystemParametersInfoA action");
}

/* Startup and support APIs of the Visual C++ 2015+ runtime (UCRT, vcruntime).
 * Thread pools, timer queues, events and new threads stay named traps: this
 * runtime executes PE code on the engine's main thread only. */
static void WINAPI w_GetSystemTimeAsFileTime(uint64_t *out) {
    struct timespec ts;clock_gettime(CLOCK_REALTIME,&ts);
    uint64_t value=((uint64_t)ts.tv_sec+11644473600ull)*10000000ull+ts.tv_nsec/100;
    memcpy(out,&value,sizeof(value));
}
/* Same 100 ns unit as GetSystemTimeAsFileTime; Windows reports 10 MHz too. */
static WIN_BOOL WINAPI w_QueryPerformanceCounter(int64_t *out) {
    struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);
    int64_t value=(int64_t)ts.tv_sec*10000000+ts.tv_nsec/100;memcpy(out,&value,sizeof(value));return 1;
}
static WIN_BOOL WINAPI w_QueryPerformanceFrequency(int64_t *out) {int64_t value=10000000;memcpy(out,&value,sizeof(value));return 1;}
static DWORD WINAPI w_GetCurrentProcessId(void) {return getpid();}
static WIN_BOOL WINAPI w_IsDebuggerPresent(void) {return 0;}
static WIN_BOOL WINAPI w_IsProcessorFeaturePresent(DWORD feature) {
    unsigned a,b,c,d;if(!__get_cpuid(1,&a,&b,&c,&d))return 0;
    switch(feature) {
    case 2:return (d>>8)&1;   /* PF_COMPARE_EXCHANGE_DOUBLE */
    case 3:return (d>>23)&1;  /* PF_MMX_INSTRUCTIONS_AVAILABLE */
    case 6:return (d>>25)&1;  /* PF_XMMI_INSTRUCTIONS_AVAILABLE */
    case 8:return (d>>4)&1;   /* PF_RDTSC_INSTRUCTION_AVAILABLE */
    case 10:return (d>>26)&1; /* PF_XMMI64_INSTRUCTIONS_AVAILABLE */
    case 12:return 1;         /* PF_NX_ENABLED */
    case 13:return c&1;       /* PF_SSE3_INSTRUCTIONS_AVAILABLE */
    /* PF_FASTFAIL_AVAILABLE (23) stays false: Linux has no int 0x29 handler,
     * so the CRT takes its UnhandledExceptionFilter path, which reports. */
    default:return 0;
    }
}
/* Pointer encoding only obfuscates; identity is a valid encoding. */
static void *WINAPI w_EncodePointer(void *p) {return p;}
static void *WINAPI w_DecodePointer(void *p) {return p;}
/* x86 SLIST_HEADER: first entry, 16-bit depth, 16-bit sequence in one
 * 8-byte word updated by cmpxchg8b. */
static uint64_t slist_word(void *first,uint64_t old,int delta) {
    return (uint32_t)(uintptr_t)first|((uint64_t)(uint16_t)((old>>32)+delta)<<32)|((uint64_t)(uint16_t)((old>>48)+1)<<48);
}
static void WINAPI w_InitializeSListHead(uint64_t *head) {__atomic_store_n(head,0,__ATOMIC_SEQ_CST);}
static void *WINAPI w_InterlockedPushEntrySList(uint64_t *head,void **entry) {
    uint64_t old=__atomic_load_n(head,__ATOMIC_SEQ_CST);
    do *entry=(void*)(uintptr_t)(uint32_t)old;
    while(!__atomic_compare_exchange_n(head,&old,slist_word(entry,old,1),false,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST));
    return (void*)(uintptr_t)(uint32_t)old;
}
static void *WINAPI w_InterlockedPopEntrySList(uint64_t *head) {
    uint64_t old=__atomic_load_n(head,__ATOMIC_SEQ_CST);void **first;
    do {first=(void**)(uintptr_t)(uint32_t)old;if(!first)return NULL;}
    while(!__atomic_compare_exchange_n(head,&old,slist_word(*first,old,-1),false,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST));
    return first;
}
static void *WINAPI w_InterlockedFlushSList(uint64_t *head) {
    uint64_t old=__atomic_load_n(head,__ATOMIC_SEQ_CST);
    /* Empty list, depth 0, next sequence number. */
    while(!__atomic_compare_exchange_n(head,&old,(uint64_t)(uint16_t)((old>>48)+1)<<48,
        false,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST));
    return (void*)(uintptr_t)(uint32_t)old;
}
static uint16_t WINAPI w_QueryDepthSList(uint64_t *head) {return __atomic_load_n(head,__ATOMIC_SEQ_CST)>>32;}
static WIN_BOOL WINAPI w_InitializeCriticalSectionAndSpinCount(void *object,DWORD spin) {
    (void)spin;w_InitializeCriticalSection(object);return 1;
}
static WIN_BOOL WINAPI w_TryEnterCriticalSection(void *object) {return pthread_mutex_trylock(critical_mutex(object))==0;}
/* ConcRT seeds a cookie from these values during static initialization. */
static WIN_BOOL WINAPI w_GetThreadTimes(HANDLE thread,uint64_t *creation,uint64_t *exit_time,uint64_t *kernel,uint64_t *user) {
    if(thread!=(void*)-2){last_error=6;return 0;}
    static uint64_t started;
    if(!started)w_GetSystemTimeAsFileTime(&started);
    struct rusage usage;if(getrusage(RUSAGE_THREAD,&usage)){last_error=5;return 0;}
    uint64_t values[4]={started,0,
        (uint64_t)usage.ru_stime.tv_sec*10000000+usage.ru_stime.tv_usec*10,
        (uint64_t)usage.ru_utime.tv_sec*10000000+usage.ru_utime.tv_usec*10};
    memcpy(creation,&values[0],8);memcpy(exit_time,&values[1],8);memcpy(kernel,&values[2],8);memcpy(user,&values[3],8);
    return 1;
}
static void WINAPI w_Sleep(DWORD milliseconds) {
    struct timespec t={milliseconds/1000,(long)(milliseconds%1000)*1000000};
    while(nanosleep(&t,&t)&&errno==EINTR){}
}
static WIN_BOOL WINAPI w_SwitchToThread(void) {sched_yield();return 0;}

/* UTF-16 <-> UTF-8 for the W variants of existing narrow implementations. */
static bool narrow(const uint16_t *wide,char *out,size_t size) {
    if(!wide)return false;
    int n=w_WideCharToMultiByte(65001,0,wide,-1,out,(int)size,NULL,NULL);
    return n>0;
}
static int widen(const char *text,uint16_t *out,int count) {return w_MultiByteToWideChar(65001,0,text,-1,out,count);}
static void WINAPI w_GetStartupInfoW(void *info) {w_GetStartupInfoA(info); /* same layout; no strings set */}
static uint16_t *WINAPI w_GetCommandLineW(void) {
    static uint16_t wide[4096];
    if(!widen(command_line,wide,4096))wide[0]=0;
    return wide;
}
static DWORD WINAPI w_GetModuleFileNameW(HANDLE module,uint16_t *dest,DWORD count) {
    if(!count){last_error=87;return 0;}
    char path[4096];DWORD n=w_GetModuleFileNameA(module,path,sizeof(path));
    if(!n)return 0;
    uint16_t wide[4096];int length=widen(path,wide,4096);
    if(length<=0){last_error=1113;return 0;}
    length--; /* without the terminator */
    if((DWORD)length>=count){memcpy(dest,wide,(count-1)*2);dest[count-1]=0;last_error=122;return count;}
    memcpy(dest,wide,(length+1)*2);return length;
}
static HANDLE WINAPI w_GetModuleHandleW(const uint16_t *name) {
    if(!name)return w_GetModuleHandleA(NULL);
    char path[4096];if(!narrow(name,path,sizeof(path))){last_error=126;return NULL;}
    return w_GetModuleHandleA(path);
}
static HANDLE module_containing(const void *address) {
    const struct pe_image *modules[]={&image,&server_image,&vgui_image,&host_image};
    for(size_t i=0;i<sizeof(modules)/sizeof(*modules);i++)
        if(modules[i]->image&&(uintptr_t)address>=(uintptr_t)modules[i]->image&&
           (uintptr_t)address<(uintptr_t)modules[i]->image+modules[i]->size)return modules[i]->image;
    return NULL;
}
static WIN_BOOL WINAPI w_GetModuleHandleExW(DWORD flags,const uint16_t *name,HANDLE *out) {
    if(!out||(flags&~7u)){last_error=87;return 0;}
    /* PIN and UNCHANGED_REFCOUNT have no effect: modules here are never refcounted. */
    *out=(flags&4)?module_containing(name):w_GetModuleHandleW(name);
    if(!*out){last_error=126;return 0;}
    return 1;
}
static HANDLE WINAPI w_LoadLibraryExW(const uint16_t *name,HANDLE file,DWORD flags) {
    if(file){last_error=87;return NULL;}
    (void)flags;char path[4096];if(!narrow(name,path,sizeof(path))){last_error=126;return NULL;}
    /* API sets are absent; the UCRT then uses its kernel32 and legacy fallbacks. */
    if(!strncasecmp(path,"api-ms-",7)||!strncasecmp(path,"ext-ms-",7)){last_error=126;return NULL;}
    return w_LoadLibraryA(path);
}
static HANDLE WINAPI w_LoadLibraryW(const uint16_t *name) {return w_LoadLibraryExW(name,NULL,0);}
static WIN_BOOL WINAPI w_GetVersionExW(void *info) {
    if(!info){last_error=87;return 0;}
    uint32_t *fields=info;uint32_t size=fields[0];
    if(size!=276 && size!=284){last_error=87;return 0;}
    memset((char*)info+4,0,size-4);
    fields[1]=5;fields[2]=1;fields[3]=2600;fields[4]=2; /* as GetVersionExA */
    if(size==284)((unsigned char*)info)[282]=1; /* VER_NT_WORKSTATION */
    return 1;
}
static WIN_BOOL WINAPI w_IsValidCodePage(unsigned cp) {return cp==1252||cp==437||cp==65001;}
static unsigned WINAPI w_GetOEMCP(void) {return 437;}
static unsigned WINAPI w_GetConsoleCP(void) {return 65001;}
/* Standard handles are pipes or terminals, never Win32 consoles. */
static WIN_BOOL WINAPI w_GetConsoleMode(HANDLE handle,DWORD *mode) {(void)handle;(void)mode;last_error=6;return 0;}

/* W file variants: the narrow CreateFileA implements engine-asset fallback. */
static HANDLE WINAPI w_CreateFileW(const uint16_t *name,DWORD access,DWORD share,void *security,DWORD creation,DWORD flags,HANDLE template) {
    char path[4096];if(!narrow(name,path,sizeof(path))){last_error=123;return (void*)-1;}
    return w_CreateFileA(path,access,share,security,creation,flags,template);
}
static WIN_BOOL WINAPI w_GetFileSizeEx(HANDLE handle,int64_t *size) {
    struct cb_file *f=file_for(handle);if(!f){last_error=6;return 0;}
    int64_t value=f->size;memcpy(size,&value,sizeof(value));return 1;
}
static WIN_BOOL WINAPI w_SetFilePointerEx(HANDLE handle,int64_t distance,int64_t *position,DWORD method) {
    int32_t high=distance>>32;
    DWORD low=w_SetFilePointer(handle,(int32_t)distance,&high,method);
    if(low==UINT32_MAX&&last_error)return 0;
    if(position){int64_t value=((int64_t)high<<32)|low;memcpy(position,&value,sizeof(value));}
    return 1;
}
static WIN_BOOL WINAPI w_SetEndOfFile(HANDLE handle) {
    struct cb_file *f=file_for(handle);
    if(!f||f->fd<0){last_error=5;return 0;}
    if(ftruncate(f->fd,f->position)){last_error=5;return 0;}
    f->size=f->position;return 1;
}

/* FILETIME/SYSTEMTIME conversion with 64-bit day arithmetic: Scout's 32-bit
 * time_t covers only 1901-2038, FILETIME 1601-30828. */
static int64_t days_from_civil(int64_t y,unsigned m,unsigned d) {
    y-=m<=2;int64_t era=(y>=0?y:y-399)/400;unsigned yoe=(unsigned)(y-era*400);
    unsigned doy=(153*(m+(m>2?-3:9))+2)/5+d-1,doe=yoe*365+yoe/4-yoe/100+doy;
    return era*146097+(int64_t)doe-719468;
}
static void civil_from_days(int64_t z,int64_t *y,unsigned *m,unsigned *d) {
    z+=719468;int64_t era=(z>=0?z:z-146096)/146097;unsigned doe=(unsigned)(z-era*146097);
    unsigned yoe=(doe-doe/1460+doe/36524-doe/146096)/365;unsigned doy=doe-(365*yoe+yoe/4-yoe/100);
    unsigned mp=(5*doy+2)/153;*d=doy-(153*mp+2)/5+1;*m=mp<10?mp+3:mp-9;*y=(int64_t)yoe+era*400+(*m<=2);
}
#define UNIX_EPOCH_DAYS_FROM_1601 134774
static void system_time_from_100ns(uint64_t value,uint16_t *out) {
    int64_t days=value/864000000000ull;uint64_t rest=value%864000000000ull;
    int64_t year;unsigned month,day;civil_from_days(days-UNIX_EPOCH_DAYS_FROM_1601,&year,&month,&day);
    out[0]=year;out[1]=month;out[2]=(days+1)%7; /* 1601-01-01 was a Monday */
    out[3]=day;out[4]=rest/36000000000ull;out[5]=rest/600000000%60;out[6]=rest/10000000%60;out[7]=rest/10000%1000;
}
static bool system_time_valid(const uint16_t *t) {
    static const unsigned char month_days[]={31,29,31,30,31,30,31,31,30,31,30,31};
    return t[0]>=1601&&t[0]<=30827&&t[1]>=1&&t[1]<=12&&t[3]>=1&&t[3]<=month_days[t[1]-1]&&t[4]<24&&t[5]<60&&t[6]<60&&t[7]<1000;
}
static uint64_t system_time_to_100ns(const uint16_t *t) {
    int64_t days=days_from_civil(t[0],t[1],t[3])+UNIX_EPOCH_DAYS_FROM_1601;
    return (((uint64_t)days*24+t[4])*60+t[5])*600000000ull+(uint64_t)t[6]*10000000+(uint64_t)t[7]*10000;
}
static uint64_t filetime_from_timespec(struct timespec ts) {
    return ((uint64_t)((int64_t)ts.tv_sec+11644473600ll))*10000000ull+ts.tv_nsec/100;
}
static WIN_BOOL WINAPI w_FileTimeToSystemTime(const uint64_t *time,uint16_t *out) {
    uint64_t value;memcpy(&value,time,sizeof(value));
    if(value>=0x8000000000000000ull){last_error=87;return 0;}
    system_time_from_100ns(value,out);return 1;
}
static WIN_BOOL WINAPI w_SystemTimeToTzSpecificLocalTime(const void *zone,const uint16_t *utc,uint16_t *local) {
    if(zone)unsupported("SystemTimeToTzSpecificLocalTime with an explicit time zone");
    if(!system_time_valid(utc)){last_error=87;return 0;}
    uint64_t value=system_time_to_100ns(utc);
    /* The offset of that instant; outside time_t's range, the current offset. */
    int64_t seconds=(int64_t)(value/10000000)-11644473600ll;
    time_t instant=(time_t)seconds;if((int64_t)instant!=seconds)instant=time(NULL);
    struct tm tm;if(!localtime_r(&instant,&tm)){last_error=87;return 0;}
    system_time_from_100ns(value+(int64_t)tm.tm_gmtoff*10000000,local);return 1;
}
static WIN_BOOL WINAPI w_GetFileInformationByHandle(HANDLE handle,uint32_t *info) {
    struct cb_file *f=file_for(handle);if(!f){last_error=6;return 0;}
    memset(info,0,52);
    uint64_t size=f->size,created=0,accessed=0,written=0;uint32_t attributes=0x80; /* NORMAL */
    if(f->fd>=0) {
        struct stat st;if(fstat(f->fd,&st)){last_error=5;return 0;}
        size=S_ISDIR(st.st_mode)?0:(uint64_t)st.st_size;
        attributes=0;
        if(S_ISDIR(st.st_mode))attributes|=0x10;
        if(!(st.st_mode&S_IWUSR))attributes|=0x1;
        if(!attributes)attributes=0x80;
        /* Linux keeps no birth time in struct stat; the status change time is closest. */
        created=filetime_from_timespec(st.st_ctim);accessed=filetime_from_timespec(st.st_atim);written=filetime_from_timespec(st.st_mtim);
        info[7]=(uint32_t)st.st_dev;info[10]=st.st_nlink;info[11]=(uint32_t)((uint64_t)st.st_ino>>32);info[12]=(uint32_t)st.st_ino;
    } else info[10]=1; /* engine/PAK asset: size only */
    info[0]=attributes;
    memcpy(&info[1],&created,8);memcpy(&info[3],&accessed,8);memcpy(&info[5],&written,8);
    info[8]=(uint32_t)(size>>32);info[9]=(uint32_t)size;
    return 1;
}
static DWORD WINAPI w_GetCurrentDirectoryW(DWORD count,uint16_t *out) {
    char path[4096];if(!getcwd(path,sizeof(path))){last_error=5;return 0;}
    for(char *p=path;*p;p++)if(*p=='/')*p='\\';
    uint16_t wide[4096];int length=widen(path,wide,4096);
    if(length<=0){last_error=1113;return 0;}
    if((DWORD)length>count)return length; /* required size, terminator included */
    memcpy(out,wide,length*2);return length-1;
}

/* Windows full path of a Linux process: drive-less, '\\'-separated (see
 * GetModuleFileNameA). Relative names are resolved against the current
 * directory; "." and ".." are collapsed as GetFullPathName does. */
static bool full_path(const char *name,char *out,size_t size) {
    char joined[8192];
    bool rooted=name[0]=='\\'||name[0]=='/'||(name[0]&&name[1]==':');
    if(rooted)snprintf(joined,sizeof(joined),"%s",name);
    else {
        char cwd[4096];if(!getcwd(cwd,sizeof(cwd)))return false;
        snprintf(joined,sizeof(joined),"%s/%s",cwd,name);
    }
    size_t prefix=0,length=0;
    if(joined[0]&&joined[1]==':'){out[0]=joined[0];out[1]=':';prefix=length=2;}
    if(length+1>=size)return false;
    out[length++]='\\';
    size_t root=length;
    for(const char *p=joined+prefix;*p;) {
        while(*p=='\\'||*p=='/')p++;
        const char *end=p;while(*end&&*end!='\\'&&*end!='/')end++;
        size_t part=end-p;
        if(part==1&&p[0]=='.'){}
        else if(part==2&&p[0]=='.'&&p[1]=='.') {
            if(length>root){length--;while(length>root&&out[length-1]!='\\')length--;}
        } else if(part) {
            if(length+part+1>=size)return false;
            memcpy(out+length,p,part);length+=part;out[length++]='\\';
        }
        p=end;
    }
    /* Keep a trailing separator only where the name had one. */
    size_t n=strlen(name);bool trailing=n&&(name[n-1]=='\\'||name[n-1]=='/');
    if(length>root&&!trailing)length--;
    out[length]=0;return true;
}
static DWORD WINAPI w_GetFullPathNameW(const uint16_t *name,DWORD count,uint16_t *out,uint16_t **file_part) {
    char path[4096],full[4096];
    if(!name||!name[0]){last_error=123;return 0;}
    if(!narrow(name,path,sizeof(path))||!full_path(path,full,sizeof(full))){last_error=206;return 0;}
    uint16_t wide[4096];int length=widen(full,wide,4096);
    if(length<=0){last_error=1113;return 0;}
    if((DWORD)length>count)return length; /* required size, terminator included */
    memcpy(out,wide,length*2);
    if(file_part) {
        uint16_t *last=NULL;for(uint16_t *p=out;*p;p++)if(*p=='\\')last=p;
        *file_part=last&&last[1]?last+1:NULL;
    }
    return length-1;
}
static unsigned WINAPI w_GetDriveTypeW(const uint16_t *root) {
    char path[4096],linux_path[4096];
    if(!root){if(!getcwd(linux_path,sizeof(linux_path)))return 1;}
    else if(!narrow(root,path,sizeof(path))||!windows_path(path,linux_path))return 1;
    struct stat st;
    return !stat(linux_path,&st)&&S_ISDIR(st.st_mode)?3:1; /* DRIVE_FIXED : DRIVE_NO_ROOT_DIR */
}

/* Process termination requested by PE code is reported, then honored. */
__attribute__((noreturn)) static void pe_exit(const char *how,unsigned code) {
    fprintf(stderr,"[bridge] PE %s(%u)\n",how,code);gb_report();fflush(NULL);_exit(code);
}
__attribute__((noreturn)) static void WINAPI w_ExitProcess(unsigned code) {pe_exit("ExitProcess",code);}
static WIN_BOOL WINAPI w_TerminateProcess(HANDLE process,unsigned code) {
    if(process!=(void*)-1){last_error=5;return 0;}
    pe_exit("TerminateProcess",code);
}
/* The CRT calls this for /GS failures and fast-fail without a debugger. */
static long WINAPI w_UnhandledExceptionFilter(void **pointers) {
    uint32_t *record=pointers?pointers[0]:NULL;
    fprintf(stderr,"[bridge] PE unhandled exception code=%#x address=%p%s\n",record?record[0]:0,
        record?(void*)(uintptr_t)record[3]:NULL,record&&record[0]==0xc0000409u?" (security check / fast fail)":"");
    gb_report();fflush(NULL);abort();
}
static int WINAPI w_MessageBoxA(void *window,const char *text,const char *caption,unsigned type) {
    (void)window;(void)type;
    fprintf(stderr,"[bridge] MessageBoxA %s: %s\n",caption?caption:"",text?text:"");
    return 1; /* IDOK */
}
static void *WINAPI w_GetActiveWindow(void) {return NULL;}

/* Visual C++ 2003-era CRT support (Decay). Its debug CRT validates every heap
 * pointer with IsBadReadPtr/IsBadWritePtr and HeapValidate, so the address
 * space is answered from a cached copy of /proc/self/maps: a miss re-reads it
 * once, and the runtime's own protection changes drop the cache. */
struct mapping {uintptr_t start,end;int prot;};
static struct mapping *mappings;
static size_t mapping_count,mapping_capacity;
static bool mappings_valid;
static void forget_mappings(void) {mappings_valid=false;}
static void read_mappings(void) {
    mapping_count=0;mappings_valid=true;
    int fd=open("/proc/self/maps",O_RDONLY|O_CLOEXEC);if(fd<0)return;
    char buffer[8192];size_t used=0;ssize_t n;
    for(;;) {
        do{n=read(fd,buffer+used,sizeof(buffer)-1-used);}while(n<0&&errno==EINTR);
        if(n<=0&&!used)break;
        used+=n>0?(size_t)n:0;buffer[used]=0;
        char *line=buffer,*end;
        while((end=memchr(line,'\n',used-(line-buffer)))||(n<=0&&*line)) {
            if(end)*end=0;
            unsigned long start,stop;char perms[5]={0};
            if(sscanf(line,"%lx-%lx %4s",&start,&stop,perms)==3) {
                if(mapping_count==mapping_capacity) {
                    size_t capacity=mapping_capacity?mapping_capacity*2:1024;
                    struct mapping *grown=realloc(mappings,capacity*sizeof(*grown));
                    if(!grown)break;mappings=grown;mapping_capacity=capacity;
                }
                mappings[mapping_count++]=(struct mapping){start,stop,
                    (perms[0]=='r'?PROT_READ:0)|(perms[1]=='w'?PROT_WRITE:0)|(perms[2]=='x'?PROT_EXEC:0)};
            }
            if(!end){line+=strlen(line);break;}
            line=end+1;
        }
        size_t rest=used-(line-buffer);memmove(buffer,line,rest);used=rest;
        if(n<=0)break;
    }
    close(fd);
}
static const struct mapping *mapping_at(uintptr_t address) {
    size_t low=0,high=mapping_count;
    while(low<high){size_t mid=(low+high)/2;
        if(address<mappings[mid].start)high=mid;else if(address>=mappings[mid].end)low=mid+1;else return &mappings[mid];}
    return NULL;
}
static bool range_has(uintptr_t address,size_t size,int prot) {
    uintptr_t last=size>UINTPTR_MAX-address?UINTPTR_MAX:address+size-1;
    while(address<=last) {
        const struct mapping *m=mapping_at(address);
        if(!m||(m->prot&prot)!=prot)return false;
        if(m->end-1>=last)return true;
        address=m->end;
    }
    return true;
}
static WIN_BOOL memory_bad(const void *address,size_t size,int prot) {
    if(!size)return 0;
    if(!address)return 1;
    if(mappings_valid&&range_has((uintptr_t)address,size,prot))return 0;
    read_mappings();return !range_has((uintptr_t)address,size,prot);
}
static WIN_BOOL WINAPI w_IsBadReadPtr(const void *address,SIZE_T size) {return memory_bad(address,size,PROT_READ);}
static WIN_BOOL WINAPI w_IsBadWritePtr(void *address,SIZE_T size) {return memory_bad(address,size,PROT_WRITE);}
static WIN_BOOL WINAPI w_IsBadCodePtr(const void *address) {return memory_bad(address,1,PROT_EXEC);}
static WIN_BOOL WINAPI w_HeapValidate(HANDLE handle,DWORD flags,const void *ptr) {
    if(flags & ~1u)unsupported("HeapValidate flags");
    if(!ptr)return 1; /* whole heap: blocks are checked when they are freed */
    const struct allocation *a=(const void*)((const char*)ptr-ALLOCATION_HEADER);
    if(memory_bad(a,sizeof(*a),PROT_READ))return 0;
    return a->magic==ALLOCATION_MAGIC&&a->heap==handle;
}
static DWORD windows_protection(int prot) {
    switch(prot&(PROT_READ|PROT_WRITE|PROT_EXEC)) {
    case 0:return 1;case PROT_READ:return 2;case PROT_READ|PROT_WRITE:case PROT_WRITE:return 4;
    case PROT_EXEC:return 0x10;case PROT_READ|PROT_EXEC:return 0x20;default:return 0x40;
    }
}
static bool image_contains(uintptr_t address,uintptr_t *base) {
    const struct pe_image *modules[]={&image,&server_image,&vgui_image,&host_image};
    for(size_t i=0;i<sizeof(modules)/sizeof(*modules);i++)
        if(modules[i]->image&&address>=(uintptr_t)modules[i]->image&&address<(uintptr_t)modules[i]->image+modules[i]->size+4096) {
            if(base)*base=(uintptr_t)modules[i]->image;return true;
        }
    return false;
}
/* MEMORY_BASIC_INFORMATION for the run of pages at address with one protection. */
static SIZE_T WINAPI w_VirtualQuery(const void *address,uint32_t *info,SIZE_T length) {
    if(length<28){last_error=24;return 0;}
    read_mappings();
    uintptr_t page=(uintptr_t)address&~(uintptr_t)4095;
    const struct mapping *m=mapping_at(page);
    memset(info,0,28);info[0]=page;
    if(!m) {
        uintptr_t next=UINTPTR_MAX-65535;
        for(size_t i=0;i<mapping_count;i++)if(mappings[i].start>page){next=mappings[i].start;break;}
        info[3]=next-page;info[4]=0x10000;info[5]=1; /* MEM_FREE, PAGE_NOACCESS */
        return 28;
    }
    uintptr_t end=m->end;
    for(const struct mapping *n=m+1;n<mappings+mapping_count&&n->start==end&&n->prot==m->prot;n++)end=n->end;
    uintptr_t base=m->start;bool in_image=image_contains(page,&base);
    bool reserved=false;
    for(struct region *r=regions;r;r=r->next)
        if(page>=(uintptr_t)r->base&&page<(uintptr_t)r->base+r->size){base=(uintptr_t)r->base;reserved=!m->prot;}
    info[1]=base;info[2]=in_image?0x80:windows_protection(m->prot);info[3]=end-page;
    info[4]=reserved?0x2000:0x1000;info[5]=reserved?0:windows_protection(m->prot);
    info[6]=in_image?0x1000000:0x20000; /* MEM_IMAGE : MEM_PRIVATE */
    return 28;
}
static WIN_BOOL WINAPI w_VirtualProtect(void *address,SIZE_T size,DWORD protect,DWORD *old) {
    if(!old||!size){last_error=87;return 0;}
    int prot;
    switch(protect) {
    case 8:prot=PROT_READ|PROT_WRITE;break;            /* PAGE_WRITECOPY */
    case 0x10:prot=PROT_EXEC;break;                    /* PAGE_EXECUTE */
    case 0x80:prot=PROT_READ|PROT_WRITE|PROT_EXEC;break; /* PAGE_EXECUTE_WRITECOPY */
    default:
        if(protect&~0x7fu)unsupported("VirtualProtect guard/nocache protection");
        prot=win_protection(protect);
    }
    uintptr_t start=(uintptr_t)address&~(uintptr_t)4095;size_t length=page_up((uintptr_t)address-start+size);
    read_mappings();const struct mapping *m=mapping_at(start);
    if(!m||!range_has(start,length,0)){last_error=487;return 0;}
    *old=windows_protection(m->prot);
    if(mprotect((void*)start,length,prot)){last_error=5;return 0;}
    forget_mappings();return 1;
}
static void WINAPI w_GetSystemInfo(uint32_t *info) {
    memset(info,0,36);
    long processors=sysconf(_SC_NPROCESSORS_ONLN);if(processors<1)processors=1;if(processors>32)processors=32;
    info[0]=0;           /* PROCESSOR_ARCHITECTURE_INTEL */
    info[1]=4096;info[2]=0x10000;info[3]=0x7ffeffff;
    info[4]=processors==32?UINT32_MAX:(1u<<processors)-1;info[5]=processors;
    info[6]=586;info[7]=0x10000;
    unsigned a,b,c,d;
    if(__get_cpuid(1,&a,&b,&c,&d)){uint16_t level=(a>>8)&15,revision=((a>>4)&15)<<8|(a&15);memcpy((char*)info+32,&level,2);memcpy((char*)info+34,&revision,2);}
}
static long WINAPI w_InterlockedExchange(long *target,long value) {return __atomic_exchange_n(target,value,__ATOMIC_SEQ_CST);}
static void WINAPI w_OutputDebugStringA(const char *text) {
    if(!text)return;size_t n=strlen(text);
    fprintf(stderr,"[bridge] OutputDebugString: %.*s\n",(int)(n&&text[n-1]=='\n'?n-1:n),text);
}
__attribute__((noreturn)) static void WINAPI w_DebugBreak(void) {
    fprintf(stderr,"[bridge] PE DebugBreak without a debugger\n");gb_report();fflush(NULL);abort();
}
__attribute__((noreturn)) static void WINAPI w_FatalAppExitA(unsigned action,const char *text) {
    (void)action;fprintf(stderr,"[bridge] FatalAppExitA: %s\n",text?text:"");pe_exit("FatalAppExitA",255);
}
/* Console control events never reach this process as Win32 console events. */
static WIN_BOOL WINAPI w_SetConsoleCtrlHandler(void *handler,WIN_BOOL add) {(void)handler;(void)add;return 1;}
static WIN_BOOL WINAPI w_SetEnvironmentVariableA(const char *name,const char *value) {
    if(!name||!*name||strchr(name,'=')){last_error=87;return 0;}
    if(value?setenv(name,value,1):unsetenv(name)){last_error=8;return 0;}
    return 1;
}

/* The runtime offers one locale, English (United States), the locale of a
 * default Windows installation and of the "C" behaviour these mods expect. */
#define LCID_EN_US 0x409u
static bool locale_supported(DWORD lcid) {return lcid==0||lcid==LCID_EN_US||lcid==0x400||lcid==0x800||lcid==0x7f;}
static DWORD WINAPI w_GetUserDefaultLCID(void) {return LCID_EN_US;}
static WIN_BOOL WINAPI w_IsValidLocale(DWORD lcid,DWORD flags) {(void)flags;return lcid==LCID_EN_US||lcid==0x7f;}
static WIN_BOOL WINAPI w_EnumSystemLocalesA(WIN_BOOL (WINAPI *visit)(char*),DWORD flags) {
    (void)flags;if(!visit){last_error=87;return 0;}
    char name[]="00000409";visit(name);return 1;
}
static const char *const day_names[]={"Monday","Tuesday","Wednesday","Thursday","Friday","Saturday","Sunday"};
static const char *const month_names[]={"January","February","March","April","May","June","July",
    "August","September","October","November","December"};
static const char *locale_value(DWORD type,char scratch[16]) {
    static const struct {DWORD type;const char *value;} values[]={
        {0x1,"0409"},{0x2,"English (United States)"},{0x3,"ENU"},{0x4,"English"},{0x5,"1"},
        {0x6,"United States"},{0x7,"USA"},{0x8,"United States"},{0x9,"0409"},{0xa,"1"},{0xb,"437"},
        {0xc,","},{0xd,"1"},{0xe,"."},{0xf,","},{0x10,"3;0"},{0x11,"2"},{0x12,"1"},{0x13,"0123456789"},
        {0x14,"$"},{0x15,"USD"},{0x16,"."},{0x17,","},{0x18,"3;0"},{0x19,"2"},{0x1a,"2"},{0x1b,"0"},
        {0x1c,"0"},{0x1d,"/"},{0x1e,":"},{0x1f,"M/d/yyyy"},{0x20,"dddd, MMMM dd, yyyy"},{0x21,"0"},
        {0x22,"0"},{0x23,"0"},{0x24,"1"},{0x25,"0"},{0x26,"0"},{0x27,"0"},{0x28,"AM"},{0x29,"PM"},
        {0x50,""},{0x51,"-"},{0x52,"3"},{0x53,"1"},{0x54,"1"},{0x55,"0"},{0x56,"1"},{0x57,"0"},
        {0x1001,"English"},{0x1002,"United States"},{0x1003,"h:mm:ss tt"},{0x1004,"1252"},
        {0x1005,"0"},{0x1007,"10000"},{0x1009,"1"},{0x100b,"0"},{0x100c,"6"},{0x100d,"0"},
        {0x1011,"2"},{0x1012,"037"},{0x1014,"0"},{0x1016,"1"},{0x1017,"1"},{0x1018,"0"},
        {0x1019,"0"},{0x101a,"0"},{0x101b,"0"},
    };
    if(type>=0x2a&&type<=0x30)return day_names[type-0x2a];
    if(type>=0x31&&type<=0x37){snprintf(scratch,16,"%.3s",day_names[type-0x31]);return scratch;}
    if(type>=0x38&&type<=0x43)return month_names[type-0x38];
    if(type>=0x44&&type<=0x4f){snprintf(scratch,16,"%.3s",month_names[type-0x44]);return scratch;}
    for(size_t i=0;i<sizeof(values)/sizeof(*values);i++)if(values[i].type==type)return values[i].value;
    return NULL;
}
static int locale_info(DWORD lcid,DWORD type,char *text,size_t size,DWORD *number) {
    if(!locale_supported(lcid)){last_error=87;return -1;}
    /* LOCALE_NOUSEROVERRIDE and LOCALE_USE_CP_ACP do not change these values. */
    DWORD kind=type&~(0x80000000u|0x40000000u|0x20000000u);
    char scratch[16];const char *value=locale_value(kind,scratch);
    if(!value) {
        fprintf(stderr,"[bridge] GetLocaleInfo: LCTYPE %#x\n",kind);
        unsupported("GetLocaleInfo LCTYPE outside the en-US table");
    }
    if(type&0x20000000u)*number=kind==0x1||kind==0x9?strtoul(value,NULL,16):strtoul(value,NULL,10);
    snprintf(text,size,"%s",value);return 0;
}
static int WINAPI w_GetLocaleInfoA(DWORD lcid,DWORD type,char *out,int count) {
    char text[64];DWORD number;
    if(count<0||(count&&!out)){last_error=87;return 0;}
    if(locale_info(lcid,type,text,sizeof(text),&number))return 0;
    if(type&0x20000000u) {
        if(!count)return sizeof(number);
        if(count<(int)sizeof(number)){last_error=122;return 0;}
        memcpy(out,&number,sizeof(number));return sizeof(number);
    }
    int length=strlen(text)+1;
    if(!count)return length;
    if(count<length){last_error=122;return 0;}
    memcpy(out,text,length);return length;
}
static int WINAPI w_GetLocaleInfoW(DWORD lcid,DWORD type,uint16_t *out,int count) {
    char text[64];DWORD number;
    if(count<0||(count&&!out)){last_error=87;return 0;}
    if(locale_info(lcid,type,text,sizeof(text),&number))return 0;
    if(type&0x20000000u) {
        if(!count)return 2;
        if(count<2){last_error=122;return 0;}
        memcpy(out,&number,sizeof(number));return 2;
    }
    int length=strlen(text)+1;
    if(!count)return length;
    if(count<length){last_error=122;return 0;}
    for(int i=0;i<length;i++)out[i]=(unsigned char)text[i];
    return length;
}
/* GetDateFormat/GetTimeFormat picture strings: repeated pattern letters and
 * 'quoted' literal text. Only fields of the requested kind are expanded. */
static bool picture_format(const char *picture,const uint16_t *t,bool date,DWORD time_flags,char *out,size_t size) {
    size_t length=0;
#define APPEND(...) do{int n=snprintf(out+length,size-length,__VA_ARGS__);if(n<0||(size_t)n>=size-length)return false;length+=n;}while(0)
    for(const char *p=picture;*p;) {
        if(*p=='\'') {
            p++;
            while(*p&&!(p[0]=='\''&&p[1]!='\''))
                {if(p[0]=='\''&&p[1]=='\'')p++;APPEND("%c",*p);p++;}
            if(*p)p++;continue;
        }
        char c=*p;int run=0;while(p[run]==c)run++;
        if(date&&c=='d'){
            if(run==1)APPEND("%u",t[3]);else if(run==2)APPEND("%02u",t[3]);
            else{const char *name=day_names[(t[2]+6)%7];if(run==3)APPEND("%.3s",name);else APPEND("%s",name);}
        } else if(date&&c=='M') {
            if(run==1)APPEND("%u",t[1]);else if(run==2)APPEND("%02u",t[1]);
            else if(run==3)APPEND("%.3s",month_names[t[1]-1]);else APPEND("%s",month_names[t[1]-1]);
        } else if(date&&c=='y') {
            if(run==1)APPEND("%u",t[0]%100);else if(run==2)APPEND("%02u",t[0]%100);else APPEND("%u",t[0]);
        } else if(date&&c=='g') {APPEND("A.D.");
        } else if(!date&&(c=='h'||c=='H')) {
            unsigned hour=t[4];if(c=='h'&&!(time_flags&8)){hour%=12;if(!hour)hour=12;}
            if(run==1)APPEND("%u",hour);else APPEND("%02u",hour);
        } else if(!date&&c=='m') {
            if(time_flags&1){p+=run;while(*p&&strchr(":. ",*p))p++;continue;}
            if(run==1)APPEND("%u",t[5]);else APPEND("%02u",t[5]);
        } else if(!date&&c=='s') {
            if(time_flags&3){while(length&&strchr(":. ",out[length-1]))length--;out[length]=0;p+=run;continue;}
            if(run==1)APPEND("%u",t[6]);else APPEND("%02u",t[6]);
        } else if(!date&&c=='t') {
            if(!(time_flags&4)){const char *marker=t[4]<12?"AM":"PM";if(run==1)APPEND("%c",marker[0]);else APPEND("%s",marker);}
            else while(length&&out[length-1]==' ')out[--length]=0;
        } else {for(int i=0;i<run;i++)APPEND("%c",c);}
        p+=run;
    }
#undef APPEND
    return true;
}
static int date_time_format(DWORD lcid,const uint16_t *when,const char *picture,char *out,int count,bool date,DWORD flags) {
    if(!locale_supported(lcid)||count<0||(count&&!out)){last_error=87;return 0;}
    uint16_t now[8];if(!when){w_GetLocalTime(now);when=now;}
    else if(!system_time_valid(when)){last_error=87;return 0;}
    if(!picture)picture=date?(flags&2?"dddd, MMMM dd, yyyy":"M/d/yyyy"):"h:mm:ss tt";
    char text[256];
    if(!picture_format(picture,when,date,date?0:flags,text,sizeof(text))){last_error=122;return 0;}
    int length=strlen(text)+1;
    if(!count)return length;
    if(count<length){last_error=122;return 0;}
    memcpy(out,text,length);return length;
}
static int WINAPI w_GetDateFormatA(DWORD lcid,DWORD flags,const uint16_t *when,const char *picture,char *out,int count) {
    if(flags&~(0x80000000u|1u|2u)){last_error=1004;return 0;}
    return date_time_format(lcid,when,picture,out,count,true,flags);
}
static int WINAPI w_GetTimeFormatA(DWORD lcid,DWORD flags,const uint16_t *when,const char *picture,char *out,int count) {
    if(flags&~(0x80000000u|0xfu)){last_error=1004;return 0;}
    return date_time_format(lcid,when,picture,out,count,false,flags);
}
static int compare_wide(DWORD flags,const uint16_t *a,int a_count,const uint16_t *b,int b_count) {
    /* NORM_IGNORECASE; SORT_STRINGSORT only changes hyphen/apostrophe weight. */
    if(flags&~(1u|0x1000u))unsupported("CompareString flags other than NORM_IGNORECASE");
    if(a_count<0)a_count=wide_length(a);
    if(b_count<0)b_count=wide_length(b);
    static locale_t unicode;
    if(!unicode)unicode=newlocale(LC_CTYPE_MASK,"C.UTF-8",(locale_t)0);
    if(!unicode)unsupported("C.UTF-8 locale unavailable");
    for(int i=0;i<a_count&&i<b_count;i++) {
        wint_t x=a[i],y=b[i];
        if(flags&1){x=towlower_l(x,unicode);y=towlower_l(y,unicode);}
        if(x!=y)return x<y?1:3;
    }
    return a_count==b_count?2:a_count<b_count?1:3;
}
static int WINAPI w_CompareStringW(DWORD lcid,DWORD flags,const uint16_t *a,int a_count,const uint16_t *b,int b_count) {
    if(!locale_supported(lcid)||!a||!b){last_error=87;return 0;}
    return compare_wide(flags,a,a_count,b,b_count);
}
static int WINAPI w_CompareStringA(DWORD lcid,DWORD flags,const char *a,int a_count,const char *b,int b_count) {
    if(!locale_supported(lcid)||!a||!b){last_error=87;return 0;}
    if(a_count<0)a_count=strlen(a);
    if(b_count<0)b_count=strlen(b);
    uint16_t *wide=malloc(((size_t)a_count+b_count+2)*2);if(!wide){last_error=8;return 0;}
    int na=a_count?w_MultiByteToWideChar(1252,0,a,a_count,wide,a_count):0;
    int nb=b_count?w_MultiByteToWideChar(1252,0,b,b_count,wide+a_count,b_count):0;
    int result=compare_wide(flags,wide,na,wide+a_count,nb);
    free(wide);return result;
}

/* Directory APIs of the narrow, '\\'-separated Windows path convention. */
static DWORD WINAPI w_GetCurrentDirectoryA(DWORD count,char *out) {
    char path[4096];if(!getcwd(path,sizeof(path))){last_error=5;return 0;}
    for(char *p=path;*p;p++)if(*p=='/')*p='\\';
    DWORD length=strlen(path);
    if(length>=count)return length+1;
    memcpy(out,path,length+1);return length;
}
static WIN_BOOL WINAPI w_SetCurrentDirectoryA(const char *name) {
    char path[4096];if(!name||!windows_path(name,path))return 0;
    fprintf(stderr,"[bridge] SetCurrentDirectoryA(%s)\n",path);
    if(chdir(path)){last_error=errno==ENOENT||errno==ENOTDIR?3:5;return 0;}
    return 1;
}
static DWORD WINAPI w_GetFullPathNameA(const char *name,DWORD count,char *out,char **file_part) {
    char full[4096];
    if(!name||!name[0]){last_error=123;return 0;}
    if(!full_path(name,full,sizeof(full))){last_error=206;return 0;}
    DWORD length=strlen(full);
    if(length>=count)return length+1;
    memcpy(out,full,length+1);
    if(file_part){char *last=strrchr(out,'\\');*file_part=last&&last[1]?last+1:NULL;}
    return length;
}
static unsigned WINAPI w_GetDriveTypeA(const char *root) {
    char linux_path[4096];
    if(!root){if(!getcwd(linux_path,sizeof(linux_path)))return 1;}
    else if(!windows_path(root,linux_path))return 1;
    struct stat st;
    return !stat(linux_path,&st)&&S_ISDIR(st.st_mode)?3:1;
}
static WIN_BOOL WINAPI w_FileTimeToLocalFileTime(const uint64_t *utc,uint64_t *local) {
    uint64_t value;memcpy(&value,utc,sizeof(value));
    int64_t seconds=(int64_t)(value/10000000)-11644473600ll;
    time_t instant=(time_t)seconds;if((int64_t)instant!=seconds)instant=time(NULL);
    struct tm tm;if(!localtime_r(&instant,&tm)){last_error=87;return 0;}
    value+=(int64_t)tm.tm_gmtoff*10000000;memcpy(local,&value,sizeof(value));return 1;
}
static WIN_BOOL WINAPI w_GetComputerNameA(char *out,DWORD *size) {
    char name[256];if(!size||gethostname(name,sizeof(name))){last_error=87;return 0;}
    name[15]=0; /* MAX_COMPUTERNAME_LENGTH */
    for(char *p=name;*p;p++)if(*p>='a'&&*p<='z')*p-='a'-'A';
    DWORD length=strlen(name);
    if(!out||*size<=length){*size=length+1;last_error=111;return 0;}
    memcpy(out,name,length+1);*size=length;return 1;
}

/* FindFirstFileA: WIN32_FIND_DATAA entries of one directory, matched
 * case-insensitively; '*' and '?' as in Windows, "*.*" matches every name. */
struct find {uint32_t magic;char directory[4096];char **names;size_t count,next;};
#define FIND_MAGIC 0x444e4946u /* "FIND" */
static bool wildcard_match(const char *pattern,const char *name) {
    if(!strcmp(pattern,"*.*"))pattern="*";
    for(;;) {
        if(*pattern=='*') {
            while(*pattern=='*')pattern++;
            if(!*pattern)return true;
            for(;*name;name++)if(wildcard_match(pattern,name))return true;
            return false;
        }
        if(!*pattern)return !*name;
        if(!*name)return false;
        if(*pattern!='?'&&tolower((unsigned char)*pattern)!=tolower((unsigned char)*name))return false;
        pattern++;name++;
    }
}
static WIN_BOOL find_entry(struct find *f,uint32_t *data) {
    while(f->next<f->count) {
        const char *name=f->names[f->next++];
        char path[8192];snprintf(path,sizeof(path),"%s/%s",f->directory,name);
        struct stat st;if(stat(path,&st))continue;
        memset(data,0,320);
        uint32_t attributes=S_ISDIR(st.st_mode)?0x10:0;
        if(!(st.st_mode&S_IWUSR))attributes|=1;
        if(name[0]=='.'&&strcmp(name,".")&&strcmp(name,".."))attributes|=2;
        data[0]=attributes?attributes:0x80;
        uint64_t created=filetime_from_timespec(st.st_ctim),accessed=filetime_from_timespec(st.st_atim),written=filetime_from_timespec(st.st_mtim);
        memcpy(&data[1],&created,8);memcpy(&data[3],&accessed,8);memcpy(&data[5],&written,8);
        uint64_t size=S_ISDIR(st.st_mode)?0:(uint64_t)st.st_size;data[7]=size>>32;data[8]=(uint32_t)size;
        snprintf((char*)&data[11],260,"%s",name);
        return 1;
    }
    last_error=18;return 0; /* ERROR_NO_MORE_FILES */
}
static void find_free(struct find *f) {
    for(size_t i=0;i<f->count;i++)free(f->names[i]);
    free(f->names);f->magic=0;free(f);
}
static HANDLE WINAPI w_FindFirstFileA(const char *pattern,uint32_t *data) {
    char path[4096];if(!pattern||!data||!windows_path(pattern,path))return (void*)-1;
    char *slash=strrchr(path,'/');const char *match=slash?slash+1:path;
    struct find *f=calloc(1,sizeof(*f));if(!f){last_error=8;return (void*)-1;}
    f->magic=FIND_MAGIC;
    if(slash)snprintf(f->directory,sizeof(f->directory),"%.*s",(int)(slash-path)?(int)(slash-path):1,path);
    else snprintf(f->directory,sizeof(f->directory),".");
    DIR *dir=opendir(f->directory);
    if(!dir){free(f);last_error=3;return (void*)-1;}
    for(struct dirent *entry;(entry=readdir(dir));) {
        if(!wildcard_match(match,entry->d_name))continue;
        char **grown=realloc(f->names,(f->count+1)*sizeof(*grown));
        char *name=strdup(entry->d_name);
        if(!grown||!name){free(name);if(grown)f->names=grown;closedir(dir);find_free(f);last_error=8;return (void*)-1;}
        f->names=grown;f->names[f->count++]=name;
    }
    closedir(dir);
    if(!find_entry(f,data)){find_free(f);last_error=2;return (void*)-1;}
    return f;
}
static WIN_BOOL WINAPI w_FindNextFileA(HANDLE handle,uint32_t *data) {
    struct find *f=handle;
    if(!f||f==(void*)-1||f->magic!=FIND_MAGIC){last_error=6;return 0;}
    return find_entry(f,data);
}
static WIN_BOOL WINAPI w_FindClose(HANDLE handle) {
    struct find *f=handle;
    if(!f||f==(void*)-1||f->magic!=FIND_MAGIC){last_error=6;return 0;}
    find_free(f);return 1;
}

/* Windows-only helper programs and documents (Decay's stat.exe score viewer)
 * cannot be started here; callers receive ShellExecute's "not found" result. */
static HANDLE WINAPI w_ShellExecuteA(void *window,const char *operation,const char *file,const char *parameters,const char *directory,int show) {
    (void)window;(void)show;
    fprintf(stderr,"[bridge] ShellExecuteA(%s, %s, %s, %s) not started on Linux\n",operation?operation:"open",
        file?file:"",parameters?parameters:"",directory?directory:"");
    return (HANDLE)2; /* ERROR_FILE_NOT_FOUND, <= 32 means failure */
}
/* WinMM joystick queries: joyGetNumDevs reports no devices. */
static unsigned WINAPI w_joyGetPosEx(unsigned id,void *info) {(void)id;(void)info;return 167; /* JOYERR_UNPLUGGED */}
static unsigned WINAPI w_joyGetDevCapsA(uintptr_t id,void *caps,unsigned size) {(void)id;(void)caps;(void)size;return 165; /* JOYERR_PARMS */}

struct named_api {const char *name;void *address;};
#define API(name) {#name,w_##name}
static struct named_api implemented[]={
    API(HeapCreate),API(HeapAlloc),API(HeapFree),API(HeapReAlloc),API(HeapDestroy),
    API(HeapSize),API(GetTickCount),API(GetVersionExA),API(SetUnhandledExceptionFilter),API(SystemParametersInfoA),API(joyGetNumDevs),
    API(GetLocalTime),API(GetSystemTime),API(GetTimeZoneInformation),
    API(GetCursorPos),API(SetCursorPos),
    API(CreateFileA),API(CreateDirectoryA),API(ReadFile),API(WriteFile),API(SetFilePointer),API(CloseHandle),API(FlushFileBuffers),
    API(VirtualAlloc),API(VirtualFree),API(TlsAlloc),API(TlsSetValue),API(TlsGetValue),API(TlsFree),
    API(InitializeCriticalSection),API(EnterCriticalSection),API(LeaveCriticalSection),API(DeleteCriticalSection),
    API(InterlockedIncrement),API(InterlockedDecrement),API(GetVersion),API(GetLastError),API(SetLastError),
    API(GetCurrentThreadId),API(GetCurrentThread),API(GetCurrentProcess),API(GetCommandLineA),
    API(GetStdHandle),API(SetStdHandle),API(GetFileType),API(GetStartupInfoA),
    API(GetEnvironmentStrings),API(FreeEnvironmentStringsA),API(GetModuleHandleA),API(GetProcAddress),
    API(GetModuleFileNameA),API(LoadLibraryA),API(FreeLibrary),
    API(GetEnvironmentStringsW),API(FreeEnvironmentStringsW),API(WideCharToMultiByte),API(MultiByteToWideChar),
    API(GetStringTypeW),API(GetStringTypeA),
    API(LCMapStringW),API(LCMapStringA),
    API(GetACP),API(GetCPInfo)
    ,API(SetHandleCount),
    API(GetSystemTimeAsFileTime),API(QueryPerformanceCounter),API(QueryPerformanceFrequency),
    API(GetCurrentProcessId),API(IsDebuggerPresent),API(IsProcessorFeaturePresent),
    API(EncodePointer),API(DecodePointer),
    API(InitializeSListHead),API(InterlockedPushEntrySList),API(InterlockedPopEntrySList),
    API(InterlockedFlushSList),API(QueryDepthSList),
    API(GetProcessHeap),API(InitializeCriticalSectionAndSpinCount),API(TryEnterCriticalSection),
    API(Sleep),API(SwitchToThread),API(GetThreadTimes),
    API(GetStartupInfoW),API(GetCommandLineW),API(GetModuleFileNameW),API(GetModuleHandleW),API(GetModuleHandleExW),
    API(LoadLibraryW),API(LoadLibraryExW),API(GetVersionExW),
    API(IsValidCodePage),API(GetOEMCP),API(GetConsoleCP),API(GetConsoleMode),
    API(CreateFileW),API(GetFileSizeEx),API(SetFilePointerEx),API(SetEndOfFile),
    API(ExitProcess),API(TerminateProcess),API(UnhandledExceptionFilter),
    API(MessageBoxA),API(GetActiveWindow),
    API(FileTimeToSystemTime),API(SystemTimeToTzSpecificLocalTime),API(GetFileInformationByHandle),API(GetCurrentDirectoryW),
    API(GetFullPathNameW),API(GetDriveTypeW),
    API(IsBadReadPtr),API(IsBadWritePtr),API(IsBadCodePtr),API(HeapValidate),API(VirtualQuery),API(VirtualProtect),
    API(GetSystemInfo),API(InterlockedExchange),API(OutputDebugStringA),API(DebugBreak),API(FatalAppExitA),
    API(SetConsoleCtrlHandler),API(SetEnvironmentVariableA),API(GetUserDefaultLCID),API(IsValidLocale),
    API(EnumSystemLocalesA),API(GetLocaleInfoA),API(GetLocaleInfoW),API(GetDateFormatA),API(GetTimeFormatA),
    API(CompareStringA),API(CompareStringW),API(GetCurrentDirectoryA),API(SetCurrentDirectoryA),
    API(GetFullPathNameA),API(GetDriveTypeA),API(FileTimeToLocalFileTime),API(GetComputerNameA),
    API(FindFirstFileA),API(FindNextFileA),API(FindClose),API(ShellExecuteA),API(joyGetPosEx),API(joyGetDevCapsA)
};
static void *implemented_api(const char *name) {
    for(size_t i=0;i<sizeof(implemented)/sizeof(*implemented);i++)
        if(!strcmp(implemented[i].name,name))return implemented[i].address;
    return NULL;
}

/* Each import gets a permanent named trap or counted tail-jump to an implementation. */
void *cb_module_symbol(struct pe_image *module,const char *name,bool *executable);
void *cb_module_symbol(struct pe_image *module,const char *name,bool *executable) {
    IMAGE_EXPORT_DIRECTORY *e=(void*)((char*)module->image+
        module->opt_hdr->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
    uint32_t *names=(void*)((char*)module->image+e->AddressOfNames);
    uint16_t *ordinals=(void*)((char*)module->image+e->AddressOfNameOrdinals);
    uint32_t *functions=(void*)((char*)module->image+e->AddressOfFunctions);
    for(size_t i=0;i<e->NumberOfNames;i++)if(!strcmp((char*)module->image+names[i],name)) {
        uint32_t rva=functions[ordinals[i]];
        if(executable) {
            *executable=false;
            IMAGE_SECTION_HEADER *s=(void*)((char*)&module->nt_hdr->OptionalHeader+module->nt_hdr->FileHeader.SizeOfOptionalHeader);
            for(unsigned j=0;j<module->nt_hdr->FileHeader.NumberOfSections;j++)
                if(rva>=s[j].VirtualAddress && rva<s[j].VirtualAddress+(s[j].Misc.VirtualSize>s[j].SizeOfRawData?s[j].Misc.VirtualSize:s[j].SizeOfRawData))
                    *executable=(s[j].Characteristics&0x20000000)!=0;
        }
        return (char*)module->image+rva;
    }
    return NULL;
}
static void bind_imports(struct pe_image *module_image) {
    if(!module_image->opt_hdr->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress)return;
    IMAGE_IMPORT_DESCRIPTOR *desc=(void*)((char*)module_image->image+
        module_image->opt_hdr->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    for(;desc->Name;desc++) {
        char *module=(char*)module_image->image+desc->Name;
        uint32_t *lookup=(void*)((char*)module_image->image+desc->u.OriginalFirstThunk);
        for(;*lookup;lookup++) {
            if(binding_count>=MAX_BINDINGS)unsupported("import count");
            struct binding *b=&bindings[binding_count++];
            if(*lookup&0x80000000u) {
                unsigned ordinal=*lookup&0xffff;
                if(!strcasecmp(module,"WSOCK32.dll")&&ordinal==9){b->name="WSOCK32.htons";b->target=w_htons;}
                else unsupported("unknown ordinal import");
            } else b->name=(char*)module_image->image+*lookup+2;
            b->name=strdup(b->name);if(!b->name)unsupported("import name allocation");
            bool code=true;
            if(!strcasecmp(module,"vgui.dll")) {
                b->target=cb_module_symbol(&vgui_image,b->name,&code);
                if(!b->target)unsupported(b->name);
            }
            for(size_t i=0;i<sizeof(implemented)/sizeof(*implemented);i++)
                if((!strcasecmp(module,"KERNEL32.dll")||!strcasecmp(module,"USER32.dll")||!strcasecmp(module,"WINMM.dll")||!strcasecmp(module,"SHELL32.dll"))&&!strcmp(implemented[i].name,b->name))b->target=implemented[i].address;
            /* Exception dispatch is delegated to the upstream PE runtime. */
            if(!strcmp(b->name,"RtlUnwind")||!strcmp(b->name,"RaiseException"))b->target=get_export_address(b->name);
            if(!strcasecmp(module,"SDL2.dll"))b->target=cb_sdl_symbol(b->name);
            if(!strcasecmp(module,"GDI32.dll")||!strcasecmp(module,"USER32.dll")) {
                void *gdi=cb_gdi_symbol(b->name);if(gdi)b->target=gdi;
            }
            unsigned char *thunk=(unsigned char*)thunk_page+(binding_count-1)*THUNK_BYTES;
            b->thunk=thunk;
            thunk[0]=0xff;thunk[1]=0x05;put_u32(thunk+2,(uintptr_t)&b->calls);
            if(b->target) {
                thunk[6]=0x68;put_u32(thunk+7,(uintptr_t)b->target);thunk[11]=0xc3;
            } else {
                thunk[6]=0x68;put_u32(thunk+7,(uintptr_t)b->name);
                thunk[11]=0xb8;put_u32(thunk+12,(uintptr_t)unsupported);
                thunk[16]=0xff;thunk[17]=0xd0;thunk[18]=0x0f;thunk[19]=0x0b;
            }
            /* Replace the loader's generic implementations, including permissive stubs. */
            uint32_t *iat=(void*)((char*)module_image->image+desc->FirstThunk);
            size_t index=lookup-((uint32_t*)((char*)module_image->image+desc->u.OriginalFirstThunk));
            iat[index]=(uintptr_t)(code?b->thunk:b->target);
        }
    }
}
static void protect_image(struct pe_image *module) {
    struct pe_image image=*module;
    IMAGE_SECTION_HEADER *sections=(void*)((char*)&image.nt_hdr->OptionalHeader+
        image.nt_hdr->FileHeader.SizeOfOptionalHeader);
    if(mprotect(image.image,image.size+4096,PROT_READ))unsupported("PE header protection");
    for(unsigned i=0;i<image.nt_hdr->FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER *s=&sections[i];
        size_t length=s->Misc.VirtualSize>s->SizeOfRawData?s->Misc.VirtualSize:s->SizeOfRawData;
        if(!length)continue;
        int prot=0;
        if(s->Characteristics&0x20000000)prot|=PROT_EXEC;
        if(s->Characteristics&0x40000000)prot|=PROT_READ;
        if(s->Characteristics&0x80000000)prot|=PROT_WRITE;
        if(mprotect((char*)image.image+s->VirtualAddress,page_up(length),prot))unsupported("PE section protection");
    }
}
static int map_module_file(const char *filename,struct pe_image *module) {
    if(!runtime_started) {
        owner_thread=syscall(SYS_gettid);__asm__ volatile("mov %%fs,%0":"=r"(original_fs));
        size_t size;if(!pe_load_library(filename,&module->image,&size))return -1;
        module->size=size;runtime_started=true;return 0;
    }
    check_thread();int fd=open(filename,O_RDONLY);struct stat st;if(fd<0)return -1;
    if(fstat(fd,&st)){close(fd);return -1;}
    module->size=st.st_size;module->image=mmap(NULL,module->size,PROT_READ,MAP_PRIVATE,fd,0);close(fd);
    if(module->image==MAP_FAILED){module->image=NULL;return -1;}return 0;
}
static void thunks_writable(void) {
    if(!thunk_page)thunk_page=mmap(NULL,thunk_size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(thunk_page==MAP_FAILED||mprotect(thunk_page,thunk_size,PROT_READ|PROT_WRITE))unsupported("import thunk allocation/protection");
}
static void release_runtime_if_idle(void) {
    if(image.image||server_image.image)return;
    __asm__ volatile("mov %0,%%fs"::"r"(original_fs));release_nt_threadinfo();
    if(SharedUserData&&SharedUserData!=MAP_FAILED)munmap(SharedUserData,page_up(sizeof(*SharedUserData)));SharedUserData=NULL;
    if(thunk_page)munmap(thunk_page,thunk_size);thunk_page=NULL;
    for(size_t i=0;i<binding_count;i++)free((void*)bindings[i].name);
    memset(bindings,0,sizeof(bindings));binding_count=0;
    memset(LocalStorage,0,sizeof(uintptr_t)*1024);memset(tls_used,0,sizeof(tls_used));tls_used[0]=true;
    runtime_started=false;
}
int gb_load(const char *filename) {
    if(image.image)return -1;
    if(!runtime_started){owner_thread=syscall(SYS_gettid);__asm__ volatile("mov %%fs,%0":"=r"(original_fs));}
    else check_thread();
    if(!realpath(filename,dll_path))return -1;
    snprintf(command_line,sizeof(command_line),"\"%s\"",filename);
    snprintf(image.name,sizeof(image.name),"client.dll");
    const char *dependency=getenv("CB_VGUI_DLL");
    if(!dependency||!realpath(dependency,vgui_path))return -1;
    snprintf(vgui_image.name,sizeof(vgui_image.name),"vgui.dll");
    size_t size;
    if(map_module_file(dependency,&vgui_image))return -1;
    size=vgui_image.size;
    int fd=open(filename,O_RDONLY);struct stat stat;
    if(fd<0||fstat(fd,&stat))return -1;
    image.size=stat.st_size;image.image=mmap(NULL,image.size,PROT_READ,MAP_PRIVATE,fd,0);close(fd);
    if(image.image==MAP_FAILED)return -1;
    struct pe_image modules[3]={vgui_image,image};unsigned module_count=2;
    const char *helper=getenv("CB_VGUI_HOST");
    if(helper) {
        int helper_fd=open(helper,O_RDONLY);struct stat helper_stat;
        if(helper_fd<0)return -1;
        if(fstat(helper_fd,&helper_stat)){close(helper_fd);return -1;}
        host_image.size=helper_stat.st_size;host_image.image=mmap(NULL,host_image.size,PROT_READ,MAP_PRIVATE,helper_fd,0);close(helper_fd);
        if(host_image.image==MAP_FAILED)return -1;
        snprintf(host_image.name,sizeof(host_image.name),"vgui_host.dll");
        modules[2]=host_image;module_count=3;
    }
    if(link_pe_images(modules,module_count))return -1;
    vgui_image=modules[0];image=modules[1];
    if(module_count==3)host_image=modules[2];
    if(image.opt_hdr->DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].VirtualAddress)
        unsupported("static PE TLS is outside this prototype");
    thunks_writable();
    bind_imports(&vgui_image);bind_imports(&image);
    if(host_image.image)bind_imports(&host_image);
    if(mprotect(thunk_page,thunk_size,PROT_READ|PROT_EXEC))unsupported("thunk page protection");
    int dependency_result=vgui_image.entry(vgui_image.image,1,NULL);
    fprintf(stderr,"[client] VGUI DLL_PROCESS_ATTACH=%d image=%p\n",dependency_result,vgui_image.image);
    if(!dependency_result)return -1;
    fprintf(stderr,"[bridge] mapped image=%p size=%#x entry=%p\n",image.image,image.size,image.entry);
    int result=image.entry(image.image,1,NULL);
    fprintf(stderr,"[bridge] DLL_PROCESS_ATTACH=%d\n",result);
    attached=result!=0;
    if(attached){protect_image(&image);protect_image(&vgui_image);if(host_image.image)protect_image(&host_image);}
    return result?0:-1;
}
void *gb_symbol(const char *name) {check_thread();return cb_module_symbol(&image,name,NULL);}
void *cb_vgui_symbol(const char *name) {check_thread();return cb_module_symbol(&vgui_image,name,NULL);}
void *cb_host_symbol(const char *name) {check_thread();return host_image.image?cb_module_symbol(&host_image,name,NULL):NULL;}
int gb_contains(void *address) {
    return (uintptr_t)address>=(uintptr_t)image.image && (uintptr_t)address<(uintptr_t)image.image+image.size;
}
const char *gb_symbol_name(void *address) {
    IMAGE_EXPORT_DIRECTORY *e=(void*)((char*)image.image+
        image.opt_hdr->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
    uint32_t *names=(void*)((char*)image.image+e->AddressOfNames);
    uint16_t *ordinals=(void*)((char*)image.image+e->AddressOfNameOrdinals);
    uint32_t *functions=(void*)((char*)image.image+e->AddressOfFunctions);
    for(size_t i=0;i<e->NumberOfNames;i++)
        if((char*)image.image+functions[ordinals[i]]==address)return (char*)image.image+names[i];
    return NULL;
}
void gb_report(void) {
    for(size_t i=0;i<binding_count;i++)if(bindings[i].calls)
        fprintf(stderr,"[bridge] WinAPI %-28s calls=%u implemented=%s\n",bindings[i].name,
            bindings[i].calls,bindings[i].target?"yes":"NO");
}
void gb_unload(void) {
    if(!image.image)return;
    check_thread();
    if(attached) {
        int result=image.entry(image.image,0,NULL);
        fprintf(stderr,"[bridge] DLL_PROCESS_DETACH=%d\n",result);
        attached=false;
        int vgui_result=vgui_image.entry(vgui_image.image,0,NULL);
        fprintf(stderr,"[client] VGUI DLL_PROCESS_DETACH=%d\n",vgui_result);
    }
    gb_report();
    cb_fmod_unload();
    cb_gdi_unload();
    while(files)w_CloseHandle(files);
    munmap(image.image,image.size+4096);image.image=NULL;
    munmap(vgui_image.image,vgui_image.size+4096);vgui_image.image=NULL;
    if(host_image.image){munmap(host_image.image,host_image.size+4096);host_image.image=NULL;}
    release_runtime_if_idle();
}

int cb_server_load(const char *filename) {
    if(server_image.image||!realpath(filename,server_path))return -1;
    const char *basename=strrchr(server_path,'/');basename=basename?basename+1:server_path;
    size_t basename_length=strlen(basename);
    if(basename_length>=sizeof(server_image.name))return -1;
    memcpy(server_image.name,basename,basename_length+1);
    if(map_module_file(filename,&server_image))return -1;
    if(link_pe_images(&server_image,1))return -1;
    if(server_image.opt_hdr->DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].VirtualAddress)unsupported("server static TLS");
    thunks_writable();bind_imports(&server_image);
    if(mprotect(thunk_page,thunk_size,PROT_READ|PROT_EXEC))unsupported("thunk page protection");
    int result=server_image.opt_hdr->AddressOfEntryPoint?server_image.entry(server_image.image,1,NULL):1;server_attached=result!=0;
    fprintf(stderr,"[server] DLL_PROCESS_ATTACH=%d image=%p\n",result,server_image.image);
    if(server_attached)protect_image(&server_image);return result?0:-1;
}
void *cb_server_symbol(const char *name) {check_thread();return cb_module_symbol(&server_image,name,NULL);}
int cb_server_visit_exports(int(*visit)(const char*,void*,int,void*),void *context) {
    check_thread();
    if(!server_image.image||!visit)return -1;
    IMAGE_DATA_DIRECTORY directory=server_image.opt_hdr->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if(!directory.VirtualAddress)return 0;
    IMAGE_EXPORT_DIRECTORY *e=(void*)((char*)server_image.image+directory.VirtualAddress);
    uint32_t *names=(void*)((char*)server_image.image+e->AddressOfNames);
    uint16_t *ordinals=(void*)((char*)server_image.image+e->AddressOfNameOrdinals);
    uint32_t *functions=(void*)((char*)server_image.image+e->AddressOfFunctions);
    IMAGE_SECTION_HEADER *sections=(void*)((char*)&server_image.nt_hdr->OptionalHeader+server_image.nt_hdr->FileHeader.SizeOfOptionalHeader);
    for(size_t i=0;i<e->NumberOfNames;i++) {
        if(ordinals[i]>=e->NumberOfFunctions)return -1;
        uint32_t rva=functions[ordinals[i]];int executable=0;
        if(!rva)continue;
        // Forwarder strings are not callable factory addresses.
        if(rva>=directory.VirtualAddress&&rva-directory.VirtualAddress<directory.Size)continue;
        for(unsigned j=0;j<server_image.nt_hdr->FileHeader.NumberOfSections;j++) {
            IMAGE_SECTION_HEADER *s=&sections[j];
            size_t length=s->Misc.VirtualSize>s->SizeOfRawData?s->Misc.VirtualSize:s->SizeOfRawData;
            if(rva>=s->VirtualAddress&&rva-s->VirtualAddress<length)executable=(s->Characteristics&0x20000000)!=0;
        }
        if(visit((char*)server_image.image+names[i],(char*)server_image.image+rva,executable,context))return -1;
    }
    return 0;
}
int cb_server_contains(void *p) {return (uintptr_t)p>=(uintptr_t)server_image.image&&(uintptr_t)p<(uintptr_t)server_image.image+server_image.size;}
const char *cb_server_symbol_name(void *p) {
    IMAGE_EXPORT_DIRECTORY *e=(void*)((char*)server_image.image+server_image.opt_hdr->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
    uint32_t *names=(void*)((char*)server_image.image+e->AddressOfNames),*functions=(void*)((char*)server_image.image+e->AddressOfFunctions);
    uint16_t *ordinals=(void*)((char*)server_image.image+e->AddressOfNameOrdinals);
    for(size_t i=0;i<e->NumberOfNames;i++)if((char*)server_image.image+functions[ordinals[i]]==p)return (char*)server_image.image+names[i];return NULL;
}
void cb_server_unload(void) {
    if(!server_image.image)return;check_thread();
    if(server_attached){if(server_image.opt_hdr->AddressOfEntryPoint)server_image.entry(server_image.image,0,NULL);server_attached=false;}
    gb_report();munmap(server_image.image,server_image.size+4096);server_image.image=NULL;release_runtime_if_idle();
}
