#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include "extdll.h"
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    for(unsigned iteration=0;iteration<2;iteration++) {
        void *module=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
        if(!module){std::fprintf(stderr,"%s\n",dlerror());return 3;}
        auto path=reinterpret_cast<const char*(*)()>(dlsym(module,"HLBridge_GetDLLPath"));
        auto count=reinterpret_cast<size_t(*)()>(dlsym(module,"HLBridge_GetExportCount"));
        if(!path||!count||!std::strstr(path(),"fixture_variant.dll")||count()!=3)return 4;
        auto give=reinterpret_cast<void(*)(enginefuncs_t*,globalvars_t*)>(dlsym(module,"GiveFnptrsToDll"));
        auto modern=reinterpret_cast<int(*)(DLL_FUNCTIONS*,int*)>(dlsym(module,"GetEntityAPI2"));
        auto legacy=reinterpret_cast<int(*)(DLL_FUNCTIONS*,int)>(dlsym(module,"GetEntityAPI"));
        auto extra=reinterpret_cast<int(*)(NEW_DLL_FUNCTIONS*,int*)>(dlsym(module,"GetNewDLLFunctions"));
        if(!give||!modern||!legacy||!extra)return 5;
        enginefuncs_t functions{};globalvars_t globals{};give(&functions,&globals);
        DLL_FUNCTIONS table{};int version=139;
        if(modern(&table,&version)!=0||version!=140)return 6;
        if(modern(&table,&version)!=1||!table.pfnGetGameDescription||std::strcmp(table.pfnGetGameDescription(),"BridgeFixture"))return 7;
        if(legacy(&table,140)!=1)return 8;
        NEW_DLL_FUNCTIONS extensions{};int extra_version=0;
        if(extra(&extensions,&extra_version)!=0||extra_version!=1||extra(&extensions,&extra_version)!=1)return 9;
        const char *names[]={"CompletelyNewEntityWithMixedCASE","AnotherUnlistedEntity","fixture.entity-with-dash"};
        for(unsigned i=0;i<3;i++) {
            auto factory=reinterpret_cast<void(*)(entvars_t*)>(dlsym(module,names[i]));
            if(!factory)return 10;
            entvars_t entity{};factory(&entity);
            if(entity.classname!=static_cast<int>(0x4150+i*0x100))return 11;
        }
        if(dlsym(module,"FixtureData")||dlsym(module,"worldspawn"))return 12;
        std::printf("PASS: renamed .so -> liblist Windows path -> another PE DLL; 3 new exports callable; legacy/new API; unload/reload=%u\n",iteration);
        dlclose(module);
    }
    return 0;
}
