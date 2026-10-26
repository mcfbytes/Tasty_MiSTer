// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/recent_games.h"

#include <algorithm>
#include <cctype>
#include <chrono>

#include "infra/ascii.h"
#include "os/clock.h"
#include "svc/vfs.h"

namespace mister::app {

namespace {

using infra::ieq;

bool iless(std::string_view a, std::string_view b) noexcept {
    return std::lexicographical_compare(
        a.begin(),
        a.end(),
        b.begin(),
        b.end(),
        [](char x, char y) noexcept { return infra::ascii_lower(x) < infra::ascii_lower(y); }
    );
}

std::int64_t now_ns(infra::OptRef<const os::IClock> clock) {
    return clock ? static_cast<std::int64_t>(clock->now().count()) : 0;
}

struct RecentsFile {
    std::string core;
    RecentsStore::ListId id{};
    bool ok = false;
};

RecentsFile parse_recents_name(std::string_view name) {
    RecentsFile out;
    if (name.size() <= 5 || !ieq(name.substr(name.size() - 4), ".cfg"))
        return out;
    const std::string_view stem = name.substr(0, name.size() - 4);
    const std::size_t at = stem.rfind("_recent_");
    if (at == std::string_view::npos || at == 0)
        return out;
    const std::string_view digits = stem.substr(at + 8);
    if (digits.empty() || digits.size() > 3)
        return out;
    unsigned idx = 0;
    for (const char c : digits) {
        if (c < '0' || c > '9')
            return out;
        idx = idx * 10 + static_cast<unsigned>(c - '0');
    }
    out.core = std::string{stem.substr(0, at)};
    const auto slot = static_cast<std::uint8_t>(idx >= 500 ? idx - 500 : idx);
    out.id = RecentsStore::ListId{
        idx >= 500 ? RecentsStore::ListId::Kind::Mount : RecentsStore::ListId::Kind::File,
        slot
    };
    out.ok = true;
    return out;
}

bool same_list(const RecentsStore::ListId& a, const RecentsStore::ListId& b) noexcept {
    return a.kind == b.kind && a.slot == b.slot;
}

std::string datecode_of(std::string_view image) {
    const std::size_t slash = image.rfind('/');
    const std::string_view name = slash == std::string_view::npos ? image : image.substr(slash + 1);
    std::string tail{name.substr(std::min(name.size(), recent_core_name(name).size()))};
    for (char& ch : tail)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return tail;
}

}

std::string recent_core_name(std::string_view path_or_name) {
    const std::size_t slash = path_or_name.rfind('/');
    std::string_view name = slash == std::string_view::npos ? path_or_name
                                                            : path_or_name.substr(slash + 1);
    if (name.size() > 4) {
        const std::string_view ext = name.substr(name.size() - 4);
        if (ieq(ext, ".rbf") || ieq(ext, ".cfg"))
            name = name.substr(0, name.size() - 4);
    }

    const std::size_t p = name.find("_20");
    if (p != std::string_view::npos && name.size() - p - 3 >= 6)
        name = name.substr(0, p);
    return std::string{name};
}

std::string recent_rbf_prefix(std::string_view core_image) {
    const std::string stem = recent_core_name(core_image);
    if (stem.empty())
        return {};
    const std::size_t slash = core_image.rfind('/');
    if (slash == std::string_view::npos)
        return stem;
    std::string out{core_image.substr(0, slash + 1)};
    out += stem;
    return out;
}

const RecentGames::CoreName* RecentGames::core_named_(std::string_view stem) const noexcept {
    const auto at = std::lower_bound(
        cores_.begin(),
        cores_.end(),
        stem,
        [](const CoreName& c, std::string_view s) noexcept { return iless(c.stem, s); }
    );
    if (at == cores_.end() || !ieq(at->stem, stem))
        return nullptr;
    return &*at;
}

auto RecentGames::scan_cores_(const svc::Vfs& files) -> std::vector<CoreName> {
    std::vector<CoreName> cores;
    svc::ScanFilter dirs;
    dirs.directories = true;
    dirs.files = false;
    const auto top = files.scan("", dirs);
    if (!top)
        return cores;
    std::vector<std::string> folders;
    for (const svc::DirEntry& e : *top)
        if (e.is_dir && !e.name.empty() && e.name.front() == '_')
            folders.push_back(e.name);
    std::sort(folders.begin(), folders.end());

    svc::ScanFilter rbf;
    rbf.extensions = "RBF";
    rbf.directories = false;
    rbf.files = true;
    for (const std::string& folder : folders) {
        const auto entries = files.scan(folder, rbf);
        if (!entries)
            continue;
        for (const svc::DirEntry& e : *entries) {
            if (e.is_dir)
                continue;
            const std::string stem = recent_core_name(e.name);
            if (stem.empty())
                continue;
            const std::string image = folder + "/" + e.name;
            auto at = std::lower_bound(
                cores.begin(),
                cores.end(),
                stem,
                [](const CoreName& c, const std::string& s) noexcept { return iless(c.stem, s); }
            );
            if (at != cores.end() && ieq(at->stem, stem)) {

                if (datecode_of(at->image) < datecode_of(image))
                    at->image = image;
            } else {
                cores.insert(at, CoreName{stem, image});
            }
        }
    }
    return cores;
}

std::string RecentGames::core_image_of(const svc::Vfs& files, std::string_view core_name) {
    if (core_name.empty())
        return {};
    for (const CoreName& c : scan_cores_(files))
        if (ieq(c.stem, core_name))
            return c.image;
    return {};
}

void RecentGames::read_sources_(const svc::Vfs& files, RecentsStore& recents) {
    sources_.clear();
    files_read_ = 0;
    svc::ScanFilter cfg;
    cfg.extensions = "CFG";
    cfg.directories = false;
    cfg.files = true;
    const auto entries = files.scan("config", cfg);
    if (!entries)
        return;

    struct Found {
        std::string name;
        RecentsFile rf;
        std::int64_t mtime = 0;
    };

    std::vector<Found> found;
    for (const svc::DirEntry& e : *entries) {
        if (e.is_dir)
            continue;
        Found f;
        f.rf = parse_recents_name(e.name);
        if (!f.rf.ok)
            continue;
        f.name = e.name;
        const auto m = files.modified(std::string{"config/"} + e.name);
        f.mtime = m ? *m : 0;
        found.push_back(std::move(f));
    }
    std::sort(found.begin(), found.end(), [](const Found& a, const Found& b) {
        if (a.mtime != b.mtime)
            return a.mtime > b.mtime;
        return a.name < b.name;
    });
    if (found.size() > kFiles)
        found.resize(kFiles);

    for (const Found& f : found) {
        Source s;
        s.core = f.rf.core;
        s.id = f.rf.id;
        s.mtime = f.mtime;
        s.entries = recents.load(s.core, s.id);
        sources_.push_back(std::move(s));
        ++files_read_;
    }
}

void RecentGames::reread_source_(
    const svc::Vfs& files,
    RecentsStore& recents,
    const RecentGames::Played& session
) {
    if (session.slot == 0)
        return;
    const std::string core{
        session.core_name.empty() ? recent_core_name(session.core_image)
                                  : std::string{session.core_name}
    };
    if (core.empty())
        return;
    const RecentsStore::ListId id{
        session.slot == 'S' ? RecentsStore::ListId::Kind::Mount : RecentsStore::ListId::Kind::File,
        session.ioctl_index
    };

    bool found = false;
    for (Source& s : sources_) {
        if (!ieq(s.core, core) || !same_list(s.id, id))
            continue;
        s.entries = recents.load(s.core, s.id);
        const auto m = files.modified("config/" + RecentsStore::file_name(s.core, s.id));
        if (m)
            s.mtime = *m;
        ++rereads_;
        found = true;
    }
    if (found) {
        sort_sources_();
        return;
    }

    Source s;
    s.core = core;
    s.id = id;
    const auto m = files.modified("config/" + RecentsStore::file_name(core, id));
    s.mtime = m ? *m : 0;
    s.entries = recents.load(core, id);
    ++rereads_;
    if (s.entries.empty())
        return;
    sources_.push_back(std::move(s));
    sort_sources_();
}

void RecentGames::sort_sources_() {
    std::stable_sort(sources_.begin(), sources_.end(), [](const Source& a, const Source& b) {
        if (a.mtime != b.mtime)
            return a.mtime > b.mtime;
        return a.core < b.core;
    });
}

void RecentGames::assemble_(
    const RecentGames::Played& session,
    std::span<const PlayedHistory::Record> played
) {
    std::vector<Record> out;
    out.reserve(kRecords);
    const auto emit = [&out](Record r) {
        for (const Record& have : out)
            if (ieq(have.core, r.core) && have.path == r.path)
                return;
        if (out.size() < kRecords)
            out.push_back(std::move(r));
    };

    hidden_ = 0;
    if (session.names_a_game()) {
        Record r;
        r.core = recent_core_name(session.core_image);
        r.core_image.assign(session.core_image);
        r.rbf = recent_rbf_prefix(session.core_image);
        r.path.assign(session.path);
        r.title.assign(session.title);
        r.ioctl_index = session.ioctl_index;
        r.slot = session.slot;
        r.session = true;
        emit(std::move(r));
    }

    for (const PlayedHistory::Record& h : played) {
        if (h.core_image.empty()) {
            ++hidden_;
            continue;
        }
        Record r;
        r.core = recent_core_name(h.core_image);
        r.core_image = h.core_image;
        r.rbf = recent_rbf_prefix(h.core_image);
        r.path = h.path;
        r.title = h.title;
        r.ioctl_index = h.ioctl_index;
        r.slot = h.slot;
        emit(std::move(r));
    }

    std::size_t depth = 0;
    bool more = true;
    while (more) {
        more = false;
        for (const Source& src : sources_) {
            if (depth >= src.entries.size())
                continue;
            more = true;
            const CoreName* core = core_named_(src.core);
            if (core == nullptr) {
                ++hidden_;
                continue;
            }
            Record r;
            r.core = core->stem;
            r.core_image = core->image;
            r.rbf = recent_rbf_prefix(core->image);
            r.path = RecentsStore::join(src.entries[depth].dir, src.entries[depth].name);
            r.ioctl_index = src.id.slot;
            r.slot = src.id.kind == RecentsStore::ListId::Kind::Mount ? 'S' : 'F';
            emit(std::move(r));
        }
        ++depth;
    }
    records_ = std::move(out);
}

void RecentGames::refresh(
    const svc::Vfs& files,
    RecentsStore& recents,
    const RecentGames::Played& session,
    infra::OptRef<const os::IClock> clock,
    std::span<const PlayedHistory::Record> played
) {
    TASTY_SEAT_BODY(RecentGames);
    if (built_)
        return;
    built_ = true;
    const std::int64_t start = now_ns(clock);
    cores_ = scan_cores_(files);
    read_sources_(files, recents);
    assemble_(session, played);
    build_ns_ = now_ns(clock) - start;
}

void RecentGames::update(
    const svc::Vfs& files,
    RecentsStore& recents,
    const RecentGames::Played& session,
    infra::OptRef<const os::IClock> clock,
    std::span<const PlayedHistory::Record> played
) {
    TASTY_SEAT_BODY(RecentGames);
    if (!built_)
        return;
    const std::int64_t start = now_ns(clock);
    reread_source_(files, recents, session);
    assemble_(session, played);
    update_ns_max_ = std::max(update_ns_max_, now_ns(clock) - start);
}

}
