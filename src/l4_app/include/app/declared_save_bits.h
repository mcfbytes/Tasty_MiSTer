// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string_view>

#include "cores/save_state_triggers.h"
#include "proto/bit_field.h"

namespace mister::app {

struct DeclaredSaveBits final {
    std::optional<proto::BitField> save;
    std::optional<proto::BitField> restore;
};

[[nodiscard]] inline DeclaredSaveBits declared_save_bits(std::string_view conf_str_name) noexcept {
    const auto row = cores::find_save_state_triggers(conf_str_name);
    if (!row)
        return DeclaredSaveBits{};
    return DeclaredSaveBits{.save = row->save_bits(), .restore = row->restore_bits()};
}

}
