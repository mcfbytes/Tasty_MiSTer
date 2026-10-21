// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "infra/seat.h"
#include "svc/config.h"
#include "svc/config_snapshot.h"

namespace mister::svc {

class ConfigField {
public:
    TASTY_SEAT_EXEMPT(component);

    ConfigField(ConfigSnapshot& snapshot, const Option& row) noexcept;

    void store(std::uint32_t bits) noexcept;

    void assign_string(std::string_view value) noexcept;

    void clear_slots() noexcept;
    void fill_first_empty_slot(std::string_view value) noexcept;

    void clear_count() noexcept;

    [[nodiscard]] std::optional<ConfigField> next_element() noexcept;

private:
    ConfigField(std::byte* base, const Option& row) noexcept : base_(base), row_(row) {}

    std::byte* base_;
    Option row_;
};

}
