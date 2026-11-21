// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <optional>
#include <string_view>

#include "cores/save_state_triggers_table.h"
#include "proto/bit_field.h"

namespace mister::cores {

struct SaveStateTriggers {
    std::string_view conf_str_name;
    proto::BitField save;
    proto::BitField restore;
    proto::BitField slot;

    [[nodiscard]] constexpr std::optional<proto::BitField> save_bits() const noexcept {
        return save.valid() ? std::optional{save} : std::nullopt;
    }

    [[nodiscard]] constexpr std::optional<proto::BitField> restore_bits() const noexcept {
        return restore.valid() ? std::optional{restore} : std::nullopt;
    }

    [[nodiscard]] constexpr std::optional<proto::BitField> slot_bits() const noexcept {
        return slot.valid() ? std::optional{slot} : std::nullopt;
    }

    [[nodiscard]] constexpr bool drivable() const noexcept {
        return save.valid() && restore.valid();
    }
};

[[nodiscard]] constexpr std::optional<SaveStateTriggers> find_save_state_triggers(
    std::string_view conf_str_name
) noexcept {
    const auto& rows = detail::kRows;
    const auto it = std::lower_bound(
        rows.begin(),
        rows.end(),
        conf_str_name,
        [](const detail::Row& r, std::string_view n) { return r.name < n; }
    );
    if (it == rows.end() || it->name != conf_str_name)
        return std::nullopt;
    return SaveStateTriggers{it->name, it->save, it->restore, it->slot};
}

}
