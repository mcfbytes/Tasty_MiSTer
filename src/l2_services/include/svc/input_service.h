// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "infra/error.h"
#include "infra/indexed_array.h"
#include "infra/opt_ref.h"
#include "proto/joystick_port.h"
#include "proto/user_io/rumble_motors.h"
#include "svc/device_match.h"
#include "svc/input_device.h"
#include "svc/deadzone_rule.h"
#include "svc/device_table.h"
#include "svc/hotplug_watch.h"
#include "svc/input_counters.h"
#include "svc/no_merge_rule.h"
#include "svc/player_slots.h"
#include "svc/rumble_out.h"
#include "svc/types.h"
#include "infra/seat.h"

namespace mister::svc {

inline constexpr std::uint32_t kFnvOffsetBasis = 0x811c9dc5u;
inline constexpr std::uint32_t kFnvPrime = 0x01000193u;
[[nodiscard]] std::uint32_t fnv1a_32(
    std::string_view bytes,
    std::uint32_t seed = kFnvOffsetBasis
) noexcept;

class InputService final : public IRumbleOut {
    TASTY_SEAT_RESIDENT(Input);

public:
    static constexpr bool kBornGrabbed = true;

    [[nodiscard]] static Ex<InputService> open(std::string_view input_dir);

    void install(DeviceTable& incoming) noexcept;

    void apply_deadzone_rules(std::span<const DeadzoneRule> rows) noexcept;

    HotplugWatch& hotplug() noexcept { return hotplug_; }

    const HotplugWatch& hotplug() const noexcept { return hotplug_; }

    std::span<InputDevice> devices() noexcept { return table_.devices; }

    std::span<const InputDevice> devices() const noexcept { return table_.devices; }

    JoyMask joy_mask(PlayerIndex player) const;

    PlayerSlots& players() noexcept { return players_; }

    [[nodiscard]] Ex<unsigned> on_fd_ready(int fd);

    Generation devices_generation() const noexcept { return devices_gen_; }

    proto::MouseButtons mouse_button_level() const noexcept;

    [[nodiscard]] Ex<void> set_grabbed(bool on);

    bool grabbed() const noexcept { return grabbed_; }

    [[nodiscard]] Ex<void> adopt(InputDevice&& dev);

    void set_cfg_rumble(bool on) noexcept { cfg_rumble_ = on; }

    [[nodiscard]] RumblePadSet rumble_pads() const noexcept override;
    [[nodiscard]] RumbleApply rumble(
        PlayerIndex player,
        proto::user_io::RumbleMotors motors
    ) noexcept override;

    const InputCounters& counters() const noexcept { return counters_; }

private:
    explicit InputService(HotplugWatch watch) noexcept : hotplug_(std::move(watch)) {}

    void note_report(InputDevice& dev);
    void recompute_joy_masks();

    void maybe_assign_slot(InputDevice& dev);

    void refresh_slot_census() noexcept;

    DeviceTable table_{};
    PlayerSlots players_;
    HotplugWatch hotplug_;

    bool grabbed_ = kBornGrabbed;
    bool cfg_rumble_ = true;
    Generation devices_gen_{};
    infra::IndexedArray<JoyMask, kMaxPlayers, PlayerIndex> joy_mask_{};
    InputCounters counters_{};
};

}
