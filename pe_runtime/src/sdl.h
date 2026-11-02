#ifndef GOLDSRC_PE_SDL_H
#define GOLDSRC_PE_SDL_H
/* Adapter for an SDL2.dll import, or NULL when that function is not bridged. */
void *cb_sdl_symbol(const char *name);
#endif
