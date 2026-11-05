// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>

#include "hal/types.h"
#include "infra/endian.h"
#include "infra/seat.h"

#include "proto/status_word.h"
#include "svc/types.h"

namespace mister::cores::n64 {

enum class FactsKind : std::uint8_t { Rom, IplBoot, IplPreload, Disk };

struct LoadFacts {
    TASTY_SEAT_EXEMPT(component);
    proto::StatusWord mask{};
    proto::StatusWord bits{};
    svc::Crc32 crc32{};
    std::uint32_t bytes = 0;
    hal::FabricAddr load_addr{};
    FactsKind kind = FactsKind::Rom;
    std::uint8_t pad[3]{};

    [[nodiscard]] static std::optional<LoadFacts> of(std::span<const std::uint8_t> bytes) noexcept {
        if (bytes.size() != sizeof(LoadFacts))
            return std::nullopt;
        const auto f = infra::load_native<LoadFacts>(bytes.first<sizeof(LoadFacts)>());
        if (f.kind > FactsKind::Disk)
            return std::nullopt;
        return f;
    }
};

static_assert(std::is_trivially_copyable_v<LoadFacts> && sizeof(LoadFacts) == 48);

}
