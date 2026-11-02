#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <fstream>
#include <cctype>
#include <dlfcn.h>
#include <array>
#include <cmath>
#include "SDL_mouse.h"
#include "SDL_keyboard.h"
#include "sdk.h"
#include "cvardef.h"
static unsigned hooks,messages,commands;
static std::map<std::string,cvar_t> variables;
static std::map<std::string,void(*)()> command_handlers;
static std::map<std::string,pfnUserMsgHook> message_handlers;
static void hook_event(char *name,void(*callback)(event_args_s*)) {
    if(!name||!callback)std::abort();hooks++;printf("event %s\n",name);
}
static int hook_message(char *name,pfnUserMsgHook callback) {
    if(!name||!callback)std::abort();message_handlers[name]=callback;messages++;printf("message %s\n",name);return messages;
}
static int add_command(char *name,void(*callback)()) {
    if(!name||!callback)std::abort();command_handlers[name]=callback;commands++;printf("command %s\n",name);return commands;
}
static cvar_t *register_variable(char *name,char *value,int flags) {
    auto found=variables.find(name);if(found!=variables.end())return &found->second;
    auto &var=variables[name];var.name=strdup(name);var.string=strdup(value);var.value=atof(value);var.flags=flags;return &var;
}
static cvar_t *get_variable(const char *name) {
    auto it=variables.find(name);if(it==variables.end()) {
        fprintf(stderr,"missing mock cvar %s\n",name);
        return register_variable(const_cast<char*>(name),const_cast<char*>("0"),0);
    }
    return &it->second;
}
static float get_float(char *name) {return get_variable(name)->value;}
static char *get_string(char *name) {return get_variable(name)->string;}
static const char *directory() {return "rewolf";}
static int screen(SCREENINFO *info) {
    info->iWidth=1280;info->iHeight=720;info->iCharHeight=16;
    for(auto &w:info->charWidths)w=8;return 1;
}
static float client_time() {return 1.0f;}
static int check_parameter(char*,char**) {return 0;}
static void console(char *format,...) {printf("client console %s\n",format);}
static int no_arguments() {return 0;}
static char *argument(int) {return const_cast<char*>("");}
static double mouse_x=640,mouse_y=360,absolute_scale=1;
static int relative_x,relative_y;
static SDL_bool relative_mode=SDL_FALSE;
static int window_width=1280,window_height=720,window_center_x=640,window_center_y=360;
static bool mouse_focus=true;
static bool scaled_mouse;
static unsigned mouse_warps;
static float view_angles[3];
static void mouse_position(int *x,int *y) {
    *x=scaled_mouse?static_cast<int>(640+(mouse_x-640)*0.75):mouse_x;
    *y=scaled_mouse?static_cast<int>(360+(mouse_y-360)*0.75):mouse_y;
}
static void mouse_warp(int x,int y) {mouse_x=x;mouse_y=y;mouse_warps++;}
extern "C" unsigned SDL_GetMouseState(int *x,int *y) {
    if(x)*x=static_cast<int>(mouse_x);if(y)*y=static_cast<int>(mouse_y);return 0;
}
extern "C" unsigned SDL_GetRelativeMouseState(int *x,int *y) {
    if(x)*x=relative_x;if(y)*y=relative_y;relative_x=relative_y=0;return 0;
}
extern "C" SDL_bool SDL_GetRelativeMouseMode() {return relative_mode;}
extern "C" int SDL_SetRelativeMouseMode(SDL_bool mode) {
    relative_mode=mode;relative_x=relative_y=0;return 0;
}
extern "C" SDL_Window *SDL_GetMouseFocus() {return mouse_focus?reinterpret_cast<SDL_Window*>(1):nullptr;}
extern "C" SDL_Window *SDL_GetKeyboardFocus() {return SDL_GetMouseFocus();}
extern "C" void SDL_GetWindowSize(SDL_Window*,int *w,int *h) {*w=window_width;*h=window_height;}
extern "C" void SDL_WarpMouseInWindow(SDL_Window*,int x,int y) {mouse_warp(x,y);}
static void get_angles(float *out) {memcpy(out,view_angles,sizeof(view_angles));}
static void set_angles(float *in) {memcpy(view_angles,in,sizeof(view_angles));}
static float max_speed() {return 320;}
static void mouse_motion(int x,int y) {
    mouse_x+=x*absolute_scale;mouse_y+=y*absolute_scale;relative_x+=x;relative_y+=y;
}
static void player_info(int,hud_player_info_t *p) {memset(p,0,sizeof(*p));}
static void *native_panel() {std::abort();}
static std::map<std::string,std::string> test_assets;
static std::vector<unsigned char> read_asset(const std::string &request) {
    std::string name=request;for(char &c:name){if(c=='\\')c='/';c=std::tolower(static_cast<unsigned char>(c));}
    auto fixture=test_assets.find(name);
    if(fixture!=test_assets.end())return {fixture->second.begin(),fixture->second.end()};
    const char *configured=getenv("CB_ASSET_ROOT");if(!configured){fprintf(stderr,"CB_ASSET_ROOT required\n");std::abort();}
    std::string root=configured;
    for(const auto &base:{root,root+"/../valve"}) {
        std::ifstream file(base+"/"+name,std::ios::binary);
        if(file.good())return std::vector<unsigned char>(std::istreambuf_iterator<char>(file),{});
        for(int pack=9;pack>=0;pack--) {
            std::ifstream pak(base+"/pak"+std::to_string(pack)+".pak",std::ios::binary);if(!pak)continue;
            char tag[4];uint32_t offset,count;pak.read(tag,4);pak.read(reinterpret_cast<char*>(&offset),4);pak.read(reinterpret_cast<char*>(&count),4);
            if(memcmp(tag,"PACK",4)||count%64)std::abort();
            pak.seekg(offset);
            for(unsigned entry=0;entry<count/64;entry++) {
                char filename[57]{};uint32_t start,size;pak.read(filename,56);pak.read(reinterpret_cast<char*>(&start),4);pak.read(reinterpret_cast<char*>(&size),4);
                std::string folded=filename;for(char &c:folded)c=std::tolower(static_cast<unsigned char>(c));
                if(folded==name){std::vector<unsigned char> bytes(size);pak.seekg(start);pak.read(reinterpret_cast<char*>(bytes.data()),size);if(!pak)std::abort();return bytes;}
            }
        }
    }
    fprintf(stderr,"asset absent: %s\n",request.c_str());return {};
}
static byte *load_file(char *path,int,int *length) {
    auto bytes=read_asset(path);if(bytes.empty()){if(length)*length=0;return nullptr;}
    byte *out=static_cast<byte*>(malloc(bytes.size()+1));memcpy(out,bytes.data(),bytes.size());out[bytes.size()]=0;
    if(length)*length=bytes.size();return out;
}
static void free_file(void *p) {free(p);}
static char *parse_file(char *input,char *token) {
    if(!input)return nullptr;
    for(;;) {
        while(*input&&std::isspace(static_cast<unsigned char>(*input)))input++;
        if(input[0]=='/'&&input[1]=='/'){while(*input&&*input!='\n')input++;continue;}break;
    }
    if(!*input){*token=0;return nullptr;}
    if(*input=='"'){input++;while(*input&&*input!='"')*token++=*input++;if(*input)input++;}
    else if(strchr("{}()':,",*input))*token++=*input++;
    else while(*input&&!std::isspace(static_cast<unsigned char>(*input))&&!strchr("{}()':,",*input))*token++=*input++;
    *token=0;return input;
}
static bool force_sprite_list_failure;
static client_sprite_t *sprite_list(char *name,int *count) {
    if(force_sprite_list_failure)return nullptr;
    static std::vector<client_sprite_t> sprites;
    auto bytes=read_asset(name);bytes.push_back(0);char *input=reinterpret_cast<char*>(bytes.data());char token[1024];
    input=parse_file(input,token);if(!input)return nullptr;int entries=atoi(token);
    // The real Linux SPR_GetList returns NULL without writing count for "0".
    if(!entries)return nullptr;
    sprites.clear();
    for(int i=0;i<entries;i++) {
        client_sprite_t sprite{};
        input=parse_file(input,sprite.szName);input=parse_file(input,token);sprite.iRes=atoi(token);
        input=parse_file(input,sprite.szSprite);input=parse_file(input,token);sprite.rc.left=atoi(token);
        input=parse_file(input,token);sprite.rc.top=atoi(token);input=parse_file(input,token);sprite.rc.right=sprite.rc.left+atoi(token);
        input=parse_file(input,token);sprite.rc.bottom=sprite.rc.top+atoi(token);sprites.push_back(sprite);
    }
    *count=sprites.size();return sprites.data();
}
static int sprite_load(const char *name) {auto bytes=read_asset(name);if(bytes.empty())return 0;return 1;}
static int sprite_dimension(int,int) {return 64;}
static int crosshair_sprite;
static unsigned crosshair_calls;
static void set_crosshair(int sprite,wrect_t,int,int,int) {crosshair_sprite=sprite;crosshair_calls++;}
__attribute__((noinline)) static void dirty_weapon_stack(unsigned value) {
    volatile unsigned words[2048];
    for(unsigned i=0;i<2048;i++)words[i]=value;
}
static bool test_empty_sprite_lists(void *so) {
    auto pe=reinterpret_cast<const cl_enginefunc_t*(*)()>(dlsym(so,"ClientBridge_GetPEEngineFunctions"))();
    auto weapon_list=message_handlers.at("WeaponList");
    auto current_weapon=message_handlers.at("CurWeapon");
    auto weapon_message=[](const char *name,unsigned char id) {
        std::vector<unsigned char> message;
        auto string=[&](const char *text){while(*text)message.push_back(*text++);message.push_back(0);};
        string(name);
        for(unsigned char byte:{255,255,255,255,0,0})message.push_back(byte);
        message.push_back(id);message.push_back(0);
        string(name);
        for(int i=0;i<3;i++)message.push_back(0);
        return message;
    };
    auto message=weapon_message("weapon_fists",1);
    auto beam=weapon_message("weapon_beamgun",14);
    // Exercise the actual PE WeaponList -> CurWeapon -> SetCrosshair path.
    // Dirty the stack to make its uninitialized local reproducible.
    for(unsigned fill:{0xffu,0xa5a5a5a5u,0u}) {
        dirty_weapon_stack(fill);
        if(!weapon_list("WeaponList",message.size(),message.data()))return false;
        if(!weapon_list("WeaponList",beam.size(),beam.data()))return false;
        for(unsigned char state:{1,2}) {
            unsigned char armed[]={state,14,255};crosshair_calls=0;
            if(!current_weapon("CurWeapon",sizeof(armed),armed)||!crosshair_calls||crosshair_sprite!=1)return false;
            unsigned char current[]={state,1,255};crosshair_calls=0;
            if(!current_weapon("CurWeapon",sizeof(current),current))return false;
            if(!crosshair_calls||crosshair_sprite!=0) {
                fprintf(stderr,"FAIL: empty fists sprite list: PE SetCrosshair sprite=%#x calls=%u stack_fill=%#x state=%u\n",
                    crosshair_sprite,crosshair_calls,fill,state);
                return false;
            }
        }
    }
    char path[]="sprites/weapon_fists.txt";
    int count=123;
    auto empty=pe->pfnSPR_GetList(path,&count);
    if(!empty||count!=0)return false;
    free_file(empty);
    empty=pe->pfnSPR_GetList(path,nullptr);
    if(!empty)return false;
    free_file(empty);
    // A NULL from any other failure must stay NULL, even if count happens to
    // be zero or the native callback leaves that output untouched.
    char fixture[]="sprites/bridge_list_fixture.txt";
    force_sprite_list_failure=true;
    for(const char *contents:{"", "// no count\n", "invalid", "-1", "1\n", "0invalid", "// empty\n 0\n"}) {
        test_assets[fixture]=contents;count=123;
        auto result=pe->pfnSPR_GetList(fixture,&count);
        bool valid_empty=!strcmp(contents,"// empty\n 0\n");
        if(bool(result)!=valid_empty||count!=(valid_empty?0:123))return false;
        if(result)free_file(result);
    }
    force_sprite_list_failure=false;
    test_assets.clear();
    char pistol[]="sprites/weapon_gausspistol.txt";
    auto native=sprite_list(pistol,&count);int native_count=count;
    auto forwarded=pe->pfnSPR_GetList(pistol,&count);
    if(!native||forwarded!=native||count!=native_count)return false;
    printf("PASS: empty sprite lists initialize real PE fists crosshairs for dirty/clean stacks and normal/autoaim states\n");
    printf("PASS: original PE client switches from beamgun to fists with the expected crosshair handles\n");
    printf("PASS: missing/invalid/nonempty sprite-list failures preserved; nonempty native list forwarded unchanged; empty lists remain heap-freeable\n");
    return true;
}
static const char *level_name() {return const_cast<char*>("maps/city1a.bsp");}
static client_textmessage_t *text_message(const char*) {return nullptr;}
static const char *binding(const char*) {return nullptr;}
static int max_clients() {return 1;}
static int center_x() {return window_center_x;}
static int center_y() {return window_center_y;}
static int console_visible() {return 0;}
static std::array<int,4> last_background_extents;
static unsigned background_calls;
static void paint_background(int *extents) {
    for(int i=0;i<4;i++)last_background_extents[i]=extents[i];background_calls++;
}
static bool test_mouse(void *so) {
    auto activate=reinterpret_cast<void(*)()>(dlsym(so,"IN_ActivateMouse"));
    auto accumulate=reinterpret_cast<void(*)()>(dlsym(so,"IN_Accumulate"));
    auto clear=reinterpret_cast<void(*)()>(dlsym(so,"IN_ClearStates"));
    auto move=reinterpret_cast<void(*)(float,usercmd_t*,int)>(dlsym(so,"CL_CreateMove"));
    auto pe=reinterpret_cast<const cl_enginefunc_t*(*)()>(dlsym(so,"ClientBridge_GetPEEngineFunctions"))();
    get_variable("sensitivity")->value=6;
    get_variable("m_filter")->value=0;
    get_variable("m_yaw")->value=0.022f;
    get_variable("m_pitch")->value=0.022f;
    get_variable("cl_pitchup")->value=89;
    get_variable("cl_pitchdown")->value=89;
    get_variable("cl_mousegrab")->value=1;
    command_handlers.at("+mlook")();
    activate();scaled_mouse=true;
    // UI coordinates must keep the engine's HUD scaling. Only WinAPI input
    // uses the unscaled SDL coordinates.
    mouse_x=641;mouse_y=361;int ui_x,ui_y;pe->GetMousePosition(&ui_x,&ui_y);
    if(ui_x!=640||ui_y!=360)return false;
    unsigned samples=0;
    auto sample=[&](int dx,int dy,int polls,int fps) {
        // The old DLL quantizes yaw in CL_AdjustAngles. Start each sample at
        // exactly 180 degrees so that test expectations measure input alone.
        view_angles[0]=0;view_angles[1]=180;view_angles[2]=0;
        for(int poll=0;poll<polls;poll++){mouse_motion(dx,dy);accumulate();}
        usercmd_t cmd{};move(1.0f/fps,&cmd,1);
        float yaw=view_angles[1]-180,pitch=view_angles[0];
        float expected_yaw=-polls*dx*6*0.022f,expected_pitch=polls*dy*6*0.022f;
        if(std::fabs(yaw-expected_yaw)>0.00005f||std::fabs(pitch-expected_pitch)>0.00005f) {
            fprintf(stderr,"FAIL: mouse sample %u: delta=%d,%d polls=%d fps=%d yaw=%g expected=%g pitch=%g expected=%g center=%d,%d warps=%u\n",
                samples,dx,dy,polls,fps,yaw,expected_yaw,pitch,expected_pitch,center_x(),center_y(),mouse_warps);
            return false;
        }
        if(relative_mode==SDL_TRUE) {
            if(relative_x||relative_y)return false;
        } else if(mouse_x!=window_width/2||mouse_y!=window_height/2)return false;
        samples++;return true;
    };
    for(auto dimensions:{std::array<int,2>{1280,720},std::array<int,2>{1920,1080}}) {
        window_width=dimensions[0];window_height=dimensions[1];
        for(auto origin:{std::array<int,2>{0,0},std::array<int,2>{1080,360},std::array<int,2>{-1920,-720}}) {
            window_center_x=window_width/2+origin[0];window_center_y=window_height/2+origin[1];
            mouse_x=window_width/2;mouse_y=window_height/2;clear();
            for(int fps:{60,100,240})for(int frame=0;frame<12;frame++)
                if(!sample(1,0,1,fps)||!sample(-1,0,1,fps)||!sample(0,1,1,fps)||!sample(0,-1,1,fps)||
                   !sample(1,-1,3,fps)||!sample(0,0,1,fps))return false;
        }
    }
    window_width=1280;window_height=720;window_center_x=640;window_center_y=360;
    mouse_x=640;mouse_y=360;clear();
    unsigned previous_warps=mouse_warps;
    for(double scale:{0.25,0.1,0.5,0.9}) {
        absolute_scale=scale;
        for(int fps:{60,100,240})for(auto direction:{std::array<int,2>{1,1},std::array<int,2>{-1,1},
                                                   std::array<int,2>{1,-1},std::array<int,2>{-1,-1}})
            for(int frame=0;frame<16;frame++)
                if(!sample(direction[0],direction[1],1,fps)||!sample(direction[0],direction[1],3,fps)||
                   !sample(0,0,1,fps))return false;
    }
    if(mouse_warps!=previous_warps)return false;
    absolute_scale=1;mouse_x=640;mouse_y=360;scaled_mouse=false;mouse_focus=false;clear();
    if(!sample(1,-1,1,240))return false;
    mouse_focus=true;
    printf("PASS: %u mouse samples: one-count motion, fractional diagonals in all directions, 60/100/240 FPS, accumulation, shifted/negative window origins; GUI coordinates preserved\n",samples);
    return true;
}
int main(int argc,char **argv) {
    if(argc<2)return 2;
    void *so=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!so){fprintf(stderr,"%s\n",dlerror());return 3;}
    auto initialize=reinterpret_cast<INITIALIZE_FUNC>(dlsym(so,"Initialize"));
    if(!initialize)return 4;
    void *server=nullptr;
    auto load_server=[&]() {
        server=dlopen("../gunman_bridge/build/gunman.so",RTLD_NOW|RTLD_LOCAL);
        if(!server){fprintf(stderr,"server %s\n",dlerror());std::abort();}
        void *engine_functions[159]{};alignas(16) unsigned char globals[172]{};
        reinterpret_cast<void(*)(void*,void*)>(dlsym(server,"GiveFnptrsToDll"))(engine_functions,globals);
        void *functions[50]{};int version=140;
        if(reinterpret_cast<int(*)(void*,int*)>(dlsym(server,"GetEntityAPI2"))(functions,&version)!=1)std::abort();
    };
    if(argc>4&&!strcmp(argv[4],"server-first"))load_server();
    cl_enginefunc_t engine{};
    engine.pfnHookEvent=hook_event;engine.pfnHookUserMsg=hook_message;engine.pfnAddCommand=add_command;
    engine.pfnRegisterVariable=register_variable;engine.pfnGetCvarFloat=get_float;engine.pfnGetCvarString=get_string;
    engine.pfnGetCvarPointer=get_variable;engine.pfnGetGameDirectory=directory;
    engine.pfnGetScreenInfo=screen;engine.GetClientTime=client_time;engine.CheckParm=check_parameter;
    engine.Con_Printf=console;engine.Con_DPrintf=console;engine.Cmd_Argc=no_arguments;engine.Cmd_Argv=argument;
    engine.GetMousePosition=mouse_position;engine.pfnGetPlayerInfo=player_info;
    engine.pfnSetMousePos=mouse_warp;engine.GetViewAngles=get_angles;engine.SetViewAngles=set_angles;
    engine.GetClientMaxspeed=max_speed;
    engine.VGui_GetPanel=native_panel;
    engine.pfnSPR_GetList=sprite_list;engine.pfnSPR_Load=sprite_load;engine.pfnSPR_Width=sprite_dimension;engine.pfnSPR_Height=sprite_dimension;
    engine.pfnSetCrosshair=set_crosshair;
    engine.COM_LoadFile=load_file;engine.COM_FreeFile=free_file;engine.COM_ParseFile=parse_file;engine.pfnGetLevelName=level_name;
    engine.pfnTextMessageGet=text_message;engine.Key_LookupBinding=binding;engine.GetMaxClients=max_clients;
    engine.GetWindowCenterX=center_x;engine.GetWindowCenterY=center_y;engine.Con_IsVisible=console_visible;
    engine.VGui_ViewportPaintBackground=paint_background;
    if(initialize(nullptr,7)!=0||initialize(&engine,6)!=0||initialize(&engine,7)!=1||hooks!=34)return 5;
    auto get_table=reinterpret_cast<const cl_enginefunc_t*(*)()>(dlsym(so,"ClientBridge_GetPEEngineFunctions"));
    if(!get_table||get_table()->pfnSPR_Frames||get_table()->pTriAPI)return 6;
    unsigned char old_info[24];memset(old_info,0xa5,sizeof(old_info));
    get_table()->pfnGetPlayerInfo(1,reinterpret_cast<hud_player_info_t*>(old_info));
    for(unsigned i=20;i<sizeof(old_info);i++)if(old_info[i]!=0xa5)return 7;
    if(argc>2)reinterpret_cast<HUD_INIT_FUNC>(dlsym(so,"HUD_Init"))();
    if(argc>3 && !strcmp(argv[3],"root")) {
        if(!get_table()->VGui_GetPanel())return 8;
        printf("PASS: Windows VGUI root and surface created without a Windows window\n");
    } else if(argc>3) {
        reinterpret_cast<HUD_VIDINIT_FUNC>(dlsym(so,"HUD_VidInit"))();
        reinterpret_cast<void(*)()>(dlsym(so,"ClientBridge_Paint"))();
        auto uploads=reinterpret_cast<unsigned(*)()>(dlsym(so,"ClientBridge_GlyphUploads"))();
        if(!uploads)return 9;printf("PASS: HUD_VidInit and Windows VGUI paint, %u texture/glyph uploads\n",uploads);
        auto paint_at=reinterpret_cast<void(*)(int,int,int,int)>(dlsym(so,"ClientBridge_PaintAt"));
        std::array<int,4> shifted={1080,360,2360,1080};unsigned previous_calls=background_calls;
        if(!paint_at)return 10;
        paint_at(shifted[0],shifted[1],shifted[2],shifted[3]);
        if(last_background_extents!=shifted||background_calls!=previous_calls+1)return 11;
        printf("PASS: desktop viewport origin preserved; engine background painted once\n");
    }
    if(getenv("CB_TEST_EMPTY_SPRITES")&&!test_empty_sprite_lists(so))return 13;
    if(getenv("CB_TEST_MOUSE")&&!test_mouse(so))return 12;
    printf("PASS: client Initialize, %u events, %u messages, %u commands; old player-info guard preserved\n",hooks,messages,commands);
    if(argc>4&&!server)load_server();
    reinterpret_cast<HUD_SHUTDOWN_FUNC>(dlsym(so,"HUD_Shutdown"))();dlclose(so);
    if(server) {
        auto symbol=reinterpret_cast<void*(*)(const char*)>(dlsym(server,"gb_symbol"));
        void *table[50]{};int version=140;
        if(reinterpret_cast<int(*)(void*,int*)>(symbol("GetEntityAPI2"))(table,&version)!=1)std::abort();
        printf("PASS: shared Windows runtime, server survives client unload\n");dlclose(server);
    }
    for(auto &entry:variables){free(entry.second.name);free(entry.second.string);}
    return 0;
}
