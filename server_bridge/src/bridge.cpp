#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <cstdarg>
#include <dlfcn.h>
#include <exception>
#include <vector>
#include "config.h"
#include "elf_exports.h"
#include "extdll.h"
#include "pm_defs.h"
#include "runtime.h"
#define EXPORT extern "C" __attribute__((visibility("default")))
static_assert(sizeof(DLL_FUNCTIONS)==200);
static_assert(sizeof(globalvars_t)==172);
static_assert(offsetof(edict_t,v)==128);
static_assert(offsetof(edict_t,pvPrivateData)==124);
static_assert(offsetof(entvars_t,pContainingEntity)==520);
static_assert(offsetof(enginefuncs_t,pfnFunctionFromName)==312);
static_assert(offsetof(enginefuncs_t,pfnNameForFunction)==316);
static enginefuncs_t engine_original;
static enginefuncs_t engine_pe;
/* MSVC callers guarantee only four-byte stack alignment. Every transition back
 * into native code needs an aligned entry before it can call Linux/Steam/SSE. */
template<typename Function,auto Member> struct EngineThunk;
template<typename Result,typename... Args,auto Member>
struct EngineThunk<Result(*)(Args...),Member> {
    __attribute__((force_align_arg_pointer,noinline)) static Result call(Args... args) {
        return (engine_original.*Member)(args...);
    }
};
static char *format_message(const char *format,va_list args) {
    va_list copy;va_copy(copy,args);
    int size=std::vsnprintf(nullptr,0,format,copy);va_end(copy);
    if(size<0)std::abort();
    char *buffer=static_cast<char*>(std::malloc(static_cast<size_t>(size)+1));
    if(!buffer)std::abort();
    std::vsnprintf(buffer,static_cast<size_t>(size)+1,format,args);return buffer;
}
__attribute__((force_align_arg_pointer,noinline))
static void aligned_client_command(edict_t *entity,char *format,...) {
    va_list args;va_start(args,format);char *message=format_message(format,args);va_end(args);
    engine_original.pfnClientCommand(entity,const_cast<char*>("%s"),message);std::free(message);
}
__attribute__((force_align_arg_pointer,noinline))
static void aligned_alert(ALERT_TYPE type,char *format,...) {
    va_list args;va_start(args,format);char *message=format_message(format,args);va_end(args);
    engine_original.pfnAlertMessage(type,const_cast<char*>("%s"),message);std::free(message);
}
__attribute__((force_align_arg_pointer,noinline))
static void aligned_engine_fprintf(void *file,char *format,...) {
    va_list args;va_start(args,format);char *message=format_message(format,args);va_end(args);
    engine_original.pfnEngineFprintf(file,const_cast<char*>("%s"),message);std::free(message);
}
static bool initialized;
static bool pe_loaded;
static char selected_dll[4096];
static size_t published_exports;
static DLL_FUNCTIONS original_callbacks;
static unsigned game_inits,spawns,thinks,keyvalues,activations,frames,factories;
static unsigned movement_calls;
static decltype(playermove_t::PM_PlayerTrace) native_player_trace;
static decltype(playermove_t::PM_PlayerTraceEx) native_player_trace_ex;
/* GCC i386 normally pops the hidden structure-result pointer; MSVC leaves it
 * to the caller. These two callback adapters preserve the Windows convention. */
static pmtrace_t __attribute__((callee_pop_aggregate_return(0)))
windows_player_trace(float *start,float *end,int flags,int ignore) {
    return native_player_trace(start,end,flags,ignore);
}
static pmtrace_t __attribute__((callee_pop_aggregate_return(0)))
windows_player_trace_ex(float *start,float *end,int flags,int(*ignore)(physent_t*)) {
    return native_player_trace_ex(start,end,flags,ignore);
}
static void adapt_movement(playermove_t *move,bool initialize,int server) {
    auto previous_trace=native_player_trace;
    auto previous_trace_ex=native_player_trace_ex;
    auto incoming_trace=move->PM_PlayerTrace;
    auto incoming_trace_ex=move->PM_PlayerTraceEx;
    if(reinterpret_cast<void*>(incoming_trace)!=reinterpret_cast<void*>(windows_player_trace))native_player_trace=incoming_trace;
    if(reinterpret_cast<void*>(incoming_trace_ex)!=reinterpret_cast<void*>(windows_player_trace_ex))native_player_trace_ex=incoming_trace_ex;
    move->PM_PlayerTrace=reinterpret_cast<decltype(move->PM_PlayerTrace)>(windows_player_trace);
    move->PM_PlayerTraceEx=reinterpret_cast<decltype(move->PM_PlayerTraceEx)>(windows_player_trace_ex);
    if(initialize)original_callbacks.pfnPM_Init(move);
    else{movement_calls++;original_callbacks.pfnPM_Move(move,server);}
    move->PM_PlayerTrace=incoming_trace;
    move->PM_PlayerTraceEx=incoming_trace_ex;
    native_player_trace=previous_trace;
    native_player_trace_ex=previous_trace_ex;
}
static void movement_init(playermove_t *move) {adapt_movement(move,true,0);}
static void movement(playermove_t *move,qboolean server) {adapt_movement(move,false,server);}
[[noreturn]] static void fatal(const char *message) {
    std::fprintf(stderr,"[bridge] FATAL: %s\n",message);gb_report();std::abort();
}
__attribute__((constructor)) static void prepare_exports() {
    try {
        std::string filename=bridge_dll_path(reinterpret_cast<void*>(&prepare_exports));
        if(filename.size()>=sizeof(selected_dll))fatal("DLL path too long");
        std::memcpy(selected_dll,filename.c_str(),filename.size()+1);
        if(gb_load(selected_dll))fatal("Cannot load the Windows DLL selected by liblist.gam");
        pe_loaded=true;
        if(!gb_symbol("GiveFnptrsToDll"))fatal("Missing GiveFnptrsToDll export");
        std::vector<BridgeExport> exports;
        auto collect=[](const char *name,void *target,int executable,void *context) {
            if(executable)static_cast<std::vector<BridgeExport>*>(context)->push_back({name,target});
            return 0;
        };
        if(gb_visit_exports(collect,&exports))fatal("Cannot enumerate Windows DLL exports");
        published_exports=bridge_publish_exports(exports.data(),exports.size(),&factories);
        std::fprintf(stderr,"[bridge] selected DLL: %s\n[bridge] runtime ELF exports: %zu of %zu code exports\n",
            selected_dll,published_exports,exports.size());
    } catch(const std::exception &error) {fatal(error.what());}
}
static uint32 pe_function_from_name(const char *name) {
    if(void *symbol=gb_symbol(name))return reinterpret_cast<uintptr_t>(symbol);
    return engine_original.pfnFunctionFromName?engine_original.pfnFunctionFromName(name):0;
}
static const char *pe_name_for_function(uint32 address) {
    if(gb_contains(reinterpret_cast<void*>(address))) {
        if(const char *name=gb_symbol_name(reinterpret_cast<void*>(address)))return name;
        return nullptr;
    }
    return engine_original.pfnNameForFunction?engine_original.pfnNameForFunction(address):nullptr;
}
EXPORT void GiveFnptrsToDll(enginefuncs_t *functions,globalvars_t *globals) {
    if(initialized)fatal("GiveFnptrsToDll called twice");
    if(!pe_loaded||!functions||!globals)fatal("Invalid GiveFnptrsToDll initialization");
    /* Current Steam engine and Valve SDK supply 159 callbacks. Older DLLs,
     * including Gunman, read only their own prefix of this table. */
    std::memcpy(&engine_original,functions,sizeof(engine_original));
    std::memcpy(&engine_pe,functions,sizeof(engine_pe));
    #include "engine_thunks.inc"
    engine_pe.pfnFunctionFromName=pe_function_from_name;
    engine_pe.pfnNameForFunction=pe_name_for_function;
    auto give=reinterpret_cast<void(__attribute__((stdcall)) *)(enginefuncs_t*,globalvars_t*)>(gb_symbol("GiveFnptrsToDll"));
    if(!give)fatal("Missing GiveFnptrsToDll export");
    give(&engine_pe,globals);initialized=true;
    std::fprintf(stderr,"[bridge] GiveFnptrsToDll: native callbacks forwarded, globals=%p\n",globals);
}
static void game_init() {
    game_inits++;original_callbacks.pfnGameInit();
    std::fprintf(stderr,"[bridge] GameInit completed\n");
}
static int spawn(edict_t *entity) {spawns++;return original_callbacks.pfnSpawn(entity);}
static void think(edict_t *entity) {thinks++;original_callbacks.pfnThink(entity);}
static void keyvalue(edict_t *entity,KeyValueData *data) {keyvalues++;original_callbacks.pfnKeyValue(entity,data);}
static void activate(edict_t *entities,int count,int clients) {
    activations++;original_callbacks.pfnServerActivate(entities,count,clients);
    std::fprintf(stderr,"[bridge] ServerActivate: edicts=%d maxclients=%d spawns=%u factories=%u\n",count,clients,spawns,factories);
}
static void start_frame() {frames++;original_callbacks.pfnStartFrame();}
static void instrument(DLL_FUNCTIONS *table) {
    original_callbacks=*table;
    table->pfnGameInit=game_init;table->pfnSpawn=spawn;table->pfnThink=think;
    table->pfnKeyValue=keyvalue;table->pfnServerActivate=activate;table->pfnStartFrame=start_frame;
    table->pfnPM_Init=movement_init;table->pfnPM_Move=movement;
}
EXPORT int GetEntityAPI2(DLL_FUNCTIONS *table,int *version) {
    if(!initialized||!table||!version)return 0;
    auto get=reinterpret_cast<int(*)(DLL_FUNCTIONS*,int*)>(gb_symbol("GetEntityAPI2"));
    int result=0;
    if(get)result=get(table,version);
    else {
        auto legacy=reinterpret_cast<int(*)(DLL_FUNCTIONS*,int)>(gb_symbol("GetEntityAPI"));
        if(*version!=INTERFACE_VERSION)*version=INTERFACE_VERSION;
        else if(legacy)result=legacy(table,*version);
    }
    if(result && table)instrument(table);
    std::fprintf(stderr,"[bridge] GetEntityAPI2: version=%d result=%d\n",*version,result);
    return result;
}
EXPORT int GetEntityAPI(DLL_FUNCTIONS *table,int version) {
    if(!initialized||!table)return 0;
    auto get=reinterpret_cast<int(*)(DLL_FUNCTIONS*,int)>(gb_symbol("GetEntityAPI"));
    int result=0;
    if(get)result=get(table,version);
    else if(auto modern=reinterpret_cast<int(*)(DLL_FUNCTIONS*,int*)>(gb_symbol("GetEntityAPI2")))result=modern(table,&version);
    if(result)instrument(table);return result;
}
EXPORT int GetNewDLLFunctions(NEW_DLL_FUNCTIONS *table,int *version) {
    if(!initialized||!table||!version)return 0;
    auto get=reinterpret_cast<int(*)(NEW_DLL_FUNCTIONS*,int*)>(gb_symbol("GetNewDLLFunctions"));
    return get?get(table,version):0;
}
EXPORT const char *HLBridge_GetDLLPath() {return pe_loaded?selected_dll:nullptr;}
EXPORT size_t HLBridge_GetExportCount() {return published_exports;}
__attribute__((destructor)) static void detach() {
    if(pe_loaded) {
        std::fprintf(stderr,"[bridge] server counters: init=%u factories=%u spawn=%u keyvalue=%u activate=%u think=%u frame=%u movement=%u\n",
            game_inits,factories,spawns,keyvalues,activations,thinks,frames,movement_calls);
        gb_unload();pe_loaded=false;
    }
}
