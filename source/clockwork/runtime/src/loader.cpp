#include "loader.h"

#include "dump.h"
#include "frontend/handoff.h"

#include "log.h"
#include "memory.h"

#include <cstring>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#endif

namespace {
uint32_t u32(const std::vector<uint8_t> &b, size_t at) {
    uint32_t v;
    std::memcpy(&v, b.data() + at, 4);
    return v;
}

void verify_psychic_specters_code(const std::vector<uint8_t> &code) {
    if (std::strcmp(clockwork_binary, "yw2_eur") != 0) return;
    if (dump().program_id() != 0x00040000001B2900ull || dump().has_update() || code.size() != 0x761000)
        fatal("This executable requires the supported European Psychic Specters base-game revision.");
#if defined(_WIN32)
    // The compiled function addresses correspond to exactly this code image.
    // Reject an incompatible revision before executing any compiled game code.
    constexpr unsigned char expected[32] = {
        0xb5,0x36,0xdf,0xce,0x0a,0x71,0x38,0xf2,0xd5,0x69,0x15,0x32,0x60,0x4d,0x78,0x2b,
        0x1e,0xa3,0xe2,0x07,0x31,0x12,0x0c,0xc6,0x62,0xba,0xee,0x11,0x8c,0x37,0x05,0x47};
    unsigned char actual[32] = {};
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        fatal("Could not initialize the cartridge integrity check.");
    ULONG object_size = 0, bytes = 0;
    auto status = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_size),
                                    sizeof(object_size), &bytes, 0);
    std::vector<unsigned char> object(object_size);
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (status >= 0) status = BCryptCreateHash(algorithm, &hash, object.data(), object_size, nullptr, 0, 0);
    if (status >= 0) status = BCryptHashData(hash, const_cast<unsigned char *>(code.data()), static_cast<ULONG>(code.size()), 0);
    if (status >= 0) status = BCryptFinishHash(hash, actual, sizeof(actual), 0);
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0 || std::memcmp(actual, expected, sizeof(actual)) != 0)
        fatal("Cartridge code does not match the supported European Psychic Specters revision. Rebuild from a compatible cartridge.");
    LOG("cartridge: EUR code SHA-256 verified");
#endif
}
} // namespace

GameImage load_game() {
    const auto &exheader = dump().exheader();
    const auto &code = dump().code();
    verify_psychic_specters_code(code);

    GameImage g;
    g.title.assign(reinterpret_cast<const char *>(exheader.data()), strnlen(reinterpret_cast<const char *>(exheader.data()), 8));
    uint32_t text_addr = u32(exheader, 0x10);
    uint32_t data_addr = u32(exheader, 0x30), data_size = u32(exheader, 0x38);
    g.entry = text_addr;
    g.stack_size = u32(exheader, 0x1C);
    g.data_end = data_addr + data_size;
    g.bss_size = u32(exheader, 0x3C);
    // ARM11 local system capabilities: main thread priority at 0x200 + 0x0F
    g.main_priority = exheader[0x20F];

    if (text_addr != mem::CODE_BASE) fatal("unexpected code address %08x", text_addr);
    uint64_t code_end = mem::page_up(uint64_t(text_addr) + code.size());
    uint64_t bss_end = mem::page_up(uint64_t(g.data_end) + g.bss_size);
    mem::commit(text_addr, uint32_t(std::max(code_end, bss_end) - text_addr));
    std::memcpy(mem::ptr(text_addr), code.data(), code.size());
    if (std::strcmp(clockwork_binary, "yw2_eur") == 0) {
        // The original 1.25 MiB MapBlk slots can exhaust their space while
        // expanding valid map meshes. A failed load removes BOTH rendering
        // and collision. Preserve the game's heap allocator and ownership:
        // give each of its four slots 4 MiB, with additional parent room
        // for the base map. The translated startup constants are adjusted
        // by scripts/apply-native-map-memory.ps1.
        constexpr uint32_t slot_size_word = 0x003a6af0;
        if (mem::read32(slot_size_word) != 0x0013fe00)
            fatal("Unexpected Psychic Specters map heap configuration.");
        mem::write32(slot_size_word, 0x00400000);
        g.application_memory_budget = 0x05b40000;
        // Keep the copied instruction image consistent with the native C.
        // These are the original exact ARM MOV words, checked before use.
        constexpr uint32_t words[][3] = {
            {0x00104894, 0xe3a017a7, 0xe3a01645},
            {0x00105984, 0xe3a00509, 0xe3a007fd},
            {0x001059a4, 0xe3a03721, 0xe3a0378e},
        };
        for (const auto &word : words) {
            if (mem::read32(word[0]) != word[1])
                fatal("Unexpected Psychic Specters heap budget at %08x.", word[0]);
            mem::write32(word[0], word[2]);
        }
        LOG("map heaps: 4 MiB per streamed block; 35.5 MiB parent");
    }
    // .bss shares the last .data page; make sure the tail is zero
    std::memset(mem::ptr(g.data_end), 0, size_t(bss_end - g.data_end));

    LOG("loaded %s: code %08x-%08x, .bss to %08x, stack %#x, main priority %#x", g.title.c_str(), text_addr,
        uint32_t(text_addr + code.size()), uint32_t(g.data_end + g.bss_size), g.stack_size, g.main_priority);
    return g;
}
