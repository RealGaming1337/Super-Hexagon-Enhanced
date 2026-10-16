// SPDX-License-Identifier: MIT
// HexFPS: frame-rate unlock for Super Hexagon (2022 Ethan Lee port, SuperHexagon.exe 0x62953532).
//
// The game already supports running faster than 60 Hz (the mobile 120 Hz path); on PC it is
// locked at 60. We drive that same path at 240..500 Hz and fix the two spots that only work
// for 120 Hz:
//   1. the per-frame speed multiplier (dt * 60) is rounded to 0.1 -> exact only for 60/120 Hz
//   2. the 60 Hz logic tick resets its accumulator instead of subtracting -> drifts unless
//      fps is a multiple of 60
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include "hexfps.h"

uintptr_t g_base;
int g_fps = 500;
static char g_dir[MAX_PATH];

// ---- logging / config -------------------------------------------------------------------

void Log(const char* fmt, ...)
{
    char path[MAX_PATH];
    wsprintfA(path, "%sHexFPS.log", g_dir);
    FILE* f = nullptr;
    if (fopen_s(&f, path, "a") || !f) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(f, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static void IniPath(char* out) { wsprintfA(out, "%sHexFPS.ini", g_dir); }

int ClampFps(int fps) { return fps < FPS_MIN ? FPS_MIN : fps > FPS_MAX ? FPS_MAX : fps; }

int IniGetInt(const char* key, int def)
{
    char ini[MAX_PATH];
    IniPath(ini);
    return (int)GetPrivateProfileIntA("HexFPS", key, def, ini);
}

void IniSetInt(const char* key, int value)
{
    char ini[MAX_PATH], buf[16];
    IniPath(ini);
    wsprintfA(buf, "%d", value);
    WritePrivateProfileStringA("HexFPS", key, buf, ini);
}

// ---- 1. exact speed multiplier ----------------------------------------------------------
// 0x431140 float round1(float x) { return (int)(x * 10 + 0.5) / 10.0f; }, only caller is the
// multiplier update at 0x429175. Replace with identity: fld dword [esp+4]; ret.
static bool PatchRounding()
{
    if (!Expect(0x431140, "55 8B EC F3 0F 10 45 08")) return false;
    static const uint8_t code[] = { 0xD9, 0x44, 0x24, 0x04, 0xC3 };
    WriteMem(0x431140, code, sizeof(code));
    return true;
}

// ---- 2. exact 60 Hz logic tick ----------------------------------------------------------
// 0x429205: ++tickCount(+0x2938); tickAccum(+0x2930) = 0.0; tickFlag(+0x293c) = 1;
// Make it tickAccum -= 1/60 (xmm0 still holds tickAccum from the compare at 0x4291d2).
static const double kTick = 1.0 / 60.0;
static uintptr_t g_tickRet;

static __declspec(naked) void TickStub()
{
    __asm {
        subsd   xmm0, qword ptr [kTick]
        comisd  xmm0, qword ptr [kTick]
        jb      keep
        xorps   xmm0, xmm0                   // more than a whole tick behind: resync
    keep:
        movsd   qword ptr [esi + 0x2930], xmm0
        jmp     dword ptr [g_tickRet]
    }
}

static bool PatchTick()
{
    if (!Expect(0x429205, "FF 86 38 29 00 00 0F 57 C0 F2 0F 11 86 30 29 00 00 C6 86 3C 29 00 00 01"))
        return false;
    g_tickRet = VA(0x429216);
    WriteJmp(0x42920B, (void*)TickStub, 11);
    return true;
}

// ---- 3. drive the frame rate ------------------------------------------------------------
// FNA-style Game.Tick object, global pointer at 0x55ed28:
//   +0x98 int64 TargetElapsedTime (perf ticks)   +0x4c8 int64 perf frequency
// ofBaseApp (the game) fields read by update():
//   +0x40cc8 int fpsSetting (60 on PC)   +0x40cd0 double step = 1/fps
typedef void(__thiscall* UpdateFn)(void*);
static UpdateFn g_origUpdate;
static int g_appliedFps;

void ApplyFps(uint8_t* app)
{
    uint8_t* loop = *(uint8_t**)VA(0x55ED28);
    if (!loop || !app) return;
    int64_t freq = *(int64_t*)(loop + 0x4C8);
    if (freq <= 0) return;
    int fps = g_fps;
    *(int64_t*)(loop + 0x98) = freq / fps;
    *(int*)(app + 0x40CC8) = fps;
    *(double*)(app + 0x40CD0) = 1.0 / fps;
    if (g_appliedFps != fps) {
        Log("frame rate set to %d (target %lld ticks @ %lld Hz)", fps, freq / fps, freq);
        g_appliedFps = fps;
    }
}

static void __fastcall HookedUpdate(uint8_t* app, void*)
{
    ApplyFps(app);
    Ui_BeforeUpdate(app);
    g_origUpdate(app);
    Stats_OnUpdate(app);
}

void SetFps(int fps, bool save)
{
    g_fps = ClampFps(fps);
    if (save) IniSetInt("FPS", g_fps);
}

// ---- init -------------------------------------------------------------------------------

static void Init()
{
    g_base = (uintptr_t)GetModuleHandleA(nullptr);
    if (!Expect(0x431000, "55 8B EC 83 EC 08 56 57 8B F1")) {
        Log("unrecognised SuperHexagon.exe, HexFPS disabled");
        return;
    }
    g_fps = ClampFps(IniGetInt("FPS", 500));
    bool a = PatchRounding();
    bool b = PatchTick();
    bool c = Smooth_Init();
    bool d = Ui_Init();
    bool e = Camera_Init();
    g_origUpdate = (UpdateFn)HookVtable(0x4F1E68, 0x431000, (void*)HookedUpdate);
    Log("HexFPS " HEXFPS_VERSION " loaded: base=%p fps=%d rounding=%d tick=%d smooth=%d ui=%d camera=%d update=%d",
        (void*)g_base, g_fps, a, b, c, d, e, g_origUpdate != nullptr);
    Stats_Init();
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        GetModuleFileNameA(nullptr, g_dir, MAX_PATH);
        char* slash = strrchr(g_dir, '\\');
        if (slash) slash[1] = 0;
        // Only touch Super Hexagon, never another program that happens to load this folder's DLLs.
        char exe[MAX_PATH];
        GetModuleFileNameA(nullptr, exe, MAX_PATH);
        const char* name = strrchr(exe, '\\');
        if (name && !lstrcmpiA(name + 1, "SuperHexagon.exe")) Init();
    }
    return TRUE;
}
