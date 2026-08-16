#pragma once

#ifdef __WIIU__

#include <padscore/kpad.h>
#include <vpad/input.h>

#include "fast/backends/gfx_window_manager_api.h"

// Keep the default framebuffer at 1080p so the renderer does not rescale it.
#define WIIU_DEFAULT_FB_WIDTH 1920
#define WIIU_DEFAULT_FB_HEIGHT 1080

namespace Fast {

extern bool has_foreground;
extern uint32_t frametime;

bool GfxWiiUInitMem1();
void GfxWiiUDestroyMem1();
bool GfxWiiUInitForeground();
void GfxWiiUDestroyForeground();
void* GfxWiiUAllocMem1(uint32_t size, uint32_t alignment);
void GfxWiiUFreeMem1(void* block);
void* GfxWiiUAllocForeground(uint32_t size, uint32_t alignment);
void GfxWiiUFreeForeground(void* block);
void GfxWiiUSetContextState();

class GfxWindowBackendWiiU final : public GfxWindowBackend {
  public:
    ~GfxWindowBackendWiiU() override = default;

    void Init(const char* gameName, const char* apiName, bool startFullScreen, uint32_t width, uint32_t height,
              int32_t posX, int32_t posY) override;
    void Close() override;
    void SetKeyboardCallbacks(bool (*onKeyDown)(int scancode), bool (*onKeyUp)(int scancode),
                              void (*onAllKeysUp)()) override;
    void SetMouseCallbacks(bool (*onMouseButtonDown)(int btn), bool (*onMouseButtonUp)(int btn)) override;
    void SetFullscreenChangedCallback(void (*onFullscreenChanged)(bool isNowFullscreen)) override;
    void SetFullscreen(bool fullscreen) override;
    void GetActiveWindowRefreshRate(uint32_t* refreshRate) override;
    void SetCursorVisibility(bool visibility) override;
    void SetMousePos(int32_t posX, int32_t posY) override;
    void GetMousePos(int32_t* x, int32_t* y) override;
    void GetMouseDelta(int32_t* x, int32_t* y) override;
    void GetMouseWheel(float* x, float* y) override;
    bool GetMouseState(uint32_t btn) override;
    void SetMouseCapture(bool capture) override;
    bool IsMouseCaptured() override;
    void GetDimensions(uint32_t* width, uint32_t* height, int32_t* posX, int32_t* posY) override;
    void HandleEvents() override;
    bool IsFrameReady() override;
    void SwapBuffersBegin() override;
    void SwapBuffersEnd() override;
    double GetTime() override;
    int GetTargetFps() override;
    void SetTargetFps(int fps) override;
    void SetMaxFrameLatency(int latency) override;
    const char* GetKeyName(int scancode) override;
    bool CanDisableVsync() override;
    bool IsRunning() override;
    void Destroy() override;
    bool IsFullscreen() override;
};

} // namespace Fast

#endif
