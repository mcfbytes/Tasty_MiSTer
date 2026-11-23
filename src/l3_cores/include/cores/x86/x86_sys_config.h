// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

#include "cores/config_codec.h"
#include "cores/x86/x86_slot.h"
#include "infra/byte_offset.h"
#include "infra/fixed_str.h"
#include "infra/indexed_array.h"
#include "infra/seat.h"

namespace mister::cores::x86 {

struct X86SysConfig {
    TASTY_SEAT_EXEMPT(component);

    static constexpr std::uint32_t kVersion = 3;
    static constexpr std::size_t kPathBytes = 1024;
    static constexpr infra::ByteOffset kPathsAt{sizeof(std::uint32_t)};
    static constexpr std::size_t kBytes = sizeof(std::uint32_t) + X86Slot::kCount * kPathBytes;

    using Path = FixedStr<kPathBytes, StrFit::Clip>;
    using Image = std::span<std::byte, kBytes>;
    using ConstImage = std::span<const std::byte, kBytes>;

    infra::IndexedArray<Path, X86Slot::kCount, X86Slot> paths{};

    [[nodiscard]] std::string_view path(X86Slot slot) const noexcept { return paths[slot].view(); }

    void name(X86Slot slot, std::string_view path) noexcept { (void)paths[slot].assign(path); }

    void encode(Image img) const noexcept;

    [[nodiscard]] static X86SysConfig decode(ConstImage img) noexcept;
};

static_assert(
    std::is_trivially_copyable_v<X86SysConfig>,
    "the value crosses as its bytes: MAIN decodes it and T-RT copies it"
);
static_assert(sizeof(X86SysConfig) <= kMaxConfigValueBytes);
static_assert(X86SysConfig::kBytes == 6148);

}
