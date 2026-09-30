// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "infra/error.h"
#include "infra/seat.h"
#include "svc/adv7513_io.h"

namespace mister::svc {

struct CecMessage {
    std::uint8_t header = 0;
    std::uint8_t opcode = 0;
    std::uint8_t length = 0;
    std::array<std::uint8_t, 14> data{};
};

enum class CecState : std::uint8_t {
    Disabled,
    AwaitingInit,
    Idle,
    WaitingTx,
};

class CecStateMachine {
    TASTY_SEAT_RESIDENT(Ui);

public:
    Ex<void> poll_deadline();
    std::uint32_t max_rx_depth_seen() const noexcept { return max_rx_depth_; }

    void set_io(const adv7513::Io& io) noexcept { io_ = io; }
    CecState state() const noexcept { return state_; }
    void set_state(CecState s) noexcept { state_ = s; }

    const std::optional<CecMessage>& last_message() const noexcept { return last_; }

    std::uint32_t rx_released() const noexcept { return rx_released_; }
    std::uint32_t rx_rejected() const noexcept { return rx_rejected_; }

    static constexpr std::uint32_t kReleasePulseUs = 200;
    static constexpr int kRxBuffers = 3;

private:
    std::uint32_t max_rx_depth_ = 0;
    adv7513::Io io_{};
    CecState state_ = CecState::Disabled;
    std::optional<CecMessage> last_{};
    std::uint32_t rx_released_ = 0;
    std::uint32_t rx_rejected_ = 0;
};

}  // namespace mister::svc
