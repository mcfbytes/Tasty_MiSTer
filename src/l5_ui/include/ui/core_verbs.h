// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string_view>

#include "app/declared_save_bits.h"
#include "infra/seat.h"
#include "proto/bit_field.h"

namespace mister::ui {

struct CoreVerbs final {
    TASTY_SEAT_RESIDENT(Ui);
    std::optional<proto::BitField> save;
    std::optional<proto::BitField> restore;
};

[[nodiscard]] inline CoreVerbs declared_core_verbs(std::string_view conf_str_name) noexcept {
    const app::DeclaredSaveBits bits = app::declared_save_bits(conf_str_name);
    return CoreVerbs{.save = bits.save, .restore = bits.restore};
}

}
