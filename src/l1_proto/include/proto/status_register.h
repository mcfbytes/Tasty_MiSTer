// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "infra/error.h"
#include "hal/types.h"
#include "infra/opt_ref.h"
#include "proto/link_frame.h"
#include "proto/reset_fence.h"
#include "proto/status_edit.h"
#include "proto/status_word.h"
#include "proto/types.h"
#include "infra/seat.h"

namespace mister::proto {

class StatusRegister {
    TASTY_SEAT_RESIDENT(RT);

public:
    static constexpr unsigned kBits = StatusWord::kBits;
    static constexpr unsigned kWords = StatusWord::kWords;

    void write(const StatusEdit& edit) noexcept;

    void set_bit(StatusBit bit, bool value);
    bool get_bit(StatusBit bit) const;

    void adopt(std::span<const std::uint16_t, kWords> words) noexcept;

    void fence_with(IResetFence* fence) noexcept { fence_ = infra::opt_ref_of(fence); }

    [[nodiscard]] Ex<void> flush(ILinkFrame& frame);

    bool dirty() const noexcept { return dirty_; }

    hal::SpiWord word(unsigned i) const { return hal::SpiWord{shadow_.words[i]}; }

    [[nodiscard]] StatusWord value() const noexcept;

private:
    StatusWord shadow_{};
    bool dirty_ = false;
    StatusWord sent_{};
    infra::OptRef<IResetFence> fence_{};
};

}
