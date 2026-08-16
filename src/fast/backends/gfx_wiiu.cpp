#ifdef __WIIU__

#include <stdio.h>
#include <time.h>
#include <malloc.h>

#include <coreinit/time.h>
#include <coreinit/foreground.h>
#include <coreinit/memory.h>
#include <coreinit/memheap.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memexpheap.h>
#include <coreinit/memfrmheap.h>

#include <gx2/state.h>
#include <gx2/context.h>
#include <gx2/display.h>
#include <gx2/event.h>
#include <gx2/swap.h>
#include <gx2/mem.h>
#include <gx2r/mem.h>

#include <whb/proc.h>
#include <proc_ui/procui.h>
#include <proc_ui/memory.h>

#include <vpad/input.h>
#include <padscore/kpad.h>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif

#include "fast/backends/gfx_gx2.h"
#include "fast/backends/gfx_wiiu.h"

#include "ship/port/wiiu/ImGui/imgui_impl_wiiu.h"
#include "ship/port/wiiu/WiiUImpl.h"
#include "libultraship/classes.h"

namespace Fast {

static MEMHeapHandle heap_MEM1 = nullptr;
static MEMHeapHandle heap_foreground = nullptr;

bool has_foreground = false;
static void* mem1_storage = nullptr;
static void* command_buffer_pool = nullptr;
GX2ContextState* context_state = nullptr;

static GX2TVRenderMode tv_render_mode;
static void* tv_scan_buffer = nullptr;
static uint32_t tv_scan_buffer_size = 0;
static uint32_t tv_width;
static uint32_t tv_height;

static GX2DrcRenderMode drc_render_mode;
static void* drc_scan_buffer = nullptr;
static uint32_t drc_scan_buffer_size = 0;

static int frame_divisor = 1;

// for ImGui DeltaTime
// (initialized to 1 to not trigger imguis assert on initial draw)
uint32_t frametime = 1;

bool GfxWiiUInitMem1() {
    MEMHeapHandle heap = MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM1);
    uint32_t size;
    void* base;

    size = MEMGetAllocatableSizeForFrmHeapEx(heap, 4);
    if (!size) {
        printf("%s: MEMGetAllocatableSizeForFrmHeapEx == 0", __FUNCTION__);
        return false;
    }

    base = MEMAllocFromFrmHeapEx(heap, size, 4);
    if (!base) {
        printf("%s: MEMAllocFromFrmHeapEx(heap, 0x%X, 4) failed", __FUNCTION__, size);
        return false;
    }

    heap_MEM1 = MEMCreateExpHeapEx(base, size, 0);
    if (!heap_MEM1) {
        printf("%s: MEMCreateExpHeapEx(%p, 0x%X, 0) failed", __FUNCTION__, base, size);
        return false;
    }

    return true;
}

void gfx_wiiu_close(void) {
}

void GfxWiiUDestroyMem1() {
    MEMHeapHandle heap = MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM1);

    if (heap_MEM1) {
        MEMDestroyExpHeap(heap_MEM1);
        heap_MEM1 = NULL;
    }
}

bool GfxWiiUInitForeground() {
    MEMHeapHandle heap = MEMGetBaseHeapHandle(MEM_BASE_HEAP_FG);
    uint32_t size;
    void* base;

    size = MEMGetAllocatableSizeForFrmHeapEx(heap, 4);
    if (!size) {
        printf("%s: MEMAllocFromFrmHeapEx(heap, 0x%X, 4)", __FUNCTION__, size);
        return false;
    }

    base = MEMAllocFromFrmHeapEx(heap, size, 4);
    if (!base) {
        printf("%s: MEMGetAllocatableSizeForFrmHeapEx == 0", __FUNCTION__);
        return false;
    }

    heap_foreground = MEMCreateExpHeapEx(base, size, 0);
    if (!heap_foreground) {
        printf("%s: MEMCreateExpHeapEx(%p, 0x%X, 0)", __FUNCTION__, base, size);
        return false;
    }

    return true;
}

void GfxWiiUDestroyForeground() {
    MEMHeapHandle foreground = MEMGetBaseHeapHandle(MEM_BASE_HEAP_FG);

    if (heap_foreground) {
        MEMDestroyExpHeap(heap_foreground);
        heap_foreground = NULL;
    }

    MEMFreeToFrmHeap(foreground, MEM_FRM_HEAP_FREE_ALL);
}

void* GfxWiiUAllocMem1(uint32_t size, uint32_t alignment) {
    void* block;

    if (!heap_MEM1) {
        return NULL;
    }

    if (alignment < 4) {
        alignment = 4;
    }

    block = MEMAllocFromExpHeapEx(heap_MEM1, size, alignment);
    return block;
}

void GfxWiiUFreeMem1(void* block) {
    if (!heap_MEM1) {
        return;
    }

    MEMFreeToExpHeap(heap_MEM1, block);
}

void* GfxWiiUAllocForeground(uint32_t size, uint32_t alignment) {
    void* block;

    if (!heap_foreground) {
        return NULL;
    }

    if (alignment < 4) {
        alignment = 4;
    }

    block = MEMAllocFromExpHeapEx(heap_foreground, size, alignment);
    return block;
}

void GfxWiiUFreeForeground(void* block) {
    if (!heap_foreground) {
        return;
    }

    MEMFreeToExpHeap(heap_foreground, block);
}

static uint32_t gfx_wiiu_proc_callback_acquired(void* context) {
    has_foreground = true;

    bool result = GfxWiiUInitForeground();
    assert(result);

    tv_scan_buffer = GfxWiiUAllocForeground(tv_scan_buffer_size, GX2_SCAN_BUFFER_ALIGNMENT);
    assert(tv_scan_buffer);

    GX2Invalidate(GX2_INVALIDATE_MODE_CPU, tv_scan_buffer, tv_scan_buffer_size);
    GX2SetTVBuffer(tv_scan_buffer, tv_scan_buffer_size, tv_render_mode, GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8,
                   GX2_BUFFERING_MODE_DOUBLE);

    drc_scan_buffer = GfxWiiUAllocForeground(drc_scan_buffer_size, GX2_SCAN_BUFFER_ALIGNMENT);
    assert(drc_scan_buffer);

    GX2Invalidate(GX2_INVALIDATE_MODE_CPU, drc_scan_buffer, drc_scan_buffer_size);
    GX2SetDRCBuffer(drc_scan_buffer, drc_scan_buffer_size, drc_render_mode, GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8,
                    GX2_BUFFERING_MODE_DOUBLE);

    return 0;
}

static uint32_t gfx_wiiu_proc_callback_released(void* context) {
    if (tv_scan_buffer) {
        GfxWiiUFreeForeground(tv_scan_buffer);
        tv_scan_buffer = nullptr;
    }

    if (drc_scan_buffer) {
        GfxWiiUFreeForeground(drc_scan_buffer);
        drc_scan_buffer = nullptr;
    }

    GfxWiiUDestroyForeground();

    has_foreground = false;

    return 0;
}

static void gfx_wiiu_init(const char* game_name, const char* gfx_api_name, bool start_in_fullscreen, uint32_t width,
                          uint32_t height, int32_t posX, int32_t posY) {
    WHBProcInit();

    uint32_t mem1_addr, mem1_size;
    OSGetMemBound(OS_MEM1, &mem1_addr, &mem1_size);
    mem1_storage = memalign(0x40, mem1_size);
    assert(mem1_storage);

    ProcUISetMEM1Storage(mem1_storage, mem1_size);

    bool result = GfxWiiUInitMem1();
    assert(result);

    command_buffer_pool = memalign(GX2_COMMAND_BUFFER_ALIGNMENT, 0x400000);
    assert(command_buffer_pool);

    uint32_t initAttribs[] = { GX2_INIT_CMD_BUF_BASE,
                               (uintptr_t)command_buffer_pool,
                               GX2_INIT_CMD_BUF_POOL_SIZE,
                               0x400000,
                               GX2_INIT_ARGC,
                               0,
                               GX2_INIT_ARGV,
                               0,
                               GX2_INIT_END };
    GX2Init(initAttribs);

    switch (GX2GetSystemTVScanMode()) {
        case GX2_TV_SCAN_MODE_480I:
        case GX2_TV_SCAN_MODE_480P:
            tv_render_mode = GX2_TV_RENDER_MODE_WIDE_480P;
            tv_width = 854;
            tv_height = 480;
            break;
        case GX2_TV_SCAN_MODE_1080I:
        case GX2_TV_SCAN_MODE_1080P:
            tv_render_mode = GX2_TV_RENDER_MODE_WIDE_1080P;
            tv_width = 1920;
            tv_height = 1080;
            break;
        case GX2_TV_SCAN_MODE_720P:
        default:
            tv_render_mode = GX2_TV_RENDER_MODE_WIDE_720P;
            tv_width = 1280;
            tv_height = 720;
            break;
    }

    drc_render_mode = GX2GetSystemDRCScanMode();

    uint32_t unk;
    GX2CalcTVSize(tv_render_mode, GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8, GX2_BUFFERING_MODE_DOUBLE, &tv_scan_buffer_size,
                  &unk);
    GX2CalcDRCSize(drc_render_mode, GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8, GX2_BUFFERING_MODE_DOUBLE,
                   &drc_scan_buffer_size, &unk);

    ProcUIRegisterCallback(PROCUI_CALLBACK_ACQUIRE, gfx_wiiu_proc_callback_acquired, nullptr, 100);
    ProcUIRegisterCallback(PROCUI_CALLBACK_RELEASE, gfx_wiiu_proc_callback_released, nullptr, 100);

    gfx_wiiu_proc_callback_acquired(nullptr);

    context_state = (GX2ContextState*)memalign(GX2_CONTEXT_STATE_ALIGNMENT, sizeof(GX2ContextState));
    assert(context_state);

    GX2SetupContextStateEx(context_state, TRUE);
    GX2SetContextState(context_state);

    GX2SetTVScale(WIIU_DEFAULT_FB_WIDTH, WIIU_DEFAULT_FB_HEIGHT);
    GX2SetDRCScale(WIIU_DEFAULT_FB_WIDTH, WIIU_DEFAULT_FB_HEIGHT);

    GX2SetSwapInterval(frame_divisor);

    Ship::GuiWindowInitData window_impl;
    window_impl.Gx2.Width = WIIU_DEFAULT_FB_WIDTH;
    window_impl.Gx2.Height = WIIU_DEFAULT_FB_HEIGHT;
    Ship::Context::GetInstance()->GetWindow()->GetGui()->Init(window_impl);
}

static void gfx_wiiu_shutdown(void) {
    if (has_foreground) {
        gfx_wiiu_proc_callback_released(nullptr);
        GfxWiiUDestroyMem1();
    }

    GX2Shutdown();

    if (context_state) {
        free(context_state);
        context_state = nullptr;
    }

    if (command_buffer_pool) {
        free(command_buffer_pool);
        command_buffer_pool = nullptr;
    }

    ProcUISetMEM1Storage(nullptr, 0);
    free(mem1_storage);
}

void GfxWiiUSetContextState() {
    GX2SetContextState(context_state);
}

static void gfx_wiiu_set_fullscreen_changed_callback(void (*on_fullscreen_changed)(bool is_now_fullscreen)) {
}

static void gfx_wiiu_set_fullscreen(bool enable) {
}

static void gfx_wiiu_get_active_window_refresh_rate(uint32_t* refresh_rate) {
    *refresh_rate = 60;
}

static void gfx_wiiu_set_cursor_visibility(bool hide) {
}

static void gfx_wiiu_set_mouse_pos(int32_t x, int32_t y) {
}

static void gfx_wiiu_get_mouse_pos(int32_t* x, int32_t* y) {
    *x = 0;
    *y = 0;
}

static void gfx_wiiu_get_mouse_delta(int32_t* x, int32_t* y) {
    *x = 0;
    *y = 0;
}

static void gfx_wiiu_get_mouse_wheel(float* x, float* y) {
    *x = 0;
    *y = 0;
}

static bool gfx_wiiu_get_mouse_state(uint32_t btn) {
    return false;
}

static void gfx_wiiu_set_mouse_capture(bool capture) {
}

static bool gfx_wiiu_is_mouse_captured() {
    return false;
}

static void gfx_wiiu_set_keyboard_callbacks(bool (*on_key_down)(int scancode), bool (*on_key_up)(int scancode),
                                            void (*on_all_keys_up)(void)) {
}

static void gfx_wiiu_get_dimensions(uint32_t* width, uint32_t* height, int32_t* posX, int32_t* posY) {
    *width = WIIU_DEFAULT_FB_WIDTH;
    *height = WIIU_DEFAULT_FB_HEIGHT;
    *posX = 0;
    *posY = 0;
}

static void gfx_wiiu_handle_events(void) {
    Ship::WiiU::Update();

    ImGui_ImplWiiU_ControllerInput input{};

    VPADReadError vpad_error;
    input.vpad = Ship::WiiU::GetVPADStatus(&vpad_error);
    if (vpad_error != VPAD_READ_SUCCESS) {
        input.vpad = nullptr;
    }

    KPADError kpad_error;
    for (int i = 0; i < 4; i++) {
        input.kpad[i] = Ship::WiiU::GetKPADStatus((WPADChan)i, &kpad_error);
        if (kpad_error != KPAD_ERROR_OK) {
            input.kpad[i] = nullptr;
        }
    }

    Ship::WindowEvent event_impl;
    event_impl.Gx2.Input = &input;
    Ship::Context::GetInstance()->GetWindow()->GetGui()->HandleWindowEvents(event_impl);
}

static bool gfx_wiiu_start_frame(void) {
    uint32_t swap_count, flip_count;
    OSTime last_flip, last_vsync;
    uint32_t wait_count = 0;

    while (true) {
        GX2GetSwapStatus(&swap_count, &flip_count, &last_flip, &last_vsync);

        if (flip_count >= swap_count) {
            break;
        }

        if (wait_count >= 10) {
            // GPU timed out, drop frame
            return false;
        }

        wait_count++;
        GX2WaitForVsync();
    }

    return true;
}

static void gfx_wiiu_swap_buffers_begin(void) {
    GX2SwapScanBuffers();
    GX2Flush();

    GfxWiiUSetContextState();

    GX2SetTVEnable(TRUE);
    GX2SetDRCEnable(TRUE);
}

static void gfx_wiiu_swap_buffers_end(void) {
    static OSTick tick = 0;
    frametime = OSTicksToMicroseconds(OSGetSystemTick() - tick);
    tick = OSGetSystemTick();
}

static double gfx_wiiu_get_time(void) {
    return 0.0;
}

static void gfx_wiiu_set_target_fps(int fps) {
    // use the nearest divisor
    int divisor = fps > 0 ? 60 / fps : 1;
    if (divisor < 1) {
        divisor = 1;
    }

    if (frame_divisor != divisor) {
        GX2SetSwapInterval(divisor);
        frame_divisor = divisor;
    }
}

static int gfx_wiiu_get_target_fps() {
    return 60 / frame_divisor;
}

static void gfx_wiiu_set_maximum_frame_latency(int latency) {
}

static const char* gfx_wiiu_get_key_name(int scancode) {
    return "";
}

bool gfx_wiiu_can_disable_vsync() {
    return false;
}

bool gfx_wiiu_is_running(void) {
    return WHBProcIsRunning();
}

void gfx_wiiu_destroy(void) {
    Ship::WiiU::Exit();

    GfxGX2Shutdown();
    gfx_wiiu_shutdown();
    WHBProcShutdown();
}

bool gfx_wiiu_is_fullscreen(void) {
    return true;
}

void GfxWindowBackendWiiU::Init(const char* gameName, const char* apiName, bool startFullScreen, uint32_t width,
                                uint32_t height, int32_t posX, int32_t posY) {
    gfx_wiiu_init(gameName, apiName, startFullScreen, width, height, posX, posY);
}
void GfxWindowBackendWiiU::Close() { gfx_wiiu_close(); }
void GfxWindowBackendWiiU::SetKeyboardCallbacks(bool (*onKeyDown)(int), bool (*onKeyUp)(int),
                                                void (*onAllKeysUp)()) {
    mOnKeyDown = onKeyDown;
    mOnKeyUp = onKeyUp;
    gfx_wiiu_set_keyboard_callbacks(onKeyDown, onKeyUp, onAllKeysUp);
}
void GfxWindowBackendWiiU::SetMouseCallbacks(bool (*onMouseButtonDown)(int), bool (*onMouseButtonUp)(int)) {
    mOnMouseButtonDown = onMouseButtonDown;
    mOnMouseButtonUp = onMouseButtonUp;
}
void GfxWindowBackendWiiU::SetFullscreenChangedCallback(void (*onFullscreenChanged)(bool)) {
    mOnFullscreenChanged = onFullscreenChanged;
    gfx_wiiu_set_fullscreen_changed_callback(onFullscreenChanged);
}
void GfxWindowBackendWiiU::SetFullscreen(bool fullscreen) { gfx_wiiu_set_fullscreen(fullscreen); }
void GfxWindowBackendWiiU::GetActiveWindowRefreshRate(uint32_t* refreshRate) {
    gfx_wiiu_get_active_window_refresh_rate(refreshRate);
}
void GfxWindowBackendWiiU::SetCursorVisibility(bool visibility) { gfx_wiiu_set_cursor_visibility(visibility); }
void GfxWindowBackendWiiU::SetMousePos(int32_t posX, int32_t posY) { gfx_wiiu_set_mouse_pos(posX, posY); }
void GfxWindowBackendWiiU::GetMousePos(int32_t* x, int32_t* y) { gfx_wiiu_get_mouse_pos(x, y); }
void GfxWindowBackendWiiU::GetMouseDelta(int32_t* x, int32_t* y) { gfx_wiiu_get_mouse_delta(x, y); }
void GfxWindowBackendWiiU::GetMouseWheel(float* x, float* y) { gfx_wiiu_get_mouse_wheel(x, y); }
bool GfxWindowBackendWiiU::GetMouseState(uint32_t btn) { return gfx_wiiu_get_mouse_state(btn); }
void GfxWindowBackendWiiU::SetMouseCapture(bool capture) { gfx_wiiu_set_mouse_capture(capture); }
bool GfxWindowBackendWiiU::IsMouseCaptured() { return gfx_wiiu_is_mouse_captured(); }
void GfxWindowBackendWiiU::GetDimensions(uint32_t* width, uint32_t* height, int32_t* posX, int32_t* posY) {
    gfx_wiiu_get_dimensions(width, height, posX, posY);
}
void GfxWindowBackendWiiU::HandleEvents() { gfx_wiiu_handle_events(); }
bool GfxWindowBackendWiiU::IsFrameReady() { return gfx_wiiu_start_frame(); }
void GfxWindowBackendWiiU::SwapBuffersBegin() { gfx_wiiu_swap_buffers_begin(); }
void GfxWindowBackendWiiU::SwapBuffersEnd() { gfx_wiiu_swap_buffers_end(); }
double GfxWindowBackendWiiU::GetTime() { return gfx_wiiu_get_time(); }
int GfxWindowBackendWiiU::GetTargetFps() { return gfx_wiiu_get_target_fps(); }
void GfxWindowBackendWiiU::SetTargetFps(int fps) { gfx_wiiu_set_target_fps(fps); }
void GfxWindowBackendWiiU::SetMaxFrameLatency(int latency) { gfx_wiiu_set_maximum_frame_latency(latency); }
const char* GfxWindowBackendWiiU::GetKeyName(int scancode) { return gfx_wiiu_get_key_name(scancode); }
bool GfxWindowBackendWiiU::CanDisableVsync() { return gfx_wiiu_can_disable_vsync(); }
bool GfxWindowBackendWiiU::IsRunning() { return gfx_wiiu_is_running(); }
void GfxWindowBackendWiiU::Destroy() { gfx_wiiu_destroy(); }
bool GfxWindowBackendWiiU::IsFullscreen() { return gfx_wiiu_is_fullscreen(); }

} // namespace Fast

#endif
