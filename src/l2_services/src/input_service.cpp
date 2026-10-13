// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/input_service.h"
#include "hal/selected.h"
#include "os/evdev.h"
#include "svc/analog_reshape.h"
#include "svc/button_map.h"
#include "svc/mapping_wizard.h"
#include "svc/vfs.h"

#include <fcntl.h>
#include <linux/input.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <utility>

#include "input_rules.h"

namespace mister::svc {

namespace {

constexpr hal::SpiWord kGetKbdLed{0x1F};
constexpr hal::SpiWord kPs2Ctl{0x21};
constexpr std::uint16_t kLedPollTicks = 100;

constexpr std::size_t kSysBtnCntOk = 21;
constexpr std::size_t kSysBtnCntEsc = 22;

const AxisCal kNullAxis{};

Error os_error(std::uint16_t site) {
    return Error{Errc::os, site, static_cast<std::uint32_t>(errno)};
}

}  // namespace

std::string MapStore::filename(std::string_view core, const DeviceIdentity& id, MapKind kind,
                               bool mod) {
    return rules::map_filename(core, id, kind, mod);
}

Ex<ButtonMap> MapStore::load(std::string_view core, const DeviceIdentity& id, MapKind kind,
                             bool mod) const {
    if (!vfs_) {
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    }
    const std::string name = filename(core, id, kind, mod);
    const rules::MapPaths paths = rules::map_paths(name, kind);
    const std::size_t want = map_spec(kind).bytes;

    static constexpr SearchDir kConfig[] = {SearchDir::ConfigDir};
    const SearchPolicy policy{kConfig, true};

    std::string resolved;
    if (auto r = vfs_->resolve(paths.primary, policy); r) {
        resolved = std::move(*r);
    } else if (!paths.fallback.empty()) {
        auto f = vfs_->resolve(paths.fallback, policy);
        if (!f) return std::unexpected(f.error());
        resolved = std::move(*f);
    } else {
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    }

    auto file = vfs_->open(resolved, OpenMode::ReadWhole);
    if (!file) return std::unexpected(file.error());

    ButtonMap out{};
    out.kind = kind;
    out.blob.resize(want);
    auto got = (*file)->read_at(0, out.blob);
    if (!got) return std::unexpected(got.error());
    if (*got != want) {

        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(*got)});
    }
    return out;
}

Ex<void> MapStore::save(std::string_view core, const DeviceIdentity& id, const ButtonMap& map,
                        bool mod) const {
    if (!vfs_) {
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    }
    const std::size_t want = map_spec(map.kind).bytes;
    if (map.blob.size() != want) {

        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(map.blob.size())});
    }
    const std::string name = filename(core, id, map.kind, mod);
    const rules::MapPaths paths = rules::map_paths(name, map.kind);

    const std::string root = vfs_->root_path() + "/config/";

    if (!paths.fallback.empty()) {
        const std::string stale = root + paths.fallback;
        if (::unlink(stale.c_str()) != 0 && errno != ENOENT) {
            return std::unexpected(os_error(ERR_SITE()));
        }
    }

    const std::string dest = root + paths.primary;

    {
        std::string rel("config/");
        rel.append(paths.primary);
        const auto slash = rel.rfind('/');
        if (auto r = vfs_->ensure_dir(rel.substr(0, slash)); !r) return r;
    }

    const std::string tmp = dest + ".tmp";
    {
        auto file = vfs_->open(tmp, OpenMode::Truncate);
        if (!file) return std::unexpected(file.error());
        auto put = (*file)->write_at(0, map.blob);
        if (!put) return std::unexpected(put.error());
        if (*put != map.blob.size()) {
            return std::unexpected(
                Error{Errc::short_write, ERR_SITE(), static_cast<std::uint32_t>(*put)});
        }
        if (auto f = (*file)->flush(); !f) return f;
    }
    return vfs_->replace(tmp, dest);
}

namespace {

AxisTable cache_axes(const os::Evdev& ev) {
    AxisTable axes{};
    for (unsigned code = 0; code < kAxisCount; ++code) {
        const std::optional<input_absinfo> info = ev.abs_info(code);
        if (!info) continue;
        AxisCal cal{};
        cal.min = info->minimum;
        cal.max = info->maximum;
        cal.flat = info->flat;
        cal.fuzz = info->fuzz;
        axes[code] = cal;
    }
    return axes;
}

}  // namespace

namespace {

bool reports_rumble(const os::Evdev& ev) noexcept {
    if (!ev.effect_count()) return false;
    return ev.has_ff(FF_RUMBLE);
}
}  // namespace

Ex<InputDevice> InputDevice::open(std::string_view devnode) {
    if (devnode.empty()) {
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    }
    std::string path(devnode);

    os::Evdev ev(::open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC));
    if (!ev.valid()) return std::unexpected(os_error(ERR_SITE()));

    InputDevice dev;
    dev.devnode_ = path;
    dev.mouse_ = rules::is_mouse_node(path);
    if (!dev.mouse_) {
        if (const auto id = ev.id()) {
            dev.id_.vid = Vid{id->vendor};
            dev.id_.pid = Pid{id->product};
            dev.id_.version = id->version;
            dev.id_.bustype = id->bustype;
        }
        char name[128] = {};
        if (ev.name(name)) {
            name[sizeof name - 1] = 0;
            dev.id_.name = name;
        }

        char uniq[32] = {};
        if (ev.uniq(uniq)) {
            uniq[sizeof uniq - 1] = 0;
            dev.uniq_ = uniq;
        }
        dev.axes_ = cache_axes(ev);
        dev.rumble_ = reports_rumble(ev);
    }
    dev.ev_ = std::move(ev);
    return dev;
}

Ex<InputDevice> InputDevice::adopt(int fd, std::string_view devnode, const DeviceIdentity& id,
                                   bool mouse) {
    if (fd < 0) return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    InputDevice dev;
    dev.ev_ = os::Evdev{fd};
    dev.devnode_ = devnode;
    dev.mouse_ = mouse;
    dev.id_ = id;
    return dev;
}

const AxisCal& InputDevice::axis(unsigned code) const {
    if (code >= kAxisCount) return kNullAxis;
    return axes_[code];
}

StickCal InputDevice::stick_cal(int stick) const noexcept {
    if (stick < 0 || stick > 1) return StickCal{};
    const auto i = static_cast<std::size_t>(stick);
    return StickCal{max_cardinal_[i], std::sqrt(max_range_sq_[i])};
}

void InputDevice::install_sys_map(const JoyWords& words) noexcept {
    sys_map_ = words;

    if (sys_map_[kSysBtnCntEsc] == 0) sys_map_[kSysBtnCntEsc] = sys_map_[kSysBtnCntOk];

    menu_map_ = rules::derive_menu_map(sys_map_, quirk_ != QuirkId::Wheel);
}

bool InputDevice::matches_sys_button(std::uint16_t code) const noexcept {
    if (code == 0) return false;
    for (std::size_t i = 0; i <= 11; ++i) {
        const std::uint32_t packed = sys_map_[i];
        if (packed == 0) continue;
        if ((packed & 0xFFFFu) == code) return true;
        if ((packed >> 16) == code) return true;
    }
    return false;
}

bool InputDevice::has_nonzero_joy_map() const noexcept {
    if (!joy_map_) return false;
    for (const std::uint32_t w : *joy_map_) {
        if (w != 0) return true;
    }
    return false;
}

Ex<void> InputDevice::set_grabbed(bool on) {
    if (!ev_.valid()) return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    if (!ev_.grab(on)) {
        return std::unexpected(os_error(ERR_SITE()));
    }
    grabbed_ = on;
    return {};
}

ff_effect make_rumble_effect(QuirkId quirk, std::int16_t id, std::uint16_t strong,
                             std::uint16_t weak) noexcept {
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
    if (!rumble_ || !ev_.valid()) return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    if (strong == 0 && weak == 0) {
        if (rumble_id_ == -1) return {};
        const bool removed = ev_.erase_effect(static_cast<int>(rumble_id_));
        rumble_id_ = -1;
        if (!removed) return std::unexpected(os_error(ERR_SITE()));
        return {};
    }
    ff_effect e = make_rumble_effect(quirk_, rumble_id_, strong, weak);
    if (!ev_.upload_effect(e)) return std::unexpected(os_error(ERR_SITE()));
    rumble_id_ = e.id;
    input_event play{};
    play.type = EV_FF;
    play.code = static_cast<std::uint16_t>(rumble_id_);
    play.value = 1;
    if (::write(ev_.fd(), &play, sizeof play) < 0) return std::unexpected(os_error(ERR_SITE()));
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
    if (word >= down_.size()) return;
    const std::uint64_t mask = std::uint64_t{1} << (code % 64u);
    if (on)
        down_[word] |= mask;
    else
        down_[word] &= ~mask;
}

namespace {

std::int32_t normalize_axis(std::int32_t value, const AxisCal& cal) {
    const std::int32_t hrange = (cal.max - cal.min) / 2;
    if (hrange == 0) return 0;
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

}  // namespace

Ex<unsigned> InputDevice::drain() {
    if (!ev_.valid()) return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});

    report_ = DeviceReport{};
    report_.buttons = joy_prev_;
    report_.menu_buttons = menu_prev_;
    report_.mouse_buttons = mouse_buttons_;

    report_.stick[0] = prev_stick_[0];
    report_.stick[1] = prev_stick_[1];
    unsigned decoded = 0;
    bool keys_changed = false;

    const auto push_key_edge = [&](std::uint16_t code, bool pressed) {
        mark_down(code, pressed);
        keys_changed = true;
        if (report_.key_count < kMaxKeyEdges) {
            report_.keys[report_.key_count] = KeyEdge{code, pressed};
            ++report_.key_count;
        } else {
            ++report_.key_overflow;
        }
    };

    const auto track_osd_combo = [&](std::uint16_t code, bool pressed) {
        if (code == 0) return;
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
        if (code < 256 || was == osd_combo_) return;
        if (was != 3u && osd_combo_ == 3u) {
            if (report_.osd_makes != 0xFFu) ++report_.osd_makes;
        } else if (was == 3u && osd_combo_ != 3u) {
            if (report_.osd_breaks != 0xFFu) ++report_.osd_breaks;
        }
    };

    const auto synth_axis = [&](std::uint16_t code, std::int32_t value, const AxisCal& cal) {
        if (code >= kAxisCount) return;

        if (is_pdsp_like(quirk_)) return;

        std::uint8_t edge = 0;
        if ((cal.max == 1 && cal.min == -1) || (cal.max == 2 && cal.min == 0)) {

            if (value == cal.min) edge = 1;
            if (value == cal.max) edge = 2;
        } else {

            const std::int32_t range = cal.max - cal.min + 1;
            const std::int32_t center = cal.min + (range / 2);
            const std::int32_t threshold = range / 4;

            bool only_max = true;
            for (unsigned nn = 0; nn < 4; ++nn) {
                const std::uint32_t w = sys_map_[24u + nn];
                if (w != 0 && (w & 0xFFFFu) == code) only_max = false;
            }
            if (value < center - threshold && !only_max) edge = 1;
            if (value > center + threshold) edge = 2;
        }

        const std::uint8_t last = axis_edge_[code];
        if (last == edge) return;
        axis_edge_[code] = edge;

        const auto base =
            static_cast<std::uint16_t>(kKeyEmuBase + (static_cast<unsigned>(code) << 1u) - 1u);
        if (last != 0) {
            push_key_edge(static_cast<std::uint16_t>(base + last), false);
            if (report_.axis_edges != 0xFFFFu) ++report_.axis_edges;
        }
        if (edge != 0) {
            push_key_edge(static_cast<std::uint16_t>(base + edge), true);
            if (report_.axis_edges != 0xFFFFu) ++report_.axis_edges;
        }
    };

    struct input_event evs[32];
    bool more = true;
    while (more) {
        const ssize_t n = ev_.read(evs);
        if (n < 0) {

            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            return std::unexpected(os_error(ERR_SITE()));
        }
        if (n == 0) break;
        const auto whole = static_cast<std::size_t>(n) / sizeof(struct input_event);

        const std::size_t rem = static_cast<std::size_t>(n) - whole * sizeof(struct input_event);
        if (rem != 0) {
            const std::uint32_t sum = static_cast<std::uint32_t>(report_.mouse_remainder) +
                                      static_cast<std::uint32_t>(rem);
            report_.mouse_remainder = static_cast<std::uint16_t>(sum > 0xFFFFu ? 0xFFFFu : sum);
        }
        if (whole < (sizeof evs / sizeof evs[0])) more = false;

        for (std::size_t i = 0; i < whole; ++i) {
            const struct input_event& ev = evs[i];
            ++decoded;

            if (quirk_ == QuirkId::Ds4Touch && ev.type == EV_KEY &&
                (ev.code == BTN_TOOL_FINGER || ev.code == BTN_TOUCH ||
                 ev.code == BTN_TOOL_DOUBLETAP)) {

                if (report_.quirk_drops != 0xFFFFu) ++report_.quirk_drops;
                continue;
            }
            if (ev.type == EV_ABS && is_dualshock_like(quirk_)) {

                if (ev.code > 40) {
                    if (report_.quirk_drops != 0xFFFFu) ++report_.quirk_drops;
                    continue;
                }

                if (quirk_ == QuirkId::Ds4Touch && ev.code <= 1) {
                    if (report_.quirk_drops != 0xFFFFu) ++report_.quirk_drops;
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
                            report_.mouse_buttons =
                                static_cast<std::uint8_t>(report_.mouse_buttons & ~bit);
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
                    if (card > max_cardinal_[stick]) max_cardinal_[stick] = card;

                    if (rules::is_diagonal(raw_[stick])) {
                        const auto rr = static_cast<float>(ax) * static_cast<float>(ax) +
                                        static_cast<float>(ay) * static_cast<float>(ay);
                        if (rr > max_range_sq_[stick]) max_range_sq_[stick] = rr;
                    }
                    if (ev.code < kAxisCount) {
                        const std::int32_t seen = is_y ? ay : ax;
                        if (seen > axes_[ev.code].seen_max) axes_[ev.code].seen_max = seen;
                    }

                    const StickCal sc = stick_cal(stick);
                    rules::DeadzoneParams dp{};
                    dp.deadzone = deadzone_;
                    dp.max_cardinal = sc.max_cardinal;
                    dp.max_range = sc.max_range;
                    AnalogXy xy = rules::apply_deadzone(raw_[stick], dp);

                    if (reshape_) xy = reshape_->reshape(xy, cal, sc);
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
    if (keys_changed && joy_map_) {

        const JoyMask now = rules::mask_for(*joy_map_, down_);
        report_.buttons = now;
        report_.buttons_changed = !(now == joy_prev_);
        joy_prev_ = now;
    }
    if (keys_changed) {
        const JoyMask menu_now = rules::mask_for(menu_map_, down_);
        report_.menu_buttons = menu_now;
        menu_prev_ = menu_now;
    }
    return decoded;
}

std::string_view PlayerSlots::key_of(const DeviceIdentity& id) noexcept {

    return id.merge_id.empty() ? std::string_view(id.id) : std::string_view(id.merge_id);
}

std::optional<PlayerIndex> PlayerSlots::slot_of(const DeviceIdentity& id) const {
    const std::string_view key = key_of(id);
    if (key.empty()) return std::nullopt;
    for (std::uint8_t i = 0; i < kMaxPlayers; ++i) {
        if (!bound_[i].empty() && bound_[i] == key) return PlayerIndex{i};
    }
    return std::nullopt;
}

Ex<void> PlayerSlots::assign(const DeviceIdentity& id, PlayerIndex player) {
    if (player.v >= kMaxPlayers) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), player.v});
    }

    const std::string_view key = key_of(id);
    for (std::uint8_t i = 0; i < kMaxPlayers; ++i) {
        if (i != player.v && !bound_[i].empty() && bound_[i] == key) bound_[i].clear();
    }
    bound_[player.v] = key;
    return {};
}

std::optional<PlayerIndex> PlayerSlots::first_free(std::uint32_t live_mask) const {
    for (std::uint8_t i = 0; i < kMaxPlayers; ++i) {
        if ((live_mask & (1u << i)) == 0u) return PlayerIndex{i};
    }
    return std::nullopt;
}

namespace {

std::uint32_t key_hash(std::string_view key) noexcept {
    if (key.empty()) return 0u;
    std::uint32_t h = 0x811c9dc5u;
    for (const char c : key) {
        h ^= static_cast<std::uint8_t>(c);
        h *= 0x01000193u;
    }
    return h == 0u ? 1u : h;
}
}  // namespace

void InputService::refresh_slot_census() noexcept {
    std::uint32_t bound = 0;
    std::uint32_t live = 0;
    for (const InputDevice& d : table_.devices) {
        if (const auto s = players_.slot_of(d.identity())) {
            ++bound;
            live |= 1u << s->v;
        }
    }
    n_slotted_.set(bound);
    n_live_mask_.set(live);

    std::uint32_t ghosts = 0;
    for (std::uint8_t i = 0; i < kMaxPlayers; ++i) {
        const std::uint32_t h = key_hash(players_.key_at(i));
        n_slot_hash_[i].set(h);
        if (h != 0u && (live & (1u << i)) == 0u) ++ghosts;
    }
    n_ghost_.set(ghosts);
}

InputService InputService::create() { return InputService(); }

void InputService::install(DeviceTable& incoming) noexcept {
    std::swap(table_, incoming);
    for (InputDevice& d : incoming.devices) {
        if (!d.release_for_handover()) n_grab_fail_.add(1);
    }
    for (InputDevice& d : table_.devices) {
        if (grabbed_ && !d.set_grabbed(true)) n_grab_fail_.add(1);
    }
    ++devices_gen_;
    for (JoyMask& m : joy_mask_) {
        m = JoyMask{};
    }
    n_mapped_.set(table_.mapped);
    refresh_slot_census();
    for (InputDevice& d : table_.devices) {
        if (d.drain()) note_report(d);
    }
}

void InputService::apply_deadzone_rules(std::span<const DeadzoneRule> rows) noexcept {
    for (InputDevice& d : table_.devices) {
        const DeviceIdentity& id = d.identity();
        d.set_deadzone(
            rules::deadzone_for(d.quirk(), id.vid, id.pid, id.id, d.sysfs(), id.merge_id, rows));
    }
}

Ex<void> InputService::open_hotplug_watch(std::string_view dir) {
    if (inotify_.valid()) return {};
    inotify_.reset(::inotify_init1(IN_NONBLOCK | IN_CLOEXEC));
    if (!inotify_.valid()) return std::unexpected(os_error(ERR_SITE()));
    const std::string path(dir);
    if (::inotify_add_watch(inotify_.get(), path.c_str(), IN_MODIFY | IN_CREATE | IN_DELETE) < 0) {
        const Error e = os_error(ERR_SITE());
        inotify_.reset();
        return std::unexpected(e);
    }
    return {};
}

std::uint8_t InputService::mouse_button_level() const noexcept {
    std::uint8_t level = 0;
    for (const InputDevice& d : table_.devices) {
        if (!d.is_mouse()) continue;
        level = static_cast<std::uint8_t>(level | d.report().mouse_buttons);
    }
    return level;
}

Ex<void> InputService::on_hotplug() {
    if (!inotify_.valid()) {
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    }

    alignas(struct inotify_event) char buf[4096];
    bool changed = false;
    while (true) {
        const ssize_t n = ::read(inotify_.get(), buf, sizeof buf);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            return std::unexpected(os_error(ERR_SITE()));
        }
        if (n == 0) break;
        std::size_t off = 0;
        while (off + sizeof(struct inotify_event) <= static_cast<std::size_t>(n)) {
            struct inotify_event ev {};
            std::memcpy(&ev, buf + off, sizeof ev);
            if (ev.len != 0 && (ev.mask & (IN_CREATE | IN_DELETE)) != 0) changed = true;
            off += sizeof(struct inotify_event) + ev.len;
        }
    }

    if (changed) hotplug_pending_ = true;
    return {};
}

Ex<unsigned> InputService::on_fd_ready(int fd) {
    if (fd < 0) return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    if (inotify_.valid() && fd == inotify_.get()) {
        if (auto r = on_hotplug(); !r) return std::unexpected(r.error());
        return 0u;
    }
    for (InputDevice& d : table_.devices) {
        if (d.fd() != fd) continue;
        auto n = d.drain();
        if (!n) return std::unexpected(n.error());
        note_report(d);
        return *n;
    }
    return std::unexpected(Error{Errc::not_found, ERR_SITE(), static_cast<std::uint32_t>(fd)});
}

void InputService::note_report(InputDevice& dev) {
    const DeviceReport& r = dev.report();

    if (r.key_overflow != 0) n_key_overflow_.add(r.key_overflow);
    if (r.mouse_remainder != 0) n_mouse_rem_.add(r.mouse_remainder);
    if (r.axis_edges != 0) n_axis_edges_.add(r.axis_edges);
    if (r.quirk_drops != 0) n_quirk_drops_.add(r.quirk_drops);

    maybe_assign_slot(dev);

    if (r.buttons_changed) recompute_joy_masks();
}

void InputService::maybe_assign_slot(InputDevice& dev) {

    if (dev.is_mouse()) return;
    if (players_.slot_of(dev.identity())) return;
    const DeviceReport& r = dev.report();
    bool claim = false;
    for (std::uint8_t i = 0; i < r.key_count && !claim; ++i) {
        const KeyEdge& e = r.keys[i];
        if (!e.pressed || e.code < 256) continue;
        claim = dev.matches_sys_button(e.code);
    }
    if (!claim) return;

    std::uint32_t live = 0;
    for (const InputDevice& d : table_.devices) {
        if (const auto s = players_.slot_of(d.identity())) live |= 1u << s->v;
    }
    const auto slot = players_.first_free(live);
    if (!slot) return;
    if (!players_.assign(dev.identity(), *slot)) return;
    refresh_slot_census();
}

void InputService::recompute_joy_masks() {
    n_mask_recompute_.add(1);
    for (JoyMask& m : joy_mask_)
        m = JoyMask{};
    for (const InputDevice& d : table_.devices) {

        if (d.is_mouse()) continue;
        const auto slot = players_.slot_of(d.identity());
        if (!slot) continue;
        joy_mask_[slot->v].v |= d.report().buttons.v;
    }
}

RumblePadSet InputService::rumble_pads() const noexcept {
    TASTY_SEAT_BODY(InputService);
    RumblePadSet out{};
    if (!cfg_rumble_) return out;
    for (const InputDevice& d : table_.devices) {
        if (!d.has_rumble() || d.is_mouse()) continue;
        const auto s = players_.slot_of(d.identity());
        if (!s) continue;
        out.players = static_cast<std::uint8_t>(out.players | (1u << s->v));

        std::uint32_t h = out.pads[s->v] != 0 ? out.pads[s->v] : 2166136261u;
        for (const char c : PlayerSlots::key_of(d.identity())) {
            h ^= static_cast<std::uint8_t>(c);
            h *= 16777619u;
        }
        h ^= 0xFFu;
        h *= 16777619u;
        out.pads[s->v] = h != 0 ? h : 1u;
    }
    return out;
}

RumbleApply InputService::rumble(PlayerIndex player, std::uint16_t strong,
                                 std::uint16_t weak) noexcept {
    TASTY_SEAT_BODY(InputService);
    if (!cfg_rumble_) return RumbleApply::NoDevice;
    RumbleApply out = RumbleApply::NoDevice;
    for (InputDevice& d : table_.devices) {
        if (!d.has_rumble() || d.is_mouse()) continue;
        const auto s = players_.slot_of(d.identity());
        if (!s || s->v != player.v) continue;
        if (!d.set_rumble(strong, weak)) {
            out = RumbleApply::Failed;
        } else if (out == RumbleApply::NoDevice) {
            out = RumbleApply::Applied;
        }
    }
    return out;
}

JoyMask InputService::joy_mask(PlayerIndex player) const {
    if (player.v >= kMaxPlayers) return JoyMask{};
    return joy_mask_[player.v];
}

Ex<void> InputService::set_grabbed(bool on) {
    grabbed_ = on;
    Ex<void> first{};
    for (InputDevice& d : table_.devices) {

        if (auto r = d.set_grabbed(grabbed_); !r && first) {
            first = std::unexpected(r.error());
        }
    }
    return first;
}

Ex<void> InputService::adopt(InputDevice&& dev) {
    if (table_.devices.size() >= kMaxDevices) {
        return std::unexpected(
            Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(table_.devices.size())});
    }
    table_.devices.push_back(std::move(dev));
    ++devices_gen_;
    return {};
}

Ex<void> MappingWizard::begin(const DeviceIdentity& id, MapKind kind) {
    device_ = id;
    kind_ = kind;
    captured_ = std::nullopt;

    step_ = id.id.empty() ? Step::AwaitDevice : Step::AwaitButton;
    return {};
}

void MappingWizard::on_device(const DeviceIdentity& id) {
    if (step_ != Step::AwaitDevice) return;
    device_ = id;
    step_ = Step::AwaitButton;
}

void MappingWizard::on_button(std::uint16_t code) {
    if (step_ != Step::AwaitButton || code == 0) return;
    captured_ = code;
}

Ex<MappingWizard::Step> MappingWizard::advance() {

    switch (step_) {
        case Step::Idle:
            return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
        case Step::AwaitDevice:
            break;
        case Step::AwaitButton:
            if (captured_) step_ = Step::Confirm;
            break;
        case Step::Confirm:
            step_ = Step::Persist;
            break;
        case Step::Persist:

            step_ = Step::Done;
            break;
        case Step::Done:
        case Step::Cancelled:
            break;
    }
    return step_;
}

void MappingWizard::cancel() { step_ = Step::Cancelled; }

[[nodiscard]] Ex<void> emit_but_sw(hal::ISpiTransport& link, std::uint16_t map) {
    hal::Selected cs(link, hal::ChipSelect::Io);

    if (auto r = link.transfer(hal::SpiWord{kUioButSw}); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = link.transfer(hal::SpiWord{map}); !r) {
        return std::unexpected(r.error());
    }
    return {};
}

}  // namespace mister::svc
