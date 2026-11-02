#ifndef GOLDSRC_PE_FMOD_H
#define GOLDSRC_PE_FMOD_H
/* Virtual fmod.dll (FMOD 3.x streams) backed by libmpg123 and SDL2 audio. */
void *cb_fmod_module(void);            /* LoadLibrary: handle, or NULL */
int cb_is_fmod_module(void *handle);
void *cb_fmod_symbol(const char *name); /* GetProcAddress */
void cb_fmod_unload(void);              /* stop output before the PE goes away */
#endif
