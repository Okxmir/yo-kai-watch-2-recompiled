#pragma once
// User settings (settings.ini in the user data folder), edited in the launcher and by the
// in-game hotkeys.
#include <cstdint>
#include <map>
#include <string>
#include <utility>

namespace frontend {

// what can be bound: the 3DS buttons and the four circle-pad directions
enum Control : int {
    C_A, C_B, C_X, C_Y, C_L, C_R, C_ZL, C_ZR, C_START, C_SELECT, C_UP, C_DOWN, C_LEFT, C_RIGHT,
    C_CIRCLE_UP, C_CIRCLE_DOWN, C_CIRCLE_LEFT, C_CIRCLE_RIGHT, CONTROL_COUNT
};
struct ControlInfo {
    const char *id, *label;
    uint32_t button; // HID bit; 0 for circle-pad directions
};
extern const ControlInfo CONTROLS[CONTROL_COUNT];

// controller binding codes: SDL_GamepadButton values, or these
constexpr int PAD_NONE = -1, PAD_LEFT_TRIGGER = 100, PAD_RIGHT_TRIGGER = 101;

enum Filter : int { FILTER_SHARP = 0, FILTER_SMOOTH = 1, FILTER_NEAREST = 2 };

// The verified EUR Psychic Specters RomFS contains engb, fr, de, it, es,
// nl and ru archives, movies and voices. These are the corresponding 3DS IDs.
inline constexpr int EUR_LANGUAGE_IDS[] = {1, 2, 3, 4, 5, 8, 10};
inline constexpr const char *EUR_LANGUAGE_NAMES[] = {"English", "French", "German", "Italian", "Spanish", "Dutch", "Russian"};
inline constexpr int eur_language_index(int language) {
    for (int i = 0; i < 7; ++i) if (EUR_LANGUAGE_IDS[i] == language) return i;
    return 0;
}

struct Settings {
    std::string game_path; // the dump played last (or next)
    std::string dump_folder; // where the player's dumps are; the launcher looks there for more
    std::map<uint64_t, std::string> dumps; // the dumps found, by program id
    std::map<uint64_t, std::string> updates; // their updates found (decrypted .cia/.cxi), by the game's program id
    bool show_launcher = true;
    bool launcher_fullscreen = true; // the launcher fills the screen (F11 toggles); the game has its own
    bool menu_music = true;          // the launcher plays Blasters' menu theme (menu_music.h)
    bool check_updates = true;       // the launcher looks for a newer build (updater.h)
    std::string update_channel;      // "nightly" or "releases"; empty: the one this build came from

    // display
    bool single_screen = false, swap_screens = false, picture_in_picture = true;
    int dual_layout = 0; // stacked, side-by-side left/right, top-priority left/right, custom
    bool detached_screen = false, detached_fullscreen = false;
    int main_display = 0, detached_display = 1; // 0-based enumeration, falls back to primary
    int top_size = 100, bottom_size = 100, screen_gap = 0;
    int pip_size = 35, pip_x = 100, pip_y = 100, pip_opacity = 100;
    int custom_top_x = 0, custom_top_y = 0, custom_bottom_x = 404, custom_bottom_y = 0;
    uint32_t background_rgb = 0;
    bool smart_layout = true; // one screen: arrange by what the game shows (menus, map, battles)
    bool widescreen = false, fullscreen = false, vsync = true;
    // -1 means an older settings.ini with only the widescreen boolean.
    int aspect_mode = -1; // original, 16:9, 16:10, 21:9, 32:9, custom
    int aspect_num = 16, aspect_den = 9;
    int window_width = 0, window_height = 0; // 0 = automatic window dimensions
    int window_mode = 0; // 0 windowed, 1 borderless desktop, 2 exclusive fullscreen
    int filter = FILTER_SHARP;
    int resolution = 1; // integer internal resolution multiplier (1-10)
    int hud_size = 80;  // percent: the top screen's dialogue boxes and other 2D HUD
    int camera_zoom = 90; // percent, in widescreen: the 3D scene's size (less shows more of it)
    bool gpu_renderer = true; // draw on the GPU (SDL_GPU); off: the software renderer
    bool frame_interpolation = false; // GPU renderer: 30 fps games shown at 60 (in-between frames)
    int frame_limit = 0; // 0 original presentation; 30 or 60 host presentations per second
    int performance_profile = 1; // economy, balanced, performance, maximum
    int texture_cache_mib = 128; // decoded custom HD texture cache budget
    bool prewarm_pipelines = true, show_stats = false, hd_textures = false;
    int texture_sampling = 0; // original, nearest, linear, anisotropic 2/4/8/16
    bool fxaa = false; // optional top LCD postprocessing (Vulkan)
    int sharpening = 0; // 0..100, disabled by default
    bool ai_upscale = false;          // GPU renderer: textures upscaled by Real-ESRGAN (video/upscale.h)
    int ai_upscale_factor = 2;        // 2 or 4
    bool prefer_integrated_gpu = false; // on laptops with two GPUs: the power-saving one
    // one-screen layouts chosen with F5: "<game>.<kind of scene>" -> layout (see sdl.cpp)
    std::map<std::string, std::string> layouts;

    // audio
    int volume = 100;            // percent
    std::string audio_device;    // a playback device by name; empty: the system's default
    bool mute_in_background = false; // silent while the window isn't focused
    bool mono = false;           // left and right mixed together
    int audio_latency = 1;       // buffering: 0 low, 1 normal, 2 high (fewer crackles on a busy PC)

    // local wireless over the internet (the Rooms tab; see net/air.h)
    std::string relay;        // "host[:port]" of a relay; empty: this PC only (two windows)
    std::string player_name;  // shown to the others in the room
    std::string room, room_password;

    // European release: original game languages (3DS language codes above).
    int language = 1;

    int keys[CONTROL_COUNT]; // SDL_Scancode
    int pad[CONTROL_COUNT];  // controller binding codes
    int stick_deadzone = 18; // percent of SDL axis range
    int stick_sensitivity = 100; // percent, leaving the original range at 100

    Settings() { reset_controls(); }
    void reset_controls();
    void load();
    void save() const;
    std::pair<int,int> aspect_ratio() const;
};
Settings &settings();

// display names for bindings
std::string key_name(int scancode);
std::string pad_name(int code);

} // namespace frontend
