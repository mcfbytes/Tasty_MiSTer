// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>

#include "cores/config_blob.h"
#include "cores/config_codec.h"
#include "cores/config_fit.h"
#include "cores/config_slot_count.h"
#include "cores/types.h"
#include "infra/fixed_str.h"
#include "infra/prelude.h"
#include "proto/types.h"

namespace mister::cores::minimig {

class MinimigConfigCodec final : public IConfigCodec {
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

inline constexpr MinimigConfigCodec kMinimigConfigCodec{};

}
