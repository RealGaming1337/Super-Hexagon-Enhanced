# Super Hexagon Enhanced: technical notes

HexFPS is the internal name of the Super Hexagon Enhanced DLL.

Every address and structure offset HexFPS relies on, why it is patched, and how the result was
checked. Use this to audit the source or to port the mod to another build.

Addresses are static virtual addresses with image base `0x400000`. The exe has `DYNAMIC_BASE` set,
so the mod rebases every address at runtime with `VA(x)` ([src/hexfps.h](../src/hexfps.h)).
"app" is the game object (an openFrameworks-style `ofBaseApp`); "ui" is `app + 16`.

## Target

| | |
|---|---|
| Game | Super Hexagon, Steam app 221640, 2022 PC update |
| Executable | `SuperHexagon.exe`, 1,467,904 bytes, PE timestamp `0x62953532`, 32-bit MSVC |
| Engine | C++ with FNA3D (static), FAudio, `SDL2.dll` and Steamworks (`steam_api.dll`) |
| Anti-cheat | None; the only online feature is Steam leaderboards, which HexFPS does not touch |
| Loader | `SDL2.dll` imports `VERSION.dll` (`GetFileVersionInfoA`, `GetFileVersionInfoSizeA`, `VerQueryValueA`), so HexFPS is a `version.dll` proxy |

## Main loop

The frame loop is a port of FNA's fixed-timestep `Game.Tick`. Its object is at the global pointer
`[0x55ED28]`; the tick function is `0x439910`.

| Offset | Meaning |
|---|---|
| `+0x90` | double, elapsed seconds per step |
| `+0x98` | int64 `TargetElapsedTime` in performance-counter ticks (`freq / 60`, set at `0x432658`) |
| `+0xA0` / `+0xA8` | previous counter value / accumulator |
| `+0xB0` / `+0xB8` | worst-case sleep (starts at `freq / 1000`) / clamp `freq / 250` |
| `+0xC0` | int64[128] previous sleep times, index at `+0x4C0` |
| `+0x4C8` | int64 performance-counter frequency |

The game object's vtable is at `0x4F1E60`: `[0]` destructor `0x42DB10`, `[1]` setup `0x430E70`,
`[2]` update `0x431000`, `[3]` draw `0x430C50`, `[4]` exit `0x430C70`, `[5]` windowResized
`0x4310E0`. Present is `0x44DBC0`; sleeping is an `SDL_Delay(1)` loop.

The update (`0x431000`) reads the frame time through `0x4387F0`:

| app offset | Meaning |
|---|---|
| `+0x40CC0` | double `dt` |
| `+0x40CC8` | int FPS setting (60 on PC) |
| `+0x40CD0` | double step `1 / fps`; `dt >= 2 * step` is clamped to `step` |
| `+0x40CE0` | double total game time |

`dt` is also published to the global `0x55E910` (getter `0x431130`).

**HexFPS:** hooks vtable slot `0x4F1E68` (update) and before every update sets
`TargetElapsedTime = freq / fps`, the FPS setting to `fps` and the step to `1 / fps`
([src/hexfps.cpp](../src/hexfps.cpp), `ApplyFps`).

## Speed multiplier

`0x429070` computes the per-frame multiplier at `app+0x2950` (double), also stored as a float at
`[0x55E900]`:

```
mult = round1(dt / (1/60))          round1 at 0x431140: (int)(x * 10 + 0.5) / 10
```

Rounding to 0.1 is exact at 120 FPS (0.5) but not at other rates:

| FPS | Exact | Rounded | Error |
|---|---|---|---|
| 144 | 0.417 | 0.4 | 4% slow |
| 240 | 0.25 | 0.3 | 20% fast |
| 500 | 0.12 | 0.1 | 17% slow |

The main consumers are `0x424B70` (22 references), `0x429230` (34 references) and `0x424A80`.

**HexFPS:** replaces `round1` with the identity function (`fld dword [esp+4]; ret`).

## 60 Hz logic tick

Also in `0x429070`, from `0x4291D2`:

```
tickAccum(+0x2930) += dt
if (tickAccum >= 1/60 || fpsSetting == 60) {
    ++tickCount(+0x2938); tickAccum = 0; tickFlag(+0x293C) = 1;     // code at 0x429205
}
```

Resetting to 0 instead of subtracting `1/60` keeps the rate exact only when the FPS is a multiple of
60; at 500 FPS it ticks at 55.6 Hz.

**HexFPS:** `tickAccum -= 1/60`, resynchronising if more than a whole tick behind (`TickStub`).

## Integer state

Ten places update an int as `value = (int)(value ± K * mult)`. At 60 FPS `mult == 1`; at 500 FPS
the step drops below one unit and truncation loses it. The player turn speed of 7°/frame becomes
0.84 per update, which truncates to 0, so the player cannot turn.

| Site | Address | Value | Step |
|---|---|---|---|
| Menu rotation + / − | `0x424D29` / `0x424DC1` | level-select hexagon angle | 6 per 60 Hz frame |
| Player turn left / right | `0x4250EB` / `0x425141` | player angle `app+0x297C` | turn speed (int) `app+0x2984` |
| Lane snap | `0x4251D8` / `0x425218` | player angle | |
| Walls, game-over fly-out | `0x429D47` / `0x429D85` | wall distance / length | `5 * [app+0x2970]` |
| Walls approaching | `0x429E0F` / `0x429E9E` | wall distance / length | wall speed `[app+0x2968]` |

Walls are an array at `app+0x210`, 20 bytes each (type, distance, length, ?, active); the count is
at `app+0x2920`. Truncating a positive result means vanilla walls move `ceil(K)` units per frame.

**HexFPS:** each `cvttsd2si` is detoured through a callout to `Dispatch`
([src/smooth.cpp](../src/smooth.cpp)), which carries the fractional remainder between updates (error
diffusion). Walls advance `ceil(K) * mult` per update, with one remainder per wall slot.

## Camera effects

The camera update is `0x41AA60` (`this = ui`), with the angle at `ui+0x190`.

| Effect | Where | Bug | HexFPS |
|---|---|---|---|
| Fast spin | state `ui+0x5490`, cases 2/4/5/7 at `0x41B149`, `0x41B20E`, `0x41B25C`, `0x41B31E` | `angle ±= v * 0.5f` (`0.5f` at `0x4F1664`) per update, not scaled (cases 3/6 correctly use `7 * mult`) | operand repointed to a float holding `0.5 * mult` |
| Tilt sweep | state `ui+0x5484`, cases 2/23 at `0x41AD4C`, `0x41ADF5` | `±1.0f` (`0x4F1668`) per update | operand repointed to a float holding `mult` |
| Centre hexagon zoom | `app+400` (int) at `0x42958F` / `0x42998C` | `±20` per update | error-diffused `20 * mult` |
| Rank-up banner | timer decremented at `0x40734E` | `-= mult` per *draw*, wrong when draws ≠ updates (VSync) | uses the multiplier summed since the last draw |

The multiplier update is called from `0x43108B`; HexFPS redirects that call to refresh its floats
right after the game computes `mult` ([src/camera.cpp](../src/camera.cpp)).

Menu state −2 rewrites the camera angle every update at `0x42CCDB` (`(540 - sel) % 360`). This only
matters for the `Debug=2` self-test, not for gameplay.

## OPTIONS menu

The menu screen (state 4) is drawn by `0x407B50` from `0x409BDE`. Useful routines:

| Address | Routine |
|---|---|
| `0x402720` | `std::string` constructor |
| `0x422C90` | drawText(x, y, string by value, r, g, b, center), `ret 0x30` |
| `0x422EE0` | drawTextRight, `ret 0x2C` |
| `0x422930` | measureText(string, 1), `ret 0x1C` |
| `0x40F8D0` | slanted tab: `thiscall(ui)(renderer, x1, y1, x2, y2, slant)` |
| `0x42F470` | play sound effect (`app+0x22BB8`) |
| `0x4388A0` / `0x4387C0` | window width / height |
| `0x402780` / `0x419930` | load / save `settings.dat` |

Vanilla option strings: CHANGE TO WINDOW/FULLSCREEN `0x409C7B`, DISABLE/ENABLE VSYNC `0x409CC3`,
MUSIC VOLUME `0x409D19`, SOUND VOLUME `0x409D9C`. VSync is `ui+0x10188`; the selected entry is
`app+0x101AC`.

**HexFPS adds entries 6–8** ([src/ui.cpp](../src/ui.cpp)):

| Hook | Address | Behaviour |
|---|---|---|
| Up/down | `0x425FE2` → `0x426064` | wraps over 9 entries instead of 6 |
| Confirm | `0x4261CE` | entry 5 → `0x4261D3` (vanilla); 6–8 → `0x4260DB` (sound, settings save, debounce); else `0x426208` |
| Draw | `0x409E65` | draws the three entries and their descriptions; continues at `0x409E72` if entry 4 is selected, else `0x40E693` |

To fit nine rows, the layout is tightened:

| Change | Addresses |
|---|---|
| First row y 180 → 146 | `0x409C4D`, `0x409C8C`, `0x409C9A` |
| Row step 40 → 32 | `0x409CE3`, `0x409D66`, `0x409DE9`, `0x409E2F` |
| Title y 90 → 80 | `0x409C43` |
| Selection box 177 + 40·i → 143 + 32·i | `0x406396`, `0x4063AC`, `0x40639D` (`lea`/`shl` → `imul ecx, 32`) |

## FPS counter and watermark

The UI shape and text passes are called from `0x42FFC8` (shapes, `0x405DD0`) and `0x42FFDD`
(texts, `0x407B50`). HexFPS redirects both calls and draws after the originals, so its tabs sit on
top in the same style.

The renderer is `r = app+0x101B8`: `r[0]` width, `r[1]` height, `r[27]` tab height, `r[76]`/`r[77]`
tab text x/y, `r[81]`, `r[1147]`, `r[1152]`, `r[1157]` and `r[2129]` paddings. `app+0x54B4 == -1`
means a menu is showing; the watermark is only drawn then.

## Widescreen

PC uses layout mode 0: a 768×480 canvas, pillarboxed on anything wider than 16:10. The mobile
layout mode 10 sizes the canvas to the screen. `setPlatform` (`0x4013A0`, called from `0x430D67`
and `0x430C9B`) fills a config block; HexFPS runs it, then sets `cfg[121] = 10`,
`cfg[122] = ceil(480 * aspect)` and `cfg[123] = 480`. Toggling it relayouts through the game's own
windowResized (`0x4310E0`).

## Verification

`Debug=1` writes a per-second report to `HexFPS.log` ([src/stats.cpp](../src/stats.cpp)): updates,
draws, game time, 60 Hz ticks, the summed multiplier, player turning, wall speed and camera-spin
duration. A correct build shows `gameTime/s = 1.000`, `ticks/s = 60` and `mult*updates/s = 60` at
every frame rate. `Debug=2` also starts a camera spin every 4 seconds so the spin can be measured
without playing.

Results at 500 FPS, foreground and fullscreen:

- 500 updates/s and about 500 draws/s, game time 1.0000 s per second, 60 ticks/s
- player turning 420 °/s (vanilla: turn speed 7 × 60 = 420)
- walls 1980 units/s (vanilla: wall speed 33 × 60 = 1980)
- camera spin 0.920 s / 292° at 500 FPS and 0.929 s / 293° at 240 FPS
  (vanilla: 55 frames = 0.917 s, 292°)

A window in the background is throttled by Windows (120–240 draws/s here); in the foreground it
reaches the full rate.
