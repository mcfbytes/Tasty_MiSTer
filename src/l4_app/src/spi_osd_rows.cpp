// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/spi_osd_rows.h"

#include <span>

namespace mister::app {

template <proto::OsdRow First>
proto::OsdRowCount SpiOsdRows<First>::backlog() const noexcept {
    TASTY_SEAT_BODY(SpiOsdRows);
    proto::OsdRowCount n{};
    const proto::OsdRowCount bound = surface_->visible_rows();
    for (proto::OsdRow r = First; bound.covers(r); r += proto::kOneOsdRow) {
        if (surface_->seq(r) != proto::row_cell(seen_, r))
            n += proto::kOneOsdRow;
    }
    return n;
}

template <proto::OsdRow First>
bool SpiOsdRows<First>::flush_one() noexcept {
    TASTY_SEAT_BODY(SpiOsdRows);
    unsigned pending = 0;
    proto::OsdRow target{};
    std::uint32_t target_seq = 0;

    const proto::OsdRowCount bound = surface_->visible_rows();
    for (proto::OsdRow r = First; bound.covers(r); r += proto::kOneOsdRow) {
        const std::uint32_t s = surface_->seq(r);
        if (s == proto::row_cell(seen_, r))
            continue;
        if (++pending == 1) {
            target = r;
            target_seq = s;
        }
    }
    if (pending == 0)
        return false;
    if (pending > tally_->backlog_hw)
        tally_->backlog_hw = pending;
    if (pending > 1)
        ++tally_->starved;

    const proto::OsdRow row = target;

    const std::uint32_t got = surface_->sample(row, scratch_);
    if (got == 0) {
        ++tally_->sample_refusals;
        return false;
    }
    proto::Link link{*link_};
    const auto flushed = rows_.flush_row(
        link,
        row,
        std::span<const std::uint8_t>(scratch_.b, sizeof scratch_.b)
    );
    ++(First < proto::OsdSurface::kStatusRow && row < proto::OsdSurface::kStatusRow
           ? tally_->page_rows_sent
           : tally_->status_rows_sent);
    if (!flushed) {
        ++tally_->flush_failures;
        return true;
    }

    if (got != target_seq) {
        ++tally_->commits_refused;
        return true;
    }
    proto::row_cell(seen_, row) = target_seq;
    ++tally_->rows_flushed;
    return true;
}

template class SpiOsdRows<proto::OsdRow{}>;
template class SpiOsdRows<proto::OsdSurface::kStatusRow>;

}
