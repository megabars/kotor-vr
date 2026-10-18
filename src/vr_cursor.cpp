#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "vr_cursor.h"
#include "cursor_math.h"
#include <GL/gl.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace kvr {
namespace {
constexpr GLenum Texture0=0x84C0,ActiveTexture=0x84E0,CurrentProgram=0x8B8D,Texture3D=0x806F;
using ActiveTextureFn=void(APIENTRY*)(GLenum);
using UseProgramFn=void(APIENTRY*)(GLuint);
template<class T>T glFunction(const char* name) {
    auto p=wglGetProcAddress(name);const auto value=reinterpret_cast<uintptr_t>(p);
    return (!p||value<=3||value==uintptr_t(-1))?nullptr:reinterpret_cast<T>(p);
}
struct State {
    ActiveTextureFn active=glFunction<ActiveTextureFn>("glActiveTexture");
    UseProgramFn program=glFunction<UseProgramFn>("glUseProgram");
    GLint oldActive=Texture0,oldProgram=0,mode=GL_MODELVIEW;
    State() {
        glGetIntegerv(GL_MATRIX_MODE,&mode);
        if(active)glGetIntegerv(ActiveTexture,&oldActive);
        if(program)glGetIntegerv(CurrentProgram,&oldProgram);
        glPushAttrib(GL_ALL_ATTRIB_BITS);glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
        if(active)active(Texture0);
        glMatrixMode(GL_TEXTURE);glPushMatrix();
        glMatrixMode(GL_PROJECTION);glPushMatrix();
        glMatrixMode(GL_MODELVIEW);glPushMatrix();
    }
    ~State() {
        if(active)active(Texture0);
        glMatrixMode(GL_TEXTURE);glPopMatrix();
        glMatrixMode(GL_PROJECTION);glPopMatrix();
        glMatrixMode(GL_MODELVIEW);glPopMatrix();
        glPopClientAttrib();glPopAttrib();
        if(program)program(GLuint(oldProgram));
        if(active)active(oldActive);
        glMatrixMode(mode);
    }
};
struct Surface {
    HDC dc=CreateCompatibleDC(nullptr);
    HBITMAP bitmap=nullptr;
    HGDIOBJ old=nullptr;
    unsigned char* bits=nullptr;
    Surface(int w,int h,unsigned char background) {
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        if(dc)bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,reinterpret_cast<void**>(&bits),nullptr,0);
        if(bitmap) { old=SelectObject(dc,bitmap);memset(bits,background,size_t(w)*h*4); }
    }
    ~Surface() { if(old)SelectObject(dc,old);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc); }
};
}
struct VrCursor::Impl {
    int x=0,y=0,width=0,height=0,iconWidth=0,iconHeight=0,hotX=0,hotY=0;
    HCURSOR icon=nullptr;
    GLuint texture=0;
    HGLRC context=nullptr;
    DWORD lastIconUpdate=0;
    bool showing=false,worldSample=false,hit=false,ui=false;
    float cameraPoint[3]{},projection[16]{};
    unsigned char worldColor[3]{};

    bool locate(HDC dc,int w,int h) {
        showing=false;
        HWND window=WindowFromDC(dc);
        if(!window||GetAncestor(GetForegroundWindow(),GA_ROOT)!=GetAncestor(window,GA_ROOT))return false;
        CURSORINFO cursor{sizeof(CURSORINFO)};
        if(!GetCursorInfo(&cursor)||!(cursor.flags&CURSOR_SHOWING)||!cursor.hCursor)return false;
        RECT client{};POINT point=cursor.ptScreenPos;
        if(!GetClientRect(window,&client)||!ScreenToClient(window,&point)||
            client.right<=0||client.bottom<=0||point.x<0||point.y<0||point.x>=client.right||point.y>=client.bottom)return false;
        x=std::min(w-1,int(double(point.x)*w/client.right));
        y=std::min(h-1,int(double(point.y)*h/client.bottom));
        width=w;height=h;showing=true;
        if(icon!=cursor.hCursor||!texture||DWORD(GetTickCount()-lastIconUpdate)>200)updateIcon(cursor.hCursor);
        return true;
    }
    bool updateIcon(HCURSOR cursor) {
        ICONINFO info{};if(!GetIconInfo(cursor,&info))return false;
        BITMAP dimensions{};
        const bool color=info.hbmColor!=nullptr;
        const bool gotSize=GetObjectW(color?info.hbmColor:info.hbmMask,sizeof(dimensions),&dimensions)!=0;
        const int w=gotSize?dimensions.bmWidth:0,h=gotSize?(color?dimensions.bmHeight:dimensions.bmHeight/2):0;
        if(info.hbmColor)DeleteObject(info.hbmColor);if(info.hbmMask)DeleteObject(info.hbmMask);
        if(w<=0||h<=0||w>256||h>256)return false;
        Surface black(w,h,0),white(w,h,255);
        if(!black.bits||!white.bits)return false;
        if(!DrawIconEx(black.dc,0,0,cursor,w,h,0,nullptr,DI_NORMAL)||!DrawIconEx(white.dc,0,0,cursor,w,h,0,nullptr,DI_NORMAL))return false;
        GdiFlush();
        std::vector<unsigned char> rgba(size_t(w)*h*4);
        for(size_t i=0;i<rgba.size();i+=4) {
            int delta=0;bool xorPixel=false;
            for(int c=0;c<3;++c) { delta=std::max(delta,int(white.bits[i+c])-int(black.bits[i+c]));xorPixel|=white.bits[i+c]<black.bits[i+c]; }
            const int alpha=xorPixel?255:std::clamp(255-delta,0,255);
            rgba[i+3]=static_cast<unsigned char>(alpha);
            for(int c=0;c<3;++c)rgba[i+c]=alpha?static_cast<unsigned char>(std::clamp(int(black.bits[i+2-c])*255/alpha,0,255)):0;
        }
        State saved;
        if(!texture) { glGenTextures(1,&texture);context=wglGetCurrentContext(); }
        glBindTexture(GL_TEXTURE_2D,texture);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH,0);glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,0x812F);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,0x812F);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());
        icon=cursor;iconWidth=w;iconHeight=h;hotX=int(info.xHotspot);hotY=int(info.yHotspot);lastIconUpdate=GetTickCount();
        return true;
    }
    void readColor(unsigned char* color) {
        State saved;glReadBuffer(GL_BACK);
        glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_PACK_ROW_LENGTH,0);glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);
        glReadPixels(x,height-1-y,1,1,GL_RGB,GL_UNSIGNED_BYTE,color);
    }
};
VrCursor::VrCursor():impl_(std::make_unique<Impl>()){}
VrCursor::~VrCursor() { if(impl_->texture&&wglGetCurrentContext()==impl_->context)glDeleteTextures(1,&impl_->texture); }
void VrCursor::captureWorld(HDC dc,int w,int h,const float* projection) {
    auto& p=*impl_;p.worldSample=false;p.hit=false;p.ui=false;
    if(!projection||w<=0||h<=0||!p.locate(dc,w,h))return;
    memcpy(p.projection,projection,sizeof(p.projection));
    p.readColor(p.worldColor);
    float depth=1;
    { State saved;glReadBuffer(GL_BACK);glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_PACK_ROW_LENGTH,0);glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);
      glReadPixels(p.x,h-1-p.y,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&depth); }
    p.hit=depth>=0&&depth<0.999999f;
    if(!cursorUnproject(projection,float(p.x),float(p.y),w,h,p.hit?depth:0.5f,p.cameraPoint))return;
    if(!p.hit) { const float scale=8.0f/std::max(0.001f,std::fabs(p.cameraPoint[2]));for(float& v:p.cameraPoint)v*=scale; }
    p.worldSample=true;
}
void VrCursor::captureFinal(HDC dc,int w,int h) {
    auto& p=*impl_;
    if(!p.worldSample) { p.locate(dc,w,h);p.ui=true;return; }
    // Use the same pixel sampled during the game's own world pass. UI written
    // afterward must retain its 2D hit-test location instead of world depth.
    CURSORINFO ci{sizeof(CURSORINFO)};
    if(!GetCursorInfo(&ci)||!(ci.flags&CURSOR_SHOWING)) {p.showing=false;return;}
    unsigned char finalColor[3]{};p.readColor(finalColor);
    p.ui=false;for(int c=0;c<3;++c)if(std::abs(int(finalColor[c])-int(p.worldColor[c]))>2)p.ui=true;
}
bool VrCursor::draw(const float* projection,const float* viewOffset,bool fullPanel) {
    auto& p=*impl_;if(!p.showing||!p.texture||!projection||!viewOffset||!p.width||!p.height)return false;
    State saved;
    if(saved.program)saved.program(0);
    const char* extensions=reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if(extensions&&strstr(extensions,"GL_ARB_vertex_program"))glDisable(0x8620);
    if(extensions&&strstr(extensions,"GL_ARB_fragment_program"))glDisable(0x8804);
    GLint units=1;glGetIntegerv(0x84E2,&units);
    if(saved.active)for(int i=1;i<units;++i) { saved.active(Texture0+i);glDisable(GL_TEXTURE_2D);glDisable(GL_TEXTURE_1D);glDisable(Texture3D);glDisable(0x8513); }
    if(saved.active)saved.active(Texture0);
    glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_LIGHTING);glDisable(GL_FOG);glDisable(GL_STENCIL_TEST);
    glDisable(GL_ALPHA_TEST);glDisable(GL_SCISSOR_TEST);glDisable(GL_CULL_FACE);glDisable(GL_COLOR_LOGIC_OP);
    GLint clipPlanes=0;glGetIntegerv(GL_MAX_CLIP_PLANES,&clipPlanes);for(int i=0;i<clipPlanes;++i)glDisable(GL_CLIP_PLANE0+i);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    auto blendEquation=glFunction<void(APIENTRY*)(GLenum)>("glBlendEquation");if(blendEquation)blendEquation(0x8006);
    glDisable(GL_TEXTURE_1D);glDisable(Texture3D);glDisable(0x8513);
    glDisable(GL_TEXTURE_GEN_S);glDisable(GL_TEXTURE_GEN_T);glDisable(GL_TEXTURE_GEN_R);glDisable(GL_TEXTURE_GEN_Q);
    glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,p.texture);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);glColor4f(1,1,1,1);
    glMatrixMode(GL_TEXTURE);glLoadIdentity();glMatrixMode(GL_PROJECTION);glLoadMatrixf(projection);glMatrixMode(GL_MODELVIEW);glLoadMatrixf(viewOffset);
    float x,y,z,sx,sy;
    if(fullPanel||p.ui||!p.worldSample) {
        const float halfWidth=1.25f,halfHeight=halfWidth*float(p.height)/p.width;
        x=(2.0f*(p.x+0.5f)/p.width-1)*halfWidth;y=(1-2.0f*(p.y+0.5f)/p.height)*halfHeight;z=-2.2f;
        sx=2*halfWidth/p.width;sy=2*halfHeight/p.height;
    } else {
        x=p.cameraPoint[0];y=p.cameraPoint[1];z=p.cameraPoint[2];
        sx=2*std::fabs(z)/(std::max(.001f,std::fabs(p.projection[0]))*p.width);
        sy=2*std::fabs(z)/(std::max(.001f,std::fabs(p.projection[5]))*p.height);
    }
    sx*=1.5f;sy*=1.5f;
    const float left=x-p.hotX*sx,right=left+p.iconWidth*sx,top=y+p.hotY*sy,bottom=top-p.iconHeight*sy;
    glBegin(GL_QUADS);
    glTexCoord2f(0,0);glVertex3f(left,top,z);glTexCoord2f(1,0);glVertex3f(right,top,z);
    glTexCoord2f(1,1);glVertex3f(right,bottom,z);glTexCoord2f(0,1);glVertex3f(left,bottom,z);glEnd();
    return true;
}
void VrCursor::endFrame(){impl_->worldSample=false;impl_->showing=false;}
bool VrCursor::visible()const{return impl_->showing;}
bool VrCursor::overInterface()const{return impl_->ui;}
}
