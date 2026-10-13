// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <sys/epoll.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "app/device_table_channel.h"
#include "app/input_wire.h"
#include "app/launcher_key_bridge.h"
#include "app/launcher_state.h"
#include "app/rumble_relay.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/park_fds.h"
#include "infra/telemetry.h"
#include "os/clock.h"
#include "proto/link_event.h"
#include "proto/ps2_frame.h"
#include "proto/ps2_keyboard.h"
#include "proto/ps2_mouse.h"
#include "infra/unique_fd.h"
#include "infra/wake_flag.h"
#include "hal/boards_table.h"
#include "svc/input_service.h"
#include "infra/seat.h"

namespace mister::xthread {
class DiagLog;
}

namespace mister::app {
static_assert(LauncherKeyBridge::kPadSlots >= svc::kMaxDevices);
class LinkTxChannel;
class LinkRxChannel;

struct InputDecodeCounts {
    std::uint32_t rounds = 0;
    std::uint32_t events = 0;
    std::uint32_t keys_published = 0;
    std::uint32_t keys_emitted = 0;
    std::uint32_t mouse_packets = 0;
    std::uint32_t kicks = 0;
    std::uint32_t fd_evictions = 0;
    std::uint32_t registered = 0;
    std::uint32_t joy_seen = 0;
    std::uint32_t joy_players = 0;
    std::uint32_t captures = 0;

    std::uint32_t mapped = 0;
    std::uint32_t slotted = 0;
    std::uint32_t mask_changes = 0;
    std::uint32_t axis_edges = 0;
    std::uint32_t mouse_remainder = 0;
    std::uint32_t key_overflows = 0;
    std::uint32_t quirk_drops = 0;
    std::uint32_t slot_live = 0;
    std::uint32_t slot_ghosts = 0;
    std::uint32_t slot_hash[svc::kMaxPlayers]{};
};

class InputDecode {
    TASTY_SEAT_RESIDENT(Input);

public:
    static constexpr SeatTag kSeat = SeatTag::Input;

    using Counts = InputDecodeCounts;

    using CountsCell = xthread::Telemetry<Counts, SeatTag::Input>;

    class Fds {
    public:
        [[nodiscard]] static Ex<Fds> create(InputWire& wire) noexcept;
        Fds(Fds&&) noexcept = default;
        Fds(const Fds&) = delete;
        Fds& operator=(const Fds&) = delete;
        Fds& operator=(Fds&&) = delete;

    private:
        friend class InputDecode;
        explicit Fds(UniqueFd epfd) noexcept : epfd_(std::move(epfd)) {}
        UniqueFd epfd_;
    };

    struct Wiring {
        xthread::DiagLog& diag;
        LinkTxChannel& inbox;
        LinkRxChannel& link_rx;
        xthread::WakeFlag::Kicker rt_wake;
        infra::OptRef<const LauncherStateCell> cell{};
        infra::OptRef<LauncherKeyBridge> keys{};
        infra::OptRef<xthread::WakeFlag> ui_wake{};
    };

    InputDecode(Fds fds, InputWire& wire, svc::InputService& svc, DeviceTableChannel& tables,
                os::IClock& clock, Wiring w) noexcept;
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

    unsigned drop_link_events() noexcept;

    void before_wait();
    void wait_events(int timeout_ms) noexcept;
    unsigned after_wait();

    unsigned decode_round(int timeout_ms);

    Counts counts() const noexcept;
    const CountsCell& counts_cell() const noexcept { return counts_cell_; }

    int epoll_fd() const noexcept { return epfd_.get(); }
    std::size_t registered_fds() const noexcept {
        return registered_.load(std::memory_order_relaxed);
    }

    std::uint32_t joy_ui_keys() const noexcept { return joy_ui_keys_; }

    proto::Ps2Keyboard& keyboard() noexcept { return kbd_; }
    proto::Ps2Mouse& mouse() noexcept { return mouse_; }
    const RumbleRelay& rumble() const noexcept { return rumble_; }

    [[nodiscard]] bool f12_mod_needed() const noexcept { return f12_mod_needed_; }

private:
    enum class Reseat : std::uint8_t { Skip, Bridge };
    void rebuild_registrations(Reseat bridge);
    void publish_joy_levels();
    void publish_from(const svc::DeviceReport& r, bool is_mouse, std::size_t dev_slot);

    void maybe_capture(const svc::InputDevice& d, std::size_t dev_slot);
    void publish_joy_menu(const svc::DeviceReport& r, std::size_t dev_slot, bool osd_visible);
    bool push_ui_edge(const RawKeyEdge& e);
    void drain_link_events_();
    unsigned flush_ps2_frames_();
    void flush_mouse_();
    void sweep_dropped_keys_();

    [[nodiscard]] bool is_osd_toggle_(std::uint16_t code) const noexcept;
    bool apply_requests();

    unsigned take_tables_() noexcept;
    void deregister_devices_() noexcept;
    void evict_fd(int fd);

    InputWire& wire_;
    svc::InputService& svc_;
    DeviceTableChannel::Receiver tables_rx_;
    xthread::DiagLog& diag_;
    LinkTxChannel& inbox_;
    LinkRxChannel& link_rx_;

    xthread::WakeFlag::Kicker rt_wake_;
    RumbleRelay rumble_;

    bool f12_mod_needed_ = false;
    infra::OptRef<const LauncherStateCell> launcher_cell_;
    infra::OptRef<LauncherKeyBridge> launcher_keys_;

    infra::OptRef<xthread::WakeFlag> ui_wake_;
    bool bridging_ = false;

    proto::Ps2Keyboard kbd_{};
    proto::Ps2Mouse mouse_{};
    os::IClock& clock_;

    std::int64_t last_mouse_ns_ = 0;
    bool mouse_pending_ = false;
    std::int32_t carry_x_ = 0, carry_y_ = 0, carry_z_ = 0;
    std::uint32_t wm_frame_drops_ = 0;
    bool sweep_owed_ = false;
    std::uint32_t last_joy_posted_[svc::kMaxPlayers]{};

    bool zero_armed_[svc::kMaxPlayers]{true, true, true, true, true, true};
    std::uint32_t armed_mask_[svc::kMaxPlayers]{};
    std::uint32_t armed_autofire_[svc::kMaxPlayers]{};

    std::uint32_t wm_edge_reset_ = 0;

    std::uint32_t wm_gates_ = 0;

    std::uint32_t joy_prev_[svc::kMaxDevices] = {};
    std::uint32_t joy_key_[svc::kMaxDevices] = {};

    std::uint64_t ui_key_down_[svc::kMaxDevices][4] = {};

    std::uint32_t osdbtn_ = 0;
    std::uint32_t joy_ui_keys_ = 0;
    std::uint32_t link_event_misrouted_ = 0;

    UniqueFd epfd_;

    std::atomic<std::uint32_t> registered_{0};
    std::uint32_t seen_gen_ = 0;

    static constexpr std::uint32_t kEvictRearmCredit = 4;
    std::uint32_t rearm_credit_ = kEvictRearmCredit;

    std::uint32_t m_x_ = 0, m_y_ = 0, m_w_ = 0;
    std::uint32_t m_edges_ = 0;
    std::uint8_t m_btn_ = 0;

    CaptureArmCell::Reader arm_reader_{};
    CaptureArm arm_{};
    std::uint32_t capture_served_ = 0;

    int stop_fd_ = -1;
    infra::OptRef<xthread::WakeFlag> pause_wake_;
    bool muted_ = false;

    static constexpr std::size_t kMaxEpollEvents = 16;
    epoll_event evs_[kMaxEpollEvents]{};
    int n_ready_ = 0;
    bool rebuilt_ = false;

    CountsCell counts_cell_{};
    std::atomic<std::uint32_t> n_rounds_{0};
    std::atomic<std::uint32_t> n_events_{0};
    std::atomic<std::uint32_t> n_keys_pub_{0};
    std::atomic<std::uint32_t> n_keys_emit_{0};
    std::atomic<std::uint32_t> n_mouse_pkt_{0};
    std::atomic<std::uint32_t> n_kicks_{0};
    std::atomic<std::uint32_t> n_evicted_{0};

    std::atomic<std::uint32_t> n_joy_seen_{0};
    std::atomic<std::uint32_t> n_joy_players_{0};
    std::atomic<std::uint32_t> n_captures_{0};
};

static_assert(hal::every_thread_map([](const hal::ThreadMap& m) {
                  return hal::seat_of(m, InputDecode::kSeat).policy == hal::SchedPolicy::Fifo;
              }),
              "item: the decode round is the input LATENCY path — a pad "
              "edge must reach the wire before T-RT's next round. On a "
              "SCHED_OTHER row it queues behind every T-DIAG/T-UI runnable and "
              "the coalesced kick buys nothing (arch §2).");
static_assert(hal::every_thread_map([](const hal::ThreadMap& m) {
                  return hal::seat_of(m, InputDecode::kSeat).prio <
                             hal::seat_of(m, SeatTag::RT).prio &&
                         hal::seat_of(m, InputDecode::kSeat).cpu !=
                             hal::seat_of(m, SeatTag::RT).cpu;
              }),
              "item: this lane owns a SECOND epoll set and BLOCKS in it. "
              "T-RT's seat has exactly one legal block point (the Executive's "
              "own epoll_wait, reactor/executive.h) and no legal sleep at "
              "all, so seating this lane at T-RT's priority or on T-RT's CPU "
              "puts a second block point on the RT partition.");
static_assert(hal::every_thread_map([](const hal::ThreadMap& m) {
                  return hal::seat_of(m, InputDecode::kSeat).prio >
                         hal::seat_of(m, SeatTag::Prefetch).prio;
              }),
              "item: this is the INPUT half of arch's "
              "input_outranks_prefetch rule — a decode round must PREEMPT a "
              "CHD hunk decode (measured 3.1-11.0 ms), never queue behind "
              "one.");

}  // namespace mister::app
