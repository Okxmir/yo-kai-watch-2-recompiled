// SDL3 frontend: the launcher, then one window with both screens (stacked or one large),
// keyboard, controller and mouse-as-touch input with the user's bindings, and audio output.
//
// Rendering is done by the software PICA200 rasterizer into guest memory; this side only
// uploads the two finished 400x240/320x240 images each frame, so SDL_Renderer on whichever
// backend the platform prefers (Metal, D3D, Vulkan, OpenGL) is all that is needed.
#include "dump.h"
#include "frontend.h"
#include "handoff.h"

#include "launcher.h"
#include "kernel/kernel.h"
#include "log.h"
#include "settings.h"
#include "game_menu.h"
#include "video/gpu/gpu.h"
#include "video/pica.h"
#include "video/upscale.h"
#include "platform.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#include <dxgi1_4.h>
#endif

namespace frontend {
namespace {

constexpr int WIDTHS[2] = {TOP_WIDTH, BOTTOM_WIDTH};
constexpr int CIRCLE_MAX = 156;

std::mutex frame_lock;
std::vector<uint8_t> frames[2] = {std::vector<uint8_t>(TOP_WIDTH * SCREEN_HEIGHT * 4),
                                  std::vector<uint8_t>(BOTTOM_WIDTH * SCREEN_HEIGHT * 4)};
int frame_width[2] = {TOP_WIDTH, BOTTOM_WIDTH}, frame_height[2] = {SCREEN_HEIGHT, SCREEN_HEIGHT};
// frames rendered on the GPU: the texture to show instead of `frames`
SDL_GPUTexture *gpu_frame[2] = {nullptr, nullptr};
uint64_t gpu_frame_generation[2]{}; // protected by frame_lock
bool gpu_frame_midpoint[2]{};
std::vector<SDL_GPUTexture *> retired_gpu_frames; // protected by frame_lock
bool frame_dirty[2] = {false, false};

std::mutex input_lock;
Input current_input;

void (*game_start)(const char *dump);
std::string gpu_description; // the GPU in use, for the log and the launcher
std::string game_path;

#if defined(_WIN32)
// WDDM's per-process usage for adapters matching SDL's device name. Identical
// models can have distinct adapter LUIDs; in that case query every match and
// only show a budget if exactly one is active. Never pick a card by index.
struct GpuMemoryTelemetry {
    std::vector<IDXGIAdapter3 *> adapters;
    bool attempted = false, dedicated = true;
    ~GpuMemoryTelemetry() { for(auto *adapter:adapters) adapter->Release(); }
    void init(SDL_GPUDevice *device) {
        if (attempted || !device) return;
        attempted = true;
        const char *name = SDL_GetStringProperty(SDL_GetGPUDeviceProperties(device),SDL_PROP_GPU_DEVICE_NAME_STRING,"");
        IDXGIFactory1 *factory = nullptr;
        if (!*name || FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&factory)))) return;
        unsigned matches = 0;
        std::vector<LUID> seen;
        for (UINT i=0;;++i) {
            IDXGIAdapter1 *candidate = nullptr;
            if (FAILED(factory->EnumAdapters1(i,&candidate))) break;
            DXGI_ADAPTER_DESC1 desc{};
            char utf8[512]{};
            if (SUCCEEDED(candidate->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
                WideCharToMultiByte(CP_UTF8,0,desc.Description,-1,utf8,sizeof utf8,nullptr,nullptr) &&
                SDL_strcasecmp(utf8,name)==0) {
                const LUID luid=desc.AdapterLuid;
                if(std::none_of(seen.begin(),seen.end(),[&](LUID other){return other.HighPart==luid.HighPart&&other.LowPart==luid.LowPart;})) {
                    seen.push_back(luid);
                    ++matches;
                    IDXGIAdapter3 *adapter=nullptr;
                    if(SUCCEEDED(candidate->QueryInterface(__uuidof(IDXGIAdapter3),reinterpret_cast<void **>(&adapter)))) adapters.push_back(adapter);
                    dedicated &= desc.DedicatedVideoMemory != 0;
                }
            }
            candidate->Release();
        }
        factory->Release();
        if (matches!=adapters.size()) { for(auto *adapter:adapters)adapter->Release(); adapters.clear(); }
        if (!adapters.empty()) LOG("gpu: WDDM process memory telemetry available for %s (%zu matching adapter(s))",name,adapters.size());
    }
    bool query(DXGI_MEMORY_SEGMENT_GROUP group,DXGI_QUERY_VIDEO_MEMORY_INFO &info) const {
        info={};
        if(adapters.empty())return false;
        unsigned active=0;
        for(auto *adapter:adapters){
            DXGI_QUERY_VIDEO_MEMORY_INFO sample{};
            if(FAILED(adapter->QueryVideoMemoryInfo(0,group,&sample))) return false;
            info.CurrentUsage+=sample.CurrentUsage;
            if(sample.CurrentUsage || adapters.size()==1){++active;info.Budget=sample.Budget;}
        }
        if(active!=1)info.Budget=0;
        return true;
    }
};
#endif

// YW2_DUMP_FRAMES=<dir>[:every]: saves screens as PNG (for headless testing)
struct FrameDump {
    std::string dir;
    int every = 60;
    int count[2] = {0, 0};

    FrameDump() {
        const char *env = std::getenv("YW2_DUMP_FRAMES");
        if (!env || !*env) return;
        dir = env;
        if (auto colon = dir.rfind(':'); colon != std::string::npos && colon > 1) {
            every = std::max(1, std::atoi(dir.c_str() + colon + 1));
            dir.resize(colon);
        }
    }

    void frame(int screen, const uint8_t *rgba, int width, int height) {
        if (dir.empty()) return;
        int n = count[screen]++;
        if (n % every) return;
        SDL_Surface *s = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32,
                                               const_cast<uint8_t *>(rgba), width * 4);
        if (!s) return;
        std::string path = dir + "/" + (screen ? "bottom_" : "top_") + std::to_string(n / every) + ".png";
        if (!SDL_SavePNG(s, path.c_str())) LOG("frame dump: %s", SDL_GetError());
        SDL_DestroySurface(s);
    }
} frame_dump;

// YW2_CAPTURE_TITLE=<file.png> (headless): finds the game's title screen and saves it, for the
// launcher's cards. The games open with logos (plain) and then their opening movie (colourful),
// which a press skips to the title screen. So: wait until the screen has been colourful for a
// second, press A once, and save the first colourful frame 6 s later (the title, its logo landed). A game
// that shows its title before any movie opens the title's menu with that press, below the title.
// Exit status 2 if nothing colourful showed up in 90 s
struct TitleCapture {
    static constexpr int EVERY = 30, SETTLE = 360, GIVE_UP = 90 * 60;
    std::string path;
    int colourful_for = 0;  // samples in a row
    int pressed_at = -1;    // the frame A went down
    TitleCapture() {
        if (const char *env = std::getenv("YW2_CAPTURE_TITLE")) path = env;
    }
    bool on() const { return !path.empty(); }

    void press(int frame, Input &in) const {
        if (on() && pressed_at >= 0 && frame < pressed_at + 10) in.buttons |= BTN_A;
    }

    // colourful and not plain: enough saturation and contrast over a sample of pixels
    static bool colourful(const uint8_t *rgba, size_t pixels) {
        double sat = 0, lum = 0, lum2 = 0;
        size_t n = 0;
        for (size_t i = 0; i < pixels; i += 7, n++) {
            const uint8_t *p = rgba + i * 4;
            int hi = std::max({p[0], p[1], p[2]}), lo = std::min({p[0], p[1], p[2]});
            double l = (p[0] * 0.3 + p[1] * 0.59 + p[2] * 0.11) / 255.0;
            sat += hi ? double(hi - lo) / hi : 0, lum += l, lum2 += l * l;
        }
        double mean = lum / double(n), var = lum2 / double(n) - mean * mean;
        return sat / double(n) > 0.25 && var > 0.01;
    }

    void frame(int top_frame, const uint8_t *rgba, int width, int height) {
        if (!on()) return;
        if (top_frame > GIVE_UP) {
            LOG("title capture: no title screen found");
            std::fflush(nullptr);
            std::_Exit(2);
        }
        if (top_frame % EVERY) return;
        bool bright = colourful(rgba, size_t(width) * height);
        if (pressed_at < 0) { // waiting for the movie
            colourful_for = bright ? colourful_for + 1 : 0;
            if (colourful_for >= 2) pressed_at = top_frame;
            return;
        }
        if (top_frame < pressed_at + SETTLE || !bright) return;
        SDL_Surface *s = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, const_cast<uint8_t *>(rgba), width * 4);
        bool saved = s && SDL_SavePNG(s, path.c_str());
        if (s) SDL_DestroySurface(s);
        LOG("title capture: %s at frame %d %s", saved ? "saved" : "couldn't save", top_frame, saved ? "" : SDL_GetError());
        std::fflush(nullptr);
        std::_Exit(saved ? 0 : 1);
    }
} title_capture;

// YW2_PRESS=<button>@<frame>[,...]: holds a button for 10 frames at a top-screen frame
// number (buttons: a b x y l r start select up down left right, cup cdown cleft cright for the
// circle pad, or touch:X:Y for a tap on the bottom screen), for unattended testing
struct InputScript {
    struct Press {
        uint32_t button;
        int frame;
        int touch_x = -1, touch_y = -1;
        int16_t cx = 0, cy = 0; // circle pad
    };
    std::vector<Press> presses;

    InputScript() {
        const char *env = std::getenv("YW2_PRESS");
        if (!env) return;
        static const std::pair<const char *, uint32_t> names[] = {
            {"a", BTN_A},       {"b", BTN_B},         {"x", BTN_X},       {"y", BTN_Y},
            {"l", BTN_L},       {"r", BTN_R},         {"start", BTN_START}, {"select", BTN_SELECT},
            {"up", BTN_UP},     {"down", BTN_DOWN},   {"left", BTN_LEFT}, {"right", BTN_RIGHT},
        };
        std::string spec = env;
        size_t pos = 0;
        while (pos < spec.size()) {
            size_t end = spec.find(',', pos);
            if (end == std::string::npos) end = spec.size();
            std::string item = spec.substr(pos, end - pos);
            pos = end + 1;
            size_t at = item.find('@');
            if (at == std::string::npos) continue;
            std::string name = item.substr(0, at);
            int frame = std::atoi(item.c_str() + at + 1);
            if (name.rfind("touch:", 0) == 0) {
                int x = 0, y = 0;
                if (std::sscanf(name.c_str() + 6, "%d:%d", &x, &y) == 2) presses.push_back({0, frame, x, y});
                continue;
            }
            static const std::pair<const char *, std::pair<int, int>> circle[] = {
                {"cup", {0, 1}}, {"cdown", {0, -1}}, {"cleft", {-1, 0}}, {"cright", {1, 0}}};
            for (auto &[n, d] : circle)
                if (name == n) presses.push_back({0, frame, -1, -1, int16_t(d.first * CIRCLE_MAX), int16_t(d.second * CIRCLE_MAX)});
            for (auto &[n, bit] : names)
                if (name == n) presses.push_back({bit, frame});
        }
    }

    void apply(int frame, Input &in) const {
        for (const Press &p : presses) {
            if (frame < p.frame || frame >= p.frame + 10) continue;
            in.buttons |= p.button;
            if (p.cx || p.cy) in.circle_x = p.cx, in.circle_y = p.cy;
            if (p.touch_x >= 0) in.touching = true, in.touch_x = uint16_t(p.touch_x), in.touch_y = uint16_t(p.touch_y);
        }
    }
} input_script;

// YW2_CONTROL=<dir>: input driven step by step from outside, for unattended play. Lines appended to
// <dir>/cmd run in order: "hold <buttons> <frames>" (buttons joined by +: a b x y l r start select
// up down left right, cup cdown cleft cright), "tap <x> <y> <frames>" (bottom screen), "wait <frames>",
// "shot <name>" (the window, as <dir>/<name>.png) and "log <text>" (a marker in the log).
// <dir>/status says how many lines are done.
struct Control {
    std::filesystem::path dir;
    std::mutex lock;
    Input held;
    std::vector<std::string> lines;
    size_t next = 0, read_bytes = 0;
    int until = -1; // the frame the current hold or wait ends at
    std::string pending_shot;

    Control() {
        if (const char *env = std::getenv("YW2_CONTROL")) dir = env;
    }
    explicit operator bool() const { return !dir.empty(); }

    static Input parse_buttons(const std::string &spec) {
        static const std::pair<const char *, uint32_t> names[] = {
            {"a", BTN_A},       {"b", BTN_B},         {"x", BTN_X},       {"y", BTN_Y},
            {"l", BTN_L},       {"r", BTN_R},         {"start", BTN_START}, {"select", BTN_SELECT},
            {"up", BTN_UP},     {"down", BTN_DOWN},   {"left", BTN_LEFT}, {"right", BTN_RIGHT},
        };
        Input in;
        size_t pos = 0;
        while (pos <= spec.size()) {
            size_t end = spec.find('+', pos);
            if (end == std::string::npos) end = spec.size();
            std::string n = spec.substr(pos, end - pos);
            pos = end + 1;
            for (auto &[name, bit] : names)
                if (n == name) in.buttons |= bit;
            if (n == "cup") in.circle_y = CIRCLE_MAX;
            if (n == "cdown") in.circle_y = -CIRCLE_MAX;
            if (n == "cleft") in.circle_x = -CIRCLE_MAX;
            if (n == "cright") in.circle_x = CIRCLE_MAX;
        }
        return in;
    }

    void apply(Input &in) {
        std::lock_guard lk(lock);
        in.buttons |= held.buttons;
        if (held.circle_x || held.circle_y) in.circle_x = held.circle_x, in.circle_y = held.circle_y;
        if (held.touching) in.touching = true, in.touch_x = held.touch_x, in.touch_y = held.touch_y;
    }

    // render thread, each window frame: reads new commands, advances them, takes a due screenshot
    void tick(int frame, SDL_Renderer *renderer, SDL_Renderer *secondary,
              const std::function<bool(int,int,Input&)> &mapped_touch) {
        if (std::ifstream f{dir / "cmd", std::ios::binary}) {
            f.seekg(0, std::ios::end);
            size_t size = size_t(f.tellg());
            if (size > read_bytes) {
                std::string text(size - read_bytes, '\0');
                f.seekg(std::streamoff(read_bytes));
                f.read(text.data(), std::streamsize(text.size()));
                size_t last = text.rfind('\n');
                if (last != std::string::npos) {
                    read_bytes += last + 1;
                    size_t pos = 0;
                    while (pos <= last) {
                        size_t end = text.find('\n', pos);
                        lines.push_back(text.substr(pos, end - pos));
                        pos = end + 1;
                    }
                }
            }
        }
        bool changed = false;
        while (true) {
            if (until >= 0 && frame < until) break;
            if (until >= 0) {
                std::lock_guard lk(lock);
                held = {}, until = -1, changed = true;
            }
            if (next >= lines.size()) break;
            std::string line = lines[next++];
            changed = true;
            char word[16] = {}, arg[64] = {};
            int a = 0, b = 0, c = 0;
            if (std::sscanf(line.c_str(), "%15s", word) != 1) continue;
            std::string w = word;
            if (w == "hold" && std::sscanf(line.c_str(), "%*s %63s %d", arg, &a) == 2) {
                std::lock_guard lk(lock);
                held = parse_buttons(arg), until = frame + a;
            } else if (w == "tap" && std::sscanf(line.c_str(), "%*s %d %d %d", &a, &b, &c) == 3) {
                std::lock_guard lk(lock);
                held = {}, held.touching = true, held.touch_x = uint16_t(a), held.touch_y = uint16_t(b);
                until = frame + c;
            } else if (w == "tap_mapped" && std::sscanf(line.c_str(), "%*s %d %d %d", &a, &b, &c) == 3) {
                Input mapped{};
                if(!mapped_touch(a,b,mapped))fatal("mapped touch cannot reach %d,%d in current layout",a,b);
                LOG("control: mapped window touch %d,%d -> %u,%u",a,b,unsigned(mapped.touch_x),unsigned(mapped.touch_y));
                std::lock_guard lk(lock);
                held=mapped;until=frame+c;
            } else if (w == "wait" && std::sscanf(line.c_str(), "%*s %d", &a) == 1) {
                until = frame + a;
            } else if (w == "log") {
                LOG("control: frame %d time_ns %llu %s", frame,
                    static_cast<unsigned long long>(SDL_GetTicksNS()), line.c_str() + std::min<size_t>(line.size(), 4));
            } else if (w == "quit") {
                SDL_Event quit{};
                quit.type = SDL_EVENT_QUIT;
                SDL_PushEvent(&quit);
            } else if (w == "menu" && std::sscanf(line.c_str(), "%*s %63s %d", arg, &a)==2) {
                const std::string name=arg;
                int command=name=="scale"?GameMenu::Scale+a:name=="aspect"?GameMenu::Aspect+a:
                    name=="layout"?GameMenu::Layout+a:name=="hd"?GameMenu::Hd+a:
                    name=="fps"?GameMenu::Fps+a:name=="fullscreen"?GameMenu::Fullscreen:0;
                if(!command)fatal("unknown test menu command: %s",arg);
#if defined(_WIN32)
                auto hwnd=static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(SDL_GetRenderWindow(renderer)),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr));
                if(!hwnd||!PostMessageW(hwnd,WM_COMMAND,WPARAM(command),0))fatal("could not post native menu command");
#else
                GameMenu::dispatch(command);
#endif
            } else if (w == "key" && std::sscanf(line.c_str(), "%*s %63s", arg) == 1) {
                SDL_Event key{}; key.type=SDL_EVENT_KEY_DOWN; key.key.scancode=SDL_GetScancodeFromName(arg);
                if (key.key.scancode!=SDL_SCANCODE_UNKNOWN) SDL_PushEvent(&key);
            } else if ((w == "shot" || w == "shot_secondary") && std::sscanf(line.c_str(), "%*s %63s", arg) == 1) {
                SDL_Renderer *target = w == "shot_secondary" ? secondary : renderer;
                SDL_Surface *surface = target ? SDL_RenderReadPixels(target, nullptr) : nullptr;
                std::string file = (dir / (std::string(arg) + ".png")).string();
                if (!surface || !SDL_SavePNG(surface, file.c_str())) LOG("control shot %s: %s", file.c_str(), SDL_GetError());
                if (surface) SDL_DestroySurface(surface);
            }
        }
        static int last_frame = -1000;
        if (changed || frame - last_frame >= 60) {
            last_frame = frame;
            std::ofstream st{dir / "status.tmp"};
            st << "done " << next << " of " << lines.size() << " frame " << frame << "\n";
            st.close();
            std::error_code ec;
            std::filesystem::rename(dir / "status.tmp", dir / "status", ec);
        }
    }
} control;
std::atomic<int> top_frames{0};
std::atomic<uint64_t> guest_transfers[2]{};

// audio: an SDL stream that resamples the DSP's 32728 Hz output to the device rate
SDL_AudioStream *audio_stream = nullptr;
constexpr int AUDIO_RATE = 32728;
// jitter buffer: play once some is queued (~50 ms at normal latency), refill after running dry (a
// short gap instead of crackling), and drop frames beyond three times that so latency stays low
int start_queued_bytes = AUDIO_RATE / 20 * 4;
int max_queued_bytes = AUDIO_RATE * 3 / 20 * 4;
bool audio_mono = false;
bool audio_playing = false;
std::mutex audio_lock;
// Count frames in the original DSP sample domain, independently of display FPS.
struct AudioStats {
    uint64_t generated = 0, submitted = 0, dropped = 0, underruns = 0, failures = 0;
    int max_queue = 0;
} audio_stats;

// YW2_DUMP_AUDIO=<file.wav>: also writes everything played to a WAV file
struct WavDump {
    std::FILE *f = nullptr;
    uint32_t bytes = 0;

    WavDump() {
        const char *path = std::getenv("YW2_DUMP_AUDIO");
        if (!path || !*path || !(f = std::fopen(path, "wb"))) return;
        header();
    }
    void header() {
        auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
        auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
        std::fseek(f, 0, SEEK_SET);
        std::fwrite("RIFF", 1, 4, f), u32(36 + bytes), std::fwrite("WAVEfmt ", 1, 8, f);
        u32(16), u16(1), u16(2), u32(AUDIO_RATE), u32(AUDIO_RATE * 4), u16(4), u16(16);
        std::fwrite("data", 1, 4, f), u32(bytes);
        std::fseek(f, 0, SEEK_END);
    }
    void write(const int16_t *s, int frames) {
        if (!f) return;
        if (uint64_t(bytes) + uint64_t(frames) * 4 > UINT32_MAX - 36) { finish(); return; }
        std::fwrite(s, 4, size_t(frames), f);
        bytes += uint32_t(frames) * 4;
        if ((bytes / 4) % (AUDIO_RATE / 2) < uint32_t(frames)) header(); // keep the file valid
    }
    void finish() {
        if (!f) return;
        header();
        std::fclose(f);
        f = nullptr;
    }
} wav_dump;

void open_audio() {
    const Settings &s = settings();
    static const int START_MS[3] = {25, 50, 100}; // low, normal, high latency
    start_queued_bytes = AUDIO_RATE * START_MS[std::clamp(s.audio_latency, 0, 2)] / 1000 * 4;
    max_queued_bytes = start_queued_bytes * 3;
    audio_mono = s.mono;
    // the chosen device, by name, if it's there
    SDL_AudioDeviceID device = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    if (!s.audio_device.empty()) {
        int n = 0;
        if (SDL_AudioDeviceID *ids = SDL_GetAudioPlaybackDevices(&n)) {
            for (int i = 0; i < n; i++)
                if (const char *name = SDL_GetAudioDeviceName(ids[i]); name && s.audio_device == name) device = ids[i];
            SDL_free(ids);
        }
        if (device == SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK) LOG("audio: %s isn't there; using the default", s.audio_device.c_str());
    }
    SDL_AudioSpec spec = {SDL_AUDIO_S16, 2, AUDIO_RATE};
    audio_stream = SDL_OpenAudioDeviceStream(device, &spec, nullptr, nullptr);
    if (!audio_stream) {
        LOG("no audio output (%s)", SDL_GetError());
        return;
    }
}

// Screen layout from the settings: both screens stacked like the console, or one large
// screen with the other as a picture-in-picture.
// What the game is showing, from its draw statistics (see pica::SceneInfo), for the smart
// one-screen layout
enum Context { CTX_OTHER, CTX_FIELD, CTX_SCENE, CTX_MENU, CTX_BATTLE, CTX_DUAL, CTX_COUNT };
const char *const CONTEXT_NAMES[] = {"other", "field", "scene", "menu", "battle", "dual"};
// as the F5 message names them
const char *const CONTEXT_LABELS[] = {"Other screens", "Exploring", "Scenes", "Menus", "Battles",
                                      "Both screens busy"};

Context classify(const pica::SceneInfo &s) {
    // nothing on the bottom screen (item fanfares, movies): no inset covering the top screen
    if (s.bottom_draws <= 1) return CTX_SCENE;
    // 3D on top: exploring (the map below), a cutscene or dialogue (little or nothing below),
    // or a battle (busy controls below). The map draws each Yo-kai and NPC marker on its own, so
    // a crowded map reaches battle-like draw counts (15) from few textures (5); battle controls
    // need many different ones too
    if (s.top_3d_draws > 0) {
        if (s.bottom_draws > 14 && s.bottom_textures.size() >= 8) return CTX_BATTLE;
        return s.bottom_draws >= 4 ? CTX_FIELD : CTX_SCENE;
    }
    // a menu (Yo-kai Pad and friends): just a backdrop on top, the menu below
    if (s.top_draws <= 3 && s.bottom_draws >= 4) return CTX_MENU;
    // 2D on both: a menu whose top screen shows something too (Blasters' team beside the Yo-kai Pad)
    if (s.bottom_draws >= 4) return CTX_DUAL;
    return CTX_OTHER;
}

// How one screen is arranged for a kind of scene. DEFAULT is the plain one-screen layout from the
// settings (the other screen in a corner, if picture-in-picture is on).
enum LayoutKind { L_DEFAULT, L_TOP_ONLY, L_MINIMAP, L_CONTROLS, L_CONTROLS_TOP, L_MENU, L_SIDE, LAYOUT_COUNT };
const char *const LAYOUT_IDS[] = {"default", "top", "minimap", "controls", "controls_top", "menu", "side"};
const char *const LAYOUT_LABELS[] = {"Other screen in a corner", "Top screen only", "Bottom screen as a small map",
                                     "Bottom screen large, bottom right", "Bottom screen large, top right",
                                     "Bottom screen over the top", "Side by side"};

// Each game's layouts, by what it shows: its screens are set up differently. Changed with F5 and
// remembered per game (Settings::layouts). The games share one so far; a game whose screens work
// differently gets its own entry.
struct LayoutProfile {
    const char *game; // GameInfo::id
    LayoutKind for_context[CTX_COUNT];
};
// Menus keep the field's layout (the bottom screen small in the corner) instead of zooming in on
// it (L_MENU): switching back and forth as menus open and close was distracting. F5 still picks
// L_MENU for a game's menus
//                                     other      field       scene        menu    battle      dual
constexpr LayoutProfile YW2_PROFILE = {"yw2", {L_DEFAULT, L_MINIMAP, L_TOP_ONLY, L_MINIMAP, L_CONTROLS, L_DEFAULT}};
constexpr LayoutProfile PROFILES[] = {
    YW2_PROFILE,
    // Blasters' bottom screen is always its map: a busy one reads as battle controls, but stays small
    {"blasters", {L_DEFAULT, L_MINIMAP, L_TOP_ONLY, L_MINIMAP, L_MINIMAP, L_DEFAULT}},
    // Yo-kai Watch 3's battles: the controls top right, where its map is, so the bottom screen
    // stays put when a battle's bottom screen briefly looks like the map
    {"yw3", {L_DEFAULT, L_MINIMAP, L_TOP_ONLY, L_MINIMAP, L_CONTROLS_TOP, L_DEFAULT}},
};

LayoutKind profile_layout(const std::string &game, Context c) {
    for (const LayoutProfile &p : PROFILES)
        if (game == p.game) return p.for_context[c];
    return YW2_PROFILE.for_context[c];
}

// the layout for a kind of scene in a game: the player's choice (F5), or the game's profile
LayoutKind layout_for(const std::string &game, Context c) {
    const Settings &s = settings();
    if (auto it = s.layouts.find(game + "." + CONTEXT_NAMES[c]); it != s.layouts.end())
        for (int k = 0; k < LAYOUT_COUNT; k++)
            if (it->second == LAYOUT_IDS[k]) return LayoutKind(k);
    return profile_layout(game, c);
}

struct Layout {
    float top_width = TOP_WIDTH; // the top screen's current width (wider in widescreen)
    Context context = CTX_OTHER;
    std::string game; // GameInfo::id, for its layout profile

    // logical canvas size and where each screen goes; computed by update()
    float w = 0, h = 0;
    SDL_FRect rect[2]{};
    SDL_FRect src[2]{}; // the part of each screen shown, in native pixels (all of it by default)
    float alpha[2] = {1, 1};
    bool shown[2] = {true, true};
    bool framed = false; // outline the inset
    int first = 0; // drawn first (the large screen in single mode)

    void update() {
        const Settings &s = settings();
        const float W[2] = {top_width, float(BOTTOM_WIDTH)}, H = float(SCREEN_HEIGHT);
        src[0] = {0, 0, W[0], H}, src[1] = {0, 0, W[1], H};
        alpha[0] = alpha[1] = 1;
        framed = false;
        if (s.detached_screen) {
            first = s.swap_screens ? 1 : 0;
            w = W[first]; h = H;
            rect[first] = {0,0,w,h};
            shown[first] = true; shown[1-first] = false;
            return;
        }
        LayoutKind kind = s.single_screen && s.smart_layout && !s.swap_screens ? layout_for(game, context) : L_DEFAULT;
        if (kind != L_DEFAULT) {
            w = W[0], h = H;
            shown[0] = shown[1] = true;
            if (kind == L_SIDE) { // both whole, side by side at the same height, centered
                float gap = 4, k = std::min(1.0f, (w - gap * 3) / (W[0] + W[1]));
                float total = (W[0] + W[1]) * k + gap, x = (w - total) / 2, y = (h - H * k) / 2;
                rect[0] = {x, y, W[0] * k, H * k};
                rect[1] = {x + W[0] * k + gap, y, W[1] * k, H * k};
                first = 0;
                return;
            }
            if (kind == L_MENU) { // the menu fills the height, over the top screen's backdrop
                rect[0] = {0, 0, w, h};
                rect[1] = {(w - W[1]) / 2, 0, W[1], H};
                first = 0;
                return;
            }
            rect[0] = {0, 0, w, h};
            first = 0;
            if (kind == L_TOP_ONLY) { // nothing worth showing below
                shown[1] = false;
                return;
            }
            if (kind == L_MINIMAP) { // the map as a minimap in the corner (Main Menu is on X)
                // all of the bottom screen: other screens with 3D on top (e.g. name entry) look
                // the same to classify(), and cropping them hides controls from the touch screen
                float mh = H * 0.3f, mw = mh * W[1] / H;
                rect[1] = {w - mw - 6, 6, mw, mh};
                alpha[1] = 0.9f;
            } else { // L_CONTROLS(_TOP) (battles): the controls large in the corner
                float bh = H * 0.5f, bw = bh * W[1] / H;
                rect[1] = {w - bw - 6, kind == L_CONTROLS_TOP ? 6 : h - bh - 6, bw, bh};
                alpha[1] = 0.92f;
            }
            framed = true;
            return;
        }
        if (!s.single_screen) {
            shown[0] = shown[1] = true;
            first = 0;
            const float top_scale = float(s.top_size)/100, bottom_scale = float(s.bottom_size)/100;
            const float gap = float(s.screen_gap);
            if (s.dual_layout == 5) {
                rect[0] = {float(s.custom_top_x),float(s.custom_top_y),W[0]*top_scale,H*top_scale};
                rect[1] = {float(s.custom_bottom_x),float(s.custom_bottom_y),W[1]*bottom_scale,H*bottom_scale};
                if (s.swap_screens) std::swap(rect[0].x,rect[1].x),std::swap(rect[0].y,rect[1].y);
                w = std::max(rect[0].x+rect[0].w,rect[1].x+rect[1].w);
                h = std::max(rect[0].y+rect[0].h,rect[1].y+rect[1].h);
                return;
            }
            if (s.dual_layout) {
                const float k = (s.dual_layout >= 3 ? 0.60f : 1.0f)*bottom_scale;
                bool top_left = (s.dual_layout == 1 || s.dual_layout == 3) != s.swap_screens;
                w = W[0]*top_scale + gap + W[1]*k;
                h = H*std::max(top_scale,k);
                rect[0] = {top_left ? 0.0f : W[1] * k + gap, (h-H*top_scale)*.5f, W[0]*top_scale, H*top_scale};
                rect[1] = {top_left ? W[0]*top_scale + gap : 0.0f, (h-H*k)*.5f, W[1]*k, H*k};
                return;
            }
            w = std::max(W[0]*top_scale,W[1]*bottom_scale);
            h = H*(top_scale+bottom_scale)+gap;
            rect[0] = {(w-W[0]*top_scale)*.5f, s.swap_screens ? H*bottom_scale+gap : 0.0f, W[0]*top_scale,H*top_scale};
            rect[1] = {(w-W[1]*bottom_scale)*.5f, s.swap_screens ? 0.0f : H*top_scale+gap, W[1]*bottom_scale,H*bottom_scale};
            return;
        }
        int big = s.swap_screens ? 1 : 0, small = 1 - big;
        w = W[0], h = H;
        rect[big] = {(w - W[big]) / 2, 0, W[big], H};
        float k = float(s.pip_size)/100, margin = 4;
        k = std::min(k,(w-2*margin)/W[small]);
        rect[small] = {margin+(w-W[small]*k-2*margin)*float(s.pip_x)/100,margin+(h-H*k-2*margin)*float(s.pip_y)/100,W[small]*k,H*k};
        alpha[small] = float(s.pip_opacity)/100;
        shown[big] = true, shown[small] = s.picture_in_picture;
        first = big;
        framed = true;
    }

    // a window point to bottom-screen coordinates; false if it isn't on the bottom screen
    bool touch_at(float lx, float ly, float &tx, float &ty) const {
        const SDL_FRect &r = rect[1], &c = src[1];
        if (!shown[1] || lx < r.x || ly < r.y || lx >= r.x + r.w || ly >= r.y + r.h) return false;
        tx = c.x + (lx - r.x) * c.w / r.w, ty = c.y + (ly - r.y) * c.h / r.h;
        return tx >= 0 && ty >= 0 && tx < BOTTOM_WIDTH && ty < SCREEN_HEIGHT;
    }
};

int SDLCALL game_thread(void *) {
    game_start(game_path.c_str());
    return 0;
}

constexpr int TRIGGER_THRESHOLD = 16000;

uint32_t keyboard_buttons(const bool *k, int16_t &cx, int16_t &cy) {
    const Settings &s = settings();
    uint32_t b = 0;
    bool dir[4] = {};
    for (int i = 0; i < CONTROL_COUNT; i++) {
        int sc = s.keys[i];
        if (sc <= SDL_SCANCODE_UNKNOWN || sc >= SDL_SCANCODE_COUNT || !k[sc]) continue;
        if (CONTROLS[i].button) b |= CONTROLS[i].button;
        else dir[i - C_CIRCLE_UP] = true;
    }
    int x = (dir[3] ? 1 : 0) - (dir[2] ? 1 : 0), y = (dir[0] ? 1 : 0) - (dir[1] ? 1 : 0);
    if (x || y) cx = int16_t(x * CIRCLE_MAX), cy = int16_t(y * CIRCLE_MAX);
    return b;
}

bool pad_pressed(SDL_Gamepad *g, int code) {
    if (code == PAD_LEFT_TRIGGER) return SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > TRIGGER_THRESHOLD;
    if (code == PAD_RIGHT_TRIGGER) return SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > TRIGGER_THRESHOLD;
    return code >= 0 && SDL_GetGamepadButton(g, SDL_GamepadButton(code));
}

uint32_t gamepad_buttons(SDL_Gamepad *g, int16_t &cx, int16_t &cy) {
    const Settings &s = settings();
    uint32_t b = 0;
    bool dir[4] = {};
    for (int i = 0; i < CONTROL_COUNT; i++) {
        if (!pad_pressed(g, s.pad[i])) continue;
        if (CONTROLS[i].button) b |= CONTROLS[i].button;
        else dir[i - C_CIRCLE_UP] = true;
    }
    // the left stick is always the circle pad
    int ax = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX), ay = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY);
    const int deadzone = s.stick_deadzone == 18 ? 6000 : s.stick_deadzone * 32767 / 100;
    if (std::abs(ax) > deadzone || std::abs(ay) > deadzone) {
        auto axis = [&](int v) { return int16_t(std::clamp(v * CIRCLE_MAX * s.stick_sensitivity / (32767 * 100), -CIRCLE_MAX, CIRCLE_MAX)); };
        cx = axis(ax);
        cy = axis(-ay);
    }
    int x = (dir[3] ? 1 : 0) - (dir[2] ? 1 : 0), y = (dir[0] ? 1 : 0) - (dir[1] ? 1 : 0);
    if (x || y) cx = int16_t(x * CIRCLE_MAX), cy = int16_t(y * CIRCLE_MAX);
    return b;
}

SDL_ScaleMode scale_mode() {
    switch (settings().filter) {
    case FILTER_SMOOTH: return SDL_SCALEMODE_LINEAR;
    case FILTER_NEAREST: return SDL_SCALEMODE_NEAREST;
    default: return SDL_SCALEMODE_PIXELART;
    }
}

SDL_Window *fatal_window = nullptr;
void show_fatal(const char *message) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Clockwork: Yo-kai Watch 2 stopped", message, fatal_window);
}

} // namespace

void note_display_transfer(int screen) {
    if(screen>=0 && screen<2) guest_transfers[screen].fetch_add(1,std::memory_order_relaxed);
}

Input input() {
    std::lock_guard lk(input_lock);
    Input in = current_input;
    // Isolate scripted regression runs from physical keyboard/controllers
    // unless the tester explicitly requests mixed manual input.
    static const bool allow_live_test_input=std::getenv("YW2_CONTROL_ALLOW_LIVE")!=nullptr;
    if (control && !allow_live_test_input) in = {};
    input_script.apply(top_frames, in);
    control.apply(in);
    title_capture.press(top_frames, in);
    return in;
}

void play_audio(const int16_t *samples, int frames) {
    std::lock_guard lk(audio_lock);
    audio_stats.generated += frames;
    wav_dump.write(samples, frames);
    if (!audio_stream) { audio_stats.failures += frames; return; }
    int queued = SDL_GetAudioStreamQueued(audio_stream);
    if (queued < 0) { audio_stats.failures += frames; return; }
    audio_stats.max_queue = std::max(audio_stats.max_queue, queued);
    if (queued > max_queued_bytes) { audio_stats.dropped += frames; return; }
    std::vector<int16_t> mixed;
    if (audio_mono) { // both sides the average
        mixed.assign(samples, samples + frames * 2);
        for (int i = 0; i < frames; i++)
            mixed[i * 2] = mixed[i * 2 + 1] = int16_t((int(mixed[i * 2]) + int(mixed[i * 2 + 1])) / 2);
        samples = mixed.data();
    }
    if (audio_playing && queued == 0) { // ran dry: refill before resuming
        ++audio_stats.underruns;
        SDL_PauseAudioStreamDevice(audio_stream);
        audio_playing = false;
    }
    if (!SDL_PutAudioStreamData(audio_stream, samples, frames * 4)) { audio_stats.failures += frames; return; }
    audio_stats.submitted += frames;
    if (!audio_playing && queued + frames * 4 >= start_queued_bytes) {
        SDL_ResumeAudioStreamDevice(audio_stream);
        audio_playing = true;
    }
}

void present(int screen, const uint8_t *rgba, int width, int height) {
    if (screen == 0) {
        // YW2_TRACE_FROM=<frame>: turns on GPU draw tracing at a frame (to inspect one scene)
        static const int trace_from = std::getenv("YW2_TRACE_FROM") ? std::atoi(std::getenv("YW2_TRACE_FROM")) : -1;
        if (++top_frames == trace_from) log_cfg::gpu = true;
        if (trace_from >= 0 && top_frames == trace_from + 12) log_cfg::gpu = false;
    }
    frame_dump.frame(screen, rgba, width, height);
    if (screen == 0) title_capture.frame(top_frames, rgba, width, height);
    std::lock_guard lk(frame_lock);
    frames[screen].assign(rgba, rgba + size_t(width) * height * 4);
    frame_width[screen] = width, frame_height[screen] = height;
    gpu_frame[screen] = nullptr;
    gpu_frame_generation[screen] = 0;
    gpu_frame_midpoint[screen] = false;
    frame_dirty[screen] = true;
}

void present_gpu(int screen, SDL_GPUTexture *texture, int width, int height, uint64_t generation, bool midpoint) {
    if (screen == 0) {
        static const int trace_from = std::getenv("YW2_TRACE_FROM") ? std::atoi(std::getenv("YW2_TRACE_FROM")) : -1;
        if (++top_frames == trace_from) log_cfg::gpu = true;
        if (trace_from >= 0 && top_frames == trace_from + 12) log_cfg::gpu = false;
    }
    std::lock_guard lk(frame_lock);
    gpu_frame[screen] = texture;
    gpu_frame_generation[screen] = generation;
    gpu_frame_midpoint[screen] = midpoint;
    frame_width[screen] = width, frame_height[screen] = height;
    frame_dirty[screen] = true;
}

void retire_gpu_frame(SDL_GPUTexture *texture) {
    if (!texture) return;
    std::lock_guard lk(frame_lock);
    retired_gpu_frames.push_back(texture);
}

// a dump this executable can't play: hand it to the one that can, or say why not (false)
bool hand_off_or_explain(const std::string &dump_path, std::string *problem) {
    Route r = route(dump_path);
    if (r.kind == Route::HERE) return true;
    if (r.kind == Route::OTHER) {
        play_in(r.binary.c_str(), dump_path); // returns only on failure
        *problem = "Could not start " + r.binary + ".";
        return false;
    }
    *problem = route_problem(r);
    return false;
}

void run(void (*start)(const char *dump), const char *cli_dump, bool force_launcher, bool play) {
    game_start = start;
    // SDL's internal checks (on in some distributions' builds of SDL) go to the log instead of
    // stopping everything with a dialog: the window's renderer trips one for a frame whenever the
    // window is resized under it ("Viewport size exceeds current render target dimensions")
    SDL_SetAssertionHandler(
        [](const SDL_AssertData *d, void *) {
            LOG("SDL assertion: %s (%s:%d, %s)", d->condition, d->filename, d->linenum, d->function);
            return SDL_ASSERTION_ALWAYS_IGNORE;
        },
        nullptr);
    Settings &s = settings();
    if (std::getenv("YW2_HEADLESS")) {
        if (!cli_dump) fatal("headless mode needs the dump path on the command line");
        if (std::string problem; !hand_off_or_explain(cli_dump, &problem)) fatal("%s", problem.c_str());
        if (std::getenv("YW2_WIDESCREEN")) pica::set_widescreen(true);
        if (const char *scale = std::getenv("YW2_SCALE")) pica::set_resolution_scale(std::atoi(scale));
        start(cli_dump);
        std::_Exit(0);
    }
    SDL_SetAppMetadata("Yo-kai Watch 2 Recompiled", nullptr, "io.yw2recompiled");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        LOG("no window (%s); running headless", SDL_GetError());
        if (!cli_dump) fatal("no window and no dump path given");
        start(cli_dump);
        std::_Exit(0);
    }
    s.load();
    if (cli_dump) {
        std::error_code ec;
        auto absolute = std::filesystem::absolute(cli_dump, ec);
        s.game_path = ec ? cli_dump : absolute.string();
    }

    Layout layout;
    layout.update();
    // start at about 85% of the desktop (the image scales with the window)
    float scale = 2.0f;
    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable))
        scale = std::clamp(std::min(float(usable.w) / layout.w, float(usable.h) / layout.h) * 0.85f, 1.0f, 6.0f);
    const bool explicit_window = s.window_width >= 320 && s.window_height >= 240;
    SDL_Window *window = SDL_CreateWindow("Yo-kai Watch 2 Recompiled", explicit_window ? s.window_width : int(layout.w * scale),
                                          explicit_window ? s.window_height : int(layout.h * scale),
                                          SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) fatal("cannot create window: %s", SDL_GetError());
    auto display_by_index = [](int index) {
        int count = 0;
        SDL_DisplayID *ids = SDL_GetDisplays(&count);
        SDL_DisplayID id = ids && index >= 0 && index < count ? ids[index] : SDL_GetPrimaryDisplay();
        SDL_free(ids);
        return id;
    };
    auto place_on_display = [&](SDL_Window *target,int index) {
        SDL_Rect bounds{};
        if (!SDL_GetDisplayUsableBounds(display_by_index(index), &bounds)) return;
        int w,h; SDL_GetWindowSize(target,&w,&h);
        SDL_SetWindowPosition(target,bounds.x+std::max(0,(bounds.w-w)/2),bounds.y+std::max(0,(bounds.h-h)/2));
    };
    place_on_display(window,s.main_display);
    // with the GPU renderer, the window's renderer shares its device, so GPU-rendered frames are
    // shown without copies
    SDL_Renderer *renderer = nullptr;
    SDL_GPUDevice *device = nullptr;
    auto create_renderer = [&] {
        if (s.gpu_renderer) {
            SDL_PropertiesID props = SDL_CreateProperties();
            SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
            SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_PREFERLOWPOWER_BOOLEAN, s.prefer_integrated_gpu);
            device = SDL_CreateGPUDeviceWithProperties(props);
            SDL_DestroyProperties(props);
            if (device) { // which GPU and driver: the first thing to check when it runs slowly
                SDL_PropertiesID p = SDL_GetGPUDeviceProperties(device);
                const char *name = SDL_GetStringProperty(p, SDL_PROP_GPU_DEVICE_NAME_STRING, "unknown GPU");
                const char *driver = SDL_GetStringProperty(p, SDL_PROP_GPU_DEVICE_DRIVER_NAME_STRING, "");
                const char *version = SDL_GetStringProperty(p, SDL_PROP_GPU_DEVICE_DRIVER_VERSION_STRING, "");
                gpu_description = std::string(name) + " (" + driver + (*version ? " " : "") + version + ")";
                LOG("gpu: %s", gpu_description.c_str());
            }
            if (device && !(renderer = SDL_CreateGPURenderer(device, window))) {
                SDL_DestroyGPUDevice(device);
                device = nullptr;
            }
            if (!device) {
                // the shaders are SPIR-V, which only SDL's Vulkan backend takes: an SDL built
                // without Vulkan (vcpkg's sdl3 without [vulkan]) has no backend for them at all
                bool vulkan = false;
                for (int i = 0; i < SDL_GetNumGPUDrivers(); i++)
                    vulkan |= std::string_view(SDL_GetGPUDriver(i)) == "vulkan";
                LOG("no GPU device for the GPU renderer (%s%s); drawing on the CPU", SDL_GetError(),
                    vulkan ? "" : "; this SDL3 was built without Vulkan, which the GPU renderer needs");
            }
        }
        if (!renderer && !(renderer = SDL_CreateRenderer(window, nullptr)))
            fatal("cannot create a renderer: %s", SDL_GetError());
    };
    create_renderer();
    fatal_window = window;
    fatal_hook = show_fatal;
    SDL_SetRenderVSync(renderer, 1);

    // the launcher, unless turned off (Shift at startup or --launcher brings it back)
    bool shift = SDL_GetModState() & SDL_KMOD_SHIFT;
    bool have_dump = !s.game_path.empty() && std::filesystem::is_regular_file(s.game_path);
    bool no_launcher = play || std::getenv("YW2_NO_LAUNCHER") != nullptr; // --play, unattended test runs
    bool launcher = !no_launcher && (s.show_launcher || force_launcher || shift || !have_dump);
    for (;;) { // until a dump this executable plays is chosen
        if (launcher && !run_launcher(window, renderer, gpu_description)) std::_Exit(0);
        std::string problem;
        if (hand_off_or_explain(s.game_path, &problem)) break;
        LOG("%s", problem.c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Clockwork", problem.c_str(), window);
        if (no_launcher) std::_Exit(1);
        launcher = true;
    }
    game_path = s.game_path;
    std::string game_id; // for its layouts
    if (auto id = peek_program_id(game_path))
        if (const KnownDump *d = find_dump(*id)) game_id = d->game;
    if (s.gpu_renderer != (device != nullptr)) { // changed in the launcher
        SDL_DestroyRenderer(renderer);
        renderer = nullptr;
        if (device) SDL_DestroyGPUDevice(device), device = nullptr;
        create_renderer();
    }
    pica::configure_worker_profile(s.performance_profile);
    // The dedicated Psychic Specters port uses original textures only.
    // Ignore HD settings left behind by older launchers.
    s.hd_textures=false;
    pica::gpu::configure_options(s.performance_profile,s.texture_cache_mib,s.prewarm_pipelines,false,s.texture_sampling,s.fxaa,s.sharpening);
    if (device && !pica::gpu::init(device)) LOG("GPU renderer unavailable; drawing on the CPU");

    // apply the settings
    SDL_SetRenderVSync(renderer, s.vsync ? 1 : 0);
    auto apply_window_mode = [&] {
        SDL_SetWindowFullscreen(window, false);
        SDL_SetWindowFullscreenMode(window, nullptr);
        if (s.window_mode == 2) {
            SDL_DisplayMode chosen{};
            const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
            const SDL_DisplayMode *desktop = SDL_GetDesktopDisplayMode(display);
            const int w = s.window_width >= 320 ? s.window_width : desktop ? desktop->w : 1920;
            const int h = s.window_height >= 240 ? s.window_height : desktop ? desktop->h : 1080;
            if (SDL_GetClosestFullscreenDisplayMode(display, w, h, 0.0f, true, &chosen))
                SDL_SetWindowFullscreenMode(window, &chosen);
            else LOG("display: exclusive mode unavailable, using borderless fullscreen");
        }
        s.fullscreen = s.window_mode != 0;
        SDL_SetWindowFullscreen(window, s.fullscreen);
    };
    apply_window_mode();
    auto apply_aspect = [&] {
        auto [w,h] = s.aspect_ratio();
        if (!pica::gpu::active() && w * 3 < h * 5) {
            LOG("display: vertical expansion requires Vulkan; keeping native aspect on software fallback");
            w=5; h=3;
        }
        pica::set_aspect_ratio(w,h);
        s.widescreen = s.aspect_mode != 0;
        LOG("display: selected aspect %d:%d", w,h);
    };
    if (const char *mode = std::getenv("YW2_ASPECT_MODE")) s.aspect_mode = std::clamp(std::atoi(mode),0,5);
    apply_aspect();
    pica::set_resolution_scale(pica::gpu::active() ? s.resolution : std::min(4,s.resolution));
    pica::set_hud_scale(float(s.hud_size) / 100.0f);
    pica::set_camera_zoom(float(s.camera_zoom) / 100.0f);
    pica::set_frame_interpolation(s.frame_interpolation);
    bool ai = s.ai_upscale;
    if (const char *e = std::getenv("YW2_AI_UPSCALE")) ai = std::atoi(e) != 0; // test override
    upscale::configure(ai && pica::gpu::active() && s.hd_textures, s.ai_upscale_factor);
    if (const char *e = std::getenv("YW2_INTERP")) pica::set_frame_interpolation(std::atoi(e) != 0);
    if (const char *e = std::getenv("YW2_ZOOM")) pica::set_camera_zoom(float(std::atoi(e)) / 100.0f);
    if (const char *e = std::getenv("YW2_HUD")) pica::set_hud_scale(float(std::atoi(e)) / 100.0f);
    if (const char *e = std::getenv("YW2_SCALE")) pica::set_resolution_scale(pica::gpu::active() ? std::atoi(e) : std::min(4,std::atoi(e))); // test overrides
    if (!std::getenv("YW2_ASPECT_MODE")) if (const char *e = std::getenv("YW2_WIDESCREEN")) pica::set_widescreen(std::atoi(e) != 0);
    open_audio();
    if (audio_stream) SDL_SetAudioStreamGain(audio_stream, float(s.volume) / 100.0f);
    layout.game = game_id;
    layout.update();
    // An independent lower screen has its own window and swapchain, but
    // samples the same original LCD GPU textures. No CPU readback is needed.
    SDL_Window *secondary_window = nullptr;
    SDL_Renderer *secondary_renderer = nullptr;
    if (s.detached_screen) {
        secondary_window = SDL_CreateWindow("Yo-kai Watch 2 - Touch screen",640,480,SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (secondary_window) {
            place_on_display(secondary_window,s.detached_display);
            secondary_renderer = device ? SDL_CreateGPURenderer(device,secondary_window) : SDL_CreateRenderer(secondary_window,nullptr);
        }
        if (!secondary_renderer) {
            LOG("display: separate window unavailable (%s); reverting to stacked",SDL_GetError());
            if (secondary_window) SDL_DestroyWindow(secondary_window);
            secondary_window = nullptr;
            s.detached_screen = false;
            layout.update();
        } else {
            SDL_SetRenderVSync(secondary_renderer,0); // main presentation controls cadence
            if (s.detached_fullscreen && (!s.fullscreen || SDL_GetDisplayForWindow(window) != SDL_GetDisplayForWindow(secondary_window)))
                SDL_SetWindowFullscreen(secondary_window,true);
            LOG("display: separate screen window created on display %u",unsigned(SDL_GetDisplayForWindow(secondary_window)));
        }
    }
    SDL_SetRenderLogicalPresentation(renderer, int(layout.w), int(layout.h), SDL_LOGICAL_PRESENTATION_LETTERBOX);
    LOG("window: %s renderer, %.2fx scale", SDL_GetRendererName(renderer), double(scale));
    // a line of text over the game for a moment (F5 says which layout it chose)
    std::string message;
    uint64_t message_until = 0;
    auto show_message = [&](std::string text) { message = std::move(text), message_until = SDL_GetTicks() + 2500; };
    // applies a layout change, keeping the window's width and fitting its height to the new shape
    auto relayout = [&](bool persist = false) {
        float old_h = layout.h;
        layout.update();
        // Scene classification and the game's changing framebuffer sizes can
        // relayout frequently. Only user-initiated settings changes hit disk.
        if (persist) s.save();
        SDL_SetRenderLogicalPresentation(renderer, int(layout.w), int(layout.h), SDL_LOGICAL_PRESENTATION_LETTERBOX);
        if (secondary_renderer && s.detached_screen) {
            int other = 1-layout.first;
            SDL_SetRenderLogicalPresentation(secondary_renderer,other == 0 ? int(layout.top_width) : BOTTOM_WIDTH,SCREEN_HEIGHT,SDL_LOGICAL_PRESENTATION_LETTERBOX);
            SDL_SetWindowTitle(window,layout.first == 0 ? "Yo-kai Watch 2 - Top screen" : "Yo-kai Watch 2 - Touch screen");
            SDL_SetWindowTitle(secondary_window,other == 0 ? "Yo-kai Watch 2 - Top screen" : "Yo-kai Watch 2 - Touch screen");
        } else SDL_SetWindowTitle(window,"Yo-kai Watch 2 Recompiled");
        if (old_h != layout.h && !explicit_window && !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN)) {
            int ww, wh;
            SDL_GetWindowSize(window, &ww, &wh);
            SDL_SetWindowSize(window, ww, int(float(ww) * layout.h / layout.w));
        }
        if (std::getenv("YW2_VERIFY_LAYOUT")) {
            // Exercise the same logical-to-window transform (including
            // high-DPI letterboxing) and touch hit test used by real input.
            SDL_Renderer *target=renderer;
            SDL_FRect bottom=layout.rect[1];
            bool separate_bottom=s.detached_screen && secondary_renderer && layout.first==0;
            if (separate_bottom) {target=secondary_renderer; bottom={0,0,float(BOTTOM_WIDTH),float(SCREEN_HEIGHT)};}
            if (layout.shown[1] || separate_bottom) {
                for (const auto &p : std::array<std::pair<float,float>,3>{{{2,2},{160,120},{317,237}}}) {
                    float wx,wy,lx,ly,tx=0,ty=0;
                    float x=bottom.x+p.first*bottom.w/BOTTOM_WIDTH,y=bottom.y+p.second*bottom.h/SCREEN_HEIGHT;
                    if (!SDL_RenderCoordinatesToWindow(target,x,y,&wx,&wy) || !SDL_RenderCoordinatesFromWindow(target,wx,wy,&lx,&ly))
                        fatal("touch layout coordinate conversion failed");
                    bool valid=separate_bottom ? (tx=lx,ty=ly,true) : layout.touch_at(lx,ly,tx,ty);
                    if (!valid || std::abs(tx-p.first)>1.0f || std::abs(ty-p.second)>1.0f)
                        fatal("touch layout mismatch at %.1f,%.1f -> %.1f,%.1f",p.first,p.second,tx,ty);
                }
                LOG("display: verified three window-to-touch points (%s)",separate_bottom?"secondary":"main");
            }
        }
    };
    relayout();
    GameMenu game_menu(window);
    auto sync_menu=[&]{game_menu.sync(s,pica::gpu::active(),secondary_window!=nullptr);};
    sync_menu();

    // One path maps both real window mouse clicks and test-injected points.
    // An ordinary click never becomes a touch when the lower display is hidden.
    auto map_window_touch=[&](SDL_Window *focus,float mx,float my,Input &in) {
        float lx=0,ly=0,tx=0,ty=0;
        if(s.detached_screen&&secondary_renderer&&focus==secondary_window&&layout.first==0){
            if(!SDL_RenderCoordinatesFromWindow(secondary_renderer,mx,my,&lx,&ly))return false;
            if(lx<0||ly<0||lx>=BOTTOM_WIDTH||ly>=SCREEN_HEIGHT)return false;
            tx=lx;ty=ly;
        }else if(focus==window){
            if(!SDL_RenderCoordinatesFromWindow(renderer,mx,my,&lx,&ly)||!layout.touch_at(lx,ly,tx,ty))return false;
        }else return false;
        in.touching=true;in.touch_x=uint16_t(tx);in.touch_y=uint16_t(ty);return true;
    };
    const std::function<bool(int,int,Input&)> map_guest_test_touch=[&](int x,int y,Input &in){
        if(x<0||y<0||x>=BOTTOM_WIDTH||y>=SCREEN_HEIGHT)return false;
        const bool separate=s.detached_screen&&secondary_renderer&&layout.first==0;
        if(!separate&&!layout.shown[1])return false;
        SDL_Renderer *target=separate?secondary_renderer:renderer;
        SDL_Window *target_window=separate?secondary_window:window;
        SDL_FRect rect=separate?SDL_FRect{0,0,float(BOTTOM_WIDTH),float(SCREEN_HEIGHT)}:layout.rect[1];
        SDL_FRect src=separate?SDL_FRect{0,0,float(BOTTOM_WIDTH),float(SCREEN_HEIGHT)}:layout.src[1];
        float lx=rect.x+(float(x)+0.5f-src.x)*rect.w/src.w,ly=rect.y+(float(y)+0.5f-src.y)*rect.h/src.h,wx,wy;
        if(!SDL_RenderCoordinatesToWindow(target,lx,ly,&wx,&wy)||!map_window_touch(target_window,wx,wy,in))return false;
        return std::abs(int(in.touch_x)-x)<=1&&std::abs(int(in.touch_y)-y)<=1;
    };

    SDL_Texture *textures[2], *secondary_textures[2] = {nullptr,nullptr};
    for (int i = 0; i < 2; i++) {
        textures[i] = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, WIDTHS[i],
                                        SCREEN_HEIGHT);
        if (!textures[i]) fatal("cannot create screen texture: %s", SDL_GetError());
        SDL_SetTextureScaleMode(textures[i], scale_mode());
        if (secondary_renderer) {
            secondary_textures[i] = SDL_CreateTexture(secondary_renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STREAMING,WIDTHS[i],SCREEN_HEIGHT);
            if (!secondary_textures[i]) fatal("cannot create second-screen texture: %s",SDL_GetError());
            SDL_SetTextureScaleMode(secondary_textures[i],scale_mode());
        }
    }

    // the game gets a large host stack: recompiled code nests as deeply as the guest's calls
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER, reinterpret_cast<void *>(game_thread));
    SDL_SetStringProperty(props, SDL_PROP_THREAD_CREATE_NAME_STRING, "game");
    SDL_SetNumberProperty(props, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER, 64 << 20);
    if (!SDL_CreateThreadWithProperties(props)) fatal("cannot start the game thread: %s", SDL_GetError());
    SDL_DestroyProperties(props);

    bool capture_requested = false;
    int burst_left = 0; // Shift+F12: frames still to save, one after another
    std::filesystem::path burst_dir;
    // renderer textures wrapping the GPU renderer's frame textures
    // and the size each was made for: the GPU renderer remakes its frame textures when a frame's
    // size changes, and a new one can come back at a freed one's address; shown with the old
    // size, only its top-left part filled the screen
    struct Wrapped {
        SDL_Texture *tex = nullptr;
        int w = 0, h = 0;
    };
    std::map<SDL_GPUTexture *, Wrapped> wrapped, secondary_wrapped;
    SDL_Texture *shown_gpu[2] = {nullptr, nullptr};
    SDL_GPUTexture *shown_gpu_ptr[2] = {nullptr, nullptr};
    SDL_Texture *secondary_shown[2] = {nullptr,nullptr};
    SDL_GPUTexture *secondary_shown_ptr[2] = {nullptr,nullptr};
    auto gpu_texture = [&](SDL_Renderer *target,std::map<SDL_GPUTexture *,Wrapped> &cache,SDL_GPUTexture *t,int w,int h) {
        Wrapped &wr = cache[t];
        if (wr.tex && (wr.w != w || wr.h != h)) SDL_DestroyTexture(wr.tex), wr.tex = nullptr;
        SDL_Texture *&tex = wr.tex;
        if (!tex) {
            wr.w = w, wr.h = h;
            SDL_PropertiesID p = SDL_CreateProperties();
            SDL_SetPointerProperty(p, SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER, t);
            SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_RGBA32);
            SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_TARGET);
            SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, w);
            SDL_SetNumberProperty(p, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, h);
            tex = SDL_CreateTextureWithProperties(target, p);
            SDL_DestroyProperties(p);
            if (!tex) LOG("cannot show a GPU frame: %s", SDL_GetError());
            else SDL_SetTextureScaleMode(tex, scale_mode());
        }
        return tex;
    };

    uint64_t last_scene = 0;
    Context candidate = CTX_OTHER;
    int candidate_frames = 0;

    std::vector<SDL_Gamepad *> pads;
    int count = 0; // controllers already connected (the launcher consumed their "added" events)
    if (SDL_JoystickID *ids = SDL_GetGamepads(&count)) {
        for (int j = 0; j < count; j++)
            if (SDL_Gamepad *g = SDL_OpenGamepad(ids[j])) pads.push_back(g);
        SDL_free(ids);
    }
    // Optional test-only host presentation telemetry. top_frames counts
    // guest VBlank screen publications (not necessarily a new guest image).
    std::ofstream frame_times;
    if (const char *file = std::getenv("YW2_FRAME_TIMES")) {
        frame_times.open(file);
        frame_times << "present_ns,interval_ns,guest_vblank_publications,guest_top_transfers,guest_bottom_transfers,top_gpu_generation,top_gpu_midpoint,top_gpu_new_shown,top_gpu_mids_shown\n";
    }
    std::ofstream resource_times;
    if (const char *file = std::getenv("YW2_RESOURCE_TIMES")) {
        resource_times.open(file);
        resource_times << "time_ns,working_set_bytes,private_bytes,cpu_kernel_100ns,cpu_user_100ns,gpu_local_bytes,gpu_local_budget,gpu_nonlocal_bytes,gpu_nonlocal_budget,owned_texture_payload_bytes\n";
    }
    uint64_t last_resource = 0;
    std::ofstream audio_times;
    if (const char *file = std::getenv("YW2_AUDIO_TIMES")) {
        audio_times.open(file);
        audio_times << "time_ns,guest_vblank_publications,generated_frames,submitted_frames,dropped_frames,underruns,failed_frames,queued_bytes,peak_queue_bytes\n";
    }
    uint64_t last_audio_sample = 0;
    auto sample_audio = [&](uint64_t now) {
        if (!audio_times.is_open()) return;
        AudioStats stats;
        int queued;
        {
            std::lock_guard lk(audio_lock);
            stats = audio_stats;
            queued = audio_stream ? SDL_GetAudioStreamQueued(audio_stream) : -1;
        }
        audio_times << now << ',' << top_frames.load() << ',' << stats.generated << ',' << stats.submitted << ','
                    << stats.dropped << ',' << stats.underruns << ',' << stats.failures << ',' << queued << ',' << stats.max_queue << '\n';
        audio_times.flush();
    };
    auto flush_telemetry = [&] {
        if (frame_times.is_open()) frame_times.flush();
        if (resource_times.is_open()) resource_times.flush();
        sample_audio(SDL_GetTicksNS());
        std::lock_guard lk(audio_lock);
        wav_dump.finish();
    };
    int cap = s.frame_limit;
    if (const char *limit = std::getenv("YW2_FRAME_LIMIT")) cap = std::atoi(limit);
    if (cap != 30 && cap != 60) cap = 0;
    uint64_t period = cap ? 1000000000ull / static_cast<unsigned>(cap) : 0;
    uint64_t previous_present = 0;
    uint64_t next_present_deadline = 0;
    unsigned present_count = 0;
    unsigned stats_count = 0;
    uint64_t stats_since = SDL_GetTicksNS();
    int stats_guest = top_frames.load();
    uint64_t stats_transfers = guest_transfers[0].load(), stats_cpu=0;
    uint64_t shown_top_generation=0,last_top_generation=0,new_top_shown=0,mids_shown=0,stats_new_top=0;
    bool shown_top_midpoint=false;
    char stats_text[128] = "Measuring display...", stats_detail[128]="", stats_gpu[128]="";
#if defined(_WIN32)
    GpuMemoryTelemetry gpu_memory;
    if (s.show_stats || resource_times.is_open()) gpu_memory.init(device);
    DXGI_QUERY_VIDEO_MEMORY_INFO local_memory{}, nonlocal_memory{};
    bool has_local=false,has_nonlocal=false;
    uint64_t memory_sample_time=0;
    auto query_gpu_memory = [&](uint64_t now) {
        if (memory_sample_time && now-memory_sample_time<900000000ull) return;
        gpu_memory.init(device);
        has_local=gpu_memory.query(DXGI_MEMORY_SEGMENT_GROUP_LOCAL,local_memory);
        has_nonlocal=gpu_memory.query(DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,nonlocal_memory);
        memory_sample_time=now;
    };
#endif
    for (;;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if(e.type==GameMenu::event_type()){
                const int command=e.user.code;
                bool handled=true;
                if(command>GameMenu::Scale && command<=GameMenu::Scale+10){
                    s.resolution=command-GameMenu::Scale;
                    if(!pica::gpu::active())s.resolution=std::min(4,s.resolution);
                    pica::set_resolution_scale(s.resolution);
                    show_message("Upscaling: "+std::to_string(s.resolution)+"x");
                }else if(command>=GameMenu::Aspect && command<=GameMenu::Aspect+5){
                    s.aspect_mode=command-GameMenu::Aspect;apply_aspect();
                }else if(command>=GameMenu::Layout && command<=GameMenu::Layout+9){
                    int l=command-GameMenu::Layout;bool separate=l==6&&secondary_window;
                    if(l==6&&!secondary_window)handled=false;
                    else{
                        s.detached_screen=separate;s.single_screen=l>=7;
                        s.smart_layout=l==7;s.picture_in_picture=l==7||l==9;
                        if(l<=5)s.dual_layout=l;
                        s.swap_screens=false;
                        if(secondary_window){if(separate)SDL_ShowWindow(secondary_window);else SDL_HideWindow(secondary_window);}
                        relayout();
                    }
                }else if(command==GameMenu::Swap){s.swap_screens=!s.swap_screens;relayout();}
                else if(command==GameMenu::Fullscreen){s.window_mode=s.fullscreen?0:1;apply_window_mode();}
                else if(command==GameMenu::Stats){s.show_stats=!s.show_stats;}
                else if(command>=GameMenu::Fps&&command<=GameMenu::Fps+2){
                    int mode=command-GameMenu::Fps;
                    if(mode==2&&!pica::gpu::active())handled=false;
                    else{
                        s.frame_limit=mode==2?60:mode==1?30:0;
                        s.frame_interpolation=mode==2;
                        pica::set_frame_interpolation(s.frame_interpolation);
                        cap=s.frame_limit;
                        period=cap?1000000000ull/static_cast<unsigned>(cap):0;
                        next_present_deadline=0;
                        show_message(mode==2?"60 FPS (Interpolation)":mode==1?"30 FPS":"Original timing");
                    }
                }else handled=false;
                if(handled){s.save();sync_menu();LOG("menu: applied %d resolution=%d aspect=%d layout=%d hd=%d fps=%d interpolation=%d",command,s.resolution,s.aspect_mode,s.dual_layout,int(s.hd_textures),s.frame_limit,int(s.frame_interpolation));}
                continue;
            }
            switch (e.type) {
            case SDL_EVENT_QUIT:
                flush_telemetry();
                std::_Exit(0);
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (secondary_window && e.window.windowID == SDL_GetWindowID(secondary_window)) {
                    SDL_HideWindow(secondary_window); s.detached_screen=false; s.single_screen=false; relayout(true);
                } else if (e.window.windowID == SDL_GetWindowID(window)) {
                    flush_telemetry();
                    std::_Exit(0);
                }
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
                if (std::none_of(pads.begin(), pads.end(), [&](SDL_Gamepad *g) { return SDL_GetGamepadID(g) == e.gdevice.which; }))
                    if (SDL_Gamepad *g = SDL_OpenGamepad(e.gdevice.which)) pads.push_back(g);
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                std::erase_if(pads, [&](SDL_Gamepad *g) {
                    if (SDL_GetGamepadID(g) != e.gdevice.which) return false;
                    SDL_CloseGamepad(g);
                    return true;
                });
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                if (s.mute_in_background && audio_stream) SDL_SetAudioStreamGain(audio_stream, 0);
                break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                if (audio_stream) SDL_SetAudioStreamGain(audio_stream, float(s.volume) / 100.0f);
                break;
            case SDL_EVENT_KEY_DOWN:
                if (e.key.repeat) break;
                if (e.key.scancode == SDL_SCANCODE_F11) {
                    s.window_mode = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) ? 0 : 1;
                    apply_window_mode();
                    s.save();
                }
                if (e.key.scancode == SDL_SCANCODE_F9) {
                    if (s.detached_screen && secondary_window) { SDL_HideWindow(secondary_window); s.detached_screen=false; s.single_screen=true; }
                    else s.single_screen = !s.single_screen;
                    relayout(true);
                }
                if (e.key.scancode == SDL_SCANCODE_F3 && secondary_window) {
                    s.detached_screen = !s.detached_screen;
                    if (s.detached_screen) SDL_ShowWindow(secondary_window); else SDL_HideWindow(secondary_window);
                    relayout(true);
                }
                if (e.key.scancode == SDL_SCANCODE_F4) { s.show_stats = !s.show_stats; s.save(); }
                if (e.key.scancode == SDL_SCANCODE_F10)
                    show_message(pica::toggle_texture_dump() ? "Saving textures to dump/textures (F10 to stop)"
                                                             : "Stopped saving textures");
                if (e.key.scancode == SDL_SCANCODE_TAB) s.swap_screens = !s.swap_screens, relayout(true);
                if (e.key.scancode == SDL_SCANCODE_F8) s.picture_in_picture = !s.picture_in_picture, relayout(true);
                if (e.key.scancode == SDL_SCANCODE_F6) s.smart_layout = !s.smart_layout, relayout(true);
                if (e.key.scancode == SDL_SCANCODE_F5) { // the next layout for this kind of scene, in this game
                    if (!s.single_screen || !s.smart_layout || s.swap_screens) {
                        show_message("F5 changes adaptive one-screen layouts (F9 one screen, F6 adaptive)");
                    } else {
                        Context c = layout.context;
                        int next = (layout_for(game_id, c) + 1) % LAYOUT_COUNT;
                        s.layouts[game_id + "." + CONTEXT_NAMES[c]] = LAYOUT_IDS[next];
                        relayout(true);
                        show_message(std::string(CONTEXT_LABELS[c]) + ": " + LAYOUT_LABELS[next]);
                    }
                }
                if (e.key.scancode == SDL_SCANCODE_F12 && (e.key.mod & SDL_KMOD_SHIFT)) { // a burst: animations
                    burst_dir = platform::data_dir() / "captures" / ("burst" + std::to_string(SDL_GetTicks()));
                    std::error_code ec;
                    std::filesystem::create_directories(burst_dir, ec);
                    burst_left = 90;
                    pica::request_draw_burst(burst_dir.string(), 60);
                } else if (e.key.scancode == SDL_SCANCODE_F12) {
                    capture_requested = true;
                }
                if (e.key.scancode == SDL_SCANCODE_F7) {
                    s.aspect_mode = (s.aspect_mode + 1) % 5;
                    apply_aspect();
                    static const char *names[] = {"Original (5:3)","16:9","16:10","21:9","32:9 (experimental)"};
                    show_message(names[s.aspect_mode]);
                    s.save();
                }
                sync_menu();
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_STICK) s.swap_screens = !s.swap_screens, relayout(true), sync_menu();
                break;
            default: break;
            }
        }

        // smart layout: follow what the game shows, once it has shown it for a few frames
        if (s.single_screen && s.smart_layout) {
            pica::SceneInfo scene = pica::scene_info();
            if (scene.serial != last_scene) {
                last_scene = scene.serial;
                Context c = classify(scene);
                candidate_frames = c == candidate ? candidate_frames + 1 : 0;
                candidate = c;
                // a battle's bottom screen sometimes looks like something else for a moment: leaving
                // one takes longer (about 1.5 s; two scenes a frame, top and bottom)
                int needed = layout.context == CTX_BATTLE ? 90 : 8;
                if (candidate_frames >= needed && c != layout.context) {
                    layout.context = c;
                    LOG("layout: %s scene, %s", CONTEXT_NAMES[c], LAYOUT_IDS[layout_for(game_id, c)]);
                    relayout();
                }
            }
        }

        Input in;
        in.buttons = keyboard_buttons(SDL_GetKeyboardState(nullptr), in.circle_x, in.circle_y);
        for (SDL_Gamepad *g : pads) in.buttons |= gamepad_buttons(g, in.circle_x, in.circle_y);
        float mx, my;
        if (SDL_GetMouseState(&mx, &my) & SDL_BUTTON_LMASK) {
            map_window_touch(SDL_GetMouseFocus(),mx,my,in);
        }
        {
            std::lock_guard lk(input_lock);
            current_input = in;
        }

        {
            std::lock_guard lk(frame_lock);
            for (int i = 0; i < 2; i++) {
                if (!frame_dirty[i]) continue;
                if (gpu_frame[i]) { // shown directly; just track its size for the layout
                    float logical = float(frame_width[i]) * SCREEN_HEIGHT / float(frame_height[i]);
                    if (i == 0 && logical != layout.top_width) layout.top_width = logical, relayout();
                    shown_gpu[i] = gpu_texture(renderer,wrapped,gpu_frame[i], frame_width[i], frame_height[i]);
                    shown_gpu_ptr[i] = shown_gpu[i] ? gpu_frame[i] : nullptr;
                    if (secondary_renderer) {
                        secondary_shown[i]=gpu_texture(secondary_renderer,secondary_wrapped,gpu_frame[i],frame_width[i],frame_height[i]);
                        secondary_shown_ptr[i]=secondary_shown[i]?gpu_frame[i]:nullptr;
                    }
                    if (i == 0) {
                        shown_top_generation = shown_gpu[i] || secondary_shown[i] ? gpu_frame_generation[i] : 0;
                        shown_top_midpoint = shown_top_generation && gpu_frame_midpoint[i];
                    }
                    frame_dirty[i] = false;
                    continue;
                }
                shown_gpu[i] = nullptr;
                if (i == 0) { shown_top_generation=0; shown_top_midpoint=false; }
                shown_gpu_ptr[i] = nullptr;
                secondary_shown[i]=nullptr; secondary_shown_ptr[i]=nullptr;
                if (textures[i]->w != frame_width[i] || textures[i]->h != frame_height[i]) { // new size
                    SDL_DestroyTexture(textures[i]);
                    textures[i] = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                                    frame_width[i], frame_height[i]);
                    SDL_SetTextureScaleMode(textures[i], scale_mode());
                    // the layout is in native-resolution units
                    if (i == 0) layout.top_width = float(frame_width[0]) * SCREEN_HEIGHT / float(frame_height[0]), relayout();
                }
                SDL_UpdateTexture(textures[i], nullptr, frames[i].data(), frame_width[i] * 4);
                if (secondary_renderer) {
                    if (secondary_textures[i]->w!=frame_width[i] || secondary_textures[i]->h!=frame_height[i]) {
                        SDL_DestroyTexture(secondary_textures[i]);
                        secondary_textures[i]=SDL_CreateTexture(secondary_renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STREAMING,frame_width[i],frame_height[i]);
                        if (!secondary_textures[i]) fatal("cannot resize second-screen texture: %s",SDL_GetError());
                        SDL_SetTextureScaleMode(secondary_textures[i],scale_mode());
                    }
                    SDL_UpdateTexture(secondary_textures[i],nullptr,frames[i].data(),frame_width[i]*4);
                }
                frame_dirty[i] = false;
            }
        }
        auto clear_background = [&](SDL_Renderer *target) {
            SDL_SetRenderDrawColor(target, uint8_t(s.background_rgb>>16),uint8_t(s.background_rgb>>8),uint8_t(s.background_rgb),255);
            SDL_RenderClear(target);
        };
        clear_background(renderer);
        for (int i : {layout.first, 1 - layout.first}) {
            if (!layout.shown[i]) continue;
            SDL_Texture *t = shown_gpu[i] ? shown_gpu[i] : textures[i];
            // the source rect is in native pixels; frames may be rendered larger
            float k = float(t->h) / SCREEN_HEIGHT;
            SDL_FRect src = {layout.src[i].x * k, layout.src[i].y * k, layout.src[i].w * k, layout.src[i].h * k};
            SDL_SetTextureAlphaModFloat(t, layout.alpha[i]);
            SDL_RenderTexture(renderer, t, &src, &layout.rect[i]);
            if (layout.framed && i != layout.first) { // frame the inset
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 160);
                SDL_RenderRect(renderer, &layout.rect[i]);
            }
        }
        if (s.show_stats) {
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 12, 20, 36, 220);
            float sx=1,sy=1;SDL_GetRenderScale(renderer,&sx,&sy);
            SDL_SetRenderScale(renderer,sx*0.65f,sy*0.65f);
            const size_t widest=std::max({std::strlen(stats_text),std::strlen(stats_detail),std::strlen(stats_gpu)});
            SDL_FRect box{4,4,float(widest*SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE+8),float((*stats_gpu?3:2)*SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE+9)};
            SDL_RenderFillRect(renderer,&box);
            SDL_SetRenderDrawColor(renderer,255,255,255,255);
            SDL_RenderDebugText(renderer,8,8,stats_text);
            SDL_RenderDebugText(renderer,8,8+SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE,stats_detail);
            if (*stats_gpu) SDL_RenderDebugText(renderer,8,8+2*SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE,stats_gpu);
            SDL_SetRenderScale(renderer,sx,sy);
        }
        if (!message.empty() && SDL_GetTicks() < message_until) { // in logical units, over the bottom edge
            float tw = float(message.size()) * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE, th = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
            float x = std::max(2.0f, (layout.w - tw) / 2), y = layout.h - th - 8;
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 10, 30, 70, 210);
            SDL_FRect box{x - 4, y - 3, tw + 8, th + 6};
            SDL_RenderFillRect(renderer, &box);
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderDebugText(renderer, x, y, message.c_str());
        }
        // YW2_WINDOW_SCREENSHOT=<prefix>:<frame>[:<every>:<count>]: saves what the window shows
        // at game frame <frame> (then every <every> frames, <count> times) as <prefix>N.png, then quits
        static const char *window_shot = std::getenv("YW2_WINDOW_SCREENSHOT");
        static int shots_taken = 0;
        if (window_shot) {
            std::string spec = window_shot, prefix = spec;
            int at = 600, every = 1, count = 1;
            if (auto c = spec.find(':', 2); c != std::string::npos) {
                prefix = spec.substr(0, c);
                std::sscanf(spec.c_str() + c + 1, "%d:%d:%d", &at, &every, &count);
            }
            if (top_frames >= at + shots_taken * every) {
                std::string file = prefix + std::to_string(shots_taken) + ".png";
                SDL_Surface *surface = SDL_RenderReadPixels(renderer, nullptr);
                if (!surface || !SDL_SavePNG(surface, file.c_str())) LOG("window screenshot %s: %s", file.c_str(), SDL_GetError());
                if (surface) SDL_DestroySurface(surface);
                if (++shots_taken >= count) std::_Exit(0);
            }
        }
        if (secondary_renderer && s.detached_screen) {
            clear_background(secondary_renderer);
            int other=1-layout.first;
            SDL_Texture *t=secondary_shown[other]?secondary_shown[other]:secondary_textures[other];
            SDL_FRect dst{0,0,other == 0 ? layout.top_width : float(BOTTOM_WIDTH),float(SCREEN_HEIGHT)};
            SDL_RenderTexture(secondary_renderer,t,nullptr,&dst);
        }
        if (control) control.tick(top_frames, renderer, s.detached_screen ? secondary_renderer : nullptr,map_guest_test_touch);
        if (burst_left > 0) {
            if (SDL_Surface *surface = SDL_RenderReadPixels(renderer, nullptr)) {
                char file[16];
                std::snprintf(file, sizeof file, "%03d.png", 90 - burst_left);
                SDL_SavePNG(surface, (burst_dir / file).string().c_str());
                SDL_DestroySurface(surface);
            }
            if (--burst_left == 0) LOG("captured %s", burst_dir.string().c_str());
        }
        if (capture_requested) { // F12: a screenshot plus what the scene detection saw, for tuning it
            capture_requested = false;
            auto dir = platform::data_dir() / "captures";
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            std::string name = std::to_string(SDL_GetTicks());
            if (SDL_Surface *surface = SDL_RenderReadPixels(renderer, nullptr)) {
                SDL_SavePNG(surface, (dir / (name + ".png")).string().c_str());
                SDL_DestroySurface(surface);
            }
            pica::request_draw_log((dir / (name + "_draws.txt")).string());
            // what the game did lately and what each of its threads waits on, for bugs that build up
            // over a session (the map's missing chunks)
            log_cfg::save_recent((dir / (name + "_recent.txt")).string().c_str());
            if (std::FILE *f = std::fopen((dir / (name + "_threads.txt")).string().c_str(), "w")) {
                kernel::dump_threads(f);
                std::fclose(f);
            }
            pica::SceneInfo scene = pica::scene_info();
            std::ofstream f(dir / (name + ".txt"));
            f << "context " << CONTEXT_NAMES[classify(scene)] << "\ntop_draws " << scene.top_draws << "\ntop_3d_draws "
              << scene.top_3d_draws << "\nbottom_draws " << scene.bottom_draws << "\nbottom_textures";
            for (uint64_t t : scene.bottom_textures) f << " " << std::hex << t << std::dec;
            f << "\n";
            LOG("captured %s", (dir / name).string().c_str());
        }
        // Separate host presentation pacing from the ARM scheduler. A 30-FPS
        // presentation cap must not halve game updates, audio or VBlank IRQs.
        if (period && next_present_deadline) {
            uint64_t now = SDL_GetTicksNS();
            // With VSync, wake before the target refresh rather than just after
            // it. On a matching refresh rate the backend's VSync alone paces.
            uint64_t lead = 0;
            if (s.vsync) {
                const SDL_DisplayMode *display = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window));
                double refresh = display && display->refresh_rate > 1.0f ? display->refresh_rate : 60.0;
                lead = uint64_t(500000000.0 / refresh);
            }
            if (next_present_deadline > now + lead) SDL_DelayPrecise(next_present_deadline - now - lead);
        }
        if (secondary_renderer && s.detached_screen) SDL_RenderPresent(secondary_renderer);
        SDL_RenderPresent(renderer);
        if (device) {
            // Keep the previous image alive until neither a queued publication
            // nor the displayed wrapper can refer to it. SDL then defers the
            // actual GPU destruction until the submitted work completes.
            std::lock_guard lk(frame_lock);
            std::erase_if(retired_gpu_frames, [&](SDL_GPUTexture *t) {
                for (int i = 0; i < 2; ++i)
                    if (gpu_frame[i] == t || shown_gpu_ptr[i] == t || secondary_shown_ptr[i] == t) return false;
                if (auto it = wrapped.find(t); it != wrapped.end()) {
                    if (it->second.tex) SDL_DestroyTexture(it->second.tex);
                    wrapped.erase(it);
                }
                if (auto it=secondary_wrapped.find(t);it!=secondary_wrapped.end()) {
                    if (it->second.tex) SDL_DestroyTexture(it->second.tex);
                    secondary_wrapped.erase(it);
                }
                pica::gpu::release_owned_texture(t);
                return true;
            });
        }
        uint64_t presented = SDL_GetTicksNS();
        const bool top_visible=layout.shown[0] || (s.detached_screen && secondary_renderer);
        const uint64_t visible_generation=top_visible ? shown_top_generation : 0;
        if (visible_generation && visible_generation != last_top_generation) {
            ++new_top_shown;
            if (shown_top_midpoint) ++mids_shown;
            last_top_generation=visible_generation;
        }
        ++stats_count;
        if (presented-stats_since >= 1000000000ull) {
            const double seconds=double(presented-stats_since)/1e9;
            const int guest=top_frames.load();
            const uint64_t transfers=guest_transfers[0].load();
            std::snprintf(stats_text,sizeof stats_text,"FPS %.1f | new GPU top %.1f/s | game %.1f/s",stats_count/seconds,(new_top_shown-stats_new_top)/seconds,(transfers-stats_transfers)/seconds);
            std::snprintf(stats_detail,sizeof stats_detail,"%.1f ms | VBlank %.1f Hz",1000.0*seconds/std::max(1u,stats_count),(guest-stats_guest)/seconds);
#if defined(_WIN32)
            if(s.show_stats) {
                query_gpu_memory(presented);
                if (has_local && local_memory.Budget) std::snprintf(stats_gpu,sizeof stats_gpu,"GPU %s %.0f / %.0f MiB",gpu_memory.dedicated?"local VRAM":"local memory",double(local_memory.CurrentUsage)/(1024*1024),double(local_memory.Budget)/(1024*1024));
                else if(has_local) std::snprintf(stats_gpu,sizeof stats_gpu,"GPU local usage %.0f MiB (multi-GPU)",double(local_memory.CurrentUsage)/(1024*1024));
                else if (pica::gpu::active()) std::snprintf(stats_gpu,sizeof stats_gpu,"GPU texture payload %.0f MiB (estimate)",double(pica::gpu::owned_texture_payload_bytes())/(1024*1024));
                else stats_gpu[0]=0;
                PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);
                FILETIME start{},end{},kernel{},user{};
                if(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory)) &&
                   GetProcessTimes(GetCurrentProcess(),&start,&end,&kernel,&user)) {
                    auto count=[](FILETIME t){return (uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;};
                    const uint64_t cpu=count(kernel)+count(user);
                    if(stats_cpu)std::snprintf(stats_detail,sizeof stats_detail,"%.1f ms | RAM %.0f MiB | CPU %.1f cores",1000.0*seconds/std::max(1u,stats_count),double(memory.PrivateUsage)/(1024*1024),(cpu-stats_cpu)/1e7/seconds);
                    stats_cpu=cpu;
                }
            } else stats_cpu=0;
#endif
            stats_count=0;stats_since=presented;stats_guest=guest;stats_transfers=transfers;stats_new_top=new_top_shown;
        }
        if (period) {
            // Keep an absolute time base: waking before VSync must not
            // repeatedly subtract the lead time from the target frame period.
            // Drop missed deadlines rather than issuing a burst after a stall.
            if (!next_present_deadline || presented > next_present_deadline + period)
                next_present_deadline = presented + period;
            else
                next_present_deadline += period;
        }
        if (frame_times.is_open() && previous_present) {
            frame_times << presented << ',' << (presented - previous_present) << ',' << top_frames.load() << ',' << guest_transfers[0].load() << ',' << guest_transfers[1].load() << ',' << visible_generation << ',' << (visible_generation && shown_top_midpoint) << ',' << new_top_shown << ',' << mids_shown << '\n';
            if (++present_count % 60 == 0) frame_times.flush();
        }
        previous_present = presented;
        if (audio_times.is_open() && presented - last_audio_sample >= 1000000000ull) {
            sample_audio(presented);
            last_audio_sample = presented;
        }
#if defined(_WIN32)
        if (resource_times.is_open() && presented - last_resource >= 1000000000ull) {
            query_gpu_memory(presented);
            PROCESS_MEMORY_COUNTERS_EX memory{};
            memory.cb = sizeof(memory);
            FILETIME start{}, end{}, kernel{}, user{};
            if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory), sizeof(memory)) &&
                GetProcessTimes(GetCurrentProcess(), &start, &end, &kernel, &user)) {
                auto units = [](FILETIME t) { return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
                resource_times << presented << ',' << memory.WorkingSetSize << ',' << memory.PrivateUsage << ','
                               << units(kernel) << ',' << units(user) << ','
                               << (has_local?int64_t(local_memory.CurrentUsage):-1) << ',' << (has_local && local_memory.Budget?int64_t(local_memory.Budget):-1) << ','
                               << (has_nonlocal?int64_t(nonlocal_memory.CurrentUsage):-1) << ',' << (has_nonlocal && nonlocal_memory.Budget?int64_t(nonlocal_memory.Budget):-1) << ',' << pica::gpu::owned_texture_payload_bytes() << '\n';
                resource_times.flush();
            }
            last_resource = presented;
        }
#endif
    }
}

} // namespace frontend
