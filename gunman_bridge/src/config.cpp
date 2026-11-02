#include "config.h"
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <stdexcept>
#include <unistd.h>

namespace {
std::string parent(const std::string &path) {
    auto slash=path.find_last_of('/');
    return slash==0?"/":path.substr(0,slash);
}
std::string canonical(std::string path) {
    for(char &c:path)if(c=='\\')c='/';
    char resolved[4096];
    if(!realpath(path.c_str(),resolved))
        throw std::runtime_error("Cannot resolve Windows DLL "+path+": "+std::strerror(errno));
    return resolved;
}
class Tokens {
    const std::string &text;
    size_t at=0;
public:
    explicit Tokens(const std::string &source):text(source) {
        if(text.compare(0,3,"\xef\xbb\xbf")==0)at=3;
    }
    bool next(std::string &out) {
        for(;;) {
            while(at<text.size()&&std::isspace(static_cast<unsigned char>(text[at])))++at;
            if(text.compare(at,2,"//")==0) {
                while(at<text.size()&&text[at]!='\n')++at;
            } else if(text.compare(at,2,"/*")==0) {
                auto end=text.find("*/",at+2);
                if(end==std::string::npos)throw std::runtime_error("Unclosed comment in liblist.gam");
                at=end+2;
            } else break;
        }
        if(at==text.size())return false;
        out.clear();
        if(text[at]=='"') {
            ++at;
            while(at<text.size()&&text[at]!='"')out+=text[at++];
            if(at==text.size())throw std::runtime_error("Unclosed quoted value in liblist.gam");
            ++at;
        } else {
            while(at<text.size()&&!std::isspace(static_cast<unsigned char>(text[at])))out+=text[at++];
        }
        return true;
    }
};
}

std::string bridge_dll_path(void *module_anchor) {
    const char *override_path=std::getenv("HLBRIDGE_DLL");
    if(!override_path||!*override_path)override_path=std::getenv("GUNMAN_BRIDGE_DLL");
    if(override_path&&*override_path)return canonical(override_path);
    Dl_info info{};
    if(!dladdr(module_anchor,&info)||!info.dli_fname)
        throw std::runtime_error("Cannot locate the server bridge");
    std::string path=info.dli_fname;
    if(path.empty()||path.front()!='/') {
        char cwd[4096];if(!getcwd(cwd,sizeof(cwd)))throw std::runtime_error("Cannot get current directory");
        path=std::string(cwd)+"/"+path;
    }
    // Keep the loaded path's mod directory even when the .so is a symlink.
    std::string directory=parent(path);
    for(;;) {
        std::string config=directory+"/liblist.gam";
        FILE *file=std::fopen(config.c_str(),"rb");
        if(file) {
            std::string text;char buffer[4096];size_t count;
            while((count=std::fread(buffer,1,sizeof(buffer),file))!=0) {
                text.append(buffer,count);
                if(text.size()>1024*1024){std::fclose(file);throw std::runtime_error("liblist.gam is too large: "+config);}
            }
            bool failed=std::ferror(file);std::fclose(file);
            if(failed)throw std::runtime_error("Cannot read "+config);
            Tokens tokens(text);std::string key,value;
            while(tokens.next(key)) {
                if(!tokens.next(value))throw std::runtime_error("Missing value for "+key+" in "+config);
                if(key!="gamedll")continue;
                if(value.empty())throw std::runtime_error("Empty gamedll in "+config);
                for(char &c:value)if(c=='\\')c='/';
                if(value.size()>1&&value[1]==':')throw std::runtime_error("Windows drive path is not supported: "+value);
                return canonical(value.front()=='/'?value:directory+"/"+value);
            }
            throw std::runtime_error("No gamedll entry in "+config);
        }
        if(errno!=ENOENT&&errno!=ENOTDIR)throw std::runtime_error("Cannot open "+config+": "+std::strerror(errno));
        if(directory=="/")break;
        directory=parent(directory);
    }
    throw std::runtime_error("No liblist.gam above server bridge "+path+" (or set HLBRIDGE_DLL)");
}
