#pragma once
void cb_native_viewport_attach(void *parent,int width,int height);
void cb_native_viewport_detach(void *live_parent);
void cb_native_viewport_cursor(int code);
bool cb_native_viewport_cursor_visible();
