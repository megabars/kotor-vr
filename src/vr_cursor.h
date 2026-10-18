#pragma once
#include <windows.h>
#include <memory>

namespace kvr {
class VrCursor {
public:
    VrCursor();
    ~VrCursor();
    void captureWorld(HDC dc,int width,int height,const float* gameProjection);
    void captureFinal(HDC dc,int width,int height);
    bool draw(const float* projection,const float* viewOffset,bool fullPanel);
    void endFrame();
    bool visible() const;
    bool overInterface() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
