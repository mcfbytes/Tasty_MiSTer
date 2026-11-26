// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "hal/pad_entries.h"
#include "proto/link/pad_levels.h"
#include "proto/pad_word.h"
#include "proto/ps2_frame.h"
#include "proto/uio_opcodes.h"

namespace mister::proto::link {

class PadKeyImage {
public:
    static constexpr PadLevels::Reg kReg{UioOp::Keyboard, 0};

    static constexpr hal::PadEntries kEntries{1};

    PadKeyImage() = default;

    [[nodiscard]] static std::optional<PadKeyImage> of(const Ps2Frame& f) noexcept;

    [[nodiscard]] constexpr PadWord word() const noexcept { return PadWord{raw_}; }

    friend constexpr bool operator==(PadKeyImage, PadKeyImage) = default;

private:
    constexpr explicit PadKeyImage(std::uint32_t raw) noexcept : raw_(raw) {}

    std::uint32_t raw_ = 0;
};

}
