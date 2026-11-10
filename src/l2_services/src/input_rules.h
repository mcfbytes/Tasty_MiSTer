// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "svc/admission_rule.h"
#include "svc/analog_xy.h"
#include "svc/button_map.h"
#include "svc/deadzone_rule.h"
#include "svc/device_facts.h"
#include "svc/device_match.h"
#include "svc/input_device.h"
#include "svc/joy_plan.h"
#include "svc/mouse_fixup_rule.h"
#include "svc/no_merge_rule.h"
#include "svc/packed_keys.h"
#include "svc/stick_cal.h"
#include "svc/types.h"

namespace mister::svc::rules {

using FactsSpan = std::span<DeviceFacts* const>;

struct MergeStats {
    unsigned merged = 0;
    unsigned mouse_fixups = 0;
    unsigned empty_id_skips = 0;
    unsigned mssp = 0;
};

inline constexpr VidPidKey kVidPidEndRow = VidPidKey{};

struct UniquePolicy {
    bool force = false;
    bool all = false;

    std::span<const VidPidKey> vidpids{};
};

inline constexpr std::uint32_t kDjb2Seed = 5381u;
inline constexpr std::uint32_t kDjb2Step = 33u;

inline constexpr std::size_t kNameSumChars = 128;

inline constexpr std::string_view kSpinnerSuffix = "_sp";

std::uint32_t str_hash(std::string_view s, std::uint32_t seed = kDjb2Seed);

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
    Deadzone deadzone{};
    StickCal learned{};
    std::int32_t min_out = -AnalogXy::kAxisMax;
};

AnalogXy apply_deadzone(AnalogXy in, const DeadzoneParams& p);

inline constexpr Deadzone kDeadzoneNone{0};
inline constexpr Deadzone kDeadzoneDualShock{10};
inline constexpr Deadzone kDeadzoneDefault{2};
Deadzone deadzone_class(QuirkId quirk);

bool deadzone_uid_matches(std::string_view uid, const DeviceFacts& dev);

Deadzone deadzone_for(const DeviceFacts& dev, std::span<const DeadzoneRule> cfg);

SysWords default_sys_map();

WireWords derive_joy_map(const SysWords& sys_map, bool axis_emu, const JoyPlan& plan = {});

WireWords derive_menu_map(const SysWords& sys_map, bool axis_keys);

JoyMask mask_for(std::span<const PackedKeys> joy_map, const KeyDownSet<kDownWords>& down);

}
