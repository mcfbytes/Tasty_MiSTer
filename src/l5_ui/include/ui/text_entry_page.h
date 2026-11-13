// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "infra/message_sum.h"
#include "infra/seat.h"
#include "ui/entry_buffer.h"
#include "ui/key_grid.h"
#include "ui/page.h"
#include "ui/shift_state.h"
#include "ui/text_entry_request.h"
#include "svc/types.h"

namespace mister::ui {

class TextEntryPage final : public Page {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit TextEntryPage(const TextEntryRequest& request) noexcept;

    PageId id() const override;
    bool walk(PageWalk& out) const override;
    Transition handle(const UiEvent& ev) override;
    MenuMask mask() const override;
    void set_selection(RowIndex i) override;
    RowIndex selection() const override;
    svc::Tally refusals() const noexcept override;

private:
    enum class Line : std::uint8_t {
        Field,
        Grid0,
        Grid1,
        Grid2,
        Grid3,
        Layer,
        Reveal,
        Space,
        Backspace,
        Done,
        Cancel,
        kCount,
    };
    static constexpr std::array<Line, 11> kLines{
        Line::Field,
        Line::Grid0,
        Line::Grid1,
        Line::Grid2,
        Line::Grid3,
        Line::Layer,
        Line::Reveal,
        Line::Space,
        Line::Backspace,
        Line::Done,
        Line::Cancel
    };
    static_assert(kLines.size() == 11 && infra::ordinal(Line::kCount) == 11);
    [[nodiscard]] bool shown_(Line line) const noexcept;
    [[nodiscard]] bool focusable_(Line line) const noexcept;
    [[nodiscard]] static std::optional<RowIndex> grid_row_(Line line) noexcept;
    [[nodiscard]] Line line_at_(RowIndex i) const noexcept;
    [[nodiscard]] bool valid_() const noexcept;
    [[nodiscard]] bool accepts_(char c) const noexcept;
    Transition act_(Line line);
    Transition finish_();
    Transition cancel_();

    TextEntryRequest request_;
    EntryBuffer text_{};
    KeyGrid grid_;
    ShiftState shift_ = ShiftState::Up;
    bool revealed_ = false;
    bool typing_ = false;
    RowIndex sel_{};
    svc::Tally refusals_{};
};

}
