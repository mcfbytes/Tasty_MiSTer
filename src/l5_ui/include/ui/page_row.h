// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

#include "app/page_description.h"
#include "infra/fixed_str.h"
#include "proto/types.h"
#include "ui/osd_renderer.h"

namespace mister::ui {

inline constexpr std::size_t kClassicRowCap = 40;

inline constexpr std::uint8_t kMaxPageRows = 32;

struct PageRow {
    struct Classic {
        FixedStr<kClassicRowCap, StrFit::Clip> text{};
        proto::OsdRow row{};
        std::optional<Glyph> leftchar{};
        std::optional<OsdRenderer::Arrow> arrow{};
        bool inverted = false;
        bool stippled = false;
    };

    app::PageDescription::Row hd{};
    std::optional<Classic> classic{};
    bool has_hd = false;
    bool legend = false;
};

static_assert(std::is_trivially_copyable_v<PageRow>);
static_assert(kMaxPageRows >= 30);
static_assert(sizeof(PageRow) <= 256);

}
