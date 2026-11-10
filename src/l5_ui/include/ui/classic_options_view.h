// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "app/page_description.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/conf_str.h"
#include "ui/options_view.h"
#include "ui/osd_renderer.h"

namespace mister::ui {

class IOptionsSurface;
class OptionItem;
class OptionsModel;

class ClassicOptionsView final : public IOptionsView {
    TASTY_SEAT_RESIDENT(Ui);

public:
    static constexpr RowCount kOsdRows = kFullOsdRows;

    explicit ClassicOptionsView(IOptionsSurface& surface) noexcept;

    PageId id() const override;
    BuildEnv walk_env(const BuildEnv& env) const override;
    void on_model_rebuilt() override;

    bool walk(PageWalk& out) const override;
    Transition handle(const UiEvent& ev) override;
    MenuMask mask() const override;
    void set_selection(RowIndex i) override;
    RowIndex selection() const override;

    RowIndex first_row() const noexcept;
    bool empty() const override;

private:
    [[nodiscard]] proto::FieldValue status_field(proto::BitField b) const;
    [[nodiscard]] std::string compose(const OptionItem& it) const;

    [[nodiscard]] RowCount osd_height() const noexcept { return kOsdRows; }

    [[nodiscard]] std::optional<Glyph> leftchar_of(proto::OsdRow row, bool has_ring) const noexcept;

    struct Drawn {
        app::PageDescription::Row hd;
        infra::OptRef<const OptionItem> item;
    };

    struct DrawnList {
        std::vector<Drawn> rows;
        RowIndex selected{};
    };

    struct Line {
        std::string text;
        bool emitted = false;
    };

    [[nodiscard]] DrawnList compose_drawn() const;

    void compose_lines(RowSpan<Line> lines) const;

    infra::OptRef<const OptionItem> row_at(RowCount row) const;

    template <class Visit>
    void for_each_drawn(Visit&& visit) const;

    void track_viewport() noexcept;

    IOptionsSurface& surface_;
    const OptionsModel& model_;
    RowIndex sel_{};

    RowIndex first_row_{};
};

}
