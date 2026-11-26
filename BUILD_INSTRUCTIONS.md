# Build and play

Place a decrypted European Psychic Specters cartridge (`CTR-P-BYSP`) you own in the project directory, or pass its full path. No 3DS firmware, external emulator or cryptographic keys are needed. The original ROM is read only.

Windows x64, PowerShell and built-in tar/curl are required. The portable script uses checksum-pinned LLVM, CMake, Ninja, Rust, SDL3 and memmap2; if missing it downloads them on the first build. No installed Visual Studio or Python is required. The reference's cached tools were copied independently for the initial local development.

From a terminal opened in the project directory:

```bat
build.cmd -Cartridge "YourRom.3ds"
```

`-ValidateOnly` validates the cartridge without compiling the game. The builder only accepts a matching decompressed executable fingerprint. The normal build creates `dist/YoKaiWatch2.exe` plus its DLLs and build manifest; both cached and clean dependency downloads have been tested.

Launch the locally compiled game with the same cartridge:

```bat
dist\YoKaiWatch2.exe --play "YourRom.3ds"
```

Default standalone data lives beside the executable in `dist/data`. `YW2_DATA_DIR` and `YW2_STATE_DIR` may override this for isolated tests. Do not delete or overwrite a player's real save when testing.

A fresh extracted ZIP in a folder with spaces downloaded all the pinned tools, validated the original cartridge and built every native compilation unit. The WPF Build flow was also tested through its own code path, including the Windows PowerShell module-path fix. Development configurations are available using `-Configuration Debug` or `-Configuration RelWithDebInfo`; optional `-Sanitizer Undefined` instruments the native build. Those alternate configurations still require full runtime validation. They use separate default output and build directories.

Generate source-only platform builder archives with `powershell -ExecutionPolicy Bypass -File scripts/package-builder.ps1`. It verifies the archives exclude the ROM, compiled game, extracted files and local test saves.

## Linux x86-64

Use a recent Linux distribution with Python 3, Tk, CMake 3.24+ and a C/C++20 compiler. On Ubuntu 24.04 or Debian 13, install the prerequisites if not already present:

```sh
sudo apt install build-essential cmake ninja-build pkg-config python3 python3-tk curl ca-certificates tar libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev libxtst-dev libwayland-dev libxkbcommon-dev libegl-dev libdecor-0-dev libasound2-dev libpulse-dev libudev-dev libvulkan-dev
```

Extract with `tar -xzf`, open the extracted folder, and run `./YoKaiWatch2Builder` or `bash YoKaiWatch2Builder`. You can also build without the graphical launcher:

```sh
bash build.sh --cartridge "/path/to/your decrypted EURO ROM.3ds" --language en
./dist/YoKaiWatch2 --play "/path/to/your decrypted EURO ROM.3ds"
```

The first build prepares a pinned Rust toolchain when the installed version is too old and compiles pinned SDL3 locally. Paths with spaces are supported; no global Rust installation or changes to shell profiles are made. The Linux builder is experimental and runtime compatibility varies by graphics driver. `bash build.sh --runtime-check` builds the runtime without using a ROM.

The standalone local WPF launcher is `dist/Launcher.exe`. Settings include original/30/60 FPS presentation, native to ×10 scaling, display resolution/layout, resource profiles, controls and audio. FPS interpolation is optional and separate from guest timing. In-game: F3 toggles an existing detached second window; F4 shows statistics; F7 cycles aspects; F9 toggles one screen; Tab or right-stick click swaps screens; F11 toggles fullscreen. Local source-only packaging is still in development. Do not distribute the ROM, extracted data, or compiled proprietary game code.

Graphics → FXAA and Sharpening optionally process only the upper Vulkan display. Both default to off and do not change gameplay timing or the touchscreen. Higher resolutions and these effects may cost GPU time on weaker hardware.

While the game is closed, use Maintenance → Export Diary to copy the local `head.yw`, `game1.yw`/other slots and any secure-value files to a new timestamped folder. Import decrypted Diary accepts a compatible European Psychic Specters save folder or a previous export. It verifies copied files before switching, moves existing saves to a recovery backup and never modifies the imported originals. The game itself validates Diary contents; other regions, encrypted saves and every slot configuration are not yet verified.
