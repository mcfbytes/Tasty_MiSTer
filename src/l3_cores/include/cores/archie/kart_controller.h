// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "cores/archie/kart_axis.h"
#include "cores/archie/kart_byte.h"
#include "cores/archie/kart_key.h"
#include "cores/input_sink.h"
#include "infra/fixed_ring.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "proto/mouse_delta.h"
#include "proto/ps2_frame.h"
#include "proto/types.h"
#include "svc/types.h"

namespace mister::cores::archie {

class KartController final : public IInputSink {
    TASTY_SEAT_RESIDENT(Input);

public:
    static constexpr KartByte kHrst{0xFF};
    static constexpr KartByte kRak1{0xFE};
    static constexpr KartByte kRak2{0xFD};
    static constexpr KartByte kRqid{0x20};
    static constexpr KartByte kKbid{0x81};
    static constexpr KartByte kKeyDown{0xC0};
    static constexpr KartByte kKeyUp{0xD0};
    static constexpr KartByte kBack{0x3F};
    static constexpr KartByte kNack{0x30};
    static constexpr KartByte kSack{0x31};
    static constexpr KartByte kMack{0x32};
    static constexpr KartByte kSmak{0x33};

    static constexpr std::chrono::milliseconds kAckTimeout{10};
    static constexpr std::chrono::milliseconds kResetTimeout{20};

    static constexpr std::size_t kQueueSlots = 8;
    static constexpr std::size_t kQueueDepth = kQueueSlots - 1;

    enum class State : std::uint8_t { Hrst, Rak1, Rak2, Idle, WaitAck1, WaitAck2 };

    void bind(Instant now) noexcept override;
    void key(svc::EvdevCode code, bool pressed, Instant now) noexcept override;
    void mouse(proto::MouseDelta delta, proto::MouseButtons buttons, Instant now) noexcept override;
    void controller_word(hal::LinkWord word, Instant now) noexcept override;
    void tick(Instant now) noexcept override;
    [[nodiscard]] std::optional<Instant> deadline() const noexcept override;
    [[nodiscard]] std::optional<proto::Ps2Frame> next_frame() noexcept override;

    [[nodiscard]] State state() const noexcept { return state_; }

    [[nodiscard]] bool scan_enabled() const noexcept { return scan_enabled_; }

    [[nodiscard]] bool mouse_enabled() const noexcept { return mouse_enabled_; }

    [[nodiscard]] infra::Tally queue_drops() const noexcept { return queue_drops_; }

    [[nodiscard]] infra::Tally outbox_drops() const noexcept { return outbox_drops_; }

private:
    struct Queued {
        State next = State::Idle;
        KartByte byte{};
    };

    static constexpr std::size_t kOutboxSlots = 4;

    [[nodiscard]] bool in_reset_() const noexcept { return state_ <= State::Rak2; }

    [[nodiscard]] bool waiting_ack_() const noexcept {
        return state_ == State::WaitAck1 || state_ == State::WaitAck2;
    }

    [[nodiscard]] bool gui_held_() const noexcept { return gui_left_ || gui_right_; }

    void reset_() noexcept;
    void send_(State next, KartByte byte, Instant now) noexcept;
    void transmit_(State next, KartByte byte, Instant now) noexcept;
    void drain_one_(Instant now) noexcept;
    void send_event_(KartByte prefix, KartKey key, Instant now) noexcept;
    void run_timers_(Instant now) noexcept;
    void dispatch_(KartByte byte, Instant now) noexcept;

    State state_ = State::Hrst;
    bool scan_enabled_ = false;
    bool mouse_enabled_ = false;
    bool gui_left_ = false;
    bool gui_right_ = false;
    KartAxis mouse_x_{};
    KartAxis mouse_y_{};
    proto::MouseButtons buttons_{};
    Instant ack_due_{};
    infra::FixedRing<Queued, kQueueDepth> queue_{};
    infra::FixedRing<KartByte, kOutboxSlots> outbox_{};
    infra::Tally queue_drops_{};
    infra::Tally outbox_drops_{};
};

}
