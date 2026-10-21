// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/info_page.h"

#include "app/page_description.h"
#include "ui/osd_renderer.h"
#include "ui/ui_event.h"

namespace mister::ui {

namespace {

class InfoPage final : public Page {
public:
    InfoPage(app::InfoId id, std::uint32_t timeout_ms) noexcept
        : id_(id),
          timeout_ms_(timeout_ms != 0 ? timeout_ms : timeout_for(id)) {}

    PageId id() const override { return PageId::Info; }

    bool allows_popups() const override { return true; }

    bool counts_as_menu_present() const override { return true; }

    bool takes_input_focus() const override { return false; }

    MenuMask mask() const override { return MenuMask{}; }

    void render(OsdRenderer& out) const override {
        out.box(OsdRow{6}, OsdRow{9});
        out.text(OsdRow{7}, text_for(id_));
        out.text(OsdRow{8}, "");
    }

    bool describe(app::PageDescription& out) const override {
        out = {};
        app::PageDescription::Row& row = out.rows[0];
        row.kind = app::PageDescription::RowKind::Text;
        (void)row.label.assign(text_for(id_));
        out.row_count = 1;
        out.total = 1;
        return true;
    }

    Transition handle(const UiEvent& ev) override {
        if (ev.buttons != UiEvent::None) {
            return Transition{Transition::Kind::Pop};
        }
        return Transition{};
    }

    std::uint32_t auto_pop_ms() const override { return timeout_ms_; }

    bool has_title_strip() const override { return false; }

private:
    static std::uint32_t timeout_for(app::InfoId id) noexcept {
        switch (id) {
            case app::InfoId::SavingState:
                return 500u;
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
                return 2000u;
        }
        return 2000u;
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
    std::uint32_t timeout_ms_;
};

}

std::unique_ptr<Page> make_info_page(app::InfoId id, std::uint32_t timeout_ms) {
    return std::unique_ptr<Page>(new InfoPage(id, timeout_ms));
}

}
