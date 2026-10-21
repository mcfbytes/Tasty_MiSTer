// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "ui/browser_host.h"
#include "ui/page.h"

namespace mister::ui {

struct BrowserEntry;
class IBrowseSource;
struct ScriptLaunch;
struct PageLaunches;

class ScriptPickHost final : public IBrowserHost {
    TASTY_SEAT_RESIDENT(Ui);

public:
    struct Borrows {
        infra::OptRef<PageLaunches> launches{};
        infra::OptRef<const IBrowseSource> storage{};
    };

    void bind(Borrows b) noexcept;

    Transition on_picked(const BrowserEntry& e, std::string_view path) override;
    Transition on_cancel() override;

    std::uint32_t refusals() const noexcept { return refusals_; }

private:
    Borrows b_{};
    std::uint32_t refusals_ = 0;
};

}
