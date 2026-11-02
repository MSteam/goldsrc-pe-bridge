#include <cassert>
#include <cstdio>
#include "mouse_input.h"
#include "cvardef.h"
#include "SDL_mouse.h"
#include "SDL_keyboard.h"

static bool visible,console,focused=true,keyboard_focused=true,unsupported;
static SDL_bool relative=SDL_FALSE;
static int dx,dy,warps,reads,sets,absolute_x=650,absolute_y=355;
static cvar_t grab{};
static SDL_Window *window=reinterpret_cast<SDL_Window*>(1);
bool cb_native_viewport_cursor_visible() {return visible;}
extern "C" SDL_Window *SDL_GetMouseFocus() {return focused?window:nullptr;}
extern "C" SDL_Window *SDL_GetKeyboardFocus() {return keyboard_focused?window:nullptr;}
extern "C" SDL_bool SDL_GetRelativeMouseMode() {return relative;}
extern "C" int SDL_SetRelativeMouseMode(SDL_bool enabled) {
    sets++;if(unsupported)return -1;
    relative=enabled;dx=dy=0;return 0;
}
extern "C" unsigned SDL_GetRelativeMouseState(int *x,int *y) {
    reads++;*x=dx;*y=dy;dx=dy=0;return 0;
}
extern "C" unsigned SDL_GetMouseState(int *x,int *y) {*x=absolute_x;*y=absolute_y;return 0;}
extern "C" void SDL_GetWindowSize(SDL_Window*,int *w,int *h) {*w=1280;*h=720;}
extern "C" void SDL_WarpMouseInWindow(SDL_Window*,int x,int y) {warps++;absolute_x=x;absolute_y=y;}
static int center_x() {return 1720;}
static int center_y() {return 720;}
static int is_console() {return console;}
static cvar_t *variable(const char*) {return &grab;}
static void fallback(int *x,int *y) {*x=17;*y=23;}
static void position(int x,int y) {
    int actual_x,actual_y;cb_mouse_get(&actual_x,&actual_y);
    assert(actual_x==x&&actual_y==y);
}
int main() {
    grab.value=1;
    cl_enginefunc_t engine{};
    engine.GetWindowCenterX=center_x;engine.GetWindowCenterY=center_y;
    engine.GetMousePosition=fallback;engine.Con_IsVisible=is_console;engine.pfnGetCvarPointer=variable;
    cb_mouse_initialize(&engine);
    dx=4;dy=-5;position(1730,715);assert(dx==4&&dy==-5);
    cb_mouse_set(1720,720);assert(absolute_x==640&&absolute_y==360&&warps==1);
    cb_mouse_activate(true);assert(relative==SDL_TRUE&&dx==0&&dy==0);
    unsigned warp_count=warps;
    {
        ClientMouseScope scope;
        dx=dy=1;position(1721,721);position(1721,721);
        cb_mouse_set(1720,720);position(1720,720);
        dx=1;dy=-1;position(1721,719);
        dx=2;dy=3;cb_mouse_set(1720,720);position(1722,723);
        assert(warps==warp_count);
    }
    // GUI reads neither consume gameplay deltas nor see the virtual cursor.
    dx=9;dy=8;auto previous_reads=reads;
    position(1720,720);assert(dx==9&&dy==8&&reads==previous_reads);
    cb_mouse_clear();assert(dx==0&&dy==0);
    dx=2;dy=3;cb_mouse_activate(true);assert(dx==2&&dy==3);
    {ClientMouseScope scope;position(1722,723);cb_mouse_set(1720,720);}
    visible=true;cb_mouse_cursor_changed();assert(relative==SDL_FALSE);
    {
        ClientMouseScope scope;position(1720,720);cb_mouse_set(1730,725);
        assert(relative==SDL_FALSE&&absolute_x==650&&absolute_y==365);
    }
    visible=false;cb_mouse_cursor_changed();assert(relative==SDL_TRUE);
    console=true;
    {ClientMouseScope scope;assert(relative==SDL_FALSE);}
    console=false;
    {ClientMouseScope scope;assert(relative==SDL_TRUE);dx=dy=1;position(1721,721);}
    keyboard_focused=false;cb_mouse_clear();assert(relative==SDL_FALSE);
    keyboard_focused=true;
    {ClientMouseScope scope;assert(relative==SDL_TRUE);}
    focused=false;
    {ClientMouseScope scope;assert(relative==SDL_FALSE);position(17,23);}
    focused=true;
    {ClientMouseScope scope;assert(relative==SDL_TRUE);}
    cb_mouse_activate(false);assert(relative==SDL_FALSE);
    auto previous_sets=sets;cb_mouse_activate(false);assert(sets==previous_sets);

    // Capture already owned by the engine must not be disabled by our adapter.
    relative=SDL_TRUE;cb_mouse_initialize(&engine);
    cb_mouse_activate(true);cb_mouse_activate(false);assert(relative==SDL_TRUE);
    relative=SDL_FALSE;grab.value=0;cb_mouse_activate(true);
    {ClientMouseScope scope;assert(relative==SDL_FALSE);}
    cb_mouse_activate(false);grab.value=1;unsupported=true;
    cb_mouse_activate(true);
    {ClientMouseScope scope;assert(relative==SDL_FALSE);position(1730,725);}
    cb_mouse_activate(false);
    puts("PASS: relative mouse capture, GUI/console/focus transitions, pending input preserved across recenter/repeated activation, engine ownership, disabled grab and unsupported fallback");
}
