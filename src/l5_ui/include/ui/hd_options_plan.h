// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "infra/seat.h"
#include "ui/hd_options_view.h"
#include "ui/hd_plan.h"
#include "ui/hd_rows/hd_push_row.h"
#include "ui/hd_rows/hd_save_row.h"
#include "ui/hd_rows/hd_separator_row.h"
#include "ui/placer.h"

namespace mister::ui {

class IOptionsSurface;
class OptionItem;

class HdOptionsPlan final : public IHdPlan {
    TASTY_SEAT_RESIDENT(Ui);

public:
    HdOptionsPlan(IOptionsSurface& surface, const HdOptionsView::Spec& spec);
    HdOptionsPlan(const HdOptionsPlan&) = delete;
    HdOptionsPlan& operator=(const HdOptionsPlan&) = delete;

    PageId id() const override;
    std::string_view title() const override;
    std::span<const std::reference_wrapper<IRow>> rows() const override;
    RowCount pinned() const noexcept override;
    RowCx row_cx() const override;

private:
    [[nodiscard]] bool acts_(const OptionItem& it) const;
    IRow& separator_();

    IOptionsSurface& surface_;
    Placer placer_;
    std::vector<std::unique_ptr<HdSeparatorRow>> separators_;
    HdSaveRow save_;
    HdPushRow about_;
    std::vector<std::reference_wrapper<IRow>> refs_;
    std::uint8_t pinned_ = 0;
};

}
