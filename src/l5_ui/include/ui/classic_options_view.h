// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

#include "infra/seat.h"
#include "proto/conf_str.h"
#include "ui/options_view.h"

namespace mister::ui {

class IOptionsSurface;
class OptionItem;
class OptionsModel;

class ClassicOptionsView final : public IOptionsView {
    TASTY_SEAT_RESIDENT(Ui);

public:
    static constexpr RowCount kOsdRows{16};

    explicit ClassicOptionsView(IOptionsSurface& surface) noexcept;

    PageId id() const override;
    BuildEnv walk_env(const BuildEnv& env) const override;
    void on_walk() override;
    void render(OsdRenderer& out) const override;

    bool describe(app::PageDescription& out) const override;
    Transition handle(const UiEvent& ev) override;
    MenuMask mask() const override;
    void set_selection(RowIndex i) override;
    RowIndex selection() const override;

    std::uint32_t first_row() const noexcept override;
    bool empty() const override;

private:
    [[nodiscard]] std::uint32_t status_field(proto::BitField b) const;
    [[nodiscard]] std::string compose(const OptionItem& it) const;

    [[nodiscard]] RowCount osd_height() const noexcept { return kOsdRows; }

    const OptionItem* row_at(std::uint32_t row) const;

    template <class Visit>
    void for_each_drawn(Visit&& visit) const;

    void track_viewport() noexcept;

    IOptionsSurface& surface_;
    const OptionsModel& model_;
    RowIndex sel_{};

    std::uint32_t first_row_ = 0;
};

}
