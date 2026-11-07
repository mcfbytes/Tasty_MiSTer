// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/input_device.h"
#include "svc/analog_reshape.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cmath>
#include <utility>

#include "input_rules.h"
#include "input_service_detail.h"

namespace mister::svc {

namespace {

constexpr std::size_t kSysBtnCntOk = 21;
constexpr std::size_t kSysBtnCntEsc = 22;

constexpr std::size_t kSysAxisFirst = 24;
constexpr unsigned kSysAxisWords = 4;

const AxisCal kNullAxis{};

}

namespace {

AxisTable cache_axes(const os::Evdev& ev) {
    AxisTable axes{};
    for (unsigned code = 0; code < kAxisCount; ++code) {
        const std::optional<input_absinfo> info = ev.abs_info(code);
        if (!info)
            continue;
        AxisCal cal{};
        cal.min = info->minimum;
        cal.max = info->maximum;
        cal.flat = info->flat;
        cal.fuzz = info->fuzz;
        axes[code] = cal;
    }
    return axes;
}

}

namespace {

bool reports_rumble(const os::Evdev& ev) noexcept {
    if (!ev.effect_count())
        return false;
    return ev.has_ff(FF_RUMBLE);
}
}

Ex<InputDevice> InputDevice::open(std::string_view devnode) {
    if (devnode.empty()) {
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    }
    std::string path(devnode);

    os::Evdev ev(::open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC));
    if (!ev.valid())
        return std::unexpected(os_error(ERR_SITE()));

    InputDevice dev;
    dev.facts_.devname = path;
    dev.facts_.mouse = rules::is_mouse_node(path);
    if (!dev.facts_.mouse) {
        if (const auto id = ev.id()) {
            dev.facts_.id.vid = Vid{id->vendor};
            dev.facts_.id.pid = Pid{id->product};
            dev.facts_.id.version = id->version;
            dev.facts_.id.bustype = id->bustype;
        }
        char name[128] = {};
        if (ev.name(name)) {
            name[sizeof name - 1] = 0;
            dev.facts_.id.name = name;
        }

        char uniq[32] = {};
        if (ev.uniq(uniq)) {
            uniq[sizeof uniq - 1] = 0;
            dev.facts_.uniq = uniq;
        }
        dev.axes_ = cache_axes(ev);
        dev.rumble_ = reports_rumble(ev);
    }
    dev.ev_ = std::move(ev);
    return dev;
}

Ex<InputDevice> InputDevice::adopt(
    int fd,
    std::string_view devnode,
    const DeviceIdentity& id,
    bool mouse
) {
    if (fd < 0)
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    InputDevice dev;
    dev.ev_ = os::Evdev{fd};
    dev.facts_.devname = devnode;
    dev.facts_.mouse = mouse;
    dev.facts_.id = id;
    return dev;
}

const AxisCal& InputDevice::axis(EvdevCode code) const {
    const auto slot = code.axis_position<kAxisCount>();
    if (!slot)
        return kNullAxis;
    return axes_[*slot];
}

StickCal InputDevice::stick_cal(StickIndex stick) const noexcept {
    const auto s = sticks_.find(stick);
    return s ? s->cal() : StickCal{};
}

const JoyWords& DeviceMaps::install_sys(const JoyWords& words) noexcept {
    sys = words;
    if (packed_word(sys[kSysBtnCntEsc]) == 0)
        sys[kSysBtnCntEsc] = sys[kSysBtnCntOk];
    return sys;
}

std::span<const PackedKeys> DeviceMaps::wire_view() const noexcept {
    static constexpr JoyWords kUnset{};
    return wire ? std::span<const PackedKeys>{*wire} : std::span<const PackedKeys>{kUnset};
}

void InputDevice::install_sys_map(const JoyWords& words) noexcept {
    const JoyWords& fixed = maps_.install_sys(words);

    maps_.set_menu(rules::derive_menu_map(fixed, facts_.quirk != QuirkId::Wheel));
}

bool InputDevice::matches_sys_button(EvdevCode code) const noexcept {
    if (code.is_unset())
        return false;
    for (std::size_t i = 0; i <= 11; ++i) {
        const PackedKeys packed = maps_.sys[i];
        if (packed == PackedKeys{})
            continue;
        if (packed.lo == code || packed.hi == code)
            return true;
    }
    return false;
}

bool InputDevice::has_nonzero_joy_map() const noexcept {
    if (!maps_.wire_present())
        return false;
    for (const PackedKeys w : *maps_.wire) {
        if (w != PackedKeys{})
            return true;
    }
    return false;
}

Ex<void> InputDevice::set_grabbed(bool on) {
    if (!ev_.valid())
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    if (!ev_.grab(on)) {
        return std::unexpected(os_error(ERR_SITE()));
    }
    grabbed_ = on;
    return {};
}

ff_effect make_rumble_effect(
    QuirkId quirk,
    std::int16_t id,
    proto::user_io::RumbleMotors motors
) noexcept {
    ff_effect e{};
    e.type = FF_RUMBLE;
    e.id = id;
    e.direction = quirk == QuirkId::Wheel ? 0x4000 : 0x0000;
    e.u.rumble.strong_magnitude = motors.strong;
    e.u.rumble.weak_magnitude = motors.weak;
    e.replay.length = kRumbleLengthMs;
    e.replay.delay = 0;
    return e;
}

Ex<void> InputDevice::set_rumble(proto::user_io::RumbleMotors motors) {
    if (!rumble_ || !ev_.valid())
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    if (motors.strong == 0 && motors.weak == 0) {
        if (rumble_id_ == -1)
            return {};
        const bool removed = ev_.erase_effect(static_cast<int>(rumble_id_));
        rumble_id_ = -1;
        if (!removed)
            return std::unexpected(os_error(ERR_SITE()));
        return {};
    }
    ff_effect e = make_rumble_effect(facts_.quirk, rumble_id_, motors);
    if (!ev_.upload_effect(e))
        return std::unexpected(os_error(ERR_SITE()));
    rumble_id_ = e.id;
    input_event play{};
    play.type = EV_FF;
    play.code = static_cast<std::uint16_t>(rumble_id_);
    play.value = 1;
    if (::write(ev_.fd(), &play, sizeof play) < 0)
        return std::unexpected(os_error(ERR_SITE()));
    return {};
}

bool InputDevice::release_for_handover() noexcept {
    bool ok = true;
    if (rumble_id_ != -1) {
        ok = ev_.valid() && ev_.erase_effect(static_cast<int>(rumble_id_));
        rumble_id_ = -1;
    }
    if (grabbed_) {
        ok = ev_.valid() && ev_.grab(false) && ok;
        grabbed_ = false;
    }
    return ok;
}

void InputDevice::mark_down(EvdevCode code, bool on) noexcept {
    if (on)
        down_.set(code);
    else
        down_.clear(code);
}

void InputDevice::push_key_edge(EvdevCode code, bool pressed) noexcept {
    mark_down(code, pressed);
    keys_changed_ = true;
    report_.keys.push(KeyEdge{code, pressed});
}

void InputDevice::track_osd_combo(EvdevCode code, bool pressed) noexcept {
    if (code.is_unset())
        return;
    const std::uint8_t was = osd_combo_;
    if (maps_.sys[kSysBtnCntEsc] == PackedKeys::lone(code)) {
        if (pressed)
            osd_combo_ |= 2u;
        else
            osd_combo_ = static_cast<std::uint8_t>(osd_combo_ & ~2u);
    }
    if (maps_.sys[kSysBtnCntOk] == PackedKeys::lone(code)) {
        if (pressed)
            osd_combo_ |= 1u;
        else
            osd_combo_ = static_cast<std::uint8_t>(osd_combo_ & ~1u);
    }
    if (code.is_key() || was == osd_combo_)
        return;
    if (was != 3u && osd_combo_ == 3u) {
        if (report_.osd_makes != 0xFFu)
            ++report_.osd_makes;
    } else if (was == 3u && osd_combo_ != 3u) {
        if (report_.osd_breaks != 0xFFu)
            ++report_.osd_breaks;
    }
}

void InputDevice::synth_axis(EvdevCode code, std::int32_t value, const AxisCal& cal) noexcept {
    const auto slot = code.axis_position<kAxisCount>();
    if (!slot)
        return;

    if (is_pdsp_like(facts_.quirk))
        return;

    std::uint8_t edge = 0;
    if ((cal.max == 1 && cal.min == -1) || (cal.max == 2 && cal.min == 0)) {

        if (value == cal.min)
            edge = 1;
        if (value == cal.max)
            edge = 2;
    } else {

        const std::int32_t range = cal.max - cal.min + 1;
        const std::int32_t center = cal.min + (range / 2);
        const std::int32_t threshold = range / 4;

        bool only_max = true;
        for (unsigned nn = 0; nn < kSysAxisWords; ++nn) {
            const PackedKeys w = maps_.sys[kSysAxisFirst + nn];
            if (w != PackedKeys{} && w.lo == code)
                only_max = false;
        }
        if (value < center - threshold && !only_max)
            edge = 1;
        if (value > center + threshold)
            edge = 2;
    }

    const std::uint8_t last = axis_edge_[*slot];
    if (last == edge)
        return;
    axis_edge_[*slot] = edge;

    const auto edge_code = [code](std::uint8_t e) {
        return e == 1 ? code.key_emu_min() : code.key_emu_max();
    };
    if (last != 0) {
        push_key_edge(edge_code(last), false);
        if (report_.axis_edges != 0xFFFFu)
            ++report_.axis_edges;
    }
    if (edge != 0) {
        push_key_edge(edge_code(edge), true);
        if (report_.axis_edges != 0xFFFFu)
            ++report_.axis_edges;
    }
}

namespace {

struct PersonalityFilter {
    QuirkId quirk = QuirkId::None;

    static constexpr EvdevCode kLastDsAxis{40};

    [[nodiscard]] bool drops(const input_event& ev) const noexcept {

        if (quirk == QuirkId::Ds4Touch
            && ev.type == EV_KEY
            && (ev.code == BTN_TOOL_FINGER
                || ev.code == BTN_TOUCH
                || ev.code == BTN_TOOL_DOUBLETAP)) {
            return true;
        }
        if (ev.type != EV_ABS || !is_dualshock_like(quirk))
            return false;

        const EvdevCode code{ev.code};
        if (code > kLastDsAxis)
            return true;

        return quirk == QuirkId::Ds4Touch && (code == EvdevCode{ABS_X} || code == EvdevCode{ABS_Y});
    }
};

std::int32_t normalize_axis(std::int32_t value, const AxisCal& cal) {
    const std::int32_t hrange = (cal.max - cal.min) / 2;
    if (hrange == 0)
        return 0;
    std::int32_t v = value;
    if (v < cal.min)
        v = cal.min;
    else if (v > cal.max)
        v = cal.max;
    v -= (cal.min + cal.max) / 2;
    v = (v * 127) / hrange;
    if (v < -127)
        v = -127;
    else if (v > 127)
        v = 127;
    return v;
}

struct StickAxis {
    StickIndex stick;
    bool is_y;
};

std::optional<StickAxis> stick_of(EvdevCode code) {
    if (code == EvdevCode{ABS_X})
        return StickAxis{kLeftStick, false};
    if (code == EvdevCode{ABS_Y})
        return StickAxis{kLeftStick, true};
    if (code == EvdevCode{ABS_RX})
        return StickAxis{kRightStick, false};
    if (code == EvdevCode{ABS_RY})
        return StickAxis{kRightStick, true};
    return std::nullopt;
}

}

void InputDevice::decode_key(const input_event& ev) noexcept {
    const EvdevCode code{ev.code};
    const bool pressed = ev.value != 0;
    track_osd_combo(code, pressed);
    push_key_edge(code, pressed);
    if (ev.code >= BTN_LEFT && ev.code <= BTN_MIDDLE) {
        const proto::MouseButtons bit = proto::MouseButtons::nth(ev.code - BTN_LEFT);
        report_.mouse_buttons = pressed ? report_.mouse_buttons.with(bit)
                                        : report_.mouse_buttons.without(bit);
        report_.mouse_changed = true;
    }
}

void InputDevice::decode_rel(const input_event& ev) noexcept {
    if (ev.code == REL_X)
        report_.rel_x += ev.value;
    else if (ev.code == REL_Y)
        report_.rel_y += ev.value;
    else if (ev.code == REL_WHEEL)
        report_.rel_wheel += ev.value;
    else
        return;
    report_.mouse_changed = true;
}

void InputDevice::decode_abs(const input_event& ev) noexcept {
    const EvdevCode code{ev.code};
    const auto sa = stick_of(code);

    const AxisCal& cal = axis(code);
    if (!sa) {
        synth_axis(code, ev.value, cal);
        return;
    }
    const StickIndex stick = sa->stick;
    const bool is_y = sa->is_y;
    StickState& st = sticks_[stick];
    const std::int32_t v = normalize_axis(ev.value, cal);
    if (is_y)
        st.raw.y = v;
    else
        st.raw.x = v;

    st.learn(rules::is_diagonal(st.raw));
    if (const auto slot = code.axis_position<kAxisCount>()) {
        const std::int32_t axis_value = is_y ? st.raw.y : st.raw.x;
        const std::int32_t seen = axis_value < 0 ? -axis_value : axis_value;
        if (seen > axes_[*slot].seen_max)
            axes_[*slot].seen_max = seen;
    }

    const StickCal sc = st.cal();
    rules::DeadzoneParams dp{};
    dp.deadzone = deadzone_;
    dp.max_cardinal = sc.max_cardinal;
    dp.max_range = sc.max_range;
    AnalogXy xy = rules::apply_deadzone(st.raw, dp);

    if (reshape_)
        xy = reshape_->reshape(xy, cal, sc);
    if (!(xy == st.published)) {
        report_.stick_changed[stick] = true;
        st.published = xy;
    }
    report_.stick[stick] = xy;

    synth_axis(code, ev.value, cal);
}

Ex<unsigned> InputDevice::drain() {
    if (!ev_.valid())
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});

    report_ = DeviceReport{};
    report_.buttons = joy_prev_;
    report_.menu_buttons = menu_prev_;
    report_.mouse_buttons = mouse_buttons_;

    for (const StickIndex i : kSticks)
        report_.stick[i] = sticks_[i].published;
    unsigned decoded = 0;
    keys_changed_ = false;
    const PersonalityFilter filter{facts_.quirk};

    struct input_event evs[32];
    bool more = true;
    while (more) {
        const Ex<std::size_t> got = ev_.read(evs);
        if (!got) {

            const auto err = static_cast<int>(got.error().detail);
            if (err == EINTR)
                continue;
            if (err == EAGAIN || err == EWOULDBLOCK)
                break;
            return std::unexpected(got.error());
        }
        const std::size_t n = *got;
        if (n == 0)
            break;
        const auto whole = n / sizeof(struct input_event);

        const std::size_t rem = n - whole * sizeof(struct input_event);
        if (rem != 0) {
            const std::uint32_t sum = static_cast<std::uint32_t>(report_.mouse_remainder)
                + static_cast<std::uint32_t>(rem);
            report_.mouse_remainder = static_cast<std::uint16_t>(sum > 0xFFFFu ? 0xFFFFu : sum);
        }
        if (whole < (sizeof evs / sizeof evs[0]))
            more = false;

        for (std::size_t i = 0; i < whole; ++i) {
            const struct input_event& ev = evs[i];
            ++decoded;

            if (filter.drops(ev)) {
                if (report_.quirk_drops != 0xFFFFu)
                    ++report_.quirk_drops;
                continue;
            }
            switch (ev.type) {
                case EV_KEY:
                    decode_key(ev);
                    break;
                case EV_REL:
                    decode_rel(ev);
                    break;
                case EV_ABS:
                    decode_abs(ev);
                    break;
                default:
                    break;
            }
        }
    }

    mouse_buttons_ = report_.mouse_buttons;
    if (keys_changed_ && maps_.wire_present()) {

        const JoyMask now = rules::mask_for(*maps_.wire, down_);
        report_.buttons = now;
        report_.buttons_changed = !(now == joy_prev_);
        joy_prev_ = now;
    }
    if (keys_changed_) {
        const JoyMask menu_now = rules::mask_for(maps_.menu, down_);
        report_.menu_buttons = menu_now;
        menu_prev_ = menu_now;
    }
    return decoded;
}

}
