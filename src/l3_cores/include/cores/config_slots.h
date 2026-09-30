// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "cores/config_blob.h"
#include "cores/config_fit.h"
#include "cores/types.h"
#include "infra/error.h"
#include "infra/fixed_str.h"

namespace mister::cores {

inline constexpr std::uint8_t kMaxConfigSlots = 10;
inline constexpr std::size_t kConfigNameChars = 32;
inline constexpr std::size_t kConfigPreviewChars = 48;

class IConfigSlots {
public:
    virtual ~IConfigSlots() = default;

    [[nodiscard]] virtual std::uint8_t slot_count() const noexcept = 0;

    [[nodiscard]] virtual FixedStr<kConfigNameChars, StrFit::Clip> file_name(
        ConfigSlot slot) const noexcept = 0;
    [[nodiscard]] virtual ConfigFit fit() const noexcept = 0;

    [[nodiscard]] virtual std::span<const std::byte> snapshot() noexcept = 0;

    [[nodiscard]] virtual Ex<void> restore(ConfigBlob blob) = 0;

    [[nodiscard]] virtual FixedStr<kConfigPreviewChars, StrFit::Clip> preview(
        ConfigBlob blob) const noexcept = 0;

protected:
    IConfigSlots() = default;
    IConfigSlots(const IConfigSlots&) = default;
    IConfigSlots& operator=(const IConfigSlots&) = default;
};

}  // namespace mister::cores
