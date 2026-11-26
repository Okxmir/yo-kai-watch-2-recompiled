#pragma once
#include <cstddef>
#include <cstdint>

namespace pica {
// Legacy Citra tex1_... pack identifier, distinct from the native cache hash.
uint64_t citra_texture_hash(const char *data, std::size_t size);
}
