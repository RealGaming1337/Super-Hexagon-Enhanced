// SPDX-License-Identifier: MIT
// Integer-state fix. Ten places in the game integrate an int with a frame-scaled step and
// truncate: value = (int)(value +/- K * mult). At 60 fps mult == 1 so the step is a whole
// number of units; at 240..500 fps the step drops below one unit and truncation eats it
// (player stops turning, walls crawl at 1 unit/frame). We replace each truncation with
// error diffusion: keep the fractional remainder so every 1/60 s window advances by exactly
// the vanilla amount.
//
// Each site's cvttsd2si is detoured through a Callout (patch.cpp); Dispatch reads its inputs
// from the saved registers and writes the result into the saved eax/ebx.
#include "hexfps.h"
#include <math.h>
#include <stdlib.h>

enum Site {
    MENU_ADD, MENU_SUB,      // level-select hexagon rotation, 6 deg per 60 Hz frame
    PLAYER_L, PLAYER_R,      // player turn: angle +/- turnSpeed(int) * mult
    SNAP_A, SNAP_B,          // player lane snap, same variable
    WFLY_DIST, WFLY_LEN,     // walls during the game-over fly-out: K = 5 * v
    WALL_DIST, WALL_LEN,     // walls approaching the centre: K = wallSpeed
    SITE_COUNT
};

static double g_rMenu, g_rPlayer;
enum { MAX_WALLS = 512 };
static double g_rWall[2][MAX_WALLS];
static int g_lastWall[2][MAX_WALLS];

SmoothStats g_smooth;

static int Diffuse(double* r, double target)
{
    double x = target + *r;
    double f = floor(x);
    *r = x - f;
    return (int)f;
}

// Vanilla: (int)(base - K) with mult == 1 moves ceil(K) units per 60 Hz frame (truncation of a
// positive result). Reproduce that rate exactly: ceil(K) * mult per frame, remainder carried.
static int WallStep(int field, uint32_t slot, int base, double K, double mult)
{
    if (slot >= MAX_WALLS) return (int)(base - K * mult);
    double& r = g_rWall[field][slot];
    if (g_lastWall[field][slot] != base) r = 0;  // a new wall took this slot
    double x = base + r - ceil(K) * mult;
    double f = floor(x);
    r = x - f;
    int v = (int)f;
    g_lastWall[field][slot] = v;
    return v;
}

static uintptr_t __cdecl Dispatch(Ctx* c, int site)
{
    uint8_t* app = (uint8_t*)c->edi;
    switch (site) {
    case MENU_ADD:
    case MENU_SUB:
        c->eax = Diffuse(&g_rMenu, c->xmm[1][0]);
        break;
    case PLAYER_L:
    case PLAYER_R: {
        int before = *(int*)(app + 0x297C);
        c->eax = Diffuse(&g_rPlayer, c->xmm[1][0]);
        g_smooth.playerMoved += abs((int)c->eax - before);
        g_smooth.playerFrames++;
        g_smooth.playerSpeed = *(int*)(app + 0x2984);
        break;
    }
    case SNAP_A:
    case SNAP_B:
        c->ebx = Diffuse(&g_rPlayer, c->xmm[1][0]);
        break;
    case WFLY_DIST:
    case WFLY_LEN: {
        int field = site == WFLY_LEN;
        int base = *(int*)(c->ecx + 4 * field);
        double K = 5.0 * *(float*)(app + 0x2970);
        c->eax = WallStep(field, c->edx, base, K, *(double*)(app + 0x2950));
        break;
    }
    case WALL_DIST:
    case WALL_LEN: {
        int field = site == WALL_LEN;
        int base = *(int*)(c->ecx + 4 * field);
        double K = *(float*)(app + 0x2968);
        int v = WallStep(field, c->edx, base, K, *(double*)(app + 0x2950));
        c->eax = v;
        if (!field && v > 0) {
            g_smooth.wallMoved += base - v;
            g_smooth.wallFrames++;
            g_smooth.wallK = K;
        }
        break;
    }
    }
    return 0;
}

bool Smooth_Init()
{
    struct { int site; uintptr_t va; size_t len; const char* bytes; } sites[] = {
        { MENU_ADD,  0x424D29, 8, "F2 0F 2C C1 F2 0F 58 C3" },
        { MENU_SUB,  0x424DC1, 8, "F2 0F 2C C1 F2 0F 5C C3" },
        { PLAYER_L,  0x4250EB, 8, "F2 0F 2C C1 F2 0F 5C C2" },
        { PLAYER_R,  0x425141, 8, "F2 0F 2C C1 F2 0F 58 C2" },
        { SNAP_A,    0x4251D8, 6, "F2 0F 2C D9 8B C3" },
        { SNAP_B,    0x425218, 6, "F2 0F 2C D9 8B C3" },
        { WFLY_DIST, 0x429D47, 6, "F2 0F 2C C2 89 01" },
        { WFLY_LEN,  0x429D85, 7, "F2 0F 2C C2 89 41 04" },
        { WALL_DIST, 0x429E0F, 6, "F2 0F 2C C1 89 01" },
        { WALL_LEN,  0x429E9E, 7, "F2 0F 2C C1 89 41 04" },
    };
    int ok = 0;
    for (auto& s : sites) ok += Callout(s.va, s.len, 4, Dispatch, s.site, s.bytes);
    Log("smooth: %d/%d integer-step sites patched", ok, (int)SITE_COUNT);
    return ok == SITE_COUNT;
}
