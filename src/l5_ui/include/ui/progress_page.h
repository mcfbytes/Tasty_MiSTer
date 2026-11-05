// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string_view>

#include "app/page_description.h"
#include "infra/seat.h"
#include "ui/page.h"

namespace mister::ui {

inline constexpr unsigned kProgressNameRow = 3;
inline constexpr unsigned kProgressBarRow = 4;

inline constexpr std::size_t kProgressNameMax = 27;

std::string_view progress_leaf(std::string_view path) noexcept;

class ProgressPage final : public Page, public IProgressView {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit ProgressPage(std::string_view name) noexcept;

    PageId id() const override {
        TASTY_SEAT_BODY(ProgressPage);
        return PageId::Progress;
    }

    PageFacts facts() const override {
        TASTY_SEAT_BODY(ProgressPage);
        PageFacts f{};
        f.osd_rows = RowCount{8};
        f.auto_pop = std::chrono::milliseconds{10000};
        f.takes_input_focus = false;
        f.allows_popups = true;
        return f;
    }

    MenuMask mask() const override {
        TASTY_SEAT_BODY(ProgressPage);
        return MenuMask{};
    }

    IProgressView* progress_view() override {
        TASTY_SEAT_BODY(ProgressPage);
        return this;
    }

    void set_progress(cores::Progress progress) noexcept override;

    bool walk(PageWalk&) const override {
        TASTY_SEAT_BODY(ProgressPage);
        return false;
    }

    void render(OsdRenderer& out) const override;

    bool describe(app::PageDescription& out) const override;

    Transition handle(const UiEvent&) override {
        TASTY_SEAT_BODY(ProgressPage);
        return Transition{};
    }

private:
    char name_[kProgressNameMax]{};
    std::uint8_t name_len_ = 0;
    std::uint8_t step_ = 0;

    cores::Progress last_{};
};

std::unique_ptr<Page> make_progress_page(std::string_view name);

}
