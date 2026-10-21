// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
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

    bool allows_popups() const override {
        TASTY_SEAT_BODY(ProgressPage);
        return true;
    }

    bool counts_as_menu_present() const override {
        TASTY_SEAT_BODY(ProgressPage);
        return true;
    }

    bool takes_input_focus() const override {
        TASTY_SEAT_BODY(ProgressPage);
        return false;
    }

    MenuMask mask() const override {
        TASTY_SEAT_BODY(ProgressPage);
        return MenuMask{};
    }

    IProgressView* progress_view() override {
        TASTY_SEAT_BODY(ProgressPage);
        return this;
    }

    void set_progress(std::uint16_t cur, std::uint16_t max) noexcept override;

    void render(OsdRenderer& out) const override;

    bool describe(app::PageDescription& out) const override;

    Transition handle(const UiEvent&) override {
        TASTY_SEAT_BODY(ProgressPage);
        return Transition{};
    }

    RowCount osd_height() const override {
        TASTY_SEAT_BODY(ProgressPage);
        return RowCount{8};
    }

    std::uint32_t auto_pop_ms() const override {
        TASTY_SEAT_BODY(ProgressPage);
        return 10000;
    }

private:
    char name_[kProgressNameMax]{};
    std::uint8_t name_len_ = 0;
    std::uint8_t step_ = 0;

    std::uint16_t cur_ = 0;
    std::uint16_t max_ = 0;
};

std::unique_ptr<Page> make_progress_page(std::string_view name);

}
