// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "app/config_slot_cell.h"
#include "app/page_description.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "app/ui_request_ring.h"
#include "ui/config_slots_mode.h"
#include "ui/page.h"
#include "svc/types.h"

namespace mister::ui {

struct SlotChoice {
    std::optional<cores::ConfigSlot> slot{};

    [[nodiscard]] constexpr bool back() const noexcept { return !slot.has_value(); }

    friend constexpr bool operator==(const SlotChoice&, const SlotChoice&) = default;
};

class ConfigSlotsPage final : public Page {
    TASTY_SEAT_RESIDENT(Ui);

public:
    using Mode = ConfigSlotsMode;

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

    bool walk(PageWalk& out) const override;
    Transition handle(const UiEvent& ev) override;

    [[nodiscard]] std::string row_text(proto::OsdRow row) const;
    [[nodiscard]] bool row_stippled(proto::OsdRow row) const noexcept;

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

    svc::Tally refusals() const noexcept override {
        TASTY_SEAT_BODY(ConfigSlotsPage);
        return refusals_;
    }

    std::uint32_t commands_published() const noexcept { return published_; }

private:
    void ask_slot(cores::ConfigSlot slot);
    [[nodiscard]] RowCount slot_count() const noexcept;
    [[nodiscard]] bool selectable(cores::ConfigSlot slot) const noexcept;

    [[nodiscard]] std::optional<cores::ConfigSlot> slot_at(proto::OsdRow row) const noexcept;

    [[nodiscard]] SlotChoice choice_of(RowIndex at) const noexcept;

    const app::ConfigSlotTable* table_;
    Mode mode_;
    proto::BindGeneration drawn_;
    infra::OptRef<app::UiRequestRing> asks_{};
    MenuMask mask_{};
    RowIndex sel_{};
    svc::Tally refusals_{};
    std::uint32_t published_ = 0;
};

}
