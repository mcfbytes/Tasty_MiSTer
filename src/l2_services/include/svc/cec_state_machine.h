// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "infra/error.h"
#include "infra/seat.h"
#include "svc/adv7513_io.h"

namespace mister::svc {

struct CecLength {
    std::uint8_t v = 0;
    static constexpr std::uint8_t kMax = 16;
    static constexpr std::uint8_t kMask = 0x1F;
    static constexpr std::uint8_t kHeaderBytes = 2;

    [[nodiscard]] static constexpr std::optional<CecLength> of(std::uint8_t reg) noexcept {
        const std::uint8_t v = static_cast<std::uint8_t>(reg & kMask);
        if (v < 1 || v > kMax)
            return std::nullopt;
        return CecLength{v};
    }

    template <std::size_t N>
    [[nodiscard]] constexpr std::span<std::uint8_t> payload_of(
        std::array<std::uint8_t, N>& data
    ) const noexcept {
        static_assert(N + kHeaderBytes >= kMax, "the data field holds every payload");
        return std::span<std::uint8_t>{data}.first(
            v > kHeaderBytes ? std::size_t{v} - kHeaderBytes : std::size_t{0}
        );
    }

    friend constexpr bool operator==(CecLength, CecLength) = default;
};

static_assert(sizeof(CecLength) == 1, "the length field is one byte");
static_assert(
    CecLength::of(0).has_value() == false
        && CecLength::of(1).has_value()
        && CecLength::of(CecLength::kMax).has_value()
        && !CecLength::of(CecLength::kMax + 1).has_value(),
    "a CEC frame is 1..16 bytes"
);

struct CecMessage {
    std::uint8_t header = 0;
    std::uint8_t opcode = 0;
    CecLength length{};
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
    [[nodiscard]] Ex<void> poll_deadline();

    std::uint32_t max_rx_depth_seen() const noexcept { return max_rx_depth_; }

    explicit CecStateMachine(const adv7513::Io& io = adv7513::kUnboundIo) noexcept : io_(io) {}

    explicit CecStateMachine(const adv7513::Io&&) = delete;

    CecState state() const noexcept { return state_; }

    void set_state(CecState s) noexcept { state_ = s; }

    const std::optional<CecMessage>& last_message() const noexcept { return last_; }

    std::uint32_t rx_released() const noexcept { return rx_released_; }

    std::uint32_t rx_rejected() const noexcept { return rx_rejected_; }

    static constexpr std::uint32_t kReleasePulseUs = 200;
    static constexpr unsigned kRxBuffers = 3;

private:
    [[nodiscard]] Ex<CecMessage> decode_rx(os::I2cReg header, CecLength length);

    std::uint32_t max_rx_depth_ = 0;
    const adv7513::Io& io_;
    CecState state_ = CecState::Disabled;
    std::optional<CecMessage> last_{};
    std::uint32_t rx_released_ = 0;
    std::uint32_t rx_rejected_ = 0;
};

}
