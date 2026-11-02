// Generic headless start of any PE server DLL through bridge.so.
// The bridge selects the DLL from the liblist.gam above it, as in the game.
// Every engine callback this mock does not implement is a named trap.
//   CB_ASSET_ROOT=<mod> [CB_FILESYSTEM_SO=.../filesystem_stdio.so] server_smoke <mod>/dlls/bridge.so
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <map>
#include <string>
#include <fstream>
#include <vector>
#include <utility>
#include <dlfcn.h>
#include "extdll.h"
#include "FileSystem.h"
#include "engine_slot_names.inc"
static_assert(sizeof(enginefuncs_t)/sizeof(void*)==sizeof(engine_slot_names)/sizeof(*engine_slot_names));
static const char *stage="load";
[[noreturn]] static void trapped(const char *name) {
    std::fprintf(stderr,"[server smoke] FAIL stage=%s: unimplemented engine callback %s\n",stage,name);
    std::fflush(nullptr);std::_Exit(20);
}
template<size_t I> static void engine_trap() {trapped(engine_slot_names[I]);}
template<size_t... I> static void fill_engine(enginefuncs_t &engine,std::index_sequence<I...>) {
    void (*slots[])()={engine_trap<I>...};std::memcpy(&engine,slots,sizeof(engine));
}
static std::string asset_root;
static std::map<std::string,cvar_t*> cvars;
static std::vector<std::string> server_commands;
static unsigned precached,commands_added;
static void cvar_register(cvar_t *var) {cvars[var->name]=var;}
static cvar_t *cvar_pointer(const char *name) {auto it=cvars.find(name);return it==cvars.end()?nullptr:it->second;}
static float cvar_float(const char *name) {auto v=cvar_pointer(name);return v?v->value:0;}
static const char *cvar_string(const char *name) {auto v=cvar_pointer(name);return v?v->string:"";}
static void cvar_set_float(const char *name,float value) {if(auto v=cvar_pointer(name))v->value=value;}
static void cvar_set_string(const char *name,const char *value) {if(auto v=cvar_pointer(name)){v->value=atof(value);}}
static void alert(ALERT_TYPE,char *format,...) {
    va_list args;va_start(args,format);std::printf("[alert] ");std::vprintf(format,args);va_end(args);
}
static void engine_fprintf(void*,char *format,...) {va_list args;va_start(args,format);std::vprintf(format,args);va_end(args);}
static void server_command(char *text) {server_commands.push_back(text);std::printf("[server command] %s",text);}
static void server_execute() {}
static void add_server_command(char*,void(*)()) {commands_added++;}
static void server_print(const char *text) {std::printf("[server] %s",text);}
static void game_dir(char *out) {std::strcpy(out,asset_root.substr(asset_root.find_last_of('/')+1).c_str());}
static byte *load_file(char *name,int *length) {
    std::ifstream file(asset_root+"/"+name,std::ios::binary);
    if(!file){if(length)*length=0;return nullptr;}
    std::vector<char> bytes{std::istreambuf_iterator<char>(file),{}};
    auto out=static_cast<byte*>(std::malloc(bytes.size()+1));std::memcpy(out,bytes.data(),bytes.size());out[bytes.size()]=0;
    if(length)*length=bytes.size();return out;
}
static void free_file(void *p) {std::free(p);}
static int check_parameter(const char*,char**) {return 0;}
static int precache(char*) {return ++precached;}
static unsigned short precache_event(int,const char*) {return ++precached;}
static int is_dedicated() {return 0;}
static float engine_time() {return 1.0f;}
static int reg_user_msg(const char*,int) {static int id=64;return ++id;}
static std::vector<std::string> strings{""};
static int alloc_string(const char *text) {strings.emplace_back(text);return static_cast<int>(strings.size()-1)<<1|1;}
static const char *string_from_index(int index) {return strings.at(index>>1).c_str();}
int main(int argc,char **argv) {
    setvbuf(stdout,nullptr,_IONBF,0);
    if(argc!=2)return 2;
    const char *root=getenv("CB_ASSET_ROOT");if(!root){fprintf(stderr,"CB_ASSET_ROOT required\n");return 2;}
    asset_root=root;
    if(const char *native_fs=getenv("CB_FILESYSTEM_SO")) {
        void *library=dlopen(native_fs,RTLD_NOW|RTLD_GLOBAL);if(!library){fprintf(stderr,"%s\n",dlerror());return 3;}
        auto factory=reinterpret_cast<void*(*)(const char*,int*)>(dlsym(library,"CreateInterface"));
        auto fs=factory?static_cast<IFileSystem*>(factory(FILESYSTEM_INTERFACE_VERSION,nullptr)):nullptr;
        if(!fs)return 3;
        std::string base=asset_root.substr(0,asset_root.find_last_of('/'));
        fs->Mount();
        fs->AddSearchPath(asset_root.c_str(),"GAME");fs->AddSearchPath(asset_root.c_str(),"GAMECONFIG");
        fs->AddSearchPath((base+"/valve").c_str(),"DEFAULTGAME");fs->AddSearchPath(base.c_str(),"ROOT");
    }
    void *module=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if(!module){fprintf(stderr,"[server smoke] FAIL stage=load: %s\n",dlerror());return 3;}
    enginefuncs_t engine;fill_engine(engine,std::make_index_sequence<sizeof(enginefuncs_t)/sizeof(void*)>{});
    engine.pfnCVarRegister=cvar_register;engine.pfnCVarGetPointer=cvar_pointer;engine.pfnCVarGetFloat=cvar_float;
    engine.pfnCVarGetString=cvar_string;engine.pfnCVarSetFloat=cvar_set_float;engine.pfnCVarSetString=cvar_set_string;
    engine.pfnAlertMessage=alert;engine.pfnEngineFprintf=engine_fprintf;engine.pfnServerCommand=server_command;
    engine.pfnServerExecute=server_execute;engine.pfnAddServerCommand=add_server_command;engine.pfnServerPrint=server_print;
    engine.pfnGetGameDir=game_dir;engine.pfnLoadFileForMe=load_file;engine.pfnFreeFile=free_file;
    engine.pfnCheckParm=check_parameter;engine.pfnPrecacheModel=precache;engine.pfnPrecacheSound=precache;
    engine.pfnPrecacheEvent=precache_event;engine.pfnIsDedicatedServer=is_dedicated;engine.pfnTime=engine_time;
    engine.pfnRegUserMsg=reg_user_msg;engine.pfnAllocString=alloc_string;engine.pfnSzFromIndex=string_from_index;
    static globalvars_t globals{};static char string_base[1]{};
    globals.pStringBase=string_base;globals.maxClients=1;globals.deathmatch=0;
    auto symbol=[&](const char *name) {void *p=dlsym(module,name);if(!p){fprintf(stderr,"[server smoke] bridge lacks %s\n",name);std::_Exit(4);}return p;};
    stage="GiveFnptrsToDll";
    reinterpret_cast<void(*)(enginefuncs_t*,globalvars_t*)>(symbol("GiveFnptrsToDll"))(&engine,&globals);
    stage="GetEntityAPI2";
    DLL_FUNCTIONS functions{};int version=INTERFACE_VERSION;
    if(reinterpret_cast<int(*)(DLL_FUNCTIONS*,int*)>(symbol("GetEntityAPI2"))(&functions,&version)!=1) {
        fprintf(stderr,"[server smoke] FAIL stage=GetEntityAPI2: version=%d\n",version);return 5;
    }
    NEW_DLL_FUNCTIONS new_functions{};int new_version=NEW_DLL_FUNCTIONS_VERSION;
    stage="GetNewDLLFunctions";
    int has_new=reinterpret_cast<int(*)(NEW_DLL_FUNCTIONS*,int*)>(symbol("GetNewDLLFunctions"))(&new_functions,&new_version);
    printf("[server smoke] PASS API: description=%s new_functions=%d\n",functions.pfnGetGameDescription(),has_new);
    stage="GameInit";functions.pfnGameInit();
    printf("[server smoke] PASS GameInit: %zu cvars, %u server commands, %zu command lines\n",cvars.size(),commands_added,server_commands.size());
    return 0;
}
