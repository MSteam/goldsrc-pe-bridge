// Generic headless start of any PE client through client.so.
// Every engine callback this mock does not implement is a named trap, so the
// first unsupported engine use is reported by name instead of crashing.
//   CB_CLIENT_DLL=<mod>/cl_dlls/client.dll CB_ASSET_ROOT=<mod> smoke client.so [init|hud|vid]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cctype>
#include <map>
#include <string>
#include <vector>
#include <fstream>
#include <iterator>
#include <utility>
#include <dlfcn.h>
#include "sdk.h"
#include "cvardef.h"
#include "FileSystem.h"
#include "cl_dll/IGameClientExports.h"
#include "engine_slot_names.inc"
static_assert(sizeof(cl_enginefunc_t)/sizeof(void*)==sizeof(engine_slot_names)/sizeof(*engine_slot_names));
static const char *stage="load";
[[noreturn]] static void trapped(const char *table,const char *name) {
    std::fprintf(stderr,"[smoke] FAIL stage=%s: unimplemented engine callback %s%s%s\n",stage,table,*table?"->":"",name);
    std::fflush(nullptr);std::_Exit(20);
}
template<size_t I> static void engine_trap() {trapped("",engine_slot_names[I]);}
#define TABLE_TRAP(field) \
    template<size_t I> static void field##_trap() { \
        trapped(#field,I<sizeof(field##_slot_names)/sizeof(*field##_slot_names)?field##_slot_names[I]:"<beyond table>"); }
TABLE_TRAP(studio) TABLE_TRAP(pEventAPI) TABLE_TRAP(pTriAPI) TABLE_TRAP(pEfxAPI) TABLE_TRAP(pDemoAPI) TABLE_TRAP(pNetAPI)
template<typename Table,void(*...traps)()> static Table *trap_table() {
    static void (*slots[])()={traps...};static_assert(sizeof(slots)>=sizeof(Table));
    return reinterpret_cast<Table*>(slots);
}
template<size_t... I> static void fill_engine(cl_enginefunc_t &engine,std::index_sequence<I...>) {
    void (*slots[])()={engine_trap<I>...};std::memcpy(&engine,slots,sizeof(engine));
}
#define TRAP_TABLE(type,field,n) \
    template<size_t... I> static type *field##_table(std::index_sequence<I...>) {return trap_table<type,field##_trap<I>...>();}
TRAP_TABLE(event_api_t,pEventAPI,64) TRAP_TABLE(triangleapi_t,pTriAPI,64) TRAP_TABLE(efx_api_t,pEfxAPI,128)
TRAP_TABLE(demo_api_t,pDemoAPI,16) TRAP_TABLE(net_api_t,pNetAPI,16) TRAP_TABLE(engine_studio_api_t,studio,64)

static unsigned hooks,messages,commands;
static std::map<std::string,cvar_t> variables;
static void hook_event(char *name,void(*callback)(event_args_s*)) {if(!name||!callback)std::abort();hooks++;}
static int hook_message(char *name,pfnUserMsgHook callback) {if(!name||!callback)std::abort();return ++messages;}
static int add_command(char *name,void(*callback)()) {if(!name||!callback)std::abort();return ++commands;}
static cvar_t *register_variable(char *name,char *value,int flags) {
    auto found=variables.find(name);if(found!=variables.end())return &found->second;
    auto &var=variables[name];var.name=strdup(name);var.string=strdup(value?value:"");var.value=atof(var.string);var.flags=flags;return &var;
}
static cvar_t *get_variable(const char *name) {
    auto it=variables.find(name);
    return it==variables.end()?register_variable(const_cast<char*>(name),const_cast<char*>("0"),0):&it->second;
}
static float get_float(char *name) {return get_variable(name)->value;}
static char *get_string(char *name) {return get_variable(name)->string;}
static std::string asset_root,game_directory;
static const char *directory() {return game_directory.c_str();}
static int screen(SCREENINFO *info) {
    info->iWidth=1280;info->iHeight=720;info->iCharHeight=16;
    for(auto &w:info->charWidths)w=8;return 1;
}
static float client_time() {return 1.0f;}
static int check_parameter(char*,char**) {return 0;}
static void console(char *format,...) {
    va_list args;va_start(args,format);std::printf("[console] ");std::vprintf(format,args);va_end(args);
}
static void console_n(int,char *format,...) {
    va_list args;va_start(args,format);std::printf("[console] ");std::vprintf(format,args);va_end(args);
}
static int no_arguments() {return 0;}
static char *argument(int) {return const_cast<char*>("");}
static void mouse_position(int *x,int *y) {*x=640;*y=360;}
static void player_info(int,hud_player_info_t *p) {memset(p,0,sizeof(*p));}
static float view_angles[3];
static void get_angles(float *out) {memcpy(out,view_angles,sizeof(view_angles));}
static void set_angles(float *in) {memcpy(view_angles,in,sizeof(view_angles));}
static float max_speed() {return 320;}
static std::vector<unsigned char> read_asset(const std::string &request) {
    std::string name=request;for(char &c:name){if(c=='\\')c='/';c=std::tolower(static_cast<unsigned char>(c));}
    for(const auto &base:{asset_root,asset_root+"/../valve"}) {
        std::ifstream file(base+"/"+name,std::ios::binary);
        if(file.good())return std::vector<unsigned char>(std::istreambuf_iterator<char>(file),{});
        for(int pack=9;pack>=0;pack--) {
            std::ifstream pak(base+"/pak"+std::to_string(pack)+".pak",std::ios::binary);if(!pak)continue;
            char tag[4];uint32_t offset,count;pak.read(tag,4);pak.read(reinterpret_cast<char*>(&offset),4);pak.read(reinterpret_cast<char*>(&count),4);
            if(memcmp(tag,"PACK",4)||count%64)continue;
            pak.seekg(offset);
            for(unsigned entry=0;entry<count/64;entry++) {
                char filename[57]{};uint32_t start,size;pak.read(filename,56);pak.read(reinterpret_cast<char*>(&start),4);pak.read(reinterpret_cast<char*>(&size),4);
                std::string folded=filename;for(char &c:folded)c=std::tolower(static_cast<unsigned char>(c));
                if(folded==name){std::vector<unsigned char> bytes(size);pak.seekg(start);pak.read(reinterpret_cast<char*>(bytes.data()),size);return bytes;}
            }
        }
    }
    std::fprintf(stderr,"[smoke] asset absent: %s\n",request.c_str());return {};
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
static client_sprite_t *sprite_list(char *name,int *count) {
    auto bytes=read_asset(name);if(bytes.empty())return nullptr;bytes.push_back(0);
    char *input=reinterpret_cast<char*>(bytes.data());char token[1024];
    input=parse_file(input,token);int entries=atoi(token);if(!input||entries<=0)return nullptr;
    auto *sprites=static_cast<client_sprite_t*>(calloc(entries,sizeof(client_sprite_t)));
    for(int i=0;i<entries;i++) {
        auto &sprite=sprites[i];
        input=parse_file(input,sprite.szName);input=parse_file(input,token);sprite.iRes=atoi(token);
        input=parse_file(input,sprite.szSprite);input=parse_file(input,token);sprite.rc.left=atoi(token);
        input=parse_file(input,token);sprite.rc.top=atoi(token);input=parse_file(input,token);sprite.rc.right=sprite.rc.left+atoi(token);
        input=parse_file(input,token);sprite.rc.bottom=sprite.rc.top+atoi(token);
    }
    *count=entries;return sprites;
}
static int sprites_loaded;
static HSPRITE sprite_load(const char *name) {return read_asset(name).empty()?0:++sprites_loaded;}
static int sprite_frames(HSPRITE) {return 1;}
static int sprite_dimension(HSPRITE,int) {return 64;}
static void set_crosshair(HSPRITE,wrect_t,int,int,int) {}
static const char *level_name() {return "";}
static client_textmessage_t *text_message(const char*) {return nullptr;}
static const char *binding(const char*) {return "";}
static int max_clients() {return 1;}
static int center() {return 360;}
static int console_visible() {return 0;}
static void paint_background(int*) {}
static void *native_panel() {trapped("","VGui_GetPanel (headless smoke test has no native panel)");}
static cl_entity_t local_player;
static cl_entity_t *get_local_player() {return &local_player;}
static int is_spectate_only() {return 0;}
static int expand_filename(const char *name,char *out,int size) {
    std::string path=asset_root+"/"+name;
    if(!std::ifstream(path).good())return 0;
    std::snprintf(out,size,"%s",path.c_str());return 1;
}
static double absolute_time() {return 1.0;}
static cl_entity_t entities[64];
static cl_entity_t *entity_by_index(int index) {return index>=0&&index<64?&entities[index]:nullptr;}
static cl_entity_t *view_model() {return &entities[63];}
static int client_command(char *text) {printf("[client command] %s\n",text);return 1;}
static void cvar_set_value(char *name,float value) {auto v=get_variable(name);v->value=value;}
static void cvar_set(char *name,char *value) {auto v=get_variable(name);free(v->string);v->string=strdup(value);v->value=atof(value);}
static const char *local_info(const char*) {return "";}
static int player_unique_id(int player,char id[16]) {memset(id,0,16);id[0]=static_cast<char>(player);return 1;}
static int server_command(char *text) {printf("[server command] %s\n",text);return 1;}
static int app_id() {return 70;}
// A PE client can export or register a name only if the string is in the DLL;
// older clients lack HUD_GetStudioModelInterface or GameClientExports001.
static bool pe_names_interface(const char *name) {
    std::ifstream file(asset_root+"/cl_dlls/client.dll",std::ios::binary);
    std::string data((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
    return data.find(std::string(name,strlen(name)+1))!=std::string::npos;
}
// Decay's HUD keeps sprite model pointers from VidInit; an empty model stands in.
alignas(16) static unsigned char sprite_model[4096];
static const struct model_s *sprite_pointer(HSPRITE) {return reinterpret_cast<const struct model_s*>(sprite_model);}
int main(int argc,char **argv) {
    setvbuf(stdout,nullptr,_IONBF,0);
    if(argc<2)return 2;
    const char *root=getenv("CB_ASSET_ROOT");if(!root){fprintf(stderr,"CB_ASSET_ROOT required\n");return 2;}
    asset_root=root;game_directory=asset_root.substr(asset_root.find_last_of('/')+1);
    const char *until=argc>2?argv[2]:"vid";
    // Stand in for the engine: load and configure the native filesystem the
    // PE reaches through the bridge's virtual filesystem_stdio.dll.
    if(const char *native_fs=getenv("CB_FILESYSTEM_SO")) {
        void *library=dlopen(native_fs,RTLD_NOW|RTLD_GLOBAL);if(!library){fprintf(stderr,"%s\n",dlerror());return 3;}
        auto factory=reinterpret_cast<void*(*)(const char*,int*)>(dlsym(library,"CreateInterface"));
        auto fs=factory?static_cast<IFileSystem*>(factory(FILESYSTEM_INTERFACE_VERSION,nullptr)):nullptr;
        if(!fs){fprintf(stderr,"[smoke] native %s unavailable\n",FILESYSTEM_INTERFACE_VERSION);return 3;}
        std::string base=asset_root.substr(0,asset_root.find_last_of('/'));
        fs->Mount();
        fs->AddSearchPath(asset_root.c_str(),"GAME");fs->AddSearchPath(asset_root.c_str(),"GAMECONFIG");
        fs->AddSearchPath((base+"/valve").c_str(),"DEFAULTGAME");fs->AddSearchPath(base.c_str(),"ROOT");
        printf("[smoke] native filesystem: %s, %s exists=%d\n",native_fs,"liblist.gam",fs->FileExists("liblist.gam"));
    }
    void *so=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!so){fprintf(stderr,"%s\n",dlerror());return 3;}
    cl_enginefunc_t engine;fill_engine(engine,std::make_index_sequence<sizeof(cl_enginefunc_t)/sizeof(void*)>{});
    engine.pEventAPI=pEventAPI_table(std::make_index_sequence<64>{});
    engine.pTriAPI=pTriAPI_table(std::make_index_sequence<64>{});
    engine.pEfxAPI=pEfxAPI_table(std::make_index_sequence<128>{});
    engine.pDemoAPI=pDemoAPI_table(std::make_index_sequence<16>{});
    engine.pNetAPI=pNetAPI_table(std::make_index_sequence<16>{});
    engine.pVoiceTweak=nullptr;
    engine.pfnHookEvent=hook_event;engine.pfnHookUserMsg=hook_message;engine.pfnAddCommand=add_command;
    engine.pfnRegisterVariable=register_variable;engine.pfnGetCvarFloat=get_float;engine.pfnGetCvarString=get_string;
    engine.pfnGetCvarPointer=get_variable;engine.pfnGetGameDirectory=directory;
    engine.pfnGetScreenInfo=screen;engine.GetClientTime=client_time;engine.CheckParm=check_parameter;
    engine.Con_Printf=console;engine.Con_DPrintf=console;engine.Con_NPrintf=console_n;
    engine.Cmd_Argc=no_arguments;engine.Cmd_Argv=argument;
    engine.GetMousePosition=mouse_position;engine.pfnGetPlayerInfo=player_info;
    engine.GetViewAngles=get_angles;engine.SetViewAngles=set_angles;engine.GetClientMaxspeed=max_speed;
    engine.VGui_GetPanel=native_panel;engine.VGui_ViewportPaintBackground=paint_background;
    engine.pfnSPR_GetList=sprite_list;engine.pfnSPR_Load=sprite_load;engine.pfnSPR_Frames=sprite_frames;
    engine.pfnSPR_Width=sprite_dimension;engine.pfnSPR_Height=sprite_dimension;engine.pfnSetCrosshair=set_crosshair;
    engine.COM_LoadFile=load_file;engine.COM_FreeFile=free_file;engine.COM_ParseFile=parse_file;engine.pfnGetLevelName=level_name;
    engine.pfnTextMessageGet=text_message;engine.Key_LookupBinding=binding;engine.GetMaxClients=max_clients;
    engine.GetWindowCenterX=center;engine.GetWindowCenterY=center;engine.Con_IsVisible=console_visible;
    engine.GetLocalPlayer=get_local_player;engine.IsSpectateOnly=is_spectate_only;
    engine.COM_ExpandFilename=expand_filename;
    engine.GetEntityByIndex=entity_by_index;engine.GetViewModel=view_model;
    engine.pfnClientCmd=client_command;engine.pfnFilteredClientCmd=client_command;
    engine.GetPlayerUniqueID=player_unique_id;engine.pfnServerCmd=server_command;engine.pfnServerCmdUnreliable=server_command;
    engine.Cvar_SetValue=cvar_set_value;engine.Cvar_Set=cvar_set;engine.LocalPlayerInfo_ValueForKey=local_info;engine.GetAbsoluteTime=absolute_time;engine.pfnGetAppID=app_id;
    engine.GetSpritePointer=sprite_pointer;
    auto symbol=[&](const char *name) {void *p=dlsym(so,name);if(!p){fprintf(stderr,"[smoke] client.so lacks %s\n",name);std::_Exit(4);}return p;};
    stage="Initialize";
    if(reinterpret_cast<INITIALIZE_FUNC>(symbol("Initialize"))(&engine,CLDLL_INTERFACE_VERSION)!=1) {
        fprintf(stderr,"[smoke] FAIL stage=Initialize: client returned 0\n");return 5;
    }
    printf("[smoke] PASS Initialize: %u events, %u messages, %u commands, %zu cvars\n",hooks,messages,commands,variables.size());
    if(strcmp(until,"init")) {
        stage="HUD_Init";reinterpret_cast<HUD_INIT_FUNC>(symbol("HUD_Init"))();
        printf("[smoke] PASS HUD_Init: %u messages, %u commands, %zu cvars\n",messages,commands,variables.size());
    }
    if(!strcmp(until,"vid")) {
        stage="HUD_VidInit";reinterpret_cast<HUD_VIDINIT_FUNC>(symbol("HUD_VidInit"))();
        printf("[smoke] PASS HUD_VidInit: %d sprites\n",sprites_loaded);
    }
    if(!strcmp(until,"vid")) {
        stage="HUD_GetStudioModelInterface";
        engine_studio_api_t studio=*studio_table(std::make_index_sequence<64>{});
        studio.GetCvar=[](const char *name){return get_variable(name);};
        studio.IsHardware=[]{return 1;};
        // CStudioModelRenderer::Init keeps pointers to engine-owned state.
        static int model_count,poly_count;static float bones[128][3][4],lights[128][3][4],alias[3][4],rotation[3][4];
        static float *bone_rows[128][3],*light_rows[128][3],*alias_rows[3],*rotation_rows[3];
        static float **bone_table[128],**light_table[128],***bone_root=bone_table,***light_root=light_table;
        for(int b=0;b<128;b++){for(int r=0;r<3;r++){bone_rows[b][r]=bones[b][r];light_rows[b][r]=lights[b][r];}bone_table[b]=bone_rows[b];light_table[b]=light_rows[b];}
        for(int r=0;r<3;r++){alias_rows[r]=alias[r];rotation_rows[r]=rotation[r];}
        studio.GetChromeSprite=[]()->model_s*{return nullptr;};
        studio.GetModelCounters=[](int **s,int **a){*s=&model_count;*a=&poly_count;};
        studio.GetAliasScale=[](float *x,float *y){*x=*y=1;};
        studio.StudioGetBoneTransform=[]{return &bone_root;};
        studio.StudioGetLightTransform=[]{return &light_root;};
        studio.StudioGetAliasTransform=[]{static float **a=alias_rows;return &a;};
        studio.StudioGetRotationMatrix=[]{static float **r=rotation_rows;return &r;};
        r_studio_interface_t *studio_interface=nullptr;
        auto get=reinterpret_cast<int(*)(int,r_studio_interface_t**,engine_studio_api_t*)>(symbol("HUD_GetStudioModelInterface"));
        int result=get(STUDIO_INTERFACE_VERSION,&studio_interface,&studio);
        if(!result&&!pe_names_interface("HUD_GetStudioModelInterface")) {
            // Older clients (Gunman) leave model drawing to the engine's renderer.
            printf("[smoke] SKIP HUD_GetStudioModelInterface: the PE client does not export it\n");
        } else if(result!=1||!studio_interface||studio_interface->version!=STUDIO_INTERFACE_VERSION||!studio_interface->StudioDrawModel) {
            fprintf(stderr,"[smoke] FAIL stage=HUD_GetStudioModelInterface: result=%d interface=%p\n",result,(void*)studio_interface);return 7;
        } else printf("[smoke] PASS HUD_GetStudioModelInterface: PE studio renderer v%d\n",studio_interface->version);
    }
    if(!strcmp(until,"vid")) {
        // Native GameUI's view of the PE client: mute state must round-trip.
        stage="GameClientExports001";
        auto factory=reinterpret_cast<void*(*)(const char*,int*)>(symbol("CreateInterface"));
        int code=-1;
        auto exports=static_cast<IGameClientExports*>(factory(GAMECLIENTEXPORTS_INTERFACE_VERSION,&code));
        if(!exports&&!pe_names_interface(GAMECLIENTEXPORTS_INTERFACE_VERSION)) {
            printf("[smoke] SKIP GameClientExports001: the PE client does not register it (pre-HL25 SDK)\n");
        } else {
        if(!exports||code!=0){fprintf(stderr,"[smoke] FAIL stage=%s: interface not provided (code %d)\n",stage,code);return 8;}
        const char *host=exports->GetServerHostName();
        bool initially=exports->IsPlayerGameVoiceMuted(2);
        exports->MutePlayerGameVoice(2);bool muted=exports->IsPlayerGameVoiceMuted(2);
        exports->UnmutePlayerGameVoice(2);bool unmuted=!exports->IsPlayerGameVoiceMuted(2);
        if(!host||initially||!muted||!unmuted) {
            fprintf(stderr,"[smoke] FAIL stage=%s: host=%p initially=%d muted=%d unmuted=%d\n",stage,(const void*)host,initially,muted,unmuted);return 8;
        }
        printf("[smoke] PASS GameClientExports001: server name \"%s\", player 2 mute round-trip\n",host);
        }
    }
    // As at map start in the game: the server bridge joins the same PE runtime.
    void *server=nullptr;
    if(const char *server_so=getenv("CB_SMOKE_SERVER_SO")) {
        stage="server load";server=dlopen(server_so,RTLD_NOW|RTLD_LOCAL);
        if(!server){fprintf(stderr,"[smoke] FAIL stage=server load: %s\n",dlerror());return 6;}
        auto path=reinterpret_cast<const char*(*)()>(dlsym(server,"HLBridge_GetDLLPath"));
        if(!path||!path()||!dlsym(server,"GetEntityAPI2")){fprintf(stderr,"[smoke] FAIL stage=server load: no PE server\n");return 6;}
        printf("[smoke] PASS server bridge joined the client runtime: %s\n",path());
    }
    stage="HUD_Shutdown";reinterpret_cast<HUD_SHUTDOWN_FUNC>(symbol("HUD_Shutdown"))();
    printf("[smoke] PASS HUD_Shutdown\n");
    dlclose(so);
    if(server)dlclose(server);
    for(auto &entry:variables){free(entry.second.name);free(entry.second.string);}
    return 0;
}
