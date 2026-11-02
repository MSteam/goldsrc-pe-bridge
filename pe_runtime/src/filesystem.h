#ifndef GOLDSRC_PE_FILESYSTEM_H
#define GOLDSRC_PE_FILESYSTEM_H
/* Virtual filesystem_stdio.dll backed by the engine's native IFileSystem. */
void *cb_filesystem_module(void);           /* LoadLibrary: handle, or NULL */
int cb_is_filesystem_module(void *handle);
void *cb_filesystem_symbol(const char *name); /* GetProcAddress */
#endif
