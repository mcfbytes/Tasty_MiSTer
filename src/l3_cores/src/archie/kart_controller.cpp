// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/archie/kart_controller.h"

#include <array>
#include <optional>

#include "cores/archie/kart_key_map.h"
#include "svc/types.h"

namespace mister::cores::archie {

namespace {

constexpr svc::EvdevCode kKeyLeftMeta{125};
constexpr svc::EvdevCode kKeyRightMeta{126};

[[nodiscard]] constexpr KartKey chorded(KartKey key) noexcept {
    if (key == kKartEscape)
        return kKartBreak;
    if (key == kKartKey1)
        return kKartMouseExtra1;
    if (key == kKartKey2)
        return kKartMouseExtra2;
    if (key == kKartKey3)
        return kKartKeypadHash;
    return key;
}

}

void KartController::reset_() noexcept {
    queue_.clear();
    state_ = State::Hrst;
    mouse_x_ = mouse_y_ = KartAxis{};
    scan_enabled_ = mouse_enabled_ = false;
}

void KartController::bind(Instant now) noexcept {
    TASTY_SEAT_BODY(KartController);
    reset_();
    buttons_ = {};
    gui_left_ = gui_right_ = false;
    outbox_.clear();
    send_(State::Rak1, kHrst, now);
    ack_due_ = now + kResetTimeout;
}

void KartController::transmit_(State next, KartByte byte, Instant now) noexcept {
    if (!outbox_.push(byte))
        outbox_drops_.add();
    state_ = next;
    ack_due_ = now + kAckTimeout;
}

void KartController::send_(State next, KartByte byte, Instant now) noexcept {
    if (!waiting_ack_()) {
        transmit_(next, byte, now);
        return;
    }
    if (!queue_.push(Queued{next, byte}))
        queue_drops_.add();
}

void KartController::drain_one_(Instant now) noexcept {
    if (const auto q = queue_.pop())
        transmit_(q->next, q->byte, now);
}

void KartController::send_event_(KartByte prefix, KartKey key, Instant now) noexcept {
    const std::array<KartByte, 2> bytes = key.event(prefix);
    send_(State::WaitAck1, bytes[0], now);
    send_(State::WaitAck2, bytes[1], now);
}

void KartController::key(svc::EvdevCode code, bool pressed, Instant now) noexcept {
    TASTY_SEAT_BODY(KartController);
    if (code == kKeyLeftMeta)
        gui_left_ = pressed;
    if (code == kKeyRightMeta)
        gui_right_ = pressed;
    const std::optional<KartKey> mapped = kart_key_of(code);
    if (!mapped)
        return;
    const KartKey key = gui_held_() ? chorded(*mapped) : *mapped;
    if (in_reset_() || !scan_enabled_)
        return;
    send_event_(pressed ? kKeyDown : kKeyUp, key, now);
}

void KartController::mouse(
    proto::MouseDelta delta,
    proto::MouseButtons buttons,
    Instant now
) noexcept {
    TASTY_SEAT_BODY(KartController);
    mouse_x_ = mouse_x_.plus(KartAxis::x_of(delta));
    mouse_y_ = mouse_y_.plus(KartAxis::y_of(delta));
    if (in_reset_())
        return;

    const bool moved = !mouse_x_.at_rest() || !mouse_y_.at_rest();
    if (mouse_enabled_ && moved && state_ == State::Idle) {
        send_(State::WaitAck1, mouse_x_.byte(), now);
        send_(State::WaitAck2, mouse_y_.byte(), now);
        mouse_x_ = mouse_y_ = KartAxis{};
    }
    if (!scan_enabled_)
        return;
    for (unsigned n = 0; n < kKartMouseButtons.size(); ++n) {
        const proto::MouseButtons bit = proto::MouseButtons::nth(n);
        const bool down = buttons.intersects(bit);
        if (down == buttons_.intersects(bit))
            continue;

        const KartKey button = gui_held_() ? kKartMouseButtonsHostOrder[n] : kKartMouseButtons[n];
        send_event_(down ? kKeyDown : kKeyUp, button, now);
    }
    buttons_ = buttons;
}

void KartController::run_timers_(Instant now) noexcept {

    if (waiting_ack_() && now >= ack_due_)
        state_ = State::Idle;
    if (in_reset_() && now >= ack_due_) {
        send_(State::Rak1, kHrst, now);
        ack_due_ = now + kResetTimeout;
    }
}

void KartController::tick(Instant now) noexcept {
    TASTY_SEAT_BODY(KartController);
    run_timers_(now);
}

void KartController::controller_word(hal::LinkWord word, Instant now) noexcept {
    TASTY_SEAT_BODY(KartController);
    run_timers_(now);
    dispatch_(KartByte::of(word), now);
}

void KartController::dispatch_(KartByte byte, Instant now) noexcept {
    if (byte == kHrst) {
        reset_();
        send_(State::Rak1, kHrst, now);
        ack_due_ = now + kResetTimeout;
        return;
    }
    if (byte == kRak1 || byte == kRak2) {
        const bool first = byte == kRak1;
        if (state_ != (first ? State::Rak1 : State::Rak2)) {
            state_ = State::Hrst;
            return;
        }
        send_(first ? State::Rak2 : State::Idle, byte, now);
        ack_due_ = now + kResetTimeout;
        return;
    }
    if (byte == kRqid) {
        send_(State::Idle, kKbid, now);
        return;
    }
    if (byte == kBack) {
        if (state_ != State::WaitAck1) {
            state_ = State::Hrst;
            return;
        }

        state_ = State::Idle;
        drain_one_(now);
        state_ = State::Idle;
        drain_one_(now);
        return;
    }
    if (byte == kNack || byte == kSack || byte == kMack || byte == kSmak) {
        scan_enabled_ = byte == kSack || byte == kSmak;
        mouse_enabled_ = byte == kMack || byte == kSmak;
        state_ = State::Idle;
        drain_one_(now);
    }
}

std::optional<IInputSink::Instant> KartController::deadline() const noexcept {
    TASTY_SEAT_BODY(KartController);
    if (in_reset_() || waiting_ack_())
        return ack_due_;
    return std::nullopt;
}

std::optional<proto::Ps2Frame> KartController::next_frame() noexcept {
    TASTY_SEAT_BODY(KartController);
    const std::optional<KartByte> byte = outbox_.pop();
    if (!byte)
        return std::nullopt;
    return byte->frame();
}

}
