/* SDL2.dll imports of HL25-era PE clients, bound to the engine's native SDL2.
 *
 * SDL's Windows and Linux i386 builds both use cdecl and the same scalar and
 * pointer arguments for these functions, so each adapter only realigns the
 * stack (MSVC callers guarantee 4 bytes) and calls the native function.
 * The client uses them as the native Linux HL25 client does: relative mouse
 * deltas while capture is on, and the game-controller API. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include "sdl.h"

static void *native_symbol(const char *name) {
    /* The engine links SDL2; look in that copy first, then in the global scope. */
    static void *library;
    if(!library)library=dlopen("libSDL2-2.0.so.0",RTLD_NOW|RTLD_NOLOAD);
    void *symbol=library?dlsym(library,name):NULL;
    if(!symbol)symbol=dlsym(RTLD_DEFAULT,name);
    if(!symbol) {
        fprintf(stderr,"[bridge] native SDL2 function %s is unavailable\n",name);
        fflush(NULL);abort();
    }
    return symbol;
}
#define ADAPTER __attribute__((force_align_arg_pointer,noinline))
#define NATIVE(name,type) static __typeof__(type) native_##name; if(!native_##name)native_##name=(type)native_symbol(#name)

ADAPTER static int sdl_NumJoysticks(void) {
    NATIVE(SDL_NumJoysticks,int(*)(void));return native_SDL_NumJoysticks();
}
ADAPTER static int sdl_IsGameController(int index) {
    NATIVE(SDL_IsGameController,int(*)(int));return native_SDL_IsGameController(index);
}
ADAPTER static void *sdl_GameControllerOpen(int index) {
    NATIVE(SDL_GameControllerOpen,void*(*)(int));return native_SDL_GameControllerOpen(index);
}
ADAPTER static const char *sdl_GameControllerName(void *controller) {
    NATIVE(SDL_GameControllerName,const char*(*)(void*));return native_SDL_GameControllerName(controller);
}
ADAPTER static int16_t sdl_GameControllerGetAxis(void *controller,int axis) {
    NATIVE(SDL_GameControllerGetAxis,int16_t(*)(void*,int));return native_SDL_GameControllerGetAxis(controller,axis);
}
ADAPTER static uint8_t sdl_GameControllerGetButton(void *controller,int button) {
    NATIVE(SDL_GameControllerGetButton,uint8_t(*)(void*,int));return native_SDL_GameControllerGetButton(controller,button);
}
ADAPTER static void sdl_JoystickUpdate(void) {
    NATIVE(SDL_JoystickUpdate,void(*)(void));native_SDL_JoystickUpdate();
}
ADAPTER static int sdl_SetRelativeMouseMode(int enabled) {
    NATIVE(SDL_SetRelativeMouseMode,int(*)(int));return native_SDL_SetRelativeMouseMode(enabled);
}
ADAPTER static uint32_t sdl_GetRelativeMouseState(int *x,int *y) {
    NATIVE(SDL_GetRelativeMouseState,uint32_t(*)(int*,int*));return native_SDL_GetRelativeMouseState(x,y);
}

static const struct {const char *name;void *adapter;} adapters[]={
#define ENTRY(name) {"SDL_"#name,(void*)sdl_##name}
    ENTRY(NumJoysticks),ENTRY(IsGameController),ENTRY(GameControllerOpen),ENTRY(GameControllerName),
    ENTRY(GameControllerGetAxis),ENTRY(GameControllerGetButton),ENTRY(JoystickUpdate),
    ENTRY(SetRelativeMouseMode),ENTRY(GetRelativeMouseState)
#undef ENTRY
};
void *cb_sdl_symbol(const char *name) {
    for(size_t i=0;i<sizeof(adapters)/sizeof(*adapters);i++)
        if(!strcmp(adapters[i].name,name))return adapters[i].adapter;
    return NULL; /* bound to a named trap */
}
