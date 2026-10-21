// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "proto/status_word.h"

namespace mister::cores::n64 {

enum class FactsKind : std::uint8_t { Rom, IplBoot, IplPreload };

struct LoadFacts {
    proto::StatusWord mask{};
    proto::StatusWord bits{};
    std::uint32_t crc32 = 0;
    std::uint32_t bytes = 0;
    FactsKind kind = FactsKind::Rom;
    std::uint8_t pad[3]{};
};

static_assert(std::is_trivially_copyable_v<LoadFacts> && sizeof(LoadFacts) == 44);

}
