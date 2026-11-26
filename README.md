# Yo-kai Watch 2: Psychic Specters Recompiled — V1.0.0

Experimental native Windows and Linux x86-64 port of the European Nintendo 3DS game. The builder uses your own decrypted European cartridge (`CTR-P-BYSP`, title ID `00040000001B2900`) to generate a native executable locally. It does not distribute the game or require an external emulator.

## Quick start

1. Download the matching Windows RAR or Linux tar.gz from [Releases](https://github.com/Okxmir/yo-kai-watch-2-recompiled/releases). Extract the entire archive, keeping its folders intact.
2. On Windows, open `Launcher.exe`. On Linux, run `./YoKaiWatch2Builder` (or `bash YoKaiWatch2Builder`); see [Linux prerequisites](BUILD_INSTRUCTIONS.md). Choose your compatible decrypted European cartridge and select **Build**. The Windows builder downloads pinned portable tools; the Linux builder also provisions Rust and SDL3 locally when needed.
3. When the build completes, click **Play**. The launcher remembers the local installation and its original cartridge. Do not move/delete the cartridge while using the game.
4. Use **Settings** to select the game language, resolution, Original/30/60 FPS presentation, aspect ratio, dual-screen layout and controls.

### In-game options

In windowed mode, the game has **Upscaling**, **Aspect ratio**, **Display** and **FPS** menus. Change these settings during play without rebuilding. Press F11 to enter or leave fullscreen. The original game textures are used; the independent text readability correction remains enabled.

The 60 FPS option interpolates original rendered frames; it does not force the ARM simulation to run twice as fast. Full-game compatibility, every battle/minigame, later quests and additional hardware are still being validated.

### Automatic updates

The launchers check this repository's published stable releases, select the newest matching platform builder, verify its size and SHA-256 digest, replace the builder sources and restart. When a newer version is detected, the launcher requests a rebuild before Play is available again. Local ROMs, saves, settings and compiled game data remain on your PC. Keep the complete extracted builder folder.

For command-line builds, Linux prerequisites and save maintenance, see [BUILD_INSTRUCTIONS.md](BUILD_INSTRUCTIONS.md). See [CHANGELOG.md](CHANGELOG.md) for the release history.

This is an independent, unofficial project based on the MIT-licensed [Clockwork runtime](https://github.com/watchmakerers/clockwork), with the original copyright notices retained in `source/clockwork/LICENSE` and `launcher/LICENSE-REFERENCE.txt`. Yo-kai Watch and its characters belong to their respective rights holders. No game ROMs, extracted code/assets, compiled proprietary game executables or player saves are distributed.
