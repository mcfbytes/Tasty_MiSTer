// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>

#include "app/info_sink.h"
#include "infra/seat.h"
#include "ui/menu_model.h"
#include "ui/page_context.h"

namespace mister::fw {

class MenuInfoSink final : public app::InfoSink {
    TASTY_SEAT_RESIDENT(Ui);

public:
    MenuInfoSink(ui::MenuModel& menu, ui::PageContext& ctx) noexcept : menu_(menu), ctx_(ctx) {}

    void info(app::InfoId id, std::chrono::milliseconds = {}) override {
        TASTY_SEAT_BODY(MenuInfoSink);
        menu_.on_info(id, ctx_);
    }

    void progress(app::InfoId, cores::Progress p) override {
        TASTY_SEAT_BODY(MenuInfoSink);
        menu_.on_progress(p, ctx_);
    }

private:
    ui::MenuModel& menu_;
    ui::PageContext& ctx_;
};

}
