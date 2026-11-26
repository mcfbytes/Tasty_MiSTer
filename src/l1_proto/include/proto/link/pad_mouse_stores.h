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

class PadMouseStores {
public:
    static constexpr PadLevels::Reg kExtReg{UioOp::Mouse, 1};
    static constexpr PadLevels::Reg kPacketReg{UioOp::Mouse, 0};

    static constexpr hal::PadEntries kEntries{2};

    PadMouseStores() = default;

    [[nodiscard]] static std::optional<PadMouseStores> of(const Ps2Frame& f) noexcept;

    [[nodiscard]] constexpr PadWord ext() const noexcept { return PadWord{ext_}; }

    [[nodiscard]] constexpr PadWord packet() const noexcept { return PadWord{packet_}; }

    friend constexpr bool operator==(PadMouseStores, PadMouseStores) = default;

private:
    std::uint32_t ext_ = 0;
    std::uint32_t packet_ = 0;
};

}
