// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "infra/opt_ref.h"
#include "ui/page_id.h"
#include "ui/transition.h"
#include "ui/types.h"

namespace mister::proto {
class ItemTable;
}

namespace mister::app {
struct PageDescription;
enum class RowTier : std::uint8_t;
}

namespace mister::ui {

struct UiEvent;

class OsdRenderer;
struct PageContext;
struct BuildEnv;

class IProgressView {
public:
    virtual ~IProgressView() = default;
    virtual void set_progress(std::uint16_t cur, std::uint16_t max) noexcept = 0;

protected:
    IProgressView() = default;
    IProgressView(const IProgressView&) = default;
    IProgressView& operator=(const IProgressView&) = default;
};

class IMaskRebuild {
public:
    virtual ~IMaskRebuild() = default;
    virtual void rebuild(const proto::ItemTable& table, const BuildEnv& env) = 0;
    virtual std::uint8_t page() const noexcept = 0;

protected:
    IMaskRebuild() = default;
    IMaskRebuild(const IMaskRebuild&) = default;
    IMaskRebuild& operator=(const IMaskRebuild&) = default;
};

class Page {
public:
    virtual ~Page() = default;

    virtual PageId id() const = 0;

    virtual void on_enter(PageContext&) {}

    virtual void render(OsdRenderer&) const = 0;

    virtual bool describe(app::PageDescription&) const { return false; }

    virtual bool describe_hub(app::PageDescription&) const { return false; }

    virtual Transition handle(const UiEvent&) = 0;

    virtual void on_facts(const struct SystemFacts&) {}

    virtual class IProgressView* progress_view() { return nullptr; }

    virtual infra::OptRef<IMaskRebuild> mask_rebuild() { return {}; }

    virtual bool holds_pairing_hotkey() const { return false; }

    virtual Transition on_tick(PageContext&) { return Transition{}; }

    virtual Transition open_root_child(PageContext&) { return Transition{}; }

    virtual Transition open_scripts(PageContext&) { return Transition{}; }

    virtual bool wake_every_round() const { return false; }

    virtual MenuMask mask() const { return MenuMask{}; }

    virtual void set_selection(RowIndex) {}

    virtual RowIndex selection() const { return RowIndex{}; }

    struct TierRun {
        app::RowTier tier;
        MenuMask rows;
    };

    virtual std::optional<TierRun> tier_run(RowIndex at) const {
        (void)at;
        return std::nullopt;
    }

    virtual RowCount osd_height() const { return RowCount{16}; }

    virtual bool has_title_strip() const { return true; }

    virtual std::uint32_t auto_pop_ms() const { return 0; }

    virtual bool takes_input_focus() const { return true; }

    virtual bool allows_global_hotkeys() const { return true; }

    virtual bool allows_popups() const { return false; }

    virtual bool counts_as_menu_present() const { return true; }

    virtual bool allows_cfg_switch() const { return false; }

    virtual bool bt_pairing_active() const { return false; }

    virtual bool runs_a_process() const { return false; }

    virtual bool back_is_idle() const { return false; }

    virtual std::uint32_t refusals() const noexcept { return 0; }

protected:
    Page() = default;
    Page(const Page&) = default;
    Page& operator=(const Page&) = default;
};

}
