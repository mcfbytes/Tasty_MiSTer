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
#include "infra/unique_fd.h"
#include "proto/joystick.h"
#include "svc/device_match.h"
#include "svc/input_device.h"
#include "svc/movable_counter.h"
#include "svc/deadzone_rule.h"
#include "svc/device_table.h"
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

    [[nodiscard]] Ex<void> on_hotplug();

    bool hotplug_pending() const noexcept { return hotplug_pending_; }
    bool take_hotplug_pending() noexcept {
        const bool p = hotplug_pending_;
        hotplug_pending_ = false;
        return p;
    }

    std::span<InputDevice> devices() noexcept { return table_.devices; }
    std::span<const InputDevice> devices() const noexcept { return table_.devices; }

    JoyMask joy_mask(PlayerIndex player) const;

    PlayerSlots& players() noexcept { return players_; }

    [[nodiscard]] Ex<unsigned> on_fd_ready(int fd);

    std::uint32_t devices_generation() const noexcept { return devices_gen_; }

    std::uint8_t mouse_button_level() const noexcept;

    [[nodiscard]] Ex<void> open_hotplug_watch(std::string_view dir);

    [[nodiscard]] Ex<void> set_grabbed(bool on);
    bool grabbed() const noexcept { return grabbed_; }

    [[nodiscard]] Ex<void> adopt(InputDevice&& dev);

    int hotplug_fd() const noexcept { return inotify_.get(); }

    void set_cfg_rumble(bool on) noexcept { cfg_rumble_ = on; }

    [[nodiscard]] RumblePadSet rumble_pads() const noexcept override;
    [[nodiscard]] RumbleApply rumble(PlayerIndex player, std::uint16_t strong,
                                     std::uint16_t weak) noexcept override;

    std::uint32_t mapped_devices() const noexcept { return n_mapped_.get(); }
    std::uint32_t slotted_devices() const noexcept { return n_slotted_.get(); }
    std::uint32_t mask_recomputes() const noexcept { return n_mask_recompute_.get(); }
    std::uint32_t axis_edges() const noexcept { return n_axis_edges_.get(); }
    std::uint32_t mouse_remainder_bytes() const noexcept { return n_mouse_rem_.get(); }
    std::uint32_t key_overflows() const noexcept { return n_key_overflow_.get(); }
    std::uint32_t quirk_drops() const noexcept { return n_quirk_drops_.get(); }

    std::uint32_t grab_failures() const noexcept { return n_grab_fail_.get(); }

    std::uint32_t live_slot_mask() const noexcept { return n_live_mask_.get(); }
    std::uint32_t slot_key_hash(std::uint8_t player) const noexcept {
        return player < kMaxPlayers ? n_slot_hash_[player].get() : 0u;
    }
    std::uint32_t ghost_slots() const noexcept { return n_ghost_.get(); }

private:
    InputService() = default;

    void note_report(InputDevice& dev);
    void recompute_joy_masks();

    void maybe_assign_slot(InputDevice& dev);

    void refresh_slot_census() noexcept;

    DeviceTable table_{};
    PlayerSlots players_;
    UniqueFd inotify_{};

    bool grabbed_ = kBornGrabbed;
    bool cfg_rumble_ = true;
    bool hotplug_pending_ = false;
    std::uint32_t devices_gen_ = 0;
    JoyMask joy_mask_[kMaxPlayers]{};
    MovableCounter n_mapped_{};
    MovableCounter n_slotted_{};
    MovableCounter n_mask_recompute_{};
    MovableCounter n_axis_edges_{};
    MovableCounter n_mouse_rem_{};
    MovableCounter n_key_overflow_{};
    MovableCounter n_quirk_drops_{};
    MovableCounter n_grab_fail_{};
    MovableCounter n_live_mask_{};
    MovableCounter n_slot_hash_[kMaxPlayers]{};
    MovableCounter n_ghost_{};
};

inline constexpr std::uint8_t kUioButSw = 0x01;
[[nodiscard]] Ex<void> emit_but_sw(hal::ISpiTransport& link, std::uint16_t map);

}  // namespace mister::svc
