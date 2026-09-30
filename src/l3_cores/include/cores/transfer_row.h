// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "cores/core_window_decl.h"
#include "cores/post_notify.h"
#include "infra/error.h"
#include "proto/types.h"

namespace mister::cores {

enum class RowDest : std::uint8_t { Window, Fio, Notify, Payload };

enum class RowBracket : std::uint8_t { AroundWithLength, None };

inline constexpr std::uint8_t kPhysicalAddr = 0xFF;

struct TransferRow {
    RowDest dest = RowDest::Fio;
    bool shaped = false;
    std::uint8_t shape = 0;
    bool byte_words = false;
    proto::IoIndex index{};
    std::uint32_t addr = 0;
    std::uint8_t window = kPhysicalAddr;
    RowBracket bracket = RowBracket::AroundWithLength;
    std::uint8_t stride = 1;
    std::uint8_t lane = 0;
    std::uint8_t fill = 0;

    std::string_view source{};
    std::uint32_t file_offset = 0;
    std::uint32_t file_len = 0;
    std::uint32_t extent = 0;
    bool placed = false;
    std::uint32_t mirror = 0;
    PostNotify notify{};
};

[[nodiscard]] constexpr Ex<std::uint32_t> row_address(const TransferRow& r,
                                                      std::span<const CoreWindowDecl> windows,
                                                      std::uint64_t aperture_base) {
    if (r.window == kPhysicalAddr) return r.addr;
    if (r.window >= windows.size())
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), r.window});
    const std::uint64_t a = aperture_base + windows[r.window].region.offset + r.addr;
    if (a > 0xFFFF'FFFFu) return std::unexpected(Error{Errc::aperture_range, ERR_SITE(), r.addr});
    return static_cast<std::uint32_t>(a);
}

}  // namespace mister::cores
