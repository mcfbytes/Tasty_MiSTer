// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include "infra/seat.h"
#include "ui/options_view.h"
#include "ui/placer.h"

namespace mister::cores {
struct FileSlot;
}

namespace mister::ui {

class HdFocusMemory;
class HdListPage;
class IOptionsSurface;
class SavedBaseline;
struct CoreVerbs;

class HdOptionsView final : public IOptionsView {
    TASTY_SEAT_RESIDENT(Ui);

public:
    struct Spec {
        std::span<const cores::FileSlot> slots;
        const CoreVerbs& verbs;
        PlaceFacts facts;
        HdFocusMemory& memory;

        const SavedBaseline& baseline;
    };

    HdOptionsView(IOptionsSurface& surface, const Spec& spec) noexcept;
    ~HdOptionsView() override;
    HdOptionsView(const HdOptionsView&) = delete;
    HdOptionsView& operator=(const HdOptionsView&) = delete;

    PageId id() const override;

    BuildEnv walk_env(const BuildEnv& env) const override;

    void on_walk() override;

    void render(OsdRenderer& out) const override;
    bool describe(app::PageDescription& out) const override;
    Transition handle(const UiEvent& ev) override;
    MenuMask mask() const override;
    void set_selection(RowIndex i) override;
    RowIndex selection() const override;
    std::uint32_t first_row() const noexcept override;
    bool empty() const override;

    std::uint32_t refusals() const noexcept override;

private:
    [[nodiscard]] std::unique_ptr<HdListPage> make_list_() const;

    IOptionsSurface& surface_;
    Spec spec_;
    std::unique_ptr<HdListPage> list_;
    std::uint32_t retired_refusals_ = 0;
};

}
