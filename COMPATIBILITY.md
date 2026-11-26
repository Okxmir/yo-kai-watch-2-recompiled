# Compatibility

| Item | Status |
|---|---|
| EUR Psychic Specters `CTR-P-BYSP` / code fingerprint in status report | Code extraction, translation, Windows x64 link and title verified |
| Native executable without external emulator | Verified PE x86-64, opening and menu |
| Windows Vulkan, RTX 3080 Ti | Verified opening and title |
| Fresh local save area | Created, Diary displays Save complete, `head.yw` and `game1.yw` created; actual reload and house/street route verified |
| Dual-screen stacked presentation | Verified opening, menu and diary |
| Touch input | New Game button, touchscreen name keyboard (typed NATE), mapped touch in side-by-side/detached/inset layouts and gameplay Diary verified |
| Expanded aspects and resolution | Real 16:9/16:10/21:9/32:9 camera geometry in tested areas; ×1–10 tested up to 8192-pixel attachment limit; remaining scenes experimental |
| Independent touchscreen | Actual two Vulkan windows, swap, hide/restore, touch-coordinate round-trip verified on one monitor |
| Host 30/60 presentation | Controlled house/street route measured 30.0 / 60.0 displayed FPS; approximately 59.5–59.6 new GPU renders/s in 60 mode, original transfers remain around 29.8/s |
| Diary export/import | Export, import, backup SHA-256 checks and reload of imported fixture verified; other 3DS save variants not yet tested |
| English | Title, diary, gender, opening story and house dialogue verified |
| Initial exploration | Movement upstairs, stair transition to downstairs, NPC dialogue verified |
| Story beyond the house | Dad dialogue, car transition, Shopper's Row, Spirit Doughnuts purchase, friends cutscene and second Diary save verified |
| Terrain streaming | Reproduced and fixed missing road/ground collision at Triangle Way. Native route checks at ×1/30, ×2/30 and ×4/60; read-only audit of all 2,208 map files, including 896 model containers, 29,376 meshes and 4,472 images. Full in-game exploration of later maps remains to verify. |
| Clean Windows build | Fresh extracted source-only ZIP, all pinned downloads, native build, WPF Build flow; complete game still under validation |
| Controller editor | Actual named profile persistence, keyboard and virtual SDL controller remap self-tests pass |
| Graphics settings | Optional upper-screen FXAA and sharpening tested at ×4 60 FPS; custom window/aspect/resolution/PiP settings persisted and reloaded in actual WPF UI |
| Audio | DSP rate and queue telemetry verified at Original/30/60; actual playback device enumeration and selection persistence verified |
| Memory diagnostics | Windows WDDM process GPU memory and live renderer texture tracking verified on local adapters |
| French, German, Italian, Spanish, Dutch, Russian | Localized title and new game menu verified visually; later dialogue not yet validated |
| Other GPU vendors and Windows 10 | Not yet validated |
| macOS/Linux | Upstream supports them; this standalone build is Windows-first and not currently tested on either |
| Additional release revisions and updates | Not accepted by the first builder until code validated |

All broader game-feature and long-session compatibility remain pending actual testing.
