// SPDX-License-Identifier: GPL-3.0-or-later
#include "infra/seat.h"
#include "app/input_decode.h"

#include <sys/epoll.h>

#include <cerrno>

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

constexpr std::int64_t kMouseDividerNs = 15'000'000;

constexpr std::uint16_t kKeyF12 = 88;
constexpr std::uint16_t kKeyMenu = 139;
bool is_osd_toggle(std::uint16_t code) noexcept { return code == kKeyF12 || code == kKeyMenu; }

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

}  // namespace

Ex<void> InputDecode::open() {
    epfd_.reset(::epoll_create1(EPOLL_CLOEXEC));
    if (!epfd_.valid()) return std::unexpected(os_error(ERR_SITE()));

    const int ctrl = wire_.ctrl_fd();
    if (ctrl < 0) return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    {
        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = ctrl;
        if (::epoll_ctl(epfd_.get(), EPOLL_CTL_ADD, ctrl, &ev) != 0) {
            return std::unexpected(os_error(ERR_SITE()));
        }
    }
    return {};
}

Ex<void> InputDecode::watch_stop(const xthread::WakeFlag& stop) noexcept {
    if (!epfd_.valid() || stop.fd() < 0)
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = stop.fd();
    if (::epoll_ctl(epfd_.get(), EPOLL_CTL_ADD, stop.fd(), &ev) != 0) {
        return std::unexpected(os_error(ERR_SITE()));
    }
    stop_ = &stop;
    return {};
}

Ex<void> InputDecode::watch_wake(xthread::WakeFlag& wake) noexcept {
    if (!epfd_.valid() || wake.fd() < 0)
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = wake.fd();
    if (::epoll_ctl(epfd_.get(), EPOLL_CTL_ADD, wake.fd(), &ev) != 0) {
        return std::unexpected(os_error(ERR_SITE()));
    }
    pause_wake_ = &wake;
    return {};
}

unsigned InputDecode::drop_link_events() noexcept {
    unsigned n = 0;
    if (link_rx_ == nullptr) return n;
    while (link_rx_->pop().has_value())
        ++n;
    return n;
}

Ex<void> InputDecode::arm(svc::InputService& svc) {
    svc_ = &svc;
    return rebuild_registrations();
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
}  // namespace

Ex<void> InputDecode::rebuild_registrations() {
    if (!epfd_.valid()) return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});

    registered_.store(0, std::memory_order_relaxed);
    for (svc::InputDevice& d : svc_->devices()) {
        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = d.fd();
        (void)::epoll_ctl(epfd_.get(), EPOLL_CTL_DEL, d.fd(), &ev);
        if (::epoll_ctl(epfd_.get(), EPOLL_CTL_ADD, d.fd(), &ev) == 0) {
            InputWire::bump(registered_, 1);
        }
    }
    const int watch = svc_->hotplug_fd();
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
        for (svc::InputDevice& d : svc_->devices()) {
            if (slot >= svc::kMaxDevices) break;
            const std::uint32_t key = device_key(d);
            if (joy_key_[slot] != key) {
                joy_key_[slot] = key;
                joy_prev_[slot] = d.report().menu_buttons.v;
                if (launcher_keys_ != nullptr) launcher_keys_->reseat_pad(slot, joy_prev_[slot]);
                for (std::uint64_t& w : ui_key_down_[slot])
                    w = 0;
            }
            ++slot;
        }

        for (; slot < svc::kMaxDevices; ++slot) {
            joy_key_[slot] = 0;
            joy_prev_[slot] = 0;
            if (launcher_keys_ != nullptr) launcher_keys_->reseat_pad(slot, 0);
            for (std::uint64_t& w : ui_key_down_[slot])
                w = 0;
        }
    }
    seen_gen_ = svc_->devices_generation();
    return {};
}

bool InputDecode::apply_requests() {
    bool rebuilt = false;
    const std::uint32_t req = wire_.requests();
    if ((req & InputWire::kReqGrabApply) != 0u) {

        const bool on = (req & InputWire::kReqGrabOn) != 0u;
        (void)svc_->set_grabbed(on);
        wire_.publish_gates(on ? InputWire::kGateGrabbed : 0u, on ? 0u : InputWire::kGateGrabbed);
        wire_.clear_request(InputWire::kReqGrabApply);
    }

    bool osd_on = false;
    if (wire_.take_osd_request(osd_on)) {
        wire_.publish_gates(osd_on ? InputWire::kGateOsdVisible : 0u,
                            osd_on ? 0u : InputWire::kGateOsdVisible);
    }
    if ((req & InputWire::kReqRebuild) != 0u) {

        wire_.begin_pause();
        while ((wire_.requests() & InputWire::kReqRebuild) != 0u &&
               !(stop_ != nullptr && stop_->ever_requested())) {

            timespec ts{0, 200'000};
            (void)::nanosleep(&ts, nullptr);
        }
        wire_.end_pause();
        (void)rebuild_registrations();
        rebuilt = true;
    }

    if (svc_->devices_generation() != seen_gen_) {
        (void)rebuild_registrations();
        rebuilt = true;
    }
    return rebuilt;
}

void InputDecode::evict_fd(int fd) {
    if (fd < 0 || !epfd_.valid()) return;
    (void)::epoll_ctl(epfd_.get(), EPOLL_CTL_DEL, fd, nullptr);

    if (const std::uint32_t r = registered_.load(std::memory_order_relaxed); r != 0) {
        registered_.store(r - 1, std::memory_order_relaxed);
    }
    InputWire::bump(n_evicted_, 1);
    if (rearm_credit_ != 0) {
        --rearm_credit_;
        wire_.request(InputWire::kReqHotplug);
    }
}

unsigned InputDecode::decode_round(int timeout_ms) {

    if (!ready()) return 0;
    before_wait();
    wait_events(timeout_ms);
    return after_wait();
}

void InputDecode::before_wait() {
    if (!ready()) return;
    InputWire::bump(n_rounds_, 1);
    drain_link_events_();
    rebuilt_ = apply_requests();
}

void InputDecode::wait_events(int timeout_ms) noexcept {
    n_ready_ = ready()
                   ? ::epoll_wait(epfd_.get(), evs_, static_cast<int>(kMaxEpollEvents), timeout_ms)
                   : -1;
}

unsigned InputDecode::after_wait() {
    const int n = n_ready_;
    const bool rebuilt = rebuilt_;
    n_ready_ = 0;
    rebuilt_ = false;
    if (!ready() || n < 0) return 0;

    if (const auto a = arm_reader_.take_if_changed(wire_.capture_arm())) arm_ = *a;
    bridging_ = false;
    if (launcher_keys_ != nullptr) {
        const bool owns = launcher_cell_->sample().value.owns_screen && !muted_ &&
                          (wire_.gates() & InputWire::kGateOsdVisible) == 0u;
        bridging_ = launcher_keys_->set_active(owns);
    }

    unsigned decoded = 0;
    bool published = false;
    for (int i = 0; i < n; ++i) {
        const int fd = evs_[i].data.fd;
        const std::uint32_t events = evs_[i].events;
        if (fd == wire_.ctrl_fd()) {
            wire_.drain_ctrl();
            continue;
        }

        if (stop_ != nullptr && fd == stop_->fd()) continue;
        if (pause_wake_ != nullptr && fd == pause_wake_->fd()) {
            pause_wake_->drain();
            continue;
        }

        if ((events & (EPOLLHUP | EPOLLERR)) != 0u) {
            evict_fd(fd);
            continue;
        }
        auto got = svc_->on_fd_ready(fd);
        if (!got) {

            evict_fd(fd);
            continue;
        }
        decoded += *got;
        if (fd == svc_->hotplug_fd()) {

            if (svc_->take_hotplug_pending()) {
                rearm_credit_ = kEvictRearmCredit;
                wire_.request(InputWire::kReqHotplug);
            }
            continue;
        }
        std::size_t slot = 0;
        for (svc::InputDevice& d : svc_->devices()) {
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

    if (published || rebuilt || wire_.edge_reset_seq() != wm_edge_reset_ ||
        wire_.gates() != wm_gates_) {
        publish_joy_levels();
        InputWire::bump(n_events_, decoded);
    }

    flush_mouse_();
    unsigned framed = flush_ps2_frames_();
    sweep_dropped_keys_();
    framed += flush_ps2_frames_();
    if (!muted_ && (published || rebuilt || framed != 0 ||
                    wire_.edge_reset_seq() != wm_edge_reset_ || wire_.gates() != wm_gates_)) {

        if (wire_.kick_rt()) InputWire::bump(n_kicks_, 1);
    }
    return decoded;
}

void InputDecode::drain_link_events_() {
    if (link_rx_ == nullptr) return;
    while (const auto ev = link_rx_->pop()) {
        infra::dispatch<LinkEventRoutes<EventSink::Input>>(*ev, *this);
    }
}

void InputDecode::on(const proto::LinkEvent::Ps2Control& c) {
    (void)kbd_.accept_control(c.keyboard);
    (void)mouse_.accept_control(c.mouse);
}

void InputDecode::on(const proto::LinkEvent::Ps2ControlEnded&) noexcept { end_ps2_control(); }

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
    const InputWire::MouseDelta md = wire_.take_mouse();
    carry_x_ += md.dx;
    carry_y_ += md.dy;
    carry_z_ += md.dz;
    if (md.moved || md.button_edge) mouse_pending_ = true;
    if (!mouse_pending_) return;
    const std::int64_t now = clock_->now().count();
    const bool due = (now - last_mouse_ns_) > kMouseDividerNs;
    if (!due && !md.button_edge) return;

    if ((wire_.gates() & InputWire::kGateOsdVisible) == 0u) {
        if (mouse_.motion(carry_x_, carry_y_, carry_z_, md.buttons)) {
            InputWire::bump(n_mouse_pkt_, 1);
            wire_.note_activity();
        }
    }
    carry_x_ = 0;
    carry_y_ = 0;
    carry_z_ = 0;
    last_mouse_ns_ = now;
    mouse_pending_ = false;
}

unsigned InputDecode::flush_ps2_frames_() {
    if (inbox_ == nullptr) return 0;
    unsigned pushed = 0;
    proto::Ps2Frame frame{};
    if (muted_) {

        while (kbd_.next_frame(frame)) {
        }
        while (mouse_.next_frame(frame)) {
        }
        return pushed;
    }

    while (inbox_->ring().size() < proto::kLinkTxCapacity && kbd_.next_frame(frame)) {
        if (!inbox_->push(proto::LinkOp::Ps2Frame{frame})) {
            wire_.note_key_drop(1u);
            return pushed;
        }
        ++pushed;
        wire_.note_activity();
    }
    while (inbox_->ring().size() < proto::kLinkTxCapacity && mouse_.next_frame(frame)) {
        if (!inbox_->push(proto::LinkOp::Ps2Frame{frame})) return pushed;
        ++pushed;
    }
    return pushed;
}

void InputDecode::sweep_dropped_keys_() {
    if (const std::uint32_t drops = kbd_.frames_dropped_overflow(); drops != wm_frame_drops_) {
        wire_.note_key_drop(drops - wm_frame_drops_);
        wm_frame_drops_ = drops;
        sweep_owed_ = true;
        if (diag_ != nullptr) {
            diag_->appendf("{\"t\":\"input_drop\",\"ring\":\"keys\",\"cap\":%u,"
                           "\"dropped\":%u}",
                           static_cast<unsigned>(proto::Ps2Keyboard::capacity()),
                           static_cast<unsigned>(wire_.keys_dropped()));
        }
    }

    if (!sweep_owed_ || kbd_.queued() >= proto::Ps2Keyboard::capacity()) return;
    sweep_owed_ = false;
    const unsigned released = kbd_.release_all();

    wm_frame_drops_ = kbd_.frames_dropped_overflow();
    wire_.note_key_sweep(released);
}

bool InputDecode::push_ui_edge(const RawKeyEdge& e) {
    if (muted_) return true;
    if (wire_.push_ui_key(e)) return true;
    if (diag_ != nullptr) {
        diag_->appendf("{\"t\":\"input_drop\",\"ring\":\"ui_keys\",\"cap\":%u,"
                       "\"dropped\":%u,\"code\":%u,\"pressed\":%u}",
                       static_cast<unsigned>(InputWire::kUiKeyRingSlots),
                       static_cast<unsigned>(wire_.ui_keys_dropped()),
                       static_cast<unsigned>(e.code), static_cast<unsigned>(e.pressed));
    }
    return false;
}

void InputDecode::publish_from(const svc::DeviceReport& r, bool is_mouse, std::size_t dev_slot) {

    const bool osd_visible = (wire_.gates() & InputWire::kGateOsdVisible) != 0u;
    const bool has_slot = dev_slot < svc::kMaxDevices;
    for (std::uint8_t i = 0; i < r.key_count; ++i) {
        const svc::KeyEdge& e = r.keys[i];
        if (e.code >= 256) continue;

        if (e.pressed != 0 && !svc_->grabbed()) continue;

        const bool ui_repeat =
            has_slot && e.pressed != 0 && ui_down_test(ui_key_down_[dev_slot], e.code);
        if (has_slot) ui_down_set(ui_key_down_[dev_slot], e.code, e.pressed != 0);

        const RawKeyEdge ke{e.code, e.pressed ? std::uint8_t{1} : std::uint8_t{0}, 0};
        if (is_osd_toggle(e.code)) {

            const RawKeyEdge tog{kKeyF12, ke.pressed, ke.reserved};
            if (!ui_repeat && !push_ui_edge(tog)) break;
            continue;
        }
        if (osd_visible) {
            if (!ui_repeat && !push_ui_edge(ke)) break;
            if (!is_pass_through_modifier(e.code)) continue;
        }
        if (bridging_) {
            launcher_keys_->key(ke.code, ke.pressed != 0);
            continue;
        }

        if (kbd_.key_event(proto::HidUsage{static_cast<std::uint8_t>(ke.code)}, ke.pressed != 0)) {
            InputWire::bump(n_keys_pub_, 1);
            InputWire::bump(n_keys_emit_, 1);
        }
    }

    if (arm_.armed == 0) {
        for (std::uint8_t i = 0; i < r.osd_makes; ++i) {
            if (!push_ui_edge(RawKeyEdge{kKeyF12, 1, 0})) break;
            ++joy_ui_keys_;
        }
        for (std::uint8_t i = 0; i < r.osd_breaks; ++i) {
            if (!push_ui_edge(RawKeyEdge{kKeyF12, 0, 0})) break;
            ++joy_ui_keys_;
        }
    }

    if (!is_mouse) publish_joy_menu(r, dev_slot, osd_visible);

    if (is_mouse) {

        if (svc_->grabbed()) {
            m_x_ += static_cast<std::uint32_t>(r.rel_x);
            m_y_ += static_cast<std::uint32_t>(r.rel_y);
            m_w_ += static_cast<std::uint32_t>(r.rel_wheel);
        }

        const std::uint8_t level = svc_->mouse_button_level();
        if (level != m_btn_) {
            m_btn_ = level;
            ++m_edges_;
        }
        wire_.publish_mouse(static_cast<std::int32_t>(m_x_), static_cast<std::int32_t>(m_y_),
                            static_cast<std::int32_t>(m_w_), m_btn_, m_edges_);
    }
}

void InputDecode::maybe_capture(const svc::InputDevice& d, std::size_t dev_slot) {
    if (arm_.armed == 0 || arm_.token == 0 || arm_.token == capture_served_) return;
    const svc::DeviceReport& r = d.report();
    for (std::uint8_t i = 0; i < r.key_count; ++i) {
        const svc::KeyEdge& e = r.keys[i];
        if (!e.pressed || e.code < 256) continue;
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
        InputWire::bump(n_captures_, 1);
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

}  // namespace

void InputDecode::publish_joy_menu(const svc::DeviceReport& r, std::size_t dev_slot,
                                   bool osd_visible) {
    if (dev_slot >= svc::kMaxDevices) return;
    const std::uint32_t now = r.menu_buttons.v;
    const std::uint32_t prev = joy_prev_[dev_slot];
    joy_prev_[dev_slot] = now;
    if (launcher_keys_ != nullptr) launcher_keys_->pad(dev_slot, now);

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
                if ((osdbtn_ & (proto::kJoyBtn1 | proto::kJoyBtn2)) ==
                    (proto::kJoyBtn1 | proto::kJoyBtn2)) {
                    osdbtn_ |= proto::kJoyBtn3;
                    mask = proto::kJoyBtn3;
                }
            }
        } else {
            const std::uint32_t old = osdbtn_;
            osdbtn_ &= ~mask;
            if ((mask & (proto::kJoyBtn1 | proto::kJoyBtn2)) != 0u) {
                if ((old & (proto::kJoyBtn1 | proto::kJoyBtn2 | proto::kJoyBtn3)) ==
                    (proto::kJoyBtn1 | proto::kJoyBtn2 | proto::kJoyBtn3)) {
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
        if (code == 0) continue;
        const RawKeyEdge ke{code, press ? std::uint8_t{1} : std::uint8_t{0}, 0};

        if (!push_ui_edge(ke)) break;
        ++joy_ui_keys_;
    }
}

void InputDecode::publish_joy_levels() {

    std::uint32_t seen = n_joy_seen_.load(std::memory_order_relaxed);
    std::uint32_t players = n_joy_players_.load(std::memory_order_relaxed);

    if (const std::uint32_t rs = wire_.edge_reset_seq(); rs != wm_edge_reset_) {
        wm_edge_reset_ = rs;
        for (std::uint32_t& v : last_joy_posted_)
            v = 0u;
    }
    const std::uint32_t gates = wire_.gates();
    wm_gates_ = gates;
    const bool grabbed = (gates & InputWire::kGateGrabbed) != 0u;
    const bool osd_visible = (gates & InputWire::kGateOsdVisible) != 0u;
    std::uint32_t autofire[svc::kMaxPlayers]{};

    for (std::uint8_t i = 0; i < svc::kMaxPlayers; ++i) {
        const svc::PlayerIndex p{i};
        const svc::JoyMask mask = svc_->joy_mask(p);
        if (mask.v != 0u) {
            seen |= mask.v;
            players |= (1u << i);
        }
        autofire[i] = wire_.joy(p).autofire.v;

        wire_.publish_joy(p, mask, wire_.joy(p).autofire);
        if (mask.v != armed_mask_[i] || autofire[i] != armed_autofire_[i]) {
            armed_mask_[i] = mask.v;
            armed_autofire_[i] = autofire[i];
            zero_armed_[i] = true;
        }
        if (inbox_ != nullptr && !muted_ && grabbed && mask.v != last_joy_posted_[i]) {

            if (inbox_->push(proto::LinkOp::JoyEmit{.player = proto::PlayerIndex{i},
                                                    .mask = mask,
                                                    .autofire = proto::JoyMask{autofire[i]}})) {
                last_joy_posted_[i] = mask.v;
                wire_.note_activity();
            }
        }
    }

    if (inbox_ != nullptr && (!grabbed || osd_visible)) {
        for (std::uint8_t i = 0; i < svc::kMaxPlayers; ++i) {
            if (!zero_armed_[i]) continue;
            const proto::LinkOp::JoyRelease rel{
                .player = proto::PlayerIndex{i},
                .mask = proto::JoyMask{grabbed ? (armed_mask_[i] | autofire[i]) : armed_mask_[i]}};
            if (!muted_ && inbox_->push(rel)) wire_.note_activity();

            zero_armed_[i] = false;
        }
    }
    n_joy_seen_.store(seen, std::memory_order_relaxed);
    n_joy_players_.store(players, std::memory_order_relaxed);
}

InputDecode::Counts InputDecode::counts() const noexcept {
    Counts c{};
    c.rounds = n_rounds_.load(std::memory_order_relaxed);
    c.events = n_events_.load(std::memory_order_relaxed);
    c.keys_published = n_keys_pub_.load(std::memory_order_relaxed);
    c.keys_emitted = n_keys_emit_.load(std::memory_order_relaxed);
    c.mouse_packets = n_mouse_pkt_.load(std::memory_order_relaxed);
    c.kicks = n_kicks_.load(std::memory_order_relaxed);
    c.fd_evictions = n_evicted_.load(std::memory_order_relaxed);
    c.registered = registered_.load(std::memory_order_relaxed);
    c.joy_seen = n_joy_seen_.load(std::memory_order_relaxed);
    c.joy_players = n_joy_players_.load(std::memory_order_relaxed);
    c.captures = n_captures_.load(std::memory_order_relaxed);
    return c;
}

}  // namespace mister::app
