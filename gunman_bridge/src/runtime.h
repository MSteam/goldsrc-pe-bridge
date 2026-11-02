#ifndef GUNMAN_BRIDGE_RUNTIME_H
#define GUNMAN_BRIDGE_RUNTIME_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int gb_load(const char *filename);
void *gb_symbol(const char *name);
int gb_visit_exports(int(*visit)(const char*,void*,int,void*),void *context);
const char *gb_symbol_name(void *address);
int gb_contains(void *address);
void gb_report(void);
void gb_unload(void);
#ifdef __cplusplus
}
#endif
#endif
