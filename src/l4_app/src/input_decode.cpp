// SPDX-License-Identifier: GPL-3.0-or-later
#include "infra/seat.h"
#include "app/input_decode.h"

#include <sys/epoll.h>

#include <cerrno>
#include <optional>
#include <utility>

#include "app/link_event_dispatch.h"
#include "app/link_rx_channel.h"
#include "app/link_tx_channel.h"
#include "infra/diag_log.h"
#include "infra/message_sum.h"

namespace mister::app {

namespace {

Error os_error(infra::ErrorSite site) {
    return Error{Errc::os, site, static_cast<std::uint32_t>(errno)};
}

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
    DeviceTableLoans& tables,
    os::IClock& clock,
    Wiring w
) noexcept
    : wire_(wire),
      svc_(svc),
      tables_rx_(tables.worker()),
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
    {

        const SeatScope stands_in_for_input{kSeat};
        (void)take_tables_();
    }
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
        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = d.fd();
        (void)::epoll_ctl(epfd_.get(), EPOLL_CTL_DEL, d.fd(), &ev);
        if (::epoll_ctl(epfd_.get(), EPOLL_CTL_ADD, d.fd(), &ev) == 0) {
            ++registered_;
        }
    }
    const int watch = svc_.hotplug().fd();
    epoll_event watch_ev{};
    watch_ev.events = EPOLLIN;
    watch_ev.data.fd = watch;
    (void)::epoll_ctl(epfd_.get(), EPOLL_CTL_DEL, watch, &watch_ev);
    if (::epoll_ctl(epfd_.get(), EPOLL_CTL_ADD, watch, &watch_ev) == 0) {
        ++registered_;
    }

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
        (void)::epoll_ctl(epfd_.get(), EPOLL_CTL_DEL, d.fd(), nullptr);
    }
}

bool InputDecode::apply_requests() {
    bool rebuilt = false;

    const auto osd_on = wire_.take_osd_request();
    if (osd_on)
        wire_.set_gate(InputWire::kGateOsdVisible, *osd_on);

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

    if (registered_ != 0)
        --registered_;
    tally_.eviction();
    if (rearm_credit_ != 0) {
        --rearm_credit_;
        wire_.request(InputWire::kReqHotplug);
    }
}

unsigned InputDecode::decode_round(int timeout_ms) {
    before_wait();
    wait_events(infra::ParkTimeout{timeout_ms});
    return after_wait();
}

void InputDecode::before_wait() {
    tally_.round();
    drain_link_events_();
    rebuilt_ = apply_requests();
    rumble_.refresh();
}

void InputDecode::wait_events(infra::ParkTimeout timeout) noexcept {
    n_ready_ = ::epoll_wait(epfd_.get(), evs_, static_cast<int>(kMaxEpollEvents), timeout.count());
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
            && !wire_.gated(InputWire::kGateOsdVisible);
        bridging_ = launcher_keys_->set_active(owns);
    }

    drain_link_events_();
    unsigned decoded = 0;
    bool published = false;
    for (int i = 0; i < n; ++i) {
        const ReadyFd ready = drain_ready_fd(evs_[i].data.fd, evs_[i].events);
        decoded += ready.decoded;
        published = published || ready.published;
    }

    if (published || rebuilt || joy_.owes_publish()) {
        joy_.publish(muted_);
        tally_.events(decoded);
    }

    flush_mouse_();
    unsigned framed = flush_ps2_frames_();
    sweep_dropped_keys_();
    note_dropped_mouse_();
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

InputDecode::ReadyFd InputDecode::drain_ready_fd(int fd, std::uint32_t events) {
    ReadyFd out{};
    if (fd == wire_.ctrl_fd()) {
        wire_.drain_ctrl();
        return out;
    }

    if (fd == stop_fd_)
        return out;
    if (pause_wake_ && fd == pause_wake_->fd()) {
        pause_wake_->drain();
        return out;
    }

    if ((events & (EPOLLHUP | EPOLLERR)) != 0u) {
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
            wire_.request(InputWire::kReqHotplug);
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

void InputDecode::drain_link_events_() {
    while (const auto ev = link_rx_.pop()) {
        infra::dispatch<LinkEventRoutes<EventSink::Input>>(*ev, *this);
    }
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

    if (!wire_.gated(InputWire::kGateOsdVisible)) {
        if (mouse_.motion(p->motion, p->buttons)) {
            tally_.mouse_packet();
            wire_.note_activity();
        }
    }
}

unsigned InputDecode::flush_ps2_frames_() {
    unsigned pushed = 0;
    if (muted_) {

        while (kbd_.next_frame()) {
        }
        while (mouse_.next_frame()) {
        }
        return pushed;
    }

    while (inbox_.ring().size() < proto::kLinkTxCapacity) {
        const auto frame = kbd_.next_frame();
        if (!frame)
            break;
        if (!inbox_.push(proto::LinkOp::Ps2Frame{*frame})) {
            wire_.note_key_drop(proto::FrameDrops{1});
            return pushed;
        }
        ++pushed;
        wire_.note_activity();
    }
    while (inbox_.ring().size() < proto::kLinkTxCapacity) {
        const auto frame = mouse_.next_frame();
        if (!frame)
            break;
        if (!inbox_.push(proto::LinkOp::Ps2Frame{*frame}))
            return pushed;
        ++pushed;
    }
    return pushed;
}

void InputDecode::sweep_dropped_keys_() {
    if (const proto::FrameDrops drops = kbd_.overflow_drops(); drops != wm_frame_drops_) {
        wire_.note_key_drop(drops.since(wm_frame_drops_));
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

    wm_frame_drops_ = kbd_.overflow_drops();
    wire_.note_key_sweep(released);
}

void InputDecode::note_dropped_mouse_() {
    const proto::FrameDrops drops = mouse_.overflow_drops();
    if (drops == wm_mouse_drops_)
        return;
    wm_mouse_drops_ = drops;
    diag_.appendf(
        "{\"t\":\"input_drop\",\"ring\":\"mouse\",\"cap\":%u,\"dropped\":%s}",
        static_cast<unsigned>(proto::Ps2Mouse::capacity()),
        drops.text().c_str()
    );
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
        static_cast<unsigned>(e.code.v),
        static_cast<unsigned>(e.pressed)
    );
    return false;
}

void InputDecode::publish_from(const svc::DeviceReport& r, bool is_mouse, svc::DeviceSlot slot) {

    const bool osd_visible = wire_.gated(InputWire::kGateOsdVisible);
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

        const RawKeyEdge ke{code, e.pressed != 0, 0};
        if (is_osd_toggle_(code)) {

            const RawKeyEdge tog{kKeyF12, ke.pressed, ke.reserved};
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
            if (!push_ui_edge(RawKeyEdge{kKeyF12, true, 0}))
                break;
            ++joy_ui_keys_;
        }
        for (std::uint8_t i = 0; i < r.osd_breaks; ++i) {
            if (!push_ui_edge(RawKeyEdge{kKeyF12, false, 0}))
                break;
            ++joy_ui_keys_;
        }
    }

    if (!is_mouse)
        publish_joy_menu(r, slot, osd_visible);

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
        wire_.capture_hit().publish(hit);
        capture_served_ = arm_.token;
        tally_.capture();
        return;
    }
}

namespace {

constexpr svc::EvdevCode kKeyRight{106};
constexpr svc::EvdevCode kKeyLeft{105};
constexpr svc::EvdevCode kKeyUp{103};
constexpr svc::EvdevCode kKeyDown{108};
constexpr svc::EvdevCode kKeyEnter{28};
constexpr svc::EvdevCode kKeyBackspace{14};
constexpr svc::EvdevCode kKeyTab{15};
constexpr svc::EvdevCode kKeyMinus{12};
constexpr svc::EvdevCode kKeyEqual{13};
constexpr svc::EvdevCode kKeyGrave{41};
constexpr svc::EvdevCode kKeyBack{158};

constexpr svc::JoyMask kJoyRight{proto::kJoyRight};
constexpr svc::JoyMask kJoyLeft{proto::kJoyLeft};
constexpr svc::JoyMask kJoyUp{proto::kJoyUp};
constexpr svc::JoyMask kJoyDown{proto::kJoyDown};
constexpr svc::JoyMask kJoyBtn1{proto::kJoyBtn1};
constexpr svc::JoyMask kJoyBtn2{proto::kJoyBtn2};
constexpr svc::JoyMask kJoyBtn3{proto::kJoyBtn3};
constexpr svc::JoyMask kJoyBtn4{proto::kJoyBtn4};
constexpr svc::JoyMask kJoyL{proto::kJoyL};
constexpr svc::JoyMask kJoyR{proto::kJoyR};
constexpr svc::JoyMask kJoyR2{proto::kJoyR2};

constexpr svc::JoyMask kJoyChord = kJoyBtn1.with(kJoyBtn2);
constexpr svc::JoyMask kJoyChordTriple = kJoyChord.with(kJoyBtn3);

std::optional<svc::EvdevCode> joy_menu_key(svc::JoyMask mask) noexcept {
    if (mask == kJoyRight)
        return kKeyRight;
    if (mask == kJoyLeft)
        return kKeyLeft;
    if (mask == kJoyUp)
        return kKeyUp;
    if (mask == kJoyDown)
        return kKeyDown;
    if (mask == kJoyBtn1)
        return kKeyEnter;
    if (mask == kJoyBtn2)
        return kKeyBack;
    if (mask == kJoyBtn3)
        return kKeyBackspace;
    if (mask == kJoyBtn4)
        return kKeyTab;
    if (mask == kJoyL)
        return kKeyMinus;
    if (mask == kJoyR)
        return kKeyEqual;
    if (mask == kJoyR2)
        return kKeyGrave;
    return std::nullopt;
}

}

void InputDecode::publish_joy_menu(
    const svc::DeviceReport& r,
    svc::DeviceSlot slot,
    bool osd_visible
) {
    DeviceInputState& state = devices_[slot];
    const svc::JoyMask now = r.menu_buttons;
    const svc::JoyMask prev = state.prev_menu;
    state.prev_menu = now;
    if (launcher_keys_)
        launcher_keys_->pad(slot, now);

    if (arm_.armed || bridging_ || !osd_visible) {
        osdbtn_ = {};
        return;
    }

    svc::JoyMask diff = now.differing(prev);
    while (!diff.empty()) {
        const svc::JoyMask bit = diff.lowest();
        diff = diff.without(bit);
        const bool press = now.any(bit);

        svc::JoyMask mask = bit.without(kJoyBtn3);
        if (press) {
            osdbtn_ = osdbtn_.with(mask);
            if (mask.any(kJoyChord) && osdbtn_.only(kJoyChord) == kJoyChord) {
                osdbtn_ = osdbtn_.with(kJoyBtn3);
                mask = kJoyBtn3;
            }
        } else {
            const svc::JoyMask old = osdbtn_;
            osdbtn_ = osdbtn_.without(mask);
            if (mask.any(kJoyChord)) {
                if (old.only(kJoyChordTriple) == kJoyChordTriple) {
                    mask = kJoyBtn3;
                } else if (old.any(kJoyBtn3)) {
                    if (!osdbtn_.any(kJoyChord))
                        osdbtn_ = osdbtn_.without(kJoyBtn3);
                    mask = {};
                }
            }
            if (mask.any(kJoyBtn2) && !old.any(kJoyBtn2))
                mask = {};
        }

        const std::optional<svc::EvdevCode> code = joy_menu_key(mask);
        if (!code)
            continue;

        if (!push_ui_edge(RawKeyEdge{*code, press, 0}))
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
    c.registered = registered_;
    c.joy_seen = tally_.joy_seen();
    c.joy_players = tally_.joy_players();
    c.captures = tally_.captures();
    c.service = svc_.counters().record();
    return c;
}

}
