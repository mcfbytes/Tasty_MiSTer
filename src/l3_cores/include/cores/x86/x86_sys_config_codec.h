// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>
#include <string_view>

#include "cores/config_blob.h"
#include "cores/config_codec.h"
#include "cores/config_fit.h"
#include "cores/config_slot_count.h"
#include "cores/types.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::cores::x86 {

class X86SysConfigCodec final : public IConfigCodec {
    TASTY_SEAT_RESIDENT(Main);

public:
    explicit constexpr X86SysConfigCodec(std::string_view file_name) noexcept
        : file_name_(file_name) {}

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

private:
    std::string_view file_name_;
};

}
