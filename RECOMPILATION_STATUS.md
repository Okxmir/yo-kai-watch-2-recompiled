# Recompilation status

## Source cartridge

| Property | Observed |
|---|---|
| Game | Yo-kai Watch 2: Psychic Specters (Europe) |
| Title ID | `00040000001B2900` |
| Product code | `CTR-P-BYSP` |
| NCSD partition 0 offset | `0x4000` |
| NCCH flags | `00-00-00-00-01-03-00-04` (decrypted content successfully parsed) |
| Source size | 4,294,967,296 bytes |
| Cartridge SHA-256 | `E625CF86D8D5DDE8D64A5C64F91B356D7417024BD5FA2BDD387D48210096351E` |
| Decompressed code SHA-256 | `B536DFCE0A7138F2D5691532604D782B1EA3E20731120CC662BAEE118C370547` |
| Exheader remaster-version field | 0 (not proof of an external retail version label) |
| ARM code image | `0x761000` bytes after decompression |
| Text | `0x00100000`, `0x005F32AC` bytes |
| Read-only data | `0x006F4000`, `0x0011842C` bytes |
| Data | `0x0080D000`, `0x00053934` bytes |
| BSS | `0x000BB088` bytes |
| Stack | `0x6000` bytes |
| RomFS | 7,100 files, 3638.3 MiB reported by runtime |
| Dynamic CRO/CRS/CRR code modules | none found |

The builder accepts the specific executable revision after extracting and checking its SHA-256. Different padding/container sizes need not imply different executable code. The exact ROM fingerprint above is for this local copy. No game assets or code go into a source-only release.

## Native pipeline

Pinned baseline: [Clockwork](https://github.com/watchmakerers/clockwork) `3047d9e990be4ac2c84174cb570dfec569c882e9` (MIT), same upstream origin as Super Mario 3D Land. The original source remains available in the reference project; this project contains its own copy including the license. No Mario gameplay patches were copied.

1. `ctrx --code-only` parses the decrypted container, exheader, ExeFS and RomFS directory, decompresses `.code` locally.
2. `recomp emit` identifies ARM/Thumb functions and emits C with native calls and static lookup tables. 32,547 functions; 260,532,631 bytes of generated files, including `lookup.c` and `funcs.h`.
3. LLVM/Clang 22.1.8 compiles native x86-64 C code (O1, defined wrapping arithmetic, no floating-point contraction) and C++20 runtime (O2), links to SDL3 3.4.0. The linked game is a PE x86-64 (`llvm-objdump -p`).
4. The runtime maps the cartridge's original 32-bit address layout and exposes implemented OS services. Assets are read directly from the selected dump, not extracted in their entirety. The GPU shader programs are bundled as SPIR-V within the runtime; original PICA vertex processing, native GPU draw translation, shader pipelines and audio are handled by the shared runtime.

Evidence: `logs/extraction.txt`, `logs/recompile.txt`, `logs/configure.txt`, `logs/build-native.txt`, `tests/runs/boot-002/console.txt`. Static discovery coverage will be separately audited; a successful title screen is not proof that every function or service is implemented correctly.

The latest local command-line build also compiles the corresponding WPF launcher, verifies the cartridge/code hashes and writes the installed executable hash to `dist/build-manifest.json`. All seven localized title menus and the actual portable launcher home have been verified with the installed executable.
