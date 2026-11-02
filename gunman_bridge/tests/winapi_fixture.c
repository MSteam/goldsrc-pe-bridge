/* PE32 fixture for WinAPI paths of the Visual C++ runtime, without a CRT.
 * FixtureStat repeats the calls of UCRT stat(): a FILE_READ_ATTRIBUTES handle,
 * GetFileType, GetFileInformationByHandle, FileTimeToSystemTime and
 * SystemTimeToTzSpecificLocalTime. */
typedef unsigned long DWORD;typedef void *HANDLE;typedef int BOOL;
int _fltused=0; /* MSVC link marker for floating-point code; there is no CRT */
__declspec(dllimport) HANDLE __stdcall CreateFileA(const char*,DWORD,DWORD,void*,DWORD,DWORD,HANDLE);
__declspec(dllimport) DWORD __stdcall GetFileType(HANDLE);
__declspec(dllimport) BOOL __stdcall GetFileInformationByHandle(HANDLE,DWORD*);
__declspec(dllimport) BOOL __stdcall FileTimeToSystemTime(const DWORD*,unsigned short*);
__declspec(dllimport) BOOL __stdcall SystemTimeToTzSpecificLocalTime(const void*,const unsigned short*,unsigned short*);
__declspec(dllimport) BOOL __stdcall CloseHandle(HANDLE);
__declspec(dllimport) DWORD __stdcall GetLastError(void);
__declspec(dllimport) DWORD __stdcall GetFullPathNameW(const unsigned short*,DWORD,unsigned short*,unsigned short**);
__declspec(dllimport) unsigned __stdcall GetDriveTypeW(const unsigned short*);
__declspec(dllimport) void *__stdcall LoadLibraryA(const char*);
__declspec(dllimport) void *__stdcall GetProcAddress(void*,const char*);
__declspec(dllimport) BOOL __stdcall FreeLibrary(void*);
__declspec(dllimport) BOOL __stdcall IsBadReadPtr(const void*,DWORD);
__declspec(dllimport) BOOL __stdcall IsBadWritePtr(void*,DWORD);
__declspec(dllimport) BOOL __stdcall HeapValidate(HANDLE,DWORD,const void*);
__declspec(dllimport) HANDLE __stdcall GetProcessHeap(void);
__declspec(dllimport) void *__stdcall HeapAlloc(HANDLE,DWORD,DWORD);
__declspec(dllimport) BOOL __stdcall HeapFree(HANDLE,DWORD,void*);
__declspec(dllimport) DWORD __stdcall VirtualQuery(const void*,DWORD*,DWORD);
__declspec(dllimport) int __stdcall GetLocaleInfoA(DWORD,DWORD,char*,int);
__declspec(dllimport) int __stdcall CompareStringA(DWORD,DWORD,const char*,int,const char*,int);
__declspec(dllimport) int __stdcall GetDateFormatA(DWORD,DWORD,const unsigned short*,const char*,char*,int);
__declspec(dllimport) int __stdcall GetTimeFormatA(DWORD,DWORD,const unsigned short*,const char*,char*,int);
__declspec(dllimport) HANDLE __stdcall FindFirstFileA(const char*,DWORD*);
__declspec(dllimport) BOOL __stdcall FindNextFileA(HANDLE,DWORD*);
__declspec(dllimport) BOOL __stdcall FindClose(HANDLE);
int __stdcall DllMain(void *module,unsigned reason,void *reserved) {(void)module;(void)reason;(void)reserved;return 1;}
void __stdcall GiveFnptrsToDll(void *functions,void *globals) {(void)functions;(void)globals;}
/* out: [0] attributes [1] size low [2] size high [3] file type [4..11] UTC [12..19] local write time */
int FixtureStat(const char *path,int backup_semantics,DWORD *out) {
    HANDLE file=CreateFileA(path,0x80,7,0,3,backup_semantics?0x02000000:0,0);
    if(file==(HANDLE)-1)return -(int)GetLastError();
    DWORD info[13];unsigned short utc[8],local[8];
    out[3]=GetFileType(file);
    BOOL ok=GetFileInformationByHandle(file,info)&&FileTimeToSystemTime(&info[5],utc)&&
            SystemTimeToTzSpecificLocalTime(0,utc,local);
    CloseHandle(file);
    if(!ok)return -(int)GetLastError();
    out[0]=info[0];out[1]=info[9];out[2]=info[8];
    for(int i=0;i<8;i++){out[4+i]=utc[i];out[12+i]=local[i];}
    return 1;
}
/* out: the eight SYSTEMTIME fields of a FILETIME */
int FixtureFileTime(DWORD low,DWORD high,DWORD *out) {
    DWORD time[2]={low,high};unsigned short fields[8];
    if(!FileTimeToSystemTime(time,fields))return -(int)GetLastError();
    for(int i=0;i<8;i++)out[i]=fields[i];
    return 1;
}
/* Returns GetFullPathNameW's result; *file_part is the index of the file name or -1. */
int FixtureFullPath(const unsigned short *name,unsigned short *out,DWORD count,int *file_part) {
    unsigned short *part=0;DWORD result=GetFullPathNameW(name,count,out,&part);
    *file_part=part?(int)(part-out):-1;
    return result?(int)result:-(int)GetLastError();
}
unsigned FixtureDriveType(const unsigned short *root) {return GetDriveTypeW(root);}
/* FMOD 3.x as a Spirit of Half-Life style client uses it (Reprocessing: PlayMP3). */
static void *fmod;static void *music;
static signed char (__stdcall *fs_init)(int,int,unsigned);
static void *(__stdcall *fs_open)(const char*,unsigned,int,int);
static int (__stdcall *fs_play)(int,void*);
static signed char (__stdcall *fs_close_stream)(void*);
static int (__stdcall *fs_time)(void*);
static signed char (__stdcall *fs_set_time)(void*,int);
static signed char (__stdcall *fs_pause)(int,signed char);
static signed char (__stdcall *fs_volume)(int,int);
static void (__stdcall *fs_close)(void);
static float (__stdcall *fs_version)(void);
/* Returns the play channel, or a negative step number on failure. */
int FixtureMusicStart(const char *library,const char *song) {
    fmod=LoadLibraryA(library);if(!fmod)return -1;
    if(GetProcAddress(fmod,"_FSOUND_NoSuchFunction@0"))return -2;
    *(void**)&fs_init=GetProcAddress(fmod,"_FSOUND_Init@12");*(void**)&fs_open=GetProcAddress(fmod,"_FSOUND_Stream_Open@16");
    *(void**)&fs_play=GetProcAddress(fmod,"_FSOUND_Stream_Play@8");*(void**)&fs_close_stream=GetProcAddress(fmod,"_FSOUND_Stream_Close@4");
    *(void**)&fs_time=GetProcAddress(fmod,"_FSOUND_Stream_GetTime@4");*(void**)&fs_set_time=GetProcAddress(fmod,"_FSOUND_Stream_SetTime@8");
    *(void**)&fs_pause=GetProcAddress(fmod,"_FSOUND_SetPaused@8");*(void**)&fs_volume=GetProcAddress(fmod,"_FSOUND_SetVolume@8");
    *(void**)&fs_close=GetProcAddress(fmod,"_FSOUND_Close@0");*(void**)&fs_version=GetProcAddress(fmod,"_FSOUND_GetVersion@0");
    if(!fs_init||!fs_open||!fs_play||!fs_close_stream||!fs_time||!fs_set_time||!fs_pause||!fs_volume||!fs_close||!fs_version)return -3;
    if(fs_version()<3.0f)return -4;
    if(!fs_init(44100,1,0))return -5;
    if(fs_open("no\\such\\song.mp3",0x130,0,0))return -6;
    music=fs_open(song,0x130,0,0);if(!music)return -7;
    if(!fs_volume(0,100))return -8;
    return fs_play(0,music);
}
int FixtureMusicTime(void) {return fs_time(music);}
int FixtureMusicPause(int paused) {return fs_pause(0,(signed char)paused);}
int FixtureMusicSeek(int milliseconds) {return fs_set_time(music,milliseconds);}
int FixtureMusicStop(void) {int ok=fs_close_stream(music);fs_close();return ok&&FreeLibrary(fmod);}
/* Visual C++ 2003 (debug) runtime paths of Decay. Returns 1, or the failed step. */
static const char constant[]="read-only";
static int equal(const char *a,const char *b) {while(*a&&*a==*b){a++;b++;}return *a==*b;}
int FixtureLegacy(const char *directory_pattern,const char *expected_name,char *found) {
    if(IsBadReadPtr(constant,sizeof(constant))||!IsBadWritePtr((void*)constant,1))return -1;
    if(!IsBadReadPtr(0,1)||IsBadReadPtr(0,0)||!IsBadReadPtr((void*)0x1000,4))return -2;
    HANDLE heap=GetProcessHeap();char *block=HeapAlloc(heap,0,64);
    if(!block||!HeapValidate(heap,0,block)||HeapValidate(heap,0,constant)||!HeapFree(heap,0,block))return -3;
    DWORD info[7];
    if(VirtualQuery((const void*)FixtureLegacy,info,sizeof(info))!=28)return -4;
    if(info[4]!=0x1000||info[6]!=0x1000000||!(info[5]&0x20)||!info[1]||info[1]>info[0])return -5;
    char text[64];
    if(GetLocaleInfoA(0x409,0xe,text,sizeof(text))!=2||!equal(text,"."))return -6;
    if(GetLocaleInfoA(0x400,0x1004,text,sizeof(text))!=5||!equal(text,"1252"))return -7;
    if(CompareStringA(0x409,1,"Decay",-1,"dECAY",-1)!=2||CompareStringA(0x409,0,"abc",3,"abd",3)!=1)return -8;
    unsigned short when[8]={2008,9,6,27,14,5,9,0};
    if(GetDateFormatA(0x409,0,when,"dddd',' MMM d yyyy",text,sizeof(text))!=22||!equal(text,"Saturday, Sep 27 2008"))return -9;
    if(GetTimeFormatA(0x409,0,when,0,text,sizeof(text))!=11||!equal(text,"2:05:09 PM"))return -10;
    DWORD data[80];HANDLE find=FindFirstFileA(directory_pattern,data);
    if(find==(HANDLE)-1)return -11;
    int matched=0;
    do{if(equal((char*)&data[11],expected_name)){matched=1;for(int i=0;i<260&&(found[i]=((char*)&data[11])[i]);i++);}}
    while(FindNextFileA(find,data));
    if(!FindClose(find)||!matched)return -12;
    return 1;
}
