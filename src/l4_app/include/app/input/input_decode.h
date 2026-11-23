// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include "app/core_input_facts.h"
#include "app/input/device_table_loans.h"
#include "app/input/input_decode_tally.h"
#include "app/input/capture_arm.h"
#include "app/input/capture_hit.h"
#include "app/input/input_gates_cell.h"
#include "app/input/input_key_counts.h"
#include "app/input/joy_level_emitter.h"
#include "app/input/stick_level_emitter.h"
#include "infra/tally.h"
#include "app/input/joy_menu_chords.h"
#include "app/input/key_focus.h"
#include "app/input/ps2_egress.h"
#include "app/input/rumble_players_cell.h"
#include "app/input/launcher_key_bridge.h"
#include "app/launcher_state.h"
#include "app/input/mouse_accumulator.h"
#include "app/input/mouse_divider.h"
#include "app/input/pad_egress.h"
#include "app/input/raw_key_edge.h"
#include "app/input/rumble_relay.h"
#include "cores/input_sink.h"
#include "cores/input_sink_id.h"
#include "cores/key_policy.h"
#include "infra/xthread/coalesced_kick.h"
#include "infra/xthread/counter.h"
#include "infra/error.h"
#include "infra/indexed_array.h"
#include "infra/message_sum.h"
#include "infra/opt_ref.h"
#include "infra/xthread/park_fds.h"
#include "infra/park_timeout.h"
#include "infra/xthread/telemetry.h"
#include "infra/prelude.h"
#include "os/clock.h"
#include "os/epoll_set.h"
#include "proto/link_event.h"
#include "proto/ps2_keyboard.h"
#include "proto/ps2_mouse.h"
#include "infra/xthread/wake_flag.h"
#include "hal/boards_table.h"
#include "svc/input/device_table.h"
#include "svc/input_gates.h"
#include "svc/input/input_service_counts.h"
#include "svc/input/input_service.h"
#include "svc/types.h"
#include "infra/seat.h"

namespace mister::infra::xthread {
class DiagLog;
}

namespace mister::app {
template <SeatTag Seat>
class LinkTxChannel;
}

namespace mister::app::input {
class HostLedWatch;
class LwPadEndpoint;

struct DeviceInputState {
    svc::JoyMask prev_menu{};
    std::uint32_t key = 0;
    KeyDownSet ui_down{};
    friend constexpr bool operator==(const DeviceInputState&, const DeviceInputState&) = default;
};

struct InputDecodeCounts {
    std::uint32_t rounds = 0;
    std::uint32_t events = 0;
    std::uint32_t keys_published = 0;
    std::uint32_t keys_emitted = 0;
    std::uint32_t mouse_packets = 0;
    std::uint32_t kicks = 0;
    std::uint32_t fd_evictions = 0;
    std::uint32_t registered = 0;
    svc::JoyMask joy_seen{};
    svc::PlayerMask joy_players{};
    std::uint32_t captures = 0;

    InputKeyCounts keys{};

    svc::input::InputServiceCounts service{};
};

class InputDecode {
    TASTY_SEAT_RESIDENT(Input);

public:
    static constexpr SeatTag kSeat = SeatTag::Input;

    using Counts = InputDecodeCounts;

    using CountsCell = xthread::Telemetry<Counts, SeatTag::Input>;

    class Fds {
        TASTY_SEAT_EXEMPT(boot);

    public:
        [[nodiscard]] static Ex<Fds> create(
            xthread::WakeFlag& ctrl,
            infra::OptRef<const HostLedWatch> led = {}
        ) noexcept;
        Fds(Fds&&) noexcept = default;
        Fds(const Fds&) = delete;
        Fds& operator=(const Fds&) = delete;
        Fds& operator=(Fds&&) = delete;

    private:
        friend class InputDecode;

        explicit Fds(os::EpollSet epoll) noexcept : epoll_(std::move(epoll)) {}

        os::EpollSet epoll_;
    };

    struct Wiring {
        xthread::DiagLog& diag;
        LinkTxChannel<SeatTag::Input>& inbox;
        xthread::CoalescedKick<SeatTag::Input, SeatTag::RT>& rt_kick;
        infra::OptRef<const LauncherStateCell> cell{};
        infra::OptRef<LauncherKeyBridge> keys{};
        MenuKeyInbox& menu_keys;

        infra::OptRef<HostLedWatch> led{};

        xthread::WakeFlag& ctrl;

        InputGatesCell& gates;
        const KeyFocusCell& key_focus;
        xthread::WakeFlag& hotplug;
        const xthread::WakeFlag& joy_edge_reset;
        xthread::Counter& activity;
        const CaptureArmCell& capture_arm;
        CaptureHitCell& capture_hit;
        RumblePlayersCell& rumble_players;

        infra::OptRef<LwPadEndpoint> pads{};

        const CoreInputCell& core_facts;
    };

    InputDecode(
        Fds fds,
        svc::input::InputService& svc,
        DeviceTableLoans& tables,
        os::IClock& clock,
        Wiring w
    ) noexcept;
    InputDecode(const InputDecode&) = delete;
    InputDecode& operator=(const InputDecode&) = delete;

    void on(const proto::LinkEvent::Ps2Control& c);
    void on(const proto::LinkEvent::Ps2ControlEnded&) noexcept;

    void on(const proto::LinkEvent::PolledWord& w) noexcept;

    void end_ps2_control() noexcept;
    void misrouted(const proto::LinkEvent& m) noexcept;

    std::uint32_t link_event_misrouted() const noexcept { return link_event_misrouted_; }

    [[nodiscard]] Ex<void> watch(const xthread::ParkFds& fds) noexcept;

    void park() noexcept;

    void unpark() noexcept;

    [[nodiscard]] bool parked() const noexcept;

    void stop_pads() noexcept;

    [[nodiscard]] bool muted() const noexcept { return muted_; }

    void begin_round();
    void prepare_wait();
    void wait_events(infra::ParkTimeout timeout) noexcept;

    [[nodiscard]] infra::ParkTimeout wait_bound(infra::ParkTimeout timeout) const noexcept;

    [[nodiscard]] bool begin_batch();
    unsigned decode_batch();

    Counts counts() const noexcept;

    const CountsCell& counts_cell() const noexcept { return counts_cell_; }

    int epoll_fd() const noexcept { return epoll_.fd(); }

    std::uint32_t joy_ui_keys() const noexcept { return joy_ui_keys_; }

    proto::Ps2Keyboard& keyboard() noexcept { return kbd_; }

    proto::Ps2Mouse& mouse() noexcept { return mouse_; }

    const RumbleRelay& rumble() const noexcept { return rumble_; }

    [[nodiscard]] bool f12_mod_needed() const noexcept { return f12_mod_needed_; }

    [[nodiscard]] bool sink_bound() const noexcept { return sink_.has_value(); }

private:
    enum class Reseat : std::uint8_t { Skip, Bridge };

    struct ReadyFd {
        unsigned decoded = 0;
        bool published = false;
    };

    void rebuild_registrations(Reseat bridge);
    void publish_from(const svc::input::DeviceReport& r, bool is_mouse, svc::DeviceSlot slot);

    void maybe_capture(const svc::input::InputDevice& d, svc::DeviceSlot slot);

    ReadyFd drain_ready_fd(os::EpollSet::Event e);
    bool push_ui_edge(const RawKeyEdge& e);
    void flush_mouse_();

    void bind_core_facts_() noexcept;

    void serve_sink_() noexcept;

    void kick_for_landed_() noexcept;

    [[nodiscard]] bool is_osd_toggle_(svc::EvdevCode code) const noexcept;
    bool apply_requests();

    void publish_gates_() noexcept;

    unsigned take_tables_() noexcept;
    void deregister_devices_() noexcept;
    void evict_fd(int fd);

    xthread::WakeFlag& ctrl_;
    InputGatesCell& gates_cell_;
    const KeyFocusCell& key_focus_;
    xthread::WakeFlag& hotplug_;
    const xthread::WakeFlag& joy_edge_reset_;
    xthread::Counter& activity_;
    const CaptureArmCell& capture_arm_;
    CaptureHitCell& capture_hit_;
    RumblePlayersCell& rumble_players_;
    KeyFocusCell::Reader focus_reader_{};
    svc::input::InputService& svc_;
    DeviceTableLoans::Worker tables_rx_;
    xthread::DiagLog& diag_;
    LinkTxChannel<SeatTag::Input>& inbox_;

    xthread::CoalescedKick<SeatTag::Input, SeatTag::RT>& rt_kick_;
    infra::Tally kicked_at_{};

    svc::InputGates gates_{};
    svc::InputGates published_gates_{};

    KeyFocus focus_ = KeyFocus::Core;
    InputDecodeTally tally_{};
    RumbleRelay rumble_;

    bool f12_mod_needed_ = false;

    os::IClock& clock_;
    const CoreInputCell& core_facts_;
    CoreInputCell::Reader core_facts_reader_{};
    CoreInputFacts core_facts_scratch_{};
    bool forces_gui_f12_ = false;

    cores::KeyPolicy key_policy_{};
    bool gui_latched_ = false;

    std::array<std::unique_ptr<cores::IInputSink>, infra::ordinal(cores::InputSinkId::kCount)>
        sinks_{};
    infra::OptRef<cores::IInputSink> sink_{};
    infra::OptRef<const LauncherStateCell> launcher_cell_;
    infra::OptRef<LauncherKeyBridge> launcher_keys_;
    JoyMenuChords chords_;

    MenuKeyInbox& menu_keys_;
    infra::OptRef<HostLedWatch> led_;
    bool bridging_ = false;

    proto::Ps2Keyboard kbd_{};
    proto::Ps2Mouse mouse_{};

    MouseDivider mouse_divider_;
    infra::OptRef<LwPadEndpoint> pads_;
    PadEgress pad_egress_;
    JoyLevelEmitter joy_;
    StickLevelEmitter sticks_;

    InputKeyCounts keys_{};

    Ps2Egress egress_;

    infra::IndexedArray<DeviceInputState, svc::input::DeviceTable::kMaxDevices, svc::DeviceSlot>
        devices_{};
    std::uint32_t joy_ui_keys_ = 0;
    std::uint32_t link_event_misrouted_ = 0;

    os::EpollSet epoll_;

    std::uint32_t registered_ = 0;
    svc::Generation seen_gen_{};

    static constexpr std::uint32_t kEvictRearmCredit = 4;
    std::uint32_t rearm_credit_ = kEvictRearmCredit;

    CaptureArmCell::Reader arm_reader_{};
    CaptureArm arm_{};
    CaptureToken capture_served_{};

    int stop_fd_ = -1;
    infra::OptRef<xthread::WakeFlag> pause_wake_;
    bool muted_ = false;

    Ex<os::EpollSet::Ready> ready_{os::EpollSet::Ready{}};
    bool rebuilt_ = false;

    CountsCell counts_cell_{};
};

static_assert(
    hal::every_thread_map([](const hal::ThreadMap& m) {
        return hal::seat_of(m, InputDecode::kSeat).policy == hal::SchedPolicy::Fifo;
    }),
    "item: the decode round is the input LATENCY path — a pad "
    "edge must reach the wire before T-RT's next round. On a "
    "SCHED_OTHER row it queues behind every T-DIAG/T-UI runnable and "
    "the coalesced kick buys nothing."
);
static_assert(
    hal::every_thread_map([](const hal::ThreadMap& m) {
        return hal::seat_of(m, InputDecode::kSeat).prio < hal::seat_of(m, SeatTag::RT).prio
            && hal::seat_of(m, InputDecode::kSeat).cpu != hal::seat_of(m, SeatTag::RT).cpu;
    }),
    "item: this lane owns a SECOND epoll set and BLOCKS in it. "
    "T-RT's seat has exactly one legal block point (the Executive's "
    "own epoll_wait, reactor/executive.h) and no legal sleep at "
    "all, so seating this lane at T-RT's priority or on T-RT's CPU "
    "puts a second block point on the RT partition."
);
static_assert(
    hal::every_thread_map([](const hal::ThreadMap& m) {
        return hal::seat_of(m, InputDecode::kSeat).prio > hal::seat_of(m, SeatTag::Prefetch).prio;
    }),
    "item: this is the INPUT half of the "
    "kFifoOrder T-INPUT > T-PREFETCH edge — a decode round must PREEMPT a "
    "CHD hunk decode (measured 3.1-11.0 ms), never queue behind "
    "one."
);

}
