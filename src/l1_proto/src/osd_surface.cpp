// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/osd_surface.h"

#include <algorithm>
#include <cstring>

#include "infra/error.h"
#include "infra/prelude.h"

namespace mister::proto {

std::span<std::uint8_t> OsdSurface::row(OsdRow r) {
    if (!kMaxRows.covers(r)) {
        fatal(r.refusal(Errc::slot_range, ERR_SITE()), "osd row");
    }
    return std::span<std::uint8_t>(ui_.rows[r].data(), kRowBytes);
}

std::span<const std::uint8_t> OsdSurface::row(OsdRow r) const {
    if (!kMaxRows.covers(r)) {
        fatal(r.refusal(Errc::slot_range, ERR_SITE()), "osd row");
    }
    return std::span<const std::uint8_t>(ui_.rows[r].data(), kRowBytes);
}

void OsdSurface::publish(OsdRow r) noexcept {
    if (!kMaxRows.covers(r))
        return;
    Row staged;
    std::memcpy(staged.b, ui_.rows[r].data(), kRowBytes);
    published_[r].publish(staged);
}

infra::CellGen OsdSurface::seq(OsdRow r) const noexcept {
    if (!kMaxRows.covers(r))
        return infra::CellGen{};
    return published_[r].generation();
}

std::uint32_t OsdSurface::sample(OsdRow r, Row& dst) const noexcept {
    if (!kMaxRows.covers(r))
        return 0;
    return published_[r].sample_into(dst);
}

std::uint32_t OsdSurface::refusals() const noexcept {
    std::uint32_t n = 0;
    for (const auto& cell : published_)
        n += cell.refusals();
    return n;
}

void OsdSurface::publish_info_window(const InfoWindow& w) noexcept {
    if (w == ui_.last_info_window)
        return;
    ui_.last_info_window = w;
    info_window_.publish(w);
}

void OsdSurface::set_visible_rows(OsdRowCount n) noexcept {
    visible_rows_.publish(VisibleRows{std::min(n, kMaxRows)});
}

OsdRowCount OsdSurface::visible_rows() const noexcept {
    VisibleRows rows;
    if (visible_rows_.sample_into(rows) == 0)
        return kMaxRows;
    return rows.n;
}

void OsdSurface::clear() noexcept {
    for (OsdRow r{}; kPageRows.covers(r); r += kOneOsdRow)
        ui_.rows[r].fill(0);
    for (OsdRow r{}; kMaxRows.covers(r); r += kOneOsdRow)
        publish(r);
    publish_info_window(InfoWindow{});
}

}
