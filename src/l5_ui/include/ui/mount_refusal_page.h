// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

#include "app/hd/page_description.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "svc/mount_refusal.h"
#include "ui/page.h"

namespace mister::ui {

class MountRefusalPage final : public Page {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit MountRefusalPage(svc::MountRefusal reason) noexcept : reason_(reason) {}

    PageId id() const override {
        TASTY_SEAT_BODY(MountRefusalPage);
        return PageId::Info;
    }

    PageFacts facts() const override;

    MenuMask mask() const override {
        TASTY_SEAT_BODY(MountRefusalPage);
        return MenuMask{};
    }

    bool walk(PageWalk&) const override {
        TASTY_SEAT_BODY(MountRefusalPage);
        return false;
    }

    void render(OsdRenderer& out) const override;
    bool describe(app::hd::PageDescription& out) const override;

    Transition handle(const UiEvent& ev) override;

private:
    svc::MountRefusal reason_;
};

std::unique_ptr<Page> make_mount_refusal_page(svc::MountRefusal reason);

}
