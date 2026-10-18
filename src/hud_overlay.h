#pragma once

#include <memory>

namespace kvr {

// All methods, including shutdown/destruction, must run with the owning game
// OpenGL context current. Captures refer to the ordinary desktop backbuffer.
class HudOverlay {
public:
    HudOverlay();
    ~HudOverlay();
    HudOverlay(const HudOverlay&) = delete;
    HudOverlay& operator=(const HudOverlay&) = delete;
    bool captureWorld(int width, int height);
    bool captureFinal(int width, int height);
    void draw(const float* projection, const float* viewOffset);
    const char* lastError() const;
    void shutdown();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace kvr
