// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "infra/counter.h"
#include "infra/error.h"
#include "infra/fixed_str.h"
#include "infra/indexed_array.h"
#include "infra/opt_ref.h"
#include "infra/spsc_ring.h"
#include "infra/telemetry.h"
#include "infra/wake_flag.h"
#include "app/capture_arm.h"
#include "app/capture_hit.h"
#include "app/raw_key_edge.h"
#include "app/types.h"
#include "hal/types.h"
#include "proto/frame_drops.h"
#include "svc/input_service.h"
#include "svc/joy_plan.h"
#include "svc/types.h"
#include "infra/seat.h"

namespace mister::app {

struct JoyLevel {
    svc::JoyMask mask{};
    svc::JoyMask autofire{};

    [[nodiscard]] constexpr std::uint64_t packed() const noexcept {
        constexpr unsigned kAutofireShift = 32;
        return (static_cast<std::uint64_t>(autofire.v) << kAutofireShift) | mask.v;
    }

    [[nodiscard]] static constexpr JoyLevel unpack(std::uint64_t cell) noexcept {
        constexpr unsigned kAutofireShift = 32;
        return JoyLevel{
            svc::JoyMask{static_cast<std::uint32_t>(cell)},
            svc::JoyMask{static_cast<std::uint32_t>(cell >> kAutofireShift)}
        };
    }

    friend constexpr bool operator==(JoyLevel, JoyLevel) = default;
};

using CaptureArmCell = xthread::Telemetry<CaptureArm, SeatTag::Ui>;
using CaptureHitCell = xthread::Telemetry<CaptureHit, SeatTag::Input>;

using RumblePlayersCell = xthread::Telemetry<svc::PlayerMask, SeatTag::Input>;

class InputWire final {
    TASTY_SEAT_MEDIATOR(Input, Any);

public:
    static constexpr InputGateBits kGateGrabbed{1u << 0};
    static constexpr InputGateBits kGateOsdVisible{1u << 1};

    static constexpr InputGateBits kGateKeysDropped{1u << 2};

    static constexpr InputRequestBits kReqHotplug{1u << 4};

    static constexpr InputRequestBits kReqOsdOn{1u << 5};
    static constexpr InputRequestBits kReqOsdApply{1u << 6};

    static constexpr std::size_t kUiKeyRingSlots = 256;

    InputWire() noexcept = default;
    InputWire(const InputWire&) = delete;
    InputWire& operator=(const InputWire&) = delete;

    void publish_joy(svc::PlayerIndex player, JoyLevel level) noexcept {
        if (const auto cell = joy_.find(player)) {
            cell->store(level.packed(), std::memory_order_release);
        }
    }

    JoyLevel joy(svc::PlayerIndex player) const noexcept {
        const auto cell = joy_.find(player);
        return cell ? JoyLevel::unpack(cell->load(std::memory_order_acquire)) : JoyLevel{};
    }

    bool push_ui_key(RawKeyEdge e) noexcept;

    std::optional<RawKeyEdge> pop_ui_key() noexcept { return ui_keys_.pop(); }

    std::size_t ui_keys_queued() const noexcept { return ui_keys_.size(); }

    std::uint32_t ui_keys_dropped() const noexcept { return ui_keys_dropped_.get(); }

    std::uint32_t ui_keys_published() const noexcept { return ui_keys_pub_.get(); }

    std::uint32_t keys_dropped() const noexcept { return keys_dropped_.get(); }

    void note_key_drop(proto::FrameDrops n) noexcept {
        keys_dropped_.add(n.v);
        raise_gate(kGateKeysDropped);
    }

    void note_key_sweep(unsigned released) noexcept {
        key_sweeps_.add(1u);
        keys_released_.add(released);
    }

    std::uint32_t key_sweeps() const noexcept { return key_sweeps_.get(); }

    std::uint32_t keys_released() const noexcept { return keys_released_.get(); }

    static constexpr std::size_t kCoreNameCap = 32;

    struct CoreNameCell {

        CoreNameCell() noexcept = default;

        FixedStr<kCoreNameCap, StrFit::Clip> name{};
        bool front_end = false;

        std::string_view view() const noexcept { return name.view(); }
    };

    void publish_core_name(std::string_view name, bool front_end) noexcept {
        CoreNameCell c{};
        (void)c.name.assign(name);
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

    RumblePlayersCell& rumble_pads_cell() noexcept { return rumble_pads_; }

    const RumblePlayersCell& rumble_pads_cell() const noexcept { return rumble_pads_; }

    svc::PlayerMask rumble_pads() const noexcept { return rumble_pads_.sample().value; }

    void publish_analog_reshape(infra::OptRef<const svc::IAnalogReshape> r) noexcept {
        reshape_.publish(r);
    }

    infra::OptRef<const svc::IAnalogReshape> analog_reshape() const noexcept {
        return reshape_.sample().value;
    }

    void publish_edge_reset() noexcept { edge_reset_.request(); }

    const xthread::WakeFlag& edge_reset() const noexcept { return edge_reset_; }

    void set_gate(InputGateBits bit, bool on) noexcept {
        const InputGateBits cur{gates_.load(std::memory_order_relaxed)};
        gates_.store((on ? cur.with(bit) : cur.without(bit)).v, std::memory_order_release);
    }

    void raise_gate(InputGateBits bit) noexcept {
        const InputGateBits cur{gates_.load(std::memory_order_relaxed)};
        gates_.store(cur.with(bit).v, std::memory_order_release);
    }

    InputGateBits gates() const noexcept {
        return InputGateBits{gates_.load(std::memory_order_acquire)};
    }

    bool gated(InputGateBits bit) const noexcept { return gates().has(bit); }

    void note_activity() noexcept { activity_seq_.add(1u); }

    std::uint32_t activity_seq() const noexcept { return activity_seq_.get(); }

    bool input_grabbed() const noexcept { return gated(kGateGrabbed); }

    void update_osd_request(bool on) noexcept {
        std::uint32_t cur = req_.load(std::memory_order_relaxed);
        InputRequestBits next{};
        do {
            next = InputRequestBits{cur}.without(kReqOsdOn).with(kReqOsdApply);
            if (on)
                next = next.with(kReqOsdOn);
        } while (!req_.compare_exchange_weak(
            cur,
            next.v,
            std::memory_order_release,
            std::memory_order_relaxed
        ));
    }

    [[nodiscard]] std::optional<bool> take_osd_request() noexcept {
        std::uint32_t cur = req_.load(std::memory_order_acquire);
        InputRequestBits next{};
        do {
            if (!InputRequestBits{cur}.has(kReqOsdApply))
                return std::nullopt;
            next = InputRequestBits{cur}.without(kReqOsdApply);
        } while (!req_.compare_exchange_weak(
            cur,
            next.v,
            std::memory_order_acq_rel,
            std::memory_order_acquire
        ));
        return InputRequestBits{cur}.has(kReqOsdOn);
    }

    void request(InputRequestBits set) noexcept { req_.fetch_or(set.v, std::memory_order_release); }

    void clear_request(InputRequestBits clear) noexcept {
        req_.fetch_and(~clear.v, std::memory_order_release);
    }

    InputRequestBits requests() const noexcept {
        return InputRequestBits{req_.load(std::memory_order_acquire)};
    }

    bool requested(InputRequestBits bit) const noexcept { return requests().has(bit); }

    bool arm_kick() noexcept { return !kick_armed_.exchange(true, std::memory_order_acq_rel); }

    void consume_kick() noexcept { kick_armed_.store(false, std::memory_order_release); }

    bool kick_armed() const noexcept { return kick_armed_.load(std::memory_order_acquire); }

    [[nodiscard]] Ex<void> open_ctrl() noexcept;

    int ctrl_fd() const noexcept { return ctrl_.fd(); }

    void wake() noexcept { ctrl_.kick(); }

    void drain_ctrl() noexcept { ctrl_.drain(); }

    void on_core_loaded(
        std::string_view core_name,
        bool front_end,
        const svc::JoyPlan& plan,
        infra::OptRef<const svc::IAnalogReshape> analog_reshape
    ) noexcept;
    void on_core_detached() noexcept;

    void note_rebuild_refused() noexcept { rebuilds_refused_.add(1u); }

    void note_rebuild_done() noexcept { rebuilds_done_.add(1u); }

    std::uint32_t rebuilds_refused() const noexcept { return rebuilds_refused_.get(); }

    std::uint32_t rebuilds_done() const noexcept { return rebuilds_done_.get(); }

    void note_button_action() noexcept { button_actions_.add(1u); }

    std::uint32_t button_actions() const noexcept { return button_actions_.get(); }

    std::uint32_t core_edges() const noexcept { return core_edges_.get(); }

    static_assert(
        std::atomic<std::uint64_t>::is_always_lock_free,
        "item: every joystick cell is one naturally-aligned "
        "64-bit atomic. ARMv7 has LDREXD/STREXD so this holds on the "
        "binding toolchain — but PROVE it here rather than assume it: "
        "a libatomic call here would be a lock on a real-time seat."
    );
    static_assert(
        std::atomic<std::uint32_t>::is_always_lock_free,
        "the two gate words and every counter must be lock-free too"
    );

private:
    infra::IndexedArray<std::atomic<std::uint64_t>, svc::kMaxPlayers, svc::PlayerIndex> joy_{};

    xthread::SpscRing<RawKeyEdge, kUiKeyRingSlots> ui_keys_{};
    xthread::Counter ui_keys_dropped_{};
    xthread::Counter ui_keys_pub_{};

    xthread::WakeFlag edge_reset_{};

    xthread::Telemetry<CoreNameCell, SeatTag::Ui> name_cell_{};

    xthread::Telemetry<svc::JoyPlan, SeatTag::Ui> plan_cell_{};

    xthread::Telemetry<infra::OptRef<const svc::IAnalogReshape>, SeatTag::Ui> reshape_{};

    CaptureArmCell capture_arm_{};
    CaptureHitCell capture_hit_{};
    RumblePlayersCell rumble_pads_{};

    xthread::WakeFlag ctrl_{};

    std::atomic<std::uint32_t> gates_{0};

    xthread::Counter activity_seq_{};
    std::atomic<std::uint32_t> req_{0};
    std::atomic<bool> kick_armed_{false};

    xthread::Counter keys_dropped_{};
    xthread::Counter key_sweeps_{};
    xthread::Counter keys_released_{};
    xthread::Counter rebuilds_refused_{};
    xthread::Counter rebuilds_done_{};

    xthread::Counter button_actions_{};
    xthread::Counter core_edges_{};
};

}
