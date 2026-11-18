// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include "infra/indexed_array.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "ui/cores_entry.h"
#include "ui/page.h"
#include "ui/page_context.h"
#include "ui/scripts_entry.h"
#include "ui/system_facts.h"
#include "svc/types.h"

namespace mister::app {
class ICmdVerbSink;
class UiRequestRing;
}

#include "ui/reboot_hold.h"

namespace mister::ui {

class IBrowseSource;
struct BrowserLaunch;

enum class SystemItem : std::uint8_t {
    Core = 0,
    SwitchStorage = 1,
    RemapKeyboard = 2,
    DefineJoystick = 3,
    Scripts = 4,
    Help = 5,
    SaveSettings = 6,
    Reboot = 7,
    Autosave = 8,
    UseHd = 9,
    Exit = 10,

    kCount,
};

inline constexpr std::size_t kSystemItemTotal = std::to_underlying(SystemItem::kCount);
inline constexpr infra::IndexedArray<SystemItem, kSystemItemTotal, RowIndex> kSystemItems = [] {
    constexpr std::array<SystemItem, kSystemItemTotal> order{
        SystemItem::Core,
        SystemItem::SwitchStorage,
        SystemItem::RemapKeyboard,
        SystemItem::DefineJoystick,
        SystemItem::Scripts,
        SystemItem::Help,
        SystemItem::SaveSettings,
        SystemItem::Reboot,
        SystemItem::Autosave,
        SystemItem::UseHd,
        SystemItem::Exit
    };
    infra::IndexedArray<SystemItem, kSystemItemTotal, RowIndex> t;
    for (std::size_t i = 0; i < kSystemItemTotal; ++i)
        t[RowIndex::of(i)] = order[i];
    return t;
}();
inline constexpr RowCount kSystemItemCount = RowCount::of(kSystemItemTotal);

[[nodiscard]] constexpr std::optional<SystemItem> system_item_of(RowIndex row) noexcept {
    const auto item = kSystemItems.find(row);
    if (!item)
        return std::nullopt;
    return *item;
}

[[nodiscard]] constexpr RowIndex system_row_of(SystemItem it) noexcept {
    for (std::size_t i = 0; i < kSystemItemTotal; ++i)
        if (kSystemItems[RowIndex::of(i)] == it)
            return RowIndex::of(i);
    return RowIndex::of(kSystemItemTotal);
}

consteval bool system_items_are_dense() {
    for (std::size_t i = 0; i < kSystemItemTotal; ++i)
        if (system_row_of(kSystemItems[RowIndex::of(i)]) != RowIndex::of(i))
            return false;
    return !system_item_of(RowIndex::of(kSystemItemTotal)).has_value();
}

static_assert(system_items_are_dense());

inline constexpr std::string_view kAutosaveLabel = "Auto-save to SD";
inline constexpr std::string_view kAutosaveOn = "On";
inline constexpr std::string_view kAutosaveOff = "Off";

[[nodiscard]] constexpr std::string_view autosave_word(bool on) noexcept {
    return on ? kAutosaveOn : kAutosaveOff;
}

class SystemPage final : public Page {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit SystemPage(const SystemFacts& f, const SessionFacts& s) noexcept
        : facts_(f),
          session_(s),
          scripts_(s) {}

    PageId id() const override {
        TASTY_SEAT_BODY(SystemPage);
        return PageId::System;
    }

    MenuMask mask() const override {
        TASTY_SEAT_BODY(SystemPage);
        MenuMask m = dense_mask(kSystemItemCount);
        if (!session_.core_loaded)
            m = m.without(system_row_of(SystemItem::SaveSettings));
        if (!session_.shows_hd_switch())
            m = m.without(system_row_of(SystemItem::UseHd));
        return m;
    }

    void set_selection(RowIndex i) override {
        TASTY_SEAT_BODY(SystemPage);
        sel_ = i;
    }

    RowIndex selection() const override {
        TASTY_SEAT_BODY(SystemPage);
        return sel_;
    }

    PageFacts facts() const override {
        TASTY_SEAT_BODY(SystemPage);
        PageFacts f{};
        f.allows_cfg_switch = true;

        f.repeat_all_keys = item_selected(SystemItem::Reboot);
        return f;
    }

    void on_enter(PageContext& ctx) override;

    bool walk(PageWalk& out) const override;
    Transition handle(const UiEvent& ev) override;

    Transition open_root_child(PageContext&) override;

    void on_facts(const SystemFacts& f, const SessionFacts& s) override {
        TASTY_SEAT_BODY(SystemPage);
        facts_ = f;
        session_ = s;
        scripts_.on_facts(s);
    }

    svc::Tally refusals() const noexcept override {
        TASTY_SEAT_BODY(SystemPage);
        return refusals_.sum(scripts_.refusals()).sum(cores_.refusals());
    }

private:
    bool item_selected(SystemItem it) const noexcept { return sel_ == system_row_of(it); }

    template <class Visit>
    void for_each_row(Visit&& visit) const;

    Transition open_scripts_();
    Transition open_cores_(bool recents_first);
    Transition reboot_release(const UiEvent& ev);
    Transition use_hd_();
    Transition toggle_autosave_();
    [[nodiscard]] bool ask_reboot_();

    SystemFacts facts_{};
    SessionFacts session_{};
    RowIndex sel_{};
    svc::Tally refusals_{};
    std::uint32_t published_ = 0;

    RebootHold reboot_{};

    infra::OptRef<PageContext> ctx_{};
    infra::OptRef<app::ICmdVerbSink> route_{};
    infra::OptRef<app::UiRequestRing> ui_requests_{};
    ScriptsEntry scripts_;
    CoresEntry cores_{};
};

std::unique_ptr<SystemPage> make_system_page(const SystemFacts& facts, const SessionFacts& session);

OsdRow system_item_row(SystemItem it) noexcept;

enum class SystemRow : std::uint8_t {
    Title,
    Core,
    Remap,
    Joystick,
    Scripts,
    Help,
    Save,
    Reboot,
    UseHd,
    Exit,
};
const char* system_row_text(SystemRow r) noexcept;

}
