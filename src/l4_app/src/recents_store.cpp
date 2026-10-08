// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/recents_store.h"

#include <algorithm>
#include <cstring>

#include "app/durable_write.h"
#include "svc/vfs.h"

namespace mister::app {

namespace {

void put_field(std::byte* dst, std::size_t cap, std::string_view s) {
    const std::size_t n = std::min(s.size(), cap - 1);
    std::memcpy(dst, s.data(), n);
}

std::string_view get_field(const std::byte* p, std::size_t cap) {
    const char* c = reinterpret_cast<const char*>(p);
    return std::string_view{c, ::strnlen(c, cap)};
}

int index_on_disk(RecentsStore::ListId id) noexcept {
    switch (id.kind) {
        case RecentsStore::ListId::Kind::Cores:
            return -1;
        case RecentsStore::ListId::Kind::File:
            return id.slot;
        case RecentsStore::ListId::Kind::Mount:
            return id.slot + 500;
    }
    return -1;
}

}  // namespace

std::string RecentsStore::file_name(std::string_view core, ListId id) {
    const int idx = index_on_disk(id);
    if (idx < 0) return "cores_recent.cfg";
    std::string n{core};
    n += "_recent_";
    n += std::to_string(idx);
    n += ".cfg";
    return n;
}

std::string RecentsStore::join(std::string_view dir, std::string_view name) {
    if (dir.empty()) return std::string{name};
    std::string p{dir};
    p.push_back('/');
    p.append(name);
    return p;
}

Ex<std::vector<std::byte>> RecentsStore::read_raw(std::string_view core, ListId id) {
    std::vector<std::byte> buf(kFileSize, std::byte{0});
    auto f = vfs_->open("config/" + file_name(core, id), svc::OpenMode::ReadWhole);
    if (!f) return std::unexpected(f.error());
    std::size_t off = 0;
    while (off < buf.size()) {
        auto n = (*f)->read_at(off, std::span{buf}.subspan(off));
        if (!n) return std::unexpected(n.error());
        if (*n == 0) break;
        off += *n;
    }
    return buf;
}

std::vector<RecentsStore::Entry> RecentsStore::load(std::string_view core, ListId id) {
    TASTY_SEAT_BODY(RecentsStore);
    std::vector<Entry> out;
    if (!enabled_) return out;
    ++loads_;
    const auto raw = read_raw(core, id);
    if (!raw) return out;

    for (std::size_t i = 0; i < kMaxEntries; ++i) {
        const std::byte* rec = raw->data() + i * kRecordSize;
        const std::string_view name = get_field(rec + kDirCap, kNameCap);
        if (name.empty()) break;
        Entry e;
        e.dir = std::string{get_field(rec, kDirCap)};
        e.name = std::string{name};
        e.label = std::string{get_field(rec + kDirCap + kNameCap, kLabelCap)};
        e.exists = vfs_->file_exists(join(e.dir, e.name));
        out.push_back(static_cast<Entry&&>(e));
    }
    return out;
}

Ex<void> RecentsStore::update(std::string_view core, ListId id, std::string_view full_path,
                              std::string_view label) {
    TASTY_SEAT_BODY(RecentsStore);
    if (!enabled_ || full_path.empty()) return {};

    const std::size_t slash = full_path.rfind('/');
    const std::string_view dir =
        (slash == std::string_view::npos) ? std::string_view{} : full_path.substr(0, slash);
    const std::string_view name =
        (slash == std::string_view::npos) ? full_path : full_path.substr(slash + 1);
    if (label.empty()) label = name;

    auto raw = read_raw(core, id);
    std::vector<std::byte> buf = raw ? static_cast<std::vector<std::byte>&&>(*raw)
                                     : std::vector<std::byte>(kFileSize, std::byte{0});

    std::size_t erase = kMaxEntries - 1;
    for (std::size_t i = 0; i < kMaxEntries; ++i) {
        const std::byte* rec = buf.data() + i * kRecordSize;
        if (get_field(rec, kDirCap) == dir && get_field(rec + kDirCap, kNameCap) == name) {
            erase = i;
            break;
        }
    }
    if (erase != 0) {
        std::memmove(buf.data() + kRecordSize, buf.data(), kRecordSize * erase);
    }
    std::byte* front = buf.data();
    std::memset(front, 0, kRecordSize);
    put_field(front, kDirCap, dir);
    put_field(front + kDirCap, kNameCap, name);
    put_field(front + kDirCap + kNameCap, kLabelCap, label);

    auto r = durable_write(*vfs_, "config/" + file_name(core, id), buf);
    if (!r) {
        ++errors_;
        return r;
    }
    ++updates_;
    return {};
}

Ex<void> RecentsStore::clear(std::string_view core, ListId id) {
    TASTY_SEAT_BODY(RecentsStore);
    const std::vector<std::byte> zeroed(kFileSize, std::byte{0});
    auto r = durable_write(*vfs_, "config/" + file_name(core, id), zeroed);
    if (!r) ++errors_;
    return r;
}

}  // namespace mister::app
