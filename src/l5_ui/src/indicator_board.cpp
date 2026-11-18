// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/indicator_board.h"

namespace mister::ui {

bool IndicatorBoard::post(app::IndicatorId id, Lifetime lifetime) noexcept {
    TASTY_SEAT_BODY(IndicatorBoard);
    if (row_(id).kind != IndicatorKind::Toast) {
        refusals_.add();
        return false;
    }
    Slot& slot = slots_[infra::ordinal(id)];
    slot = Slot{.lifetime = lifetime};
    active_ = active_.with(id);
    shown_ = shown_.without(id);
    dirty_ = true;
    return true;
}

bool IndicatorBoard::raise(app::IndicatorId id, const IIndicatorSource& source) noexcept {
    TASTY_SEAT_BODY(IndicatorBoard);
    if (row_(id).kind != IndicatorKind::Overlay) {
        refusals_.add();
        return false;
    }
    slots_[infra::ordinal(id)] = Slot{.source = infra::OptRef<const IIndicatorSource>{source}};
    active_ = active_.with(id);
    dirty_ = true;
    return true;
}

bool IndicatorBoard::lower(app::IndicatorId id) noexcept {
    TASTY_SEAT_BODY(IndicatorBoard);
    if (row_(id).kind != IndicatorKind::Overlay) {
        refusals_.add();
        return false;
    }
    if (!active_.has(id))
        return true;
    slots_[infra::ordinal(id)] = Slot{};
    active_ = active_.without(id);
    shown_ = shown_.without(id);
    dirty_ = true;
    return true;
}

bool IndicatorBoard::round(std::chrono::nanoseconds now, app::IndicatorSet shown) noexcept {
    TASTY_SEAT_BODY(IndicatorBoard);
    bool changed = dirty_;
    dirty_ = false;
    for (const IndicatorRow& row : rows_) {
        if (!active_.has(row.id))
            continue;
        Slot& slot = slots_[infra::ordinal(row.id)];
        if (row.kind == IndicatorKind::Overlay) {
            shown_ = shown_.with(row.id);
            const SourceRevision revision = slot.source->revision();
            if (!(revision == slot.seen)) {
                slot.seen = revision;
                changed = true;
            }
            continue;
        }
        if (shown.has(row.id) && !slot.shown_at)
            slot.shown_at = now;
        if (slot.shown_at)
            shown_ = shown_.with(row.id);
        if (slot.shown_at && now - *slot.shown_at >= slot.lifetime) {
            slot = Slot{};
            active_ = active_.without(row.id);
            shown_ = shown_.without(row.id);
            changed = true;
        }
    }
    if (changed)
        compose_();
    return changed;
}

proto::OsdTarget indicator_target(std::span<const proto::OsdTarget> targets) noexcept {
    if (targets.empty())
        return proto::OsdTarget::All;
    proto::OsdTarget meet = targets.front();
    for (const proto::OsdTarget next : targets.subspan(1)) {
        const std::optional<proto::OsdTarget> both = proto::osd_target_meet(meet, next);
        if (!both)
            return targets.front();
        meet = *both;
    }
    return meet;
}

proto::OsdTarget IndicatorBoard::target() const noexcept {
    TASTY_SEAT_BODY(IndicatorBoard);
    app::PerIndicator<proto::OsdTarget> targets{};
    std::size_t n = 0;
    for (const IndicatorRow& row : rows_) {
        if (active_.has(row.id))
            targets[n++] = row.targets;
    }
    return indicator_target(std::span<const proto::OsdTarget>{targets}.first(n));
}

app::InfoAnchor IndicatorBoard::anchor() const noexcept {
    TASTY_SEAT_BODY(IndicatorBoard);
    for (const IndicatorRow& row : rows_) {
        if (active_.has(row.id))
            return row.anchor;
    }
    return app::InfoAnchor::InfoOrigin;
}

void IndicatorBoard::compose_() noexcept {
    next_line_ = proto::OsdRow{};
    text_.clear();
    for (const IndicatorRow& row : rows_) {
        if (!active_.has(row.id))
            continue;
        IndicatorCanvas& line = lines_[next_line_];
        line = row.kind == IndicatorKind::Toast ? IndicatorCanvas{row.text}.with_icon(row.icon)
                                                : slots_[infra::ordinal(row.id)].source->draw();
        if (next_line_ != proto::OsdRow{})
            (void)text_.append("\n");
        (void)text_.append(line.text());
        next_line_ += proto::kOneOsdRow;
    }
}

}
