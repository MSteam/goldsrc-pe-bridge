// Checks winapi_fixture.dll through a renamed bridge.so against Linux stat().
//   winapi_probe <mod>/dlls/winapi_bridge.so <regular file> <directory>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/stat.h>
using Stat=int(*)(const char*,int,unsigned*);
using FileTime=int(*)(unsigned,unsigned,unsigned*);
using FullPath=int(*)(const char16_t*,char16_t*,unsigned,int*);
using DriveType=unsigned(*)(const char16_t*);
static std::u16string wide(const std::string &text) {return std::u16string(text.begin(),text.end());}
static std::string ascii(const char16_t *text) {std::string out;while(*text)out+=static_cast<char>(*text++);return out;}
static std::string windows(const char *path) {std::string out=path;for(char &c:out)if(c=='/')c='\\';return out;}
#define CHECK(condition,...) do{if(!(condition)){std::fprintf(stderr,"FAIL: " __VA_ARGS__);std::fputc('\n',stderr);return 1;}}while(0)
int main(int argc,char **argv) {
    if(argc!=4)return 2;
    void *module=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if(!module){std::fprintf(stderr,"%s\n",dlerror());return 3;}
    auto stat_pe=reinterpret_cast<Stat>(dlsym(module,"FixtureStat"));
    auto filetime=reinterpret_cast<FileTime>(dlsym(module,"FixtureFileTime"));
    CHECK(stat_pe&&filetime,"fixture exports not published");
    unsigned out[20];struct stat st;
    CHECK(!stat(argv[2],&st),"stat %s",argv[2]);
    CHECK(stat_pe(windows(argv[2]).c_str(),0,out)==1,"metadata handle for a file");
    struct tm local;localtime_r(&st.st_mtime,&local);
    CHECK(!(out[0]&0x10)&&out[1]==static_cast<unsigned>(st.st_size)&&out[2]==0&&out[3]==1,
        "file attributes=%#x size=%u type=%u",out[0],out[1],out[3]);
    CHECK(out[12]==static_cast<unsigned>(local.tm_year+1900)&&out[13]==static_cast<unsigned>(local.tm_mon+1)&&
          out[15]==static_cast<unsigned>(local.tm_mday)&&out[16]==static_cast<unsigned>(local.tm_hour)&&
          out[17]==static_cast<unsigned>(local.tm_min)&&out[18]==static_cast<unsigned>(local.tm_sec)&&
          out[14]==static_cast<unsigned>(local.tm_wday),
        "local write time %u-%u-%u %u:%u:%u",out[12],out[13],out[15],out[16],out[17],out[18]);
    CHECK(stat_pe(windows(argv[3]).c_str(),1,out)==1&&(out[0]&0x10),"directory with FILE_FLAG_BACKUP_SEMANTICS");
    CHECK(stat_pe(windows(argv[3]).c_str(),0,out)==-5,"directory without backup semantics must be ERROR_ACCESS_DENIED");
    CHECK(stat_pe((windows(argv[3])+"\\missing.file").c_str(),0,out)==-2,"missing file must be ERROR_FILE_NOT_FOUND");
    // Beyond 32-bit time_t in both directions; weekday included.
    CHECK(filetime(0,0,out)==1&&out[0]==1601&&out[1]==1&&out[3]==1&&out[2]==1,"FILETIME 0");
    CHECK(filetime(0xee617c50u,0x022f71ccu,out)==1&&out[0]==2100&&out[1]==1&&out[2]==5&&out[3]==1&&
          out[4]==12&&out[5]==34&&out[6]==56&&out[7]==789,"FILETIME 2100-01-01 12:34:56.789 Friday");
    CHECK(filetime(0x75258000u,0x006f2c3au,out)==1&&out[0]==1700&&out[1]==3&&out[3]==1&&out[2]==1,"FILETIME 1700-03-01 Monday");
    CHECK(filetime(0,0x80000000u,out)==-87,"FILETIME above the valid range");
    auto full=reinterpret_cast<FullPath>(dlsym(module,"FixtureFullPath"));
    auto drive=reinterpret_cast<DriveType>(dlsym(module,"FixtureDriveType"));
    CHECK(full&&drive,"path exports not published");
    CHECK(!chdir(argv[3]),"chdir");
    std::string cwd=windows(argv[3]);char16_t buffer[4096];int part;
    auto expect=[&](const std::string &name,const std::string &expected,int expected_part) {
        auto input=wide(name);
        int needed=full(input.c_str(),buffer,0,&part);
        if(needed!=static_cast<int>(expected.size()+1))return false;      // size query, terminator included
        int length=full(input.c_str(),buffer,4096,&part);
        return length==static_cast<int>(expected.size())&&ascii(buffer)==expected&&part==expected_part;
    };
    CHECK(expect("a\\.\\b\\..\\c.txt",cwd+"\\a\\c.txt",static_cast<int>(cwd.size()+3)),"relative path");
    CHECK(expect("\\x\\y\\..\\z.bsp","\\x\\z.bsp",3),"rooted path with ..");
    CHECK(expect("/x//y/","\\x\\y\\",-1),"forward slashes, trailing separator");
    CHECK(expect("\\..\\..\\x","\\x",1),".. above the root");
    CHECK(drive(nullptr)==3&&drive(wide(cwd).c_str())==3&&drive(u"\\no\\such\\dir")==1,"GetDriveTypeW");
    // Visual C++ 2003 debug runtime: pointer and heap checks, en-US locale, FindFirstFileA.
    auto legacy=reinterpret_cast<int(*)(const char*,const char*,char*)>(dlsym(module,"FixtureLegacy"));
    CHECK(legacy,"legacy export not published");
    char found[260]={0};int step=legacy((windows(argv[3])+"\\WINAPI_*.DLL").c_str(),"winapi_fixture.dll",found);
    CHECK(step==1,"legacy CRT step %d",step);
    // Virtual fmod.dll: time advances while playing, stops while paused, seeks.
    if(const char *song=getenv("CB_TEST_MP3")) {
        auto start=reinterpret_cast<int(*)(const char*,const char*)>(dlsym(module,"FixtureMusicStart"));
        auto now=reinterpret_cast<int(*)()>(dlsym(module,"FixtureMusicTime"));
        auto pause=reinterpret_cast<int(*)(int)>(dlsym(module,"FixtureMusicPause"));
        auto seek=reinterpret_cast<int(*)(int)>(dlsym(module,"FixtureMusicSeek"));
        auto stop=reinterpret_cast<int(*)()>(dlsym(module,"FixtureMusicStop"));
        CHECK(start&&now&&pause&&seek&&stop,"music exports not published");
        int channel=start("fmod.dll",windows(song).c_str());
        CHECK(channel==0,"FMOD start step %d",channel);
        auto wait=[](int ms){timespec t{ms/1000,(ms%1000)*1000000L};nanosleep(&t,nullptr);};
        wait(600);int played=now();
        CHECK(played>200&&played<1500,"playing time %d ms after 600 ms",played);
        CHECK(pause(1)==1,"pause");wait(150);int paused=now();wait(400);
        CHECK(now()==paused,"time advanced while paused: %d -> %d",paused,now());
        CHECK(pause(0)==1,"resume");wait(400);
        CHECK(now()>paused+150,"time after resume %d (paused at %d)",now(),paused);
        CHECK(seek(20000)==1,"seek");wait(200);int sought=now();
        CHECK(sought>=20000&&sought<21000,"time after seek to 20 s: %d",sought);
        CHECK(stop()==1,"stop");
        std::printf("PASS: virtual fmod.dll: libmpg123 + SDL2 stream plays, pauses, resumes, seeks (%s)\n",song);
    } else std::printf("SKIP: virtual fmod.dll (no CB_TEST_MP3)\n");
    std::printf("PASS: UCRT stat path: metadata handles for files/directories, Windows errors, FILETIME/SYSTEMTIME 1601-2100, local time, GetFullPathNameW, GetDriveTypeW, VC2003 debug CRT pointer/heap/locale/FindFirstFile APIs\n");
    dlclose(module);return 0;
}
