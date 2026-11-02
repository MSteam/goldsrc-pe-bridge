#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include "extdll.h"
#include "runtime.h"
static_assert(sizeof(void*)==4);
static_assert(sizeof(DLL_FUNCTIONS)==200);
static_assert(sizeof(globalvars_t)==172);
static_assert(offsetof(edict_t,v)==128);
static_assert(offsetof(edict_t,pvPrivateData)==124);
static_assert(offsetof(entvars_t,pContainingEntity)==520);
[[noreturn]] static void forbidden_engine_call() {
    std::fprintf(stderr,"[test] unexpected engine callback during handshake\n");std::abort();
}
int main(int argc,char **argv) {
    setvbuf(stdout,nullptr,_IONBF,0);
    if(argc!=2)return 2;
    if(gb_load(argv[1]))return 3;
    auto give=reinterpret_cast<void(__attribute__((stdcall)) *)(enginefuncs_t*,globalvars_t*)>(gb_symbol("GiveFnptrsToDll"));
    auto get=reinterpret_cast<int(*)(DLL_FUNCTIONS*,int*)>(gb_symbol("GetEntityAPI2"));
    auto get1=reinterpret_cast<int(*)(DLL_FUNCTIONS*,int)>(gb_symbol("GetEntityAPI"));
    if(!give||!get||!get1)return 4;
    enginefuncs_t engine{};
    for(size_t i=0;i<sizeof(engine)/sizeof(void*);i++)
        reinterpret_cast<void(**)()>(&engine)[i]=forbidden_engine_call;
    globalvars_t globals{};
    give(&engine,&globals);
    struct guarded {uint32_t before;DLL_FUNCTIONS table;uint32_t after;};
    guarded result{0x13572468,{},0x87654321};
    int version=139;
    if(get(&result.table,&version)!=0||version!=140)return 5;
    version=140;
    if(get(&result.table,&version)!=1)return 6;
    if(result.before!=0x13572468||result.after!=0x87654321)return 7;
    void **callbacks=reinterpret_cast<void**>(&result.table);
    size_t count=0;
    for(size_t i=0;i<sizeof(result.table)/sizeof(void*);i++) {
        if(callbacks[i]&&!gb_contains(callbacks[i]))return 8;
        count+=callbacks[i]!=nullptr;
    }
    DLL_FUNCTIONS second{};
    if(get1(&second,139)!=0||get1(&second,140)!=1||std::memcmp(&second,&result.table,sizeof(second)))return 9;
    if(get(nullptr,&version)!=0)return 10;
    const char *description=result.table.pfnGetGameDescription();
    std::printf("DLL game description: %s\n",description?description:"<null>");
    if(!description||!*description)return 11;
    std::printf("PASS: API=%d callbacks=%zu description=%s guards=intact\n",version,count,description);
    gb_unload();
}
