// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <span>

#include "proto/types.h"
#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::proto {

class OsdSurface {
    TASTY_SEAT_MEDIATOR(Ui, RT);

public:
    static constexpr unsigned kMaxRows = 19;
    static constexpr unsigned kRowBytes = 256;
    static constexpr unsigned kPageRows = 16;

    struct Row {
        std::uint8_t b[kRowBytes];
    };

    std::span<std::uint8_t> row(OsdRow r);
    std::span<const std::uint8_t> row(OsdRow r) const;

    void publish(OsdRow r) noexcept;

    std::uint32_t seq(OsdRow r) const noexcept;

    [[nodiscard]] std::uint32_t sample(OsdRow r, Row& dst) const noexcept;

    std::uint32_t refusals() const noexcept;

    void clear() noexcept;

    void set_visible_rows(unsigned n) noexcept;

    unsigned visible_rows() const noexcept;

private:
    struct VisibleRows {
        std::uint8_t n;
    };
    xthread::Telemetry<VisibleRows> visible_rows_{};

    std::uint8_t rows_[kMaxRows][kRowBytes]{};
    xthread::Telemetry<Row> published_[kMaxRows]{};

    static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                  "item bound item 2: the publish counter must be a plain "
                  "load/store on the binding toolchain. A libatomic call on "
                  "T-RT's flush path is a lock on the RT path.");
};

}  // namespace mister::proto
