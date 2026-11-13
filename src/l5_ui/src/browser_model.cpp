// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/browser_session.h"

#include <algorithm>
#include <cctype>
#include <string_view>

#include "app/romset_aliases.h"
#include "infra/ascii.h"
#include "infra/opt_ref.h"
#include "ui/browse_source.h"
#include "ui/browser_entry.h"
#include "ui/browser_host.h"

namespace mister::ui {

namespace {

constexpr std::string_view kDotExt = ".ext";

EntryKind kind_of(const svc::DirEntry& d) noexcept {
    if (!d.is_dir)
        return EntryKind::File;
    if (d.name == "..")
        return EntryKind::UpDir;
    if (!d.is_zip_member && infra::iends_with(d.name, ".zip"))
        return EntryKind::ZipDir;
    return EntryKind::Dir;
}

bool entry_less(const BrowserEntry& a, const BrowserEntry& b) noexcept {
    if (a.is_up() || b.is_up())
        return a.is_up() && !b.is_up();
    if (a.is_dir() != b.is_dir())
        return a.is_dir();

    std::size_t l1 = a.altname.size();
    std::size_t l2 = b.altname.size();
    if (l1 > kDotExt.size() && a.altname[l1 - kDotExt.size()] == '.')
        l1 -= kDotExt.size();
    if (l2 > kDotExt.size() && b.altname[l2 - kDotExt.size()] == '.')
        l2 -= kDotExt.size();

    const std::weak_ordering r = infra::icompare(a.altname.substr(0, l1), b.altname.substr(0, l2));
    if (r != 0)
        return r < 0;
    return infra::icompare(a.datecode, b.datecode) < 0;
}

char group_char(const BrowserEntry& e, bool cores) noexcept {
    char c = e.altname.empty() ? '\0' : e.altname[0];
    if (c == '_' && e.is_dir() && cores)
        c = e.altname.size() > 1 ? e.altname[1] : '\0';
    return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}

BrowserEntry recent_entry_row() {
    BrowserEntry e;
    e.name = "Recent";
    e.altname = "Recent";
    e.kind = EntryKind::Recent;
    return e;
}

}

Ex<void> BrowserSession::open(const IBrowseSource& src, Request req) {
    src_ = infra::OptRef<const IBrowseSource>{src};
    req_ = static_cast<Request&&>(req);
    filter_.clear();
    filter_deadline_.reset();
    entries_.clear();
    view_.set_items({});

    path_ = req_.start_path;
    if (path_.size() > kPathMax - 1)
        path_.resize(kPathMax - 1);
    ext_ = req_.extensions;
    home_.clear();

    if (req_.scan.cores_only) {

        while (!path_.empty() && !src_->dir_exists(path_)) {
            const std::size_t slash = path_.rfind('/');
            if (slash == std::string::npos) {
                path_.clear();
            } else {
                path_.resize(slash);
            }
        }
        ext_ = "RBFMRAMGL";
    } else if (req_.policy.text_defaults) {
        if (ext_.empty())
            ext_ = "TXT";
    } else {
        home_ = resolve_content_home();

        const bool under_home = infra::istarts_with(path_, home_);
        const bool is_home = infra::ieq(home_, path_);
        const bool exists = src_->file_exists(path_) || src_->dir_exists(path_);
        if (!under_home || is_home || !exists) {
            req_.scan.no_enter = false;
            path_ = home_;
        }
    }
    constexpr std::size_t kExtWidth = 3;
    while (ext_.size() % kExtWidth != 0)
        ext_.push_back(' ');

    std::string position;
    if (req_.scan.no_enter || src_->file_exists(path_)) {
        const std::size_t slash = path_.rfind('/');
        if (slash == std::string::npos) {
            position = path_;
            path_.clear();
        } else {
            position = path_.substr(slash + 1);
            path_.resize(slash);
        }
    }

    auto r = scan_list({});
    if (!position.empty())
        position_on(position);

    req_.scan.no_enter = false;
    return r;
}

Ex<void> BrowserSession::rescan() {
    return scan_list({});
}

Ex<void> BrowserSession::change_dir(std::string_view name) {
    if (name.empty() || name == "..") {
        if (path_.empty())
            return {};

        std::string remembered;
        const std::size_t slash = path_.rfind('/');
        if (slash == std::string::npos) {
            remembered = path_;
            path_.clear();
        } else {
            remembered = path_.substr(slash + 1);
            path_.resize(slash);
        }
        if (remembered.size() > kRememberMax)
            remembered.resize(kRememberMax);
        auto r = scan_list({});
        if (!remembered.empty())
            select_dir_named(remembered);
        return r;
    }

    if (path_.size() + name.size() > kPathBudget) {
        refusals_.add();
        return std::unexpected(Error{Errc::io, ERR_SITE(), 0});
    }
    if (!path_.empty())
        path_.push_back('/');
    path_.append(name);
    return scan_list({});
}

void BrowserSession::enter_recents(std::vector<BrowserEntry> rows) {
    filter_.clear();
    filter_deadline_.reset();
    entries_ = static_cast<std::vector<BrowserEntry>&&>(rows);
    view_.set_items(entries_);
    view_.select(RowIndex{0});
    recents_mode_ = true;
}

Ex<void> BrowserSession::exit_recents() {
    return scan_list({});
}

void BrowserSession::type_ahead(char c, std::chrono::nanoseconds now) {
    if (recents_mode_)
        return;
    if (!filter_deadline_ || now >= *filter_deadline_) {
        filter_.assign(1, c);
        (void)scan_list({});
        seek_letter(c);
    } else if (filter_.size() < kFilterMax) {
        filter_.push_back(c);
        (void)scan_list(filter_);
    }
    filter_deadline_ = now + kFilterTimeout;
}

std::string BrowserSession::selected_full_path() const {
    const auto e = view_.selected();
    if (!e)
        return {};

    if (recents_mode_)
        return app::RecentsStore::join(e->dir, e->name);
    if (path_.empty())
        return e->name;
    std::string p = path_;
    p.push_back('/');
    p.append(e->name);
    return p;
}

std::string BrowserSession::resolve_content_home() const {
    if (req_.games_dir.empty())
        return req_.home_path;
    std::string dir = req_.games_dir;
    if (req_.policy.saves_home)
        return "saves/" + dir;
    const svc::SearchPolicy prefixed{svc::search::kPrefixed, true};
    if (auto p = src_->resolve(dir, prefixed))
        return *p;
    if (auto p = src_->resolve("games/" + dir, prefixed))
        return *p;
    return "games/" + dir;
}

Ex<void> BrowserSession::scan_list(std::string_view type_filter) {
    recents_mode_ = false;
    entries_.clear();
    view_.set_items({});
    view_.select(RowIndex{0});
    if (!src_) {
        refusals_.add();
        return std::unexpected(Error{Errc::io, ERR_SITE(), 0});
    }

    svc::ScanFilter f{};
    f.extensions = ext_;
    f.directories = req_.scan.include_dirs || req_.scan.cores_only;
    f.files = true;
    f.zip_as_directory = req_.scan.include_dirs && !req_.scan.no_zip;
    f.archive_browse = req_.scan.archive_browse;

    ++scans_;
    auto listing = src_->scan(path_, f);
    if (!listing)
        return std::unexpected(listing.error());

    for (svc::DirEntry& d : *listing) {

        if (!type_filter.empty() && !infra::icontains(d.name, type_filter))
            continue;

        if (d.is_dir
            && !req_.scan.include_dirs
            && req_.scan.cores_only
            && d.name != ".."
            && !d.name.starts_with('_')) {
            continue;
        }
        BrowserEntry b;
        b.kind = kind_of(d);
        b.name = static_cast<std::string&&>(d.name);
        if (req_.scan.archive_browse) {
            if (!assign_romset_display(b))
                continue;
        } else {
            assign_display(b);
        }
        entries_.push_back(static_cast<BrowserEntry&&>(b));
    }

    std::sort(entries_.begin(), entries_.end(), entry_less);

    if (req_.recent_entry)
        entries_.insert(entries_.begin(), recent_entry_row());
    view_.set_items(entries_);
    view_.select(RowIndex{0});
    return {};
}

void BrowserSession::assign_display(BrowserEntry& e) const {
    e.altname = e.name;
    if (e.is_dir())
        return;

    const bool xml = infra::iends_with(e.altname, ".mgl") || infra::iends_with(e.altname, ".mra");
    const bool rbf = infra::iends_with(e.altname, ".rbf");
    if (rbf || xml) {
        e.altname.resize(e.altname.size() - kDotExt.size());
        if (rbf) {

            constexpr std::size_t kCoreDateSkip = 3;
            constexpr std::size_t kCoreDateMin = 6;
            constexpr std::size_t kCoreDateMax = 15;
            const std::size_t p = e.altname.find("_20");
            if (p != std::string::npos && e.altname.size() - p - kCoreDateSkip >= kCoreDateMin) {
                e.datecode = e.altname.substr(p + kCoreDateSkip, kCoreDateMax);
                e.altname.resize(p);
            } else {
                e.datecode = "------";
            }
        }
        return;
    }

    constexpr std::size_t kShortExtChars = 3;
    if (!req_.scan.cores_only && ext_.size() > kShortExtChars)
        return;
    if (ext_.find('*') != std::string::npos || ext_.find('?') != std::string::npos)
        return;
    const std::size_t dot = e.altname.rfind('.');
    if (dot != std::string::npos)
        e.altname.resize(dot);
}

bool BrowserSession::assign_romset_display(BrowserEntry& e) const {
    e.altname = e.name;

    if (e.kind == EntryKind::ZipDir)
        e.kind = EntryKind::Dir;
    if (infra::iends_with(e.altname, ".zip"))
        e.altname.resize(e.altname.size() - kDotExt.size());
    if (!req_.aliases || e.name == "..")
        return true;
    const app::RomsetAlias ra = req_.aliases->query(
        app::BrowseDirs{path_, home_},
        app::EntryLookup{e.name, e.altname}
    );
    if (app::drops(ra))
        return false;
    if (const auto alias = app::alias_of(ra)) {
        e.kind = EntryKind::File;
        e.altname = *alias;
    }
    return true;
}

std::optional<RowIndex> BrowserSession::find_named(
    std::string_view name,
    NameField field,
    bool dirs_only
) const {
    const RowSpan<const BrowserEntry> list{entries_};
    std::optional<RowIndex> folded;
    for (const RowIndex i : rows(list.size())) {
        const BrowserEntry& e = list[i];
        if (e.is_recent() || (dirs_only && !e.is_dir()))
            continue;
        const std::string_view have = field == NameField::Altname ? e.altname : e.name;
        if (have == name)
            return i;
        if (infra::ieq(have, name))
            folded = i;
    }
    return folded;
}

void BrowserSession::position_on(std::string_view raw_name) {
    if (const std::optional<RowIndex> at = find_named(raw_name, NameField::Name, false)) {
        view_.select(*at);
    }
}

void BrowserSession::select_dir_named(std::string_view name) {
    if (const std::optional<RowIndex> at = find_named(name, NameField::Altname, true)) {
        view_.select(*at);
    }
}

void BrowserSession::seek_letter(char c) {
    const char want = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    (void)view_.select_next(
        [want](const BrowserEntry& e) {
            const char have = e.altname.empty()
                ? '\0'
                : static_cast<char>(std::toupper(static_cast<unsigned char>(e.altname[0])));
            return have == want;
        },
        Wrap::Yes
    );
}

void BrowserSession::next_group() {
    if (recents_mode_)
        return;
    const bool cores = req_.scan.cores_only;
    const auto cur = view_.selected();
    if (!cur)
        return;
    const char cur_char = group_char(*cur, cores);
    const bool cur_dir = cur->is_dir();
    (void)view_.select_next(
        [&](const BrowserEntry& e) {
            return group_char(e, cores) != cur_char || e.is_dir() != cur_dir;
        },
        Wrap::No
    );
}

char BrowserSession::selected_group() const noexcept {
    if (recents_mode_)
        return '\0';
    const auto e = view_.selected();
    if (!e || e->is_recent())
        return '\0';
    const char c = group_char(*e, req_.scan.cores_only);
    return c >= 'A' && c <= 'Z' ? c : '#';
}

void BrowserSession::prev_group() {
    if (recents_mode_)
        return;
    const bool cores = req_.scan.cores_only;
    const RowSpan<const BrowserEntry> list{entries_};
    const RowIndex cur = view_.viewport().selected;
    if (cur >= row_after(list.size()) || cur == RowIndex{})
        return;
    const bool cur_dir = list[cur].is_dir();
    char cur_char = group_char(list[cur], cores);
    bool saw_change = false;
    for (RowIndex i = cur; i > RowIndex{};) {
        i -= kOneRow;
        const BrowserEntry& e = list[i];
        if (group_char(e, cores) != cur_char || e.is_dir() != cur_dir) {
            if (saw_change) {
                view_.select(i + kOneRow);
                return;
            }
            saw_change = true;
            char rc = e.altname.empty() ? '\0' : e.altname[0];
            if (rc == '_')
                rc = e.altname.size() > 1 ? e.altname[1] : '\0';
            cur_char = static_cast<char>(std::toupper(static_cast<unsigned char>(rc)));
        }
    }
}

}
