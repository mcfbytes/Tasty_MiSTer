// SPDX-License-Identifier: GPL-3.0-or-later
#include "browse_rules.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <set>
#include <utility>

#include "infra/ascii.h"
#include "svc/asset_name.h"
#include "vfs_detail.h"

namespace mister::svc::detail {

using infra::iends_with;
using infra::ieq;
using infra::istarts_with;

Admit admit_entry(std::string_view name, EntryKind kind, BrowseDepth depth, const ScanFilter& f) {
    if (f.archive_browse) {

        if (kind == EntryKind::File && iends_with(name, ".zip"))
            kind = EntryKind::Dir;
        const std::string_view admitted = *f.archive_browse;
        if (kind == EntryKind::File && !asset_name::extension_matches(name, admitted)) {
            return Admit::Reject;
        }
        if (name == "..") {
            if (depth == BrowseDepth::Root)
                return Admit::Reject;
        } else if (istarts_with(name, ".")) {
            return Admit::Reject;
        }
        return kind == EntryKind::Dir ? Admit::KeepDir : Admit::KeepFile;
    }

    if (kind == EntryKind::Dir) {

        if (name == "System Volume Information")
            return Admit::Reject;
        if (name == "..") {
            if (depth == BrowseDepth::Root)
                return Admit::Reject;
        } else if (istarts_with(name, ".")) {
            return Admit::Reject;
        }

        return f.directories ? Admit::KeepDir : Admit::Reject;
    }

    if (istarts_with(name, "."))
        return Admit::Reject;
    if (ieq(name, "menu.rbf"))
        return Admit::Reject;
    if (istarts_with(name, "menu_20"))
        return Admit::Reject;
    if (istarts_with(name, "boot") && iends_with(name, ".rom")) {
        constexpr std::size_t kBootRomLen = 8;
        constexpr std::size_t kBootRomDigitLen = 9;
        constexpr std::size_t kBootRomDigitPos = 4;
        const bool digit = name.size() == kBootRomDigitLen
            && name[kBootRomDigitPos] >= '0'
            && name[kBootRomDigitPos] <= '9';
        if (name.size() == kBootRomLen || digit)
            return Admit::Reject;
    }

    if (!f.extensions.empty()) {

        if (f.zip_as_directory && f.directories && iends_with(name, ".zip")) {
            return Admit::KeepDir;
        }
        if (!asset_name::extension_matches(name, f.extensions))
            return Admit::Reject;
    }
    return f.files ? Admit::KeepFile : Admit::Reject;
}

namespace {

bool is_dot_component(std::string_view n) noexcept {
    return n == "." || n == "..";
}

}

std::vector<DirEntry> zip_folder_entries(
    std::string_view folder,
    std::span<const ZipMember> members,
    const ScanFilter& f
) {
    std::vector<DirEntry> out;
    const BrowseDepth depth = folder.empty() ? BrowseDepth::Root : BrowseDepth::Nested;

    std::set<std::string, std::less<>> seen_dirs;
    const auto already_seen = [&seen_dirs](std::string_view n) {
        return seen_dirs.find(n) != seen_dirs.end();
    };

    for (const ZipMember& m : members) {

        if (const auto rel = relative_member(folder, m.name)) {
            const std::size_t slash = rel->find('/');
            if (slash != std::string_view::npos) {
                const std::string_view dirname = rel->substr(0, slash);
                if (!rel->empty()
                    && rel->front() != '/'
                    && !is_dot_component(dirname)
                    && !already_seen(dirname)) {
                    seen_dirs.emplace(dirname);
                    if (f.directories) {
                        DirEntry e;
                        e.name = std::string(dirname);
                        e.is_dir = true;
                        e.is_zip_member = true;
                        out.push_back(std::move(e));
                    }
                }
            }
        }

        if (!in_same_folder(folder, m.name))
            continue;

        std::string_view sub = std::string_view{m.name}.substr(folder.size());
        if (!sub.empty() && sub.front() == '/')
            sub.remove_prefix(1);
        if (sub.empty())
            continue;

        if (m.is_dir)
            continue;

        const Admit verdict = admit_entry(sub, EntryKind::File, depth, f);
        if (verdict == Admit::Reject)
            continue;

        DirEntry e;
        e.name = std::string(sub);
        e.is_dir = (verdict == Admit::KeepDir);
        e.is_zip_member = true;
        e.size = m.size;
        out.push_back(std::move(e));
    }

    DirEntry up;
    up.name = "..";
    up.is_dir = true;
    up.is_zip_member = true;
    out.push_back(std::move(up));

    std::stable_sort(out.begin(), out.end(), dirent_less);
    return out;
}

bool dirent_less(const DirEntry& a, const DirEntry& b) noexcept {

    const bool a_up = a.is_dir && a.name == "..";
    const bool b_up = b.is_dir && b.name == "..";
    if (a_up || b_up) {
        if (a_up && b_up)
            return false;
        return a_up;
    }
    if (a.is_dir != b.is_dir)
        return a.is_dir;

    constexpr std::size_t kExtStrip = kExtDot.size() + 3;
    std::size_t l1 = a.name.size();
    std::size_t l2 = b.name.size();
    if (l1 > kExtStrip && a.name[l1 - kExtStrip] == kExtDot.front())
        l1 -= kExtStrip;
    if (l2 > kExtStrip && b.name[l2 - kExtStrip] == kExtDot.front())
        l2 -= kExtStrip;

    const std::size_t width = (l1 < l2) ? l1 : l2;
    const std::strong_ordering r = infra::icompare_n(a.name, b.name, infra::ByteCount{width});
    if (r != 0)
        return r < 0;
    if (l1 != l2)
        return l1 < l2;

    return false;
}

}
