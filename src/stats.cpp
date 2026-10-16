// SPDX-License-Identifier: MIT
// Debug oracle: with Debug=1 in HexFPS.ini, log once per real second how many updates and
// draws ran and how far the game's own clocks advanced. A correct unlock shows
// gameTime/s == 1.000, ticks/s == 60 and mult*updates/s == 60 at every frame rate.
#include "hexfps.h"
#include <math.h>

static bool g_enabled;
static LARGE_INTEGER g_freq, g_last;
static int g_updates, g_lastDraws;
static double g_lastGameTime, g_multSum;
static int g_lastTicks;

void Stats_Init()
{
    g_enabled = IniGetInt("Debug", 0) != 0;
    if (!g_enabled) return;
    QueryPerformanceFrequency(&g_freq);
}

void Stats_OnUpdate(uint8_t* app)
{
    if (!g_enabled) return;
    g_updates++;
    g_multSum += *(double*)(app + 0x2950);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (!g_last.QuadPart) {
        g_last = now;
        g_lastGameTime = *(double*)(app + 0x40CE0);
        g_lastTicks = *(int*)(app + 0x2938);
        return;
    }
    double real = (double)(now.QuadPart - g_last.QuadPart) / g_freq.QuadPart;
    if (real < 1.0) return;
    // Debug=2: self-test, start the game's own camera spin every 4 s (works on the menu screen,
    // which runs the same camera update) so the spin oracle below can be checked without input.
    static int s_selfTest = IniGetInt("Debug", 0);
    static int s_seconds;
    int* spinState = (int*)(app + 16 + 0x5490);
    if (s_selfTest == 2 && ++s_seconds % 4 == 0 && *spinState == 0) *spinState = 1;
    double gameTime = *(double*)(app + 0x40CE0);
    int ticks = *(int*)(app + 0x2938);
    Log("cap=%d  real=%.3fs  updates/s=%.1f  draws/s=%.1f  gameTime/s=%.4f  ticks/s=%.2f  "
        "mult=%.5f  mult*updates/s=%.3f",
        g_fps, real, g_updates / real, (g_drawCount - g_lastDraws) / real, (gameTime - g_lastGameTime) / real,
        (ticks - g_lastTicks) / real, *(double*)(app + 0x2950), g_multSum / real);
    // Vanilla 60 fps: player turns turnSpeed deg/frame, walls move ceil(wallSpeed) units/frame.
    double perFrameToSec = g_updates / real;
    if (g_smooth.playerFrames)
        Log("   player turning: %.1f deg/s (vanilla %d deg/s)",
            g_smooth.playerMoved / g_smooth.playerFrames * perFrameToSec, g_smooth.playerSpeed * 60);
    if (g_smooth.wallFrames)
        Log("   walls: %.1f units/s (vanilla %.0f units/s, wallSpeed %.3f)",
            g_smooth.wallMoved / g_smooth.wallFrames * perFrameToSec, ceil(g_smooth.wallK) * 60,
            g_smooth.wallK);
    static int s_spinsLogged;
    if (g_spin.done != s_spinsLogged) {
        s_spinsLogged = g_spin.done;
        Log("   camera spin: %.3f s, %.1f deg (vanilla 0.917 s, 292 deg)", g_spin.lastTime,
            g_spin.lastTotal);
    }
    g_smooth.playerMoved = g_smooth.wallMoved = 0;
    g_smooth.playerFrames = g_smooth.wallFrames = 0;
    g_last = now;
    g_lastGameTime = gameTime;
    g_lastTicks = ticks;
    g_updates = 0;
    g_lastDraws = g_drawCount;
    g_multSum = 0;
}
