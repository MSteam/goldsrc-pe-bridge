#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <dlfcn.h>
#include <sys/mman.h>
#include "extdll.h"
static uint32 expected_function;
static unsigned lookup_calls;
extern "C" __attribute__((naked,noinline)) int alignment_probe(char*) {
    __asm__ volatile("movl %esp,%eax; addl $4,%eax; andl $15,%eax; sete %al; movzbl %al,%eax; ret");
}
extern "C" __attribute__((naked,noinline)) int skewed_call(void*,char*) {
    __asm__ volatile("movl 4(%esp),%eax; movl 8(%esp),%edx; pushl %edx; call *%eax; addl $4,%esp; ret");
}
[[noreturn]] static void forbidden() {std::fprintf(stderr,"unexpected engine callback\n");std::abort();}
static uint32 lookup(const char *name) {
    if(std::strcmp(name,"worldspawn"))std::abort();lookup_calls++;return expected_function;
}
static const char *reverse_lookup(uint32 address) {
    if(address!=expected_function)std::abort();lookup_calls++;return "worldspawn";
}
int main(int argc,char **argv) {
    setvbuf(stdout,nullptr,_IONBF,0);if(argc<2||argc>3)return 2;
    void *reservation=nullptr;
    if(argc==3) {
        reservation=mmap(reinterpret_cast<void*>(0x10000000),0x165000,PROT_NONE,
            MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
        if(reservation==MAP_FAILED)return 12;
    }
    void *module=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if(!module){std::fprintf(stderr,"dlopen: %s\n",dlerror());return 3;}
    auto give=reinterpret_cast<void(*)(enginefuncs_t*,globalvars_t*)>(dlsym(module,"GiveFnptrsToDll"));
    auto get=reinterpret_cast<int(*)(DLL_FUNCTIONS*,int*)>(dlsym(module,"GetEntityAPI2"));
    if(!give||!get||!dlsym(module,"worldspawn")||!dlsym(module,"weapon_gausspistol"))return 4;
    // These factories are used by the real mod and require their exact spelling.
    // Omitting them suppresses weapon customization and shifts ammo registration.
    const char *mixed_case_entities[]={"cust_1GaussPistolFireType","cust_2ShotgunShells",
        "weapon_SPchemicalgun","weapon_dmlGrenade","ammo_minigunClip"};
    for(const char *name:mixed_case_entities)if(!dlsym(module,name)) {
        std::fprintf(stderr,"missing entity factory: %s\n",name);return 19;
    }
    std::printf("PASS: mixed-case weapon/customization/ammo factories exported\n");
    enginefuncs_t engine{};
    for(size_t i=0;i<sizeof(engine)/4;i++)reinterpret_cast<void(**)()>(&engine)[i]=forbidden;
    engine.pfnFunctionFromName=lookup;engine.pfnNameForFunction=reverse_lookup;
    engine.pfnPrecacheModel=alignment_probe;
    globalvars_t globals{};give(&engine,&globals);
    auto pe_functions=reinterpret_cast<const enginefuncs_t*(*)()>(dlsym(module,"GunmanBridge_GetPEEngineFunctions"));
    if(!pe_functions)return 17;
    char model[]="alignment_test";
    if(skewed_call(reinterpret_cast<void*>(alignment_probe),model)!=0 ||
       skewed_call(reinterpret_cast<void*>(pe_functions()->pfnPrecacheModel),model)!=1)return 18;
    std::printf("PASS: misaligned Windows caller -> aligned Linux callback\n");
    DLL_FUNCTIONS table{};int version=140;
    if(get(&table,&version)!=1||!table.pfnGetGameDescription)return 5;
    const char *description=table.pfnGetGameDescription();
    if(std::strcmp(description,"Gunman")||lookup_calls)return 6;
    auto pe_symbol=reinterpret_cast<void*(*)(const char*)>(dlsym(module,"gb_symbol"));
    if(!pe_symbol)return 13;
    struct record {int number;float scalar;uint32 function;};
    record input{0x12345678,17.25f,reinterpret_cast<uintptr_t>(pe_symbol("worldspawn"))},output{};
    if(reservation && input.function>=0x10000000 && input.function<0x10165000)return 14;
    TYPEDESCRIPTION fields[]={
        {FIELD_INTEGER,const_cast<char*>("number"),offsetof(record,number),1,0},
        {FIELD_FLOAT,const_cast<char*>("scalar"),offsetof(record,scalar),1,0},
        {FIELD_FUNCTION,const_cast<char*>("function"),offsetof(record,function),1,0}
    };
    char buffer[2048]={};char *tokens[4096]={};SAVERESTOREDATA state{};
    state.pBaseData=buffer;state.pCurrentData=buffer;state.bufferSize=sizeof(buffer);
    state.tokenCount=4096;state.pTokens=tokens;
    table.pfnSaveWriteFields(&state,"bridge_record",&input,fields,3);
    int written=state.size;
    if(written<=0||written>static_cast<int>(sizeof(buffer)))return 15;
    state.pCurrentData=buffer;state.size=0;state.bufferSize=written;
    table.pfnSaveReadFields(&state,"bridge_record",&output,fields,3);
    if(std::memcmp(&input,&output,sizeof(input))||lookup_calls)return 16;
    std::printf("PASS: save/restore fields bytes=%d original PE function=%#x preserved\n",written,input.function);
    std::printf("PASS: ELF32 wrapper -> PE32 API=%d description=%s factories=exported\n",version,description);
    dlclose(module);
    if(reservation)munmap(reservation,0x165000);
    return 0;
}
