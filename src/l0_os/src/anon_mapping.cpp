// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/anon_mapping.h"

#include <sys/mman.h>

namespace mister::os {

bool AnonMapping::resize(std::size_t bytes) noexcept {
    void* const p = map_.empty()
        ? ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)
        : ::mremap(map_.data(), map_.size(), bytes, MREMAP_MAYMOVE);
    if (p == MAP_FAILED)
        return false;
    map_ = {static_cast<std::byte*>(p), bytes};
    return true;
}

void AnonMapping::release() noexcept {
    if (!map_.empty())
        (void)::munmap(map_.data(), map_.size());
    map_ = {};
}

}
