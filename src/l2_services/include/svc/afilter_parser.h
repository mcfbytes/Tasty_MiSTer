// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "infra/error.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/filter_wire_image.h"

namespace mister::svc {

class Vfs;

inline constexpr int kFilterLines = 9;

inline constexpr std::int64_t kCoefScaleQ39 = 0x8000000000LL;
inline constexpr std::int32_t kCoefScaleQ21 = 0x200000;

inline constexpr proto::FileSize kFilterFileReadCap{64u * 1024u};

using AfilterParse = std::optional<FilterWireImage>;

struct AfilterRead {
    AfilterParse parse{};
    bool opened = false;
};

struct AfilterParser {
    TASTY_SEAT_RESIDENT(Ui);

    [[nodiscard]] static AfilterParser create() noexcept;

    [[nodiscard]] Ex<AfilterRead> parse(const Vfs& vfs, std::string_view name) const;
};

}
