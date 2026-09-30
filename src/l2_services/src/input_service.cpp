// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/input_service.h"
#include "hal/selected.h"
#include "svc/analog_reshape.h"
#include "svc/button_map.h"
#include "svc/mapping_wizard.h"
#include "svc/vfs.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
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

constexpr std::string_view kProcDevices = "/proc/bus/input/devices";

constexpr std::size_t kSysBtnCntOk = 21;
constexpr std::size_t kSysBtnCntEsc = 22;

const AxisCal kNullAxis{};

Error os_error(std::uint16_t site) {
    return Error{Errc::os, site, static_cast<std::uint32_t>(errno)};
}

Ex<std::string> read_all(std::string_view path) {
    std::string name(path);
    UniqueFd fd(::open(name.c_str(), O_RDONLY | O_CLOEXEC));
    if (!fd.valid()) return std::unexpected(os_error(ERR_SITE()));
    std::string out;
    char buf[4096];
    while (true) {
        const ssize_t n = ::read(fd.get(), buf, sizeof buf);
        if (n < 0) {
            if (errno == EINTR) continue;
            return std::unexpected(os_error(ERR_SITE()));
        }
        if (n == 0) break;
        out.append(buf, static_cast<std::size_t>(n));
    }
    return out;
}

constexpr std::size_t kJoyMapBytes = map_spec(MapKind::Joystick).bytes;

}  // namespace

std::string MapStore::filename(std::string_view core, const DeviceIdentity& id, MapKind kind,
                               bool mod) {
    return rules::map_filename(core, id, kind, mod);
}

Ex<ButtonMap> MapStore::load(std::string_view core, const DeviceIdentity& id, MapKind kind,
                             bool mod) const {
    if (vfs_ == nullptr) {
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
    if (vfs_ == nullptr) {
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

void cache_axes(int fd, std::array<AxisCal, kAxisCount>& axes) {
    for (unsigned code = 0; code < kAxisCount; ++code) {
        struct input_absinfo info {};
        if (::ioctl(fd, EVIOCGABS(code), &info) < 0) continue;
        AxisCal cal{};
        cal.min = info.minimum;
        cal.max = info.maximum;
        cal.flat = info.flat;
        cal.fuzz = info.fuzz;
        axes[code] = cal;
    }
}

}  // namespace

Ex<InputDevice> InputDevice::open(std::string_view devnode) {
    if (devnode.empty()) {
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    }
    std::string path(devnode);

    UniqueFd fd(::open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC));
    if (!fd.valid()) return std::unexpected(os_error(ERR_SITE()));

    InputDevice dev;
    dev.devnode_ = path;
    dev.mouse_ = rules::is_mouse_node(path);
    if (!dev.mouse_) {
        struct input_id id {};
        if (::ioctl(fd.get(), EVIOCGID, &id) >= 0) {
            dev.id_.vid = Vid{id.vendor};
            dev.id_.pid = Pid{id.product};
            dev.id_.version = id.version;
            dev.id_.bustype = id.bustype;
        }
        char name[128] = {};
        if (::ioctl(fd.get(), EVIOCGNAME(sizeof name), name) >= 0) {
            name[sizeof name - 1] = 0;
            dev.id_.name = name;
        }

        char uniq[32] = {};
        if (::ioctl(fd.get(), EVIOCGUNIQ(sizeof uniq), uniq) >= 0) {
            uniq[sizeof uniq - 1] = 0;
            dev.uniq_ = uniq;
        }
        cache_axes(fd.get(), dev.axes_);
    }
    dev.fd_ = std::move(fd);
    return dev;
}

Ex<InputDevice> InputDevice::adopt(int fd, std::string_view devnode, const DeviceIdentity& id,
                                   bool mouse) {
    if (fd < 0) return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    InputDevice dev;
    dev.fd_.reset(fd);
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

void InputDevice::set_axis(unsigned code, const AxisCal& cal) {
    if (code >= kAxisCount) return;
    axes_[code] = cal;
}

Ex<void> InputDevice::set_joy_map(const ButtonMap& map) {
    if (map.kind != MapKind::Joystick || map.blob.size() != kJoyMapBytes) {
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(map.blob.size())});
    }

    for (std::size_t i = 0; i < joy_map_.size(); ++i) {
        std::uint32_t v = 0;
        for (std::size_t b = 0; b < 4; ++b) {
            v |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(map.blob[i * 4 + b]))
                 << (8u * b);
        }
        joy_map_[i] = v;
    }
    has_map_ = true;
    return {};
}

Ex<void> InputDevice::set_sys_map(const ButtonMap& map) {
    if (map.kind != MapKind::Joystick || map.blob.size() != kJoyMapBytes) {
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(map.blob.size())});
    }
    for (std::size_t i = 0; i < sys_map_.size(); ++i) {
        std::uint32_t v = 0;
        for (std::size_t b = 0; b < 4; ++b) {
            v |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(map.blob[i * 4 + b]))
                 << (8u * b);
        }
        sys_map_[i] = v;
    }

    if (sys_map_[kSysBtnCntEsc] == 0) sys_map_[kSysBtnCntEsc] = sys_map_[kSysBtnCntOk];

    menu_map_ = rules::derive_menu_map(sys_map_, quirk_ != QuirkId::Wheel);
    return {};
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
    for (const std::uint32_t w : joy_map_) {
        if (w != 0) return true;
    }
    return false;
}

Ex<void> InputDevice::set_grabbed(bool on) {
    if (!fd_.valid()) return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    if (::ioctl(fd_.get(), EVIOCGRAB, on ? 1 : 0) < 0) {
        return std::unexpected(os_error(ERR_SITE()));
    }
    grabbed_ = on;
    return {};
}

namespace {

void bit_set(std::array<std::uint64_t, kDownWords>& bits, std::uint16_t code, bool on) {
    const std::size_t word = code / 64u;
    if (word >= bits.size()) return;
    const std::uint64_t mask = std::uint64_t{1} << (code % 64u);
    if (on)
        bits[word] |= mask;
    else
        bits[word] &= ~mask;
}

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

int stick_of(std::uint16_t code, bool& is_y) {
    switch (code) {
        case ABS_X:
            is_y = false;
            return 0;
        case ABS_Y:
            is_y = true;
            return 0;
        case ABS_RX:
            is_y = false;
            return 1;
        case ABS_RY:
            is_y = true;
            return 1;
        default:
            return -1;
    }
}

}  // namespace

Ex<unsigned> InputDevice::drain() {
    if (!fd_.valid()) return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});

    report_ = DeviceReport{};
    report_.buttons = joy_prev_;
    report_.menu_buttons = menu_prev_;
    report_.mouse_buttons = mouse_buttons_;

    report_.stick[0] = prev_stick_[0];
    report_.stick[1] = prev_stick_[1];
    unsigned decoded = 0;
    bool keys_changed = false;

    const auto push_key_edge = [&](std::uint16_t code, bool pressed) {
        bit_set(down_, code, pressed);
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
        const ssize_t n = ::read(fd_.get(), evs, sizeof evs);
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
                    bool is_y = false;
                    const int stick = stick_of(static_cast<std::uint16_t>(ev.code), is_y);

                    const AxisCal& cal = axis(ev.code);
                    if (stick < 0) {
                        synth_axis(static_cast<std::uint16_t>(ev.code), ev.value, cal);
                        break;
                    }
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

                    if (reshape_ != nullptr) xy = reshape_->reshape(xy, cal, sc);
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
    if (keys_changed && has_map_) {

        const JoyMask now = rules::mask_for(joy_map_, down_);
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
    for (const InputDevice& d : devices_) {
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

Ex<InputService> InputService::create() { return InputService(); }

Ex<void> InputService::open_hotplug_watch() {
    if (inotify_.valid()) return {};
    inotify_.reset(::inotify_init1(IN_NONBLOCK | IN_CLOEXEC));
    if (!inotify_.valid()) return std::unexpected(os_error(ERR_SITE()));
    if (::inotify_add_watch(inotify_.get(), input_dir_.c_str(), IN_MODIFY | IN_CREATE | IN_DELETE) <
        0) {
        const Error e = os_error(ERR_SITE());
        inotify_.reset();
        return std::unexpected(e);
    }
    return {};
}

std::uint8_t InputService::mouse_button_level() const noexcept {
    std::uint8_t level = 0;
    for (const InputDevice& d : devices_) {
        if (!d.is_mouse()) continue;
        level = static_cast<std::uint8_t>(level | d.report().mouse_buttons);
    }
    return level;
}

Ex<void> InputService::enumerate() {

    ++devices_gen_;
    devices_.clear();

    for (JoyMask& m : joy_mask_)
        m = JoyMask{};
    n_mapped_.set(0);
    refresh_slot_census();
    stats_.overflow = 0;
    stats_.empty_id_skips = 0;
    stats_.mouse_fixups = 0;

    stats_.rejected = 0;

    DIR* dir = ::opendir(input_dir_.c_str());
    if (dir == nullptr) return std::unexpected(os_error(ERR_SITE()));

    unsigned seen = 0;
    while (const struct dirent* de = ::readdir(dir)) {
        const std::string_view name(de->d_name);
        if (!name.starts_with("event") && !name.starts_with("mouse")) continue;
        ++seen;
        if (devices_.size() >= kMaxDevices) {

            ++stats_.overflow;
            continue;
        }
        std::string path(input_dir_);
        path.push_back('/');
        path.append(name);
        auto dev = InputDevice::open(path);
        if (!dev) continue;
        (void)admit_and_adopt(std::move(*dev));
    }
    ::closedir(dir);

    if (auto r = resolve_identities(); !r) return r;

    if (stats_.overflow != 0) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), seen});
    }
    return {};
}

bool InputService::admit_and_adopt(InputDevice&& dev) {
    const Admission adm =
        admit_node(NodeFacts{dev.identity().vid, dev.identity().pid, dev.identity().name});
    if (!adm.accept) {
        ++stats_.rejected;
        return false;
    }
    dev.set_quirk(adm.quirk);
    devices_.push_back(std::move(dev));
    return true;
}

Ex<void> InputService::resolve_identities() {

    std::vector<rules::DeviceSlot> slots(devices_.size());
    for (std::size_t i = 0; i < devices_.size(); ++i) {
        slots[i].devname = devices_[i].devnode();
        slots[i].mouse = devices_[i].is_mouse();
        slots[i].uniq = devices_[i].uniq();
        slots[i].id = devices_[i].identity();

        slots[i].quirk = devices_[i].quirk();
        rules::derive_idstr(slots[i]);
    }

    std::string proc;
    if (auto text = read_all(kProcDevices); text) {
        proc = std::move(*text);
    }

    const rules::SpinnerPolicy spin{spinner_vid_, spinner_pid_};
    const rules::MergeStats st = rules::resolve_identities(slots, proc, cfg_no_merge_, spin);
    const rules::UniquePolicy unique{false, false, unique_rows_};
    stats_.mouse_fixups = st.mouse_fixups;
    stats_.empty_id_skips = st.empty_id_skips;

    for (std::size_t i = 0; i < devices_.size(); ++i) {
        devices_[i].identity() = slots[i].id;
        devices_[i].identity().unique_filenames = rules::wants_unique_mapping(slots[i].id, unique);
        devices_[i].set_quirk(slots[i].quirk);
        devices_[i].set_bt_facts(slots[i].mac, slots[i].sysfs);
    }
    stats_.devices = static_cast<unsigned>(devices_.size());
    apply_deadzones();
    return {};
}

void InputService::apply_deadzones() noexcept {
    for (InputDevice& d : devices_) {
        const DeviceIdentity& id = d.identity();
        d.set_deadzone(rules::deadzone_for(d.quirk(), id.vid, id.pid, id.id, d.sysfs(), id.merge_id,
                                           cfg_deadzone_));
    }
}

namespace {

bool is_psx_core(std::string_view core) noexcept {
    if (core.size() != 3) return false;
    static constexpr char kPsx[3] = {'P', 'S', 'X'};
    for (std::size_t i = 0; i < 3; ++i) {
        char c = core[i];
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - ('a' - 'A'));
        if (c != kPsx[i]) return false;
    }
    return true;
}
}  // namespace

Ex<void> InputService::load_maps_for(std::string_view core_in, bool front_end) {

    const std::string_view core = front_end ? std::string_view{} : core_in;

    const bool axis_emu = !is_psx_core(core_in);

    const ButtonMap fallback_sys = rules::default_sys_map();
    ButtonMap zero_map;
    zero_map.kind = MapKind::Joystick;
    zero_map.blob.assign(kJoyMapBytes, std::byte{0});

    std::uint32_t mapped = 0;
    for (InputDevice& d : devices_) {

        const ButtonMap* sys = &fallback_sys;
        ButtonMap sys_file;
        if (auto m = maps_.load(std::string_view{}, d.identity(), MapKind::Joystick, d.mod()); m) {
            sys_file = std::move(*m);
            sys = &sys_file;
        }
        (void)d.set_sys_map(*sys);

        d.set_reshape(reshape_);

        const ButtonMap* wire = sys;
        ButtonMap core_file;
        ButtonMap derived;
        if (!core.empty()) {
            if (auto m = maps_.load(core, d.identity(), MapKind::Joystick, d.mod()); m) {

                core_file = std::move(*m);
                wire = &core_file;
            } else if (is_pdsp_like(d.quirk())) {

                wire = &zero_map;
            } else {

                derived = rules::derive_joy_map(d.sys_map(), axis_emu, joy_plan_);
                wire = &derived;
            }
        }

        (void)d.set_joy_map(*wire);
        if (d.has_nonzero_joy_map()) ++mapped;
    }
    n_mapped_.set(mapped);

    refresh_slot_census();
    return {};
}

void InputService::set_cfg_deadzone_rules(std::span<const DeadzoneRule> rows) {
    cfg_deadzone_.assign(rows.begin(), rows.end());
    apply_deadzones();
}

void InputService::set_cfg_no_merge_rules(std::span<const NoMergeRule> rows) {
    cfg_no_merge_.assign(rows.begin(), rows.end());
}

void InputService::set_spinner(Vid vid, Pid pid) noexcept {
    spinner_vid_ = vid;
    spinner_pid_ = pid;
}

void InputService::set_unique_mapping(std::span<const std::uint32_t> rows) {
    unique_rows_.assign(rows.begin(), rows.end());
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
    for (InputDevice& d : devices_) {
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
    for (const InputDevice& d : devices_) {
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
    for (const InputDevice& d : devices_) {

        if (d.is_mouse()) continue;
        const auto slot = players_.slot_of(d.identity());
        if (!slot) continue;
        joy_mask_[slot->v].v |= d.report().buttons.v;
    }
}

JoyMask InputService::joy_mask(PlayerIndex player) const {
    if (player.v >= kMaxPlayers) return JoyMask{};
    return joy_mask_[player.v];
}

Ex<void> InputService::set_grabbed(bool on) {
    grabbed_ = on;
    Ex<void> first{};
    for (InputDevice& d : devices_) {

        if (auto r = d.set_grabbed(grabbed_); !r && first) {
            first = std::unexpected(r.error());
        }
    }
    return first;
}

Ex<void> InputService::adopt(InputDevice&& dev) {
    if (devices_.size() >= kMaxDevices) {
        return std::unexpected(
            Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(devices_.size())});
    }
    devices_.push_back(std::move(dev));
    stats_.devices = static_cast<unsigned>(devices_.size());
    ++devices_gen_;
    return {};
}

Ex<void> MappingWizard::begin(const DeviceIdentity& id, MapKind kind) {
    device_ = id;
    kind_ = kind;
    captured_ = 0;
    has_capture_ = false;

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
    has_capture_ = true;
}

Ex<MappingWizard::Step> MappingWizard::advance() {

    switch (step_) {
        case Step::Idle:
            return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
        case Step::AwaitDevice:
            break;
        case Step::AwaitButton:
            if (has_capture_) step_ = Step::Confirm;
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

Ex<void> emit_but_sw(hal::ISpiTransport& link, std::uint16_t map) {
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
