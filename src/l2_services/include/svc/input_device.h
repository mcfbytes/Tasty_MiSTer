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
#include "infra/indexed_array.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "os/evdev.h"
#include "proto/user_io/rumble_poll.h"
#include "svc/analog_xy.h"
#include "svc/axis_cal.h"
#include "svc/button_map.h"
#include "svc/device_facts.h"
#include "svc/device_identity.h"
#include "svc/device_report.h"
#include "svc/quirk_id.h"
#include "svc/stick_cal.h"
#include "svc/stick_state.h"
#include "svc/types.h"

namespace mister::svc {

struct ButtonMap;

class IAnalogReshape;

inline constexpr unsigned kAxisCount = 64;

inline constexpr std::uint16_t kRumbleLengthMs = 0x7FFF;
using AxisTable = std::array<AxisCal, kAxisCount>;

inline constexpr EvdevCode kKeyEmuBase{EvdevCode::kKeyEmuBase};

inline constexpr std::size_t kDownWords = 14;
static_assert(
    kDownWords * 64u == 896u && 896u > EvdevCode::kKeyEmuBase + 2u * (kAxisCount - 1u) + 1u,
    "down_ must hold every KEY_EMU synthetic axis code "
    "(input.cpp:6113): 768 + 2*63 + 1 = 895"
);

struct DeviceMaps {
    TASTY_SEAT_EXEMPT(component);
    std::optional<JoyWords> wire{};
    JoyWords sys{};
    JoyWords menu{};

    const JoyWords& install_sys(const JoyWords& words) noexcept;

    void set_menu(JoyWords m) noexcept { menu = m; }

    std::span<const PackedKeys> wire_view() const noexcept;

    bool wire_present() const noexcept { return wire.has_value(); }
};

static_assert(
    std::is_trivially_copyable_v<DeviceMaps>,
    "the map trio crosses the T-DIAG -> T-INPUT handover by value"
);

[[nodiscard]] ff_effect make_rumble_effect(
    QuirkId quirk,
    std::int16_t id,
    proto::user_io::RumbleMotors motors
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

    const AxisCal& axis(EvdevCode code) const;

    void install_joy_map(const JoyWords& words) noexcept { maps_.wire = words; }

    bool has_joy_map() const noexcept { return maps_.wire_present(); }

    std::span<const PackedKeys> joy_map() const noexcept { return maps_.wire_view(); }

    void install_sys_map(const JoyWords& words) noexcept;

    std::span<const PackedKeys> sys_map() const noexcept { return maps_.sys; }

    std::span<const PackedKeys> menu_map() const noexcept { return maps_.menu; }

    bool matches_sys_button(EvdevCode code) const noexcept;
    bool has_nonzero_joy_map() const noexcept;

    void set_deadzone(Deadzone dz) noexcept { deadzone_ = dz; }

    Deadzone deadzone() const noexcept { return deadzone_; }

    StickCal stick_cal(StickIndex stick) const noexcept;

    void set_reshape(infra::OptRef<const IAnalogReshape> r) noexcept { reshape_ = r; }

    infra::OptRef<const IAnalogReshape> reshape() const noexcept { return reshape_; }

    [[nodiscard]] Ex<unsigned> drain();

    const DeviceReport& report() const noexcept { return report_; }

    [[nodiscard]] Ex<void> set_grabbed(bool on);

    bool has_rumble() const noexcept { return rumble_; }

    [[nodiscard]] Ex<void> set_rumble(proto::user_io::RumbleMotors motors);

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
    void mark_down(EvdevCode code, bool on) noexcept;

    void push_key_edge(EvdevCode code, bool pressed) noexcept;
    void track_osd_combo(EvdevCode code, bool pressed) noexcept;
    void synth_axis(EvdevCode code, std::int32_t value, const AxisCal& cal) noexcept;

    void decode_key(const input_event& ev) noexcept;
    void decode_rel(const input_event& ev) noexcept;
    void decode_abs(const input_event& ev) noexcept;
    os::Evdev ev_{};
    DeviceFacts facts_{};
    bool mod_ = false;
    bool grabbed_ = false;
    bool rumble_ = false;
    std::int16_t rumble_id_ = -1;
    Deadzone deadzone_{};
    infra::OptRef<const IAnalogReshape> reshape_;
    proto::MouseButtons mouse_buttons_{};
    std::uint8_t osd_combo_ = 0;
    bool keys_changed_ = false;

    DeviceMaps maps_{};
    AxisTable axes_{};
    KeyDownSet<kDownWords> down_{};
    std::array<std::uint8_t, kAxisCount> axis_edge_{};
    infra::IndexedArray<StickState, kSticks.size(), StickIndex> sticks_{};
    JoyMask joy_prev_{};
    JoyMask menu_prev_{};
    DeviceReport report_{};
};

}
