// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

#include "app/device_table_loans.h"
#include "app/input_decode_tally.h"
#include "app/capture_arm.h"
#include "app/capture_hit.h"
#include "app/input_gates_cell.h"
#include "app/input_key_counts.h"
#include "app/joy_level_emitter.h"
#include "app/joy_menu_chords.h"
#include "app/key_focus.h"
#include "app/ps2_egress.h"
#include "app/rumble_players_cell.h"
#include "app/launcher_key_bridge.h"
#include "app/launcher_state.h"
#include "app/mouse_accumulator.h"
#include "app/mouse_divider.h"
#include "app/raw_key_edge.h"
#include "app/rumble_relay.h"
#include "infra/coalesced_kick.h"
#include "infra/counter.h"
#include "infra/error.h"
#include "infra/indexed_array.h"
#include "infra/opt_ref.h"
#include "infra/park_fds.h"
#include "infra/park_timeout.h"
#include "infra/telemetry.h"
#include "os/clock.h"
#include "os/epoll_set.h"
#include "proto/link_event.h"
#include "proto/ps2_keyboard.h"
#include "proto/ps2_mouse.h"
#include "infra/wake_flag.h"
#include "hal/boards_table.h"
#include "svc/device_table.h"
#include "svc/input_gates.h"
#include "svc/input_service_counts.h"
#include "svc/input_service.h"
#include "svc/types.h"
#include "infra/seat.h"

namespace mister::xthread {
class DiagLog;
}

namespace mister::app {
class HostLedWatch;
template <SeatTag Seat>
class LinkTxChannel;

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

    svc::InputServiceCounts service{};
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
    };

    InputDecode(
        Fds fds,
        svc::InputService& svc,
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

    void mute(bool on) noexcept { muted_ = on; }

    [[nodiscard]] bool muted() const noexcept { return muted_; }

    void begin_round();
    void prepare_wait();
    void wait_events(infra::ParkTimeout timeout) noexcept;

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

private:
    enum class Reseat : std::uint8_t { Skip, Bridge };

    struct ReadyFd {
        unsigned decoded = 0;
        bool published = false;
    };

    void rebuild_registrations(Reseat bridge);
    void publish_from(const svc::DeviceReport& r, bool is_mouse, svc::DeviceSlot slot);

    void maybe_capture(const svc::InputDevice& d, svc::DeviceSlot slot);

    ReadyFd drain_ready_fd(os::EpollSet::Event e);
    bool push_ui_edge(const RawKeyEdge& e);
    void flush_mouse_();

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
    svc::InputService& svc_;
    DeviceTableLoans::Worker tables_rx_;
    xthread::DiagLog& diag_;
    LinkTxChannel<SeatTag::Input>& inbox_;

    xthread::CoalescedKick<SeatTag::Input, SeatTag::RT>& rt_kick_;

    svc::InputGates gates_{};
    svc::InputGates published_gates_{};

    KeyFocus focus_ = KeyFocus::Core;
    InputDecodeTally tally_{};
    RumbleRelay rumble_;

    bool f12_mod_needed_ = false;
    infra::OptRef<const LauncherStateCell> launcher_cell_;
    infra::OptRef<LauncherKeyBridge> launcher_keys_;
    JoyMenuChords chords_;

    MenuKeyInbox& menu_keys_;
    infra::OptRef<HostLedWatch> led_;
    bool bridging_ = false;

    proto::Ps2Keyboard kbd_{};
    proto::Ps2Mouse mouse_{};

    MouseDivider mouse_divider_;
    JoyLevelEmitter joy_;

    InputKeyCounts keys_{};

    Ps2Egress egress_;

    infra::IndexedArray<DeviceInputState, svc::DeviceTable::kMaxDevices, svc::DeviceSlot>
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
