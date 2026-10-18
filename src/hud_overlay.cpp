#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <GL/gl.h>

#include "hud_overlay.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace kvr {
namespace {
constexpr GLenum Texture0 = 0x84C0;
constexpr GLenum Texture1 = 0x84C1;
constexpr GLenum ActiveTexture = 0x84E0;
constexpr GLenum CurrentProgram = 0x8B8D;
constexpr GLenum VertexShader = 0x8B31;
constexpr GLenum FragmentShader = 0x8B30;
constexpr GLenum CompileStatus = 0x8B81;
constexpr GLenum LinkStatus = 0x8B82;
constexpr GLenum FuncAdd = 0x8006;

template<class T> T Load(const char* name) {
    PROC address = wglGetProcAddress(name);
    const auto numeric = reinterpret_cast<std::uintptr_t>(address);
    if (!address || numeric <= 3 || numeric == static_cast<std::uintptr_t>(-1)) return nullptr;
    return reinterpret_cast<T>(address);
}

struct Functions {
    void (APIENTRY* activeTexture)(GLenum) = nullptr;
    void (APIENTRY* blendEquation)(GLenum) = nullptr;
    GLuint (APIENTRY* createShader)(GLenum) = nullptr;
    void (APIENTRY* shaderSource)(GLuint, GLsizei, const char* const*, const GLint*) = nullptr;
    void (APIENTRY* compileShader)(GLuint) = nullptr;
    void (APIENTRY* getShaderiv)(GLuint, GLenum, GLint*) = nullptr;
    void (APIENTRY* getShaderInfoLog)(GLuint, GLsizei, GLsizei*, char*) = nullptr;
    void (APIENTRY* deleteShader)(GLuint) = nullptr;
    GLuint (APIENTRY* createProgram)() = nullptr;
    void (APIENTRY* attachShader)(GLuint, GLuint) = nullptr;
    void (APIENTRY* linkProgram)(GLuint) = nullptr;
    void (APIENTRY* getProgramiv)(GLuint, GLenum, GLint*) = nullptr;
    void (APIENTRY* getProgramInfoLog)(GLuint, GLsizei, GLsizei*, char*) = nullptr;
    void (APIENTRY* deleteProgram)(GLuint) = nullptr;
    void (APIENTRY* useProgram)(GLuint) = nullptr;
    GLint (APIENTRY* getUniformLocation)(GLuint, const char*) = nullptr;
    void (APIENTRY* uniform1i)(GLint, GLint) = nullptr;
    bool load() {
#define KVR_LOAD(member, name) member = Load<decltype(member)>(name); if (!member) return false
        KVR_LOAD(activeTexture, "glActiveTexture");
        KVR_LOAD(blendEquation, "glBlendEquation");
        KVR_LOAD(createShader, "glCreateShader");
        KVR_LOAD(shaderSource, "glShaderSource");
        KVR_LOAD(compileShader, "glCompileShader");
        KVR_LOAD(getShaderiv, "glGetShaderiv");
        KVR_LOAD(getShaderInfoLog, "glGetShaderInfoLog");
        KVR_LOAD(deleteShader, "glDeleteShader");
        KVR_LOAD(createProgram, "glCreateProgram");
        KVR_LOAD(attachShader, "glAttachShader");
        KVR_LOAD(linkProgram, "glLinkProgram");
        KVR_LOAD(getProgramiv, "glGetProgramiv");
        KVR_LOAD(getProgramInfoLog, "glGetProgramInfoLog");
        KVR_LOAD(deleteProgram, "glDeleteProgram");
        KVR_LOAD(useProgram, "glUseProgram");
        KVR_LOAD(getUniformLocation, "glGetUniformLocation");
        KVR_LOAD(uniform1i, "glUniform1i");
#undef KVR_LOAD
        return true;
    }
};

// GL_ALL_ATTRIB_BITS includes blend modes, texture environments, masks, read
// buffer, clip/scissor/stencil state and current immediate-mode attributes.
// Program bindings and matrix stacks are outside the attribute stack.
struct SavedState {
    Functions& gl;
    GLint program = 0, active = Texture0, matrixMode = GL_MODELVIEW;
    GLint bindings[2]{};
    GLboolean enabled[2]{};
    explicit SavedState(Functions& functions) : gl(functions) {
        glGetIntegerv(CurrentProgram, &program);
        glGetIntegerv(ActiveTexture, &active);
        glGetIntegerv(GL_MATRIX_MODE, &matrixMode);
        glPushAttrib(GL_ALL_ATTRIB_BITS);
        glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
        for (int unit = 0; unit < 2; ++unit) {
            gl.activeTexture(Texture0 + unit);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &bindings[unit]);
            enabled[unit] = glIsEnabled(GL_TEXTURE_2D);
        }
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
    }
    ~SavedState() {
        glMatrixMode(GL_MODELVIEW);
        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(matrixMode);
        gl.useProgram(static_cast<GLuint>(program));
        glPopClientAttrib();
        glPopAttrib();
        // Explicitly restore per-unit bindings/enables and selection too; this
        // avoids relying on old compatibility drivers' multitexture stacks.
        for (int unit = 0; unit < 2; ++unit) {
            gl.activeTexture(Texture0 + unit);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(bindings[unit]));
            if (enabled[unit]) glEnable(GL_TEXTURE_2D); else glDisable(GL_TEXTURE_2D);
        }
        gl.activeTexture(active);
    }
};
} // namespace

struct HudOverlay::Impl {
    Functions gl;
    HGLRC context = nullptr;
    GLuint program = 0;
    GLuint textures[2]{};
    GLint worldSampler = -1, finalSampler = -1;
    int width = 0, height = 0;
    bool worldReady = false, finalReady = false;
    std::string error;

    GLuint shader(GLenum type, const char* source) {
        const GLuint result = gl.createShader(type);
        if (!result) { error = "Cannot create HUD shader."; return 0; }
        gl.shaderSource(result, 1, &source, nullptr);
        gl.compileShader(result);
        GLint compiled = GL_FALSE;
        gl.getShaderiv(result, CompileStatus, &compiled);
        if (!compiled) {
            char log[2048]{};
            gl.getShaderInfoLog(result, sizeof(log) - 1, nullptr, log);
            error = std::string("HUD shader compilation failed: ") + log;
            gl.deleteShader(result);
            return 0;
        }
        return result;
    }

    bool initialize() {
        if (!wglGetCurrentContext()) { error = "HUD requires a current OpenGL context."; return false; }
        if (context && context != wglGetCurrentContext()) {
            error = "HUD called with a different OpenGL context.";
            return false;
        }
        if (program) return true;
        if (!gl.load()) { error = "HUD requires OpenGL 2.1 shader/multitexture functions."; return false; }
        context = wglGetCurrentContext();
        static const char* vertex =
            "#version 120\n"
            "varying vec2 uv;\n"
            "void main(){ uv=gl_MultiTexCoord0.xy; gl_Position=gl_ModelViewProjectionMatrix*gl_Vertex; }\n";
        static const char* fragment =
            "#version 120\n"
            "uniform sampler2D worldImage; uniform sampler2D finalImage; varying vec2 uv;\n"
            "void main(){ vec3 w=texture2D(worldImage,uv).rgb; vec3 f=texture2D(finalImage,uv).rgb;\n"
            " vec3 d=abs(f-w); if(max(max(d.r,d.g),d.b)<=2.0/255.0) discard;\n"
            " gl_FragColor=vec4(f,1.0); }\n";
        const GLuint vs = shader(VertexShader, vertex);
        if (!vs) return false;
        const GLuint ps = shader(FragmentShader, fragment);
        if (!ps) { gl.deleteShader(vs); return false; }
        program = gl.createProgram();
        if (program) {
            gl.attachShader(program, vs);
            gl.attachShader(program, ps);
            gl.linkProgram(program);
        }
        gl.deleteShader(vs);
        gl.deleteShader(ps);
        GLint linked = GL_FALSE;
        if (program) gl.getProgramiv(program, LinkStatus, &linked);
        if (!linked) {
            char log[2048]{};
            if (program) {
                gl.getProgramInfoLog(program, sizeof(log) - 1, nullptr, log);
                gl.deleteProgram(program);
                program = 0;
            }
            error = std::string("HUD program linking failed: ") + log;
            return false;
        }
        worldSampler = gl.getUniformLocation(program, "worldImage");
        finalSampler = gl.getUniformLocation(program, "finalImage");
        if (worldSampler < 0 || finalSampler < 0) {
            error = "HUD sampler uniforms are unavailable.";
            gl.deleteProgram(program);
            program = 0;
            return false;
        }
        error.clear();
        return true;
    }

    bool capture(int image, int requestedWidth, int requestedHeight) {
        if (!initialize()) return false;
        GLint limit = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
        if (requestedWidth <= 0 || requestedHeight <= 0 || requestedWidth > limit || requestedHeight > limit) {
            error = "HUD capture dimensions exceed the supported texture size.";
            return false;
        }
        SavedState saved(gl);
        gl.activeTexture(Texture0);
        if (width != requestedWidth || height != requestedHeight || !textures[0] || !textures[1]) {
            if (textures[0] || textures[1]) glDeleteTextures(2, textures);
            textures[0] = textures[1] = 0;
            glGenTextures(2, textures);
            if (!textures[0] || !textures[1]) {
                error = "Cannot allocate HUD capture textures.";
                return false;
            }
            for (GLuint texture : textures) {
                glBindTexture(GL_TEXTURE_2D, texture);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F /* GL_CLAMP_TO_EDGE */);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, requestedWidth, requestedHeight,
                             0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            }
            width = requestedWidth;
            height = requestedHeight;
            worldReady = finalReady = false;
        }
        glBindTexture(GL_TEXTURE_2D, textures[image]);
        GLboolean doubleBuffered = GL_FALSE;
        glGetBooleanv(GL_DOUBLEBUFFER, &doubleBuffered);
        glReadBuffer(doubleBuffered ? GL_BACK : GL_FRONT);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
        const GLenum result = glGetError();
        if (result != GL_NO_ERROR) {
            error = "OpenGL error during HUD backbuffer capture: " + std::to_string(result);
            return false;
        }
        error.clear();
        return true;
    }
};

HudOverlay::HudOverlay() : impl_(std::make_unique<Impl>()) {}
HudOverlay::~HudOverlay() { shutdown(); }
bool HudOverlay::captureWorld(int width, int height) {
    impl_->worldReady = impl_->finalReady = false;
    impl_->worldReady = impl_->capture(0, width, height);
    return impl_->worldReady;
}
bool HudOverlay::captureFinal(int width, int height) {
    impl_->finalReady = false;
    if (!impl_->worldReady || width != impl_->width || height != impl_->height) {
        impl_->error = "HUD final capture has no matching world capture.";
        return false;
    }
    impl_->finalReady = impl_->capture(1, width, height);
    return impl_->finalReady;
}

void HudOverlay::draw(const float* projection, const float* viewOffset) {
    auto& state = *impl_;
    if (!projection || !viewOffset || !state.worldReady || !state.finalReady ||
        !state.program || wglGetCurrentContext() != state.context) return;
    SavedState saved(state.gl);
    state.gl.useProgram(state.program);
    for (int unit = 0; unit < 2; ++unit) {
        state.gl.activeTexture(Texture0 + unit);
        glBindTexture(GL_TEXTURE_2D, state.textures[unit]);
        glEnable(GL_TEXTURE_2D);
    }
    state.gl.uniform1i(state.worldSampler, 0);
    state.gl.uniform1i(state.finalSampler, 1);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glDisable(GL_FOG);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_COLOR_LOGIC_OP);
    glDisable(GL_POLYGON_STIPPLE);
    GLint clipPlanes = 0;
    glGetIntegerv(GL_MAX_CLIP_PLANES, &clipPlanes);
    for (int plane = 0; plane < clipPlanes; ++plane) glDisable(GL_CLIP_PLANE0 + plane);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glEnable(GL_BLEND);
    state.gl.blendEquation(FuncAdd);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(projection);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(viewOffset);
    const float halfWidth = 1.25f;
    const float halfHeight = halfWidth * static_cast<float>(state.height) / state.width;
    state.gl.activeTexture(Texture0);
    glColor4f(1, 1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex3f(-halfWidth, -halfHeight, -2.2f);
    glTexCoord2f(1, 0); glVertex3f( halfWidth, -halfHeight, -2.2f);
    glTexCoord2f(1, 1); glVertex3f( halfWidth,  halfHeight, -2.2f);
    glTexCoord2f(0, 1); glVertex3f(-halfWidth,  halfHeight, -2.2f);
    glEnd();
}

const char* HudOverlay::lastError() const { return impl_->error.c_str(); }

void HudOverlay::shutdown() {
    if (!impl_) return;
    auto& state = *impl_;
    if (state.context && wglGetCurrentContext() == state.context) {
        if (state.program && state.gl.deleteProgram) state.gl.deleteProgram(state.program);
        if (state.textures[0] || state.textures[1]) glDeleteTextures(2, state.textures);
    }
    state.program = state.textures[0] = state.textures[1] = 0;
    state.width = state.height = 0;
    state.worldReady = state.finalReady = false;
    state.context = nullptr;
}

} // namespace kvr
