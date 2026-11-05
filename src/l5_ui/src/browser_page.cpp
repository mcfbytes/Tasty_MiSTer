// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/browser_page.h"

#include <linux/input-event-codes.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "app/hd_window.h"
#include "app/page_description.h"
#include "app/recents_store.h"
#include "infra/opt_ref.h"
#include "svc/vfs.h"
#include "ui/browse_source.h"
#include "ui/browser_host.h"
#include "ui/draw_classic.h"
#include "ui/page_row.h"
#include "ui/page_window.h"
#include "ui/page_walk.h"
#include "ui/key_decoder.h"
#include "ui/menu_model.h"
#include "ui/navigator.h"
#include "ui/osd_renderer.h"
#include "ui/page_context.h"
#include "ui/ui_event.h"
#include "ui/browser_entry.h"

namespace mister::ui {

namespace {

proto::KeyCode scancode(proto::KeyCode code) noexcept {
    return proto::KeyCode{code.v & std::numeric_limits<std::uint16_t>::max()};
}

bool is_make(proto::KeyCode code, std::uint32_t key) noexcept {
    return (code.v & KeyDecoder::kUpstroke) == 0 && scancode(code) == proto::KeyCode{key};
}

constexpr char kMoreMarker = 22;

std::string blank_row() {
    constexpr std::size_t kBrowserRowWidth = 32;
    return std::string(kBrowserRowWidth, ' ');
}

}

std::string display_name(const BrowserEntry& e, bool cores) {
    constexpr std::size_t kNameCol = 1;
    constexpr std::size_t kNameTrimAt = 28;
    constexpr std::size_t kNameTrimmed = 27;
    constexpr std::size_t kUpTagCol = 19;
    constexpr std::size_t kDatecodeCol = 19;
    constexpr std::size_t kKindTagCol = 22;
    constexpr std::size_t kZipSuffix = 4;
    constexpr std::size_t kDatecodeMarkAt = 19;
    constexpr std::string_view kUpTag = " <UP-DIR>";
    constexpr std::string_view kZipTag = " <ZIP>";
    constexpr std::string_view kDirTag = " <DIR>";
    std::string s = blank_row();
    std::size_t len = e.altname.size();
    if (len > kNameTrimAt) {
        len = kNameTrimmed;
        s[kNameTrimAt] = kMoreMarker;
    }
    if (e.is_dir && cores && !e.altname.empty() && e.altname[0] == '_') {
        std::memcpy(s.data() + kNameCol, e.altname.data() + 1, len - 1);
    } else if (e.from_zip && len > kZipSuffix) {
        std::memcpy(s.data() + kNameCol, e.altname.data(), len - kZipSuffix);
    } else {
        std::memcpy(s.data() + kNameCol, e.altname.data(), len);
    }
    if (e.is_dir) {
        if (e.altname == "..") {
            std::memcpy(s.data() + kUpTagCol, kUpTag.data(), kUpTag.size());
        } else if (e.from_zip) {
            std::memcpy(s.data() + kKindTagCol, kZipTag.data(), kZipTag.size());
        } else {
            std::memcpy(s.data() + kKindTagCol, kDirTag.data(), kDirTag.size());
        }
    } else if (!e.datecode.empty()) {

        const auto digit = [&](std::size_t i) {
            return e.datecode.size() > i ? e.datecode[i] : ' ';
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
    }
    return s;
}

std::string hd_entry_label(const BrowserEntry& e, bool cores) {
    if (e.altname == "..")
        return "Up";
    std::string name = e.altname;
    if (e.is_dir && cores && !name.empty() && name[0] == '_') {
        name.erase(0, 1);
    } else if (e.from_zip && name.size() > 4) {
        name.resize(name.size() - 4);
    }
    return name;
}

namespace {

std::string_view utf8_prefix(std::string_view s, std::size_t cap) noexcept {
    constexpr unsigned kUtf8ContinuationMask = 0xC0u;
    constexpr unsigned kUtf8Continuation = 0x80u;
    if (s.size() <= cap)
        return s;
    std::size_t n = cap;
    while (n > 0 && (static_cast<unsigned char>(s[n]) & kUtf8ContinuationMask) == kUtf8Continuation)
        --n;
    return s.substr(0, n);
}

app::PageDescription::RowIcon icon_of(const BrowserEntry& e) noexcept {
    if (e.recent)
        return app::PageDescription::RowIcon::Recent;
    if (e.altname == "..")
        return app::PageDescription::RowIcon::Up;
    if (e.from_zip)
        return app::PageDescription::RowIcon::Archive;
    if (e.is_dir)
        return app::PageDescription::RowIcon::Folder;
    return app::PageDescription::RowIcon::None;
}

std::string hd_date_value(const std::string& datecode) {

    if (datecode.find_first_not_of('-') == std::string::npos)
        return {};
    const auto at = [&](std::size_t i) {
        return i < datecode.size() ? datecode[i] : ' ';
    };
    constexpr std::size_t kDateValueWidth = 8;
    std::string s;
    s.reserve(kDateValueWidth);
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
    if ((code.v & KeyDecoder::kUpstroke) != 0)
        return 0;
    const std::uint32_t k = scancode(code).v;
    if (k >= KEY_1 && k <= KEY_9)
        return static_cast<char>('1' + (k - KEY_1));
    if (k == KEY_0)
        return '0';
    constexpr const char* kRow1 = "QWERTYUIOP";
    constexpr const char* kRow2 = "ASDFGHJKL";
    constexpr const char* kRow3 = "ZXCVBNM";
    if (k >= KEY_Q && k <= KEY_P)
        return kRow1[k - KEY_Q];
    if (k >= KEY_A && k <= KEY_L)
        return kRow2[k - KEY_A];
    if (k >= KEY_Z && k <= KEY_M)
        return kRow3[k - KEY_Z];
    return 0;
}

void BrowserPage::on_enter(PageContext& ctx) {
    TASTY_SEAT_BODY(BrowserPage);
    clock_ = ctx.clock;
    recents_ = ctx.recents;

    (void)session_.open(*src_, static_cast<BrowserSession::Request&&>(req_));

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

    const app::HdWindow win = window_page(out.page, selected, count);
    const RowRange hd_rows = rows(RowIndex::of(win.first), RowCount::of(win.count));

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
            k < end && k == selected,
            stippled,
            leftchar,
            OsdRenderer::kArrowNone
        );
    };

    for (const RowIndex at : hd_rows) {
        const BrowserEntry& e = entries[at];
        PageRow& r = out.rows[out.count++];
        r.has_hd = true;
        (void)r.hd.label.assign(
            utf8_prefix(hd_entry_label(e, cores), decltype(r.hd.label)::kCapacity)
        );
        r.hd.kind = e.is_dir ? app::PageDescription::RowKind::Submenu
                             : app::PageDescription::RowKind::Action;
        r.hd.icon = icon_of(e);
        r.hd.enabled = !e.stippled;
        r.hd.selected = at == selected;

        if (!e.is_dir && !e.datecode.empty())
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

    if (ev.buttons.has_any({UiEvent::Menu})) {
        return host ? host->on_cancel() : Transition{Transition::Kind::Pop};
    }

    if (ev.buttons.has_any({UiEvent::Back})) {
        const BrowserPolicy& policy = session_.request().policy;
        if (session_.filter().empty() && (policy.eject_on_backspace || policy.clear_on_backspace)) {
            if (policy.eject_on_backspace && host)
                return host->on_eject();
            if (policy.clear_on_backspace && host) {

                const Transition t = host->on_clear();
                if (t.kind == Transition::Kind::Stay)
                    ++refusals_;
                return t;
            }
            ++refusals_;
            return Transition{};
        }
        session_.clear_filter();
        session_.note_navigation();
        (void)session_.rescan();
        return Transition{};
    }

    if (ev.buttons.has_any({UiEvent::Recent}))
        return open_recents_list();

    if (is_make(ev.raw, KEY_HOME)) {
        session_.note_navigation();
        (void)session_.rescan();
        return Transition{};
    }
    if (is_make(ev.raw, KEY_TAB)) {

        session_.note_navigation();
        (void)session_.rescan();
        const BrowserEntry* e = session_.view().selected();

        if (e != nullptr
            && e->recent
            && session_.entries().size() > 1
            && session_.entries()[1].is_dir
            && session_.entries()[1].name == "..") {
            session_.view().select(RowIndex{} + kOneRow);
            return select_current();
        }
        if (e != nullptr && e->is_dir && e->name == "..")
            return select_current();
        return Transition{};
    }
    if (is_make(ev.raw, KEY_END)) {
        session_.note_navigation();
        session_.view().end();
        return Transition{};
    }

    if (ev.buttons.has_any({UiEvent::Left}) || is_make(ev.raw, KEY_PAGEUP)) {
        session_.note_navigation();
        session_.view().prev_page();
        return Transition{};
    }
    if (ev.buttons.has_any({UiEvent::Right}) || is_make(ev.raw, KEY_PAGEDOWN)) {
        session_.note_navigation();
        session_.view().next_page();
        return Transition{};
    }
    if (ev.buttons.has_any({UiEvent::Down})) {
        session_.note_navigation();
        session_.view().next();
        return Transition{};
    }
    if (ev.buttons.has_any({UiEvent::Up})) {
        session_.note_navigation();
        session_.view().prev();
        return Transition{};
    }

    if (ev.buttons.has_any({UiEvent::Plus})) {
        session_.next_group();
        return Transition{};
    }
    if (ev.buttons.has_any({UiEvent::Minus})) {
        session_.prev_group();
        return Transition{};
    }

    if (ev.buttons.has_any({UiEvent::Select}))
        return select_current();

    if (const char c = browser_ascii_key(ev.raw); c != 0) {
        session_.type_ahead(c, now_ns());
    }
    return Transition{};
}

Transition BrowserPage::open_recents_list() {
    const BrowserSession::Request& req = session_.request();
    if (!recents_ || !req.recents.has_value()) {
        ++refusals_;
        return Transition{};
    }
    auto rows = recents_->load(req.recents_core, *req.recents);
    if (rows.empty())
        return Transition{};
    std::vector<BrowserEntry> mapped;
    mapped.reserve(rows.size());
    for (app::RecentsStore::Entry& r : rows) {
        BrowserEntry e;
        e.dir = static_cast<std::string&&>(r.dir);
        e.name = static_cast<std::string&&>(r.name);
        e.altname = static_cast<std::string&&>(r.label);
        e.stippled = !r.exists;
        mapped.push_back(static_cast<BrowserEntry&&>(e));
    }
    session_.enter_recents(static_cast<std::vector<BrowserEntry>&&>(mapped));
    return Transition{};
}

Transition BrowserPage::handle_recents(const UiEvent& ev) {
    const infra::OptRef<IBrowserHost> host = session_.request().host;

    if (ev.buttons.has_any({UiEvent::Menu | UiEvent::Recent})) {
        if (session_.request().recents_first) {
            return host ? host->on_cancel() : Transition{Transition::Kind::Pop};
        }
        (void)session_.exit_recents();
        return Transition{};
    }
    if (ev.buttons.has_any({UiEvent::Back})) {
        ++refusals_;
        return Transition{};
    }
    if (is_make(ev.raw, KEY_HOME)) {
        session_.view().select(RowIndex{});
        return Transition{};
    }
    if (is_make(ev.raw, KEY_END)) {
        session_.view().end();
        return Transition{};
    }
    if (ev.buttons.has_any({UiEvent::Left}) || is_make(ev.raw, KEY_PAGEUP)) {
        session_.view().prev_page();
        return Transition{};
    }
    if (ev.buttons.has_any({UiEvent::Right}) || is_make(ev.raw, KEY_PAGEDOWN)) {
        session_.view().next_page();
        return Transition{};
    }
    if (ev.buttons.has_any({UiEvent::Down})) {
        session_.view().next();
        return Transition{};
    }
    if (ev.buttons.has_any({UiEvent::Up})) {
        session_.view().prev();
        return Transition{};
    }
    if (ev.buttons.has_any({UiEvent::Select})) {
        const BrowserEntry* e = session_.view().selected();

        if (e == nullptr || e->stippled) {
            ++refusals_;
            return Transition{};
        }
        const std::string full = session_.selected_full_path();
        return host ? host->on_picked(*e, full) : Transition{Transition::Kind::Pop};
    }
    return Transition{};
}

Transition BrowserPage::select_current() {
    const infra::OptRef<IBrowserHost> host = session_.request().host;
    const BrowserEntry* e = session_.view().selected();
    if (e == nullptr) {
        ++refusals_;
        return Transition{};
    }

    if (e->recent)
        return open_recents_list();

    if (e->is_dir) {

        if (session_.request().cd_descend && e->name != ".." && src_ != nullptr) {
            std::string child = session_.path();
            if (!child.empty())
                child.push_back('/');
            child.append(e->name);
            svc::ScanFilter f{};
            f.extensions = session_.extensions();
            f.directories = false;
            f.files = true;
            f.zip_as_directory = false;
            if (auto listing = src_->scan(child, f); listing && listing->size() == 1) {
                BrowserEntry picked;
                picked.name = (*listing)[0].name;
                picked.altname = picked.name;
                picked.is_dir = false;
                child.push_back('/');
                child.append(picked.name);
                return host ? host->on_picked(picked, child) : Transition{Transition::Kind::Pop};
            }
        }
        (void)session_.change_dir(e->name);
        return Transition{};
    }

    const std::string full = session_.selected_full_path();
    return host ? host->on_picked(*e, full) : Transition{Transition::Kind::Pop};
}

}
