# Performance evidence

Machine observed by the game: NVIDIA GeForce RTX 3080 Ti, driver `610.88.0.0`. Windows x64 native binary, Vulkan, default scale 1, interpolation off. First boot fresh test data.

`YW2_FPS=1` reports top-screen display transfers per nominal five seconds of VBlank; these are not a direct measurement of distinct displayed images, GPU present intervals or gameplay simulation updates. The opening movie reported 54.8–60.0 transfers/s across sampled windows. The English title and diary / gender menus reported 30.0 transfers/s. One cold pipeline compilation cost 124.6 ms. These measurements are not gameplay benchmarks.

## Controlled exploration benchmark

The replay in `tests/benchmark-house-clean.txt` reloads a separate Diary, runs through the house door and into Spring Breeze Boulevard, and approaches Dad. It sends identical guest input durations in each mode. `tests/analyze-trace.ps1` only samples the tagged approximately 12-second interval. All screen captures are outside it; capturing a PNG synchronously can otherwise introduce a large GPU readback stall.

For subsequent local builds, run `powershell -ExecutionPolicy Bypass -File tests/graphics-regression.ps1` (add `-AllDisplays` for extended ratios, high resolutions and detached-window checks). Every run uses an isolated copy of the fixture and produces its own screenshots, logs, CSV measurements and executable fingerprint. Verify the final scene screenshot before directly comparing two runs, as loading-time variation can alter the scripted route.

Windows / RTX 3080 Ti, Vulkan, after the initial resource fixes (known pipeline states warm), original aspect:

| Mode / profile | Mean host FPS | 1% low | 95th percentile | 99th percentile | Maximum | Peak private RAM | Average CPU logical cores |
|---|---:|---:|---:|---:|---:|---:|---:|
| 30 FPS ×1 Balanced | 30.00 | 29.46 | 33.67 ms | 33.92 ms | 33.98 ms | 468.8 MiB | 1.65 |
| 60 FPS ×4 Balanced | 60.00 | 58.09 | 16.96 ms | 17.12 ms | 17.36 ms | 773.9 MiB | 1.88 |
| 60 FPS ×1 Economy | 60.00 | 55.85 | 17.16 ms | 17.66 ms | 18.23 ms | 442.9 MiB | 1.49 |
| 60 FPS ×4 Maximum, anisotropic ×8 | 60.00 | 57.59 | 16.90 ms | 17.17 ms | 18.22 ms | 748.5 MiB | 1.92 |
| 30 FPS ×4 Balanced | 30.00 | 29.46 | 33.55 ms | 33.81 ms | 34.10 ms | 516.6 MiB | 1.67 |
| 60 FPS ×1 Balanced | 60.00 | 56.54 | 17.03 ms | 17.50 ms | 17.90 ms | 483.0 MiB | 1.80 |
| 60 FPS ×3 custom 4:3, stats on | 60.00 | 57.20 | 17.01 ms | 17.29 ms | 17.72 ms | 640.5 MiB | 1.83 |
| 60 FPS ×4 16:9, touch inset, movie-proportion fix | 60.00 | 57.00 | 16.81 ms | 17.24 ms | 17.91 ms | 788.2 MiB | 1.60 |

Evidence: `tests/runs/pace-clean-new30`, `pace-clean-new60`, `pace-clean-economy`, `pace-clean-max`, `final-30x4`, `final-60x1`, `final-custom43`. Guest VBlank publications remained around 59.75–59.84/s. The newer traces also directly measure 29.71–29.84 top and bottom transfers per second. These are actual host presentations, not a claim that every gameplay system updates 60 times/s. Interpolation produces suitable midpoint pictures. A few pixels of positional difference at the endpoint are expected from the intermediate display phase.

The previous local executable was retained and replayed without screenshot stalls in `pace-clean-old30` and `pace-clean-old60`, yielding:

| Mode | Old p99 → updated p99 | Old 1% low → updated 1% low | Old peak private RAM → updated |
|---|---:|---:|---:|
| 30 ×1 | 34.39 → 33.92 ms | 28.82 → 29.46 | 526.1 → 468.8 MiB |
| 60 ×4 | 17.62 → 17.12 ms | 56.01 → 58.09 | 750.8 → 773.9 MiB |

The clean replay improved frametime consistency at both tested settings; peak private RAM improved at ×1 and was slightly higher at ×4 on this particular run. Memory varies with caching and exact scene timing, so do not claim a universal percentage reduction.

The release-candidate cold-cache run (`tests/runs/release-candidate-cold`) loaded an exported/reimported Diary. Seven common pipeline states were prewarmed at startup in 473 ms. In the following house scene, ×4/60 presented 60.00 FPS, p99 17.27 ms, maximum 17.50 ms; top and bottom original transfers were 29.83/s. The fixed input script did not leave the house on that particular cold run, so its 589.4 MiB peak private RAM is not a comparable house-to-street result.

The later `movie-aspect-gameplay` run verified the 16:9/cold-cache route with an interactive lower-screen inset after preserving the prerecorded intro's 5:3 canvas. It reached Dad in Spring Breeze Boulevard, had a successful touch-coordinate check and finished at 60.00 displayed FPS.

Earlier paired runs with synchronous screenshots inside the measured interval showed the following effect of resource recycling and ETC decoding:

| Mode | Previous peak private RAM | Updated peak private RAM | Previous p99 | Updated p99 |
|---|---:|---:|---:|---:|
| 30 ×1 | 572.3 MiB | 477.1 MiB | 35.51 ms | 34.06 ms |
| 60 ×4 | 835.7 MiB | 748.7 MiB | 19.11 ms | 17.29 ms |

These earlier runs are not directly comparable to the screenshot-free measurement. VRAM and GPU utilization are not inferred from process private bytes.

## Implemented optimization and memory controls

- Retire old LCD rings only after the host no longer references the GPU textures. Expire stale LCD addresses and recycle transfer/vertex buffers with fence completion. Limit in-flight submissions and buffer reuse by profile.
- Reuse decoded texture replacements with a bounded LRU CPU budget (32–512 MiB), rather than holding all decoded HD images. GPU caches continue to discard stale textures. No HD pack was supplied for an extended replacement stress test.
- Block-wise ETC1/ETC1A4 decoding; optional scalar byte-for-byte cross-check succeeded on the gameplay probe.
- Persist known Vulkan pipeline descriptions, keyed by the shader contents and driver. Seven common generic states can also be prewarmed on a new installation. This is a portable state cache, not a claim of persistent driver-native machine code or elimination of all cold compilation.
- Economy, Balanced, Performance and Maximum change CPU vertex worker counts and upload resource budgets without modifying ARM timing. Maximum is not necessarily faster on every machine.
- F4 stats distinguish host present FPS, original top transfers, average present interval and local RAM/CPU when available.

The 16:9 ×5 run measured 59.39 FPS; ×8 ×10 rendering completed at 30 FPS in the tested scenes. Higher multipliers/ultrawide consume more memory; extreme attachments are capped at 8192 pixels (32:9 ×10 becomes ×9).

Still to measure: cold versus warm timings across the whole game, long sessions, every battle/transition, audio/video synchronization, actual GPU utilization and VRAM, alternate hardware, low-end PCs and refresh-rate combinations. Preserve original speed and visual correctness before broader performance claims.

## Longer initial-story session

`tests/runs/progress-ch2-001`: 29.29 minutes, Vulkan ×2, 30 FPS cap, two side-by-side screens. Included exploration, town scene loading, transport, several long in-engine cutscenes and dialogue, a shop transaction, and a Diary overwrite. The process exited normally. Peak private commit was 867.5 MiB; peak working set was 427.4 MiB; final private commit 537.6 MiB. This is one session on the local RTX 3080 Ti, not an all-game leak/stability proof. Some waiting periods were intentionally idle while driving the interactive test.

## Audio and presentation timing

The optional `YW2_AUDIO_TIMES` trace records generated/submitted DSP frames, queue depth, underruns, dropped frames and output failures. `tests/run-probe.ps1 -DumpAudio` also captures a properly finalized local 32728 Hz stereo PCM WAV; it is never distributed. Identical scripted house/street routes at ×4, 16:9, inset touchscreen gave the following results on the local PC:

| Mode | Host presentations/s | p99 frametime | Original top transfers/s | DSP frames/s | Drops / underruns / failures |
|---|---:|---:|---:|---:|---:|
| Original (uncapped host) | 164.97 | 7.12 ms | 29.88 | 32727.4 | 0 / 0 / 0 |
| 30 FPS | 30.00 | 33.99 ms | 29.92 | 32736.3 | 0 / 0 / 0 |
| 60 FPS | 60.00 | 17.10 ms | 29.79 | 32733.5 | 0 / 0 / 0 |

The output sample rate and generated sample counts are consistent with the original DSP rate in all three modes. All WAV headers match their actual payload lengths. Evidence: `tests/runs/audio-mode-{0,30,60}`. This verifies the measured timing and buffer behavior, not perceptual lip-sync or every sound in the game.

## Optional graphics postprocessing

The upper display can enable a directional FXAA pass and adjustable sharpening. The bottom display and the unchanged default GPU shader are not affected. The optional shader is embedded at build time (see `scripts/build-lcd-post.ps1`, Khronos glslang 16.6.0); the ordinary builder needs no extra shader compiler. Vulkan ×4 16:9 inset, 60 FPS identical route:

| Settings | Host FPS | p99 | 1% low |
|---|---:|---:|---:|
| Off | 60.00 | 16.89 ms | 57.95 |
| FXAA | 60.00 | 17.14 ms | 57.42 |
| Sharpening 50% | 60.00 | 17.02 ms | 57.45 |
| Both | 60.00 | 17.06 ms | 58.45 |

All four local runs finished without errors and reached the same house/street endpoint (`tests/runs/post-*`). The measured performance applies to the local GPU only, and both effects default to off.

## New GPU images actually presented

Frame identity tags now distinguish an original LCD render, a midpoint render, and repeated presentation of the same GPU image. The tags are attached when the GPU completes a render and preserved through the queue; the host counts changes of the actually selected visible top image, not merely generated work. The same movement route (host presentation trace, without screenshots inside the benchmark interval) gave:

| Mode | Host FPS | New upper GPU images/s | Midpoints/s | Original top transfers/s | p99 |
|---|---:|---:|---:|---:|---:|
| 30 ×1 | 30.00 | 29.92 | 0.00 | 29.92 | 33.81 ms |
| 60 ×1 | 60.00 | 59.50 | 29.71 | 29.79 | 16.95 ms |
| 30 ×4 | 30.00 | 29.84 | 0.00 | 29.92 | 34.08 ms |
| 60 ×4 | 60.00 | 59.58 | 29.71 | 29.79 | 16.89 ms |

Evidence: `tests/runs/unique-{30x1,60x1,30x4,60x4}`. These are distinct render identities, not a pixel-difference test: an idle scene can render identical pixels in separate images. CPU/movie paths do not receive GPU IDs, and transitions can repeat a frame. The result confirms actual intermediate renders in this gameplay passage, not a universal 60 Hz simulation.

## Windows GPU memory telemetry

F4 and the resource CSV now query the Windows WDDM process-local and nonlocal video memory through DXGI. This PC reports two distinct adapter LUIDs with the same RTX 3080 Ti model name; the runtime sums the task's reported usages for those matching adapters and does not report an ambiguous combined budget. When the OS cannot supply the figures, the displayed fallback is explicitly labeled as an estimate of live renderer texture payloads (not resident VRAM). The renderer tracks texture creation and actual release including retired LCD rings.

| Mode | Peak private RAM | WDDM local peak | WDDM nonlocal peak | Live texture payload peak | Host FPS | p99 |
|---|---:|---:|---:|---:|---:|---:|
| 30 ×1 | 502.5 MiB | 350.7 MiB | 153.0 MiB | 48.2 MiB | 30.00 | 33.84 ms |
| 60 ×4 | 677.9 MiB | 706.7 MiB | 161.7 MiB | 212.3 MiB | 60.00 | 17.27 ms |

Latest route replays `tests/runs/resource-{30x1,60x4}` completed cleanly with the additional telemetry. OS local memory and logical texture payload differ because the GPU has other resources, allocation padding and driver overhead; they must not be added together.
