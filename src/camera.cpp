// SPDX-License-Identifier: MIT
// Camera fixes. A few camera effects step once per update instead of per unit of game time, so
// they play 4-8x too fast above 60 fps (the mobile 120 Hz build already runs them 2x fast):
//   - the "fast spin" (camera update 0x41AA60, spin states 2/4/5/7): angle += v * 0.5
//   - the tilt sweep (states 2/23 of the other effect machine): tilt -/+= 1.0
//   - the centre-hexagon zoom at the start (-20/update) and end (+20/update) of a run
//   - the rank-up banner ("LINE", "TRIANGLE", ...) timer is ticked per *draw*, not per update
// The spin/tilt constants are memory operands (0.5f at 0x4F1664, 1.0f at 0x4F1668); we point
// those instructions at our own floats holding 0.5*mult and mult. The zoom goes through the
// same error diffusion as the other integer state. The banner timer reads the multiplier summed
// over all updates since the previous draw.
#include "hexfps.h"
#include <math.h>

static float g_multF = 1.0f;
static float g_halfMultF = 0.5f;
static double g_multSinceDraw;
static double g_drawMult = 1.0;
static double g_rZoom;

SpinStats g_spin;

// Vanilla spin, per 60 Hz frame: v climbs 1..14 adding v/2 (52.5 deg), then 31 frames of 7 deg
// (217 deg), then v falls 9..0 adding v/2 (22.5 deg): 55 frames, 292 deg.
static void TrackSpin(uint8_t* app)
{
    uint8_t* cam = app + 16;
    int state = *(int*)(cam + 0x5490);
    float angle = *(float*)(cam + 0x190);
    double mult = *(double*)(app + 0x2950);
    if (state >= 2 && state <= 7) {
        if (!g_spin.active) {
            g_spin.active = true;
            g_spin.time = g_spin.angle = 0;
        } else {
            float d = angle - g_spin.lastAngle;
            if (d > 180) d -= 360;
            if (d < -180) d += 360;
            g_spin.angle += fabs(d);
        }
        g_spin.time += mult / 60.0;
        g_spin.lastAngle = angle;
    } else if (g_spin.active) {
        g_spin.active = false;
        g_spin.done++;
        g_spin.lastTime = g_spin.time;
        g_spin.lastTotal = g_spin.angle;
    }
}

// Redirected call to the multiplier update (0x429070) inside update(): runs first every update.
typedef void(__thiscall* CalcMultFn)(void* app);
static CalcMultFn g_origCalcMult;

static void __fastcall CalcMultHook(uint8_t* app, void*)
{
    g_origCalcMult(app);
    double mult = *(double*)(app + 0x2950);
    g_multF = (float)mult;
    g_halfMultF = 0.5f * g_multF;
    g_multSinceDraw += mult;
    TrackSpin(app);
}

void Camera_OnDraw()
{
    g_drawMult = g_multSinceDraw;
    g_multSinceDraw = 0;
}

// Point an instruction's absolute memory operand (4 bytes after a 4-byte opcode) somewhere else.
static bool RepointOperand(uintptr_t va, const char* opcode, uintptr_t expectVa, const void* target)
{
    if (!Expect(va, opcode) || *(uint32_t*)VA(va + 4) != (uint32_t)VA(expectVa)) {
        Log("camera: operand at %08X unexpected", (unsigned)va);
        return false;
    }
    uint32_t p = (uint32_t)(uintptr_t)target;
    WriteMem(va + 4, &p, 4);
    return true;
}

static int Diffuse(double* r, double target)
{
    double x = target + *r;
    double f = floor(x);
    *r = x - f;
    return (int)f;
}

enum { ZOOM_OUT, ZOOM_IN };

// app+400 is the centre hexagon radius: 300 -> 40 at the start of a run, 40 -> 320 on death,
// vanilla 20 units per 60 Hz frame. eax holds the current value, edi = app.
static uintptr_t __cdecl ZoomHandler(Ctx* c, int site)
{
    double step = 20.0 * *(double*)((uint8_t*)c->edi + 0x2950);
    c->eax = Diffuse(&g_rZoom, (int)c->eax + (site == ZOOM_OUT ? step : -step));
    return 0;
}

bool Camera_Init()
{
    bool ok = true;
    g_origCalcMult = (CalcMultFn)RedirectCall(0x43108B, 0x429070, (void*)CalcMultHook);
    ok &= g_origCalcMult != nullptr;
    // fast spin: mulss xmmN, [0.5f]
    ok &= RepointOperand(0x41B149, "F3 0F 59 05", 0x4F1664, &g_halfMultF);
    ok &= RepointOperand(0x41B20E, "F3 0F 59 05", 0x4F1664, &g_halfMultF);
    ok &= RepointOperand(0x41B25C, "F3 0F 59 0D", 0x4F1664, &g_halfMultF);
    ok &= RepointOperand(0x41B31E, "F3 0F 59 0D", 0x4F1664, &g_halfMultF);
    // tilt sweep: subss/addss xmmN, [1.0f]
    ok &= RepointOperand(0x41AD4C, "F3 0F 5C 0D", 0x4F1668, &g_multF);
    ok &= RepointOperand(0x41ADF5, "F3 0F 58 05", 0x4F1668, &g_multF);
    // rank banner timer in the draw pass: subsd xmm0,[esi+0x2940] -> subsd xmm0,[g_drawMult]
    if (Expect(0x40734E, "F2 0F 5C 86 40 29 00 00")) {
        uint8_t code[8] = { 0xF2, 0x0F, 0x5C, 0x05 };
        *(uint32_t*)(code + 4) = (uint32_t)(uintptr_t)&g_drawMult;
        WriteMem(0x40734E, code, sizeof(code));
    } else ok = false;
    // centre hexagon zoom: add eax, +/-20 (followed by position-independent instructions)
    ok &= Callout(0x42958F, 13, 3, ZoomHandler, ZOOM_OUT, "83 C0 14 C7 87 70 29 00 00 00 00 20 C2");
    ok &= Callout(0x42998C, 9, 3, ZoomHandler, ZOOM_IN, "83 C0 EC 89 87 90 01 00 00");
    Log("camera: %s", ok ? "ok" : "PARTIAL");
    return ok;
}
