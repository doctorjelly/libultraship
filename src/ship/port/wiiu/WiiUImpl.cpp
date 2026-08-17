#ifdef __WIIU__
#include "ship/port/wiiu/WiiUImpl.h"

#include <stdio.h>
#include <map>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/iosupport.h>

#include <whb/log.h>
#include <whb/log_udp.h>
#include <coreinit/debug.h>

#include <SDL2/SDL.h>

namespace Ship {
namespace WiiU {

static bool updateControllers;
static std::map<int, SDL_GameController*> controllers;

static bool hasVpad = false;
static VPADReadError vpadError;
static VPADStatus vpadStatus;

static bool hasKpad[4] = { false };
static KPADError kpadError[4] = { KPAD_ERROR_OK };
static KPADStatus kpadStatus[4];

#ifdef _DEBUG
extern "C" {
void __wrap_abort() {
    printf("Abort called.\n");
    // force a stack trace
    *(uint32_t*)0xdeadc0de = 0xcafebabe;
    while (1)
        ;
}

static ssize_t wiiu_log_write(struct _reent* r, void* fd, const char* ptr, size_t len) {
    char buf[1024];
    snprintf(buf, sizeof(buf), "%*.*s", len, len, ptr);
    OSReport(buf);
    WHBLogWritef("%*.*s", len, len, ptr);
    return len;
}

static const devoptab_t dotab_stdout = {
    .name = "stdout_whb",
    .write_r = wiiu_log_write,
};
};
#endif

void Init(const std::string& shortName) {
#ifdef _DEBUG
    WHBLogUdpInit();
    WHBLogPrint("Hello World!");

    devoptab_list[STD_OUT] = &dotab_stdout;
    devoptab_list[STD_ERR] = &dotab_stdout;
#endif

    // make sure the required folders exist
    mkdir("/vol/external01/wiiu/", 0755);
    mkdir("/vol/external01/wiiu/apps/", 0755);
    const std::string appPath = "/vol/external01/wiiu/apps/" + shortName + "/";
    mkdir(appPath.c_str(), 0755);

    if (chdir(appPath.c_str()) != 0) {
        OSFatal("Could not access the Ship of Harkinian folder on the SD card.");
        return;
    }

    // We construct or input based on SDL
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        OSFatal("Could not initialize Wii U controller input.");
        return;
    }
    updateControllers = true;
}

void Exit() {
    for (auto& [index, controller] : controllers) {
        SDL_GameControllerClose(controller);
    }
    controllers.clear();

    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);

#ifdef _DEBUG
    WHBLogUdpDeinit();
#endif
}

void ThrowMissingOTR(const char* otrPath) {
    // TODO handle this better in the future
    OSFatal("Main OTR file not found!");
}

void ThrowInvalidOTR() {
    OSFatal("Invalid OTR files! Try regenerating them!");
}

static void UpdateVPADButtonState(VPADStatus* status, VPADButtons button, bool pressed) {
    if (pressed) {
        // Set the trigger bit if it wasn't held before
        if (!(status->hold & button)) {
            status->trigger |= button;
        } else {
            status->trigger &= ~button;
        }

        status->hold |= button;
        status->release &= ~button;
    } else {
        // Set the release bit if it was held before
        if (status->hold & button) {
            status->release |= button;
        } else {
            status->release &= ~button;
        }

        status->hold &= ~button;
        status->trigger &= ~button;
    }
}

static void UpdateVPADButton(VPADStatus* status, SDL_GameController* controller, VPADButtons button,
                             SDL_GameControllerButton sdlButton) {
    UpdateVPADButtonState(status, button, SDL_GameControllerGetButton(controller, sdlButton) != 0);
}

static void UpdateKPADProButtonState(KPADStatus* status, WPADProButton button, bool pressed) {
    if (pressed) {
        // Set the trigger bit if it wasn't held before
        if (!(status->pro.hold & button)) {
            status->pro.trigger |= button;
        } else {
            status->pro.trigger &= ~button;
        }

        status->pro.hold |= button;
        status->pro.release &= ~button;
    } else {
        // Set the release bit if it was held before
        if (status->pro.hold & button) {
            status->pro.release |= button;
        } else {
            status->pro.release &= ~button;
        }

        status->pro.hold &= ~button;
        status->pro.trigger &= ~button;
    }
}

static void UpdateKPADProButton(KPADStatus* status, SDL_GameController* controller, WPADProButton button,
                                SDL_GameControllerButton sdlButton) {
    UpdateKPADProButtonState(status, button, SDL_GameControllerGetButton(controller, sdlButton) != 0);
}

static float GetControllerAxis(SDL_GameController* controller, SDL_GameControllerAxis axis) {
    const Sint16 value = SDL_GameControllerGetAxis(controller, axis);
    return value < 0 ? static_cast<float>(value) / 32768.0f : static_cast<float>(value) / 32767.0f;
}

static bool GetControllerTrigger(SDL_GameController* controller, SDL_GameControllerAxis axis) {
    return SDL_GameControllerGetAxis(controller, axis) > 16384;
}

void Update() {
    SDL_PumpEvents();

    SDL_Event event;
    // Leave controller events queued for SDLAddRemoveDeviceEventHandler to update gameplay input.
    if (SDL_PeepEvents(&event, 1, SDL_PEEKEVENT, SDL_CONTROLLERDEVICEADDED, SDL_CONTROLLERDEVICEREMOVED) > 0) {
        updateControllers = true;
    }

    // SDL controller mappings poll current state rather than consuming axis/button events. Drain everything except
    // device add/remove events so analog input does not make the SDL event queue grow indefinitely.
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_CONTROLLERDEVICEADDED - 1) > 0) {
    }
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_CONTROLLERDEVICEREMOVED + 1, SDL_LASTEVENT) > 0) {
    }

    if (updateControllers) {
        for (auto& [index, controller] : controllers) {
            SDL_GameControllerClose(controller);
        }
        controllers.clear();
        hasVpad = false;
        for (bool& connected : hasKpad) {
            connected = false;
        }

        int numJoysticks = SDL_NumJoysticks();
        for (int i = 0; i < numJoysticks; i++) {
            if (SDL_IsGameController(i)) {
                SDL_GameController* controller = SDL_GameControllerOpen(i);
                if (controller) {
                    int playerIndex = SDL_GameControllerGetPlayerIndex(controller);
                    if (playerIndex == 0) {
                        hasVpad = true;
                    } else if (playerIndex > 0 && playerIndex <= 4) {
                        hasKpad[playerIndex - 1] = true;
                    }

                    controllers.emplace(playerIndex, controller);
                }
            }
        }
        updateControllers = false;
    }

    // SDL owns the native VPAD/KPAD reads. Reconstruct the status structures from SDL's current controller state so
    // the Wii U ImGui backend receives the same buttons and sticks as gameplay without consuming input twice.
    for (auto& [index, controller] : controllers) {
        if (index == 0) {
            UpdateVPADButton(&vpadStatus, controller, VPAD_BUTTON_A, SDL_CONTROLLER_BUTTON_A);
            UpdateVPADButton(&vpadStatus, controller, VPAD_BUTTON_B, SDL_CONTROLLER_BUTTON_B);
            UpdateVPADButton(&vpadStatus, controller, VPAD_BUTTON_X, SDL_CONTROLLER_BUTTON_X);
            UpdateVPADButton(&vpadStatus, controller, VPAD_BUTTON_Y, SDL_CONTROLLER_BUTTON_Y);
            UpdateVPADButton(&vpadStatus, controller, VPAD_BUTTON_PLUS, SDL_CONTROLLER_BUTTON_START);
            UpdateVPADButton(&vpadStatus, controller, VPAD_BUTTON_MINUS, SDL_CONTROLLER_BUTTON_BACK);
            UpdateVPADButton(&vpadStatus, controller, VPAD_BUTTON_LEFT, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
            UpdateVPADButton(&vpadStatus, controller, VPAD_BUTTON_RIGHT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
            UpdateVPADButton(&vpadStatus, controller, VPAD_BUTTON_UP, SDL_CONTROLLER_BUTTON_DPAD_UP);
            UpdateVPADButton(&vpadStatus, controller, VPAD_BUTTON_DOWN, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
            UpdateVPADButton(&vpadStatus, controller, VPAD_BUTTON_L, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
            UpdateVPADButton(&vpadStatus, controller, VPAD_BUTTON_R, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
            UpdateVPADButton(&vpadStatus, controller, VPAD_BUTTON_STICK_L, SDL_CONTROLLER_BUTTON_LEFTSTICK);
            UpdateVPADButton(&vpadStatus, controller, VPAD_BUTTON_STICK_R, SDL_CONTROLLER_BUTTON_RIGHTSTICK);
            UpdateVPADButtonState(&vpadStatus, VPAD_BUTTON_ZL,
                                  GetControllerTrigger(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT));
            UpdateVPADButtonState(&vpadStatus, VPAD_BUTTON_ZR,
                                  GetControllerTrigger(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT));

            vpadStatus.leftStick.x = GetControllerAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
            vpadStatus.leftStick.y = -GetControllerAxis(controller, SDL_CONTROLLER_AXIS_LEFTY);
            vpadStatus.rightStick.x = GetControllerAxis(controller, SDL_CONTROLLER_AXIS_RIGHTX);
            vpadStatus.rightStick.y = -GetControllerAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY);
        } else if (index > 0 && index <= 4) {
            KPADStatus* status = &kpadStatus[index - 1];
            status->extensionType = WPAD_EXT_PRO_CONTROLLER;

            UpdateKPADProButton(status, controller, WPAD_PRO_BUTTON_A, SDL_CONTROLLER_BUTTON_A);
            UpdateKPADProButton(status, controller, WPAD_PRO_BUTTON_B, SDL_CONTROLLER_BUTTON_B);
            UpdateKPADProButton(status, controller, WPAD_PRO_BUTTON_X, SDL_CONTROLLER_BUTTON_X);
            UpdateKPADProButton(status, controller, WPAD_PRO_BUTTON_Y, SDL_CONTROLLER_BUTTON_Y);
            UpdateKPADProButton(status, controller, WPAD_PRO_BUTTON_PLUS, SDL_CONTROLLER_BUTTON_START);
            UpdateKPADProButton(status, controller, WPAD_PRO_BUTTON_MINUS, SDL_CONTROLLER_BUTTON_BACK);
            UpdateKPADProButton(status, controller, WPAD_PRO_BUTTON_LEFT, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
            UpdateKPADProButton(status, controller, WPAD_PRO_BUTTON_RIGHT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
            UpdateKPADProButton(status, controller, WPAD_PRO_BUTTON_UP, SDL_CONTROLLER_BUTTON_DPAD_UP);
            UpdateKPADProButton(status, controller, WPAD_PRO_BUTTON_DOWN, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
            UpdateKPADProButton(status, controller, WPAD_PRO_TRIGGER_L, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
            UpdateKPADProButton(status, controller, WPAD_PRO_TRIGGER_R, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
            UpdateKPADProButton(status, controller, WPAD_PRO_BUTTON_STICK_L, SDL_CONTROLLER_BUTTON_LEFTSTICK);
            UpdateKPADProButton(status, controller, WPAD_PRO_BUTTON_STICK_R, SDL_CONTROLLER_BUTTON_RIGHTSTICK);
            UpdateKPADProButtonState(status, WPAD_PRO_TRIGGER_ZL,
                                     GetControllerTrigger(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT));
            UpdateKPADProButtonState(status, WPAD_PRO_TRIGGER_ZR,
                                     GetControllerTrigger(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT));

            status->pro.leftStick.x = GetControllerAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
            status->pro.leftStick.y = -GetControllerAxis(controller, SDL_CONTROLLER_AXIS_LEFTY);
            status->pro.rightStick.x = GetControllerAxis(controller, SDL_CONTROLLER_AXIS_RIGHTX);
            status->pro.rightStick.y = -GetControllerAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY);
        }
    }

    if (hasVpad) {
        vpadStatus.tpNormal.touched = false;

        int numTouchDevices = SDL_GetNumTouchDevices();
        if (numTouchDevices > 0) {
            SDL_TouchID touchId = SDL_GetTouchDevice(0);
            int numFingers = SDL_GetNumTouchFingers(touchId);
            if (numFingers > 0) {
                SDL_Finger *finger = SDL_GetTouchFinger(touchId, 0);
                if (finger) {
                    vpadStatus.tpNormal.touched = true;
                    vpadStatus.tpNormal.validity = VPAD_VALID;
                    vpadStatus.tpNormal.x = finger->x * 1280;
                    vpadStatus.tpNormal.y = finger->y * 720;
                }
            }
        }
    }
}

VPADStatus* GetVPADStatus(VPADReadError* error) {
    *error = vpadError;
    return hasVpad ? &vpadStatus : nullptr;
}

KPADStatus* GetKPADStatus(WPADChan chan, KPADError* error) {
    *error = kpadError[chan];
    return hasKpad[chan] ? &kpadStatus[chan] : nullptr;
}

}; // namespace WiiU
}; // namespace Ship

#endif
