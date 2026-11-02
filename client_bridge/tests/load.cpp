#include <stdio.h>
#include "runtime.h"
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    if(gb_load(argv[1]))return 3;
    const char *symbols[]={"Initialize","HUD_Init","HUD_VidInit","HUD_Redraw","CL_CreateMove"};
    for(unsigned i=0;i<sizeof(symbols)/sizeof(*symbols);i++) {
        void *address=gb_symbol(symbols[i]);printf("client export %s=%p\n",symbols[i],address);
        if(!address)return 4;
    }
    printf("PASS: original VGUI and client DLL initialized in Linux\n");
    gb_unload();return 0;
}
