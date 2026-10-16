// SPDX-License-Identifier: MIT
// In-game UI: three extra OPTIONS entries (FPS limit, FPS counter, widescreen), an FPS
// counter and a menu-only watermark, all drawn with the game's own text and tab routines so
// they share its font, colours and slanted-tab style.
#include "hexfps.h"
#include <stdio.h>
#include <math.h>

// ---- game routines ------------------------------------------------------------------------
// MSVC std::string (24 bytes). Always built by the game's own constructor and passed by value,
// so the game allocates and frees it on its own heap.
struct GStr { uint32_t d[6]; };
typedef void(__thiscall* StrCtorFn)(GStr* self, const char* s);                                         // 0x402720
typedef void(__thiscall* DrawTextFn)(void* r, int x, int y, GStr s, int cr, int cg, int cb, int center);  // 0x422C90
typedef void(__thiscall* DrawTextRightFn)(void* r, int x, int y, GStr s, int cr, int cg, int cb);         // 0x422EE0
typedef int(__thiscall* MeasureFn)(void* r, GStr s, int flag);                                           // 0x422930
typedef void(__thiscall* TabFn)(void* ui, void* r, int x1, int y1, int x2, int y2, int slant);          // 0x40F8D0
typedef void(__thiscall* SfxFn)(void* sfx, int id);                                                      // 0x42F470
typedef void(__thiscall* ResizeFn)(void* app, int w, int h);                                             // 0x4310E0
typedef int(__cdecl* IntFn)();                                                                           // 0x4388A0/0x4387C0

static GStr Str(const char* s)
{
    GStr g;
    ((StrCtorFn)VA(0x402720))(&g, s);
    return g;
}
static void Text(void* r, int x, int y, const char* s, int c, bool center)
{
    ((DrawTextFn)VA(0x422C90))(r, x, y, Str(s), c, c, c, center);
}
static void TextRight(void* r, int x, int y, const char* s)
{
    ((DrawTextRightFn)VA(0x422EE0))(r, x, y, Str(s), 255, 255, 255);
}
static int Measure(void* r, const char* s) { return ((MeasureFn)VA(0x422930))(r, Str(s), 1); }
static void Tab(void* ui, void* r, int x1, int y1, int x2, int y2, int slant)
{
    ((TabFn)VA(0x40F8D0))(ui, r, x1, y1, x2, y2, slant);
}
static void Sfx(uint8_t* app, int id) { ((SfxFn)VA(0x42F470))(app + 0x22BB8, id); }
static int WindowW() { return ((IntFn)VA(0x4388A0))(); }
static int WindowH() { return ((IntFn)VA(0x4387C0))(); }

// ---- settings -----------------------------------------------------------------------------
static bool g_showFps = true;
static bool g_wide = true;
static bool g_relayout;
static const int kFpsPresets[] = { 240, 280, 300, 360, 400, 480, 500 };

static void CycleFps()
{
    int next = kFpsPresets[0];
    for (int p : kFpsPresets)
        if (p > g_fps) { next = p; break; }
    SetFps(next, true);
}

// ---- FPS measurement ----------------------------------------------------------------------
int g_drawCount;
static int g_fpsShown;
static int g_fpsFrames;
static LARGE_INTEGER g_fpsT0, g_qpf;

typedef void(__thiscall* DrawFn)(void*);
static DrawFn g_origDraw;

static void __fastcall HookedDraw(void* app, void*)
{
    g_drawCount++;
    Camera_OnDraw();
    g_fpsFrames++;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double dt = (double)(now.QuadPart - g_fpsT0.QuadPart) / g_qpf.QuadPart;
    if (dt >= 0.5) {
        g_fpsShown = (int)(g_fpsFrames / dt + 0.5);
        g_fpsFrames = 0;
        g_fpsT0 = now;
    }
    g_origDraw(app);
}

// ---- OPTIONS menu -------------------------------------------------------------------------
// Vanilla has 6 entries 40 px apart starting at y=180; nine entries need a tighter column.
enum { OPT_FPS = 6, OPT_SHOWFPS = 7, OPT_WIDE = 8, OPT_COUNT = 9 };
enum { MENU_Y0 = 146, MENU_STEP = 32, MENU_TITLE_Y = 80, MENU_DESC_Y = 440 };

// 0x425FE2: up/down handling for the options screen (was "% 6"). edi = app, bl = first index.
static uintptr_t __cdecl NavHandler(Ctx* c, int)
{
    uint8_t* app = (uint8_t*)c->edi;
    int first = (uint8_t)c->ebx;
    int& sel = *(int*)(app + 0x101AC);
    if (app[0x3B] || app[0x39]) {
        sel = (sel + OPT_COUNT - 1) % OPT_COUNT;
        if (sel < first) sel = OPT_COUNT - 1;
        *(float*)(app + 0x120) = 8.0f;  // key-repeat delay, as vanilla
        Sfx(app, 12);
    }
    if (app[0x3C] || app[0x3A]) {
        sel = (sel + 1) % OPT_COUNT;
        if (sel < first) sel = first;
        *(float*)(app + 0x120) = 8.0f;
        Sfx(app, 12);
    }
    return VA(0x426064);
}

// 0x4261CE: "cmp eax,5 / jne" at the end of the confirm switch. eax = selection, edi = app.
static uintptr_t __cdecl ConfirmHandler(Ctx* c, int)
{
    switch (c->eax) {
    case 5:
        return VA(0x4261D3);  // DELETE RECORDS, untouched
    case OPT_FPS:
        CycleFps();
        return VA(0x4260DB);  // shared tail: confirm sound, save settings, debounce
    case OPT_SHOWFPS:
        g_showFps = !g_showFps;
        IniSetInt("ShowFPS", g_showFps);
        return VA(0x4260DB);
    case OPT_WIDE:
        g_wide = !g_wide;
        IniSetInt("Widescreen", g_wide);
        g_relayout = true;
        return VA(0x4260DB);
    }
    return VA(0x426208);
}

// 0x409E65: right after DELETE RECORDS is drawn. ebx = app+16, edi = renderer, esi = pulse src.
static uintptr_t __cdecl OptionsDrawHandler(Ctx* c, int)
{
    uint8_t* ui = (uint8_t*)c->ebx;
    void* r = (void*)c->edi;
    int y = *(int*)(c->ebp - 0x46C);
    char buf[48];
    sprintf_s(buf, "FPS LIMIT: %d", g_fps);
    Text(r, 0, y + MENU_STEP, buf, 255, true);
    Text(r, 0, y + 2 * MENU_STEP, g_showFps ? "HIDE FPS COUNTER" : "SHOW FPS COUNTER", 255, true);
    Text(r, 0, y + 3 * MENU_STEP, g_wide ? "DISABLE WIDESCREEN" : "ENABLE WIDESCREEN", 255, true);

    int sel = *(int*)(ui + 0x1019C);
    bool vsync = *(int*)(ui + 0x10188) == 1;
    const char* desc = nullptr;
    if (sel == OPT_FPS)
        desc = vsync ? "VSYNC IS ON - DISABLE IT TO UNLOCK THE FPS" : "240 TO 500 FPS, SAME GAME SPEED";
    else if (sel == OPT_SHOWFPS)
        desc = "FRAME RATE IN THE BOTTOM LEFT CORNER";
    else if (sel == OPT_WIDE)
        desc = "FILLS WIDE SCREENS, NO BLACK BARS";
    if (desc) {
        int pulse = (int)(*(float*)(c->esi + 0x30) + 128.0f);  // same fade as the arcade note
        Text(r, 0, MENU_DESC_Y, desc, pulse, true);
    }
    return sel == 4 ? VA(0x409E72) : VA(0x40E693);
}

static bool PatchImm32(uintptr_t va, int off, const char* expect, int32_t value)
{
    if (!Expect(va, expect)) return false;
    WriteMem(va + off, &value, 4);
    return true;
}

static bool PatchMenuLayout()
{
    bool ok = true;
    const char* movY = "C7 85 94 FB FF FF";
    ok &= PatchImm32(0x409C4D, 6, movY, MENU_Y0);               // first row when fullscreen is hidden
    ok &= PatchImm32(0x409C8C, 1, "68 B4 00 00 00", MENU_Y0);   // fullscreen row
    ok &= PatchImm32(0x409C9A, 6, movY, MENU_Y0 + MENU_STEP);   // row after it
    static const uintptr_t kStepAdds[] = { 0x409CE3, 0x409D66, 0x409DE9, 0x409E2F };
    for (uintptr_t va : kStepAdds) {
        if (!Expect(va, "83 85 94 FB FF FF 28")) { ok = false; continue; }
        uint8_t step = MENU_STEP;
        WriteMem(va + 6, &step, 1);
    }
    if (Expect(0x409C42, "6A 5A")) {                            // "OPTIONS" title
        uint8_t ty = MENU_TITLE_Y;
        WriteMem(0x409C43, &ty, 1);
    } else ok = false;
    // selection box: y = 177 + 40 * index  ->  MENU_Y0 - 3 + MENU_STEP * index
    ok &= PatchImm32(0x406396, 1, "05 B1 00 00 00", MENU_Y0 - 3);
    ok &= PatchImm32(0x4063AC, 2, "8D 81 B1 00 00 00", MENU_Y0 - 3);
    if (Expect(0x40639D, "8D 0C 89 C1 E1 03")) {
        const uint8_t imul[] = { 0x6B, 0xC9, MENU_STEP, 0x90, 0x90, 0x90 };  // imul ecx, ecx, step
        WriteMem(0x40639D, imul, sizeof(imul));
    } else ok = false;
    return ok;
}

// ---- FPS counter + watermark ----------------------------------------------------------------
// Corner tabs in the game's own slanted style: watermark top-left (menus only), FPS counter
// bottom-left (the vertical mirror of the top tabs).
typedef void(__fastcall* PassFn)(uint8_t* ui, void* edx, int* r, void* a3);
static PassFn g_origShapes, g_origTexts;
static const char kWatermark[] = "MADE BY REALGAMING1337";

static void Overlay(uint8_t* ui, int* r, bool textPass)
{
    uint8_t* app = ui - 16;
    bool inMenu = *(int*)(app + 0x54B4) == -1;
    int W = r[0], H = r[1], tabH = r[27], textY = H - tabH + r[77];
    if (g_showFps) {
        char buf[32];
        sprintf_s(buf, "FPS: %d", g_fpsShown);
        if (textPass)
            Text(r, r[76] + r[2129], textY, buf, 255, false);
        else
            Tab(ui, r, -r[1157], H - tabH, Measure(r, "FPS: 000") + r[81] + r[1152] + r[2129], H + 5, 1);
    }
    if (inMenu) {  // top-left, same geometry as the game's own top-left tab
        if (textPass)
            Text(r, r[76] + r[2129], r[77], kWatermark, 255, false);
        else
            Tab(ui, r, -r[1157], -5, Measure(r, kWatermark) + r[81] + r[1152] + r[2129], tabH, -1);
    }
}

static void __fastcall ShapesHook(uint8_t* ui, void* edx, int* r, void* a3)
{
    g_origShapes(ui, edx, r, a3);
    Overlay(ui, r, false);
}

static void __fastcall TextsHook(uint8_t* ui, void* edx, int* r, void* a3)
{
    g_origTexts(ui, edx, r, a3);
    Overlay(ui, r, true);
}

// ---- widescreen ---------------------------------------------------------------------------
// PC uses layout mode 0: a fixed 768x480 canvas, pillarboxed on anything wider than 16:10.
// The mobile build's mode 10 sizes the canvas to the screen (height 480, width by aspect) and
// scales UI positions to fit; the 3D projection only depends on the height, so the playfield
// keeps its size and simply shows more to the sides.
typedef void(__thiscall* SetPlatformFn)(int32_t* cfg, int platform, int mode);
static SetPlatformFn g_origSetPlatform;

static void __fastcall SetPlatformHook(int32_t* cfg, void*, int platform, int mode)
{
    g_origSetPlatform(cfg, platform, mode);
    if (!g_wide) return;
    int w = WindowW(), h = WindowH();
    if (w <= 0 || h <= 0) return;
    double aspect = (double)w / h;
    if (aspect <= 1.6 + 1e-6) return;  // 16:10 or taller: vanilla already fills the width
    cfg[121] = 10;
    cfg[122] = (int)ceil(480.0 * aspect);
    cfg[123] = 480;
}

void Ui_BeforeUpdate(uint8_t* app)
{
    if (!g_relayout) return;
    g_relayout = false;
    ((ResizeFn)VA(0x4310E0))(app, WindowW(), WindowH());  // the game's own windowResized path
}

bool Ui_Init()
{
    g_showFps = IniGetInt("ShowFPS", 1) != 0;
    g_wide = IniGetInt("Widescreen", 1) != 0;
    QueryPerformanceFrequency(&g_qpf);
    QueryPerformanceCounter(&g_fpsT0);

    bool ok = true;
    g_origDraw = (DrawFn)HookVtable(0x4F1E6C, 0x430C50, (void*)HookedDraw);
    ok &= g_origDraw != nullptr;
    ok &= PatchMenuLayout();
    ok &= Callout(0x425FE2, 6, 6, NavHandler, 0, "80 7F 3B 00 75 0E");
    ok &= Callout(0x4261CE, 5, 5, ConfirmHandler, 0, "83 F8 05 75 35");
    ok &= Callout(0x409E65, 13, 13, OptionsDrawHandler, 0, "83 BB 9C 01 01 00 04 0F 85 21 48 00 00");
    g_origShapes = (PassFn)RedirectCall(0x42FFC8, 0x405DD0, (void*)ShapesHook);
    g_origTexts = (PassFn)RedirectCall(0x42FFDD, 0x407B50, (void*)TextsHook);
    ok &= g_origShapes && g_origTexts;
    g_origSetPlatform = (SetPlatformFn)RedirectCall(0x430D67, 0x4013A0, (void*)SetPlatformHook);
    ok &= g_origSetPlatform && RedirectCall(0x430C9B, 0x4013A0, (void*)SetPlatformHook);
    Log("ui: %s (showFps=%d widescreen=%d)", ok ? "ok" : "PARTIAL", g_showFps, g_wide);
    return ok;
}
