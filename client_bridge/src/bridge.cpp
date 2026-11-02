#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <dlfcn.h>
#include <string>
#include <chrono>
#include "sdk.h"
#include "runtime.h"
#include "vgui_host_api.h"
#include "render.h"
#include "native_viewport.h"
#include "mouse_input.h"
#include "cl_dll/IGameClientExports.h"
#undef EXPORT
#define EXPORT extern "C" __attribute__((visibility("default")))
static_assert(offsetof(cl_enginefunc_t,pNetAPI)==344);
static cl_enginefunc_t engine_native,engine_pe;
static event_api_t table_pEventAPI;
static triangleapi_t table_pTriAPI;
static efx_api_t table_pEfxAPI;
static demo_api_t table_pDemoAPI;
static net_api_t table_pNetAPI;
static bool loaded;
static bool native_viewport_attached,in_paint;
static unsigned test_frames,redraws,paint_calls,movement_calls;
/* Test-only control of the engine's startup menu. This vtable was checked
 * against hw.so's IBaseUI vtable, including IBaseInterface's two destructor slots. */
struct NativeBaseUI {
    virtual ~NativeBaseUI()=default;
    virtual void Initialize(void*,int)=0;
    virtual void Start(void*,int)=0;
    virtual void Shutdown()=0;
    virtual int KeyEvent(int,int,const char*)=0;
    virtual void SurfaceHandler(void*,void*)=0;
    virtual void Paint(int,int,int,int)=0;
    virtual void HideGameUI()=0;
    virtual void ActivateGameUI()=0;
    virtual void HideConsole()=0;
    virtual void ShowConsole()=0;
};
static void hide_test_ui() {
    Dl_info info{};
    if(!dladdr(reinterpret_cast<void*>(engine_native.pfnGetScreenInfo),&info)) {std::fprintf(stderr,"[client test] dladdr engine failed\n");return;}
    void *module=dlopen(info.dli_fname,RTLD_NOW|RTLD_NOLOAD);
    if(!module){std::fprintf(stderr,"[client test] engine module lookup failed: %s\n",dlerror());return;}
    auto factory=reinterpret_cast<void*(*)(const char*,int*)>(dlsym(module,"CreateInterface"));
    if(factory)if(auto ui=static_cast<NativeBaseUI*>(factory("BaseUI001",nullptr))) {
        ui->HideConsole();ui->HideGameUI();
    }
    else std::fprintf(stderr,"[client test] BaseUI interface absent\n");
    dlclose(module);
}
[[noreturn]] static void fatal(const char *message) {
    std::fprintf(stderr,"[client] FATAL: %s\n",message);gb_report();std::abort();
}
static void ensure_loaded() {
    if(loaded)return;
    Dl_info info{};if(!dladdr(reinterpret_cast<void*>(ensure_loaded),&info))fatal("dladdr");
    std::string directory=info.dli_fname;directory.resize(directory.find_last_of('/')+1);
    std::string beside=directory+"client.dll";
    const char *path=std::getenv("CB_CLIENT_DLL");if(!path)path=beside.c_str();
    if(!std::getenv("CB_VGUI_DLL"))setenv("CB_VGUI_DLL",(directory+"vgui.dll").c_str(),0);
    if(!std::getenv("CB_VGUI_HOST"))setenv("CB_VGUI_HOST",(directory+"vgui_host.dll").c_str(),0);
    if(gb_load(path))fatal("loading client DLL and Windows VGUI failed");
    loaded=true;
}
template<typename Function,auto Member> struct EngineThunk;
template<typename Result,typename... Args,auto Member>
struct EngineThunk<Result(*)(Args...),Member> {
    __attribute__((force_align_arg_pointer,noinline)) static Result call(Args... args) {
        return (engine_native.*Member)(args...);
    }
};
template<typename Function,auto Member,auto Table> struct TableThunk;
template<typename Result,typename... Args,auto Member,auto Table>
struct TableThunk<Result(*)(Args...),Member,Table> {
    __attribute__((force_align_arg_pointer,noinline)) static Result call(Args... args) {
        return ((engine_native.*Table)->*Member)(args...);
    }
};
static engine_studio_api_t studio_native,studio_pe;
template<typename Function,auto Member> struct StudioThunk;
template<typename Result,typename... Args,auto Member>
struct StudioThunk<Result(*)(Args...),Member> {
    __attribute__((force_align_arg_pointer,noinline)) static Result call(Args... args) {
        return (studio_native.*Member)(args...);
    }
};
static std::string format_message(const char *format,va_list args) {
    va_list copy;va_copy(copy,args);int count=vsnprintf(nullptr,0,format,copy);va_end(copy);
    if(count<0)fatal("console formatting failed");
    std::string out(static_cast<size_t>(count)+1,'\0');vsnprintf(out.data(),out.size(),format,args);out.resize(count);return out;
}
#define PRINT_THUNK(name) \
__attribute__((force_align_arg_pointer,noinline)) static void aligned_##name(char *format,...) { \
    va_list args;va_start(args,format);auto out=format_message(format,args);va_end(args); \
    engine_native.name(const_cast<char*>("%s"),out.c_str()); }
PRINT_THUNK(Con_Printf)
PRINT_THUNK(Con_DPrintf)
__attribute__((force_align_arg_pointer,noinline)) static void aligned_Con_NPrintf(int position,char *format,...) {
    va_list args;va_start(args,format);auto out=format_message(format,args);va_end(args);
    engine_native.Con_NPrintf(position,const_cast<char*>("%s"),out.c_str());
}
__attribute__((force_align_arg_pointer,noinline)) static void aligned_Con_NXPrintf(con_nprint_s *print,char *format,...) {
    va_list args;va_start(args,format);auto out=format_message(format,args);va_end(args);
    engine_native.Con_NXPrintf(print,const_cast<char*>("%s"),out.c_str());
}
/* The 2000 client allocates the old 20-byte player-info structure. The native
 * engine writes SteamID in the newer structure; use native storage first. */
__attribute__((force_align_arg_pointer,noinline)) static void aligned_pfnGetPlayerInfo(int index,hud_player_info_t *result) {
    hud_player_info_t info{};engine_native.pfnGetPlayerInfo(index,&info);
    std::memcpy(result,&info,offsetof(hud_player_info_t,m_nSteamID));
}
/* The native engine rejects a sprite list whose declared count is zero.
 * Old clients return early on NULL, leaving weapon crosshair handles from an
 * uninitialized WeaponList local. Gunman's weapon_fists.txt is exactly "0".
 * Represent a valid empty list by a heap allocation and count zero, so the
 * DLL reaches its normal missing-crosshair initialization. Preserve NULL for
 * missing files, invalid counts and failed nonempty lists. */
__attribute__((force_align_arg_pointer,noinline)) static client_sprite_t *aligned_pfnSPR_GetList(char *path,int *count) {
    auto list=engine_native.pfnSPR_GetList(path,count);
    if(list||!path||!engine_native.COM_LoadFile||!engine_native.COM_FreeFile||!engine_native.COM_ParseFile)return list;
    int length=0;
    auto data=engine_native.COM_LoadFile(path,5,&length);
    if(!data)return nullptr;
    char token[1024]{};
    engine_native.COM_ParseFile(reinterpret_cast<char*>(data),token);
    char *end=nullptr;
    long entries=std::strtol(token,&end,10);
    bool empty=end!=token&&!*end&&entries==0;
    engine_native.COM_FreeFile(data);
    if(!empty)return nullptr;
    // SPR_GetList itself uses calloc. Keep its allocation/free convention.
    list=static_cast<client_sprite_t*>(std::calloc(1,sizeof(client_sprite_t)));
    if(!list)fatal("allocating an empty sprite list");
    if(count)*count=0;
    return list;
}
static void *root_panel;
__attribute__((force_align_arg_pointer)) static void *gui_allocate(unsigned int size) {auto p=std::malloc(size?size:1);if(!p)fatal("VGUI allocation");return p;}
__attribute__((force_align_arg_pointer)) static void gui_free(void *p) {std::free(p);}
__attribute__((force_align_arg_pointer)) static void gui_fatal(const char *message) {fatal(message);}
__attribute__((force_align_arg_pointer)) static unsigned gui_millis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
__attribute__((force_align_arg_pointer)) static void gui_mouse(int *x,int *y) {engine_native.GetMousePosition(x,y);}
__attribute__((force_align_arg_pointer)) static void gui_cursor(int code) {
    cb_native_viewport_cursor(code);cb_mouse_cursor_changed();
}
static vgui_host_api gui_api() {return {gui_allocate,gui_free,gui_fatal,gui_millis,gui_mouse,gui_cursor};}
__attribute__((force_align_arg_pointer)) static void aligned_VGui_ViewportPaintBackground(int *extents) {
    // The Linux proxy paints the engine background once before the PE overlay.
    if(!in_paint)engine_native.VGui_ViewportPaintBackground(extents);
}
__attribute__((force_align_arg_pointer,noinline)) static void *aligned_VGui_GetPanel() {
    if(root_panel)return root_panel;
    auto create=reinterpret_cast<void*(*)(const vgui_host_api*,int,int)>(cb_host_symbol("CBVGUI_Create"));
    if(!create)fatal("Windows VGUI host helper not loaded");
    vgui_host_api api=gui_api();
    SCREENINFO screen{};screen.iSize=sizeof(screen);engine_native.pfnGetScreenInfo(&screen);
    cb_render_configure(api,screen.iWidth,screen.iHeight);
    root_panel=create(&api,screen.iWidth,screen.iHeight);
    if(!(std::getenv("CB_HEADLESS_VGUI")&&std::strcmp(std::getenv("CB_HEADLESS_VGUI"),"1")==0)) {
        cb_native_viewport_attach(engine_native.VGui_GetPanel(),screen.iWidth,screen.iHeight);
        native_viewport_attached=true;
    }
    return root_panel;
}
template<typename T> static T function(const char *name) {
    ensure_loaded();auto result=reinterpret_cast<T>(gb_symbol(name));
    if(!result)fatal(name);return result;
}
EXPORT int Initialize(cl_enginefunc_t *engine,int version) {
    if(!engine||version!=CLDLL_INTERFACE_VERSION)return 0;
    ensure_loaded();engine_native=*engine;engine_pe=engine_native;
    cb_set_file_loader(engine_native.COM_LoadFile,engine_native.COM_FreeFile,
        engine_native.pfnGetGameDirectory?engine_native.pfnGetGameDirectory():nullptr);
    cb_mouse_initialize(&engine_native);
    cb_set_mouse(cb_mouse_get,cb_mouse_set);
    #include "engine_thunks.inc"
    if(auto create_app=reinterpret_cast<void(*)(const vgui_host_api*)>(cb_host_symbol("CBVGUI_CreateApp"))) {
        vgui_host_api api=gui_api();create_app(&api);
    }
    return function<INITIALIZE_FUNC>("Initialize")(&engine_pe,version);
}
EXPORT const cl_enginefunc_t *ClientBridge_GetPEEngineFunctions() {return &engine_pe;}
/* Native GameUI asks the client factory for GameClientExports001 (server name
 * and in-game voice muting). The PE object has the Microsoft C++ ABI: thiscall
 * methods after a single destructor slot (checked by
 * tools/check_client_exports_abi.py). This native object forwards to it and
 * answers with neutral values while no PE client is loaded. */
enum {msvc_slot_GetServerHostName=1,msvc_slot_IsPlayerGameVoiceMuted=2,
      msvc_slot_MutePlayerGameVoice=3,msvc_slot_UnmutePlayerGameVoice=4};
class PEClientExports final:public IGameClientExports {
    template<typename Result,typename... Args> Result call(int slot,Args... args) {
        typedef Result (__attribute__((thiscall)) *Method)(void*,Args...);
        return reinterpret_cast<Method>((*static_cast<void***>(pe))[slot])(pe,args...);
    }
public:
    void *pe=nullptr;
    const char *GetServerHostName() override {return pe?call<const char*>(msvc_slot_GetServerHostName):"";}
    bool IsPlayerGameVoiceMuted(int player) override {return pe&&call<bool>(msvc_slot_IsPlayerGameVoiceMuted,player);}
    void MutePlayerGameVoice(int player) override {if(pe)call<void>(msvc_slot_MutePlayerGameVoice,player);}
    void UnmutePlayerGameVoice(int player) override {if(pe)call<void>(msvc_slot_UnmutePlayerGameVoice,player);}
};
static PEClientExports client_exports;
/* Current Linux engines query this factory even for a VGUI1 client. Only
 * interfaces with a native adapter are handed out; others fail as before. */
EXPORT IBaseInterface *CreateInterface(const char *name,int *code) {
    if(name&&!std::strcmp(name,GAMECLIENTEXPORTS_INTERFACE_VERSION)) {
        ensure_loaded();
        auto factory=reinterpret_cast<void*(*)(const char*,int*)>(gb_symbol("CreateInterface"));
        if(void *pe=factory?factory(name,nullptr):nullptr) {
            client_exports.pe=pe;if(code)*code=0;
            std::fprintf(stderr,"[client] native interface query: %s -> PE adapter\n",name);
            return static_cast<IGameClientExports*>(&client_exports);
        }
    }
    std::fprintf(stderr,"[client] native interface query: %s (not provided)\n",name?name:"NULL");
    if(code)*code=1;return nullptr;
}
EXPORT void *ClientFactory() {return reinterpret_cast<void*>(CreateInterface);}
EXPORT void ClientBridge_Report() {gb_report();}
EXPORT unsigned ClientBridge_GlyphUploads() {return cb_render_glyph_uploads();}
EXPORT void ClientBridge_PaintAt(int x0,int y0,int x1,int y1) {
    if(!root_panel)return;paint_calls++;
    SCREENINFO info{};info.iSize=sizeof(info);engine_native.pfnGetScreenInfo(&info);
    int extents[]={x0,y0,x1,y1};
    engine_native.VGui_ViewportPaintBackground(extents);
    auto paint=reinterpret_cast<void(*)(int,int)>(cb_host_symbol("CBVGUI_Paint"));if(!paint)fatal("VGUI paint export");
    in_paint=true;cb_render_begin(info.iWidth,info.iHeight);paint(info.iWidth,info.iHeight);cb_render_end();in_paint=false;
    const char *limit=std::getenv("CB_TEST_FRAMES");
    if(limit&&test_frames+1==std::strtoul(limit,nullptr,10))cb_render_capture(std::getenv("CB_TEST_CAPTURE"));
}
EXPORT void ClientBridge_Paint() {
    // Headless test entry point. Native GUI painting uses PaintAt with the
    // real panel origin, which need not be (0,0) in desktop coordinates.
    if(!root_panel)return;
    SCREENINFO info{};info.iSize=sizeof(info);engine_native.pfnGetScreenInfo(&info);
    ClientBridge_PaintAt(0,0,info.iWidth,info.iHeight);
}
#define F0(ret,name) EXPORT ret name() {return function<ret(*)()>(#name)();}
#define F1(ret,name,t1) EXPORT ret name(t1 a) {return function<ret(*)(t1)>(#name)(a);}
#define F2(ret,name,t1,t2) EXPORT ret name(t1 a,t2 b) {return function<ret(*)(t1,t2)>(#name)(a,b);}
#define F3(ret,name,t1,t2,t3) EXPORT ret name(t1 a,t2 b,t3 c) {return function<ret(*)(t1,t2,t3)>(#name)(a,b,c);}
F0(void,HUD_Init)
F0(int,HUD_VidInit)
EXPORT int HUD_Redraw(float time,int intermission) {
    redraws++;return function<HUD_REDRAW_FUNC>("HUD_Redraw")(time,intermission);
}
F2(int,HUD_UpdateClientData,client_data_t*,float)
F0(void,HUD_Reset)
F1(char,HUD_PlayerMoveTexture,char*)
EXPORT void IN_ActivateMouse() {function<void(*)()>("IN_ActivateMouse")();cb_mouse_activate(true);}
EXPORT void IN_DeactivateMouse() {cb_mouse_activate(false);function<void(*)()>("IN_DeactivateMouse")();}
F1(void,IN_MouseEvent,int)
EXPORT void IN_ClearStates() {function<void(*)()>("IN_ClearStates")();cb_mouse_clear();}
EXPORT void IN_Accumulate() {ClientMouseScope scope;function<void(*)()>("IN_Accumulate")();}
EXPORT void CL_CreateMove(float time,usercmd_t *cmd,int active) {
    ClientMouseScope scope;function<void(*)(float,usercmd_t*,int)>("CL_CreateMove")(time,cmd,active);
}
F0(int,CL_IsThirdPerson)
F1(void,CL_CameraOffset,float*)
F1(kbutton_s*,KB_Find,const char*)
EXPORT void CAM_Think() {ClientMouseScope scope;function<void(*)()>("CAM_Think")();}
F1(void,V_CalcRefdef,ref_params_s*)
F3(int,HUD_AddEntity,int,cl_entity_s*,const char*)
F0(void,HUD_CreateEntities)
F0(void,HUD_DrawNormalTriangles)
F0(void,HUD_DrawTransparentTriangles)
F2(void,HUD_StudioEvent,const mstudioevent_s*,const cl_entity_s*)
F2(void,HUD_TxferLocalOverrides,entity_state_s*,const clientdata_s*)
F2(void,HUD_ProcessPlayerState,entity_state_s*,const entity_state_s*)
F2(void,Demo_ReadBuffer,int,unsigned char*)
F3(int,HUD_GetHullBounds,int,float*,float*)
EXPORT void HUD_Frame(double time) {
    function<HUD_FRAME_FUNC>("HUD_Frame")(time);
    const char *limit=std::getenv("CB_TEST_FRAMES");
    if(limit&&engine_native.pfnGetLevelName&&*engine_native.pfnGetLevelName()&&
       engine_native.GetLocalPlayer&&engine_native.GetLocalPlayer()&&engine_native.GetLocalPlayer()->model) {
        unsigned maximum=std::strtoul(limit,nullptr,10);
        if(test_frames==100) {
            hide_test_ui();
            if(std::getenv("CB_TEST_VGUI_TEXT")) {
                auto show=reinterpret_cast<void(*)()>(cb_host_symbol("CBVGUI_TestFont"));
                if(!show)fatal("test font helper export");show();
            }
            if(const char *commands=std::getenv("CB_TEST_COMMANDS"))
                engine_native.pfnClientCmd(const_cast<char*>(commands));
        }
        if(test_frames==200)
            if(const char *commands=std::getenv("CB_TEST_SECOND_COMMANDS"))
                engine_native.pfnClientCmd(const_cast<char*>(commands));
        if(maximum&&++test_frames==maximum) {
            auto player=engine_native.GetLocalPlayer();
            std::fprintf(stderr,"[client test] completed frames=%u redraws=%u paints=%u moves=%u level=%s origin=%.1f %.1f %.1f uploads=%u\n",
                test_frames,redraws,paint_calls,movement_calls,engine_native.pfnGetLevelName(),player->origin[0],player->origin[1],player->origin[2],cb_render_glyph_uploads());
            engine_native.pfnClientCmd(const_cast<char*>("quit\n"));
        }
    }
}
F3(int,HUD_Key_Event,int,int,const char*)
/* HL SDK 2.x exports. Older DLLs (Gunman) lack them: the engine then uses its
 * own studio renderer, and the other callbacks keep their engine defaults. */
template<typename T> static T optional_function(const char *name) {ensure_loaded();return reinterpret_cast<T>(gb_symbol(name));}
EXPORT int HUD_GetStudioModelInterface(int version,r_studio_interface_t **pe_interface,engine_studio_api_t *studio) {
    auto get=optional_function<int(*)(int,r_studio_interface_t**,engine_studio_api_t*)>("HUD_GetStudioModelInterface");
    if(!get||!studio)return 0;
    studio_native=*studio;studio_pe=studio_native;
    #include "studio_thunks.inc"
    // The engine calls the PE's StudioDrawModel/StudioDrawPlayer directly; the
    // PE only needs aligned entries for its calls back into the engine.
    return get(version,pe_interface,&studio_pe);
}
EXPORT void HUD_VoiceStatus(int entity,qboolean talking) {
    if(auto f=optional_function<void(*)(int,qboolean)>("HUD_VoiceStatus"))f(entity,talking);
}
EXPORT void HUD_DirectorMessage(int size,void *buffer) {
    if(auto f=optional_function<void(*)(int,void*)>("HUD_DirectorMessage"))f(size,buffer);
}
EXPORT void HUD_ChatInputPosition(int *x,int *y) {
    if(auto f=optional_function<void(*)(int*,int*)>("HUD_ChatInputPosition"))f(x,y);
}
F1(cl_entity_s*,HUD_GetUserEntity,int)
EXPORT void HUD_Shutdown() {
    if(!loaded)return;
    cb_mouse_activate(false);
    if(native_viewport_attached)cb_native_viewport_detach(engine_native.VGui_GetPanel());
    native_viewport_attached=false;
    function<HUD_SHUTDOWN_FUNC>("HUD_Shutdown")();client_exports.pe=nullptr;cb_render_unload();gb_unload();loaded=false;root_panel=nullptr;
}
EXPORT int HUD_ConnectionlessPacket(const netadr_s *from,const char *args,char *buffer,int *size) {
    return function<HUD_CONNECTIONLESS_FUNC>("HUD_ConnectionlessPacket")(from,args,buffer,size);
}
EXPORT void HUD_PostRunCmd(local_state_s *from,local_state_s *to,usercmd_s *cmd,int run,double time,unsigned seed) {
    function<HUD_POSTRUNCMD_FUNC>("HUD_PostRunCmd")(from,to,cmd,run,time,seed);
}
EXPORT void HUD_TxferPredictionData(entity_state_s *state,const entity_state_s *prev,clientdata_s *client,
    const clientdata_s *oldclient,weapon_data_s *weapons,const weapon_data_s *oldweapons) {
    function<HUD_TXFERPREDICTIONDATA_FUNC>("HUD_TxferPredictionData")(state,prev,client,oldclient,weapons,oldweapons);
}
/* Movement's trace result uses a different hidden-return-pointer convention. */
static decltype(playermove_t::PM_PlayerTrace) native_trace;
static decltype(playermove_t::PM_PlayerTraceEx) native_trace_ex;
__attribute__((force_align_arg_pointer,callee_pop_aggregate_return(0)))
static pmtrace_t windows_trace(float *a,float *b,int flags,int ignore) {return native_trace(a,b,flags,ignore);}
__attribute__((force_align_arg_pointer,callee_pop_aggregate_return(0)))
static pmtrace_t windows_trace_ex(float *a,float *b,int flags,int(*ignore)(physent_t*)) {return native_trace_ex(a,b,flags,ignore);}
static void movement(playermove_t *move,int server,bool initialize) {
    if(!initialize)movement_calls++;
    auto trace=move->PM_PlayerTrace;auto trace_ex=move->PM_PlayerTraceEx;
    auto previous=native_trace;auto previous_ex=native_trace_ex;
    native_trace=trace;native_trace_ex=trace_ex;
    move->PM_PlayerTrace=reinterpret_cast<decltype(trace)>(windows_trace);
    move->PM_PlayerTraceEx=reinterpret_cast<decltype(trace_ex)>(windows_trace_ex);
    if(initialize)function<HUD_CLIENTMOVEINIT_FUNC>("HUD_PlayerMoveInit")(move);
    else function<HUD_CLIENTMOVE_FUNC>("HUD_PlayerMove")(move,server);
    move->PM_PlayerTrace=trace;move->PM_PlayerTraceEx=trace_ex;native_trace=previous;native_trace_ex=previous_ex;
}
EXPORT void HUD_PlayerMoveInit(playermove_t *move) {movement(move,0,true);}
EXPORT void HUD_PlayerMove(playermove_t *move,qboolean server) {movement(move,server,false);}
static int(*visible_callback)(cl_entity_s*);
static void(*sound_callback)(tempent_s*,float);
__attribute__((force_align_arg_pointer)) static int visible(cl_entity_s *e) {return visible_callback(e);}
__attribute__((force_align_arg_pointer)) static void sound(tempent_s *e,float damp) {sound_callback(e,damp);}
EXPORT void HUD_TempEntUpdate(double frame,double time,double gravity,tempent_s **free,tempent_s **active,
    int(*add)(cl_entity_s*),void(*play)(tempent_s*,float)) {
    auto oldadd=visible_callback;auto oldplay=sound_callback;visible_callback=add;sound_callback=play;
    function<HUD_TEMPENTUPDATE_FUNC>("HUD_TempEntUpdate")(frame,time,gravity,free,active,add?visible:nullptr,play?sound:nullptr);
    visible_callback=oldadd;sound_callback=oldplay;
}
