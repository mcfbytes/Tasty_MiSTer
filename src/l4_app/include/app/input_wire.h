// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "infra/error.h"
#include "infra/spsc_ring.h"
#include "infra/telemetry.h"
#include "infra/wake_flag.h"
#include "app/activity_source.h"
#include "app/capture_arm.h"
#include "app/capture_hit.h"
#include "app/raw_key_edge.h"
#include "svc/input_service.h"
#include "svc/joy_plan.h"
#include "infra/seat.h"

namespace mister::app {

using CaptureArmCell = xthread::Telemetry<CaptureArm, SeatTag::Ui>;
using CaptureHitCell = xthread::Telemetry<CaptureHit, SeatTag::Input>;

using RumblePadsCell = xthread::Telemetry<std::uint8_t, SeatTag::Input>;

class InputWire final : public IActivitySource {
    TASTY_SEAT_MEDIATOR(Input, Any);

public:
    static constexpr std::uint32_t kGateGrabbed = 1u << 0;
    static constexpr std::uint32_t kGateOsdVisible = 1u << 1;

    static constexpr std::uint32_t kGateKeysDropped = 1u << 2;

    static constexpr std::uint32_t kReqGrabOn = 1u << 1;
    static constexpr std::uint32_t kReqGrabApply = 1u << 2;

    static constexpr std::uint32_t kReqHotplug = 1u << 4;

    static constexpr std::uint32_t kReqOsdOn = 1u << 5;
    static constexpr std::uint32_t kReqOsdApply = 1u << 6;

    static constexpr std::size_t kUiKeyRingSlots = 256;

    InputWire() noexcept = default;
    InputWire(const InputWire&) = delete;
    InputWire& operator=(const InputWire&) = delete;

    void publish_joy(svc::PlayerIndex player, svc::JoyMask mask, svc::JoyMask autofire) noexcept {
        if (player.v >= svc::kMaxPlayers) return;
        const std::uint64_t cell = (static_cast<std::uint64_t>(autofire.v) << 32) | mask.v;
        joy_[player.v].store(cell, std::memory_order_release);
    }

    struct JoyCell {
        svc::JoyMask mask{};
        svc::JoyMask autofire{};
    };
    JoyCell joy(svc::PlayerIndex player) const noexcept {
        if (player.v >= svc::kMaxPlayers) return JoyCell{};
        const std::uint64_t cell = joy_[player.v].load(std::memory_order_acquire);
        return JoyCell{svc::JoyMask{static_cast<std::uint32_t>(cell)},
                       svc::JoyMask{static_cast<std::uint32_t>(cell >> 32)}};
    }

    bool push_ui_key(RawKeyEdge e) noexcept;

    std::optional<RawKeyEdge> pop_ui_key() noexcept { return ui_keys_.pop(); }
    std::size_t ui_keys_queued() const noexcept { return ui_keys_.size(); }
    std::uint32_t ui_keys_dropped() const noexcept {
        return ui_keys_dropped_.load(std::memory_order_relaxed);
    }
    std::uint32_t ui_keys_published() const noexcept {
        return ui_keys_pub_.load(std::memory_order_relaxed);
    }

    std::uint32_t keys_dropped() const noexcept {
        return keys_dropped_.load(std::memory_order_relaxed);
    }

    void note_key_drop(unsigned n) noexcept {
        bump(keys_dropped_, n);
        publish_gates(kGateKeysDropped, 0u);
    }

    void note_key_sweep(unsigned released) noexcept {
        bump(key_sweeps_, 1u);
        bump(keys_released_, released);
    }
    std::uint32_t key_sweeps() const noexcept {
        return key_sweeps_.load(std::memory_order_relaxed);
    }
    std::uint32_t keys_released() const noexcept {
        return keys_released_.load(std::memory_order_relaxed);
    }

    static constexpr std::size_t kCoreNameCap = 32;

    struct CoreNameCell {

        CoreNameCell() noexcept = default;
        char name[kCoreNameCap] = {};
        std::uint8_t len = 0;
        bool front_end = false;
        std::string_view view() const noexcept { return std::string_view(name, len); }
    };

    void publish_core_name(std::string_view name, bool front_end) noexcept {
        CoreNameCell c{};
        std::size_t n = name.size();
        if (n > kCoreNameCap - 1u) n = kCoreNameCap - 1u;
        for (std::size_t i = 0; i < n; ++i)
            c.name[i] = name[i];
        c.name[n] = '\0';
        c.len = static_cast<std::uint8_t>(n);
        c.front_end = front_end;
        name_cell_.publish(c);
    }

    CoreNameCell core_name() const noexcept { return name_cell_.sample().value; }

    void publish_joy_plan(const svc::JoyPlan& plan) noexcept { plan_cell_.publish(plan); }

    svc::JoyPlan joy_plan() const noexcept { return plan_cell_.sample().value; }

    CaptureArmCell& capture_arm() noexcept { return capture_arm_; }
    const CaptureArmCell& capture_arm() const noexcept { return capture_arm_; }
    CaptureHitCell& capture_hit() noexcept { return capture_hit_; }
    const CaptureHitCell& capture_hit() const noexcept { return capture_hit_; }

    RumblePadsCell& rumble_pads_cell() noexcept { return rumble_pads_; }
    const RumblePadsCell& rumble_pads_cell() const noexcept { return rumble_pads_; }

    std::uint8_t rumble_pads() const noexcept { return rumble_pads_.sample().value; }

    void publish_analog_reshape(const svc::IAnalogReshape* r) noexcept {
        reshape_.store(r, std::memory_order_release);
    }

    const svc::IAnalogReshape* analog_reshape() const noexcept {
        return reshape_.load(std::memory_order_acquire);
    }

    void publish_edge_reset() noexcept {
        edge_reset_.store(edge_reset_.load(std::memory_order_relaxed) + 1u,
                          std::memory_order_release);
    }

    std::uint32_t edge_reset_seq() const noexcept {
        return edge_reset_.load(std::memory_order_acquire);
    }

    void publish_gates(std::uint32_t set, std::uint32_t clear) noexcept {
        std::uint32_t g = gates_.load(std::memory_order_relaxed);
        g = (g | set) & ~clear;
        gates_.store(g, std::memory_order_release);
    }
    std::uint32_t gates() const noexcept { return gates_.load(std::memory_order_acquire); }

    void note_activity() noexcept { bump(activity_seq_, 1u); }
    std::uint32_t activity_seq() const noexcept override {
        return activity_seq_.load(std::memory_order_acquire);
    }
    bool input_grabbed() const noexcept override { return (gates() & kGateGrabbed) != 0u; }

    void update_osd_request(bool on) noexcept {
        std::uint32_t cur = req_.load(std::memory_order_relaxed);
        std::uint32_t next = 0;
        do {
            next = (cur & ~kReqOsdOn) | kReqOsdApply | (on ? kReqOsdOn : 0u);
        } while (!req_.compare_exchange_weak(cur, next, std::memory_order_release,
                                             std::memory_order_relaxed));
    }

    [[nodiscard]] std::optional<bool> take_osd_request() noexcept {
        std::uint32_t cur = req_.load(std::memory_order_acquire);
        std::uint32_t next = 0;
        do {
            if ((cur & kReqOsdApply) == 0u) return std::nullopt;
            next = cur & ~kReqOsdApply;
        } while (!req_.compare_exchange_weak(cur, next, std::memory_order_acq_rel,
                                             std::memory_order_acquire));
        return (cur & kReqOsdOn) != 0u;
    }

    void request(std::uint32_t set) noexcept { req_.fetch_or(set, std::memory_order_release); }
    void clear_request(std::uint32_t clear) noexcept {
        req_.fetch_and(~clear, std::memory_order_release);
    }
    std::uint32_t requests() const noexcept { return req_.load(std::memory_order_acquire); }

    bool arm_kick() noexcept { return !kick_armed_.exchange(true, std::memory_order_acq_rel); }
    void consume_kick() noexcept { kick_armed_.store(false, std::memory_order_release); }
    bool kick_armed() const noexcept { return kick_armed_.load(std::memory_order_acquire); }

    [[nodiscard]] Ex<void> open_ctrl() noexcept;

    int ctrl_fd() const noexcept { return ctrl_.fd(); }

    void wake() noexcept { ctrl_.kick(); }

    void drain_ctrl() noexcept { ctrl_.drain(); }

    void on_core_loaded(std::string_view core_name, bool front_end, const svc::JoyPlan& plan,
                        const svc::IAnalogReshape* analog_reshape) noexcept;
    void on_core_detached() noexcept;

    void note_rebuild_refused() noexcept { bump(rebuilds_refused_, 1u); }
    void note_rebuild_done() noexcept { bump(rebuilds_done_, 1u); }

    static void bump(std::atomic<std::uint32_t>& c, std::uint32_t by) noexcept {
        c.store(c.load(std::memory_order_relaxed) + by, std::memory_order_relaxed);
    }
    std::uint32_t rebuilds_refused() const noexcept {
        return rebuilds_refused_.load(std::memory_order_relaxed);
    }
    std::uint32_t rebuilds_done() const noexcept {
        return rebuilds_done_.load(std::memory_order_relaxed);
    }

    static constexpr std::uint16_t kButtonOsd = 1u << 0;
    static constexpr std::uint16_t kButtonUser = 1u << 1;

    void publish_buttons(std::uint16_t level) noexcept {
        buttons_.store(level, std::memory_order_relaxed);
        if (level != 0u) bump(button_samples_, 1u);
    }
    std::uint16_t buttons() const noexcept {
        return static_cast<std::uint16_t>(buttons_.load(std::memory_order_relaxed));
    }

    std::uint32_t button_samples() const noexcept {
        return button_samples_.load(std::memory_order_relaxed);
    }

    void note_button_action() noexcept { bump(button_actions_, 1u); }
    std::uint32_t button_actions() const noexcept {
        return button_actions_.load(std::memory_order_relaxed);
    }

    std::uint32_t core_edges() const noexcept {
        return core_edges_.load(std::memory_order_relaxed);
    }

    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                  "item: every joystick cell is one naturally-aligned "
                  "64-bit atomic. ARMv7 has LDREXD/STREXD so this holds on the "
                  "binding toolchain — but PROVE it here rather than assume it: "
                  "a libatomic call on the T-RT drain is a lock on the RT path.");
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                  "the two gate words and every counter must be lock-free too");
    static_assert(std::atomic<const svc::IAnalogReshape*>::is_always_lock_free,
                  "the reshape cell is ONE pointer and is read on T-DIAG in "
                  "the rebuild: a libatomic lock here would be a lock on the "
                  "hotplug path");

private:
    std::atomic<std::uint64_t> joy_[svc::kMaxPlayers]{};

    xthread::SpscRing<RawKeyEdge, kUiKeyRingSlots> ui_keys_{};
    std::atomic<std::uint32_t> ui_keys_dropped_{0};
    std::atomic<std::uint32_t> ui_keys_pub_{0};

    std::atomic<std::uint32_t> edge_reset_{0};

    xthread::Telemetry<CoreNameCell, SeatTag::Ui> name_cell_{};

    xthread::Telemetry<svc::JoyPlan, SeatTag::Ui> plan_cell_{};

    std::atomic<const svc::IAnalogReshape*> reshape_{nullptr};

    CaptureArmCell capture_arm_{};
    CaptureHitCell capture_hit_{};
    RumblePadsCell rumble_pads_{};

    xthread::WakeFlag ctrl_{};

    std::atomic<std::uint32_t> gates_{0};

    std::atomic<std::uint32_t> activity_seq_{0};
    std::atomic<std::uint32_t> req_{0};
    std::atomic<bool> kick_armed_{false};

    std::atomic<std::uint32_t> keys_dropped_{0};
    std::atomic<std::uint32_t> key_sweeps_{0};
    std::atomic<std::uint32_t> keys_released_{0};
    std::atomic<std::uint32_t> rebuilds_refused_{0};
    std::atomic<std::uint32_t> rebuilds_done_{0};

    std::atomic<std::uint32_t> buttons_{0};
    std::atomic<std::uint32_t> button_samples_{0};
    std::atomic<std::uint32_t> button_actions_{0};
    std::atomic<std::uint32_t> core_edges_{0};
};

}  // namespace mister::app
