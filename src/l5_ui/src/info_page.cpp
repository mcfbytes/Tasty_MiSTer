// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/info_page.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

#include "app/page_description.h"
#include "ui/osd_renderer.h"
#include "ui/ui_event.h"

namespace mister::ui {

namespace {

struct InfoFact {
    app::InfoId id;
    std::string_view text;
    std::chrono::milliseconds timeout;
};

inline constexpr std::chrono::milliseconds kSavingTimeout{500};
inline constexpr std::chrono::milliseconds kDefaultTimeout{2000};

inline constexpr std::size_t kInfoCount = static_cast<std::size_t>(app::InfoId::SavingState) + 1;

inline constexpr std::array<InfoFact, kInfoCount> kInfoFacts{{
    {app::InfoId::None, "", kDefaultTimeout},
    {app::InfoId::CoreNotFound, "     Core Not Found", kDefaultTimeout},
    {app::InfoId::CoreLoadFailed, "    Core Load Failed", kDefaultTimeout},
    {app::InfoId::ImageMountFailed, "   Image Mount Failed", kDefaultTimeout},
    {app::InfoId::ImageUnmounted, "    Image Unmounted", kDefaultTimeout},
    {app::InfoId::OptionCleared, "     Option Cleared", kDefaultTimeout},
    {app::InfoId::SaveWritten, "      Save Written", kDefaultTimeout},
    {app::InfoId::SaveFailed, "      Save Failed", kDefaultTimeout},
    {app::InfoId::ScreenshotWritten, "  Screenshot Written", kDefaultTimeout},
    {app::InfoId::ScreenshotFailed, "   Screenshot Failed", kDefaultTimeout},
    {app::InfoId::BluetoothUnavailable, " Bluetooth Unavailable", kDefaultTimeout},
    {app::InfoId::FbTerminalRequired, " FB Terminal Required", kDefaultTimeout},
    {app::InfoId::CdBiosMissing, "   CD BIOS not found!", kDefaultTimeout},
    {app::InfoId::RomLoadFailed, "    ROM Load Failed", kDefaultTimeout},
    {app::InfoId::SavingState, "    Saving the state", kSavingTimeout},
}};

consteval bool info_facts_ascend() {
    for (std::size_t i = 1; i < kInfoFacts.size(); ++i) {
        if (!(kInfoFacts[i - 1].id < kInfoFacts[i].id))
            return false;
    }
    return true;
}

static_assert(info_facts_ascend(), "kInfoFacts is ordered by InfoId");
static_assert(kInfoFacts.front().id == app::InfoId::None, "kInfoFacts starts at InfoId::None");
static_assert(
    kInfoFacts.back().id == app::InfoId::SavingState,
    "kInfoFacts ends at InfoId's last kind"
);

[[nodiscard]] const InfoFact& facts_of(app::InfoId id) noexcept {
    const auto row = std::ranges::find(kInfoFacts, id, &InfoFact::id);
    return *row;
}

class InfoPage final : public Page {
public:
    InfoPage(app::InfoId id, InfoTimeout deadline) noexcept
        : facts_(facts_of(id)),
          timeout_(deadline.value_or(facts_.timeout)) {}

    PageId id() const override { return PageId::Info; }

    PageFacts facts() const override {
        PageFacts f{};
        f.auto_pop = timeout_;
        f.has_title_strip = false;
        f.takes_input_focus = false;
        f.allows_popups = true;
        return f;
    }

    MenuMask mask() const override { return MenuMask{}; }

    bool walk(PageWalk&) const override { return false; }

    std::optional<proto::InfoWindow> info_window() const override { return window(); }

    void render(OsdRenderer& out) const override {
        const proto::InfoWindow w = window();
        for (OsdRow row{}; w.height.covers(row); row += proto::kOneOsdRow) {
            out.text(row, row == OsdRow{1} ? facts_.text : "");
        }
    }

    [[nodiscard]] proto::InfoWindow window() const noexcept {
        return proto::info_window_for(facts_.text);
    }

    bool describe(app::PageDescription& out) const override {
        out = {};
        app::PageDescription::Row& row = out.rows[0];
        row.kind = app::PageDescription::RowKind::Text;
        (void)row.label.assign(facts_.text);
        out.row_count = RowCount::of(1);
        out.total = RowCount::of(1);
        return true;
    }

    Transition handle(const UiEvent& ev) override {
        if (ev.buttons != UiButtonMask{}) {
            return Transition{Transition::Kind::Pop};
        }
        return Transition{};
    }

private:
    InfoFact facts_;
    std::chrono::milliseconds timeout_;
};

}

std::unique_ptr<Page> make_info_page(app::InfoId id, InfoTimeout timeout) {
    return std::unique_ptr<Page>(new InfoPage(id, timeout));
}

}
