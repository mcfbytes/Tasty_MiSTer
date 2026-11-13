// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "cores/config_codec.h"
#include "cores/config_slot_count.h"
#include "cores/st_config_layout.h"
#include "cores/st_ctrl.h"
#include "cores/types.h"
#include "infra/fixed_str.h"
#include "infra/indexed_array.h"
#include "infra/seat.h"

namespace mister::cores {

struct StConfig {
    TASTY_SEAT_EXEMPT(component);

    static constexpr std::size_t kAcsiTargets = 2;
    using Path = FixedStr<st_cfg::kPathBytes, StrFit::Clip>;

    st_ctrl::CtrlWord system_ctrl{};

    std::uint32_t ext_ctrl = 0;
    Path tos_img{};
    infra::IndexedArray<Path, kAcsiTargets, AcsiTarget> acsi_img{};

    Path cart_img{};

    [[nodiscard]] static StConfig defaults() noexcept;

    void overlay(
        std::span<const std::byte> blob,
        std::span<std::byte, st_cfg::kBytes> img
    ) noexcept;

    void encode(std::span<std::byte, st_cfg::kBytes> img) const noexcept;
    void decode(std::span<const std::byte, st_cfg::kBytes> img) noexcept;
    [[nodiscard]] static FixedStr<kConfigPreviewChars, StrFit::Clip> preview(
        ConfigBlob blob
    ) noexcept;
};

static_assert(
    std::is_trivially_copyable_v<StConfig>,
    "the value crosses as its bytes: MAIN decodes it and T-RT copies it"
);
static_assert(
    sizeof(StConfig) <= kMaxConfigValueBytes,
    "MAIN holds one decoded value in a kMaxConfigValueBytes buffer"
);

class StConfigCodec final : public IConfigCodec {
public:
    [[nodiscard]] ConfigSlotCount slot_count() const noexcept override;
    [[nodiscard]] FixedStr<kConfigNameChars, StrFit::Clip> file_name(
        ConfigSlot slot
    ) const noexcept override;
    [[nodiscard]] ConfigFit fit() const noexcept override;
    [[nodiscard]] proto::FileSize value_bytes() const noexcept override;
    [[nodiscard]] bool decode(ConfigBlob blob, std::span<std::byte> value) const noexcept override;
    [[nodiscard]] std::span<const std::byte> encode(
        std::span<const std::byte> value,
        std::span<std::byte> dst
    ) const noexcept override;
    [[nodiscard]] FixedStr<kConfigPreviewChars, StrFit::Clip> preview(
        ConfigBlob blob
    ) const noexcept override;
};

inline constexpr StConfigCodec kStConfigCodec{};

}
