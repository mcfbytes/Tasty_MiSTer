// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "infra/error.h"
#include "infra/unique_fd.h"
#include "proto/joystick.h"
#include "svc/device_match.h"
#include "svc/input_device.h"
#include "svc/joy_plan.h"
#include "svc/map_store.h"
#include "svc/movable_counter.h"
#include "svc/deadzone_rule.h"
#include "svc/no_merge_rule.h"
#include "svc/player_slots.h"
#include "svc/types.h"
#include "infra/seat.h"

namespace mister::svc {

class Vfs;
class IAnalogReshape;

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

class InputService {
    TASTY_SEAT_OPEN();

public:
    static Ex<InputService> create();

    Ex<void> enumerate();

    bool admit_and_adopt(InputDevice&& dev);
    Ex<void> resolve_identities();

    void apply_deadzones() noexcept;

    Ex<void> on_hotplug();

    bool hotplug_pending() const noexcept { return hotplug_pending_; }
    bool take_hotplug_pending() noexcept {
        const bool p = hotplug_pending_;
        hotplug_pending_ = false;
        return p;
    }

    std::span<InputDevice> devices() noexcept { return devices_; }

    JoyMask joy_mask(PlayerIndex player) const;

    PlayerSlots& players() noexcept { return players_; }
    const MapStore& maps() const noexcept { return maps_; }

    Ex<void> load_maps_for(std::string_view core, bool front_end);

    void bind_storage(const Vfs& vfs) noexcept { maps_.bind_storage(&vfs); }

    Ex<unsigned> on_fd_ready(int fd);

    std::uint32_t devices_generation() const noexcept { return devices_gen_; }

    std::uint8_t mouse_button_level() const noexcept;

    Ex<void> open_hotplug_watch();

    Ex<void> set_grabbed(bool on);
    bool grabbed() const noexcept { return grabbed_; }

    Ex<void> adopt(InputDevice&& dev);

    int hotplug_fd() const noexcept { return inotify_.get(); }

    void set_input_dir(std::string_view dir) { input_dir_ = dir; }
    const std::string& input_dir() const noexcept { return input_dir_; }

    void set_cfg_no_merge_rules(std::span<const NoMergeRule> rows);

    void set_cfg_deadzone_rules(std::span<const DeadzoneRule> rows);
    void set_spinner(Vid vid, Pid pid) noexcept;

    void set_joy_plan(const JoyPlan& plan) noexcept { joy_plan_ = plan; }
    const JoyPlan& joy_plan() const noexcept { return joy_plan_; }

    void set_analog_reshape(const IAnalogReshape* r) noexcept { reshape_ = r; }
    const IAnalogReshape* analog_reshape() const noexcept { return reshape_; }

    void set_unique_mapping(std::span<const std::uint32_t> rows);

    struct Stats {
        unsigned devices = 0;
        unsigned mouse_fixups = 0;
        unsigned empty_id_skips = 0;
        unsigned overflow = 0;
        unsigned axis_range_zero = 0;
        unsigned rejected = 0;
    };
    const Stats& stats() const noexcept { return stats_; }

    std::uint32_t mapped_devices() const noexcept { return n_mapped_.get(); }
    std::uint32_t slotted_devices() const noexcept { return n_slotted_.get(); }
    std::uint32_t mask_recomputes() const noexcept { return n_mask_recompute_.get(); }
    std::uint32_t axis_edges() const noexcept { return n_axis_edges_.get(); }
    std::uint32_t mouse_remainder_bytes() const noexcept { return n_mouse_rem_.get(); }
    std::uint32_t key_overflows() const noexcept { return n_key_overflow_.get(); }
    std::uint32_t quirk_drops() const noexcept { return n_quirk_drops_.get(); }
    unsigned rejected_nodes() const noexcept { return stats_.rejected; }

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

    std::vector<InputDevice> devices_;
    PlayerSlots players_;
    MapStore maps_;
    UniqueFd inotify_{};
    std::string input_dir_ = "/dev/input";

    bool grabbed_ = true;
    bool hotplug_pending_ = false;
    std::uint32_t devices_gen_ = 0;
    JoyMask joy_mask_[kMaxPlayers]{};
    std::vector<NoMergeRule> cfg_no_merge_;
    std::vector<DeadzoneRule> cfg_deadzone_;
    JoyPlan joy_plan_{};
    const IAnalogReshape* reshape_ = nullptr;
    std::vector<std::uint32_t> unique_rows_;
    Vid spinner_vid_{};
    Pid spinner_pid_{};
    Stats stats_{};
    MovableCounter n_mapped_{};
    MovableCounter n_slotted_{};
    MovableCounter n_mask_recompute_{};
    MovableCounter n_axis_edges_{};
    MovableCounter n_mouse_rem_{};
    MovableCounter n_key_overflow_{};
    MovableCounter n_quirk_drops_{};
    MovableCounter n_live_mask_{};
    MovableCounter n_slot_hash_[kMaxPlayers]{};
    MovableCounter n_ghost_{};
};

inline constexpr std::uint8_t kUioButSw = 0x01;
Ex<void> emit_but_sw(hal::ISpiTransport& link, std::uint16_t map);

}  // namespace mister::svc
