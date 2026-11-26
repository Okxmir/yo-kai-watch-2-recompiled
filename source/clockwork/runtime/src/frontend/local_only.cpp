// Standalone local-only distribution: the original local mod loader, texture
// replacement, game runtime and save handling remain enabled. No remote asset
// downloader or updater may silently fetch another project's binaries.
#include "gamebanana.h"
#include "updater.h"

namespace gamebanana {
int game_id(const std::string &) { return 0; }
static bool unavailable(std::string &error) {
    error = "Online downloads are not enabled in this build. Install local mods in the mods folder.";
    return false;
}
bool list_mods(int, int, std::vector<Mod> &out, bool &more, std::string &error) {
    out.clear(); more = false; return unavailable(error);
}
bool mod_files(long long, std::vector<File> &out, std::string &error) {
    out.clear(); return unavailable(error);
}
bool install(const std::string &, const Mod &, const File &, std::atomic<float> &progress, std::string &error) {
    progress = 0; return unavailable(error);
}
bool fetch_archive(const std::string &, const std::filesystem::path &, std::atomic<float> &progress,
                   std::string &error) {
    progress = 0; return unavailable(error);
}
}

namespace updater {
const char *build() { return ""; }
std::string build_label() { return "local build"; }
bool enabled() { return false; }
bool built_nightly() { return false; }
Status status() { return {}; }
void check(bool) {}
void install() {}
void restart() {}
void cleanup() {}
}
