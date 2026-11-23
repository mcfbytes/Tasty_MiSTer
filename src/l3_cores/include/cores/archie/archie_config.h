// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

#include "cores/archie/archie_ctrl.h"
#include "cores/config_codec.h"
#include "infra/byte_count.h"
#include "infra/byte_offset.h"
#include "infra/fixed_str.h"
#include "infra/prelude.h"
#include "infra/seat.h"

namespace mister::cores::archie {

struct ArchieConfig {
    TASTY_SEAT_EXEMPT(component);

    static constexpr std::size_t kPathBytes = 1024;
    static constexpr std::size_t kHddUnits = 2;
    static constexpr infra::ByteOffset kSystemCtrlAt{0};
    static constexpr infra::ByteOffset kRomImgAt = kSystemCtrlAt
        + infra::ByteCount{sizeof(std::uint32_t)};
    static constexpr infra::ByteOffset kHddImgAt = kRomImgAt + infra::ByteCount{kPathBytes};
    static constexpr std::size_t kBytes = kHddImgAt.bytes() + kHddUnits * kPathBytes;

    using Path = FixedStr<kPathBytes, StrFit::Clip>;
    using Image = std::span<std::byte, kBytes>;
    using ConstImage = std::span<const std::byte, kBytes>;

    SystemCtrl system_ctrl{};

    Path rom_img{};

    std::array<Path, kHddUnits> hdd_img{};

    void encode(Image img) const noexcept;
    [[nodiscard]] static ArchieConfig decode(ConstImage img) noexcept;
};

static_assert(
    std::is_trivially_copyable_v<ArchieConfig>,
    "the value crosses as its bytes: MAIN decodes it and T-RT copies it"
);
static_assert(
    sizeof(ArchieConfig) <= kMaxConfigValueBytes,
    "MAIN holds one decoded value in a kMaxConfigValueBytes buffer"
);

}
