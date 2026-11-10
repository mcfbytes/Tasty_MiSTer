// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "cores/core_window_decl.h"
#include "cores/types.h"
#include "cores/post_notify.h"
#include "hal/phys_region.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::cores {

enum class RowDest : std::uint8_t { Window, Fio, Notify, Payload };

enum class RowBracket : std::uint8_t { AroundWithLength, None };

struct TransferRow {
    TASTY_SEAT_EXEMPT(component);

    RowDest dest = RowDest::Fio;
    bool shaped = false;
    std::uint8_t shape = 0;
    bool byte_words = false;
    proto::WideIoIndex index{};
    std::uint32_t addr = 0;
    WindowRef window = WindowRef::physical();
    RowBracket bracket = RowBracket::AroundWithLength;
    std::uint8_t stride = 1;
    std::uint8_t lane = 0;
    std::uint8_t fill = 0;

    std::string_view source{};
    std::uint32_t file_offset = 0;
    proto::FileSize file_len{};
    proto::FileSize extent{};
    proto::FileSize length{};
    bool placed = false;
    std::uint32_t mirror = 0;
    PostNotify notify{};

    [[nodiscard]] constexpr proto::FileSize spread_bytes(
        proto::FileSize file_bytes
    ) const noexcept {
        return file_bytes * std::uint64_t{stride};
    }

    [[nodiscard]] constexpr proto::FileSize spread_at(
        proto::FileSize file_position
    ) const noexcept {
        return file_position * std::uint64_t{stride};
    }

    [[nodiscard]] constexpr proto::FileSize extent_bytes(
        proto::FileSize file_bytes
    ) const noexcept {
        return extent.empty() ? spread_bytes(file_bytes) : extent;
    }

    [[nodiscard]] constexpr proto::FileSize bracket_length(proto::FileSize owned) const noexcept {
        return length.empty() ? owned : length;
    }

    constexpr void set_file_extent(proto::FileSize file, proto::FileSize owned) noexcept {
        file_len = file;
        extent = owned;
        length = file;
    }
};

[[nodiscard]] constexpr Ex<hal::FabricAddr> row_address(
    const TransferRow& r,
    std::span<const CoreWindowDecl> windows,
    os::PhysAddr aperture
) {
    const auto slot = r.window.slot();
    if (!slot)
        return hal::FabricAddr{r.addr};
    const auto window = slot->in(windows);
    if (!window)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), r.addr});
    const os::PhysAddr at = aperture.advanced(window->region.offset).advanced(r.addr);
    if (!hal::to_fabric(at).has_value())
        return std::unexpected(Error{Errc::aperture_range, ERR_SITE(), r.addr});
    return hal::FabricAddr{static_cast<std::uint32_t>(at.v)};
}

}
