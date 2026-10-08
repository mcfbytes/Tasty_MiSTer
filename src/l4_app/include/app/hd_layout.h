// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>

namespace mister::app {

struct HdLayout {
    struct Rect {
        std::int32_t x = 0;
        std::int32_t y = 0;
        std::int32_t w = 0;
        std::int32_t h = 0;
    };
    Rect panel{};
    Rect game{};
    Rect cover{};
    float scale = 1.f;
};

[[nodiscard]] constexpr HdLayout layout_for(std::uint32_t w, std::uint32_t h) noexcept {
    HdLayout L{};
    if (h == 0 || w == 0) return L;
    L.scale = static_cast<float>(h) / 720.f;
    const auto ih = static_cast<std::int32_t>(h);
    const auto iw = static_cast<std::int32_t>(w);
    const auto panel_w = static_cast<std::int32_t>((static_cast<std::uint64_t>(h) * 3u) / 4u);
    L.panel = {.x = 0, .y = 0, .w = panel_w, .h = ih};
    L.game = {.x = panel_w, .y = 0, .w = iw - panel_w, .h = ih};
    const std::int64_t gw = L.game.w;
    const std::int64_t gh = L.game.h;
    std::int64_t cw = 0;
    std::int64_t ch = 0;
    if (gw * 3 >= gh * 4) {
        cw = gw;
        ch = gw * 3 / 4;
    } else {
        ch = gh;
        cw = gh * 4 / 3;
    }
    const std::int64_t cx = L.game.x + (gw - cw) / 2;
    const std::int64_t cy = L.game.y + (gh - ch) / 2;
    const std::int64_t x0 = std::max(cx, static_cast<std::int64_t>(L.game.x));
    const std::int64_t y0 = std::max(cy, static_cast<std::int64_t>(L.game.y));
    const std::int64_t x1 = std::min(cx + cw, static_cast<std::int64_t>(L.game.x) + gw);
    const std::int64_t y1 = std::min(cy + ch, static_cast<std::int64_t>(L.game.y) + gh);
    L.cover = {.x = static_cast<std::int32_t>(x0),
               .y = static_cast<std::int32_t>(y0),
               .w = static_cast<std::int32_t>(x1 - x0),
               .h = static_cast<std::int32_t>(y1 - y0)};
    return L;
}

}  // namespace mister::app
