// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

#include "app/conf_str_cell.h"
#include "app/cheat_catalog_cell.h"
#include "app/config_slot_cell.h"
#include "app/option_cell.h"
#include "app/osd_close.h"
#include "app/cheat_store.h"
#include "app/dip_cell.h"
#include "app/launcher_state.h"
#include "app/event.h"
#include "app/hd_osd_status.h"
#include "app/recent_games.h"
#include "app/session_identity.h"
#include "proto/status_cell.h"
#include "app/tx_digest_cell.h"
#include "proto/conf_str.h"
#include "proto/core_session.h"
#include "proto/item_probe.h"
#include "proto/link_op.h"
#include "ui/core_cell_mirror.h"
#include "ui/core_verbs.h"
#include "ui/hd_focus_memory.h"
#include "ui/hd_key_remap.h"
#include "ui/menu_core_binding.h"
#include "ui/navigator.h"
#include "ui/now_playing.h"
#include "ui/saved_baseline.h"
#include "ui/osd_style_preference.h"
#include "ui/page.h"
#include "ui/page_stack.h"
#include "ui/progress_page.h"
#include "ui/status_view.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"

namespace mister::app {
class IdentityLatch;
class UiRequestRing;
struct Event;
}

namespace mister::ui {

class IHdPlan;
class OsdRenderer;
struct BuildEnv;
struct HdPlanInputs;
struct PageContext;

class MenuModel : public app::IOsdClose {
    TASTY_SEAT_RESIDENT(Ui);

public:
    bool pump(PageContext& ctx, OsdRenderer& out);

    void on_core_loaded(proto::CoreType type, bool front_end, PageContext& ctx);

    void on_hd_mask(HdMask m, bool front_end, infra::OptRef<app::UiRequestRing> asks);
    void on_info(app::InfoId id, PageContext& ctx);
    void on_progress(std::uint16_t cur, std::uint16_t max, PageContext& ctx);

    void on_request_done(proto::CorrelationTag tag, std::int64_t unix_time, PageContext& ctx);

    void on_request_refused(proto::CorrelationTag tag, PageContext& ctx);

    void on_core_load_done(proto::CorrelationTag tag, PageContext& ctx);

    const NowPlaying& now_playing() const noexcept { return now_playing_; }

    void close_osd() noexcept override;

    std::uint32_t osd_close_refusals() const noexcept { return close_refusals_; }

    PageStack& stack() noexcept { return stack_; }

    const PageStack& stack() const noexcept { return stack_; }

    Navigator& navigator() noexcept { return nav_; }

    const HdKeyRemap& key_remap() const noexcept { return remap_; }

    HdFocusMemory& hd_focus() noexcept { return hd_focus_; }

    const HdFocusMemory& hd_focus() const noexcept { return hd_focus_; }

    void open_root(PageContext& ctx);

    [[nodiscard]] bool open_classic_root(PageContext& ctx);

    void open_bt_pairing(PageContext& ctx);

    std::uint32_t bt_refusals() const noexcept { return bt_refusals_; }

    const app::RecentGames& recent_games() const noexcept { return recent_games_; }

    bool bt_pairing_active() const noexcept { return stack_.any_bt_pairing(); }

    void set_item_table(proto::ItemTable t);

    const proto::ItemTable& item_table() const noexcept { return items_; }

    struct Wiring {
        const app::ConfStrCell& conf_str;
        const proto::StatusCell& status;
        const app::DipCell& dip;
        const app::OptionCell& option;
        const app::LauncherStateCell& launcher;
        const app::ConfigSlotCell& config_slot;
        const app::TxDigestCell& tx_digest;
        const app::CheatCatalogCell& cheat_catalog;
        app::CheatStore& cheats;
        const app::IdentityLatch& identity;
    };

    explicit MenuModel(Wiring w) noexcept
        : mirror_(
              CoreCellMirror::Wiring{
                  w.conf_str,
                  w.status,
                  w.dip,
                  w.option,
                  w.config_slot,
                  w.tx_digest,
                  w.cheat_catalog,
                  w.cheats,
                  w.identity
              }
          ),
          launcher_cell_(w.launcher),
          cheats_(w.cheats),
          identity_(w.identity) {}

    void forget_cells() noexcept { mirror_.forget(); }

    std::uint32_t cheat_opens() const noexcept { return mirror_.cheat_opens(); }

    std::uint32_t cheat_open_failures() const noexcept { return mirror_.cheat_open_failures(); }

    const app::DipTable& dip_table() const noexcept { return mirror_.dip_table(); }

    const app::OptionTable& option_table() const noexcept { return mirror_.option_table(); }

    const app::ConfigSlotTable& config_slot_table() const noexcept {
        return mirror_.config_slot_table();
    }

    std::uint32_t conf_str_reparses() const noexcept { return mirror_.conf_reparses(); }

    std::uint32_t conf_str_parse_failures() const noexcept { return mirror_.conf_failures(); }

    std::uint32_t conf_str_truncations() const noexcept { return mirror_.conf_truncations(); }

    std::uint32_t status_cell_reads() const noexcept { return mirror_.status_reads(); }

    [[nodiscard]] bool cells_pending() const noexcept;

    std::uint32_t cell_refusals() const noexcept { return mirror_.cell_refusals(); }

    void set_status_words(StatusView s) noexcept;

    const StatusView& status_words() const noexcept { return mirror_.status_words(); }

    void set_flat(bool on) noexcept { flat_ = on; }

    bool flat() const noexcept { return flat_; }

    BuildEnv build_env(std::uint8_t page) const noexcept;

    bool core_options_available() const;

    PageId resume_page() const noexcept { return resume_; }

    void set_resume_page(PageId p) noexcept { resume_ = p; }

    bool popups_visible() const noexcept;

    bool menu_present() const noexcept;

    bool allow_cfg_switch() const;

    bool osd_open() const noexcept { return !stack_.empty(); }

    bool menu_has_focus() const noexcept { return stack_.any_takes_focus(); }

    proto::BindGeneration conf_gen() const noexcept { return mirror_.conf_gen(); }

    [[nodiscard]] app::HdOsdStatus::Badge pause_badge() const noexcept;

    proto::LinkOp::OsdShow osd_show() const noexcept {
        if (!osd_open())
            return proto::LinkOp::OsdShow::Off;
        return menu_has_focus() ? proto::LinkOp::OsdShow::Menu : proto::LinkOp::OsdShow::Overlay;
    }

    std::uint32_t overlay_trades() const noexcept { return overlay_trades_; }

    std::uint32_t progress_masked() const noexcept { return progress_masked_; }

    bool global_hotkeys_allowed() const noexcept;

    void set_core_type(proto::CoreType t) noexcept { binding_.set_core_type(t); }

    proto::CoreType core_type() const noexcept { return binding_.core_type(); }

    bool ui_capable() const noexcept { return binding_.ui_capable(); }

    void set_front_end(bool on) noexcept { binding_.set_front_end(on); }

    bool front_end() const noexcept { return binding_.front_end(); }

    void set_hd_root(bool on) noexcept { hd_root_ = on; }

    bool hd_root() const noexcept { return hd_root_; }

    void set_osd_style(bool hd) noexcept { style_ = OsdStylePreference{hd}; }

    OsdStylePreference& style() noexcept { return style_; }

    const OsdStylePreference& style() const noexcept { return style_; }

    bool hd_tree() const noexcept;

    static constexpr std::size_t kHdWindowDepths = 16;

    bool describe_top(app::PageDescription& out);

    void request_repaint() noexcept { repaint_ = true; }

    bool repaint_pending() const noexcept { return repaint_; }

    bool status_due(std::int64_t now_ns) const noexcept {
        return binding_.front_end() && binding_.ui_capable() && now_ns >= status_due_ns_;
    }

    bool deadline_due(std::int64_t now_ns) const noexcept {
        return status_due(now_ns) || (auto_pop_at_ns_ != 0 && now_ns >= auto_pop_at_ns_);
    }

    std::uint32_t auto_pops() const noexcept { return auto_pops_; }

    void invalidate_surface() noexcept {
        invalidate_ = true;
        repaint_ = true;
    }

    std::uint32_t surface_invalidations() const noexcept { return invalidations_; }

    std::uint32_t show_edges() const noexcept { return show_edges_; }

    HdMask hd_mask() const noexcept { return hd_mask_; }

    std::uint16_t sdram_cfg() const noexcept { return sdram_cfg_; }

    std::uint32_t sdram_ask_refusals() const noexcept { return sdram_ask_refusals_; }

    std::uint32_t pump_calls() const noexcept { return pumps_; }

    std::uint32_t root_arms_refused() const noexcept { return root_refusals_; }

    std::uint32_t bootcore_cancels() const noexcept { return bootcore_cancels_; }

    std::uint32_t help_rearms() const noexcept { return help_rearms_; }

    std::uint32_t status_bar_paints() const noexcept { return status_paints_; }

    std::uint32_t info_refusals() const noexcept { return info_refusals_; }

    std::uint32_t progress_refusals() const noexcept { return progress_refusals_; }

    std::uint32_t page_refusals() const noexcept { return stack_.page_refusals(); }

private:
    void open_root_(PageContext& ctx, bool hd_allowed);

    SystemFacts page_facts_(const SystemFacts& f) const noexcept;
    void apply(const Transition& t, PageContext& ctx);

    void forget_hd_window_(std::size_t depth0) noexcept;

    void sync_cells();

    void stamp_facts(PageContext& ctx) const noexcept;
    void seed_navigator() noexcept;

    void drop_pages_() noexcept;

    static void disarm_launches_(PageContext& ctx) noexcept;

    void latch_browse_home() noexcept;
    [[nodiscard]] std::string_view browse_home() const noexcept;
    [[nodiscard]] app::RememberedStem file_stem() const noexcept;

    void arm_auto_pop(const PageContext& ctx) noexcept;

    bool push_info(PageContext& ctx, app::InfoId id);
    void place_overlay(std::unique_ptr<Page> p);

    bool show_progress(PageContext& ctx, std::uint16_t cur, std::uint16_t max);
    void take_transfer_name_(const PageContext& ctx) noexcept;

    std::unique_ptr<Page> make_page(PageId id, ItemId focus, PageContext& ctx);

    [[nodiscard]] HdPlanInputs plan_inputs_(PageContext& ctx, ItemId focus);

    void refresh_recent_(PageContext& ctx);

    [[nodiscard]] std::span<const app::PlayedHistory::Record> played_records_(PageContext& ctx);
    [[nodiscard]] app::RecentGames::Played played_record_() const noexcept;

    [[nodiscard]] std::unique_ptr<Page> make_hd_list_(std::unique_ptr<IHdPlan> plan, ItemId focus);

    PageStack stack_;
    NowPlaying now_playing_{};

    app::RecentGames recent_games_{};

    std::array<std::uint16_t, kHdWindowDepths> hd_window_first_{};
    Navigator nav_;
    HdKeyRemap remap_;
    HdFocusMemory hd_focus_;

    CoreVerbs verbs_{};
    OsdStylePreference style_{true};
    PageId resume_ = PageId::None;

    MenuCoreBinding binding_{};
    bool hd_root_ = false;
    PageId hd_classic_ = PageId::System;
    bool repaint_ = false;
    bool bootcore_armed_ = false;
    HdMask hd_mask_{};

    std::uint16_t sdram_cfg_ = 0;
    std::uint32_t sdram_ask_refusals_ = 0;
    bool sdram_reported_ = false;
    proto::ItemTable items_{};
    std::optional<proto::ItemProbe> pause_probe_{};

    CoreCellMirror mirror_;

    SavedBaseline baseline_;
    const app::LauncherStateCell& launcher_cell_;
    app::CheatStore& cheats_;
    const app::IdentityLatch& identity_;
    bool flat_ = false;
    std::int64_t help_rearm_at_ns_ = 0;
    std::uint32_t pumps_ = 0;
    std::uint32_t root_refusals_ = 0;
    std::uint32_t bootcore_cancels_ = 0;
    std::uint32_t help_rearms_ = 0;
    std::uint32_t status_paints_ = 0;
    std::int64_t status_due_ns_ = 0;

    std::int64_t auto_pop_at_ns_ = 0;
    std::uint32_t close_refusals_ = 0;
    std::uint32_t auto_pops_ = 0;
    bool invalidate_ = false;
    std::uint32_t invalidations_ = 0;

    bool osd_was_open_ = false;
    std::uint32_t show_edges_ = 0;
    std::uint32_t overlay_trades_ = 0;
    std::uint32_t progress_masked_ = 0;
    std::uint32_t info_refusals_ = 0;
    std::uint32_t progress_refusals_ = 0;
    std::uint32_t bt_refusals_ = 0;

    bool in_transfer_ = false;
    std::uint32_t load_asks_seen_ = 0;
    std::uint8_t transfer_name_len_ = 0;
    char transfer_name_[kProgressNameMax]{};
};

}
