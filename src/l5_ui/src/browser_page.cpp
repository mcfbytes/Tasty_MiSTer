// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/browser_page.h"

#include <linux/input-event-codes.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "app/hd/hd_window.h"
#include "app/hd/page_description.h"
#include "app/recents/recents_store.h"
#include "infra/opt_ref.h"
#include "infra/utf8.h"
#include "svc/vfs.h"
#include "ui/browse_source.h"
#include "ui/browser_host.h"
#include "ui/draw_classic.h"
#include "ui/page_row.h"
#include "ui/page_walk.h"
#include "ui/key_decoder.h"
#include "ui/navigator.h"
#include "ui/osd_renderer.h"
#include "ui/page_context.h"
#include "ui/ui_event.h"
#include "ui/browser_entry.h"

namespace mister::ui {

namespace {

constexpr char kMoreMarker = 22;

constexpr std::size_t kZipSuffixChars = 4;

constexpr std::size_t kDatecodeColumns = 8;
constexpr std::size_t kDatecodeChars = kDatecodeColumns - 2;

std::string blank_row() {
    return std::string(kDirectoryRowColumns, ' ');
}

struct DisplayStem {
    std::string_view view;
    std::size_t prefix = 0;
    std::size_t suffix = 0;

    [[nodiscard]] std::string str() const { return std::string{view}; }
};

DisplayStem display_stem(const BrowserEntry& e, bool cores) noexcept {
    const std::string_view name{e.altname};
    if (e.is_dir() && cores && !name.empty() && name.front() == '_')
        return DisplayStem{name.substr(1), 1, 0};
    if (e.is_zip_dir() && name.size() > kZipSuffixChars)
        return DisplayStem{name.substr(0, name.size() - kZipSuffixChars), 0, kZipSuffixChars};
    return DisplayStem{name, 0, 0};
}

}

std::string display_name(const BrowserEntry& e, bool cores) {
    constexpr std::size_t kNameCol = 1;
    constexpr std::size_t kNameTrimAt = 28;
    constexpr std::size_t kNameTrimmed = 27;
    constexpr std::size_t kUpTagCol = 19;
    constexpr std::size_t kDatecodeCol = 19;
    constexpr std::size_t kKindTagCol = 22;
    constexpr std::size_t kDatecodeMarkAt = 19;
    constexpr std::string_view kUpTag = " <UP-DIR>";
    constexpr std::string_view kZipTag = " <ZIP>";
    constexpr std::string_view kDirTag = " <DIR>";
    std::string s = blank_row();

    const DisplayStem stem = display_stem(e, cores);
    const std::size_t trim = e.altname.size() > kNameTrimAt ? kNameTrimmed : e.altname.size();
    std::size_t len = trim - stem.prefix - stem.suffix;
    if (len > stem.view.size())
        len = stem.view.size();
    if (e.altname.size() > kNameTrimAt)
        s[kNameTrimAt] = kMoreMarker;
    std::memcpy(s.data() + kNameCol, stem.view.data(), len);
    switch (e.kind) {
        case EntryKind::UpDir:
            std::memcpy(s.data() + kUpTagCol, kUpTag.data(), kUpTag.size());
            break;
        case EntryKind::ZipDir:
            std::memcpy(s.data() + kKindTagCol, kZipTag.data(), kZipTag.size());
            break;
        case EntryKind::Dir:
        case EntryKind::Recent:
            std::memcpy(s.data() + kKindTagCol, kDirTag.data(), kDirTag.size());
            break;
        case EntryKind::File: {
            if (e.datecode.empty())
                break;

            const auto digit = [&](std::size_t i) {
                return i < kDatecodeChars && e.datecode.size() > i ? e.datecode[i] : ' ';
            };
            char* d = s.data() + kDatecodeCol;
            *d++ = ' ';
            *d++ = e.datecode[0];
            *d++ = digit(1);
            *d++ = '.';
            *d++ = digit(2);
            *d++ = digit(3);
            *d++ = '.';
            *d++ = digit(4);
            *d = digit(5);
            if (e.altname.size() >= kDatecodeMarkAt) {
                s[kDatecodeMarkAt] = kMoreMarker;
                s[kNameTrimAt] = ' ';
            }
            break;
        }
    }
    return s;
}

std::string hd_entry_label(const BrowserEntry& e, bool cores) {
    if (e.is_up())
        return "Up";
    return display_stem(e, cores).str();
}

namespace {

app::hd::PageDescription::RowIcon icon_of(const BrowserEntry& e) noexcept {
    switch (e.kind) {
        case EntryKind::Recent:
            return app::hd::PageDescription::RowIcon::Recent;
        case EntryKind::UpDir:
            return app::hd::PageDescription::RowIcon::Up;
        case EntryKind::ZipDir:
            return app::hd::PageDescription::RowIcon::Archive;
        case EntryKind::Dir:
            return app::hd::PageDescription::RowIcon::Folder;
        case EntryKind::File:
            return app::hd::PageDescription::RowIcon::None;
    }
    return app::hd::PageDescription::RowIcon::None;
}

std::string hd_date_value(const std::string& datecode) {

    if (datecode.find_first_not_of('-') == std::string::npos)
        return {};
    const auto at = [&](std::size_t i) {
        return i < datecode.size() ? datecode[i] : ' ';
    };
    std::string s;
    s.reserve(kDatecodeColumns);
    s.push_back(at(0));
    s.push_back(at(1));
    s.push_back('.');
    s.push_back(at(2));
    s.push_back(at(3));
    s.push_back('.');
    s.push_back(at(4));
    s.push_back(at(5));
    while (!s.empty() && s.back() == ' ')
        s.pop_back();
    return s;
}

}

char browser_ascii_key(proto::KeyCode code) noexcept {
    if (code.is_release())
        return 0;
    const svc::EvdevCode k = svc::EvdevCode::of(code);
    constexpr svc::EvdevRun kDigits{svc::EvdevCode{KEY_1}, svc::EvdevCode{KEY_9}};
    constexpr svc::EvdevRun kRow1Keys{svc::EvdevCode{KEY_Q}, svc::EvdevCode{KEY_P}};
    constexpr svc::EvdevRun kRow2Keys{svc::EvdevCode{KEY_A}, svc::EvdevCode{KEY_L}};
    constexpr svc::EvdevRun kRow3Keys{svc::EvdevCode{KEY_Z}, svc::EvdevCode{KEY_M}};
    if (const auto i = kDigits.place_of(k))
        return static_cast<char>('1' + *i);
    if (k == svc::EvdevCode{KEY_0})
        return '0';
    constexpr const char* kRow1 = "QWERTYUIOP";
    constexpr const char* kRow2 = "ASDFGHJKL";
    constexpr const char* kRow3 = "ZXCVBNM";
    if (const auto i = kRow1Keys.place_of(k))
        return kRow1[*i];
    if (const auto i = kRow2Keys.place_of(k))
        return kRow2[*i];
    if (const auto i = kRow3Keys.place_of(k))
        return kRow3[*i];
    return 0;
}

void BrowserPage::on_enter(PageContext& ctx) {
    TASTY_SEAT_BODY(BrowserPage);
    clock_ = ctx.clock;
    recents_ = ctx.recents;

    (void)session_.open(src_, static_cast<BrowserSession::Request&&>(req_));

    if (session_.request().recents_first)
        (void)open_recents_list();
}

bool BrowserPage::walk(PageWalk& out) const {
    TASTY_SEAT_BODY(BrowserPage);
    const bool cores = session_.request().scan.cores_only;
    const bool recents = session_.recents_active();
    (void)out.page.title.assign(
        recents ? (cores ? "Recent Cores" : "Recent Files") : (cores ? "Cores" : "Select")
    );
    out.title_arrow = OsdRenderer::kArrowNone;
    out.count = 0;

    const RowSpan<const BrowserEntry> entries{session_.entries()};
    const RowCount count = entries.size();
    const RowIndex end = row_after(count);
    const RowCount height = height_;
    const RowIndex first = session_.view().viewport().first;
    const RowIndex selected = session_.view().viewport().selected;

    const app::hd::HdWindow win = out.window(selected, count);
    const RowRange hd_rows = rows(win.start, win.len);

    if (!session_.filter().empty())
        (void)out.page.footer.assign(session_.filter());
    out.page.letter = session_.selected_group();

    const auto classic_at = [&](RowCount i) {
        std::string s = blank_row();
        std::optional<Glyph> leftchar;
        bool stippled = false;
        const RowIndex k = first + i;
        if (k < end) {
            s = display_name(entries[k], recents ? false : cores);
            if (recents) {
                if (i.empty() && k != RowIndex{})
                    leftchar = Glyph::ArrowUp;
                if (i + kOneRow == height && k + kOneRow < end)
                    leftchar = Glyph::ArrowDown;
                stippled = entries[k].stippled;
            }
        }
        return classic_row(
            osd_row_at(i),
            s,
            {.inverted = k < end && k == selected,
             .stippled = stippled,
             .leftchar = leftchar,
             .arrow = OsdRenderer::kArrowNone}
        );
    };

    for (const RowIndex at : hd_rows) {
        const BrowserEntry& e = entries[at];
        PageRow& r = out.rows[out.count++];
        r.has_hd = true;
        (void)r.hd.label.assign(
            infra::utf8_prefix(hd_entry_label(e, cores), decltype(r.hd.label)::kCapacity)
        );
        r.hd.kind = e.is_dir() ? app::hd::PageDescription::RowKind::Submenu
                               : app::hd::PageDescription::RowKind::Action;
        r.hd.icon = icon_of(e);
        r.hd.enabled = !e.stippled;
        r.hd.selected = at == selected;

        if (!e.is_dir() && !e.datecode.empty())
            (void)r.hd.value.assign(hd_date_value(e.datecode));
        if (at < first || at - first >= height)
            continue;
        r.classic = classic_at(at - first);
    }

    for (const RowIndex line : rows(height)) {
        const RowIndex k = first + line.before();
        if (k < end && hd_rows.contains(k))
            continue;
        out.rows[out.count++].classic = classic_at(line.before());
    }
    return true;
}

bool BrowserPage::back_is_idle() const {
    TASTY_SEAT_BODY(BrowserPage);
    const BrowserPolicy& policy = session_.request().policy;
    return !session_.recents_active()
        && session_.filter().empty()
        && !policy.eject_on_backspace
        && !policy.clear_on_backspace;
}

Transition BrowserPage::handle(const UiEvent& ev) {
    TASTY_SEAT_BODY(BrowserPage);
    if (session_.recents_active())
        return handle_recents(ev);
    const infra::OptRef<IBrowserHost> host = session_.request().host;

    if (ev.buttons.intersects({UiEvent::Menu})) {
        return host ? host->on_cancel() : Transition::pop();
    }

    if (ev.buttons.intersects({UiEvent::Back})) {
        const BrowserPolicy& policy = session_.request().policy;
        if (session_.filter().empty() && (policy.eject_on_backspace || policy.clear_on_backspace)) {
            if (policy.eject_on_backspace && host)
                return host->on_eject();
            if (policy.clear_on_backspace && host) {

                const Transition t = host->on_clear();
                if (t.kind() == Transition::Kind::Stay)
                    refusals_.add();
                return t;
            }
            refusals_.add();
            return Transition{};
        }
        session_.clear_filter();
        session_.note_navigation();
        (void)session_.rescan();
        return Transition{};
    }

    if (ev.buttons.intersects({UiEvent::Recent}))
        return open_recents_list();

    if (KeyDecoder::is_make(ev.raw, svc::EvdevCode{KEY_HOME})) {
        session_.note_navigation();
        (void)session_.rescan();
        return Transition{};
    }
    if (KeyDecoder::is_make(ev.raw, svc::EvdevCode{KEY_TAB})) {

        session_.note_navigation();
        (void)session_.rescan();
        const auto e = session_.view().selected();

        if (e && e->is_recent() && session_.entries().size() > 1 && session_.entries()[1].is_up()) {
            session_.view().select(RowIndex{} + kOneRow);
            return select_current();
        }
        if (e && e->is_up())
            return select_current();
        return Transition{};
    }
    if (navigate_(ev)) {
        session_.note_navigation();
        return Transition{};
    }

    if (ev.buttons.intersects({UiEvent::Plus})) {
        session_.next_group();
        return Transition{};
    }
    if (ev.buttons.intersects({UiEvent::Minus})) {
        session_.prev_group();
        return Transition{};
    }

    if (ev.buttons.intersects({UiEvent::Select}))
        return select_current();

    if (const char c = browser_ascii_key(ev.raw); c != 0) {
        session_.type_ahead(c, now());
    }
    return Transition{};
}

Transition BrowserPage::open_recents_list() {
    const BrowserSession::Request& req = session_.request();
    if (!recents_ || !req.recents.has_value()) {
        refusals_.add();
        return Transition{};
    }
    auto rows = recents_->load(req.recents_core, *req.recents);
    if (rows.empty())
        return Transition{};
    std::vector<BrowserEntry> mapped;
    mapped.reserve(rows.size());
    for (app::recents::RecentsStore::Entry& r : rows) {
        BrowserEntry e;
        e.dir = static_cast<std::string&&>(r.dir);
        e.name = static_cast<std::string&&>(r.name);
        e.altname = static_cast<std::string&&>(r.label);
        e.kind = EntryKind::File;
        e.stippled = !r.exists;
        mapped.push_back(static_cast<BrowserEntry&&>(e));
    }
    session_.enter_recents(static_cast<std::vector<BrowserEntry>&&>(mapped));
    return Transition{};
}

Transition BrowserPage::handle_recents(const UiEvent& ev) {
    const infra::OptRef<IBrowserHost> host = session_.request().host;

    if (ev.buttons.intersects({UiEvent::Menu | UiEvent::Recent})) {
        if (session_.request().recents_first) {
            return host ? host->on_cancel() : Transition::pop();
        }
        (void)session_.exit_recents();
        return Transition{};
    }
    if (ev.buttons.intersects({UiEvent::Back})) {
        refusals_.add();
        return Transition{};
    }
    if (KeyDecoder::is_make(ev.raw, svc::EvdevCode{KEY_HOME})) {
        session_.view().select(RowIndex{});
        return Transition{};
    }
    if (navigate_(ev))
        return Transition{};
    if (ev.buttons.intersects({UiEvent::Select})) {
        const auto e = session_.view().selected();

        if (!e || e->stippled) {
            refusals_.add();
            return Transition{};
        }
        const std::string full = session_.selected_full_path();
        return host ? host->on_picked(*e, full) : Transition::pop();
    }
    return Transition{};
}

bool BrowserPage::navigate_(const UiEvent& ev) {
    if (KeyDecoder::is_make(ev.raw, svc::EvdevCode{KEY_END})) {
        session_.view().end();
        return true;
    }
    if (ev.buttons.intersects({UiEvent::Left})
        || KeyDecoder::is_make(ev.raw, svc::EvdevCode{KEY_PAGEUP})) {
        session_.view().prev_page();
        return true;
    }
    if (ev.buttons.intersects({UiEvent::Right})
        || KeyDecoder::is_make(ev.raw, svc::EvdevCode{KEY_PAGEDOWN})) {
        session_.view().next_page();
        return true;
    }
    if (ev.buttons.intersects({UiEvent::Down})) {
        session_.view().next();
        return true;
    }
    if (ev.buttons.intersects({UiEvent::Up})) {
        session_.view().prev();
        return true;
    }
    return false;
}

Transition BrowserPage::select_current() {
    const infra::OptRef<IBrowserHost> host = session_.request().host;
    const auto e = session_.view().selected();
    if (!e) {
        refusals_.add();
        return Transition{};
    }

    if (e->is_recent())
        return open_recents_list();

    if (e->is_dir()) {

        if (session_.request().cd_descend && !e->is_up()) {
            std::string child = session_.path();
            if (!child.empty())
                child.push_back('/');
            child.append(e->name);
            svc::ScanFilter f{};
            f.extensions = session_.extensions();
            f.directories = false;
            f.files = true;
            f.zip_as_directory = false;
            if (auto listing = src_.scan(child, f); listing && listing->size() == 1) {
                BrowserEntry picked;
                picked.name = (*listing)[0].name;
                picked.altname = picked.name;
                picked.kind = EntryKind::File;
                child.push_back('/');
                child.append(picked.name);
                return host ? host->on_picked(picked, child) : Transition::pop();
            }
        }
        (void)session_.change_dir(e->name);
        return Transition{};
    }

    const std::string full = session_.selected_full_path();
    return host ? host->on_picked(*e, full) : Transition::pop();
}

}
