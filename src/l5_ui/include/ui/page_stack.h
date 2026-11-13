// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "ui/page.h"
#include "svc/types.h"

namespace mister::ui {

class PageStack {
    TASTY_SEAT_RESIDENT(Ui);

public:
    bool empty() const noexcept { return pages_.empty(); }

    std::size_t depth() const noexcept { return pages_.size(); }

    [[nodiscard]] StackDepth top_level() const noexcept {
        return StackDepth::of(pages_.size() - 1);
    }

    infra::OptRef<Page> top() noexcept { return infra::opt_ref_of(top_()); }

    infra::OptRef<const Page> top() const noexcept { return infra::opt_ref_of<const Page>(top_()); }

    PageId top_id() const noexcept { return pages_.empty() ? PageId::None : pages_.back()->id(); }

    PageId bottom_id() const noexcept {
        return pages_.empty() ? PageId::None : pages_.front()->id();
    }

    infra::OptRef<const Page> bottom() const noexcept {
        return infra::opt_ref_of<const Page>(pages_.empty() ? nullptr : pages_.front().get());
    }

    static constexpr std::size_t kBeneathTopFromEnd = 2;

    infra::OptRef<Page> beneath() noexcept { return infra::opt_ref_of(beneath_()); }

    infra::OptRef<const Page> beneath() const noexcept {
        return infra::opt_ref_of<const Page>(beneath_());
    }

    PageId beneath_top_id() const noexcept {
        const auto under = beneath();
        return under.has_value() ? under->id() : PageId::None;
    }

    void push(std::unique_ptr<Page> p);
    void pop();
    void replace(std::unique_ptr<Page> p);
    void clear() noexcept;

    svc::Tally page_refusals() const noexcept;

    bool any_bt_pairing() const noexcept;

    ConnectDemand connect_demand() const noexcept;

    bool any_runs_a_process() const noexcept;

    bool any_takes_focus() const noexcept;

    infra::OptRef<const Page> focus_top() const noexcept;

private:
    [[nodiscard]] Page* top_() noexcept { return pages_.empty() ? nullptr : pages_.back().get(); }

    [[nodiscard]] const Page* top_() const noexcept {
        return pages_.empty() ? nullptr : pages_.back().get();
    }

    [[nodiscard]] Page* beneath_() noexcept {
        return pages_.size() >= kBeneathTopFromEnd
            ? pages_[pages_.size() - kBeneathTopFromEnd].get()
            : nullptr;
    }

    [[nodiscard]] const Page* beneath_() const noexcept {
        return pages_.size() >= kBeneathTopFromEnd
            ? pages_[pages_.size() - kBeneathTopFromEnd].get()
            : nullptr;
    }

    std::vector<std::unique_ptr<Page>> pages_;
    svc::Tally retired_refusals_{};
};

}
