// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

#include "cores/input_sink.h"
#include "cores/minimig/amiga_key_code.h"
#include "infra/byte_offset.h"
#include "infra/fixed_ring.h"
#include "infra/indexed_array.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "proto/mouse_delta.h"
#include "proto/ps2_frame.h"
#include "proto/types.h"
#include "svc/types.h"

namespace mister::cores::minimig {

struct AmigaMouseReport {
    std::array<std::byte, 4> v{};

    [[nodiscard]] static constexpr AmigaMouseReport of(
        proto::MouseDelta delta,
        proto::MouseButtons buttons
    ) noexcept {
        constexpr infra::ByteOffset kX{0};
        constexpr infra::ByteOffset kY{1};
        constexpr infra::ByteOffset kButtons{2};
        constexpr infra::ByteOffset kWheel{3};
        constexpr int kReach = std::numeric_limits<std::int8_t>::max();
        const int held_x = std::clamp(delta.dx, -kReach, kReach);
        const int held_y = std::clamp(delta.dy, -kReach, kReach);
        const int held_wheel = std::clamp(delta.wheel, -kReach, kReach);
        AmigaMouseReport out{};
        infra::at_index(out.v, kX) = std::byte{static_cast<std::uint8_t>(held_x)};
        infra::at_index(out.v, kY) = std::byte{static_cast<std::uint8_t>(held_y)};
        infra::at_index(out.v, kButtons) = std::byte{buttons.ps2_packet_bits().flags};
        infra::at_index(out.v, kWheel) = std::byte{static_cast<std::uint8_t>(held_wheel)};
        return out;
    }

    [[nodiscard]] constexpr proto::Ps2Frame frame() const noexcept {
        proto::Ps2Frame out{
            .stream = proto::Ps2Stream::MouseBytes,
            .len = static_cast<std::uint8_t>(v.size())
        };
        for (std::size_t i = 0; i < v.size(); ++i)
            out.bytes[i] = std::to_integer<std::uint8_t>(v[i]);
        return out;
    }
};

class AmigaKeyboard final : public IInputSink {
    TASTY_SEAT_RESIDENT(Input);

public:
    struct Mapping {
        svc::EvdevCode code{};
        AmigaKeyCode key{};
        bool caps_toggle = false;
    };

    static constexpr std::chrono::milliseconds kKeyGap{10};
    static constexpr std::size_t kQueueSlots = 16;
    static constexpr std::size_t kQueueDepth = kQueueSlots - 1;

    static constexpr std::size_t kOutboxSlots = 4;

    [[nodiscard]] static std::optional<Mapping> mapping_of(svc::EvdevCode code) noexcept;

    void bind(Instant now) noexcept override;
    void key(svc::EvdevCode code, bool pressed, Instant now) noexcept override;
    void mouse(proto::MouseDelta delta, proto::MouseButtons buttons, Instant now) noexcept override;
    void controller_word(hal::SpiWord word, Instant now) noexcept override;
    void tick(Instant now) noexcept override;
    [[nodiscard]] std::optional<Instant> deadline() const noexcept override;
    [[nodiscard]] std::optional<proto::Ps2Frame> next_frame() noexcept override;

    [[nodiscard]] bool caps_down() const noexcept { return caps_down_; }

    [[nodiscard]] infra::Tally queue_drops() const noexcept { return queue_drops_; }

    [[nodiscard]] infra::Tally outbox_drops() const noexcept { return outbox_drops_; }

private:
    void send_(AmigaKeyCode code, Instant now) noexcept;
    void post_(const proto::Ps2Frame& frame) noexcept;

    bool caps_down_ = false;
    std::optional<Instant> gap_ends_{};
    infra::FixedRing<AmigaKeyCode, kQueueDepth> queue_{};
    infra::FixedRing<proto::Ps2Frame, kOutboxSlots> outbox_{};
    infra::Tally queue_drops_{};
    infra::Tally outbox_drops_{};
};

}
