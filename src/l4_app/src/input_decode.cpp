// SPDX-License-Identifier: GPL-3.0-or-later
#include "infra/seat.h"
#include "app/input_decode.h"

#include <sys/epoll.h>

#include <cerrno>
#include <utility>

#include "app/link_event_dispatch.h"
#include "app/link_rx_channel.h"
#include "app/link_tx_channel.h"
#include "infra/diag_log.h"
#include "infra/message_sum.h"

namespace mister::app {

namespace {

Error os_error(std::uint16_t site) {
    return Error{Errc::os, site, static_cast<std::uint32_t>(errno)};
}

constexpr std::uint16_t kKeyF12 = 88;
constexpr std::uint16_t kKeyMenu = 139;
constexpr std::uint16_t kKeyLeftMeta = 125;
constexpr std::uint16_t kKeyRightMeta = 126;

bool is_pass_through_modifier(std::uint16_t code) noexcept {
    return code == 56 || code == 100 || code == 125 || code == 126;
}

bool ui_down_test(const std::uint64_t (&bits)[4], std::uint16_t code) noexcept {
    return (bits[code >> 6] & (std::uint64_t{1} << (code & 63u))) != 0u;
}

void ui_down_set(std::uint64_t (&bits)[4], std::uint16_t code, bool on) noexcept {
    const std::uint64_t m = std::uint64_t{1} << (code & 63u);
    if (on) {
        bits[code >> 6] |= m;
    } else {
        bits[code >> 6] &= ~m;
    }
}

}

Ex<InputDecode::Fds> InputDecode::Fds::create(InputWire& wire) noexcept {
    if (auto c = wire.open_ctrl(); !c)
        return std::unexpected(c.error());
    UniqueFd epfd{::epoll_create1(EPOLL_CLOEXEC)};
    if (!epfd.valid())
        return std::unexpected(os_error(ERR_SITE()));
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = wire.ctrl_fd();
    if (::epoll_ctl(epfd.get(), EPOLL_CTL_ADD, wire.ctrl_fd(), &ev) != 0) {
        return std::unexpected(os_error(ERR_SITE()));
    }
    return Fds{std::move(epfd)};
}

InputDecode::InputDecode(
    Fds fds,
    InputWire& wire,
    svc::InputService& svc,
    DeviceTableChannel& tables,
    os::IClock& clock,
    Wiring w
) noexcept
    : wire_(wire),
      svc_(svc),
      tables_rx_(tables),
      diag_(w.diag),
      inbox_(w.inbox),
      link_rx_(w.link_rx),
      rt_wake_(w.rt_wake),
      rumble_(svc, wire.rumble_pads_cell(), w.diag),
      launcher_cell_(w.cell),
      launcher_keys_(w.cell ? w.keys : infra::OptRef<LauncherKeyBridge>{}),
      ui_wake_(w.ui_wake),
      mouse_divider_(clock),
      joy_(wire, svc, w.inbox, tally_),
      epfd_(std::move(fds.epfd_)) {
    (void)take_tables_();
    rebuild_registrations(Reseat::Skip);
}

Ex<void> InputDecode::watch(const xthread::ParkFds& fds) noexcept {
    const int fd[2] = {fds.stop_fd(), fds.wake().fd()};
    for (const int f : fd) {
        if (f < 0)
            return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = f;
        if (::epoll_ctl(epfd_.get(), EPOLL_CTL_ADD, f, &ev) != 0) {
            return std::unexpected(os_error(ERR_SITE()));
        }
    }
    stop_fd_ = fds.stop_fd();
    pause_wake_ = infra::OptRef<xthread::WakeFlag>{fds.wake()};
    return {};
}

unsigned InputDecode::drop_link_events() noexcept {
    unsigned n = 0;
    while (link_rx_.pop().has_value())
        ++n;
    return n;
}

namespace {

std::uint32_t device_key(const svc::InputDevice& d) noexcept {
    const svc::DeviceIdentity& id = d.identity();
    std::uint32_t h = 2166136261u;
    auto mix = [&h](std::uint8_t b) noexcept {
        h ^= b;
        h *= 16777619u;
    };
    const std::string& s = !id.merge_id.empty() ? id.merge_id : id.id;
    for (const char c : s)
        mix(static_cast<std::uint8_t>(c));
    mix(static_cast<std::uint8_t>(id.vid.v & 0xFFu));
    mix(static_cast<std::uint8_t>(id.vid.v >> 8));
    mix(static_cast<std::uint8_t>(id.pid.v & 0xFFu));
    mix(static_cast<std::uint8_t>(id.pid.v >> 8));
    return h != 0u ? h : 1u;
}
}

void InputDecode::rebuild_registrations(Reseat bridge) {

    registered_.store(0, std::memory_order_relaxed);
    for (svc::InputDevice& d : svc_.devices()) {
        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = d.fd();
        (void)::epoll_ctl(epfd_.get(), EPOLL_CTL_DEL, d.fd(), &ev);
        if (::epoll_ctl(epfd_.get(), EPOLL_CTL_ADD, d.fd(), &ev) == 0) {
            InputWire::bump(registered_, 1);
        }
    }
    const int watch = svc_.hotplug().fd();
    if (watch >= 0) {
        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = watch;
        (void)::epoll_ctl(epfd_.get(), EPOLL_CTL_DEL, watch, &ev);
        if (::epoll_ctl(epfd_.get(), EPOLL_CTL_ADD, watch, &ev) == 0) {
            InputWire::bump(registered_, 1);
        }
    }

    {
        std::size_t slot = 0;
        for (svc::InputDevice& d : svc_.devices()) {
            if (slot >= svc::kMaxDevices)
                break;
            const std::uint32_t key = device_key(d);
            if (joy_key_[slot] != key) {
                joy_key_[slot] = key;
                joy_prev_[slot] = d.report().menu_buttons.v;
                if (bridge == Reseat::Bridge && launcher_keys_)
                    launcher_keys_->reseat_pad(slot, joy_prev_[slot]);
                for (std::uint64_t& w : ui_key_down_[slot])
                    w = 0;
            }
            ++slot;
        }

        for (; slot < svc::kMaxDevices; ++slot) {
            joy_key_[slot] = 0;
            joy_prev_[slot] = 0;
            if (bridge == Reseat::Bridge && launcher_keys_)
                launcher_keys_->reseat_pad(slot, 0);
            for (std::uint64_t& w : ui_key_down_[slot])
                w = 0;
        }
    }
    seen_gen_ = svc_.devices_generation();
}

unsigned InputDecode::take_tables_() noexcept {
    unsigned n = 0;
    while (const auto t = tables_rx_.next()) {
        deregister_devices_();
        svc_.install(*t);
        tables_rx_.taken();
        ++n;
    }
    return n;
}

void InputDecode::deregister_devices_() noexcept {
    for (const svc::InputDevice& d : svc_.devices()) {
        (void)::epoll_ctl(epfd_.get(), EPOLL_CTL_DEL, d.fd(), nullptr);
    }
}

bool InputDecode::apply_requests() {
    bool rebuilt = false;
    const std::uint32_t req = wire_.requests();
    if ((req & InputWire::kReqGrabApply) != 0u) {

        const bool on = (req & InputWire::kReqGrabOn) != 0u;
        (void)svc_.set_grabbed(on);
        wire_.publish_gates(on ? InputWire::kGateGrabbed : 0u, on ? 0u : InputWire::kGateGrabbed);
        wire_.clear_request(InputWire::kReqGrabApply);
    }

    const auto osd_on = wire_.take_osd_request();
    if (osd_on) {
        wire_.publish_gates(
            *osd_on ? InputWire::kGateOsdVisible : 0u,
            *osd_on ? 0u : InputWire::kGateOsdVisible
        );
    }

    if (take_tables_() != 0u) {
        rebuild_registrations(Reseat::Bridge);
        rebuilt = true;
    }

    if (svc_.devices_generation() != seen_gen_) {
        rebuild_registrations(Reseat::Bridge);
        rebuilt = true;
    }
    return rebuilt;
}

void InputDecode::evict_fd(int fd) {
    if (fd < 0)
        return;
    (void)::epoll_ctl(epfd_.get(), EPOLL_CTL_DEL, fd, nullptr);

    if (const std::uint32_t r = registered_.load(std::memory_order_relaxed); r != 0) {
        registered_.store(r - 1, std::memory_order_relaxed);
    }
    tally_.eviction();
    if (rearm_credit_ != 0) {
        --rearm_credit_;
        wire_.request(InputWire::kReqHotplug);
    }
}

unsigned InputDecode::decode_round(int timeout_ms) {
    before_wait();
    wait_events(timeout_ms);
    return after_wait();
}

void InputDecode::before_wait() {
    tally_.round();
    drain_link_events_();
    rebuilt_ = apply_requests();
    rumble_.refresh();
}

void InputDecode::wait_events(int timeout_ms) noexcept {
    n_ready_ = ::epoll_wait(epfd_.get(), evs_, static_cast<int>(kMaxEpollEvents), timeout_ms);
}

unsigned InputDecode::after_wait() {
    const int n = n_ready_;
    const bool rebuilt = rebuilt_;
    n_ready_ = 0;
    rebuilt_ = false;
    if (n < 0) {
        counts_cell_.publish(counts());
        return 0;
    }

    if (const auto a = arm_reader_.take_if_changed(wire_.capture_arm()))
        arm_ = *a;
    bridging_ = false;
    if (launcher_keys_) {
        const bool owns = launcher_cell_->sample().value.owns_screen
            && !muted_
            && (wire_.gates() & InputWire::kGateOsdVisible) == 0u;
        bridging_ = launcher_keys_->set_active(owns);
    }

    drain_link_events_();
    unsigned decoded = 0;
    bool published = false;
    for (int i = 0; i < n; ++i) {
        const int fd = evs_[i].data.fd;
        const std::uint32_t events = evs_[i].events;
        if (fd == wire_.ctrl_fd()) {
            wire_.drain_ctrl();
            continue;
        }

        if (fd == stop_fd_)
            continue;
        if (pause_wake_ && fd == pause_wake_->fd()) {
            pause_wake_->drain();
            continue;
        }

        if ((events & (EPOLLHUP | EPOLLERR)) != 0u) {
            evict_fd(fd);
            continue;
        }
        auto got = svc_.on_fd_ready(fd);
        if (!got) {

            evict_fd(fd);
            continue;
        }
        decoded += *got;
        if (fd == svc_.hotplug().fd()) {

            if (svc_.hotplug().take_pending()) {
                rearm_credit_ = kEvictRearmCredit;
                wire_.request(InputWire::kReqHotplug);
            }
            continue;
        }
        std::size_t slot = 0;
        for (svc::InputDevice& d : svc_.devices()) {
            if (d.fd() != fd) {
                ++slot;
                continue;
            }
            maybe_capture(d, slot < svc::kMaxDevices ? slot : 0u);
            publish_from(d.report(), d.is_mouse(), slot < svc::kMaxDevices ? slot : 0u);
            published = true;
            break;
        }
    }

    if (published || rebuilt || joy_.owes_publish()) {
        joy_.publish(muted_);
        tally_.events(decoded);
    }

    flush_mouse_();
    unsigned framed = flush_ps2_frames_();
    sweep_dropped_keys_();
    framed += flush_ps2_frames_();
    if (!muted_ && (published || rebuilt || framed != 0 || joy_.owes_publish())) {

        if (wire_.arm_kick()) {
            rt_wake_.kick();
            tally_.kick();
        }
    }
    counts_cell_.publish(counts());
    return decoded;
}

void InputDecode::drain_link_events_() {
    while (const auto ev = link_rx_.pop()) {
        infra::dispatch<LinkEventRoutes<EventSink::Input>>(*ev, *this);
    }
}

void InputDecode::on(const proto::LinkEvent::Ps2Control& c) {
    (void)kbd_.accept_control(c.keyboard);
    (void)mouse_.accept_control(c.mouse);
}

void InputDecode::on(const proto::LinkEvent::Ps2ControlEnded&) noexcept {
    end_ps2_control();
}

void InputDecode::on(const proto::LinkEvent::PolledWord& w) noexcept {
    switch (w.shape) {
        case proto::user_io::PollShape::RumblePoll:
            rumble_.on(w);
            return;
        case proto::user_io::PollShape::F12ModPoll:
            f12_mod_needed_ = w.word != 0;
            return;
        case proto::user_io::PollShape::InfoPoll:
        case proto::user_io::PollShape::kCount:
            break;
    }
    ++link_event_misrouted_;
}

bool InputDecode::is_osd_toggle_(std::uint16_t code) const noexcept {
    if (code == kKeyMenu)
        return true;
    if (code != kKeyF12)
        return false;
    if (!f12_mod_needed_)
        return true;
    for (const auto& down : ui_key_down_) {
        if (ui_down_test(down, kKeyLeftMeta) || ui_down_test(down, kKeyRightMeta))
            return true;
    }
    return false;
}

void InputDecode::end_ps2_control() noexcept {
    kbd_.end_control();
    mouse_.end_control();
}

void InputDecode::misrouted(const proto::LinkEvent&) noexcept {
    TASTY_SEAT_BODY(InputDecode);
    ++link_event_misrouted_;
}

static_assert(LinkEventInputSink<InputDecode>);

void InputDecode::flush_mouse_() {
    const auto p = mouse_divider_.flush();
    if (!p)
        return;

    if ((wire_.gates() & InputWire::kGateOsdVisible) == 0u) {
        if (mouse_.motion(p->dx, p->dy, p->dz, p->buttons)) {
            tally_.mouse_packet();
            wire_.note_activity();
        }
    }
}

unsigned InputDecode::flush_ps2_frames_() {
    unsigned pushed = 0;
    proto::Ps2Frame frame{};
    if (muted_) {

        while (kbd_.next_frame(frame)) {
        }
        while (mouse_.next_frame(frame)) {
        }
        return pushed;
    }

    while (inbox_.ring().size() < proto::kLinkTxCapacity && kbd_.next_frame(frame)) {
        if (!inbox_.push(proto::LinkOp::Ps2Frame{frame})) {
            wire_.note_key_drop(1u);
            return pushed;
        }
        ++pushed;
        wire_.note_activity();
    }
    while (inbox_.ring().size() < proto::kLinkTxCapacity && mouse_.next_frame(frame)) {
        if (!inbox_.push(proto::LinkOp::Ps2Frame{frame}))
            return pushed;
        ++pushed;
    }
    return pushed;
}

void InputDecode::sweep_dropped_keys_() {
    if (const std::uint32_t drops = kbd_.frames_dropped_overflow(); drops != wm_frame_drops_) {
        wire_.note_key_drop(drops - wm_frame_drops_);
        wm_frame_drops_ = drops;
        sweep_owed_ = true;
        diag_.appendf(
            "{\"t\":\"input_drop\",\"ring\":\"keys\",\"cap\":%u,"
            "\"dropped\":%u}",
            static_cast<unsigned>(proto::Ps2Keyboard::capacity()),
            static_cast<unsigned>(wire_.keys_dropped())
        );
    }

    if (!sweep_owed_ || kbd_.queued() >= proto::Ps2Keyboard::capacity())
        return;
    sweep_owed_ = false;
    const unsigned released = kbd_.release_all();

    wm_frame_drops_ = kbd_.frames_dropped_overflow();
    wire_.note_key_sweep(released);
}

bool InputDecode::push_ui_edge(const RawKeyEdge& e) {
    if (muted_)
        return true;
    if (wire_.push_ui_key(e)) {

        if (ui_wake_)
            ui_wake_->kick_if_armed();
        return true;
    }
    diag_.appendf(
        "{\"t\":\"input_drop\",\"ring\":\"ui_keys\",\"cap\":%u,"
        "\"dropped\":%u,\"code\":%u,\"pressed\":%u}",
        static_cast<unsigned>(InputWire::kUiKeyRingSlots),
        static_cast<unsigned>(wire_.ui_keys_dropped()),
        static_cast<unsigned>(e.code),
        static_cast<unsigned>(e.pressed)
    );
    return false;
}

void InputDecode::publish_from(const svc::DeviceReport& r, bool is_mouse, std::size_t dev_slot) {

    const bool osd_visible = (wire_.gates() & InputWire::kGateOsdVisible) != 0u;
    const bool has_slot = dev_slot < svc::kMaxDevices;
    for (std::uint8_t i = 0; i < r.key_count; ++i) {
        const svc::KeyEdge& e = r.keys[i];
        if (e.code >= 256)
            continue;

        if (e.pressed != 0 && !svc_.grabbed())
            continue;

        const bool ui_repeat = has_slot
            && e.pressed != 0
            && ui_down_test(ui_key_down_[dev_slot], e.code);
        if (has_slot)
            ui_down_set(ui_key_down_[dev_slot], e.code, e.pressed != 0);

        const RawKeyEdge ke{e.code, e.pressed ? std::uint8_t{1} : std::uint8_t{0}, 0};
        if (is_osd_toggle_(e.code)) {

            const RawKeyEdge tog{kKeyF12, ke.pressed, ke.reserved};
            if (!ui_repeat && !push_ui_edge(tog))
                break;
            continue;
        }
        if (osd_visible) {
            if (!ui_repeat && !push_ui_edge(ke))
                break;
            if (!is_pass_through_modifier(e.code))
                continue;
        }
        if (bridging_) {
            launcher_keys_->key(ke.code, ke.pressed != 0);
            continue;
        }

        if (kbd_.key_event(proto::HidUsage{static_cast<std::uint8_t>(ke.code)}, ke.pressed != 0)) {
            tally_.key_published();
            tally_.key_emitted();
        }
    }

    if (arm_.armed == 0) {
        for (std::uint8_t i = 0; i < r.osd_makes; ++i) {
            if (!push_ui_edge(RawKeyEdge{kKeyF12, 1, 0}))
                break;
            ++joy_ui_keys_;
        }
        for (std::uint8_t i = 0; i < r.osd_breaks; ++i) {
            if (!push_ui_edge(RawKeyEdge{kKeyF12, 0, 0}))
                break;
            ++joy_ui_keys_;
        }
    }

    if (!is_mouse)
        publish_joy_menu(r, dev_slot, osd_visible);

    if (is_mouse) {

        if (svc_.grabbed())
            mouse_divider_.accumulate(r.rel_x, r.rel_y, r.rel_wheel);

        mouse_divider_.note_buttons(svc_.mouse_button_level());
    }
}

void InputDecode::maybe_capture(const svc::InputDevice& d, std::size_t dev_slot) {
    if (arm_.armed == 0 || arm_.token == 0 || arm_.token == capture_served_)
        return;
    const svc::DeviceReport& r = d.report();
    for (std::uint8_t i = 0; i < r.key_count; ++i) {
        const svc::KeyEdge& e = r.keys[i];
        if (!e.pressed || e.code < 256)
            continue;
        const svc::DeviceIdentity& id = d.identity();
        CaptureHit hit{};
        hit.token = arm_.token;
        hit.unique_hash = id.unique_hash;
        hit.code = e.code;
        hit.device_ordinal = static_cast<std::uint8_t>(dev_slot);
        hit.mod = d.mod();
        hit.unique_filenames = id.unique_filenames;
        hit.vid = id.vid;
        hit.pid = id.pid;
        (void)hit.id.assign(id.id);
        hit.id_clipped = hit.id.clipped();
        wire_.capture_hit().publish(hit);
        capture_served_ = arm_.token;
        tally_.capture();
        return;
    }
}

namespace {

constexpr std::uint16_t kKeyRight = 106;
constexpr std::uint16_t kKeyLeft = 105;
constexpr std::uint16_t kKeyUp = 103;
constexpr std::uint16_t kKeyDown = 108;
constexpr std::uint16_t kKeyEnter = 28;
constexpr std::uint16_t kKeyBackspace = 14;
constexpr std::uint16_t kKeyTab = 15;
constexpr std::uint16_t kKeyMinus = 12;
constexpr std::uint16_t kKeyEqual = 13;
constexpr std::uint16_t kKeyGrave = 41;
constexpr std::uint16_t kKeyBack = 158;

std::uint16_t joy_menu_key(std::uint32_t mask) noexcept {
    switch (mask) {
        case proto::kJoyRight:
            return kKeyRight;
        case proto::kJoyLeft:
            return kKeyLeft;
        case proto::kJoyUp:
            return kKeyUp;
        case proto::kJoyDown:
            return kKeyDown;
        case proto::kJoyBtn1:
            return kKeyEnter;
        case proto::kJoyBtn2:
            return kKeyBack;
        case proto::kJoyBtn3:
            return kKeyBackspace;
        case proto::kJoyBtn4:
            return kKeyTab;
        case proto::kJoyL:
            return kKeyMinus;
        case proto::kJoyR:
            return kKeyEqual;
        case proto::kJoyR2:
            return kKeyGrave;
        default:
            return 0;
    }
}

}

void InputDecode::publish_joy_menu(
    const svc::DeviceReport& r,
    std::size_t dev_slot,
    bool osd_visible
) {
    if (dev_slot >= svc::kMaxDevices)
        return;
    const std::uint32_t now = r.menu_buttons.v;
    const std::uint32_t prev = joy_prev_[dev_slot];
    joy_prev_[dev_slot] = now;
    if (launcher_keys_)
        launcher_keys_->pad(dev_slot, now);

    if (arm_.armed != 0) {
        osdbtn_ = 0;
        return;
    }

    if (bridging_) {
        osdbtn_ = 0;
        return;
    }

    if (!osd_visible) {
        osdbtn_ = 0;
        return;
    }

    std::uint32_t diff = now ^ prev;
    while (diff != 0u) {
        const std::uint32_t bit = diff & (~diff + 1u);
        diff &= ~bit;
        const bool press = (now & bit) != 0u;

        std::uint32_t mask = bit & ~proto::kJoyBtn3;
        if (press) {
            osdbtn_ |= mask;
            if ((mask & (proto::kJoyBtn1 | proto::kJoyBtn2)) != 0u) {
                if ((osdbtn_ & (proto::kJoyBtn1 | proto::kJoyBtn2))
                    == (proto::kJoyBtn1 | proto::kJoyBtn2)) {
                    osdbtn_ |= proto::kJoyBtn3;
                    mask = proto::kJoyBtn3;
                }
            }
        } else {
            const std::uint32_t old = osdbtn_;
            osdbtn_ &= ~mask;
            if ((mask & (proto::kJoyBtn1 | proto::kJoyBtn2)) != 0u) {
                if ((old & (proto::kJoyBtn1 | proto::kJoyBtn2 | proto::kJoyBtn3))
                    == (proto::kJoyBtn1 | proto::kJoyBtn2 | proto::kJoyBtn3)) {
                    mask = proto::kJoyBtn3;
                } else if ((old & proto::kJoyBtn3) != 0u) {
                    if ((osdbtn_ & (proto::kJoyBtn1 | proto::kJoyBtn2)) == 0u) {
                        osdbtn_ &= ~proto::kJoyBtn3;
                    }
                    mask = 0;
                }
            }
            if ((mask & proto::kJoyBtn2) != 0u && (old & proto::kJoyBtn2) == 0u) {
                mask = 0;
            }
        }

        const std::uint16_t code = joy_menu_key(mask);
        if (code == 0)
            continue;
        const RawKeyEdge ke{code, press ? std::uint8_t{1} : std::uint8_t{0}, 0};

        if (!push_ui_edge(ke))
            break;
        ++joy_ui_keys_;
    }
}

InputDecode::Counts InputDecode::counts() const noexcept {
    Counts c{};
    c.rounds = tally_.rounds();
    c.events = tally_.events();
    c.keys_published = tally_.keys_published();
    c.keys_emitted = tally_.keys_emitted();
    c.mouse_packets = tally_.mouse_packets();
    c.kicks = tally_.kicks();
    c.fd_evictions = tally_.evictions();
    c.registered = registered_.load(std::memory_order_relaxed);
    c.joy_seen = tally_.joy_seen();
    c.joy_players = tally_.joy_players();
    c.captures = tally_.captures();
    c.mapped = svc_.counters().mapped_devices();
    c.slotted = svc_.counters().slotted_devices();
    c.mask_changes = svc_.counters().mask_recomputes();
    c.axis_edges = svc_.counters().axis_edges();
    c.mouse_remainder = svc_.counters().mouse_remainder_bytes();
    c.key_overflows = svc_.counters().key_overflows();
    c.quirk_drops = svc_.counters().quirk_drops();
    c.slot_live = svc_.counters().live_slot_mask();
    c.slot_ghosts = svc_.counters().ghost_slots();
    for (std::uint8_t i = 0; i < svc::kMaxPlayers; ++i) {
        c.slot_hash[i] = svc_.counters().slot_key_hash(i);
    }
    return c;
}

}
