// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/info_page.h"

#include "app/page_description.h"
#include "ui/osd_renderer.h"
#include "ui/ui_event.h"

namespace mister::ui {

namespace {

class InfoPage final : public Page {
public:
    InfoPage(app::InfoId id, std::chrono::milliseconds timeout) noexcept
        : id_(id),
          timeout_(timeout.count() != 0 ? timeout : timeout_for(id)) {}

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

    void render(OsdRenderer& out) const override {
        constexpr OsdRow kBoxTop{6};
        constexpr OsdRow kMessageRow{7};
        constexpr OsdRow kBlankRow{8};
        constexpr OsdRow kBoxBottom{9};
        out.box(kBoxTop, kBoxBottom);
        out.text(kMessageRow, text_for(id_));
        out.text(kBlankRow, "");
    }

    bool describe(app::PageDescription& out) const override {
        out = {};
        app::PageDescription::Row& row = out.rows[0];
        row.kind = app::PageDescription::RowKind::Text;
        (void)row.label.assign(text_for(id_));
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
    static std::chrono::milliseconds timeout_for(app::InfoId id) noexcept {
        constexpr std::chrono::milliseconds kSavingTimeout{500};
        constexpr std::chrono::milliseconds kDefaultTimeout{2000};
        switch (id) {
            case app::InfoId::SavingState:
                return kSavingTimeout;
            case app::InfoId::None:
            case app::InfoId::CoreNotFound:
            case app::InfoId::CoreLoadFailed:
            case app::InfoId::ImageMountFailed:
            case app::InfoId::ImageUnmounted:
            case app::InfoId::OptionCleared:
            case app::InfoId::SaveWritten:
            case app::InfoId::SaveFailed:
            case app::InfoId::ScreenshotWritten:
            case app::InfoId::ScreenshotFailed:
            case app::InfoId::BluetoothUnavailable:
            case app::InfoId::FbTerminalRequired:
            case app::InfoId::CdBiosMissing:
            case app::InfoId::RomLoadFailed:
                return kDefaultTimeout;
        }
        return kDefaultTimeout;
    }

    static const char* text_for(app::InfoId id) noexcept {
        switch (id) {
            case app::InfoId::CoreNotFound:
                return "     Core Not Found";
            case app::InfoId::CoreLoadFailed:
                return "    Core Load Failed";
            case app::InfoId::ImageMountFailed:
                return "   Image Mount Failed";
            case app::InfoId::ImageUnmounted:
                return "    Image Unmounted";
            case app::InfoId::OptionCleared:
                return "     Option Cleared";
            case app::InfoId::SaveWritten:
                return "      Save Written";
            case app::InfoId::SaveFailed:
                return "      Save Failed";
            case app::InfoId::ScreenshotWritten:
                return "  Screenshot Written";
            case app::InfoId::ScreenshotFailed:
                return "   Screenshot Failed";
            case app::InfoId::BluetoothUnavailable:
                return " Bluetooth Unavailable";
            case app::InfoId::FbTerminalRequired:
                return " FB Terminal Required";
            case app::InfoId::CdBiosMissing:
                return "   CD BIOS not found!";
            case app::InfoId::RomLoadFailed:
                return "    ROM Load Failed";
            case app::InfoId::SavingState:
                return "    Saving the state";
            case app::InfoId::None:
                return "";
        }

        return "";
    }

    app::InfoId id_;
    std::chrono::milliseconds timeout_;
};

}

std::unique_ptr<Page> make_info_page(app::InfoId id, std::chrono::milliseconds timeout) {
    return std::unique_ptr<Page>(new InfoPage(id, timeout));
}

}
