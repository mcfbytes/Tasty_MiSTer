// SPDX-License-Identifier: GPL-3.0-or-later
#include "infra/seat.h"
#include "app/input_decode.h"

#include <sys/epoll.h>

#include <cerrno>
#include <utility>

#include "app/host_led_watch.h"
#include "app/link_event_dispatch.h"
#include "app/link_tx_channel.h"
#include "infra/diag_log.h"
#include "infra/message_sum.h"

namespace mister::app {

namespace {

constexpr svc::EvdevCode kKeyF12{88};
constexpr svc::EvdevCode kKeyMenu{139};
constexpr svc::EvdevCode kKeyLeftMeta{125};
constexpr svc::EvdevCode kKeyRightMeta{126};

bool is_pass_through_modifier(svc::EvdevCode code) noexcept {
    constexpr svc::EvdevCode kKeyLeftAlt{56};
    constexpr svc::EvdevCode kKeyRightAlt{100};
    return code == kKeyLeftAlt
        || code == kKeyRightAlt
        || code == kKeyLeftMeta
        || code == kKeyRightMeta;
}

proto::HidUsage hid_usage_of(svc::EvdevCode code) noexcept {
    return code.hid_usage();
}

constexpr svc::DeviceSlot device_slot_of(std::size_t i) noexcept {
    return svc::DeviceSlot{static_cast<std::uint8_t>(i)};
}

}

Ex<InputDecode::Fds> InputDecode::Fds::create(
    xthread::WakeFlag& ctrl,
    infra::OptRef<const HostLedWatch> led
) noexcept {
    if (ctrl.fd() < 0) {
        if (auto c = ctrl.open_fd(); !c)
            return std::unexpected(c.error());
    }
    auto epoll = os::EpollSet::create();
    if (!epoll)
        return std::unexpected(epoll.error());
    if (auto added = epoll->add(ctrl.fd()); !added)
        return std::unexpected(added.error());
    if (led) {

        epoll_event led_ev{};
        led_ev.events = EPOLLPRI;
        led_ev.data.fd = HostLedWatch::fd_of(*led).get();
        if (::epoll_ctl(epoll->fd(), EPOLL_CTL_ADD, led_ev.data.fd, &led_ev) != 0) {
            return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
        }
    }
    return Fds{std::move(*epoll)};
}

InputDecode::InputDecode(
    Fds fds,
    svc::InputService& svc,
    DeviceTableLoans& tables,
    os::IClock& clock,
    Wiring w
) noexcept
    : ctrl_(w.ctrl),
      gates_cell_(w.gates),
      key_focus_(w.key_focus),
      hotplug_(w.hotplug),
      joy_edge_reset_(w.joy_edge_reset),
      activity_(w.activity),
      capture_arm_(w.capture_arm),
      capture_hit_(w.capture_hit),
      rumble_players_(w.rumble_players),
      svc_(svc),
      tables_rx_(tables.worker()),
      diag_(w.diag),
      inbox_(w.inbox),
      rt_kick_(w.rt_kick),
      rumble_(svc, w.rumble_players, w.diag),
      launcher_cell_(w.cell),
      launcher_keys_(w.cell ? w.keys : infra::OptRef<LauncherKeyBridge>{}),
      chords_(launcher_keys_),
      menu_keys_(w.menu_keys),
      led_(w.led),
      mouse_divider_(clock),
      joy_(w.joy_edge_reset, w.activity, svc, w.inbox, tally_),
      egress_(kbd_, mouse_, w.inbox, w.activity, w.diag),
      epoll_(std::move(fds.epoll_)) {
    {

        const SeatScope stands_in_for_input{kSeat};
        gates_ = svc::InputGates{svc_.grabbed(), false, false};
        published_gates_ = gates_;
        gates_cell_.publish(gates_);
        (void)take_tables_();
    }
    rebuild_registrations(Reseat::Skip);
}

Ex<void> InputDecode::watch(const xthread::ParkFds& fds) noexcept {
    const int fd[2] = {fds.stop_fd(), fds.wake().fd()};
    for (const int f : fd) {
        if (f < 0)
            return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
        if (auto added = epoll_.add(f); !added)
            return std::unexpected(added.error());
    }
    stop_fd_ = fds.stop_fd();
    pause_wake_ = infra::OptRef<xthread::WakeFlag>{fds.wake()};
    return {};
}

namespace {

std::uint32_t device_key(const svc::InputDevice& d) noexcept {
    constexpr std::uint32_t kFnvOffsetBasis = 2166136261u;
    constexpr std::uint32_t kFnvPrime = 16777619u;
    constexpr unsigned kByteBits = 8;
    constexpr unsigned kByteMask = 0xFFu;
    const svc::DeviceIdentity& id = d.identity();
    std::uint32_t h = kFnvOffsetBasis;
    auto mix = [&h](std::uint8_t b) noexcept {
        h ^= b;
        h *= kFnvPrime;
    };
    const std::string& s = !id.merge_id.empty() ? id.merge_id : id.id;
    for (const char c : s)
        mix(static_cast<std::uint8_t>(c));
    mix(static_cast<std::uint8_t>(id.vid.v & kByteMask));
    mix(static_cast<std::uint8_t>(id.vid.v >> kByteBits));
    mix(static_cast<std::uint8_t>(id.pid.v & kByteMask));
    mix(static_cast<std::uint8_t>(id.pid.v >> kByteBits));
    return h != 0u ? h : 1u;
}
}

void InputDecode::rebuild_registrations(Reseat bridge) {

    registered_ = 0;
    for (svc::InputDevice& d : svc_.devices()) {
        if (epoll_.readd(d.fd()))
            ++registered_;
    }
    const int watch = svc_.hotplug().fd();
    if (epoll_.readd(watch))
        ++registered_;

    std::size_t slot = 0;
    for (svc::InputDevice& d : svc_.devices()) {
        if (slot >= svc::DeviceTable::kMaxDevices)
            break;
        const svc::DeviceSlot dev = device_slot_of(slot);
        DeviceInputState& state = devices_[dev];
        const std::uint32_t key = device_key(d);
        if (state.key != key) {
            state.key = key;
            state.prev_menu = d.report().menu_buttons;
            if (bridge == Reseat::Bridge && launcher_keys_)
                launcher_keys_->reseat_pad(dev, state.prev_menu);
            state.ui_down = KeyDownSet{};
        }
        ++slot;
    }

    for (; slot < svc::DeviceTable::kMaxDevices; ++slot) {
        const svc::DeviceSlot dev = device_slot_of(slot);
        DeviceInputState& state = devices_[dev];
        state.key = 0;
        state.prev_menu = {};
        if (bridge == Reseat::Bridge && launcher_keys_)
            launcher_keys_->reseat_pad(dev, {});
        state.ui_down = KeyDownSet{};
    }
    seen_gen_ = svc_.devices_generation();
}

unsigned InputDecode::take_tables_() noexcept {
    unsigned n = 0;
    while (auto job = tables_rx_.take()) {
        deregister_devices_();
        svc_.install(*job);
        job.complete();
        ++n;
    }
    return n;
}

void InputDecode::deregister_devices_() noexcept {
    for (const svc::InputDevice& d : svc_.devices()) {
        epoll_.remove(d.fd());
    }
}

bool InputDecode::apply_requests() {
    bool rebuilt = false;

    if (const auto focus = focus_reader_.take_if_changed(key_focus_)) {
        focus_ = *focus;
        gates_.osd_visible = focus_ != KeyFocus::Core;
    }

    gates_.grabbed = svc_.grabbed();
    publish_gates_();

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

void InputDecode::publish_gates_() noexcept {
    if (gates_ == published_gates_)
        return;
    gates_cell_.publish(gates_);
    published_gates_ = gates_;
}

void InputDecode::evict_fd(int fd) {
    if (fd < 0)
        return;
    epoll_.remove(fd);

    if (registered_ != 0)
        --registered_;
    tally_.eviction();
    if (rearm_credit_ != 0) {
        --rearm_credit_;
        hotplug_.request();
    }
}

void InputDecode::begin_round() {
    tally_.round();
}

void InputDecode::prepare_wait() {
    rebuilt_ = apply_requests();
    rumble_.refresh();
}

void InputDecode::wait_events(infra::ParkTimeout timeout) noexcept {
    ready_ = epoll_.wait(timeout);
}

bool InputDecode::begin_batch() {
    if (!ready_) {
        ready_ = Ex<os::EpollSet::Ready>{os::EpollSet::Ready{}};
        rebuilt_ = false;
        counts_cell_.publish(counts());
        return false;
    }

    if (const auto a = arm_reader_.take_if_changed(capture_arm_))
        arm_ = *a;
    bridging_ = false;
    if (launcher_keys_) {
        const bool owns = launcher_cell_->sample().value.owns_screen
            && !muted_
            && !gates_.osd_visible;
        bridging_ = launcher_keys_->set_active(owns);
    }
    return true;
}

unsigned InputDecode::decode_batch() {
    auto ready = std::exchange(ready_, Ex<os::EpollSet::Ready>{os::EpollSet::Ready{}});
    const bool rebuilt = rebuilt_;
    rebuilt_ = false;

    unsigned decoded = 0;
    bool published = false;
    for (const auto e : ready->events()) {
        const ReadyFd fd_ready = drain_ready_fd(e);
        decoded += fd_ready.decoded;
        published = published || fd_ready.published;
    }

    if (published || rebuilt || joy_.owes_publish(gates_)) {
        joy_.publish(muted_, gates_);
        tally_.events(decoded);
    }

    flush_mouse_();
    const Ps2Egress::Flushed first = egress_.flush_ps2_frames_(muted_, gates_, keys_);
    gates_ = first.gates;
    publish_gates_();
    gates_ = egress_.sweep_dropped_keys_(gates_, keys_);
    publish_gates_();
    egress_.note_dropped_mouse_();
    const Ps2Egress::Flushed second = egress_.flush_ps2_frames_(muted_, gates_, keys_);
    gates_ = second.gates;
    publish_gates_();
    const bool framed = first.framed || second.framed;
    if (!muted_ && (published || rebuilt || framed || joy_.owes_publish(gates_))) {

        if (rt_kick_.kick_once())
            tally_.kick();
    }
    counts_cell_.publish(counts());
    return decoded;
}

InputDecode::ReadyFd InputDecode::drain_ready_fd(os::EpollSet::Event e) {
    ReadyFd out{};
    const int fd = e.fd();
    if (fd == ctrl_.fd()) {
        ctrl_.drain();
        return out;
    }

    if (fd == stop_fd_)
        return out;
    if (pause_wake_ && fd == pause_wake_->fd()) {
        pause_wake_->drain();
        return out;
    }

    if (led_ && fd == led_->fd().get()) {
        led_->on_ready();
        return out;
    }

    if (e.failed()) {
        evict_fd(fd);
        return out;
    }
    auto got = svc_.on_fd_ready(fd);
    if (!got) {

        evict_fd(fd);
        return out;
    }
    out.decoded = *got;
    if (fd == svc_.hotplug().fd()) {

        if (svc_.hotplug().take_pending()) {
            rearm_credit_ = kEvictRearmCredit;
            hotplug_.request();
        }
        return out;
    }
    std::size_t pos = 0;
    for (svc::InputDevice& d : svc_.devices()) {
        if (d.fd() != fd) {
            ++pos;
            continue;
        }
        const svc::DeviceSlot slot = device_slot_of(pos < svc::DeviceTable::kMaxDevices ? pos : 0u);
        maybe_capture(d, slot);
        publish_from(d.report(), d.is_mouse(), slot);
        out.published = true;
        break;
    }
    return out;
}

void InputDecode::on(const proto::LinkEvent::Ps2Control& c) {
    kbd_.accept_control(c.keyboard);
    mouse_.accept_control(c.mouse);
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

bool InputDecode::is_osd_toggle_(svc::EvdevCode code) const noexcept {
    if (code == kKeyMenu)
        return true;
    if (code != kKeyF12)
        return false;
    if (!f12_mod_needed_)
        return true;
    for (const DeviceInputState& state : devices_) {
        if (state.ui_down.has(kKeyLeftMeta) || state.ui_down.has(kKeyRightMeta))
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

    if (!gates_.osd_visible) {
        if (mouse_.motion(p->motion, p->buttons)) {
            tally_.mouse_packet();
            activity_.add();
        }
    }
}

bool InputDecode::push_ui_edge(const RawKeyEdge& e) {
    if (muted_)
        return true;
    if (menu_keys_.push(e)) {

        keys_.menu_keys.add();
        return true;
    }
    keys_.menu_keys_dropped.add();
    diag_.appendf(
        "{\"t\":\"input_drop\",\"ring\":\"ui_keys\",\"cap\":%u,"
        "\"dropped\":%s,\"code\":%u,\"pressed\":%u}",
        static_cast<unsigned>(kMenuKeySlots),
        keys_.menu_keys_dropped.text().c_str(),
        static_cast<unsigned>(e.code.v),
        static_cast<unsigned>(e.pressed)
    );
    return false;
}

void InputDecode::publish_from(const svc::DeviceReport& r, bool is_mouse, svc::DeviceSlot slot) {

    const KeyFocus focus = focus_;
    const bool osd_visible = focus != KeyFocus::Core;
    DeviceInputState& state = devices_[slot];
    for (std::size_t i = 0; i < r.keys.count(); ++i) {
        const svc::KeyEdge e = r.keys.at(i);

        const svc::EvdevCode code = e.code;
        if (!code.is_key())
            continue;

        if (e.pressed != 0 && !svc_.grabbed())
            continue;

        const bool ui_repeat = e.pressed != 0 && state.ui_down.has(code);
        if (e.pressed != 0) {
            state.ui_down.set(code);
        } else {
            state.ui_down.clear(code);
        }

        const RawKeyEdge ke{code, e.pressed != 0, KeyIntent::Key};
        if (is_osd_toggle_(code)) {

            const RawKeyEdge tog{kKeyF12, ke.pressed, ke.intent};
            if (!ui_repeat && !push_ui_edge(tog))
                break;
            continue;
        }
        if (osd_visible) {
            if (!ui_repeat && !push_ui_edge(ke))
                break;
            if (!is_pass_through_modifier(code))
                continue;
        }
        if (bridging_) {
            launcher_keys_->key(code, ke.pressed != 0);
            continue;
        }

        if (kbd_.key_event(hid_usage_of(code), ke.pressed != 0)) {
            tally_.key_published();
            tally_.key_emitted();
        }
    }

    if (!arm_.armed) {
        for (std::uint8_t i = 0; i < r.osd_makes; ++i) {
            if (!push_ui_edge(RawKeyEdge{kKeyF12, true, KeyIntent::Key}))
                break;
            ++joy_ui_keys_;
        }
        for (std::uint8_t i = 0; i < r.osd_breaks; ++i) {
            if (!push_ui_edge(RawKeyEdge{kKeyF12, false, KeyIntent::Key}))
                break;
            ++joy_ui_keys_;
        }
    }

    if (!is_mouse) {
        chords_.publish_joy_menu(
            r,
            slot,
            focus,
            arm_.armed || bridging_,
            state.prev_menu,
            *this,
            [](InputDecode& lane, const RawKeyEdge& edge) {
                if (!lane.push_ui_edge(edge))
                    return false;
                ++lane.joy_ui_keys_;
                return true;
            }
        );
    }

    if (is_mouse) {

        if (svc_.grabbed()) {
            mouse_divider_.accumulate(r.motion);
        }

        mouse_divider_.note_buttons(svc_.mouse_button_level());
    }
}

void InputDecode::maybe_capture(const svc::InputDevice& d, svc::DeviceSlot slot) {
    if (!arm_.armed || arm_.token == CaptureToken{} || arm_.token == capture_served_)
        return;
    const svc::DeviceReport& r = d.report();
    for (std::size_t i = 0; i < r.keys.count(); ++i) {
        const svc::KeyEdge e = r.keys.at(i);
        if (!e.pressed || e.code.is_key())
            continue;
        const svc::DeviceIdentity& id = d.identity();
        CaptureHit hit{};
        hit.token = arm_.token;
        hit.unique_hash = id.unique_hash;
        hit.code = e.code;
        hit.device_ordinal = slot;
        hit.mod = d.mod();
        hit.unique_filenames = id.unique_filenames;
        hit.vid = id.vid;
        hit.pid = id.pid;
        (void)hit.id.assign(id.id);
        hit.id_clipped = hit.id.clipped();
        capture_hit_.publish(hit);
        capture_served_ = arm_.token;
        tally_.capture();
        return;
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
    c.registered = registered_;
    c.joy_seen = tally_.joy_seen();
    c.joy_players = tally_.joy_players();
    c.captures = tally_.captures();
    c.keys = keys_;
    c.service = svc_.counters().record();
    return c;
}

}
