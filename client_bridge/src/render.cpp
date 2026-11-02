#include <GL/gl.h>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>
#include <array>
#include <string>
#include "render.h"
static bool headless;
static int screen_width,screen_height;
static std::map<int,std::vector<unsigned char>> textures;
static unsigned uploads;
static float color[4]={1,1,1,1};
static std::vector<std::array<int,6>> panels;
static int next_id=1;
[[noreturn]] static void fail(const char *message) {fprintf(stderr,"[client render] %s\n",message);std::abort();}
static void context() {if(!headless&&!glGetString(GL_VERSION))fail("OpenGL context required");}
__attribute__((force_align_arg_pointer)) static int texture_id() {
    context();GLuint id;if(headless)id=next_id++;else glGenTextures(1,&id);
    if(!id)fail("texture allocation");textures[id]={};return id;
}
__attribute__((force_align_arg_pointer)) static void set_color(int r,int g,int b,int a) {
    color[0]=r/255.0f;color[1]=g/255.0f;color[2]=b/255.0f;color[3]=1-a/255.0f;
}
__attribute__((force_align_arg_pointer)) static void rectangle(int x,int y,int right,int bottom,int outline) {
    if(headless)return;glDisable(GL_TEXTURE_2D);glColor4fv(color);glBegin(outline?GL_LINE_LOOP:GL_QUADS);
    glVertex2i(x,y);glVertex2i(right,y);glVertex2i(right,bottom);glVertex2i(x,bottom);glEnd();
}
__attribute__((force_align_arg_pointer)) static void rgba(int id,const char *data,int w,int h) {
    if(w<1||h<1||w>4096||h>4096||!textures.count(id))fail("texture upload dimensions/id");
    uploads++;if(headless){textures[id].assign(data,data+w*h*4);return;}
    context();glPushAttrib(GL_TEXTURE_BIT);glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glBindTexture(GL_TEXTURE_2D,id);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,data);glPopClientAttrib();glPopAttrib();
}
__attribute__((force_align_arg_pointer)) static void set_texture(int id) {
    if(!headless){glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,id);}
}
__attribute__((force_align_arg_pointer)) static void textured(int x,int y,int right,int bottom) {
    if(headless)return;glEnable(GL_TEXTURE_2D);glColor4fv(color);glBegin(GL_QUADS);
    glTexCoord2f(0,0);glVertex2i(x,y);glTexCoord2f(1,0);glVertex2i(right,y);
    glTexCoord2f(1,1);glVertex2i(right,bottom);glTexCoord2f(0,1);glVertex2i(x,bottom);glEnd();
}
static void apply_panel(const std::array<int,6> &p) {
    if(headless)return;
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();glTranslatef(p[0],p[1],0);
    glScissor(p[2],screen_height-p[5],p[4]>p[2]?p[4]-p[2]:0,p[5]>p[3]?p[5]-p[3]:0);
}
__attribute__((force_align_arg_pointer)) static void push(int x,int y,int a,int b,int c,int d) {panels.push_back({x,y,a,b,c,d});apply_panel(panels.back());}
__attribute__((force_align_arg_pointer)) static void pop() {
    if(panels.empty())fail("surface stack underflow");panels.pop_back();if(!panels.empty())apply_panel(panels.back());
}
void cb_render_configure(vgui_host_api &api,int width,int height) {
    headless=std::getenv("CB_HEADLESS_VGUI")&&std::string(std::getenv("CB_HEADLESS_VGUI"))=="1";
    screen_width=width;screen_height=height;
    api.texture_id=texture_id;api.color=set_color;api.rectangle=rectangle;api.texture_rgba=rgba;
    api.texture=set_texture;api.textured_rectangle=textured;api.push=push;api.pop=pop;
}
void cb_render_begin(int width,int height) {
    screen_width=width;screen_height=height;context();panels.clear();
    if(headless)return;
    glPushAttrib(GL_ALL_ATTRIB_BITS);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_ALPHA_TEST);glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glEnable(GL_SCISSOR_TEST);
    glViewport(0,0,width,height);glMatrixMode(GL_PROJECTION);glPushMatrix();glLoadIdentity();glOrtho(0,width,height,0,-1,1);
    glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadIdentity();
}
void cb_render_end() {
    if(!panels.empty())fail("unbalanced surface stack");if(headless)return;
    glMatrixMode(GL_MODELVIEW);glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();glPopAttrib();
}
void cb_render_unload() {
    if(!headless&&glGetString(GL_VERSION))for(auto &entry:textures){GLuint id=entry.first;glDeleteTextures(1,&id);}
    textures.clear();panels.clear();next_id=1;
}
unsigned cb_render_glyph_uploads() {return uploads;}
void cb_render_capture(const char *path) {
    if(headless||!path)return;
    std::vector<unsigned char> pixels(screen_width*screen_height*3);
    glPushAttrib(GL_PIXEL_MODE_BIT);glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    glPixelStorei(GL_PACK_ALIGNMENT,1);glReadBuffer(GL_BACK);
    glReadPixels(0,0,screen_width,screen_height,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
    glPopClientAttrib();glPopAttrib();
    FILE *file=fopen(path,"wb");if(!file)fail("test capture file");
    fprintf(file,"P6\n%d %d\n255\n",screen_width,screen_height);
    for(int y=screen_height-1;y>=0;y--)
        if(fwrite(pixels.data()+y*screen_width*3,3,screen_width,file)!=(unsigned)screen_width)fail("test capture write");
    fclose(file);
}
