#pragma once
#include "vgui_host_api.h"
void cb_render_configure(vgui_host_api &api,int width,int height);
void cb_render_begin(int width,int height);
void cb_render_end();
void cb_render_unload();
unsigned cb_render_glyph_uploads();
void cb_render_capture(const char *path);
