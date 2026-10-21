// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "svc/button_map.h"
#include "svc/deadzone_rule.h"
#include "svc/device_facts.h"
#include "svc/input_service.h"
#include "svc/joy_plan.h"

namespace mister::svc::rules {

using FactsSpan = std::span<DeviceFacts* const>;

struct MergeStats {
    unsigned merged = 0;
    unsigned mouse_fixups = 0;
    unsigned empty_id_skips = 0;
    unsigned mssp = 0;
};

inline constexpr Vid kAllMiceVid{0xFFFF};
inline constexpr Pid kAllMicePid{0xFFFF};

struct SpinnerPolicy {
    Vid vid{};
    Pid pid{};
};

struct UniquePolicy {
    bool force = false;
    bool all = false;

    std::span<const std::uint32_t> vidpids{};
};

std::uint32_t str_hash(std::string_view s, std::uint32_t seed = 5381u);

void derive_idstr(DeviceFacts& slot);

bool wants_unique_mapping(const DeviceIdentity& id, const UniquePolicy& pol);
std::string unique_mapping(const DeviceIdentity& id, const UniquePolicy& pol);

unsigned apply_proc_devices(std::string_view proc_text, FactsSpan slots);

bool bt_eligible(const DeviceFacts& slot) noexcept;

void make_unique(FactsSpan slots, const DeviceMatch& match);

void apply_no_merge(FactsSpan slots, std::span<const NoMergeRule> extra);

MergeStats merge_by_id(FactsSpan slots);
MergeStats fixup_mouse_nodes(FactsSpan slots, const SpinnerPolicy& spin);

MergeStats resolve_identities(
    FactsSpan slots,
    std::string_view proc_text,
    std::span<const NoMergeRule> extra,
    const SpinnerPolicy& spin
);

bool is_mouse_node(std::string_view devname);

std::string map_filename(std::string_view core, const DeviceIdentity& id, MapKind kind, bool mod);

struct MapPaths {
    std::string primary;
    std::string fallback;
};

MapPaths map_paths(std::string_view name, MapKind kind);

bool is_diagonal(AnalogXy in);

struct DeadzoneParams {
    std::int32_t deadzone = 0;
    float max_cardinal = 0.0f;
    float max_range = 0.0f;
    std::int32_t min_out = -127;
};

AnalogXy apply_deadzone(AnalogXy in, const DeadzoneParams& p);

std::uint8_t deadzone_class(QuirkId quirk);

bool deadzone_uid_matches(
    std::string_view uid,
    Vid vid,
    Pid pid,
    std::string_view id,
    std::string_view sysfs,
    std::string_view merge_id
);

std::uint8_t deadzone_for(
    QuirkId quirk,
    Vid vid,
    Pid pid,
    std::string_view id,
    std::string_view sysfs,
    std::string_view merge_id,
    std::span<const DeadzoneRule> cfg
);

ButtonMap default_sys_map();

ButtonMap derive_joy_map(
    std::span<const std::uint32_t> sys_map,
    bool axis_emu,
    const JoyPlan& plan = {}
);

std::array<std::uint32_t, 32> derive_menu_map(
    std::span<const std::uint32_t> sys_map,
    bool axis_keys
);

JoyMask mask_for(std::span<const std::uint32_t> joy_map, std::span<const std::uint64_t> down_bits);

}
