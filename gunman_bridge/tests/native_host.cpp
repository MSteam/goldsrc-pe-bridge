/* Headless integration host for the installed engine's dedicated-server interface.
 * Its vtable/signatures were checked against this engine's DWARF and disassembly.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <dlfcn.h>
#include "FileSystem.h"
#include "extdll.h"
class IDedicatedServerAPI : public IBaseInterface {
public:
    virtual bool Init(char*,char*,CreateInterfaceFn,CreateInterfaceFn)=0;
    virtual int Shutdown()=0;
    virtual bool RunFrame()=0;
    virtual void AddConsoleText(char*)=0;
    virtual void UpdateStatus(float*,int*,int*,char*)=0;
};
class DedicatedExports : public IBaseInterface {
public:
    virtual void Sys_Printf(const char *text) {std::fputs(text,stdout);std::fflush(stdout);}
};
static DedicatedExports exports;
static void *launcher_factory(const char *name,int *code) {
    if(!std::strcmp(name,"VENGINE_DEDICATEDEXPORTS_API_VERSION001")) {
        if(code)*code=0;return &exports;
    }
    std::fprintf(stderr,"[host] unknown launcher interface: %s\n",name);
    if(code)*code=1;return nullptr;
}
static void *module(const std::string &filename) {
    void *handle=dlopen(filename.c_str(),RTLD_NOW|RTLD_GLOBAL);
    if(!handle){std::fprintf(stderr,"[host] dlopen %s: %s\n",filename.c_str(),dlerror());std::exit(2);}
    return handle;
}
int main(int argc,char **argv) {
    setvbuf(stdout,nullptr,_IONBF,0);
    if(argc<3){std::fprintf(stderr,"usage: native_host GAME_ROOT TEST_BASEDIR [frames] [map]\n");return 2;}
    std::string root=argv[1];
    auto filesystem_factory=reinterpret_cast<CreateInterfaceFn>(dlsym(module(root+"/filesystem_stdio.so"),"CreateInterface"));
    if(!filesystem_factory)return 3;
    auto filesystem=static_cast<IFileSystem*>(filesystem_factory(FILESYSTEM_INTERFACE_VERSION,nullptr));
    if(!filesystem)return 4;
    filesystem->Mount();filesystem->AddSearchPath(root.c_str(),"BIN");
    auto engine_factory=reinterpret_cast<CreateInterfaceFn>(dlsym(module(root+"/engine_i486.so"),"CreateInterface"));
    if(!engine_factory)return 5;
    auto server=static_cast<IDedicatedServerAPI*>(engine_factory("VENGINE_HLDS_API_VERSION002",nullptr));
    if(!server)return 6;
    char command[1024];
    std::snprintf(command,sizeof(command),"bridge_host -game rewolf -console -insecure -nomaster -nobreakpad -ip 127.0.0.1 -port 27960 +maxplayers 1 +sv_lan 1 +deathmatch 0 +map %s",argc>=5?argv[4]:"city1a");
    std::fprintf(stderr,"[host] dedicated Init: %s\n",command);
    if(!server->Init(argv[2],command,launcher_factory,filesystem_factory))return 7;
    edict_t *test_client=nullptr;
    const enginefuncs_t *functions=nullptr;
    Vector initial_position;
    if(std::getenv("GB_TEST_CLIENT")) {
        std::string wrapper=std::string(argv[2])+"/rewolf/dlls/gunman.so";
        void *handle=dlopen(wrapper.c_str(),RTLD_NOW|RTLD_NOLOAD);
        if(!handle)return 11;
        auto native_functions=reinterpret_cast<const enginefuncs_t*(*)()>(dlsym(handle,"GunmanBridge_GetNativeEngineFunctions"));
        auto entity_api=reinterpret_cast<int(*)(DLL_FUNCTIONS*,int*)>(dlsym(handle,"GetEntityAPI2"));
        if(!native_functions||!entity_api)return 12;
        functions=native_functions();DLL_FUNCTIONS callbacks{};int version=140;
        if(!functions||!entity_api(&callbacks,&version))return 13;
        test_client=functions->pfnCreateFakeClient("bridge_test");
        if(!test_client)return 14;
        char reason[128]={};
        if(!callbacks.pfnClientConnect(test_client,"bridge_test","127.0.0.1",reason)) {
            std::fprintf(stderr,"[host] test client rejected: %s\n",reason);return 15;
        }
        callbacks.pfnClientPutInServer(test_client);
        initial_position=test_client->v.origin;
        std::printf("[host] fake client spawned: health=%.1f position=%.1f %.1f %.1f\n",
            test_client->v.health,test_client->v.origin.x,test_client->v.origin.y,test_client->v.origin.z);
        dlclose(handle);
    }
    int frames=argc>=4?std::atoi(argv[3]):500;
    for(int i=0;i<frames;i++) {
        if(argc>=6 && i==frames/2) {
            char transition[128];std::snprintf(transition,sizeof(transition),"changelevel %s\n",argv[5]);
            std::fprintf(stderr,"[host] %s",transition);server->AddConsoleText(transition);
        }
        if(!server->RunFrame()){std::fprintf(stderr,"[host] RunFrame stopped at %d\n",i);return 8;}
        if(test_client) {
            const float angles[3]={0,0,0};
            functions->pfnRunPlayerMove(test_client,angles,50,0,0,i%50==0?IN_ATTACK:0,0,10);
        }
        usleep(10000);
    }
    float fps=0;int clients=0,max_clients=0;char map[64]={};
    server->UpdateStatus(&fps,&clients,&max_clients,map);
    std::printf("[host] completed frames=%d map=%s clients=%d/%d fps=%.1f\n",frames,map,clients,max_clients,fps);
    if(!*map)return 9;
    if(std::strcmp(map,argc>=6?argv[5]:(argc>=5?argv[4]:"city1a")))return 10;
    if(test_client)std::printf("[host] fake client final: health=%.1f position=%.1f %.1f %.1f\n",
        test_client->v.health,test_client->v.origin.x,test_client->v.origin.y,test_client->v.origin.z);
    if(test_client && ((test_client->v.origin-initial_position).Length()<0.01f || test_client->v.health<=0))return 16;
    server->Shutdown();filesystem->Unmount();return 0;
}
