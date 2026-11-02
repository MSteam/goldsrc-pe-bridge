#include <cstdio>
#include <cstddef>
#include "extdll.h"
#include "pm_defs.h"
#define SIZE(T) std::printf("sizeof.%s=%zu\n", #T, sizeof(T))
#define OFFSET(T,F) std::printf("offsetof.%s.%s=%zu\n", #T, #F, offsetof(T,F))
int main() {
    SIZE(void*); SIZE(enginefuncs_t); SIZE(DLL_FUNCTIONS);
    SIZE(globalvars_t); SIZE(edict_t); SIZE(entvars_t);
    SIZE(TraceResult); SIZE(TYPEDESCRIPTION); SIZE(playermove_t);
    OFFSET(edict_t,pvPrivateData); OFFSET(edict_t,v);
    OFFSET(entvars_t,pContainingEntity); OFFSET(globalvars_t,pStringBase);
    OFFSET(enginefuncs_t,pfnFunctionFromName);
    OFFSET(enginefuncs_t,pfnNameForFunction);
    std::printf("interface_version=%d\n", INTERFACE_VERSION);
}
