#ifndef GOLDSRC_PE_RUNTIME_H
#define GOLDSRC_PE_RUNTIME_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int gb_load(const char *filename);
void *gb_symbol(const char *name);
const char *gb_symbol_name(void *address);
int gb_contains(void *address);
void gb_report(void);
void gb_unload(void);
void *cb_host_symbol(const char *name);
void *cb_vgui_symbol(const char *name);
void cb_set_file_loader(unsigned char *(*load)(char*,int,int*),void(*release)(void*),const char *directory);
void cb_set_mouse(void(*get)(int*,int*),void(*set)(int,int));
#ifdef __cplusplus
}
#endif
#endif
