// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/osd_rows.h"

#include <span>

namespace mister::app {

template <unsigned First>
unsigned SpiOsdRows<First>::backlog() const noexcept {
    TASTY_SEAT_BODY(SpiOsdRows);
    unsigned n = 0;
    const unsigned bound = surface_->visible_rows();
    for (unsigned i = First; i < bound; ++i) {
        const proto::OsdRow r{static_cast<std::uint8_t>(i)};
        if (surface_->seq(r) != seen_[i - First])
            ++n;
    }
    return n;
}

template <unsigned First>
bool SpiOsdRows<First>::flush_one() noexcept {
    TASTY_SEAT_BODY(SpiOsdRows);
    unsigned pending = 0;
    int target = -1;
    std::uint32_t target_seq = 0;

    const unsigned bound = surface_->visible_rows();
    for (unsigned i = First; i < bound; ++i) {
        const proto::OsdRow r{static_cast<std::uint8_t>(i)};
        const std::uint32_t s = surface_->seq(r);
        if (s == seen_[i - First])
            continue;
        ++pending;
        if (target < 0) {
            target = static_cast<int>(i);
            target_seq = s;
        }
    }
    if (pending > tally_->backlog_hw)
        tally_->backlog_hw = pending;
    if (target < 0)
        return false;
    if (pending > 1)
        ++tally_->starved;

    const proto::OsdRow row{static_cast<std::uint8_t>(target)};

    const std::uint32_t got = surface_->sample(row, scratch_);
    if (got == 0) {
        ++tally_->sample_refusals;
        return false;
    }
    const auto flushed = rows_.flush_row(
        *link_,
        row,
        std::span<const std::uint8_t>(scratch_.b, sizeof scratch_.b)
    );
    ++(static_cast<unsigned>(target) < proto::OsdSurface::kPageRows ? tally_->page_rows_sent
                                                                    : tally_->status_rows_sent);
    if (!flushed) {
        ++tally_->flush_failures;
        return true;
    }

    if (got != target_seq) {
        ++tally_->commits_refused;
        return true;
    }
    seen_[static_cast<unsigned>(target) - First] = target_seq;
    ++tally_->rows_flushed;
    return true;
}

template class SpiOsdRows<0>;
template class SpiOsdRows<proto::OsdSurface::kPageRows>;

}
