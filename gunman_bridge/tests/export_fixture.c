/* PE32 fixture deliberately unrelated to Gunman; no CRT or external imports. */
int FixtureData=9182;
int __stdcall DllMain(void *module,unsigned reason,void *reserved) {
    (void)module;(void)reason;(void)reserved;return 1;
}
void __stdcall GiveFnptrsToDll(void *functions,void *globals) {(void)functions;(void)globals;}
static const char *description(void) {return "BridgeFixture";}
int GetEntityAPI(void **table,int version) {
    if(!table||version!=140)return 0;
    void *volatile *slots=table;
    for(unsigned i=0;i<50;i++)slots[i]=0;
    slots[28]=(void*)description;return 1;
}
int GetNewDLLFunctions(void **table,int *version) {
    if(!table||!version)return 0;
    if(*version!=1){*version=1;return 0;}
    void *volatile *slots=table;
    for(unsigned i=0;i<5;i++)slots[i]=0;
    return 1;
}
void CompletelyNewEntityWithMixedCASE(void *vars) {*(int*)vars=0x4150;}
void AnotherUnlistedEntity(void *vars) {*(int*)vars=0x4250;}
void FixtureAlias(void *vars) {*(int*)vars=0x4350;}
