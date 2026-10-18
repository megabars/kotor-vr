// Direct headset diagnostic for the same x86 OpenGL/OpenXR backend as the mod.
// Run deliberately: kotor-vr-probe.exe --seconds=30
#include "vr_backend.h"
#include <GL/gl.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
bool quit=false,recenter=false;
LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    if (message==WM_CLOSE) { quit=true; return 0; }
    if (message==WM_KEYDOWN && wparam==VK_ESCAPE) { quit=true; return 0; }
    if (message==WM_KEYDOWN && wparam=='R') { recenter=true; return 0; }
    return DefWindowProcW(window,message,wparam,lparam);
}
void logLine(const char* message) { std::printf("%s\n",message); std::fflush(stdout); }
void scene() {
    glDisable(GL_LIGHTING); glDisable(GL_TEXTURE_2D); glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE); glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDepthFunc(GL_LEQUAL);
    glBegin(GL_QUADS);
    glColor3f(1,0.15f,0.1f);
    glVertex3f(-0.5f,-0.5f,0.5f); glVertex3f(0.5f,-0.5f,0.5f); glVertex3f(0.5f,0.5f,0.5f); glVertex3f(-0.5f,0.5f,0.5f);
    glColor3f(0.1f,0.2f,1);
    glVertex3f(-0.5f,-0.5f,-0.5f); glVertex3f(-0.5f,0.5f,-0.5f); glVertex3f(0.5f,0.5f,-0.5f); glVertex3f(0.5f,-0.5f,-0.5f);
    glColor3f(0.1f,1,0.2f);
    glVertex3f(-0.5f,0.5f,-0.5f); glVertex3f(-0.5f,0.5f,0.5f); glVertex3f(0.5f,0.5f,0.5f); glVertex3f(0.5f,0.5f,-0.5f);
    glColor3f(1,1,0.1f);
    glVertex3f(-0.5f,-0.5f,-0.5f); glVertex3f(0.5f,-0.5f,-0.5f); glVertex3f(0.5f,-0.5f,0.5f); glVertex3f(-0.5f,-0.5f,0.5f);
    glColor3f(1,0.1f,1);
    glVertex3f(-0.5f,-0.5f,-0.5f); glVertex3f(-0.5f,-0.5f,0.5f); glVertex3f(-0.5f,0.5f,0.5f); glVertex3f(-0.5f,0.5f,-0.5f);
    glColor3f(0.1f,1,1);
    glVertex3f(0.5f,-0.5f,-0.5f); glVertex3f(0.5f,0.5f,-0.5f); glVertex3f(0.5f,0.5f,0.5f); glVertex3f(0.5f,-0.5f,0.5f);
    glEnd();
    glLineWidth(1); glColor3f(0.35f,0.4f,0.4f); glBegin(GL_LINES);
    for (int i=-10;i<=10;++i) {
        glVertex3f(float(i),-1,-10); glVertex3f(float(i),-1,10);
        glVertex3f(-10,-1,float(i)); glVertex3f(10,-1,float(i));
    }
    glEnd();
}
void desktopFrame(HDC dc) {
    glViewport(0,0,960,600); glClearColor(0.025f,0.035f,0.055f,1);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glFrustum(-0.16,0.16,-0.1,0.1,0.1,100);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity(); glTranslatef(0,0,-3);
    scene(); SwapBuffers(dc);
}
}

int main(int argc,char** argv) {
    double seconds=5;
    for (int i=1;i<argc;++i) {
        if (std::strncmp(argv[i],"--seconds=",10)==0) {
            char* end=nullptr; const double value=std::strtod(argv[i]+10,&end);
            if (!end || *end || value<1 || value>3600) { logLine("--seconds must be in range 1..3600"); return 1; }
            seconds=value;
        } else { logLine("Usage: kotor-vr-probe.exe [--seconds=5..3600]"); return 1; }
    }
    constexpr wchar_t runtimePath[]=L"C:\\Program Files\\Virtual Desktop Streamer\\OpenXR\\virtualdesktop-openxr-32.json";
    if (GetEnvironmentVariableW(L"XR_RUNTIME_JSON",nullptr,0)==0 && GetFileAttributesW(runtimePath)!=INVALID_FILE_ATTRIBUTES)
        SetEnvironmentVariableW(L"XR_RUNTIME_JSON",runtimePath);
    wchar_t runtime[1024]{}; GetEnvironmentVariableW(L"XR_RUNTIME_JSON",runtime,1024);
    std::printf("KotOR VR x86 headset probe; runtime override: %ls\n",runtime[0]?runtime:L"(system default)");
    logLine("R: recenter head. Escape: exit. Connect headset to Virtual Desktop before starting.");
    const auto module=GetModuleHandleW(nullptr);
    WNDCLASSW windowClass{}; windowClass.style=CS_OWNDC; windowClass.lpfnWndProc=windowProc;
    windowClass.hInstance=module; windowClass.lpszClassName=L"KotORVrProbe";
    if (!RegisterClassW(&windowClass)) { logLine("RegisterClass failed"); return 1; }
    HWND window=CreateWindowW(windowClass.lpszClassName,L"KotOR VR headset probe",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
        CW_USEDEFAULT,CW_USEDEFAULT,976,639,nullptr,nullptr,module,nullptr);
    if (!window) { logLine("CreateWindow failed"); return 1; }
    HDC dc=GetDC(window);
    PIXELFORMATDESCRIPTOR pixel{}; pixel.nSize=sizeof(pixel); pixel.nVersion=1;
    pixel.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;
    pixel.iPixelType=PFD_TYPE_RGBA; pixel.cColorBits=32; pixel.cDepthBits=24; pixel.cStencilBits=8; pixel.iLayerType=PFD_MAIN_PLANE;
    const int format=ChoosePixelFormat(dc,&pixel);
    if (!format || !SetPixelFormat(dc,format,&pixel)) {
        logLine("ChoosePixelFormat/SetPixelFormat failed"); ReleaseDC(window,dc); DestroyWindow(window); return 1;
    }
    const HGLRC context=wglCreateContext(dc);
    if (!context || !wglMakeCurrent(dc,context)) {
        logLine("wglCreateContext/wglMakeCurrent failed");
        if (context) wglDeleteContext(context); ReleaseDC(window,dc); DestroyWindow(window); return 1;
    }
    std::printf("GL_VERSION=%s\nGL_RENDERER=%s\n",glGetString(GL_VERSION),glGetString(GL_RENDERER)); std::fflush(stdout);
    desktopFrame(dc);
    kvr::VrBackend backend; backend.setLogger(logLine); backend.setClipPlanes(0.1f,100);
    const bool initialized=backend.initialize(dc,context);
    unsigned stereoFrames=0;
    if (initialized) {
        const auto started=std::chrono::steady_clock::now();
        while (!quit && std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()<seconds) {
            MSG message{};
            while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
                if (message.message==WM_QUIT) quit=true;
                TranslateMessage(&message); DispatchMessageW(&message);
            }
            if (recenter) { backend.recenter(); recenter=false; }
            if (backend.beginFrame()) {
                bool rendered=true;
                for (int i=0;i<2;++i) {
                    if (!backend.beginEye(i)) { rendered=false; break; }
                    const auto& eye=backend.eye(i);
                    glMatrixMode(GL_PROJECTION); glLoadMatrixf(eye.projection);
                    glMatrixMode(GL_MODELVIEW); glLoadMatrixf(eye.viewOffset); glTranslatef(0,0,-3);
                    scene(); backend.endEye();
                }
                backend.endFrame();
                if (rendered) {
                    ++stereoFrames;
                    if (stereoFrames==1) logLine("First stereo headset frame submitted. Inspect cube depth and turn/move your head.");
                }
            } else Sleep(10);
            desktopFrame(dc);
        }
    }
    backend.shutdown();
    wglMakeCurrent(nullptr,nullptr); wglDeleteContext(context); ReleaseDC(window,dc); DestroyWindow(window);
    std::printf("Probe finished: initialized=%d stereo_frames=%u\n",int(initialized),stereoFrames);
    return !initialized?2:(stereoFrames?0:3);
}
