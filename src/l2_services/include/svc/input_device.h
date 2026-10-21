// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <linux/input.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "os/evdev.h"
#include "svc/analog_xy.h"
#include "svc/axis_cal.h"
#include "svc/button_map.h"
#include "svc/device_facts.h"
#include "svc/device_identity.h"
#include "svc/device_report.h"
#include "svc/quirk_id.h"
#include "svc/stick_cal.h"
#include "svc/types.h"

namespace mister::svc {

struct ButtonMap;

class IAnalogReshape;

inline constexpr unsigned kAxisCount = 64;

inline constexpr std::uint16_t kRumbleLengthMs = 0x7FFF;
using AxisTable = std::array<AxisCal, kAxisCount>;

inline constexpr std::size_t kDownWords = 14;
inline constexpr std::uint16_t kKeyEmuBase = 768;
static_assert(
    kDownWords * 64u == 896u && 896u > kKeyEmuBase + 2u * (kAxisCount - 1u) + 1u,
    "down_ must hold every KEY_EMU synthetic axis code "
    "(input.cpp:6113): 768 + 2*63 + 1 = 895"
);

[[nodiscard]] ff_effect make_rumble_effect(
    QuirkId quirk,
    std::int16_t id,
    std::uint16_t strong,
    std::uint16_t weak
) noexcept;

class InputDevice {
    TASTY_SEAT_EXEMPT(component);

public:
    [[nodiscard]] static Ex<InputDevice> open(std::string_view devnode);

    [[nodiscard]] static Ex<InputDevice> adopt(
        int fd,
        std::string_view devnode,
        const DeviceIdentity& id,
        bool mouse
    );

    int fd() const noexcept { return ev_.fd(); }

    const std::string& devnode() const noexcept { return facts_.devname; }

    bool is_mouse() const noexcept { return facts_.mouse; }

    const std::string& uniq() const noexcept { return facts_.uniq; }

    void set_uniq(std::string_view u) { facts_.uniq = u; }

    const std::string& mac() const noexcept { return facts_.mac; }

    const std::string& sysfs() const noexcept { return facts_.sysfs; }

    DeviceFacts& facts() noexcept { return facts_; }

    const DeviceFacts& facts() const noexcept { return facts_; }

    const DeviceIdentity& identity() const noexcept { return facts_.id; }

    DeviceIdentity& identity() noexcept { return facts_.id; }

    QuirkId quirk() const noexcept { return facts_.quirk; }

    void set_quirk(QuirkId q) noexcept { facts_.quirk = q; }

    bool mod() const noexcept { return mod_; }

    void set_mod(bool on) noexcept { mod_ = on; }

    bool grabbed() const noexcept { return grabbed_; }

    const AxisCal& axis(unsigned code) const;

    void install_joy_map(const JoyWords& words) noexcept { joy_map_ = words; }

    bool has_joy_map() const noexcept { return joy_map_.has_value(); }

    std::span<const std::uint32_t> joy_map() const noexcept {
        return joy_map_ ? std::span<const std::uint32_t>{*joy_map_}
                        : std::span<const std::uint32_t>{kUnsetJoyMap};
    }

    void install_sys_map(const JoyWords& words) noexcept;

    std::span<const std::uint32_t> sys_map() const noexcept { return sys_map_; }

    std::span<const std::uint32_t> menu_map() const noexcept { return menu_map_; }

    bool matches_sys_button(std::uint16_t code) const noexcept;
    bool has_nonzero_joy_map() const noexcept;

    void set_deadzone(std::int32_t dz) noexcept { deadzone_ = dz; }

    std::int32_t deadzone() const noexcept { return deadzone_; }

    StickCal stick_cal(int stick) const noexcept;

    void set_reshape(infra::OptRef<const IAnalogReshape> r) noexcept { reshape_ = r; }

    infra::OptRef<const IAnalogReshape> reshape() const noexcept { return reshape_; }

    [[nodiscard]] Ex<unsigned> drain();

    const DeviceReport& report() const noexcept { return report_; }

    [[nodiscard]] Ex<void> set_grabbed(bool on);

    bool has_rumble() const noexcept { return rumble_; }

    [[nodiscard]] Ex<void> set_rumble(std::uint16_t strong, std::uint16_t weak);

    [[nodiscard]] bool release_for_handover() noexcept;

    InputDevice(InputDevice&&) noexcept = default;
    InputDevice& operator=(InputDevice&&) noexcept = default;
    InputDevice(const InputDevice&) = delete;
    InputDevice& operator=(const InputDevice&) = delete;

private:
    friend struct InputDeviceTestAccess;

    void replace_axes(const AxisTable& table) noexcept { axes_ = table; }

    void mark_rumble(bool on) noexcept { rumble_ = on; }

    void mark_rumble_id(std::int16_t id) noexcept { rumble_id_ = id; }

    void mark_grabbed(bool on) noexcept { grabbed_ = on; }

    std::int16_t rumble_id() const noexcept { return rumble_id_; }

    InputDevice() = default;
    void mark_down(std::uint16_t code, bool on) noexcept;

    void push_key_edge(std::uint16_t code, bool pressed) noexcept;
    void track_osd_combo(std::uint16_t code, bool pressed) noexcept;
    void synth_axis(std::uint16_t code, std::int32_t value, const AxisCal& cal) noexcept;
    os::Evdev ev_{};
    DeviceFacts facts_{};
    bool mod_ = false;
    bool grabbed_ = false;
    bool rumble_ = false;
    std::int16_t rumble_id_ = -1;
    std::int32_t deadzone_ = 0;
    infra::OptRef<const IAnalogReshape> reshape_;
    std::uint8_t mouse_buttons_ = 0;
    std::uint8_t osd_combo_ = 0;
    bool keys_changed_ = false;

    static constexpr std::array<std::uint32_t, 32> kUnsetJoyMap{};
    std::optional<std::array<std::uint32_t, 32>> joy_map_{};

    static_assert(std::is_trivially_copyable_v<decltype(joy_map_)>);
    std::array<std::uint32_t, 32> sys_map_{};
    std::array<std::uint32_t, 32> menu_map_{};
    AxisTable axes_{};
    std::array<std::uint64_t, kDownWords> down_{};
    std::array<std::uint8_t, kAxisCount> axis_edge_{};
    AnalogXy raw_[2]{};
    AnalogXy prev_stick_[2]{};
    float max_cardinal_[2]{0.0f, 0.0f};
    float max_range_sq_[2]{0.0f, 0.0f};
    JoyMask joy_prev_{};
    JoyMask menu_prev_{};
    DeviceReport report_{};
};

}
