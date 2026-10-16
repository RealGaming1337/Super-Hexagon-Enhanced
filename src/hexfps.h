// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
#include <stdint.h>

#define HEXFPS_VERSION "1.0.0"

// Addresses are the game's static VAs (image base 0x400000); VA() rebases them for ASLR.
extern uintptr_t g_base;
#define VA(x) (g_base + ((uintptr_t)(x) - 0x400000))

enum { FPS_MIN = 240, FPS_MAX = 500 };
extern int g_fps;

void Log(const char* fmt, ...);
int IniGetInt(const char* key, int def);
void IniSetInt(const char* key, int value);
int ClampFps(int fps);
void SetFps(int fps, bool save);

// patch.cpp
bool Expect(uintptr_t va, const char* hex);
void WriteMem(uintptr_t va, const void* data, size_t n);
void WriteJmp(uintptr_t va, void* target, size_t len);
void* HookVtable(uintptr_t slotVa, uintptr_t expectVa, void* hook);
void* RedirectCall(uintptr_t callVa, uintptr_t expectTargetVa, void* hook);

// Register state handed to a callout handler (xmm save area followed by the pushad frame).
struct Ctx {
    double xmm[8][2];
    uint32_t edi, esi, ebp, esp_, ebx, edx, ecx, eax;
};
// Returns the absolute address to continue at, or 0 for "right after the patched bytes".
typedef uintptr_t(__cdecl* CalloutHandler)(Ctx* c, int site);
// Overwrites [va, va+len) with a jump to a trampoline that calls handler(ctx, site), then
// re-executes the original bytes [va+keep, va+len) and continues.
bool Callout(uintptr_t va, size_t len, size_t keep, CalloutHandler handler, int site, const char* expectHex);

// stats.cpp: optional once-per-second log of real FPS vs. game speed (HexFPS.ini Debug=1)
void Stats_Init();
void Stats_OnUpdate(uint8_t* app);

// smooth.cpp: error-diffused replacements for the game's int-truncated per-frame steps
struct SmoothStats {
    double playerMoved, wallMoved, wallK;
    int playerFrames, wallFrames, playerSpeed;
};
extern SmoothStats g_smooth;
bool Smooth_Init();

// camera.cpp: spin/tilt/zoom/banner effects that stepped per update instead of per game time
struct SpinStats {
    bool active;
    float lastAngle;
    double time, angle, lastTime, lastTotal;
    int done;
};
extern SpinStats g_spin;
bool Camera_Init();
void Camera_OnDraw();

// ui.cpp: OPTIONS entries, FPS counter, watermark, widescreen
bool Ui_Init();
void Ui_BeforeUpdate(uint8_t* app);
extern int g_drawCount;
