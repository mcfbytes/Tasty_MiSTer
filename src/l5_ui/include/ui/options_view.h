// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "ui/build_env.h"
#include "ui/page_id.h"
#include "ui/transition.h"
#include "ui/types.h"

namespace mister::app {
struct PageDescription;
}

namespace mister::ui {

class OsdRenderer;
struct UiEvent;

class IOptionsView {
public:
    virtual ~IOptionsView() = default;

    [[nodiscard]] virtual PageId id() const = 0;

    [[nodiscard]] virtual BuildEnv walk_env(const BuildEnv& env) const = 0;

    virtual void on_walk() = 0;

    virtual void render(OsdRenderer& out) const = 0;
    virtual bool describe(app::PageDescription& out) const = 0;
    virtual Transition handle(const UiEvent& ev) = 0;

    [[nodiscard]] virtual MenuMask mask() const = 0;
    virtual void set_selection(RowIndex i) = 0;
    [[nodiscard]] virtual RowIndex selection() const = 0;

    [[nodiscard]] virtual std::uint32_t first_row() const noexcept = 0;

    [[nodiscard]] virtual bool empty() const = 0;

    [[nodiscard]] virtual std::uint32_t refusals() const noexcept { return 0; }

protected:
    IOptionsView() = default;
    IOptionsView(const IOptionsView&) = default;
    IOptionsView& operator=(const IOptionsView&) = default;
};

}
