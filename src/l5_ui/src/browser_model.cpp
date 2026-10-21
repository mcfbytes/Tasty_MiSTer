// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/browser_session.h"

#include <algorithm>
#include <cctype>

#include "app/romset_aliases.h"
#include "infra/opt_ref.h"
#include "ui/browse_source.h"
#include "ui/browser_entry.h"
#include "ui/browser_host.h"

namespace mister::ui {

namespace {

char lower(char c) noexcept {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool ieq(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (lower(a[i]) != lower(b[i]))
            return false;
    }
    return true;
}

bool istarts_with(std::string_view s, std::string_view p) noexcept {
    return s.size() >= p.size() && ieq(s.substr(0, p.size()), p);
}

bool iends_with(std::string_view s, std::string_view p) noexcept {
    return s.size() >= p.size() && ieq(s.substr(s.size() - p.size()), p);
}

bool icontains(std::string_view s, std::string_view needle) noexcept {
    if (needle.empty())
        return true;
    if (needle.size() > s.size())
        return false;
    for (std::size_t i = 0; i + needle.size() <= s.size(); ++i) {
        if (ieq(s.substr(i, needle.size()), needle))
            return true;
    }
    return false;
}

int icompare_n(std::string_view a, std::string_view b, std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) {
        const int d = lower(a[i]) - lower(b[i]);
        if (d != 0)
            return d;
    }
    return 0;
}

int icompare(std::string_view a, std::string_view b) noexcept {
    const std::size_t n = a.size() < b.size() ? a.size() : b.size();
    const int r = icompare_n(a, b, n);
    if (r != 0)
        return r;
    return (a.size() < b.size()) ? -1 : (a.size() > b.size() ? 1 : 0);
}

bool entry_less(const BrowserEntry& a, const BrowserEntry& b) noexcept {
    const bool a_up = a.is_dir && a.altname == "..";
    const bool b_up = b.is_dir && b.altname == "..";
    if (a_up || b_up)
        return a_up && !b_up;
    if (a.is_dir != b.is_dir)
        return a.is_dir;

    std::size_t l1 = a.altname.size();
    std::size_t l2 = b.altname.size();
    if (l1 > 4 && a.altname[l1 - 4] == '.')
        l1 -= 4;
    if (l2 > 4 && b.altname[l2 - 4] == '.')
        l2 -= 4;

    const int r = icompare_n(a.altname, b.altname, l1 < l2 ? l1 : l2);
    if (r != 0)
        return r < 0;
    if (l1 != l2)
        return l1 < l2;
    return icompare(a.datecode, b.datecode) < 0;
}

char group_char(const BrowserEntry& e, bool cores) noexcept {
    char c = e.altname.empty() ? '\0' : e.altname[0];
    if (c == '_' && e.is_dir && cores)
        c = e.altname.size() > 1 ? e.altname[1] : '\0';
    return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}

BrowserEntry recent_entry_row() {
    BrowserEntry e;
    e.name = "Recent";
    e.altname = "Recent";
    e.is_dir = true;
    e.recent = true;
    return e;
}

}

Ex<void> BrowserSession::open(const IBrowseSource& src, Request req) {
    src_ = infra::OptRef<const IBrowseSource>{src};
    req_ = static_cast<Request&&>(req);
    filter_.clear();
    filter_deadline_ns_ = 0;
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

        const bool under_home = istarts_with(path_, home_);
        const bool is_home = ieq(home_, path_);
        const bool exists = src_->file_exists(path_) || src_->dir_exists(path_);
        if (!under_home || is_home || !exists) {
            req_.scan.no_enter = false;
            path_ = home_;
        }
    }
    while (ext_.size() % 3 != 0)
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
        ++refusals_;
        return std::unexpected(Error{Errc::io, ERR_SITE(), 0});
    }
    if (!path_.empty())
        path_.push_back('/');
    path_.append(name);
    return scan_list({});
}

void BrowserSession::enter_recents(std::vector<BrowserEntry> rows) {
    filter_.clear();
    filter_deadline_ns_ = 0;
    entries_ = static_cast<std::vector<BrowserEntry>&&>(rows);
    view_.set_items(entries_);
    view_.select(RowIndex{0});
    recents_mode_ = true;
}

Ex<void> BrowserSession::exit_recents() {
    return scan_list({});
}

void BrowserSession::type_ahead(char c, std::int64_t now_ns) {
    if (recents_mode_)
        return;
    if (now_ns >= filter_deadline_ns_) {
        filter_.assign(1, c);
        (void)scan_list({});
        seek_letter(c);
    } else if (filter_.size() < kFilterMax) {
        filter_.push_back(c);
        (void)scan_list(filter_);
    }
    filter_deadline_ns_ = now_ns + kFilterTimeoutNs;
}

std::string BrowserSession::selected_full_path() const {
    const BrowserEntry* e = view_.selected();
    if (e == nullptr)
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
        ++refusals_;
        return std::unexpected(Error{Errc::io, ERR_SITE(), 0});
    }

    svc::ScanFilter f{};
    f.extensions = ext_;
    f.directories = req_.scan.include_dirs || req_.scan.cores_only;
    f.files = true;
    f.zip_as_directory = req_.scan.include_dirs && !req_.scan.no_zip;
    f.romset_browse = req_.scan.romset_browse;

    ++scans_;
    auto listing = src_->scan(path_, f);
    if (!listing)
        return std::unexpected(listing.error());

    for (svc::DirEntry& d : *listing) {
        if (!type_filter.empty() && !icontains(d.name, type_filter))
            continue;

        if (d.is_dir
            && !req_.scan.include_dirs
            && req_.scan.cores_only
            && d.name != ".."
            && !d.name.starts_with('_')) {
            continue;
        }
        BrowserEntry b;
        b.is_dir = d.is_dir;
        b.from_zip = d.is_dir && !d.is_zip_member && iends_with(d.name, ".zip");
        b.name = static_cast<std::string&&>(d.name);
        if (req_.scan.romset_browse) {
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
    if (req_.host) {
        std::string alias = req_.host->display_alias(e.name, e.is_dir);
        if (!alias.empty()) {
            e.altname = static_cast<std::string&&>(alias);
            return;
        }
    }
    e.altname = e.name;
    if (e.is_dir)
        return;

    const bool xml = iends_with(e.altname, ".mgl") || iends_with(e.altname, ".mra");
    const bool rbf = iends_with(e.altname, ".rbf");
    if (rbf || xml) {
        e.altname.resize(e.altname.size() - 4);
        if (rbf) {
            const std::size_t p = e.altname.find("_20");
            if (p != std::string::npos && e.altname.size() - p - 3 >= 6) {
                e.datecode = e.altname.substr(p + 3, 15);
                e.altname.resize(p);
            } else {
                e.datecode = "------";
            }
        }
        return;
    }

    if (!req_.scan.cores_only && ext_.size() > 3)
        return;
    if (ext_.find('*') != std::string::npos || ext_.find('?') != std::string::npos)
        return;
    const std::size_t dot = e.altname.rfind('.');
    if (dot != std::string::npos)
        e.altname.resize(dot);
}

bool BrowserSession::assign_romset_display(BrowserEntry& e) const {
    e.altname = e.name;

    e.from_zip = false;
    if (iends_with(e.altname, ".zip"))
        e.altname.resize(e.altname.size() - 4);
    if (!req_.aliases || e.name == "..")
        return true;
    const app::RomsetAlias ra = req_.aliases->query(path_, home_, e.name, e.altname);
    if (ra.drop)
        return false;
    if (ra.alias) {
        e.is_dir = false;
        e.altname = *ra.alias;
    }
    return true;
}

void BrowserSession::position_on(std::string_view raw_name) {
    std::size_t pos = entries_.size();
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].recent)
            continue;
        if (entries_[i].name == raw_name) {
            pos = i;
            break;
        }
        if (ieq(entries_[i].name, raw_name))
            pos = i;
    }
    if (pos < entries_.size())
        view_.select(RowIndex{static_cast<std::uint32_t>(pos)});
}

void BrowserSession::select_dir_named(std::string_view name) {
    std::size_t pos = entries_.size();
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (!entries_[i].is_dir || entries_[i].recent)
            continue;
        if (entries_[i].altname == name) {
            pos = i;
            break;
        }
        if (ieq(entries_[i].altname, name))
            pos = i;
    }
    if (pos < entries_.size())
        view_.select(RowIndex{static_cast<std::uint32_t>(pos)});
}

void BrowserSession::seek_letter(char c) {
    if (entries_.empty())
        return;
    const char want = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    const std::size_t cur = view_.viewport().selected.v;
    const auto first_of = [&](std::size_t from, std::size_t to) -> std::size_t {
        for (std::size_t i = from; i < to; ++i) {
            const char have = entries_[i].altname.empty()
                ? '\0'
                : static_cast<char>(
                      std::toupper(static_cast<unsigned char>(entries_[i].altname[0]))
                  );
            if (have == want)
                return i;
        }
        return entries_.size();
    };
    std::size_t found = first_of(cur + 1, entries_.size());
    if (found == entries_.size())
        found = first_of(0, entries_.size());
    if (found < entries_.size())
        view_.select(RowIndex{static_cast<std::uint32_t>(found)});
}

void BrowserSession::next_group() {
    if (entries_.empty() || recents_mode_)
        return;
    const bool cores = req_.scan.cores_only;
    const std::size_t cur = view_.viewport().selected.v;
    if (cur >= entries_.size())
        return;
    const char cur_char = group_char(entries_[cur], cores);
    const bool cur_dir = entries_[cur].is_dir;
    for (std::size_t i = cur + 1; i < entries_.size(); ++i) {
        if (group_char(entries_[i], cores) != cur_char || entries_[i].is_dir != cur_dir) {
            view_.select(RowIndex{static_cast<std::uint32_t>(i)});
            return;
        }
    }
}

void BrowserSession::prev_group() {
    if (entries_.empty() || recents_mode_)
        return;
    const bool cores = req_.scan.cores_only;
    const std::size_t cur = view_.viewport().selected.v;
    if (cur >= entries_.size() || cur == 0)
        return;
    const bool cur_dir = entries_[cur].is_dir;
    char cur_char = group_char(entries_[cur], cores);
    bool saw_change = false;
    for (std::size_t i = cur; i-- > 0;) {
        if (group_char(entries_[i], cores) != cur_char || entries_[i].is_dir != cur_dir) {
            if (saw_change) {
                view_.select(RowIndex{static_cast<std::uint32_t>(i + 1)});
                return;
            }
            saw_change = true;
            char rc = entries_[i].altname.empty() ? '\0' : entries_[i].altname[0];
            if (rc == '_')
                rc = entries_[i].altname.size() > 1 ? entries_[i].altname[1] : '\0';
            cur_char = static_cast<char>(std::toupper(static_cast<unsigned char>(rc)));
        }
    }
}

}
