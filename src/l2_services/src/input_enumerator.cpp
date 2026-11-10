// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/input_enumerator.h"
#include "svc/admission_rule.h"
#include "svc/analog_reshape.h"
#include "svc/button_map.h"
#include "svc/vfs.h"

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <utility>

#include "input_rules.h"
#include "input_service_detail.h"

namespace mister::svc {

namespace {

constexpr std::string_view kProcDevices = "/proc/bus/input/devices";

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

}

InputEnumerator InputEnumerator::create(std::string_view input_dir) {
    return InputEnumerator(input_dir);
}

InputEnumerator InputEnumerator::create(const Vfs& storage, std::string_view input_dir) {
    return InputEnumerator(storage, input_dir);
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
        if (table_.full()) {

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
    if (!table_.add(std::move(dev))) {
        ++stats_.overflow;
        return false;
    }
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

    const rules::MergeStats st = rules::resolve_identities(slots, proc, cfg_no_merge_, spinner_);
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
        d.set_deadzone(rules::deadzone_for(d.facts(), cfg_deadzone_));
    }
}

Ex<void> InputEnumerator::load_maps_for(
    std::string_view core_in,
    bool front_end,
    infra::OptRef<const IAnalogReshape> reshape,
    const JoyPlan& joy
) {

    const std::string_view core = front_end ? std::string_view{} : core_in;

    const bool axis_emu = joy.stick_dpad;

    const SysWords fallback_sys = rules::default_sys_map();
    const WireWords zero_map{};

    std::uint32_t mapped = 0;
    for (InputDevice& d : table_.devices) {

        const SysWords* sys = &fallback_sys;
        SysWords sys_file{};
        if (auto m = maps_.load_joy_words(std::string_view{}, d.identity(), d.mod()); m) {
            sys_file = as_sys_words(*m);
            sys = &sys_file;
        }
        d.install_sys_map(*sys);

        d.set_reshape(reshape);

        WireWords sys_as_wire = as_wire_words(*sys);
        const WireWords* wire = &sys_as_wire;
        WireWords core_file{};
        WireWords derived{};
        if (!core.empty()) {
            if (auto m = maps_.load_joy_words(core, d.identity(), d.mod()); m) {

                core_file = *m;
                wire = &core_file;
            } else if (is_pdsp_like(d.quirk())) {

                wire = &zero_map;
            } else {

                derived = rules::derive_joy_map(d.sys_map(), axis_emu, joy);
                wire = &derived;
            }
        }

        d.install_joy_map(*wire);
        if (d.has_nonzero_joy_map())
            ++mapped;
    }
    table_.mapped = mapped;
    reshape_ = reshape;
    joy_plan_ = joy;
    return {};
}

Ex<void> InputEnumerator::adopt(InputDevice&& dev) {
    if (auto r = table_.add(std::move(dev)); !r)
        return r;
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

void InputEnumerator::set_spinner(SpinnerPolicy spinner) noexcept {
    spinner_ = spinner;
}

void InputEnumerator::set_unique_mapping(std::span<const VidPidKey> rows) {
    unique_rows_.assign(rows.begin(), rows.end());
}

}
