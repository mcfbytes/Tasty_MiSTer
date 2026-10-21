// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/browser_page.h"

#include <linux/input-event-codes.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "app/hd_window.h"
#include "app/page_description.h"
#include "app/recents_store.h"
#include "infra/opt_ref.h"
#include "svc/vfs.h"
#include "ui/browse_source.h"
#include "ui/browser_host.h"
#include "ui/key_decoder.h"
#include "ui/menu_model.h"
#include "ui/navigator.h"
#include "ui/osd_renderer.h"
#include "ui/page_context.h"
#include "ui/ui_event.h"
#include "ui/browser_entry.h"

namespace mister::ui {

namespace {

bool is_make(std::uint32_t code, std::uint32_t key) noexcept {
    return (code & KeyDecoder::kUpstroke) == 0 && (code & 0xFFFFu) == key;
}

constexpr char kMoreMarker = 22;

}

std::string display_name(const BrowserEntry& e, bool cores) {
    std::string s(32, ' ');
    std::size_t len = e.altname.size();
    if (len > 28) {
        len = 27;
        s[28] = kMoreMarker;
    }
    if (e.is_dir && cores && !e.altname.empty() && e.altname[0] == '_') {
        std::memcpy(s.data() + 1, e.altname.data() + 1, len - 1);
    } else if (e.from_zip && len > 4) {
        std::memcpy(s.data() + 1, e.altname.data(), len - 4);
    } else {
        std::memcpy(s.data() + 1, e.altname.data(), len);
    }
    if (e.is_dir) {
        if (e.altname == "..") {
            std::memcpy(s.data() + 19, " <UP-DIR>", 9);
        } else if (e.from_zip) {
            std::memcpy(s.data() + 22, " <ZIP>", 6);
        } else {
            std::memcpy(s.data() + 22, " <DIR>", 6);
        }
    } else if (!e.datecode.empty()) {
        char* d = s.data() + 19;
        *d++ = ' ';
        *d++ = e.datecode[0];
        *d++ = e.datecode.size() > 1 ? e.datecode[1] : ' ';
        *d++ = '.';
        *d++ = e.datecode.size() > 2 ? e.datecode[2] : ' ';
        *d++ = e.datecode.size() > 3 ? e.datecode[3] : ' ';
        *d++ = '.';
        *d++ = e.datecode.size() > 4 ? e.datecode[4] : ' ';
        *d = e.datecode.size() > 5 ? e.datecode[5] : ' ';
        if (e.altname.size() >= 19) {
            s[19] = kMoreMarker;
            s[28] = ' ';
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
    if (s.size() <= cap)
        return s;
    std::size_t n = cap;
    while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0u) == 0x80u)
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
    std::string s;
    s.reserve(8);
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

char browser_ascii_key(std::uint32_t code) noexcept {
    if ((code & KeyDecoder::kUpstroke) != 0)
        return 0;
    const std::uint32_t k = code & 0xFFFFu;
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

void BrowserPage::render(OsdRenderer& out) const {
    TASTY_SEAT_BODY(BrowserPage);
    const bool cores = session_.request().scan.cores_only;
    if (session_.recents_active()) {
        out.set_title(cores ? "Recent Cores" : "Recent Files");

        const auto& rows = session_.entries();
        const std::uint32_t height = height_.v;
        const std::uint32_t first = session_.view().viewport().first.v;
        const std::uint32_t selected = session_.view().viewport().selected.v;
        for (std::uint32_t i = 0; i < height; ++i) {
            std::string s(32, ' ');
            std::uint8_t leftchar = 0;
            bool stippled = false;
            const std::uint32_t k = first + i;
            if (k < rows.size()) {
                s = display_name(rows[k], false);
                if (i == 0 && k != 0)
                    leftchar = 17;
                if (i == height - 1 && k + 1 < rows.size())
                    leftchar = 16;
                stippled = rows[k].stippled;
            }
            out.text(
                proto::OsdRow{static_cast<std::uint8_t>(i)},
                std::string_view{s.data(), 32},
                k < rows.size() && k == selected,
                stippled,
                leftchar
            );
        }
        return;
    }
    const auto& entries = session_.entries();
    const std::uint32_t rows = height_.v;
    const std::uint32_t first = session_.view().viewport().first.v;
    const std::uint32_t selected = session_.view().viewport().selected.v;
    out.set_title(cores ? "Cores" : "Select");

    for (std::uint32_t i = 0; i < rows; ++i) {
        std::string s(32, ' ');
        const std::uint32_t k = first + i;
        if (k < entries.size())
            s = display_name(entries[k], cores);
        out.text(
            proto::OsdRow{static_cast<std::uint8_t>(i)},
            std::string_view{s.data(), 32},
            k < entries.size() && k == selected,
            false
        );
    }
}

bool BrowserPage::describe(app::PageDescription& out) const {
    TASTY_SEAT_BODY(BrowserPage);
    const std::uint16_t hint = out.first;
    out = {};
    const bool cores = session_.request().scan.cores_only;
    const bool recents = session_.recents_active();
    (void)out.title.assign(
        recents ? (cores ? "Recent Cores" : "Recent Files") : (cores ? "Cores" : "Select")
    );

    const std::vector<BrowserEntry>& entries = session_.entries();
    const std::uint32_t count = static_cast<std::uint32_t>(entries.size());
    const std::uint32_t selected = session_.view().viewport().selected.v;
    const app::HdWindow win = app::hd_window(
        static_cast<std::uint16_t>(selected),
        static_cast<std::uint16_t>(count),
        hint
    );
    const std::uint32_t first = win.first;
    const std::uint32_t last = first + win.count;
    out.first = static_cast<std::uint16_t>(first);
    out.row_count = static_cast<std::uint8_t>(last - first);
    out.total = static_cast<std::uint16_t>(count);
    for (std::uint32_t at = first; at < last; ++at) {
        app::PageDescription::Row& row = out.rows[at - first];
        const BrowserEntry& e = entries[at];
        (void)row.label.assign(
            utf8_prefix(hd_entry_label(e, cores), decltype(row.label)::kCapacity)
        );
        row.kind = e.is_dir ? app::PageDescription::RowKind::Submenu
                            : app::PageDescription::RowKind::Action;
        row.icon = icon_of(e);
        row.enabled = !e.stippled;
        row.selected = at == selected;

        if (!e.is_dir && !e.datecode.empty())
            (void)row.value.assign(hd_date_value(e.datecode));
    }

    if (!session_.filter().empty())
        (void)out.footer.assign(session_.filter());
    return true;
}

Transition BrowserPage::handle(const UiEvent& ev) {
    TASTY_SEAT_BODY(BrowserPage);
    if (session_.recents_active())
        return handle_recents(ev);
    const infra::OptRef<IBrowserHost> host = session_.request().host;

    if ((ev.buttons & UiEvent::Menu) != 0) {
        return host ? host->on_cancel() : Transition{Transition::Kind::Pop};
    }

    if ((ev.buttons & UiEvent::Back) != 0) {
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

    if ((ev.buttons & UiEvent::Recent) != 0)
        return open_recents_list();

    if (is_make(ev.raw.v, KEY_HOME)) {
        session_.note_navigation();
        (void)session_.rescan();
        return Transition{};
    }
    if (is_make(ev.raw.v, KEY_TAB)) {

        session_.note_navigation();
        (void)session_.rescan();
        const BrowserEntry* e = session_.view().selected();

        if (e != nullptr
            && e->recent
            && session_.entries().size() > 1
            && session_.entries()[1].is_dir
            && session_.entries()[1].name == "..") {
            session_.view().select(RowIndex{1});
            return select_current();
        }
        if (e != nullptr && e->is_dir && e->name == "..")
            return select_current();
        return Transition{};
    }
    if (is_make(ev.raw.v, KEY_END)) {
        session_.note_navigation();
        session_.view().end();
        return Transition{};
    }

    if ((ev.buttons & UiEvent::Left) != 0) {
        session_.note_navigation();
        session_.view().prev_page();
        return Transition{};
    }
    if ((ev.buttons & UiEvent::Right) != 0) {
        session_.note_navigation();
        session_.view().next_page();
        return Transition{};
    }
    if ((ev.buttons & UiEvent::Down) != 0) {
        session_.note_navigation();
        session_.view().next();
        return Transition{};
    }
    if ((ev.buttons & UiEvent::Up) != 0) {
        session_.note_navigation();
        session_.view().prev();
        return Transition{};
    }

    if ((ev.buttons & UiEvent::Plus) != 0) {
        session_.next_group();
        return Transition{};
    }
    if ((ev.buttons & UiEvent::Minus) != 0) {
        session_.prev_group();
        return Transition{};
    }

    if ((ev.buttons & UiEvent::Select) != 0)
        return select_current();

    if (const char c = browser_ascii_key(ev.raw.v); c != 0) {
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

    if ((ev.buttons & (UiEvent::Menu | UiEvent::Recent)) != 0) {
        if (session_.request().recents_first) {
            return host ? host->on_cancel() : Transition{Transition::Kind::Pop};
        }
        (void)session_.exit_recents();
        return Transition{};
    }
    if ((ev.buttons & UiEvent::Back) != 0) {
        ++refusals_;
        return Transition{};
    }
    if (is_make(ev.raw.v, KEY_HOME)) {
        session_.view().select(RowIndex{0});
        return Transition{};
    }
    if (is_make(ev.raw.v, KEY_END)) {
        session_.view().end();
        return Transition{};
    }
    if ((ev.buttons & UiEvent::Left) != 0) {
        session_.view().prev_page();
        return Transition{};
    }
    if ((ev.buttons & UiEvent::Right) != 0) {
        session_.view().next_page();
        return Transition{};
    }
    if ((ev.buttons & UiEvent::Down) != 0) {
        session_.view().next();
        return Transition{};
    }
    if ((ev.buttons & UiEvent::Up) != 0) {
        session_.view().prev();
        return Transition{};
    }
    if ((ev.buttons & UiEvent::Select) != 0) {
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
