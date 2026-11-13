// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "infra/seat.h"
#include "proto/conf_str_file_row.h"
#include "proto/types.h"

namespace mister::proto {

[[nodiscard]] std::uint32_t parse_load_addr(std::string_view field) noexcept;

struct ConfStrEntry {
    TASTY_SEAT_EXEMPT(const_shared);

    proto::FileRowKind kind = proto::FileRowKind::File;
    std::uint8_t selentry = 0;

    std::optional<FileSlotDigit> digit{};
    bool opensave = false;
    bool store_name = false;
    std::string ext;

    IoIndex ioctl_index{};

    std::uint32_t load_addr = 0;
    std::string addon;
    bool addon_after = false;

    [[nodiscard]] IoIndex default_index() const noexcept;

    [[nodiscard]] ConfStrFileRow wire_row() const noexcept;

    [[nodiscard]] constexpr proto::FileRowId file_row() const noexcept {
        return proto::FileRowId::of(kind, ioctl_index);
    }

    [[nodiscard]] constexpr bool carries_load_addr() const noexcept {
        return kind == proto::FileRowKind::File;
    }
};

}
