// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

#include "app/config_slot_cell.h"
#include "app/page_description.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "app/ui_request_ring.h"
#include "ui/page.h"

namespace mister::ui {

class ConfigSlotsPage final : public Page {
    TASTY_SEAT_RESIDENT(Ui);

public:
    enum class Mode : std::uint8_t { Load, Save };

    ConfigSlotsPage(
        const app::ConfigSlotTable& table,
        Mode mode,
        proto::BindGeneration drawn
    ) noexcept
        : table_(&table),
          mode_(mode),
          drawn_(drawn) {}

    PageId id() const override {
        TASTY_SEAT_BODY(ConfigSlotsPage);
        return PageId::ConfigSlots;
    }

    Mode mode() const noexcept { return mode_; }

    void on_enter(PageContext& ctx) override;
    void render(OsdRenderer& out) const override;

    bool describe(app::PageDescription& out) const override;
    Transition handle(const UiEvent& ev) override;

    [[nodiscard]] std::string row_text(std::uint32_t row) const;
    [[nodiscard]] bool row_stippled(std::uint32_t row) const noexcept;

    MenuMask mask() const override {
        TASTY_SEAT_BODY(ConfigSlotsPage);
        return mask_;
    }

    void set_selection(RowIndex i) override {
        TASTY_SEAT_BODY(ConfigSlotsPage);
        sel_ = i;
    }

    RowIndex selection() const override {
        TASTY_SEAT_BODY(ConfigSlotsPage);
        return sel_;
    }

    std::uint32_t refusals() const noexcept override {
        TASTY_SEAT_BODY(ConfigSlotsPage);
        return refusals_;
    }

    std::uint32_t commands_published() const noexcept { return published_; }

private:
    static constexpr std::uint32_t kNoSlot = 0xFFFF'FFFFu;

    void ask_slot(cores::ConfigSlot slot);
    [[nodiscard]] std::uint32_t slot_count() const noexcept;
    [[nodiscard]] bool selectable(std::uint32_t slot) const noexcept;
    [[nodiscard]] std::uint32_t slot_at(std::uint32_t row) const noexcept;

    const app::ConfigSlotTable* table_;
    Mode mode_;
    proto::BindGeneration drawn_;
    infra::OptRef<app::UiRequestRing> asks_{};
    MenuMask mask_{};
    RowIndex sel_{};
    std::uint32_t refusals_ = 0;
    std::uint32_t published_ = 0;
};

}
