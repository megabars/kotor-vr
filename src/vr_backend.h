#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <memory>

namespace kvr {

struct EyeData {
    // OpenGL column-major matrices. Load projection directly, and multiply
    // viewOffset * the game's unchanged third-person view matrix.
    float projection[16]{};
    float viewOffset[16]{};
    int width = 0;
    int height = 0;
};

class VrBackend {
public:
    using Logger = void (*)(const char*);
    VrBackend();
    ~VrBackend();
    VrBackend(const VrBackend&) = delete;
    VrBackend& operator=(const VrBackend&) = delete;
    void setLogger(Logger logger);
    // The game's context must be current on the calling thread for all methods.
    bool initialize(HDC dc, HGLRC context);
    // False means the caller should render its ordinary desktop frame. Any XR
    // frame that should not render is ended internally before false is returned.
    bool beginFrame();
    bool beginEye(int index);
    void endEye();
    void endFrame();
    const EyeData& eye(int index) const;
    void recenter();
    void setClipPlanes(float nearZ, float farZ);
    void setWorldScale(float gameUnitsPerMeter);
    bool active() const;
    const char* lastError() const;
    void shutdown();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace kvr
