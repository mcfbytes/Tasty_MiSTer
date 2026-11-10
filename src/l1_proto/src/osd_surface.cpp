// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/osd_surface.h"

#include <algorithm>
#include <cstring>

#include "infra/error.h"

namespace mister::proto {

std::span<std::uint8_t> OsdSurface::row(OsdRow r) {
    if (!kMaxRows.covers(r)) {
        fatal(Error{Errc::slot_range, ERR_SITE(), r.v}, "osd row");
    }
    return std::span<std::uint8_t>(row_cell(ui_.rows, r).data(), kRowBytes);
}

std::span<const std::uint8_t> OsdSurface::row(OsdRow r) const {
    if (!kMaxRows.covers(r)) {
        fatal(Error{Errc::slot_range, ERR_SITE(), r.v}, "osd row");
    }
    return std::span<const std::uint8_t>(row_cell(ui_.rows, r).data(), kRowBytes);
}

void OsdSurface::publish(OsdRow r) noexcept {
    if (!kMaxRows.covers(r))
        return;
    Row staged;
    std::memcpy(staged.b, row_cell(ui_.rows, r).data(), kRowBytes);
    row_cell(published_, r).publish(staged);
}

std::uint32_t OsdSurface::seq(OsdRow r) const noexcept {
    if (!kMaxRows.covers(r))
        return 0;
    return row_cell(published_, r).generation();
}

std::uint32_t OsdSurface::sample(OsdRow r, Row& dst) const noexcept {
    if (!kMaxRows.covers(r))
        return 0;
    return row_cell(published_, r).sample_into(dst);
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
    constexpr std::size_t kPageBytes = kPageRows.size() * kRowBytes;
    std::memset(&ui_.rows, 0, kPageBytes);
    for (OsdRow r{}; kMaxRows.covers(r); r += kOneOsdRow)
        publish(r);
    publish_info_window(InfoWindow{});
}

}
