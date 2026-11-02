#include "VGUI_Panel.h"
#include "VGUI_App.h"
#include "native_viewport.h"
#include "runtime.h"
extern "C" void ClientBridge_PaintAt(int,int,int,int);
/* A Linux VGUI child connects the native compositor to the separate PE tree.
 * The Windows viewport's background calls the engine, which draws the world
 * and invokes HUD_Redraw. HUD_Redraw must not recursively paint this tree. */
class NativeViewport:public vgui::Panel {
public:
    NativeViewport(int w,int h):Panel(0,0,w,h) {
        setPaintBorderEnabled(false);setPaintEnabled(false);setPaintBackgroundEnabled(true);
    }
protected:
    void paintBackground() override {
        if(auto parent=getParent())setSize(parent->getWide(),parent->getTall());
        // Native VGUI extents include the SDL window's desktop position.
        // The engine uses that origin when setting its viewport/scissor.
        int x0,y0,x1,y1;getAbsExtents(x0,y0,x1,y1);
        ClientBridge_PaintAt(x0,y0,x1,y1);
    }
    void internalCursorMoved(int x,int y) override {
        auto mouse=reinterpret_cast<void(*)(int,int,int,int)>(cb_host_symbol("CBVGUI_Mouse"));
        if(mouse)mouse(x,y,-1,0);
    }
    void internalMousePressed(vgui::MouseCode code) override {mouse(code,true);}
    void internalMouseReleased(vgui::MouseCode code) override {mouse(code,false);}
    void internalKeyPressed(vgui::KeyCode code) override {key(code,true);}
    void internalKeyReleased(vgui::KeyCode code) override {key(code,false);}
    void internalKeyTyped(vgui::KeyCode) override {}
private:
    void mouse(vgui::MouseCode code,bool down) {
        int x,y;getApp()->getCursorPos(x,y);
        auto callback=reinterpret_cast<void(*)(int,int,int,int)>(cb_host_symbol("CBVGUI_Mouse"));
        if(callback)callback(x,y,code,down);
    }
    void key(vgui::KeyCode code,bool down) {
        auto callback=reinterpret_cast<void(*)(int,int)>(cb_host_symbol("CBVGUI_Key"));
        if(callback)callback(code,down);
    }
};
static NativeViewport *viewport;
static vgui::Cursor *cursors[vgui::Cursor::dc_last];
static int cursor_code=vgui::Cursor::dc_none;
bool cb_native_viewport_cursor_visible() {return cursor_code!=vgui::Cursor::dc_none;}
void cb_native_viewport_cursor(int code) {
    if(code<=vgui::Cursor::dc_user||code>=vgui::Cursor::dc_last)return;
    cursor_code=code;if(!viewport)return;
    if(!cursors[code])cursors[code]=new vgui::Cursor(static_cast<vgui::Cursor::DefaultCursor>(code));
    viewport->setCursor(cursors[code]);
}
void cb_native_viewport_attach(void *parent,int width,int height) {
    if(viewport)return;
    viewport=new NativeViewport(width,height);viewport->setParent(static_cast<vgui::Panel*>(parent));
    cb_native_viewport_cursor(cursor_code);
}
void cb_native_viewport_detach(void *live_parent) {
    // Engine shutdown frees its root before calling HUD_Shutdown. Its public
    // VGui_GetPanel callback then returns null; never dereference that old root.
    if(!viewport)return;
    if(live_parent)viewport->setParent(nullptr);
    delete viewport;viewport=nullptr;
    for(auto &cursor:cursors){delete cursor;cursor=nullptr;}
}
