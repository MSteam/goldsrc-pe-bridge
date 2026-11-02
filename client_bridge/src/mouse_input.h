#pragma once
#include "sdk.h"

void cb_mouse_initialize(const cl_enginefunc_t *engine);
void cb_mouse_activate(bool active);
void cb_mouse_clear();
void cb_mouse_cursor_changed();
void cb_mouse_get(int *x,int *y);
void cb_mouse_set(int x,int y);

/* Only legacy gameplay input sees a virtual, relative-motion cursor. GUI
 * callbacks keep their usual absolute position and never consume that input. */
struct ClientMouseScope {
    ClientMouseScope();
    ~ClientMouseScope();
    ClientMouseScope(const ClientMouseScope&)=delete;
    ClientMouseScope& operator=(const ClientMouseScope&)=delete;
};
