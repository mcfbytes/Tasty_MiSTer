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

const AxisCal& InputDevice::axis(unsigned code) const {
    if (code >= kAxisCount)
        return kNullAxis;
    return axes_[code];
}

StickCal InputDevice::stick_cal(int stick) const noexcept {
    if (stick < 0 || stick > 1)
        return StickCal{};
    const auto i = static_cast<std::size_t>(stick);
    return StickCal{max_cardinal_[i], std::sqrt(max_range_sq_[i])};
}

void InputDevice::install_sys_map(const JoyWords& words) noexcept {
    sys_map_ = words;

    if (sys_map_[kSysBtnCntEsc] == 0)
        sys_map_[kSysBtnCntEsc] = sys_map_[kSysBtnCntOk];

    menu_map_ = rules::derive_menu_map(sys_map_, facts_.quirk != QuirkId::Wheel);
}

bool InputDevice::matches_sys_button(std::uint16_t code) const noexcept {
    if (code == 0)
        return false;
    for (std::size_t i = 0; i <= 11; ++i) {
        const std::uint32_t packed = sys_map_[i];
        if (packed == 0)
            continue;
        if ((packed & 0xFFFFu) == code)
            return true;
        if ((packed >> 16) == code)
            return true;
    }
    return false;
}

bool InputDevice::has_nonzero_joy_map() const noexcept {
    if (!joy_map_)
        return false;
    for (const std::uint32_t w : *joy_map_) {
        if (w != 0)
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
    std::uint16_t strong,
    std::uint16_t weak
) noexcept {
    ff_effect e{};
    e.type = FF_RUMBLE;
    e.id = id;
    e.direction = quirk == QuirkId::Wheel ? 0x4000 : 0x0000;
    e.u.rumble.strong_magnitude = strong;
    e.u.rumble.weak_magnitude = weak;
    e.replay.length = kRumbleLengthMs;
    e.replay.delay = 0;
    return e;
}

Ex<void> InputDevice::set_rumble(std::uint16_t strong, std::uint16_t weak) {
    if (!rumble_ || !ev_.valid())
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    if (strong == 0 && weak == 0) {
        if (rumble_id_ == -1)
            return {};
        const bool removed = ev_.erase_effect(static_cast<int>(rumble_id_));
        rumble_id_ = -1;
        if (!removed)
            return std::unexpected(os_error(ERR_SITE()));
        return {};
    }
    ff_effect e = make_rumble_effect(facts_.quirk, rumble_id_, strong, weak);
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

void InputDevice::mark_down(std::uint16_t code, bool on) noexcept {
    const std::size_t word = code / 64u;
    if (word >= down_.size())
        return;
    const std::uint64_t mask = std::uint64_t{1} << (code % 64u);
    if (on)
        down_[word] |= mask;
    else
        down_[word] &= ~mask;
}

void InputDevice::push_key_edge(std::uint16_t code, bool pressed) noexcept {
    mark_down(code, pressed);
    keys_changed_ = true;
    if (report_.key_count < kMaxKeyEdges) {
        report_.keys[report_.key_count] = KeyEdge{code, pressed};
        ++report_.key_count;
    } else {
        ++report_.key_overflow;
    }
}

void InputDevice::track_osd_combo(std::uint16_t code, bool pressed) noexcept {
    if (code == 0)
        return;
    const std::uint8_t was = osd_combo_;
    const auto wide = static_cast<std::uint32_t>(code);
    if (sys_map_[kSysBtnCntEsc] != 0 && wide == sys_map_[kSysBtnCntEsc]) {
        if (pressed)
            osd_combo_ |= 2u;
        else
            osd_combo_ = static_cast<std::uint8_t>(osd_combo_ & ~2u);
    }
    if (sys_map_[kSysBtnCntOk] != 0 && wide == sys_map_[kSysBtnCntOk]) {
        if (pressed)
            osd_combo_ |= 1u;
        else
            osd_combo_ = static_cast<std::uint8_t>(osd_combo_ & ~1u);
    }
    if (code < 256 || was == osd_combo_)
        return;
    if (was != 3u && osd_combo_ == 3u) {
        if (report_.osd_makes != 0xFFu)
            ++report_.osd_makes;
    } else if (was == 3u && osd_combo_ != 3u) {
        if (report_.osd_breaks != 0xFFu)
            ++report_.osd_breaks;
    }
}

void InputDevice::synth_axis(std::uint16_t code, std::int32_t value, const AxisCal& cal) noexcept {
    if (code >= kAxisCount)
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
        for (unsigned nn = 0; nn < 4; ++nn) {
            const std::uint32_t w = sys_map_[24u + nn];
            if (w != 0 && (w & 0xFFFFu) == code)
                only_max = false;
        }
        if (value < center - threshold && !only_max)
            edge = 1;
        if (value > center + threshold)
            edge = 2;
    }

    const std::uint8_t last = axis_edge_[code];
    if (last == edge)
        return;
    axis_edge_[code] = edge;

    const auto base = static_cast<std::uint16_t>(
        kKeyEmuBase + (static_cast<unsigned>(code) << 1u) - 1u
    );
    if (last != 0) {
        push_key_edge(static_cast<std::uint16_t>(base + last), false);
        if (report_.axis_edges != 0xFFFFu)
            ++report_.axis_edges;
    }
    if (edge != 0) {
        push_key_edge(static_cast<std::uint16_t>(base + edge), true);
        if (report_.axis_edges != 0xFFFFu)
            ++report_.axis_edges;
    }
}

namespace {

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
    int stick;
    bool is_y;
};

std::optional<StickAxis> stick_of(std::uint16_t code) {
    switch (code) {
        case ABS_X:
            return StickAxis{0, false};
        case ABS_Y:
            return StickAxis{0, true};
        case ABS_RX:
            return StickAxis{1, false};
        case ABS_RY:
            return StickAxis{1, true};
        default:
            return std::nullopt;
    }
}

}

Ex<unsigned> InputDevice::drain() {
    if (!ev_.valid())
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});

    report_ = DeviceReport{};
    report_.buttons = joy_prev_;
    report_.menu_buttons = menu_prev_;
    report_.mouse_buttons = mouse_buttons_;

    report_.stick[0] = prev_stick_[0];
    report_.stick[1] = prev_stick_[1];
    unsigned decoded = 0;
    keys_changed_ = false;

    struct input_event evs[32];
    bool more = true;
    while (more) {
        const ssize_t n = ev_.read(evs);
        if (n < 0) {

            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            return std::unexpected(os_error(ERR_SITE()));
        }
        if (n == 0)
            break;
        const auto whole = static_cast<std::size_t>(n) / sizeof(struct input_event);

        const std::size_t rem = static_cast<std::size_t>(n) - whole * sizeof(struct input_event);
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

            if (facts_.quirk == QuirkId::Ds4Touch
                && ev.type == EV_KEY
                && (ev.code == BTN_TOOL_FINGER
                    || ev.code == BTN_TOUCH
                    || ev.code == BTN_TOOL_DOUBLETAP)) {

                if (report_.quirk_drops != 0xFFFFu)
                    ++report_.quirk_drops;
                continue;
            }
            if (ev.type == EV_ABS && is_dualshock_like(facts_.quirk)) {

                if (ev.code > 40) {
                    if (report_.quirk_drops != 0xFFFFu)
                        ++report_.quirk_drops;
                    continue;
                }

                if (facts_.quirk == QuirkId::Ds4Touch && ev.code <= 1) {
                    if (report_.quirk_drops != 0xFFFFu)
                        ++report_.quirk_drops;
                    continue;
                }
            }
            switch (ev.type) {
                case EV_KEY: {
                    const auto code = static_cast<std::uint16_t>(ev.code);
                    const bool pressed = ev.value != 0;
                    track_osd_combo(code, pressed);
                    push_key_edge(code, pressed);
                    if (code >= BTN_LEFT && code <= BTN_MIDDLE) {
                        const auto bit = static_cast<std::uint8_t>(1u << (code - BTN_LEFT));
                        if (pressed)
                            report_.mouse_buttons |= bit;
                        else
                            report_.mouse_buttons = static_cast<std::uint8_t>(
                                report_.mouse_buttons & ~bit
                            );
                        report_.mouse_changed = true;
                    }
                    break;
                }
                case EV_REL:

                    if (ev.code == REL_X)
                        report_.rel_x += ev.value;
                    else if (ev.code == REL_Y)
                        report_.rel_y += ev.value;
                    else if (ev.code == REL_WHEEL)
                        report_.rel_wheel += ev.value;
                    else
                        break;
                    report_.mouse_changed = true;
                    break;
                case EV_ABS: {
                    const auto sa = stick_of(static_cast<std::uint16_t>(ev.code));

                    const AxisCal& cal = axis(ev.code);
                    if (!sa) {
                        synth_axis(static_cast<std::uint16_t>(ev.code), ev.value, cal);
                        break;
                    }
                    const int stick = sa->stick;
                    const bool is_y = sa->is_y;
                    const std::int32_t v = normalize_axis(ev.value, cal);
                    if (is_y)
                        raw_[stick].y = v;
                    else
                        raw_[stick].x = v;

                    const std::int32_t ax = raw_[stick].x < 0 ? -raw_[stick].x : raw_[stick].x;
                    const std::int32_t ay = raw_[stick].y < 0 ? -raw_[stick].y : raw_[stick].y;
                    const auto card = static_cast<float>(ax > ay ? ax : ay);
                    if (card > max_cardinal_[stick])
                        max_cardinal_[stick] = card;

                    if (rules::is_diagonal(raw_[stick])) {
                        const auto rr = static_cast<float>(ax) * static_cast<float>(ax)
                            + static_cast<float>(ay) * static_cast<float>(ay);
                        if (rr > max_range_sq_[stick])
                            max_range_sq_[stick] = rr;
                    }
                    if (ev.code < kAxisCount) {
                        const std::int32_t seen = is_y ? ay : ax;
                        if (seen > axes_[ev.code].seen_max)
                            axes_[ev.code].seen_max = seen;
                    }

                    const StickCal sc = stick_cal(stick);
                    rules::DeadzoneParams dp{};
                    dp.deadzone = deadzone_;
                    dp.max_cardinal = sc.max_cardinal;
                    dp.max_range = sc.max_range;
                    AnalogXy xy = rules::apply_deadzone(raw_[stick], dp);

                    if (reshape_)
                        xy = reshape_->reshape(xy, cal, sc);
                    if (!(xy == prev_stick_[stick])) {
                        report_.stick_changed[stick] = true;
                        prev_stick_[stick] = xy;
                    }
                    report_.stick[stick] = xy;

                    synth_axis(static_cast<std::uint16_t>(ev.code), ev.value, cal);
                    break;
                }
                default:
                    break;
            }
        }
    }

    mouse_buttons_ = report_.mouse_buttons;
    if (keys_changed_ && joy_map_) {

        const JoyMask now = rules::mask_for(*joy_map_, down_);
        report_.buttons = now;
        report_.buttons_changed = !(now == joy_prev_);
        joy_prev_ = now;
    }
    if (keys_changed_) {
        const JoyMask menu_now = rules::mask_for(menu_map_, down_);
        report_.menu_buttons = menu_now;
        menu_prev_ = menu_now;
    }
    return decoded;
}

}
