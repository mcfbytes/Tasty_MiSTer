// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "infra/error.h"
#include "infra/opt_ref.h"
#include "proto/joystick.h"
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

inline constexpr unsigned kMaxDevices = 32;

struct MouseFixupRule {
    DeviceMatch match;
    QuirkId quirk;
    const char* why;
};

std::optional<MouseFixupRule> find_mouse_fixup(Vid vid, Pid pid);

enum class NameMatch : std::uint8_t {
    Any = 0,
    Exact,
    Contains,
    ContainsCs,
};

enum class AdmitAction : std::uint8_t {
    Reject = 0,
    Assign,
};

struct AdmissionRule {
    Vid vid;
    Pid pid;
    const char* name;
    NameMatch name_match;
    AdmitAction action;
    QuirkId quirk;
    const char* why;
};

std::span<const AdmissionRule> admission_rules();

struct NodeFacts {
    Vid vid{};
    Pid pid{};
    std::string_view name{};
};

struct Admission {
    bool accept = true;
    QuirkId quirk = QuirkId::None;
};

Admission admit_node(const NodeFacts& f);

class InputService final : public IRumbleOut {
    TASTY_SEAT_RESIDENT(Input);

public:
    static constexpr bool kBornGrabbed = true;

    [[nodiscard]] static InputService create();

    void install(DeviceTable& incoming) noexcept;

    void apply_deadzone_rules(std::span<const DeadzoneRule> rows) noexcept;

    HotplugWatch& hotplug() noexcept { return hotplug_; }

    const HotplugWatch& hotplug() const noexcept { return hotplug_; }

    std::span<InputDevice> devices() noexcept { return table_.devices; }

    std::span<const InputDevice> devices() const noexcept { return table_.devices; }

    JoyMask joy_mask(PlayerIndex player) const;

    PlayerSlots& players() noexcept { return players_; }

    [[nodiscard]] Ex<unsigned> on_fd_ready(int fd);

    std::uint32_t devices_generation() const noexcept { return devices_gen_; }

    std::uint8_t mouse_button_level() const noexcept;

    [[nodiscard]] Ex<void> set_grabbed(bool on);

    bool grabbed() const noexcept { return grabbed_; }

    [[nodiscard]] Ex<void> adopt(InputDevice&& dev);

    void set_cfg_rumble(bool on) noexcept { cfg_rumble_ = on; }

    [[nodiscard]] RumblePadSet rumble_pads() const noexcept override;
    [[nodiscard]] RumbleApply rumble(
        PlayerIndex player,
        std::uint16_t strong,
        std::uint16_t weak
    ) noexcept override;

    const InputCounters& counters() const noexcept { return counters_; }

private:
    InputService() = default;

    void note_report(InputDevice& dev);
    void recompute_joy_masks();

    void maybe_assign_slot(InputDevice& dev);

    void refresh_slot_census() noexcept;

    DeviceTable table_{};
    PlayerSlots players_;
    HotplugWatch hotplug_{};

    bool grabbed_ = kBornGrabbed;
    bool cfg_rumble_ = true;
    std::uint32_t devices_gen_ = 0;
    JoyMask joy_mask_[kMaxPlayers]{};
    InputCounters counters_{};
};

inline constexpr std::uint8_t kUioButSw = 0x01;
[[nodiscard]] Ex<void> emit_but_sw(hal::ISpiTransport& link, std::uint16_t map);

}
