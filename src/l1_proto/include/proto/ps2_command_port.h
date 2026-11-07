// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "infra/fixed_ring.h"
#include "infra/seat.h"

namespace mister::proto {

enum class Ps2Command : std::uint8_t {
    kNone = 0x00,
    kReset = 0xFF,
    kReadId = 0xF2,
    kSetDefaults = 0xF6,
    kSetLeds = 0xED,
    kScanCodeOrRemote = 0xF0,
    kTypematicOrSampleRate = 0xF3,
    kEnable = 0xF4,
    kDisable = 0xF5,
    kEnableTypematic = 0xFA,
    kEcho = 0xEE,
    kSetResolution = 0xE8,
    kSetScaling = 0xE6,
    kStreamMode = 0xEA,
    kStatusRequest = 0xE9,
};

enum class Ps2Reply : std::uint8_t {
    kAck = 0xFA,
    kResend = 0xFE,
    kBatOk = 0xAA,
    kIdFirst = 0xAB,
    kIdSecond = 0x83,
    kEcho = 0xEE,
    kZero = 0x00,
};

[[nodiscard]] constexpr Ps2Command command_of(std::uint8_t byte) noexcept {
    return static_cast<Ps2Command>(byte);
}

[[nodiscard]] constexpr Ps2Reply reply_of(std::uint8_t byte) noexcept {
    return static_cast<Ps2Reply>(byte);
}

[[nodiscard]] constexpr std::uint8_t byte_of(Ps2Reply reply) noexcept {
    return static_cast<std::uint8_t>(reply);
}

class Ps2CommandPort {
    TASTY_SEAT_RESIDENT(Input);

public:
    static constexpr std::size_t kReplyCap = 16;

    template <class Decode>
    void accept(std::uint16_t word, Decode&& decode) {
        if ((word & 0x100u) == 0u)
            return;
        const auto byte = static_cast<std::uint8_t>(word);
        if (!awaiting_param_)
            cmd_ = command_of(byte);
        decode(byte);
    }

    void end() noexcept {
        cmd_ = Ps2Command::kNone;
        awaiting_param_ = false;
        replies_.clear();
    }

    void reply(Ps2Reply code) noexcept { (void)replies_.push(code); }

    [[nodiscard]] std::optional<Ps2Reply> next_reply() noexcept { return replies_.pop(); }

    [[nodiscard]] bool awaiting_param() const noexcept { return awaiting_param_; }

    [[nodiscard]] Ps2Command command() const noexcept { return cmd_; }

    void expect_param() noexcept { awaiting_param_ = true; }

    void param_done() noexcept { awaiting_param_ = false; }

private:
    Ps2Command cmd_ = Ps2Command::kNone;
    bool awaiting_param_ = false;
    infra::FixedRing<Ps2Reply, kReplyCap> replies_{};
};

}
