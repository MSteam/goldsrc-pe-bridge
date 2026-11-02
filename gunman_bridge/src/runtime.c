/* Both wrappers use one Windows TEB/TLS/import runtime in a listen server. */
#include "runtime.h"
extern int cb_server_load(const char*);
extern void *cb_server_symbol(const char*);
extern int cb_server_visit_exports(int(*)(const char*,void*,int,void*),void*);
extern const char *cb_server_symbol_name(void*);
extern int cb_server_contains(void*);
extern void cb_server_unload(void);
extern void cb_report(void);
int gb_load(const char *path) {return cb_server_load(path);}
void *gb_symbol(const char *name) {return cb_server_symbol(name);}
int gb_visit_exports(int(*visit)(const char*,void*,int,void*),void *context) {return cb_server_visit_exports(visit,context);}
const char *gb_symbol_name(void *address) {return cb_server_symbol_name(address);}
int gb_contains(void *address) {return cb_server_contains(address);}
void gb_unload(void) {cb_server_unload();}
void gb_report(void) {cb_report();}
