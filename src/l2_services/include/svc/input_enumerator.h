// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "infra/error.h"
#include "infra/opt_ref.h"
#include "svc/deadzone_rule.h"
#include "svc/device_match.h"
#include "svc/device_table.h"
#include "svc/input_device.h"
#include "svc/joy_plan.h"
#include "svc/map_store.h"
#include "svc/no_merge_rule.h"
#include "svc/types.h"
#include "infra/seat.h"

namespace mister::svc {

class Vfs;
class IAnalogReshape;

inline constexpr std::string_view kDefaultInputDir = "/dev/input";

class InputEnumerator {
    TASTY_SEAT_RESIDENT(Diag);

public:
    [[nodiscard]] static InputEnumerator create(std::string_view input_dir);

    [[nodiscard]] static InputEnumerator create(const Vfs& storage, std::string_view input_dir);

    [[nodiscard]] Ex<void> enumerate();

    bool admit_and_adopt(InputDevice&& dev);
    [[nodiscard]] Ex<void> resolve_identities();

    void apply_deadzones() noexcept;

    [[nodiscard]] Ex<void> load_maps_for(
        std::string_view core,
        bool front_end,
        infra::OptRef<const IAnalogReshape> reshape = std::nullopt,
        const JoyPlan& joy = JoyPlan{}
    );

    [[nodiscard]] Ex<void> adopt(InputDevice&& dev);

    const DeviceTable& table() const noexcept { return table_; }

    DeviceTable& table() noexcept { return table_; }

    [[nodiscard]] DeviceTable take_table() noexcept;

    const std::string& input_dir() const noexcept { return input_dir_; }

    void set_cfg_no_merge_rules(std::span<const NoMergeRule> rows);

    void set_cfg_deadzone_rules(std::span<const DeadzoneRule> rows);
    void set_spinner(SpinnerPolicy spinner) noexcept;

    void set_unique_mapping(std::span<const VidPidKey> rows);

    const JoyPlan& joy_plan() const noexcept { return joy_plan_; }

    infra::OptRef<const IAnalogReshape> analog_reshape() const noexcept { return reshape_; }

    const MapStore& maps() const noexcept { return maps_; }

    struct Stats {
        unsigned devices = 0;
        unsigned mouse_fixups = 0;
        unsigned empty_id_skips = 0;
        unsigned overflow = 0;
        unsigned rejected = 0;
    };

    const Stats& stats() const noexcept { return stats_; }

    unsigned rejected_nodes() const noexcept { return stats_.rejected; }

private:
    explicit InputEnumerator(std::string_view input_dir) noexcept : input_dir_(input_dir) {}

    InputEnumerator(const Vfs& storage, std::string_view input_dir) noexcept
        : maps_(storage),
          input_dir_(input_dir) {}

    DeviceTable table_{};
    MapStore maps_;
    std::string input_dir_;
    std::vector<NoMergeRule> cfg_no_merge_;
    std::vector<DeadzoneRule> cfg_deadzone_;
    JoyPlan joy_plan_{};
    infra::OptRef<const IAnalogReshape> reshape_;
    std::vector<VidPidKey> unique_rows_;
    SpinnerPolicy spinner_{};
    Stats stats_{};
};

}
