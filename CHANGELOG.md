# Changelog

## 1.0.0 — first public development release

- Added the Whisper launcher icon, V1.0.0 version display, and local Windows and Linux builder entry points.
- Automatically check for stable releases, verify the full builder package before replacing source files, restart the launcher, and require a rebuild when needed. Preserve existing ROMs, saves and settings.

## 0.1.0-dev — initial local development

- Created an independent project with a pinned MIT-licensed native ARM/Thumb translator and runtime.
- Verified the local European cartridge and statically compiled its code to Windows x64 for the first time.
- Added an independent portable data directory, local-only frontend option, and correctly named `YoKaiWatch2.exe` target.
- Began reproducible portable build and scripted boot test tooling. Opening, English title and new-game screens confirmed in a local Vulkan run.
- Added runtime SHA-256 validation before guest execution, seven-language CFG selection, host presentation cap + timing/resource telemetry, and save checkpoint/atomic secure-value handling.
- Built WPF reference-style launcher with original-language selection, graphics/audio settings, local mod toggle, keyboard/controller mapping and game-build/launch integration; rendered initial UI previews.
- Confirmed initial story, 3D movement/stairs/NPC dialog and the first completed Diary save on the original native baseline.
- Verified Diary reload and movement into Springdale. Added independently paced original/30/60 presentation modes; geometry interpolation retains original game scheduling.
- Extended Vulkan render resolution through ×10, genuine 16:9/16:10/21:9/32:9 and custom aspect, custom window resolution, configurable dual-screen layouts and detached touchscreen.
- Added bounded upload-buffer recycling, screen-texture retirement, decoded HD texture cache, block-wise ETC decoding, pipeline prewarming, resource profiles and host frame telemetry.
- Added Display and Performance launcher pages, texture sampling controls, and FPS/statistics hotkey.
- Added local Diary export/import, verified staged copies and recovery backups in the launcher.
- Preserve prerecorded video proportions automatically in expanded-aspect modes, then restore the selected gameplay view.
- Verified independent clean dependency downloads, source package native compilation and WPF build flow; fixed inherited PowerShell module search paths.
- Added named controller profiles, cache archiving, log export and diagnostic reporting.
- Verified Shopper's Row and the Spirit Doughnuts story sequence, then a new Diary save at Triangle Way.
- Tested actual Diary overwrites with byte-verified recovery copies, mapped mouse touch in three screen layouts and local mod order/profiles.
- Added DSP sample/queue telemetry and tested all three display timing modes; added optional top-screen FXAA and a sharpening control (off by default).
- Added rendered-image identity counters, real Windows video-memory telemetry with a labeled payload estimate fallback, and playback-device enumeration.
- Fixed a native map-streaming memory exhaustion issue that could remove both terrain and its collision geometry. Increased the four original map-block heaps to 4 MiB each, with matching base-map and startup budgets. No map assets, physics or save data are modified.
- Added native in-game menus for upscaling, aspect ratio, display layout, HD textures and FPS, and verified changing settings live using the original simulation timing.
- Added a local archive installer and direct download option for Psychic Specters HD, with exact legacy Citra CityHash texture matching, bounded decoded texture caching and safe live HD on/off switching. Verified original and HD rendering in Triangle Way.
- Added verified European texture-ID aliases for the English title, navigation and dialogue UI, and a building atlas in the US-targeted HD pack. Upscale the pack's two original-resolution font-mask atlases independently without changing glyphs, translations or texture coordinates.
- Preserve the original glyph masks when the pack only supplies equivalent native-resolution copies; render the two font atlases at twice their original dimensions with corrected coverage contrast even when HD textures are disabled. This affects the letters, not scene colors or the game's chosen text colors.
- Exclude the exact 8×8 font-channel selection palette from HD replacement: the pack's filtered copy changed the primary reference colors and faded text. Keep its original numerical values in all modes and languages.
- Removed the optional texture-pack downloader, import UI and in-game HD menu. Older pack settings are ignored. Original textures and independent improved glyph rendering are retained.
