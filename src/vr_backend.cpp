#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#include "vr_backend.h"
#ifndef KVR_MATH_TEST_ONLY
#include "kvr_version.h"
#endif
#include <unknwn.h>
#include <GL/gl.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace kvr {
namespace {
constexpr GLenum kFramebuffer = 0x8D40;
constexpr GLenum kDrawFramebuffer = 0x8CA9;
constexpr GLenum kReadFramebuffer = 0x8CA8;
constexpr GLenum kDrawFramebufferBinding = 0x8CA6;
constexpr GLenum kReadFramebufferBinding = 0x8CAA;
constexpr GLenum kRenderbuffer = 0x8D41;
constexpr GLenum kRenderbufferBinding = 0x8CA7;
constexpr GLenum kColorAttachment0 = 0x8CE0;
constexpr GLenum kDepthAttachment = 0x8D00;
constexpr GLenum kStencilAttachment = 0x8D20;
constexpr GLenum kFramebufferComplete = 0x8CD5;
constexpr GLenum kDepth24Stencil8 = 0x88F0;
constexpr int64_t kSrgb8Alpha8 = 0x8C43;
constexpr int64_t kRgba8 = 0x8058;

using GenObjects = void (APIENTRY*)(GLsizei, GLuint*);
using DeleteObjects = void (APIENTRY*)(GLsizei, const GLuint*);
using BindObject = void (APIENTRY*)(GLenum, GLuint);
using AttachTexture = void (APIENTRY*)(GLenum, GLenum, GLenum, GLuint, GLint);
using AttachRenderbuffer = void (APIENTRY*)(GLenum, GLenum, GLenum, GLuint);
using RenderbufferStorage = void (APIENTRY*)(GLenum, GLenum, GLsizei, GLsizei);
using CheckFramebuffer = GLenum (APIENTRY*)(GLenum);

template <typename T> T glProc(const char* name, const char* fallback) {
    auto p = wglGetProcAddress(name);
    if (!p || p == reinterpret_cast<PROC>(1) || p == reinterpret_cast<PROC>(2) ||
        p == reinterpret_cast<PROC>(3) || p == reinterpret_cast<PROC>(-1))
        p = wglGetProcAddress(fallback);
    return reinterpret_cast<T>(p);
}

XrQuaternionf conjugate(XrQuaternionf q) { return {-q.x, -q.y, -q.z, q.w}; }
XrQuaternionf multiply(XrQuaternionf a, XrQuaternionf b) {
    return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
            a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
            a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
            a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
XrVector3f rotate(XrQuaternionf q, XrVector3f p) {
    const auto v = multiply(multiply(q, {p.x, p.y, p.z, 0}), conjugate(q));
    return {v.x, v.y, v.z};
}
void identity(float* m) {
    std::fill(m, m + 16, 0.0f);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}
void inversePoseMatrix(const XrPosef& pose, float scale, float* m) {
    const auto q = conjugate(pose.orientation);
    const float x=q.x, y=q.y, z=q.z, w=q.w;
    identity(m);
    m[0]=1-2*(y*y+z*z); m[1]=2*(x*y+z*w); m[2]=2*(x*z-y*w);
    m[4]=2*(x*y-z*w); m[5]=1-2*(x*x+z*z); m[6]=2*(y*z+x*w);
    m[8]=2*(x*z+y*w); m[9]=2*(y*z-x*w); m[10]=1-2*(x*x+y*y);
    const auto p=rotate(q, pose.position);
    m[12]=-p.x*scale; m[13]=-p.y*scale; m[14]=-p.z*scale;
}
void projectionMatrix(XrFovf fov, float nearZ, float farZ, float* m) {
    const float l=std::tan(fov.angleLeft), r=std::tan(fov.angleRight);
    const float d=std::tan(fov.angleDown), u=std::tan(fov.angleUp);
    std::fill(m, m+16, 0.0f);
    m[0]=2/(r-l); m[5]=2/(u-d);
    m[8]=(r+l)/(r-l); m[9]=(u+d)/(u-d);
    m[10]=-(farZ+nearZ)/(farZ-nearZ); m[11]=-1;
    m[14]=-2*farZ*nearZ/(farZ-nearZ);
}
} // namespace

#ifndef KVR_MATH_TEST_ONLY
struct VrBackend::Impl {
    Logger logger = nullptr;
    std::string error;
    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSpace localSpace = XR_NULL_HANDLE, viewSpace = XR_NULL_HANDLE;
    XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;
    bool running = false, frameStarted = false, ready = false;
    bool recenterPending = true, referenceValid = false, splitFramebuffers = true;
    XrTime frameTime = 0;
    XrPosef reference{{0,0,0,1},{0,0,0}};
    float nearZ = 0.1f, farZ = 10000.0f, worldScale = 1.0f;
    HGLRC context = nullptr;
    int boundEye = -1;
    GLint oldDraw = 0, oldRead = 0, oldViewport[4]{};
    GenObjects genFramebuffers = nullptr, genRenderbuffers = nullptr;
    DeleteObjects deleteFramebuffers = nullptr, deleteRenderbuffers = nullptr;
    BindObject bindFramebuffer = nullptr, bindRenderbuffer = nullptr;
    AttachTexture framebufferTexture2D = nullptr;
    AttachRenderbuffer framebufferRenderbuffer = nullptr;
    RenderbufferStorage renderbufferStorage = nullptr;
    CheckFramebuffer checkFramebufferStatus = nullptr;
    struct Eye {
        EyeData data;
        XrSwapchain swapchain = XR_NULL_HANDLE;
        std::vector<XrSwapchainImageOpenGLKHR> images;
        std::vector<GLuint> fbos;
        GLuint depth = 0;
        uint32_t imageIndex = 0;
        bool acquired = false, rendered = false;
    };
    std::array<Eye,2> eyes;
    std::array<XrView,2> views{{{XR_TYPE_VIEW},{XR_TYPE_VIEW}}};
    std::array<XrCompositionLayerProjectionView,2> projectionViews{};

    void log(const std::string& message) { if (logger) logger(message.c_str()); }
    bool fail(const std::string& message) { error=message; log(error); return false; }
    bool ok(XrResult result, const char* operation) {
        if (XR_SUCCEEDED(result)) return true;
        char resultName[XR_MAX_RESULT_STRING_SIZE]{};
        if (instance) xrResultToString(instance,result,resultName);
        char message[512];
        std::snprintf(message,sizeof(message),"%s failed: %s (%d)",operation,resultName,int(result));
        return fail(message);
    }
    bool glFunctions() {
        genFramebuffers=glProc<GenObjects>("glGenFramebuffers","glGenFramebuffersEXT");
        genRenderbuffers=glProc<GenObjects>("glGenRenderbuffers","glGenRenderbuffersEXT");
        deleteFramebuffers=glProc<DeleteObjects>("glDeleteFramebuffers","glDeleteFramebuffersEXT");
        deleteRenderbuffers=glProc<DeleteObjects>("glDeleteRenderbuffers","glDeleteRenderbuffersEXT");
        bindFramebuffer=glProc<BindObject>("glBindFramebuffer","glBindFramebufferEXT");
        bindRenderbuffer=glProc<BindObject>("glBindRenderbuffer","glBindRenderbufferEXT");
        framebufferTexture2D=glProc<AttachTexture>("glFramebufferTexture2D","glFramebufferTexture2DEXT");
        framebufferRenderbuffer=glProc<AttachRenderbuffer>("glFramebufferRenderbuffer","glFramebufferRenderbufferEXT");
        renderbufferStorage=glProc<RenderbufferStorage>("glRenderbufferStorage","glRenderbufferStorageEXT");
        checkFramebufferStatus=glProc<CheckFramebuffer>("glCheckFramebufferStatus","glCheckFramebufferStatusEXT");
        return genFramebuffers&&genRenderbuffers&&deleteFramebuffers&&deleteRenderbuffers&&
            bindFramebuffer&&bindRenderbuffer&&framebufferTexture2D&&framebufferRenderbuffer&&
            renderbufferStorage&&checkFramebufferStatus;
    }
    void saveFramebuffer() {
        glGetIntegerv(kDrawFramebufferBinding,&oldDraw);
        if (splitFramebuffers) glGetIntegerv(kReadFramebufferBinding,&oldRead);
        else oldRead=oldDraw;
        glGetIntegerv(GL_VIEWPORT,oldViewport);
    }
    void restoreFramebuffer() {
        if (splitFramebuffers) {
            bindFramebuffer(kDrawFramebuffer,GLuint(oldDraw));
            bindFramebuffer(kReadFramebuffer,GLuint(oldRead));
        } else bindFramebuffer(kFramebuffer,GLuint(oldDraw));
        glViewport(oldViewport[0],oldViewport[1],oldViewport[2],oldViewport[3]);
    }
    void pollEvents() {
        XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
        XrResult result;
        while ((result=xrPollEvent(instance,&event))==XR_SUCCESS) {
            if (event.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                const auto& changed=*reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
                if (changed.session==session) {
                    sessionState=changed.state;
                    char line[96]; std::snprintf(line,sizeof(line),"OpenXR session state: %d",int(sessionState)); log(line);
                    if (sessionState==XR_SESSION_STATE_READY && !running) {
                        XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
                        begin.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                        if (ok(xrBeginSession(session,&begin),"xrBeginSession")) running=true;
                    } else if (sessionState==XR_SESSION_STATE_STOPPING) {
                        if (running) ok(xrEndSession(session),"xrEndSession");
                        running=false;
                    } else if (sessionState==XR_SESSION_STATE_EXITING || sessionState==XR_SESSION_STATE_LOSS_PENDING) {
                        running=false; ready=false;
                        fail("OpenXR session exited or was lost; restart the game to reconnect.");
                    }
                }
            } else if (event.type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
                running=false; ready=false; fail("OpenXR instance loss pending; restart the game to reconnect.");
            } else if (event.type==XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
                recenterPending=true;
            }
            event={XR_TYPE_EVENT_DATA_BUFFER};
        }
        if (result!=XR_EVENT_UNAVAILABLE) ok(result,"xrPollEvent");
    }
};

VrBackend::VrBackend():impl_(new Impl) {}
VrBackend::~VrBackend() { shutdown(); }
void VrBackend::setLogger(Logger logger) { impl_->logger=logger; }
const char* VrBackend::lastError() const { return impl_->error.c_str(); }
bool VrBackend::active() const { return impl_->ready && impl_->running; }
const EyeData& VrBackend::eye(int index) const { return impl_->eyes[index==1?1:0].data; }
void VrBackend::recenter() { impl_->recenterPending=true; }
void VrBackend::setClipPlanes(float nearZ,float farZ) {
    if (nearZ>0 && farZ>nearZ) { impl_->nearZ=nearZ; impl_->farZ=farZ; }
}
void VrBackend::setWorldScale(float scale) { if (scale>0 && std::isfinite(scale)) impl_->worldScale=scale; }

bool VrBackend::initialize(HDC dc,HGLRC context) {
    auto& p=*impl_;
    if (p.ready) return true;
    shutdown();
    p.error.clear();
    if (!dc || !context || wglGetCurrentContext()!=context) return p.fail("OpenXR initialization needs the game's current OpenGL context.");
    p.context=context;
    auto abort=[&]() { shutdown(); return false; };
    uint32_t count=0;
    if (!p.ok(xrEnumerateInstanceExtensionProperties(nullptr,0,&count,nullptr),"xrEnumerateInstanceExtensionProperties")) return abort();
    std::vector<XrExtensionProperties> extensions(count,{XR_TYPE_EXTENSION_PROPERTIES});
    if (!p.ok(xrEnumerateInstanceExtensionProperties(nullptr,count,&count,extensions.data()),"xrEnumerateInstanceExtensionProperties")) return abort();
    if (std::none_of(extensions.begin(),extensions.end(),[](const XrExtensionProperties& e) { return std::strcmp(e.extensionName,XR_KHR_OPENGL_ENABLE_EXTENSION_NAME)==0; }))
        { p.fail("The selected OpenXR runtime does not expose XR_KHR_opengl_enable for this 32-bit game."); return abort(); }
    const char* enabledExtensions[]={XR_KHR_OPENGL_ENABLE_EXTENSION_NAME};
    XrInstanceCreateInfo create{XR_TYPE_INSTANCE_CREATE_INFO};
    std::snprintf(create.applicationInfo.applicationName,sizeof(create.applicationInfo.applicationName),"KotOR VR Prototype");
    std::snprintf(create.applicationInfo.engineName,sizeof(create.applicationInfo.engineName),"KotOR Odyssey Hook");
    create.applicationInfo.applicationVersion=KVR_VERSION_NUMBER;
    create.applicationInfo.engineVersion=1;
    create.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);
    create.enabledExtensionCount=1; create.enabledExtensionNames=enabledExtensions;
    if (!p.ok(xrCreateInstance(&create,&p.instance),"xrCreateInstance")) return abort();
    XrInstanceProperties props{XR_TYPE_INSTANCE_PROPERTIES};
    if (XR_SUCCEEDED(xrGetInstanceProperties(p.instance,&props))) p.log(std::string("OpenXR runtime: ")+props.runtimeName);
    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO}; systemInfo.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (!p.ok(xrGetSystem(p.instance,&systemInfo,&p.system),"xrGetSystem (connect headset through Virtual Desktop)")) return abort();
    PFN_xrGetOpenGLGraphicsRequirementsKHR graphicsRequirements=nullptr;
    if (!p.ok(xrGetInstanceProcAddr(p.instance,"xrGetOpenGLGraphicsRequirementsKHR",reinterpret_cast<PFN_xrVoidFunction*>(&graphicsRequirements)),"xrGetInstanceProcAddr")) return abort();
    XrGraphicsRequirementsOpenGLKHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR};
    if (!p.ok(graphicsRequirements(p.instance,p.system,&requirements),"xrGetOpenGLGraphicsRequirementsKHR")) return abort();
    const auto version=reinterpret_cast<const char*>(glGetString(GL_VERSION));
    int major=0,minor=0;
    if (!version || sscanf_s(version,"%d.%d",&major,&minor)!=2) { p.fail("Cannot read the game's OpenGL context version."); return abort(); }
    const XrVersion glVersion=XR_MAKE_VERSION(major,minor,0);
    p.splitFramebuffers=major>=3;
    if (glVersion<requirements.minApiVersionSupported || glVersion>requirements.maxApiVersionSupported) {
        char line[256]; std::snprintf(line,sizeof(line),"Game OpenGL %s is outside runtime range %u.%u to %u.%u.",version,
            unsigned(XR_VERSION_MAJOR(requirements.minApiVersionSupported)),unsigned(XR_VERSION_MINOR(requirements.minApiVersionSupported)),
            unsigned(XR_VERSION_MAJOR(requirements.maxApiVersionSupported)),unsigned(XR_VERSION_MINOR(requirements.maxApiVersionSupported)));
        p.fail(line); return abort();
    }
    if (!p.glFunctions()) { p.fail("The OpenGL context is missing framebuffer object functions."); return abort(); }
    XrGraphicsBindingOpenGLWin32KHR binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR}; binding.hDC=dc; binding.hGLRC=context;
    XrSessionCreateInfo sessionCreate{XR_TYPE_SESSION_CREATE_INFO}; sessionCreate.next=&binding; sessionCreate.systemId=p.system;
    if (!p.ok(xrCreateSession(p.instance,&sessionCreate,&p.session),"xrCreateSession (OpenGL Win32)")) return abort();
    XrReferenceSpaceCreateInfo spaceCreate{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    spaceCreate.poseInReferenceSpace.orientation.w=1; spaceCreate.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;
    if (!p.ok(xrCreateReferenceSpace(p.session,&spaceCreate,&p.localSpace),"xrCreateReferenceSpace LOCAL")) return abort();
    spaceCreate.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;
    if (!p.ok(xrCreateReferenceSpace(p.session,&spaceCreate,&p.viewSpace),"xrCreateReferenceSpace VIEW")) return abort();
    if (!p.ok(xrEnumerateViewConfigurationViews(p.instance,p.system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,0,&count,nullptr),"xrEnumerateViewConfigurationViews")) return abort();
    if (count!=2) { p.fail("OpenXR runtime does not provide exactly two stereo views."); return abort(); }
    std::vector<XrViewConfigurationView> configs(count,{XR_TYPE_VIEW_CONFIGURATION_VIEW});
    if (!p.ok(xrEnumerateViewConfigurationViews(p.instance,p.system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,count,&count,configs.data()),"xrEnumerateViewConfigurationViews")) return abort();
    if (!p.ok(xrEnumerateSwapchainFormats(p.session,0,&count,nullptr),"xrEnumerateSwapchainFormats")) return abort();
    std::vector<int64_t> formats(count);
    if (!p.ok(xrEnumerateSwapchainFormats(p.session,count,&count,formats.data()),"xrEnumerateSwapchainFormats")) return abort();
    int64_t format=0;
    // Prefer RGBA8 for the legacy game's gamma-encoded fixed-function output.
    for (int64_t candidate : {kRgba8,kSrgb8Alpha8})
        if (std::find(formats.begin(),formats.end(),candidate)!=formats.end()) { format=candidate; break; }
    if (!format) { p.fail("OpenXR runtime offers no RGBA8 or SRGB8_ALPHA8 OpenGL swapchain format."); return abort(); }
    p.saveFramebuffer();
    GLint oldRenderbuffer=0; glGetIntegerv(kRenderbufferBinding,&oldRenderbuffer);
    bool created=true;
    for (int i=0;i<2 && created;++i) {
        auto& e=p.eyes[i]; e.data.width=int(configs[i].recommendedImageRectWidth); e.data.height=int(configs[i].recommendedImageRectHeight);
        XrSwapchainCreateInfo swapCreate{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        swapCreate.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT; swapCreate.format=format;
        swapCreate.sampleCount=1; swapCreate.width=e.data.width; swapCreate.height=e.data.height;
        swapCreate.faceCount=1; swapCreate.arraySize=1; swapCreate.mipCount=1;
        if (!p.ok(xrCreateSwapchain(p.session,&swapCreate,&e.swapchain),"xrCreateSwapchain")) { created=false; break; }
        if (!p.ok(xrEnumerateSwapchainImages(e.swapchain,0,&count,nullptr),"xrEnumerateSwapchainImages")) { created=false; break; }
        e.images.assign(count,{XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
        if (!p.ok(xrEnumerateSwapchainImages(e.swapchain,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(e.images.data())),"xrEnumerateSwapchainImages")) { created=false; break; }
        p.genRenderbuffers(1,&e.depth); p.bindRenderbuffer(kRenderbuffer,e.depth);
        p.renderbufferStorage(kRenderbuffer,kDepth24Stencil8,e.data.width,e.data.height);
        e.fbos.resize(count); p.genFramebuffers(GLsizei(count),e.fbos.data());
        for (uint32_t j=0;j<count;++j) {
            p.bindFramebuffer(kFramebuffer,e.fbos[j]);
            p.framebufferTexture2D(kFramebuffer,kColorAttachment0,GL_TEXTURE_2D,e.images[j].image,0);
            p.framebufferRenderbuffer(kFramebuffer,kDepthAttachment,kRenderbuffer,e.depth);
            p.framebufferRenderbuffer(kFramebuffer,kStencilAttachment,kRenderbuffer,e.depth);
            if (p.checkFramebufferStatus(kFramebuffer)!=kFramebufferComplete) { p.fail("An OpenXR eye framebuffer is incomplete."); created=false; break; }
        }
    }
    p.bindRenderbuffer(kRenderbuffer,GLuint(oldRenderbuffer)); p.restoreFramebuffer();
    if (!created) return abort();
    p.referenceValid=false; p.recenterPending=true; p.ready=true;
    p.log("OpenXR initialized: separate eye swapchains, head tracking, depth/stencil buffers.");
    p.pollEvents();
    return true;
}

bool VrBackend::beginFrame() {
    auto& p=*impl_;
    if (!p.ready || p.frameStarted || wglGetCurrentContext()!=p.context) return false;
    p.pollEvents();
    if (!p.ready || !p.running) return false;
    XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO}; XrFrameState state{XR_TYPE_FRAME_STATE};
    if (!p.ok(xrWaitFrame(p.session,&wait,&state),"xrWaitFrame")) return false;
    XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
    if (!p.ok(xrBeginFrame(p.session,&begin),"xrBeginFrame")) return false;
    p.frameStarted=true; p.frameTime=state.predictedDisplayTime;
    for (auto& e:p.eyes) e.rendered=false;
    if (!state.shouldRender) { endFrame(); return false; }
    XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
    locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locate.displayTime=p.frameTime; locate.space=p.localSpace;
    XrViewState viewState{XR_TYPE_VIEW_STATE}; uint32_t viewCount=0;
    if (!p.ok(xrLocateViews(p.session,&locate,&viewState,2,&viewCount,p.views.data()),"xrLocateViews") || viewCount!=2 ||
        (viewState.viewStateFlags&(XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT))!=
        (XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT)) { endFrame(); return false; }
    if (p.recenterPending || !p.referenceValid) {
        XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
        if (!p.ok(xrLocateSpace(p.viewSpace,p.localSpace,p.frameTime,&head),"xrLocateSpace HEAD") ||
            (head.locationFlags&(XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT))!=
            (XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT)) { endFrame(); return false; }
        p.reference.position=head.pose.position;
        const auto forward=rotate(head.pose.orientation,{0,0,-1});
        const float yaw=std::atan2(-forward.x,-forward.z);
        p.reference.orientation={0,std::sin(yaw*0.5f),0,std::cos(yaw*0.5f)};
        p.referenceValid=true; p.recenterPending=false; p.log("OpenXR head origin recentered (level yaw and position).");
    }
    for (int i=0;i<2;++i) {
        auto& e=p.eyes[i];
        const auto invRef=conjugate(p.reference.orientation);
        const auto pos=p.views[i].pose.position;
        XrPosef relative;
        relative.orientation=multiply(invRef,p.views[i].pose.orientation);
        relative.position=rotate(invRef,{pos.x-p.reference.position.x,pos.y-p.reference.position.y,pos.z-p.reference.position.z});
        inversePoseMatrix(relative,p.worldScale,e.data.viewOffset);
        projectionMatrix(p.views[i].fov,p.nearZ,p.farZ,e.data.projection);
    }
    return true;
}

bool VrBackend::beginEye(int index) {
    auto& p=*impl_;
    if (!p.frameStarted || index<0 || index>1 || p.boundEye>=0) return false;
    auto& e=p.eyes[index]; if (e.rendered || e.acquired) return false;
    XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (!p.ok(xrAcquireSwapchainImage(e.swapchain,&acquire,&e.imageIndex),"xrAcquireSwapchainImage")) return false;
    e.acquired=true;
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO}; wait.timeout=XR_INFINITE_DURATION;
    if (!p.ok(xrWaitSwapchainImage(e.swapchain,&wait),"xrWaitSwapchainImage")) return false;
    p.saveFramebuffer();
    p.bindFramebuffer(kFramebuffer,e.fbos[e.imageIndex]); glViewport(0,0,e.data.width,e.data.height);
    // Clear independently of the game's masks and scissor, then restore them.
    glPushAttrib(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT|GL_SCISSOR_BIT);
    glDisable(GL_SCISSOR_TEST); glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE); glDepthMask(GL_TRUE);
    glStencilMask(~0u); glClearColor(0,0,0,1); glClearDepth(1); glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT); glPopAttrib();
    p.boundEye=index; return true;
}

void VrBackend::endEye() {
    auto& p=*impl_; if (p.boundEye<0) return;
    auto& e=p.eyes[p.boundEye];
    glFlush();
    p.restoreFramebuffer();
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    if (p.ok(xrReleaseSwapchainImage(e.swapchain,&release),"xrReleaseSwapchainImage")) { e.acquired=false; e.rendered=true; }
    p.boundEye=-1;
}

void VrBackend::endFrame() {
    auto& p=*impl_; if (!p.frameStarted) return;
    endEye();
    for (auto& e:p.eyes) {
        if (e.acquired) {
            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            p.ok(xrReleaseSwapchainImage(e.swapchain,&release),"xrReleaseSwapchainImage cleanup"); e.acquired=false;
        }
    }
    XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION}; projection.space=p.localSpace;
    const XrCompositionLayerBaseHeader* layer=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);
    if (p.eyes[0].rendered && p.eyes[1].rendered) {
        for (int i=0;i<2;++i) {
            auto& v=p.projectionViews[i]; v={XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
            v.pose=p.views[i].pose; v.fov=p.views[i].fov;
            v.subImage.swapchain=p.eyes[i].swapchain; v.subImage.imageRect.extent={p.eyes[i].data.width,p.eyes[i].data.height};
        }
        projection.viewCount=2; projection.views=p.projectionViews.data();
    }
    XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO}; end.displayTime=p.frameTime; end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    end.layerCount=projection.viewCount==2?1:0; end.layers=end.layerCount?&layer:nullptr;
    p.ok(xrEndFrame(p.session,&end),"xrEndFrame"); p.frameStarted=false;
}

void VrBackend::shutdown() {
    auto& p=*impl_;
    if (p.frameStarted) endFrame();
    const bool canDelete=p.context && wglGetCurrentContext()==p.context;
    for (auto& e:p.eyes) {
        if (canDelete && p.deleteFramebuffers && !e.fbos.empty()) p.deleteFramebuffers(GLsizei(e.fbos.size()),e.fbos.data());
        if (canDelete && p.deleteRenderbuffers && e.depth) p.deleteRenderbuffers(1,&e.depth);
        e.fbos.clear(); e.images.clear(); e.depth=0; e.acquired=false; e.rendered=false;
        if (e.swapchain) xrDestroySwapchain(e.swapchain);
        e.swapchain=XR_NULL_HANDLE;
    }
    if (p.viewSpace) xrDestroySpace(p.viewSpace);
    if (p.localSpace) xrDestroySpace(p.localSpace);
    if (p.session) xrDestroySession(p.session);
    if (p.instance) xrDestroyInstance(p.instance);
    p.viewSpace=p.localSpace=XR_NULL_HANDLE; p.session=XR_NULL_HANDLE; p.instance=XR_NULL_HANDLE; p.system=XR_NULL_SYSTEM_ID;
    p.ready=p.running=p.frameStarted=p.referenceValid=false; p.recenterPending=true; p.context=nullptr; p.boundEye=-1;
    p.sessionState=XR_SESSION_STATE_UNKNOWN;
}

#endif // KVR_MATH_TEST_ONLY
} // namespace kvr
