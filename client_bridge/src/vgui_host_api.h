#pragma once
struct vgui_host_api {
    void *(*allocate)(unsigned int);
    void (*release)(void*);
    void (*fatal)(const char*);
    unsigned (*milliseconds)();
    void (*mouse)(int*,int*);
    void (*cursor)(int);
    int (*texture_id)();
    void (*color)(int,int,int,int);
    void (*rectangle)(int,int,int,int,int);
    void (*texture_rgba)(int,const char*,int,int);
    void (*texture)(int);
    void (*textured_rectangle)(int,int,int,int);
    void (*push)(int,int,int,int,int,int);
    void (*pop)();
};
