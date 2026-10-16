# Super Hexagon Enhanced

**240–500 FPS for Super Hexagon.** Super Hexagon Enhanced (internal name: HexFPS) unlocks the Steam version of Super Hexagon from its fixed 60 FPS to anything between 240 and
500 FPS, **with the game speed unchanged**. It also adds a widescreen mode without black bars, an
FPS counter, and settings for all of it inside the game's own OPTIONS menu.

Made by **RealGaming1337**. Open source (MIT). No game files are modified, and the repository contains
no game code or assets.

## Features

- **240–500 FPS**, selectable in OPTIONS (240 / 280 / 300 / 360 / 400 / 480 / 500) or any value
  in `HexFPS.ini`
- **Same speed as vanilla.** Player turning, wall speed, camera spins and the 60 Hz logic tick are
  measured identical to 60 FPS (see [Verified](#verified))
- **Widescreen:** fills 16:9, 21:9 and wider screens instead of pillarboxing to 16:10
- **FPS counter** in the bottom-left corner, in the game's own tab style
- **No installer:** one DLL next to the game; delete it to uninstall

## Install

1. Download `Super-Hexagon-Enhanced.zip` from
   [Releases](https://github.com/RealGaming1337/Super-Hexagon-Enhanced/releases) and extract
   `version.dll`.
2. In Steam, right-click **Super Hexagon → Manage → Browse local files**, and put `version.dll`
   next to `SuperHexagon.exe`.
3. Start the game. In **OPTIONS**, set **DISABLE VSYNC**. With VSync on, the frame rate is capped
   at your monitor's refresh rate.

**Uninstall:** delete `version.dll` (and, if you like, `HexFPS.ini` and `HexFPS.log`).

## Settings

Three entries are added to the bottom of the game's OPTIONS menu:

| Entry | What it does |
|---|---|
| `FPS LIMIT: 500` | Confirm to cycle 240 / 280 / 300 / 360 / 400 / 480 / 500 |
| `SHOW / HIDE FPS COUNTER` | FPS counter tab in the bottom-left corner |
| `ENABLE / DISABLE WIDESCREEN` | Fill wide screens instead of pillarboxing to 16:10 |

They are stored in `HexFPS.ini` next to the game:

```ini
[HexFPS]
FPS=500          ; 240..500, any value
ShowFPS=1
Widescreen=1
Debug=0          ; 1 = write a per-second timing report to HexFPS.log
```

## Compatibility

- Super Hexagon on **Steam for Windows**, current build (the 2022 PC update,
  `SuperHexagon.exe` PE timestamp `0x62953532`).
- On any other build, HexFPS checks the code it would change, finds it different, and leaves the
  game alone (`HexFPS.log` then says `unrecognised SuperHexagon.exe`).

## What the DLL does (and doesn't do)

HexFPS is meant to be easy to audit. All of it is in [`src/`](src), about 1,000 lines of commented
C++. [docs/TECHNICAL_NOTES.md](docs/TECHNICAL_NOTES.md) lists every address it touches and why.

- **Why `version.dll`:** Windows looks for a program's DLLs in its own folder first. The game's
  `SDL2.dll` imports `version.dll`, so a file with that name next to the exe is loaded with the
  game. All 17 of its functions are forwarded to the real `version.dll` in the Windows folder
  ([src/proxy.cpp](src/proxy.cpp)). This is a common, installer-free modding technique.
- **Only inside Super Hexagon:** if the process isn't `SuperHexagon.exe`, it only forwards calls.
- **Memory-only changes, each one checked first:** every patch compares the original bytes at its
  address and is skipped on a mismatch ([src/patch.cpp](src/patch.cpp)). Nothing on disk is
  changed.
- **Files it writes:** `HexFPS.ini` (its settings) and `HexFPS.log` (a load report, plus the timing
  report when `Debug=1`), both next to the game. Changing a HexFPS option also runs the game's own
  settings save, just like changing a vanilla option.
- **No network, registry or process access.** The DLL imports only file, INI, timer and
  memory-protection functions from `KERNEL32`, `wsprintfA` from `USER32`, and the C runtime. You
  can check this with `dumpbin /imports version.dll`.
- **No save, score or Steam changes:** records, score submission and leaderboards are untouched.

### Release binaries are built from this source

The DLLs on the [Releases](https://github.com/RealGaming1337/Super-Hexagon-Enhanced/releases) page are compiled by
[GitHub Actions](.github/workflows/build.yml) on GitHub's servers from the tagged commit, not
uploaded by hand. Each release lists SHA-256 checksums and has a signed build-provenance
attestation, so you can check that a download came from this source:

```
gh attestation verify version.dll --repo RealGaming1337/Super-Hexagon-Enhanced
```

Or build it yourself (see [Building](#building)).

Some antivirus tools flag new, unsigned DLLs that change another program's memory, as any game mod
of this kind does. If yours does, the source is here to read, and you can build it yourself.

## Leaderboards and fairness

HexFPS doesn't touch score submission, and the game runs at vanilla speed. However, the game
updates up to 500 times a second instead of 60, so motion is smoother and turning responds in
finer steps. If you play for leaderboard records, follow the community's rules for modded runs.

## How it works

The 2022 port can already run faster than 60 Hz; that is how the mobile 120 Hz mode works. On PC
it is locked to 60. HexFPS drives the same code path at 240–500 Hz and fixes the spots that only
worked at 60 and 120:

- **Frame rate:** the FNA-style fixed-timestep loop runs at `freq / fps`, and the game's own FPS
  setting and step are set to match.
- **Speed multiplier:** the per-frame multiplier `dt × 60` was rounded to 0.1. That is exact at
  120 FPS (0.5) but wrong at 144 (0.4), 240 (0.3) and 500 (0.1). It is now exact.
- **60 Hz logic tick:** the accumulator reset to 0 instead of subtracting, so it drifted unless
  the FPS was a multiple of 60. It now subtracts.
- **Integer state:** the player angle, wall distances and menu rotation are integers updated as
  `(int)(value ± speed × mult)`. Below one unit per frame, truncation ate the movement, so the
  player could not turn. These updates now use error diffusion: the fractional remainder is
  carried, so every 1/60 s window advances by exactly the vanilla amount. Walls keep vanilla's
  effective `ceil(speed)` units per 60 Hz frame.
- **Camera effects:** the fast camera spin, the tilt sweep, the centre-hexagon zoom at the start
  and end of a run, and the rank-up banner timer stepped once per frame instead of per unit of
  game time, so they ran 4–8× too fast (the mobile 120 Hz build has the same bugs at 2×). They now
  advance by game time.
- **Widescreen:** uses the mobile build's layout mode, which sizes the canvas to the screen
  aspect ratio. The 3D projection depends only on height, so the playfield keeps its size and
  you simply see more to the sides.

### Verified

Measured with the built-in timing report (`Debug=1`) at 500 FPS:

| | HexFPS at 500 FPS | Vanilla 60 FPS |
|---|---|---|
| Game time per real second | 1.000 s | 1.000 s |
| 60 Hz logic ticks per second | 60 | 60 |
| Player turning | 420 °/s | 420 °/s |
| Wall speed | 1980 units/s | 1980 units/s |
| Camera fast spin | 0.92 s, 292° | 0.917 s, 292° |

## Building

Requires Visual Studio 2022 or newer with **Desktop development with C++** (the x86 MSVC tools;
the code uses 32-bit inline assembly). Run `build.bat`; the output is `build\version.dll`.

### Source tour

| File | Purpose |
|---|---|
| [src/proxy.cpp](src/proxy.cpp), [src/version.def](src/version.def) | The `version.dll` exports, forwarded to the real DLL |
| [src/hexfps.cpp](src/hexfps.cpp) | Entry point, settings, frame-rate control, multiplier and tick fixes |
| [src/patch.cpp](src/patch.cpp) | Patching helpers: byte checks, call/vtable redirects, register-saving trampolines |
| [src/smooth.cpp](src/smooth.cpp) | Error diffusion for the ten integer-state updates |
| [src/camera.cpp](src/camera.cpp) | Spin, tilt, zoom and rank-banner timing |
| [src/ui.cpp](src/ui.cpp) | OPTIONS entries, FPS counter, watermark, widescreen |
| [src/stats.cpp](src/stats.cpp) | The `Debug=1` timing report |

## Credits

- **Super Hexagon** by Terry Cavanagh, music by Chipzel. The 2022 PC update is by Ethan Lee.
  HexFPS is a fan project and is not affiliated with or endorsed by them.
- **AI disclosure:** the reverse engineering and the code were done by Claude (Anthropic's AI)
  working with RealGaming1337, who directed and play-tested it.

## License

[MIT](LICENSE) for the HexFPS source. Super Hexagon itself is (c) Terry Cavanagh; you need your own
copy of the game.
