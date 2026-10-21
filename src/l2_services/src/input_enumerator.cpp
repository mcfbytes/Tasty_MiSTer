// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/input_enumerator.h"
#include "svc/analog_reshape.h"
#include "svc/button_map.h"
#include "svc/input_service.h"
#include "svc/vfs.h"

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <utility>

#include "input_rules.h"

namespace mister::svc {

namespace {

constexpr std::string_view kProcDevices = "/proc/bus/input/devices";

Error os_error(std::uint16_t site) {
    return Error{Errc::os, site, static_cast<std::uint32_t>(errno)};
}

[[nodiscard]] Ex<std::string> read_all(std::string_view path) {
    std::string name(path);
    UniqueFd fd(::open(name.c_str(), O_RDONLY | O_CLOEXEC));
    if (!fd.valid())
        return std::unexpected(os_error(ERR_SITE()));
    std::string out;
    char buf[4096];
    while (true) {
        const ssize_t n = ::read(fd.get(), buf, sizeof buf);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return std::unexpected(os_error(ERR_SITE()));
        }
        if (n == 0)
            break;
        out.append(buf, static_cast<std::size_t>(n));
    }
    return out;
}

constexpr std::size_t kJoyMapBytes = map_spec(MapKind::Joystick).bytes;

bool is_psx_core(std::string_view core) noexcept {
    if (core.size() != 3)
        return false;
    static constexpr char kPsx[3] = {'P', 'S', 'X'};
    for (std::size_t i = 0; i < 3; ++i) {
        char c = core[i];
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - ('a' - 'A'));
        if (c != kPsx[i])
            return false;
    }
    return true;
}

}

InputEnumerator InputEnumerator::create() {
    return InputEnumerator();
}

InputEnumerator InputEnumerator::create(const Vfs& storage) {
    return InputEnumerator(storage);
}

DeviceTable InputEnumerator::take_table() noexcept {
    DeviceTable out = std::move(table_);
    table_ = DeviceTable{};
    return out;
}

Ex<void> InputEnumerator::enumerate() {

    table_ = DeviceTable{};
    stats_.overflow = 0;
    stats_.empty_id_skips = 0;
    stats_.mouse_fixups = 0;

    stats_.rejected = 0;
    stats_.devices = 0;

    DIR* dir = ::opendir(input_dir_.c_str());
    if (dir == nullptr)
        return std::unexpected(os_error(ERR_SITE()));

    unsigned seen = 0;
    while (const struct dirent* de = ::readdir(dir)) {
        const std::string_view name(de->d_name);
        if (!name.starts_with("event") && !name.starts_with("mouse"))
            continue;
        ++seen;
        if (table_.devices.size() >= kMaxDevices) {

            ++stats_.overflow;
            continue;
        }
        std::string path(input_dir_);
        path.push_back('/');
        path.append(name);
        auto dev = InputDevice::open(path);
        if (!dev)
            continue;
        (void)admit_and_adopt(std::move(*dev));
    }
    ::closedir(dir);

    if (auto r = resolve_identities(); !r)
        return r;

    if (stats_.overflow != 0) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), seen});
    }
    return {};
}

bool InputEnumerator::admit_and_adopt(InputDevice&& dev) {
    const Admission adm = admit_node(
        NodeFacts{dev.identity().vid, dev.identity().pid, dev.identity().name}
    );
    if (!adm.accept) {
        ++stats_.rejected;
        return false;
    }
    dev.set_quirk(adm.quirk);
    table_.devices.push_back(std::move(dev));
    return true;
}

Ex<void> InputEnumerator::resolve_identities() {

    std::vector<InputDevice>& devices = table_.devices;
    std::vector<DeviceFacts*> slots;
    slots.reserve(devices.size());
    for (InputDevice& d : devices) {

        rules::derive_idstr(d.facts());
        slots.push_back(&d.facts());
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

    for (InputDevice& d : devices) {
        d.identity().unique_filenames = rules::wants_unique_mapping(d.identity(), unique);
    }
    stats_.devices = static_cast<unsigned>(devices.size());
    apply_deadzones();
    return {};
}

void InputEnumerator::apply_deadzones() noexcept {
    for (InputDevice& d : table_.devices) {
        const DeviceIdentity& id = d.identity();
        d.set_deadzone(
            rules::deadzone_for(
                d.quirk(),
                id.vid,
                id.pid,
                id.id,
                d.sysfs(),
                id.merge_id,
                cfg_deadzone_
            )
        );
    }
}

Ex<void> InputEnumerator::load_maps_for(
    std::string_view core_in,
    bool front_end,
    infra::OptRef<const IAnalogReshape> reshape,
    const JoyPlan& joy
) {

    const std::string_view core = front_end ? std::string_view{} : core_in;

    const bool axis_emu = !is_psx_core(core_in);

    const ButtonMap fallback_sys = rules::default_sys_map();
    ButtonMap zero_map;
    zero_map.kind = MapKind::Joystick;
    zero_map.blob.assign(kJoyMapBytes, std::byte{0});

    std::uint32_t mapped = 0;
    for (InputDevice& d : table_.devices) {

        const ButtonMap* sys = &fallback_sys;
        ButtonMap sys_file;
        if (auto m = maps_.load(std::string_view{}, d.identity(), MapKind::Joystick, d.mod()); m) {
            sys_file = std::move(*m);
            sys = &sys_file;
        }
        if (const auto w = sys->words())
            d.install_sys_map(*w);

        d.set_reshape(reshape);

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

                derived = rules::derive_joy_map(d.sys_map(), axis_emu, joy);
                wire = &derived;
            }
        }

        if (const auto w = wire->words())
            d.install_joy_map(*w);
        if (d.has_nonzero_joy_map())
            ++mapped;
    }
    table_.mapped = mapped;
    reshape_ = reshape;
    joy_plan_ = joy;
    return {};
}

Ex<void> InputEnumerator::adopt(InputDevice&& dev) {
    if (table_.devices.size() >= kMaxDevices) {
        return std::unexpected(
            Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(table_.devices.size())}
        );
    }
    table_.devices.push_back(std::move(dev));
    stats_.devices = static_cast<unsigned>(table_.devices.size());
    return {};
}

void InputEnumerator::set_cfg_deadzone_rules(std::span<const DeadzoneRule> rows) {
    cfg_deadzone_.assign(rows.begin(), rows.end());
    apply_deadzones();
}

void InputEnumerator::set_cfg_no_merge_rules(std::span<const NoMergeRule> rows) {
    cfg_no_merge_.assign(rows.begin(), rows.end());
}

void InputEnumerator::set_spinner(Vid vid, Pid pid) noexcept {
    spinner_vid_ = vid;
    spinner_pid_ = pid;
}

void InputEnumerator::set_unique_mapping(std::span<const std::uint32_t> rows) {
    unique_rows_.assign(rows.begin(), rows.end());
}

}
