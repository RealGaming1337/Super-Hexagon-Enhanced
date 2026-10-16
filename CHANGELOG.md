## v1.0.0

First public release.

- 240–500 FPS, selectable in OPTIONS (240 / 280 / 300 / 360 / 400 / 480 / 500) or `HexFPS.ini`
- Game speed identical to vanilla 60 FPS: exact speed multiplier, exact 60 Hz logic tick, error
  diffusion for player turning, walls and menu rotation, and per-game-time camera spin, tilt,
  zoom and rank-banner timing
- Widescreen mode without black bars
- FPS counter
- `Debug=1` timing report in `HexFPS.log`

Install: put `version.dll` next to `SuperHexagon.exe` and disable VSync in OPTIONS.
Checksums are in `SHA256SUMS.txt`; verify the build with
`gh attestation verify version.dll --repo RealGaming1337/Super-Hexagon-Enhanced`.
