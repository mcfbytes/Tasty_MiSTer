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
#include "ui/confirm_host.h"
#include "ui/core_pick_host.h"
#include "ui/menu_model.h"
#include "ui/page.h"
#include "ui/page_launches.h"
#include "ui/script_pick_host.h"
#include "ui/system_facts.h"

namespace mister::app {
class ICmdVerbSink;
class NameConfig;
class RecentsStore;
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
    UseHd = 8,
    Exit = 9,

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

class SystemPage final : public Page, private IConfirmHost {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit SystemPage(const SystemFacts& f, const SessionFacts& s) noexcept
        : facts_(f),
          session_(s) {}

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

    Transition open_scripts(PageContext&);

    void on_facts(const SystemFacts& f, const SessionFacts& s) override {
        TASTY_SEAT_BODY(SystemPage);
        facts_ = f;
        session_ = s;
    }

    std::uint32_t refusals() const noexcept override {
        TASTY_SEAT_BODY(SystemPage);
        return refusals_ + script_pick_.refusals() + core_pick_.refusals();
    }

private:
    infra::OptRef<BrowserLaunch> browser_() const noexcept {
        return launches_ ? launches_->browser : infra::OptRef<BrowserLaunch>{};
    }

    bool item_selected(SystemItem it) const noexcept { return sel_ == system_row_of(it); }

    template <class Visit>
    void for_each_row(Visit&& visit) const;

    Transition open_scripts_confirm();
    Transition open_scripts_browser(Transition::Kind kind);
    Transition open_core_browser(bool recents_first);
    Transition reboot_release(const UiEvent& ev);
    Transition use_hd_();
    [[nodiscard]] bool ask_reboot_();

    Transition on_cancel() override {
        TASTY_SEAT_BODY(SystemPage);
        return Transition{Transition::Kind::Pop};
    }

    Transition on_confirm(RowIndex choice) override;

    SystemFacts facts_{};
    SessionFacts session_{};
    RowIndex sel_{};
    std::uint32_t refusals_ = 0;
    std::uint32_t published_ = 0;

    RebootHold reboot_{};

    infra::OptRef<PageLaunches> launches_{};
    infra::OptRef<const IBrowseSource> storage_{};
    infra::OptRef<app::ICmdVerbSink> route_{};
    infra::OptRef<app::UiRequestRing> ui_requests_{};
    infra::OptRef<app::RecentsStore> recents_{};
    infra::OptRef<app::NameConfig> names_{};
    ScriptPickHost script_pick_{};
    CorePickHost core_pick_{};
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
