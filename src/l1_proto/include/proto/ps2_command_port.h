// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "infra/fixed_ring.h"
#include "infra/seat.h"

namespace mister::proto {

class Ps2CommandPort {
    TASTY_SEAT_RESIDENT(Input);

public:
    static constexpr std::size_t kReplyCap = 16;

    template <class Decode>
    void accept(std::uint16_t word, Decode&& decode) {
        if ((word & 0x100u) == 0u)
            return;
        const auto byte = static_cast<std::uint8_t>(word & 0xFFu);
        if (!awaiting_param_)
            cmd_ = byte;
        decode(byte);
    }

    void end() noexcept {
        cmd_ = 0;
        awaiting_param_ = false;
        replies_.clear();
    }

    void reply(std::uint8_t code) noexcept { (void)replies_.push(code); }

    [[nodiscard]] std::optional<std::uint8_t> next_reply() noexcept { return replies_.pop(); }

    [[nodiscard]] bool awaiting_param() const noexcept { return awaiting_param_; }

    [[nodiscard]] std::uint8_t command() const noexcept { return cmd_; }

    void expect_param() noexcept { awaiting_param_ = true; }

    void param_done() noexcept { awaiting_param_ = false; }

private:
    std::uint8_t cmd_ = 0;
    bool awaiting_param_ = false;
    infra::FixedRing<std::uint8_t, kReplyCap> replies_{};
};

}
