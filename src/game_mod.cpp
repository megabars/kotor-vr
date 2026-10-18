#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <GL/gl.h>
#include <MinHook.h>
#include "vr_backend.h"
#include "hud_overlay.h"
#include "vr_cursor.h"
#include "kvr_version.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <memory>
#include <string>
#include <vector>
#include <share.h>

namespace {
// Address database matches the launcher's exact SHA-256 gate. The renderer and
// camera layout were checked in Ghidra 12.1.4 against that same game binary.
constexpr uintptr_t kCameraRender = 0x45c540;
constexpr uintptr_t kSceneRender = 0x4512d0;
using CameraRender = void (__thiscall*)(void*);
using SceneRender = void (__thiscall*)(void*);
using Swap = BOOL (WINAPI*)(HDC);
CameraRender realCameraRender = nullptr;
SceneRender realSceneRender = nullptr;
Swap realSwap = nullptr;
HMODULE moduleHandle = nullptr;
FILE* logFile = nullptr;
std::unique_ptr<kvr::VrBackend> vr;
std::unique_ptr<kvr::HudOverlay> hud;
std::unique_ptr<kvr::VrCursor> cursor;
bool worldCaptured=false;
void* candidateCamera = nullptr;
int candidateArea = 0;
bool renderingEye = false;
int currentEye = -1;
bool showPanel = false;
bool enabled = true;
bool initialized = false;
DWORD nextInitAttempt = 0;
unsigned long stereoFrames = 0;
GLuint desktopTexture = 0;
int desktopWidth = 0, desktopHeight = 0;
float worldScale = 1.0f;
const kvr::EyeData* selectedEye = nullptr;
bool smokePending = false;
std::wstring outputDir;
float originalProjection[16]{};
float originalView[16]{};
bool haveOriginalMatrices=false;

template <class T> T& field(void* object, size_t offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}
void log(const char* format, ...) {
    char line[2048];
    va_list args; va_start(args, format); vsnprintf_s(line, sizeof(line), _TRUNCATE, format, args); va_end(args);
    if (logFile) { fprintf(logFile, "%lu %s\n", GetTickCount(), line); fflush(logFile); }
    OutputDebugStringA(line); OutputDebugStringA("\n");
}
void xrLog(const char* text) { log("XR %s", text); }
void multiply(const float* a, const float* b, float* result) {
    float out[16]{};
    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
        for (int k = 0; k < 4; ++k) out[c*4+r] += a[k*4+r]*b[c*4+k];
    memcpy(result, out, sizeof(out));
}

struct GLState {
    GLint matrixMode = GL_MODELVIEW;
    GLState() {
        glGetIntegerv(GL_MATRIX_MODE, &matrixMode);
        glPushAttrib(GL_ALL_ATTRIB_BITS);
        glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
        glMatrixMode(GL_PROJECTION); glPushMatrix();
        glMatrixMode(GL_MODELVIEW); glPushMatrix();
        glMatrixMode(GL_TEXTURE); glPushMatrix();
        glMatrixMode(matrixMode);
    }
    ~GLState() {
        glMatrixMode(GL_TEXTURE); glPopMatrix();
        glMatrixMode(GL_MODELVIEW); glPopMatrix();
        glMatrixMode(GL_PROJECTION); glPopMatrix();
        glPopClientAttrib(); glPopAttrib(); glMatrixMode(matrixMode);
    }
};

void __fastcall cameraHook(void* camera, void*) {
    if (!renderingEye && wglGetCurrentContext()) {
        GLint viewport[4]; glGetIntegerv(GL_VIEWPORT, viewport);
        int width = field<int>(camera, 0x1ec), height = field<int>(camera, 0x1f0);
        if (!width) width = viewport[2];
        if (!height) height = viewport[3];
        const int screenW = *reinterpret_cast<int*>(0x78d1d4);
        const int screenH = *reinterpret_cast<int*>(0x78d1d8);
        const int area = width * height;
        if (field<void*>(camera, 0x6c) && field<float>(camera, 0x1d0) > 0 &&
            width >= screenW * 3 / 4 && height >= screenH * 3 / 4 && area >= candidateArea) {
            candidateCamera = camera; candidateArea = area;
            static void* previous = nullptr;
            if (previous != camera) { log("Main camera=%p scene=%p viewport=%dx%d fov=%f",camera,field<void*>(camera,0x6c),width,height,field<float>(camera,0x1d0)); previous=camera; }
        }
    }
    realCameraRender(camera);
    if(!renderingEye && candidateCamera==camera) {
        if(!hud)hud=std::make_unique<kvr::HudOverlay>();
        worldCaptured=hud->captureWorld(*reinterpret_cast<int*>(0x78d1d4),*reinterpret_cast<int*>(0x78d1d8));
        if(!cursor)cursor=std::make_unique<kvr::VrCursor>();
        cursor->captureWorld(wglGetCurrentDC(),*reinterpret_cast<int*>(0x78d1d4),*reinterpret_cast<int*>(0x78d1d8),haveOriginalMatrices?originalProjection:nullptr);
        static bool reported=false;
        if(!reported) { log("HUD world capture=%d %s",int(worldCaptured),worldCaptured?"ready":hud->lastError());reported=true; }
    }
}

// The camera sets its world-space culling planes before Scene::Render. Rebuild
// those planes from the tracked per-eye clip matrix to match the actual view.
void updateFrustum(void* camera, const float* projection, const float* view) {
    float clip[16]; multiply(projection, view, clip);
    auto planes = field<float*>(camera, 0x1f8);
    if (!planes) return;
    constexpr int axes[6] = {2, 2, 0, 0, 1, 1};
    constexpr int signs[6] = {1, -1, 1, -1, -1, 1};
    for (int p = 0; p < 6; ++p) {
        float plane[4];
        for (int c = 0; c < 4; ++c) plane[c] = -(clip[c*4+3] + signs[p]*clip[c*4+axes[p]]);
        const float length = std::sqrt(plane[0]*plane[0]+plane[1]*plane[1]+plane[2]*plane[2]);
        if (length > 1e-8f) for (int k = 0; k < 4; ++k) planes[p*4+k] = plane[k]/length;
    }
    const int index = *reinterpret_cast<int*>(0x7bb4ec);
    if (index >= 0 && index < 6) {
        memcpy(reinterpret_cast<void*>(0x7bb4f8), planes+index*4, 12);
        *reinterpret_cast<float*>(0x7bb4e4) = planes[index*4+3];
    }
}
void __fastcall sceneHook(void* scene, void*) {
    if (!renderingEye && candidateCamera && field<void*>(candidateCamera,0x6c)==scene) {
        glGetFloatv(GL_PROJECTION_MATRIX,originalProjection);
        glGetFloatv(GL_MODELVIEW_MATRIX,originalView);
        if(!haveOriginalMatrices)log("Original projection diag=%f,%f,%f,%f view diag=%f,%f,%f,%f",originalProjection[0],originalProjection[5],originalProjection[10],originalProjection[15],originalView[0],originalView[5],originalView[10],originalView[15]);
        haveOriginalMatrices=true;
    }
    if (renderingEye && candidateCamera && selectedEye) {
        const auto& eye = *selectedEye;
        float baseView[16], eyeView[16];
        glGetFloatv(GL_MODELVIEW_MATRIX, baseView);
        multiply(eye.viewOffset, baseView, eyeView);
        glViewport(0, 0, eye.width, eye.height);
        glMatrixMode(GL_PROJECTION); glLoadMatrixf(eye.projection);
        glMatrixMode(GL_MODELVIEW); glLoadMatrixf(eyeView);
        glDisable(GL_SCISSOR_TEST);
        updateFrustum(candidateCamera, eye.projection, eyeView);
    }
    realSceneRender(scene);
}

void captureDesktop() {
    const int w = *reinterpret_cast<int*>(0x78d1d4), h = *reinterpret_cast<int*>(0x78d1d8);
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) return;
    glEnable(GL_TEXTURE_2D);
    if (!desktopTexture) glGenTextures(1, &desktopTexture);
    glBindTexture(GL_TEXTURE_2D, desktopTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
    glReadBuffer(GL_BACK);
    if (desktopWidth != w || desktopHeight != h) {
        glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 0, 0, w, h, 0);
        desktopWidth = w; desktopHeight = h;
    } else glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
}
void drawPanel() {
    if (!desktopTexture || !desktopWidth || !desktopHeight) return;
    const auto& eye = vr->eye(currentEye);
    glMatrixMode(GL_PROJECTION); glLoadMatrixf(eye.projection);
    glMatrixMode(GL_MODELVIEW); glLoadMatrixf(eye.viewOffset);
    glDisable(GL_DEPTH_TEST); glDisable(GL_LIGHTING); glDisable(GL_FOG);
    glDisable(GL_ALPHA_TEST); glDisable(GL_CULL_FACE); glDisable(GL_SCISSOR_TEST);
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, desktopTexture);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glColor4f(1, 1, 1, 1);
    glMatrixMode(GL_TEXTURE); glLoadIdentity(); glMatrixMode(GL_MODELVIEW);
    const float x=1.25f, y=x*float(desktopHeight)/float(desktopWidth), z=-2.2f;
    glBegin(GL_QUADS);
    glTexCoord2f(0,0); glVertex3f(-x,-y,z);
    glTexCoord2f(1,0); glVertex3f(x,-y,z);
    glTexCoord2f(1,1); glVertex3f(x,y,z);
    glTexCoord2f(0,1); glVertex3f(-x,y,z);
    glEnd();
}

void renderCameraForEye() {
    float savedPlanes[24]; auto planes=field<float*>(candidateCamera,0x1f8);
    if(planes)memcpy(savedPlanes,planes,sizeof(savedPlanes));
    float savedClipNormal[3];memcpy(savedClipNormal,reinterpret_cast<void*>(0x7bb4f8),sizeof(savedClipNormal));
    const float savedClipD=*reinterpret_cast<float*>(0x7bb4e4);
    void* scene=field<void*>(candidateCamera,0x6c);
    const int sceneRenderCount=field<int>(scene,0x50);
    const int cycle=*reinterpret_cast<int*>(0x78e3f8);
    realCameraRender(candidateCamera);
    if(planes)memcpy(planes,savedPlanes,sizeof(savedPlanes));
    memcpy(reinterpret_cast<void*>(0x7bb4f8),savedClipNormal,sizeof(savedClipNormal));
    *reinterpret_cast<float*>(0x7bb4e4)=savedClipD;
    field<int>(scene,0x50)=sceneRenderCount;
    *reinterpret_cast<int*>(0x78e3f8)=cycle;
}

void renderStereo(HDC dc) {
    if (!enabled || !wglGetCurrentContext()) return;
    GLState saved;
    if (!vr) { vr = std::make_unique<kvr::VrBackend>(); vr->setLogger(xrLog); }
    if (!initialized && LONG(GetTickCount()-nextInitAttempt) >= 0) {
        nextInitAttempt = GetTickCount()+5000;
        log("Game GL %s; GPU %s", glGetString(GL_VERSION), glGetString(GL_RENDERER));
        initialized = vr->initialize(dc, wglGetCurrentContext());
        vr->setWorldScale(worldScale);
    }
    if (!initialized) return;
    if (candidateCamera) {
        const float nearZ=field<float>(candidateCamera,0x1dc), farZ=field<float>(candidateCamera,0x1e0);
        if (nearZ>0 && farZ>nearZ) vr->setClipPlanes(nearZ,farZ);
    }
    if (!vr->beginFrame()) return;
    const bool panel = showPanel || !candidateCamera;
    if (panel) captureDesktop();
    const bool drawHud=!panel && hud && worldCaptured && hud->captureFinal(*reinterpret_cast<int*>(0x78d1d4),*reinterpret_cast<int*>(0x78d1d8));
    if(!cursor)cursor=std::make_unique<kvr::VrCursor>();
    cursor->captureFinal(dc,*reinterpret_cast<int*>(0x78d1d4),*reinterpret_cast<int*>(0x78d1d8));
    renderingEye = true;
    bool bothEyes = true;
    bool cursorDrawn=false;
    for (currentEye=0; currentEye<2; ++currentEye) {
        if (!vr->beginEye(currentEye)) { bothEyes=false; break; }
        selectedEye = &vr->eye(currentEye);
        if (candidateCamera) {
            renderCameraForEye();
        }
        if (panel) drawPanel();
        else if(drawHud)hud->draw(selectedEye->projection,selectedEye->viewOffset);
        cursorDrawn=cursor->draw(selectedEye->projection,selectedEye->viewOffset,panel)||cursorDrawn;
        vr->endEye();
    }
    selectedEye=nullptr; currentEye=-1; renderingEye=false; vr->endFrame();
    if (bothEyes && (++stereoFrames == 1 || stereoFrames % 600 == 0))
        log("STEREO frame=%lu camera=%p panel=%d cursor=%d cursor_ui=%d", stereoFrames, candidateCamera, int(panel),int(cursorDrawn),int(cursor->overInterface()));
}

void saveEyeBitmap(int eye, int w, int h) {
    const int stride=(w*3+3)&~3;
    std::vector<unsigned char> pixels(size_t(stride)*h);
    glPixelStorei(GL_PACK_ALIGNMENT,4); glReadBuffer(GL_BACK);
    glReadPixels(0,0,w,h,0x80e0,GL_UNSIGNED_BYTE,pixels.data());
    BITMAPFILEHEADER file{}; file.bfType=0x4d42; file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER); file.bfSize=file.bfOffBits+DWORD(pixels.size());
    BITMAPINFOHEADER info{}; info.biSize=sizeof(info); info.biWidth=w; info.biHeight=h; info.biPlanes=1; info.biBitCount=24; info.biSizeImage=DWORD(pixels.size());
    FILE* f=nullptr; const auto path=outputDir+(eye==2?L"\\smoke-desktop.bmp":eye?L"\\smoke-right.bmp":L"\\smoke-left.bmp");
    if(!_wfopen_s(&f,path.c_str(),L"wb")) { fwrite(&file,sizeof(file),1,f); fwrite(&info,sizeof(info),1,f); fwrite(pixels.data(),pixels.size(),1,f); fclose(f); }
}
// A bounded offline render test uses the same scene hook and two eye transforms
// without claiming headset validation. F8 repeats it in the loaded level.
void smokeRender() {
    if (!smokePending || !candidateCamera || !wglGetCurrentContext()) return;
    smokePending=false;
    GLState saved;
    captureDesktop();
    const int w=desktopWidth,h=desktopHeight;
    if(!w||!h)return;
    saveEyeBitmap(2,w,h);
    kvr::EyeData eye{}; eye.width=w;eye.height=h;
    const float n=field<float>(candidateCamera,0x1dc), f=field<float>(candidateCamera,0x1e0);
    const float a=float(w)/h, cot=1.0f/std::tan(0.5f*field<float>(candidateCamera,0x1d0)*3.14159265358979323846f/180.0f);
    eye.projection[0]=cot/a;eye.projection[5]=cot;eye.projection[10]=-(f+n)/(f-n);eye.projection[11]=-1;eye.projection[14]=-2*f*n/(f-n);
    if(haveOriginalMatrices)memcpy(eye.projection,originalProjection,sizeof(originalProjection));
    for(int i=0;i<4;++i)eye.viewOffset[i*5]=1;
    renderingEye=true; selectedEye=&eye;
    for(currentEye=0;currentEye<2;++currentEye) {
        eye.viewOffset[12]=(currentEye?-.032f:.032f)*worldScale;
        glViewport(0,0,w,h);glDisable(GL_SCISSOR_TEST);glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
        renderCameraForEye();glFinish();saveEyeBitmap(currentEye,w,h);
    }
    renderingEye=false;selectedEye=nullptr;currentEye=-1;
    // Restore the desktop image so the test does not change the monitor view.
    glViewport(0,0,w,h);glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,1,0,1,-1,1);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();glDisable(GL_DEPTH_TEST);glDisable(GL_LIGHTING);glDisable(GL_ALPHA_TEST);glDisable(GL_BLEND);glDisable(GL_FOG);
    glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,desktopTexture);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    glBegin(GL_QUADS);glTexCoord2f(0,0);glVertex2f(0,0);glTexCoord2f(1,0);glVertex2f(1,0);glTexCoord2f(1,1);glVertex2f(1,1);glTexCoord2f(0,1);glVertex2f(0,1);glEnd();
    log("SMOKE rendered two geometry views without a game tick; saved smoke-left.bmp and smoke-right.bmp");
}

BOOL WINAPI swapHook(HDC dc) {
    if (GetAsyncKeyState(VK_F8)&1) smokePending=true;
    if (GetAsyncKeyState(VK_F10)&1) { if(vr) vr->recenter(); log("Recenter requested"); }
    if (GetAsyncKeyState(VK_F9)&1) { showPanel=!showPanel; log("Menu panel=%d",int(showPanel)); }
    if (GetAsyncKeyState(VK_F11)&1) { enabled=!enabled; log("VR enabled=%d",int(enabled)); }
    smokeRender();renderStereo(dc);
    candidateCamera=nullptr; candidateArea=0;worldCaptured=false;
    if(cursor)cursor->endFrame();
    return realSwap(dc);
}

DWORD WINAPI initializeMod(void*) {
    wchar_t dllPath[MAX_PATH]; GetModuleFileNameW(moduleHandle,dllPath,MAX_PATH);
    auto dir=std::wstring(dllPath); dir.resize(dir.find_last_of(L"\\/"));
    outputDir=dir;
    logFile=_wfsopen((dir+L"\\kotor-vr.log").c_str(),L"w",_SH_DENYNO);
    log("KotOR VR " KVR_VERSION " loaded; F9 menu panel, F10 recenter, F11 VR toggle");
    wchar_t flag[8]; if(GetEnvironmentVariableW(L"KVR_XR_DISABLED",flag,8))enabled=false;
    if(GetEnvironmentVariableW(L"KVR_SMOKE_TEST",flag,8))smokePending=true;
    const auto image=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (image!=0x400000) { log("Unexpected image base: %p; hooks refused",reinterpret_cast<void*>(image)); return 1; }
    if (MH_Initialize()!=MH_OK) { log("MinHook initialization failed"); return 1; }
    HMODULE gdi=GetModuleHandleW(L"gdi32.dll"); if(!gdi) gdi=LoadLibraryW(L"gdi32.dll");
    const auto swap=reinterpret_cast<void*>(GetProcAddress(gdi,"SwapBuffers"));
    if (MH_CreateHook(reinterpret_cast<void*>(kCameraRender),cameraHook,reinterpret_cast<void**>(&realCameraRender))!=MH_OK ||
        MH_CreateHook(reinterpret_cast<void*>(kSceneRender),sceneHook,reinterpret_cast<void**>(&realSceneRender))!=MH_OK ||
        MH_CreateHook(swap,swapHook,reinterpret_cast<void**>(&realSwap))!=MH_OK || MH_EnableHook(MH_ALL_HOOKS)!=MH_OK) {
        log("Hook installation failed"); MH_DisableHook(MH_ALL_HOOKS); return 1;
    }
    log("Camera::RenderScene 0045c540, Scene::Render 004512d0, SwapBuffers hooks installed");
    return 0;
}
} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        moduleHandle=instance; DisableThreadLibraryCalls(instance);
        HANDLE thread=CreateThread(nullptr,0,initializeMod,nullptr,0,nullptr);
        if(thread) CloseHandle(thread);
    }
    return TRUE;
}
