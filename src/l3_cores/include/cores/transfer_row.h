// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <variant>

#include "cores/core_window_decl.h"
#include "cores/row_shape_key.h"
#include "cores/types.h"
#include "cores/window_spot.h"
#include "cores/post_notify.h"
#include "hal/phys_region.h"
#include "infra/error.h"
#include "infra/message_sum.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::cores {

enum class PlaceKind : std::uint8_t { Fabric, Window, kCount };
using RowPlace = std::variant<hal::FabricAddr, WindowSpot>;
static_assert(
    std::variant_size_v<RowPlace> == infra::ordinal(PlaceKind::kCount),
    "RowPlace: one PlaceKind name per arm"
);
template <PlaceKind K, class T>
inline constexpr bool
    kPlaceArmIs = std::is_same_v<std::variant_alternative_t<infra::ordinal(K), RowPlace>, T>;
static_assert(
    kPlaceArmIs<PlaceKind::Fabric, hal::FabricAddr> && kPlaceArmIs<PlaceKind::Window, WindowSpot>
);

enum class RowDest : std::uint8_t { Window, Fio, Notify, Payload };

enum class RowBracket : std::uint8_t { AroundWithLength, None };

struct TransferRow {
    TASTY_SEAT_EXEMPT(component);

    RowDest dest = RowDest::Fio;
    bool shaped = false;
    bool derived = false;
    RowShapeKey shape{};
    bool byte_words = false;
    proto::WideIoIndex index{};
    RowPlace place = hal::FabricAddr{};
    RowBracket bracket = RowBracket::AroundWithLength;
    std::uint8_t stride = 1;
    std::uint8_t lane = 0;
    std::byte fill = std::byte{0};

    std::string_view source{};
    proto::FileOffset file_offset{};
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
    if (const auto* fabric = std::get_if<hal::FabricAddr>(&r.place))
        return *fabric;
    if (const auto* spot = std::get_if<WindowSpot>(&r.place))
        return spot->address_in(windows, aperture);
    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
}

}
