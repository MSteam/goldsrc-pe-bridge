#define _GNU_SOURCE 1
#include "elf_exports.h"
#include "export_slots.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <elf.h>
#include <map>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

extern "C" __attribute__((visibility("hidden"))) unsigned char hlbridge_slots_begin[],hlbridge_slots_end[];
namespace {
[[noreturn]] void fail(const char *message) {throw std::runtime_error(message);}
uint32_t hash_name(const char *name) {
    uint32_t hash=0;
    while(*name) {
        hash=(hash<<4)+static_cast<unsigned char>(*name++);
        uint32_t top=hash&0xf0000000u;
        if(top)hash^=top>>24;
        hash&=~top;
    }
    return hash;
}
struct Mapping {uintptr_t begin,end;int protection;};
class WritablePages {
    std::map<uintptr_t,int> pages;
    std::vector<Mapping> mappings;
    size_t page_size;
public:
    WritablePages():page_size(static_cast<size_t>(sysconf(_SC_PAGESIZE))) {
        FILE *file=std::fopen("/proc/self/maps","r");if(!file)fail("Cannot read ELF page protections");
        char line[8192];
        while(std::fgets(line,sizeof(line),file)) {
            unsigned long begin,end;char flags[5];
            if(std::sscanf(line,"%lx-%lx %4s",&begin,&end,flags)!=3)continue;
            int protection=(flags[0]=='r'?PROT_READ:0)|(flags[1]=='w'?PROT_WRITE:0)|(flags[2]=='x'?PROT_EXEC:0);
            mappings.push_back({begin,end,protection});
        }
        std::fclose(file);
    }
    void add(void *address,size_t length) {
        uintptr_t start=reinterpret_cast<uintptr_t>(address)&~(page_size-1);
        uintptr_t end=(reinterpret_cast<uintptr_t>(address)+length+page_size-1)&~(page_size-1);
        for(auto page=start;page<end;page+=page_size) {
            auto mapping=std::find_if(mappings.begin(),mappings.end(),[page](const Mapping &m){return page>=m.begin&&page<m.end;});
            if(mapping==mappings.end()||!(mapping->protection&PROT_READ))fail("ELF metadata lies outside readable mappings");
            pages.emplace(page,mapping->protection);
        }
    }
    void make_writable() {
        for(auto entry:pages)
            if(mprotect(reinterpret_cast<void*>(entry.first),page_size,entry.second|PROT_WRITE))fail("Cannot make ELF export pages writable");
    }
    void restore() {
        for(auto entry:pages)
            if(mprotect(reinterpret_cast<void*>(entry.first),page_size,entry.second))fail("Cannot restore ELF export page protections");
    }
};
}

size_t bridge_publish_exports(const BridgeExport *exports,size_t count,unsigned *calls) {
    static_assert(sizeof(void*)==4,"PE32 bridge requires ELF32");
    Dl_info location{};
    if(!dladdr(reinterpret_cast<void*>(&bridge_publish_exports),&location)||!location.dli_fbase)fail("Cannot locate ELF header");
    uintptr_t base=reinterpret_cast<uintptr_t>(location.dli_fbase);
    auto header=reinterpret_cast<Elf32_Ehdr*>(base);
    if(std::memcmp(header->e_ident,ELFMAG,SELFMAG)||header->e_ident[EI_CLASS]!=ELFCLASS32||header->e_machine!=EM_386)
        fail("Unexpected server bridge ELF architecture");
    auto segments=reinterpret_cast<Elf32_Phdr*>(base+header->e_phoff);
    Elf32_Dyn *dynamic=nullptr;size_t dynamic_count=0;
    for(unsigned i=0;i<header->e_phnum;i++)if(segments[i].p_type==PT_DYNAMIC) {
        dynamic=reinterpret_cast<Elf32_Dyn*>(base+segments[i].p_vaddr);
        dynamic_count=segments[i].p_memsz/sizeof(*dynamic);
    }
    char *strings=nullptr;Elf32_Sym *symbols=nullptr;uint32_t *hash=nullptr;size_t string_size=0;
    for(size_t i=0;i<dynamic_count&&dynamic[i].d_tag!=DT_NULL;i++) {
        uintptr_t pointer=dynamic[i].d_un.d_ptr;if(pointer<base)pointer+=base;
        switch(dynamic[i].d_tag) {
        case DT_STRTAB:strings=reinterpret_cast<char*>(pointer);break;
        case DT_STRSZ:string_size=dynamic[i].d_un.d_val;break;
        case DT_SYMTAB:symbols=reinterpret_cast<Elf32_Sym*>(pointer);break;
        case DT_SYMENT:if(dynamic[i].d_un.d_val!=sizeof(Elf32_Sym))fail("Unexpected ELF symbol size");break;
        case DT_HASH:hash=reinterpret_cast<uint32_t*>(pointer);break;
        case DT_GNU_HASH:fail("Runtime exports require --hash-style=sysv");
        default:break;
        }
    }
    if(!strings||!symbols||!hash||!string_size)fail("Missing ELF export tables");
    uint32_t bucket_count=hash[0],symbol_count=hash[1];
    if(!bucket_count||symbol_count<HLBRIDGE_EXPORT_CAPACITY)fail("Invalid ELF export reserve");
    uint32_t *buckets=hash+2,*chains=buckets+bucket_count;
    std::vector<char*> slots(HLBRIDGE_EXPORT_CAPACITY,nullptr);
    std::map<std::string,uint32_t> native_names;
    uintptr_t code_begin=reinterpret_cast<uintptr_t>(hlbridge_slots_begin);
    uintptr_t code_end=reinterpret_cast<uintptr_t>(hlbridge_slots_end);
    if(code_end-code_begin!=HLBRIDGE_EXPORT_CAPACITY*HLBRIDGE_EXPORT_STRIDE)fail("Invalid ELF trampoline reserve");
    for(uint32_t i=1;i<symbol_count;i++) {
        if(symbols[i].st_name>=string_size)fail("ELF symbol name outside string table");
        char *name=strings+symbols[i].st_name;
        uintptr_t address=base+symbols[i].st_value;
        if(address>=code_begin&&address<code_end&&symbols[i].st_shndx!=SHN_UNDEF) {
            size_t index=(address-code_begin)/HLBRIDGE_EXPORT_STRIDE;
            if((address-code_begin)%HLBRIDGE_EXPORT_STRIDE||slots[index]||std::strlen(name)+1!=HLBRIDGE_EXPORT_NAME_BYTES)
                fail("Invalid reserved ELF symbol");
            slots[index]=name;
        } else if(*name)native_names.emplace(name,i);
    }
    for(auto slot:slots)if(!slot)fail("Missing reserved ELF symbol");
    std::vector<BridgeExport> selected;
    for(size_t i=0;i<count;i++) {
        if(!exports[i].name||!exports[i].target)fail("Invalid PE export");
        // Preserve native ABI adapters and native imports, including lazy ones.
        if(native_names.count(exports[i].name))continue;
        if(std::strlen(exports[i].name)+1>HLBRIDGE_EXPORT_NAME_BYTES)
            throw std::runtime_error("PE export name exceeds ELF reserve: "+std::string(exports[i].name));
        selected.push_back(exports[i]);
    }
    if(selected.size()>slots.size())fail("PE export count exceeds ELF reserve (4096)");
    WritablePages pages;
    pages.add(strings,string_size);
    pages.add(hash,(2+bucket_count+symbol_count)*sizeof(uint32_t));
    pages.add(hlbridge_slots_begin,selected.size()*HLBRIDGE_EXPORT_STRIDE);
    pages.make_writable();
    for(size_t i=0;i<selected.size();i++) {
        std::strcpy(slots[i],selected[i].name);
        unsigned char *code=hlbridge_slots_begin+i*HLBRIDGE_EXPORT_STRIDE;
        // Count calls without changing arguments, registers, flags or the stack.
        code[0]=0x9c;code[1]=0xff;code[2]=0x05;
        uint32_t counter=reinterpret_cast<uintptr_t>(calls);std::memcpy(code+3,&counter,4);
        code[7]=0x9d;code[8]=0xe9;
        uint32_t distance=reinterpret_cast<uintptr_t>(selected[i].target)-(reinterpret_cast<uintptr_t>(code)+13);
        std::memcpy(code+9,&distance,4);
    }
    // Keep cached table pointers/counts unchanged. Rebuild lookup chains in place.
    std::memset(buckets,0,(bucket_count+symbol_count)*sizeof(uint32_t));
    for(uint32_t i=1;i<symbol_count;i++)if(symbols[i].st_name) {
        uint32_t bucket=hash_name(strings+symbols[i].st_name)%bucket_count;
        chains[i]=buckets[bucket];buckets[bucket]=i;
    }
    pages.restore();
    return selected.size();
}
