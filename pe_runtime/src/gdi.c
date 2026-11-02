/* The bitmap/font subset used by the original VGUI library. No Windows HWNDs. */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <fontconfig/fontconfig.h>
#include "winnt_types.h"
/* Win32 BOOL is a 32-bit int; the loader headers declare it as uint8_t,
 * which would leave the upper bits of EAX and of BOOL* outputs undefined. */
typedef int WIN_BOOL;
enum {FONT=1,BITMAP,REGION,DC};
struct object {unsigned kind;struct object *next;};
struct font {struct object object;FT_Face face;int height,weight,italic,symbol;};
struct bitmap {struct object object;int width,height,bpp,stride,topdown;unsigned char *data;};
struct rect {int left,top,right,bottom;};
struct region {struct object object;struct rect rect;};
struct dc {struct object object;struct font *font;struct bitmap *bitmap;uint32_t fg,bg,align;int bk,x,y;};
static struct object *objects;
static FT_Library ft;
__attribute__((noreturn)) static void fail(const char *what) {fprintf(stderr,"[client GDI] unsupported: %s\n",what);abort();}
static void *make(unsigned kind,size_t size) {
    struct object *o=calloc(1,size);if(!o)fail("allocation");o->kind=kind;o->next=objects;objects=o;return o;
}
static struct object *object(void *p,unsigned kind) {
    for(struct object *o=objects;o;o=o->next)if(o==p&&o->kind==kind)return o;
    fail("invalid GDI object");
}
static void * WINAPI w_CreateFontA(int height,int width,int escapement,int orientation,int weight,
    unsigned italic,unsigned underline,unsigned strike,unsigned charset,unsigned output,
    unsigned clip,unsigned quality,unsigned pitch,const char *name) {
    (void)width;(void)charset;(void)output;(void)clip;(void)quality;(void)pitch;
    if(escapement||orientation||underline||strike)fail("rotated or decorated font");
    if(!ft&&FT_Init_FreeType(&ft))fail("FreeType initialization");
    FcPattern *pattern=FcPatternCreate();FcPatternAddString(pattern,FC_FAMILY,(const FcChar8*)(name&&*name?name:"sans-serif"));
    FcPatternAddInteger(pattern,FC_WEIGHT,weight>=600?FC_WEIGHT_BOLD:FC_WEIGHT_NORMAL);
    FcPatternAddInteger(pattern,FC_SLANT,italic?FC_SLANT_ITALIC:FC_SLANT_ROMAN);
    FcConfigSubstitute(NULL,pattern,FcMatchPattern);FcDefaultSubstitute(pattern);FcResult result;
    FcPattern *match=FcFontMatch(NULL,pattern,&result);FcPatternDestroy(pattern);
    FcChar8 *file=NULL;int index=0;
    if(!match||FcPatternGetString(match,FC_FILE,0,&file)!=FcResultMatch)fail("no matching native font");
    FcPatternGetInteger(match,FC_INDEX,0,&index);
    struct font *font=make(FONT,sizeof(*font));
    if(FT_New_Face(ft,(const char*)file,index,&font->face))fail("font loading");
    if(FT_Select_Charmap(font->face,FT_ENCODING_UNICODE)) {
        if(FT_Select_Charmap(font->face,FT_ENCODING_MS_SYMBOL))fail("Unicode or symbol charmap");
        font->symbol=1;
    }
    FcPatternDestroy(match);int pixels=height<0?-height:height;if(!pixels)pixels=16;
    if(height>0 && font->face->height>0)pixels=(int)lround((double)pixels*font->face->units_per_EM/font->face->height);
    if(pixels<1)pixels=1;
    if(FT_Set_Pixel_Sizes(font->face,0,pixels))fail("font size");
    font->height=(font->face->size->metrics.height+63)>>6;
    font->weight=weight;font->italic=italic;return font;
}
static void * WINAPI w_CreateCompatibleDC(void *source) {(void)source;struct dc *dc=make(DC,sizeof(*dc));dc->bk=2;dc->fg=0;dc->bg=0xffffff;return dc;}
static void * WINAPI w_GetDC(void *window) {if(window)fail("GetDC for Windows HWND");return w_CreateCompatibleDC(NULL);}
static void * WINAPI w_SelectObject(void *handle,void *selected) {
    struct dc *dc=(void*)object(handle,DC);struct object *o=NULL;
    for(o=objects;o;o=o->next)if(o==selected)break;
    /* Restoring a null/default selection is allowed. */
    if(!selected){void *previous=dc->font;dc->font=NULL;return previous;}
    if(!o)fail("SelectObject foreign handle");
    if(o->kind==FONT){void *old=dc->font;dc->font=selected;return old;}
    if(o->kind==BITMAP){void *old=dc->bitmap;dc->bitmap=selected;return old;}
    fail("SelectObject kind");
}
static WIN_BOOL WINAPI w_DeleteObject(void *p) {
    struct object **at=&objects;while(*at&&*at!=p)at=&(*at)->next;
    if(!*at)return 0;
    struct object *o=*at;*at=o->next;
    if(o->kind==FONT)FT_Done_Face(((struct font*)o)->face);
    if(o->kind==BITMAP)free(((struct bitmap*)o)->data);
    free(o);return 1;
}
static struct bitmap *bitmap(int width,int height,int bits,int topdown) {
    if(width<=0||height<=0||width>4096||height>4096||(bits!=24&&bits!=32))fail("bitmap format or dimensions");
    struct bitmap *b=make(BITMAP,sizeof(*b));b->width=width;b->height=height;b->bpp=bits/8;
    b->stride=(width*b->bpp+3)&~3;b->topdown=topdown;b->data=calloc(height,b->stride);if(!b->data)fail("bitmap allocation");return b;
}
static void * WINAPI w_CreateDIBSection(void *dc,const void *info,unsigned usage,void **bits,void *section,unsigned offset) {
    (void)dc;const int32_t *p=info;const uint16_t *w=info;
    if(!info||p[0]!=40||w[6]!=1||p[4]||usage||section||offset)fail("DIBSection header");
    struct bitmap *b=bitmap(p[1],p[2]<0?-p[2]:p[2],w[7],p[2]<0);*bits=b->data;return b;
}
static void * WINAPI w_CreateCompatibleBitmap(void *dc,int width,int height) {(void)dc;return bitmap(width,height,32,0);}
static unsigned char *pixel(struct bitmap *b,int x,int y) {
    if(x<0||y<0||x>=b->width||y>=b->height)return NULL;
    return b->data+(b->topdown?y:b->height-1-y)*b->stride+x*b->bpp;
}
static void paint(struct bitmap *b,int x,int y,uint32_t color,unsigned alpha) {
    unsigned char *p=pixel(b,x,y);if(!p)return;
    unsigned char rgb[3]={color>>16,color>>8,color};
    for(unsigned i=0;i<3;i++)p[i]=(rgb[i]*alpha+p[i]*(255-alpha)+127)/255;
    if(b->bpp==4)p[3]=255;
}
static uint32_t WINAPI w_SetTextColor(void *p,uint32_t color) {struct dc *dc=(void*)object(p,DC);uint32_t old=dc->fg;dc->fg=color;return old;}
static uint32_t WINAPI w_SetBkColor(void *p,uint32_t color) {struct dc *dc=(void*)object(p,DC);uint32_t old=dc->bg;dc->bg=color;return old;}
static int WINAPI w_SetBkMode(void *p,int mode) {struct dc *dc=(void*)object(p,DC);if(mode!=1&&mode!=2)fail("background mode");int old=dc->bk;dc->bk=mode;return old;}
static uint32_t WINAPI w_SetTextAlign(void *p,uint32_t flags) {struct dc *dc=(void*)object(p,DC);uint32_t old=dc->align;dc->align=flags;return old;}
static WIN_BOOL WINAPI w_SetViewportOrgEx(void *p,int x,int y,int *old) {
    struct dc *dc=(void*)object(p,DC);if(old){old[0]=dc->x;old[1]=dc->y;}dc->x=x;dc->y=y;return 1;
}
static WIN_BOOL WINAPI w_MoveToEx(void *p,int x,int y,int *old) {return w_SetViewportOrgEx(p,x,y,old);}
static unsigned unicode(unsigned ch) {
    static const uint16_t upper[32]={0x20ac,0x81,0x201a,0x192,0x201e,0x2026,0x2020,0x2021,0x2c6,0x2030,0x160,0x2039,0x152,0x8d,0x17d,0x8f,0x90,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,0x2dc,0x2122,0x161,0x203a,0x153,0x9d,0x17e,0x178};
    return ch>=128&&ch<160?upper[ch-128]:ch;
}
static FT_GlyphSlot glyph(struct dc *dc,unsigned ch) {
    if(!dc->font)fail("no selected font");
    unsigned code=dc->font->symbol?0xf000u+ch:unicode(ch);
    unsigned idx=FT_Get_Char_Index(dc->font->face,code);
    /* Some Windows symbol fonts omit the .notdef outline. Missing characters
     * still have a width and produce an empty bitmap, as GDI does. */
    if(!idx) {
        static struct FT_GlyphSlotRec_ missing;memset(&missing,0,sizeof(missing));
        missing.advance.x=dc->font->face->size->metrics.max_advance;return &missing;
    }
    int error=FT_Load_Glyph(dc->font->face,idx,FT_LOAD_RENDER);
    if(error){fprintf(stderr,"[client GDI] FreeType error=%d char=%u index=%u face=%p\n",error,ch,idx,(void*)dc->font->face);fail("glyph loading");}return dc->font->face->glyph;
}
static WIN_BOOL WINAPI w_GetCharABCWidthsA(void *p,unsigned first,unsigned last,int *out) {
    struct dc *dc=(void*)object(p,DC);if(last<first||last>255)fail("glyph range");
    for(unsigned i=first;i<=last;i++,out+=3){FT_GlyphSlot g=glyph(dc,i);out[0]=g->bitmap_left;out[1]=g->bitmap.width;out[2]=(g->advance.x>>6)-out[0]-out[1];}return 1;
}
static WIN_BOOL WINAPI w_GetTextMetricsA(void *p,void *out) {
    struct dc *dc=(void*)object(p,DC);if(!dc->font)fail("font metrics selection");
    memset(out,0,56);int32_t *m=out;FT_Size_Metrics *ftm=&dc->font->face->size->metrics;
    m[0]=dc->font->height;m[1]=(ftm->ascender+63)>>6;m[2]=(-ftm->descender+63)>>6;
    m[5]=glyph(dc,'x')->advance.x>>6;m[6]=ftm->max_advance>>6;m[7]=dc->font->weight;m[9]=96;m[10]=96;
    unsigned char *bytes=(unsigned char*)out+44;bytes[0]=32;bytes[1]=255;bytes[2]='?';bytes[3]=' ';bytes[4]=dc->font->italic;bytes[7]=1;return 1;
}
static WIN_BOOL WINAPI w_ExtTextOutA(void *p,int x,int y,unsigned options,const struct rect *rect,const char *text,unsigned count,const int *spacing) {
    struct dc *dc=(void*)object(p,DC);if(!dc->bitmap)fail("text destination bitmap");
    if(options&~6u)fail("text output flags");
    if(options&2u){if(!rect)fail("opaque text rect");for(int yy=rect->top;yy<rect->bottom;yy++)for(int xx=rect->left;xx<rect->right;xx++)paint(dc->bitmap,xx+dc->x,yy+dc->y,dc->bg,255);}
    if(!count)return 1;
    int advance=0;for(unsigned i=0;i<count;i++)advance+=spacing?spacing[i]:glyph(dc,(unsigned char)text[i])->advance.x>>6;
    if((dc->align&6u)==6u)x-=advance/2;else if(dc->align&2u)x-=advance;
    int asc=(dc->font->face->size->metrics.ascender+63)>>6;
    int baseline=(dc->align&24u)==24u?y:((dc->align&8u)?y-dc->font->height+asc:y+asc);
    for(unsigned i=0;i<count;i++) {
        FT_GlyphSlot g=glyph(dc,(unsigned char)text[i]);int width=spacing?spacing[i]:g->advance.x>>6;
        if(dc->bk==2)for(int yy=baseline-asc;yy<baseline-asc+dc->font->height;yy++)for(int xx=x;xx<x+width;xx++)paint(dc->bitmap,xx+dc->x,yy+dc->y,dc->bg,255);
        for(unsigned yy=0;yy<g->bitmap.rows;yy++)for(unsigned xx=0;xx<g->bitmap.width;xx++) {
            int px=x+g->bitmap_left+xx,py=baseline-g->bitmap_top+yy;
            if((options&4u)&&rect&&(px<rect->left||px>=rect->right||py<rect->top||py>=rect->bottom))continue;
            unsigned alpha=g->bitmap.buffer[yy*g->bitmap.pitch+xx];paint(dc->bitmap,px+dc->x,py+dc->y,dc->fg,alpha);
        }
        x+=width;
    }
    return 1;
}
static WIN_BOOL WINAPI w_StretchBlt(void *dest,int x,int y,int width,int height,void *source,int sx,int sy,int sw,int sh,uint32_t rop) {
    struct dc *d=(void*)object(dest,DC),*s=(void*)object(source,DC);
    if(!d->bitmap||!s->bitmap||rop!=0xcc0020||width<=0||height<=0||sw<=0||sh<=0)fail("bitmap blit parameters");
    for(int yy=0;yy<height;yy++)for(int xx=0;xx<width;xx++) {
        unsigned char *p=pixel(s->bitmap,sx+xx*sw/width+s->x,sy+yy*sh/height+s->y);
        if(p)paint(d->bitmap,x+xx+d->x,y+yy+d->y,p[2]|p[1]<<8|p[0]<<16,255);
    }
    return 1;
}
static WIN_BOOL WINAPI w_BitBlt(void *d,int x,int y,int w,int h,void *s,int sx,int sy,uint32_t rop) {return w_StretchBlt(d,x,y,w,h,s,sx,sy,w,h,rop);}
static void * WINAPI w_CreateRectRgn(int a,int b,int c,int d) {struct region *r=make(REGION,sizeof(*r));r->rect=(struct rect){a,b,c,d};return r;}
static WIN_BOOL WINAPI w_SetRectRgn(void *p,int a,int b,int c,int d) {struct region *r=(void*)object(p,REGION);r->rect=(struct rect){a,b,c,d};return 1;}
void *cb_gdi_symbol(const char *name) {
    struct {const char *name;void *address;} functions[]={
#define GDI(name) {#name,w_##name}
        GDI(CreateFontA),GDI(CreateCompatibleDC),GDI(GetDC),GDI(SelectObject),GDI(DeleteObject),
        GDI(CreateDIBSection),GDI(CreateCompatibleBitmap),GDI(SetTextColor),GDI(SetBkColor),GDI(SetBkMode),
        GDI(SetTextAlign),GDI(SetViewportOrgEx),GDI(MoveToEx),GDI(GetCharABCWidthsA),GDI(GetTextMetricsA),
        GDI(ExtTextOutA),GDI(BitBlt),GDI(StretchBlt),GDI(CreateRectRgn),GDI(SetRectRgn)
    };
    for(unsigned i=0;i<sizeof(functions)/sizeof(*functions);i++)if(!strcmp(name,functions[i].name))return functions[i].address;
    return NULL;
}
void cb_gdi_unload(void) {while(objects)w_DeleteObject(objects);if(ft){FT_Done_FreeType(ft);ft=NULL;}}
