// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "os/clock.h"
#include "ui/browser_session.h"
#include "ui/page.h"

namespace mister::ui {

char browser_ascii_key(std::uint32_t code) noexcept;

[[nodiscard]] std::string display_name(const BrowserEntry& e, bool cores);

[[nodiscard]] std::string hd_entry_label(const BrowserEntry& e, bool cores);

class BrowserPage final : public Page {
    TASTY_SEAT_RESIDENT(Ui);

public:
    BrowserPage(
        const IBrowseSource& src,
        BrowserSession::Request req,
        RowCount height,
        infra::OptRef<const os::IClock> clock
    ) noexcept
        : src_(&src),
          req_(static_cast<BrowserSession::Request&&>(req)),
          height_(height),
          clock_(clock) {}

    PageId id() const override {
        TASTY_SEAT_BODY(BrowserPage);
        return PageId::Browser;
    }

    void on_enter(PageContext& ctx) override;
    void render(OsdRenderer& out) const override;
    bool describe(app::PageDescription& out) const override;
    Transition handle(const UiEvent& ev) override;

    MenuMask mask() const override {
        TASTY_SEAT_BODY(BrowserPage);
        return MenuMask{};
    }

    RowIndex selection() const override {
        TASTY_SEAT_BODY(BrowserPage);
        return session_.view().viewport().selected;
    }

    void set_selection(RowIndex i) override {
        TASTY_SEAT_BODY(BrowserPage);
        session_.view().select(i);
    }

    RowCount osd_height() const override {
        TASTY_SEAT_BODY(BrowserPage);
        return height_;
    }

    bool back_is_idle() const override;

    BrowserSession& session() noexcept { return session_; }

    const BrowserSession& session() const noexcept { return session_; }

    std::uint32_t refusals() const noexcept override {
        TASTY_SEAT_BODY(BrowserPage);
        return refusals_ + session_.refusals();
    }

private:
    Transition select_current();

    Transition handle_recents(const UiEvent& ev);

    Transition open_recents_list();

    std::int64_t now_ns() const noexcept { return clock_ ? clock_->now().count() : 0; }

    BrowserSession session_{};
    const IBrowseSource* src_;
    BrowserSession::Request req_{};
    RowCount height_{16};
    infra::OptRef<const os::IClock> clock_;

    infra::OptRef<app::RecentsStore> recents_{};
    std::uint32_t refusals_ = 0;
};

}
