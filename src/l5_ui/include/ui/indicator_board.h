// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <optional>
#include <span>
#include <tuple>

#include "app/indicator_id.h"
#include "infra/fixed_str.h"
#include "infra/indexed_array.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/osd_target.h"
#include "proto/types.h"
#include "svc/types.h"
#include "ui/indicator_canvas.h"
#include "ui/indicator_row.h"
#include "ui/indicator_source.h"
#include "ui/source_revision.h"

namespace mister::ui {

[[nodiscard]] proto::OsdTarget indicator_target(std::span<const proto::OsdTarget> targets) noexcept;

class IndicatorBoard {
    TASTY_SEAT_RESIDENT(Ui);

public:
    using Lifetime = std::chrono::milliseconds;

    using Text = FixedStr<
        std::tuple_size_v<app::PerIndicator<IndicatorCanvas>> * IndicatorCanvas::Line::kBufSize,
        StrFit::Clip>;

    explicit IndicatorBoard(std::span<const IndicatorRow> rows = kIndicatorRows) noexcept
        : rows_(rows) {}

    [[nodiscard]] bool post(app::IndicatorId id, Lifetime lifetime) noexcept;

    [[nodiscard]] bool raise(app::IndicatorId id, const IIndicatorSource& source) noexcept;

    [[nodiscard]] bool lower(app::IndicatorId id) noexcept;

    [[nodiscard]] bool round(std::chrono::nanoseconds now, app::IndicatorSet shown) noexcept;

    [[nodiscard]] app::IndicatorSet active() const noexcept { return active_; }

    [[nodiscard]] app::IndicatorSet shown() const noexcept { return shown_; }

    [[nodiscard]] proto::OsdRowCount line_count() const noexcept {
        return next_line_ - proto::OsdRow{};
    }

    [[nodiscard]] infra::OptRef<const IndicatorCanvas> line(proto::OsdRow row) const noexcept {
        if (!line_count().covers(row))
            return {};
        return lines_.find(row);
    }

    [[nodiscard]] const Text& text() const noexcept { return text_; }

    [[nodiscard]] proto::OsdTarget target() const noexcept;

    [[nodiscard]] app::InfoAnchor anchor() const noexcept;

    [[nodiscard]] svc::Tally refusals() const noexcept { return refusals_; }

private:
    struct Slot {
        Lifetime lifetime{};
        std::optional<std::chrono::nanoseconds> shown_at{};
        infra::OptRef<const IIndicatorSource> source{};
        SourceRevision seen{};
    };

    void compose_() noexcept;

    [[nodiscard]] const IndicatorRow& row_(app::IndicatorId id) const noexcept {
        return rows_[infra::ordinal(id)];
    }

    std::span<const IndicatorRow> rows_;
    app::PerIndicator<Slot> slots_{};
    app::IndicatorSet active_{};
    app::IndicatorSet shown_{};
    infra::IndexedArray<
        IndicatorCanvas,
        std::tuple_size_v<app::PerIndicator<IndicatorCanvas>>,
        proto::OsdRow>
        lines_{};
    proto::OsdRow next_line_{};
    Text text_{};
    svc::Tally refusals_{};
    bool dirty_ = false;
};

}
