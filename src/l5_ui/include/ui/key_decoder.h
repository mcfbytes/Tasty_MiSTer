// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

#include "infra/seat.h"
#include "proto/types.h"
#include "svc/types.h"
#include "ui/ui_event.h"

namespace mister::os {
class IClock;
}

namespace mister::ui {

class KeyDecoder {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit KeyDecoder(const os::IClock& clock) noexcept : clock_(&clock) {}

    void push_raw(proto::KeyCode code) noexcept;

    void push_synth(proto::KeyCode code) noexcept;

    void reset() noexcept;

    std::optional<UiEvent> pop() noexcept;

    void set_ini_select_mode(bool on) noexcept { ini_select_ = on; }

    bool ini_select_mode() const noexcept { return ini_select_; }

    void set_repeat_all(bool on) noexcept { repeat_all_ = on; }

    bool repeat_all() const noexcept { return repeat_all_; }

    bool pending() const noexcept { return !ring_.empty() || db_pending_ != proto::KeyCode{}; }

    std::uint32_t dropped() const noexcept { return dropped_; }

    std::uint16_t repeat_depth() const noexcept { return hold_cnt_; }

    static UiButtonMask decode(proto::KeyCode code, bool ini_select) noexcept;

    static bool repeatable(proto::KeyCode code) noexcept;

    [[nodiscard]] static constexpr bool is_make(proto::KeyCode code, svc::EvdevCode key) noexcept {
        return !code.is_release() && svc::EvdevCode::of(code) == key;
    }

private:
    static constexpr std::size_t kQueue = 32;

    struct QueuedKey {
        proto::KeyCode code{};
        bool synth = false;
    };

    class KeyRing {
        TASTY_SEAT_EXEMPT(component);

    public:
        bool push(QueuedKey k) noexcept {
            bool dropped = false;
            if (count_ == kQueue) {
                head_ = (head_ + 1u) % kQueue;
                --count_;
                dropped = true;
            }
            queue_[(head_ + count_) % kQueue] = k;
            ++count_;
            return dropped;
        }

        [[nodiscard]] bool empty() const noexcept { return count_ == 0; }

        [[nodiscard]] std::size_t size() const noexcept { return count_; }

        [[nodiscard]] QueuedKey& head() noexcept { return queue_[head_]; }

        [[nodiscard]] const QueuedKey& head() const noexcept { return queue_[head_]; }

        QueuedKey take() noexcept {
            const QueuedKey k = queue_[head_];
            head_ = (head_ + 1u) % kQueue;
            --count_;
            return k;
        }

    private:
        std::array<QueuedKey, kQueue> queue_{};
        std::size_t head_ = 0;
        std::size_t count_ = 0;
    };

    void enqueue(proto::KeyCode code, bool synth) noexcept;

    std::optional<UiEvent> deliver_(
        proto::KeyCode code,
        std::chrono::nanoseconds now,
        bool first
    ) noexcept;

    std::optional<UiEvent> accept_(proto::KeyCode code, std::chrono::nanoseconds now) noexcept;

    const os::IClock* clock_;
    KeyRing ring_;
    bool ini_select_ = false;
    bool repeat_all_ = false;
    std::uint32_t dropped_ = 0;

    proto::KeyCode last_seen_{};
    proto::KeyCode db_pending_{};
    std::chrono::nanoseconds db_at_{};
    std::chrono::nanoseconds repeat_at_{};
    std::uint16_t hold_cnt_ = 0;
};

}
