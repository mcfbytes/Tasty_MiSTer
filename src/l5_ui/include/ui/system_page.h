// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

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
};

[[nodiscard]] constexpr std::optional<SystemItem> system_item_from_raw(std::uint32_t raw) noexcept {
    switch (raw) {
        case 0:
            return SystemItem::Core;
        case 1:
            return SystemItem::SwitchStorage;
        case 2:
            return SystemItem::RemapKeyboard;
        case 3:
            return SystemItem::DefineJoystick;
        case 4:
            return SystemItem::Scripts;
        case 5:
            return SystemItem::Help;
        case 6:
            return SystemItem::SaveSettings;
        case 7:
            return SystemItem::Reboot;
        case 8:
            return SystemItem::UseHd;
        case 9:
            return SystemItem::Exit;
        default:
            return std::nullopt;
    }
}

class SystemPage final : public Page, private IConfirmHost {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit SystemPage(const SystemFacts& f) noexcept : facts_(f) {}

    PageId id() const override {
        TASTY_SEAT_BODY(SystemPage);
        return PageId::System;
    }

    MenuMask mask() const override {
        TASTY_SEAT_BODY(SystemPage);
        std::uint64_t m = 0x3FFu;
        if (!facts_.core_loaded) {
            m &= ~(std::uint64_t{1} << static_cast<unsigned>(SystemItem::SaveSettings));
        }
        if (!facts_.hd_switchable) {
            m &= ~(std::uint64_t{1} << static_cast<unsigned>(SystemItem::UseHd));
        }
        return MenuMask{m};
    }

    void set_selection(RowIndex i) override {
        TASTY_SEAT_BODY(SystemPage);
        sel_ = i;
    }

    RowIndex selection() const override {
        TASTY_SEAT_BODY(SystemPage);
        return sel_;
    }

    RowCount osd_height() const override {
        TASTY_SEAT_BODY(SystemPage);
        return RowCount{16};
    }

    bool allows_cfg_switch() const override {
        TASTY_SEAT_BODY(SystemPage);
        return true;
    }

    void on_enter(PageContext& ctx) override;
    void render(OsdRenderer& out) const override;
    bool describe(app::PageDescription& out) const override;
    Transition handle(const UiEvent& ev) override;

    void on_facts(const SystemFacts& f) override {
        TASTY_SEAT_BODY(SystemPage);
        facts_ = f;
    }

    std::uint32_t refusals() const noexcept override {
        TASTY_SEAT_BODY(SystemPage);
        return refusals_ + script_pick_.refusals() + core_pick_.refusals();
    }

private:
    infra::OptRef<BrowserLaunch> browser_() const noexcept {
        return launches_ ? launches_->browser : infra::OptRef<BrowserLaunch>{};
    }

    bool item_selected(SystemItem it) const noexcept {
        return sel_.v == static_cast<std::uint32_t>(it);
    }

    template <class Visit>
    void for_each_row(Visit&& visit) const;

    void render_row(
        OsdRenderer& out,
        std::uint8_t row,
        std::optional<SystemItem> item,
        std::string_view text,
        bool stippled
    ) const;

    Transition open_scripts_confirm();
    Transition open_scripts_browser(Transition::Kind kind);
    Transition open_core_browser(bool recents_first);
    Transition open_root_child(PageContext&) override;
    Transition open_scripts(PageContext&) override;
    Transition reboot_release(const UiEvent& ev);
    Transition use_hd_();
    [[nodiscard]] bool ask_reboot_();

    Transition on_cancel() override {
        TASTY_SEAT_BODY(SystemPage);
        return Transition{Transition::Kind::Pop};
    }

    Transition on_confirm(std::uint8_t choice) override;

    SystemFacts facts_{};
    RowIndex sel_{};
    std::uint32_t refusals_ = 0;
    std::uint32_t published_ = 0;

    bool reboot_req_ = false;
    bool cold_sent_ = false;
    std::uint8_t reboot_off_ = 0;

    infra::OptRef<PageLaunches> launches_{};
    infra::OptRef<const IBrowseSource> storage_{};
    infra::OptRef<app::ICmdVerbSink> route_{};
    infra::OptRef<app::UiRequestRing> ui_requests_{};
    infra::OptRef<app::RecentsStore> recents_{};
    infra::OptRef<app::NameConfig> names_{};
    ScriptPickHost script_pick_{};
    CorePickHost core_pick_{};
};

std::unique_ptr<Page> make_system_page(const SystemFacts& facts);

std::uint8_t system_item_row(SystemItem it) noexcept;

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
