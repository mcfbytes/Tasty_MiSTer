// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/error.h"
#include "infra/seat.h"
#include "svc/adv7513_io.h"
#include "svc/types.h"

namespace mister::svc {

struct EdidBlock {
    std::array<std::byte, 256> bytes{};
    bool valid = false;
};

class EdidStore {
    TASTY_SEAT_EXEMPT(component);

public:
    Ex<void> refresh();
    const EdidBlock& current() const noexcept { return edid_; }
    Generation generation() const noexcept { return gen_; }

    void set_io(const adv7513::Io& io) noexcept { io_ = io; }

    static constexpr std::uint32_t kReadyTimeoutMs = 500;
    static constexpr std::uint32_t kReadyPollUs = 10000;
    static constexpr std::uint32_t kTriggerSettleUs = 1000;
    static constexpr std::uint32_t kRetryDelayUs = 100000;
    static constexpr int kMaxRetries = 20;

    static constexpr std::size_t kMaxSegments = 8;

    std::uint32_t ready_timeouts() const noexcept { return ready_timeouts_; }

    std::span<const std::byte> full() const noexcept;

private:
    Ex<bool> read_segment(std::uint8_t segment, std::span<std::byte> out);

    EdidBlock edid_{};
    Generation gen_{};
    adv7513::Io io_{};
    std::array<std::byte, 256 * kMaxSegments> full_{};
    std::size_t full_len_ = 0;
    std::uint32_t ready_timeouts_ = 0;
};

}  // namespace mister::svc
