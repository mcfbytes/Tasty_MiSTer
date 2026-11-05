// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "app/page_description.h"
#include "cores/progress.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "ui/page_facts.h"
#include "ui/page_id.h"
#include "ui/page_row.h"
#include "ui/page_walk.h"
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
struct PageWalk;

class IProgressView {
public:
    virtual ~IProgressView() = default;
    virtual void set_progress(cores::Progress progress) noexcept = 0;

protected:
    IProgressView() = default;
    IProgressView(const IProgressView&) = default;
    IProgressView& operator=(const IProgressView&) = default;
};

class IMaskRebuild {
public:
    virtual ~IMaskRebuild() = default;
    virtual void rebuild(const proto::ItemTable& table, const BuildEnv& env) = 0;
    virtual proto::SubmenuNo page() const noexcept = 0;

protected:
    IMaskRebuild() = default;
    IMaskRebuild(const IMaskRebuild&) = default;
    IMaskRebuild& operator=(const IMaskRebuild&) = default;
};

class Page {
public:
    TASTY_SEAT_RESIDENT(Ui);

    virtual ~Page() = default;

    virtual PageId id() const = 0;

    virtual void on_enter(PageContext&) {}

    virtual bool walk(PageWalk& out) const = 0;

    virtual void render(OsdRenderer& out) const {
        TASTY_SEAT_BODY(Page);
        app::PageDescription page{};
        std::array<PageRow, kMaxPageRows> rows{};
        PageWalk w{.page = page, .rows = rows};
        if (!walk(w))
            return;
        draw_classic(out, w);
    }

    template <class Walker>
    [[nodiscard]] static bool describe_walk(const Walker& source, app::PageDescription& out) {
        TASTY_SEAT_BODY(Page);
        app::PageDescription page{};
        page.first = out.first;
        std::array<PageRow, kMaxPageRows> rows{};
        PageWalk w{.page = page, .rows = rows};
        if (!source.walk(w))
            return false;
        (void)copy_hd_rows(w);
        out = page;
        return true;
    }

    virtual bool describe(app::PageDescription& out) const {
        TASTY_SEAT_BODY(Page);
        return describe_walk(*this, out);
    }

    virtual bool describe_hub(app::PageDescription&) const { return false; }

    virtual Transition handle(const UiEvent&) = 0;

    virtual void on_facts(const struct SystemFacts&) {}

    virtual class IProgressView* progress_view() { return nullptr; }

    virtual infra::OptRef<IMaskRebuild> mask_rebuild() { return {}; }

    virtual bool holds_pairing_hotkey() const { return false; }

    virtual Transition on_tick(PageContext&) { return Transition{}; }

    virtual Transition open_root_child(PageContext&) { return Transition{}; }

    virtual Transition open_scripts(PageContext&) { return Transition{}; }

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

    virtual PageFacts facts() const { return kDefaultPageFacts; }

    RowCount osd_height() const noexcept { return facts().osd_rows; }

    virtual bool bt_pairing_active() const { return false; }

    virtual bool runs_a_process() const { return false; }

    virtual bool back_is_idle() const { return false; }

    virtual std::uint32_t refusals() const noexcept { return 0; }

protected:
    Page() = default;
    Page(const Page&) = default;
    Page& operator=(const Page&) = default;

    void draw_classic(OsdRenderer& out, const PageWalk& w) const;
};

}
