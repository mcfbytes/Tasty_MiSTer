// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "ui/browser_host.h"
#include "ui/page.h"

namespace mister::app {
class ICmdVerbSink;
class RecentsStore;
}

namespace mister::ui {

struct BrowserEntry;

class CorePickHost final : public IBrowserHost {
    TASTY_SEAT_RESIDENT(Ui);

public:
    struct Borrows {
        infra::OptRef<app::ICmdVerbSink> route{};
        infra::OptRef<app::RecentsStore> recents{};
    };

    void bind(Borrows b) noexcept;

    Transition on_picked(const BrowserEntry& e, std::string_view path) override;
    Transition on_cancel() override;

    std::uint32_t refusals() const noexcept { return refusals_; }

    std::uint32_t commands_published() const noexcept { return published_; }

private:
    [[nodiscard]] bool deliver_pick_(std::string_view path);

    Borrows b_{};
    std::uint32_t refusals_ = 0;
    std::uint32_t published_ = 0;
};

}
