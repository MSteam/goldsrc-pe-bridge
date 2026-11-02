#include "mouse_input.h"
#include "native_viewport.h"
#include "cvardef.h"
#include "SDL_mouse.h"
#include "SDL_keyboard.h"

namespace {
const cl_enginefunc_t *engine;
cvar_t *grab;
bool active,owned_capture;
unsigned input_depth;
int cursor_dx,cursor_dy;

bool wants_capture() {
    if(!engine||!active||(grab&&!grab->value)||cb_native_viewport_cursor_visible())return false;
    if(engine->Con_IsVisible&&engine->Con_IsVisible())return false;
    auto window=SDL_GetMouseFocus();
    return window&&SDL_GetKeyboardFocus()==window;
}
void synchronize_capture() {
    if(wants_capture()) {
        if(SDL_GetRelativeMouseMode()==SDL_FALSE&&SDL_SetRelativeMouseMode(SDL_TRUE)==0) {
            owned_capture=true;
            cursor_dx=cursor_dy=0;
        }
    } else if(owned_capture) {
        SDL_SetRelativeMouseMode(SDL_FALSE);
        owned_capture=false;cursor_dx=cursor_dy=0;
    }
}
bool relative_input() {
    return input_depth&&wants_capture()&&SDL_GetRelativeMouseMode()==SDL_TRUE;
}
}

void cb_mouse_initialize(const cl_enginefunc_t *functions) {
    engine=functions;
    grab=engine->pfnGetCvarPointer?engine->pfnGetCvarPointer("cl_mousegrab"):nullptr;
    active=owned_capture=false;input_depth=0;cursor_dx=cursor_dy=0;
}
void cb_mouse_clear() {
    synchronize_capture();
    cursor_dx=cursor_dy=0;
    if(SDL_GetRelativeMouseMode()==SDL_TRUE) {
        int x,y;SDL_GetRelativeMouseState(&x,&y);
    }
}
void cb_mouse_activate(bool enabled) {
    if(active==enabled){synchronize_capture();return;}
    active=enabled;synchronize_capture();cb_mouse_clear();
}
void cb_mouse_cursor_changed() {synchronize_capture();}
ClientMouseScope::ClientMouseScope() {++input_depth;synchronize_capture();}
ClientMouseScope::~ClientMouseScope() {--input_depth;}

__attribute__((force_align_arg_pointer)) void cb_mouse_get(int *x,int *y) {
    if(relative_input()) {
        int dx,dy;SDL_GetRelativeMouseState(&dx,&dy);
        cursor_dx+=dx;cursor_dy+=dy;
        *x=engine->GetWindowCenterX()+cursor_dx;
        *y=engine->GetWindowCenterY()+cursor_dy;
    } else if(auto window=SDL_GetMouseFocus()) {
        int width,height;SDL_GetWindowSize(window,&width,&height);
        SDL_GetMouseState(x,y);
        *x+=engine->GetWindowCenterX()-width/2;
        *y+=engine->GetWindowCenterY()-height/2;
    } else engine->GetMousePosition(x,y);
}
__attribute__((force_align_arg_pointer)) void cb_mouse_set(int x,int y) {
    if(relative_input()) {
        // Recenter only the emulated cursor. A desktop warp would throw away
        // subpixel motion and could discard events arriving after GetCursorPos.
        cursor_dx=x-engine->GetWindowCenterX();
        cursor_dy=y-engine->GetWindowCenterY();
    } else if(auto window=SDL_GetMouseFocus()) {
        int width,height;SDL_GetWindowSize(window,&width,&height);
        SDL_WarpMouseInWindow(window,x-engine->GetWindowCenterX()+width/2,
                             y-engine->GetWindowCenterY()+height/2);
    } else if(engine->pfnSetMousePos)engine->pfnSetMousePos(x,y);
}
