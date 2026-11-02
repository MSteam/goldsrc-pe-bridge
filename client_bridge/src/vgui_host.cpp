/* Compiled with the Microsoft i386 C++ ABI. All VGUI objects stay in PE code. */
#include "vgui_host_api.h"
#include "vgui_abi.h"
#include "VGUI_Panel.h"
#include "VGUI_App.h"
#include "VGUI_SurfaceBase.h"
#include "VGUI_Font.h"
static vgui_host_api api;
extern "C" { int _fltused=0; } // MSVC's link marker for floating-point code.
extern "C" void *memset(void *out,int value,unsigned int count) {
    unsigned char *p=static_cast<unsigned char*>(out);while(count--)*p++=value;return out;
}
void *operator new(unsigned int count) {return api.allocate(count);}
void *operator new[](unsigned int count) {return api.allocate(count);}
void operator delete(void *p) noexcept {api.release(p);}
void operator delete[](void *p) noexcept {api.release(p);}
extern "C" void exit(int) {api.fatal("Windows VGUI host exit");}
extern "C" int _purecall() {api.fatal("Windows VGUI pure virtual call");return 0;}
class HostApp:public vgui::App {
public:
    HostApp():App(true) {}
    void main(int,char**) override {}
    long getTimeMillis() override {return api.milliseconds();}
    void platTick() override {}
};
class HostSurface:public vgui::SurfaceBase {
    vgui::Font *font=nullptr;int text_x=0,text_y=0;int text_color[4]={255,255,255,0};int glyph_texture=0;
public:
    HostSurface(vgui::Panel *p):SurfaceBase(p) {}
    void setTitle(const char*) override {}
    bool setFullscreenMode(int,int,int) override {return false;}
    void setWindowedMode() override {}
    void setAsTopMost(bool) override {}
    void createPopup(vgui::Panel *p) override {p->setParent(_embeddedPanel);}
    bool hasFocus() override {return true;}
    bool isWithin(int x,int y) override {return x>=0&&y>=0&&x<_embeddedPanel->getWide()&&y<_embeddedPanel->getTall();}
    int createNewTextureID() override {return api.texture_id();}
#if CB_VGUI_HAS_MOUSEPOS
    void GetMousePos(int &x,int &y) override {api.mouse(&x,&y);}
#endif
protected:
    void drawSetColor(int r,int g,int b,int a) override {api.color(r,g,b,a);}
    void drawFilledRect(int a,int b,int c,int d) override {api.rectangle(a,b,c,d,0);}
    void drawOutlinedRect(int a,int b,int c,int d) override {api.rectangle(a,b,c,d,1);}
    void drawSetTextFont(vgui::Font *f) override {font=f;}
    void drawSetTextColor(int r,int g,int b,int a) override {text_color[0]=r;text_color[1]=g;text_color[2]=b;text_color[3]=a;}
    void drawSetTextPos(int x,int y) override {text_x=x;text_y=y;}
    void drawPrintText(const char *text,int length) override {
        if(!font)return;
        int tall=font->getTall();if(tall<=0||tall>512)api.fatal("invalid Windows VGUI font height");
        api.color(text_color[0],text_color[1],text_color[2],text_color[3]);
        if(!glyph_texture)glyph_texture=api.texture_id();
        for(int i=0;i<length;i++) {
            unsigned char ch=text[i];int a,b,c;font->getCharABCwide(ch,a,b,c);
            if(ch=='\n'){text_x=0;text_y+=tall;continue;}
            if(b>0 && b<=512) {
                unsigned bytes=b*tall*4;unsigned char *rgba=static_cast<unsigned char*>(api.allocate(bytes));
                memset(rgba,0,bytes);font->getCharRGBA(ch,0,0,b,tall,rgba);
                api.texture_rgba(glyph_texture,reinterpret_cast<const char*>(rgba),b,tall);api.texture(glyph_texture);
                api.textured_rectangle(text_x+a,text_y,text_x+a+b,text_y+tall);api.release(rgba);
            }
            text_x+=a+b+c;
        }
    }
    void drawSetTextureRGBA(int id,const char *rgba,int w,int h) override {api.texture_rgba(id,rgba,w,h);}
    void drawSetTexture(int id) override {api.texture(id);}
    void drawTexturedRect(int a,int b,int c,int d) override {api.textured_rectangle(a,b,c,d);}
    void invalidate(vgui::Panel*) override {}
    void enableMouseCapture(bool state) override {if(!state)vgui::App::getInstance()->setMouseCapture(nullptr);}
    void setCursor(vgui::Cursor *cursor) override {api.cursor(cursor?cursor->getDefaultCursor():0);}
    void swapBuffers() override {}
    void pushMakeCurrent(vgui::Panel *p,bool insets) override {
        int x,y,unused_w,unused_h;p->getAbsExtents(x,y,unused_w,unused_h);int a,b,c,d;p->getClipRect(a,b,c,d);
        if(insets){int l,t,r,bottom;p->getInset(l,t,r,bottom);x+=l;y+=t;}
        api.push(x,y,a,b,c,d);
    }
    void popMakeCurrent(vgui::Panel*) override {api.pop();}
    void applyChanges() override {}
};
static HostApp *app;
static vgui::Panel *root;
static HostSurface *surface;
/* The Windows engine creates the VGUI App before it loads the client; HL25
 * clients already query App-owned fonts during HUD_Init, before any panel. */
extern "C" __declspec(dllexport) void CBVGUI_CreateApp(const vgui_host_api *callbacks) {
    api=*callbacks;if(!app)app=new HostApp;
}
extern "C" __declspec(dllexport) void *CBVGUI_Create(const vgui_host_api *callbacks,int w,int h) {
    CBVGUI_CreateApp(callbacks);root=new vgui::Panel(0,0,w,h);surface=new HostSurface(root);
    root->setSurfaceBaseTraverse(surface);root->setPaintBackgroundEnabled(false);return root;
}
extern "C" __declspec(dllexport) void CBVGUI_Paint(int w,int h) {
    root->setSize(w,h);app->externalTick();root->solveTraverse();
    // A new native GL frame has no persistent window backing store.
    root->repaintAll();root->paintTraverse();
}
extern "C" __declspec(dllexport) void CBVGUI_Mouse(int x,int y,int button,int down) {
    app->internalCursorMoved(x,y,surface);
    if(button>=0) {
        if(down)app->internalMousePressed(static_cast<vgui::MouseCode>(button),surface);
        else app->internalMouseReleased(static_cast<vgui::MouseCode>(button),surface);
    }
}
extern "C" __declspec(dllexport) void CBVGUI_Key(int code,int down) {
    if(down){app->internalKeyPressed(static_cast<vgui::KeyCode>(code),surface);app->internalKeyTyped(static_cast<vgui::KeyCode>(code),surface);}
    else app->internalKeyReleased(static_cast<vgui::KeyCode>(code),surface);
}
/* Explicit test overlay exercises the Windows Font -> GDI -> native GL path. */
class FontTestPanel:public vgui::Panel {
    vgui::Font font;
public:
    FontTestPanel():Panel(20,20,420,45),font("Arial",20,0,0,400,false,false,false,false) {
        setParent(root);setPaintBackgroundEnabled(false);setPaintBorderEnabled(false);
    }
protected:
    void paint() override {
        drawSetColor(0,0,0,30);drawFilledRect(0,0,420,45);
        drawSetTextFont(&font);drawSetTextColor(255,255,255,0);drawSetTextPos(10,10);
        const char text[]="Windows VGUI font in native Linux";
        drawPrintText(text,sizeof(text)-1);
    }
};
extern "C" __declspec(dllexport) void CBVGUI_TestFont() {new FontTestPanel;}
