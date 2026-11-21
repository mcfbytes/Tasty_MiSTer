// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "infra/fixed_str.h"
#include "infra/prelude.h"
#include "proto/types.h"

namespace mister::ui {

enum class IndicatorIcon : std::uint8_t { None, Card, CardFault, kCount };

class IndicatorCanvas {
public:
    using Line = FixedStr<33, StrFit::Clip>;

    IndicatorCanvas() = default;

    explicit IndicatorCanvas(std::string_view text) noexcept { (void)text_.assign(text); }

    [[nodiscard]] IndicatorCanvas inverted_over(
        proto::OsdCol first,
        proto::OsdColCount count
    ) const noexcept {
        IndicatorCanvas out = *this;
        out.first_ = first;
        out.count_ = count;
        return out;
    }

    [[nodiscard]] IndicatorCanvas with_icon(IndicatorIcon icon) const noexcept {
        IndicatorCanvas out = *this;
        out.icon_ = icon;
        return out;
    }

    [[nodiscard]] std::string_view text() const noexcept { return text_.view(); }

    [[nodiscard]] IndicatorIcon icon() const noexcept { return icon_; }

    [[nodiscard]] bool inverted() const noexcept { return count_ != proto::OsdColCount{}; }

    [[nodiscard]] bool inverts(proto::OsdCol cell) const noexcept {
        return first_ <= cell && cell < first_ + count_;
    }

    [[nodiscard]] proto::OsdCol invert_first() const noexcept { return first_; }

    [[nodiscard]] proto::OsdColCount invert_count() const noexcept { return count_; }

private:
    Line text_{};
    proto::OsdCol first_{};
    proto::OsdColCount count_{};
    IndicatorIcon icon_ = IndicatorIcon::None;
};

}
