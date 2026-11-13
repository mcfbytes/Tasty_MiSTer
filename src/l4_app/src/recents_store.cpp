// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/recents_store.h"

#include <algorithm>
#include <cstring>
#include <span>

#include "app/text_field.h"
#include "app/ui_write_lane.h"
#include "infra/ascii.h"
#include "infra/parse_int.h"
#include "infra/path_parts.h"
#include "svc/vfs.h"

namespace mister::app {

namespace {

class RecentRecord {
public:
    using Bytes = std::span<std::byte, RecentsStore::kRecordSize>;

    explicit RecentRecord(Bytes bytes) noexcept : bytes_(bytes) {}

    [[nodiscard]] std::string_view dir() const noexcept {
        return field(bytes_.first<RecentsStore::kDirCap>()).text();
    }

    [[nodiscard]] std::string_view name() const noexcept {
        return field(bytes_.subspan<RecentsStore::kDirCap, RecentsStore::kNameCap>()).text();
    }

    [[nodiscard]] std::string_view label() const noexcept {
        return field(bytes_.subspan<
                         RecentsStore::kDirCap + RecentsStore::kNameCap,
                         RecentsStore::kLabelCap>())
            .text();
    }

    void assign(std::string_view dir, std::string_view name, std::string_view label) noexcept {
        std::fill(bytes_.begin(), bytes_.end(), std::byte{0});
        field(bytes_.first<RecentsStore::kDirCap>()).assign(dir);
        field(bytes_.subspan<RecentsStore::kDirCap, RecentsStore::kNameCap>()).assign(name);
        field(
            bytes_
                .subspan<RecentsStore::kDirCap + RecentsStore::kNameCap, RecentsStore::kLabelCap>()
        )
            .assign(label);
    }

private:
    template <std::size_t N>
    [[nodiscard]] static TextField<std::byte, N> field(std::span<std::byte, N> bytes) noexcept {
        return TextField<std::byte, N>{bytes};
    }

    Bytes bytes_;
};

class RecentRecords {
public:
    class Iterator {
    public:
        explicit Iterator(std::span<std::byte> rest) noexcept : rest_(rest) {}

        [[nodiscard]] RecentRecord operator*() const noexcept {
            return RecentRecord{rest_.first<RecentsStore::kRecordSize>()};
        }

        Iterator& operator++() noexcept {
            rest_ = rest_.subspan(RecentsStore::kRecordSize);
            return *this;
        }

        [[nodiscard]] bool operator!=(const Iterator& other) const noexcept {
            return rest_.data() != other.rest_.data();
        }

    private:
        std::span<std::byte> rest_;
    };

    explicit RecentRecords(std::span<std::byte> image) noexcept : image_(image) {}

    [[nodiscard]] Iterator begin() const noexcept { return Iterator{image_}; }

    [[nodiscard]] Iterator end() const noexcept { return Iterator{image_.subspan(image_.size())}; }

private:
    std::span<std::byte> image_;
};

}

std::string RecentsStore::ListId::file_name(std::string_view core) const {
    if (!row_)
        return "cores_recent.cfg";

    const proto::ListIndex index = row_->index.index_above(
        row_->kind == proto::FileRowKind::Mount ? kMountIndexBase : proto::ListIndex{}
    );
    std::string n{core};
    n += "_recent_";
    n += index.text().view();
    n += ".cfg";
    return n;
}

std::optional<std::pair<std::string, proto::FileRowId>> RecentsStore::ListId::from_file_name(
    std::string_view name
) {
    constexpr std::string_view kRecentTag = "_recent_";
    constexpr std::string_view kConfigExt = ".cfg";
    if (name.size() <= kConfigExt.size() + 1 || !infra::iends_with(name, kConfigExt))
        return std::nullopt;
    const std::string_view stem = name.substr(0, name.size() - kConfigExt.size());
    const std::size_t at = stem.rfind(kRecentTag);
    if (at == std::string_view::npos || at == 0)
        return std::nullopt;

    constexpr std::size_t kMaxIndexDigits = 3;
    const std::string_view digits = stem.substr(at + kRecentTag.size());
    if (digits.size() > kMaxIndexDigits)
        return std::nullopt;

    const auto parsed = infra::parse_int<std::uint32_t>(digits);
    if (!parsed)
        return std::nullopt;
    const proto::ListIndex index{*parsed};
    const bool mount = index.at_or_above(kMountIndexBase);
    const proto::ListIndex slot = mount ? index.since(kMountIndexBase) : index;
    if (!slot.names_slot())
        return std::nullopt;
    const proto::IoIndex io = slot.as_slot();
    return std::pair{
        std::string{stem.substr(0, at)},
        mount ? proto::FileRowId::mount(io) : proto::FileRowId::file(io)
    };
}

std::string RecentsStore::file_name(std::string_view core, ListId id) {
    return id.file_name(core);
}

std::string RecentsStore::join(std::string_view dir, std::string_view name) {
    if (dir.empty())
        return std::string{name};
    std::string p{dir};
    p.push_back('/');
    p.append(name);
    return p;
}

Ex<std::vector<std::byte>> RecentsStore::read_raw(std::string_view core, ListId id) {
    static_assert(UiWriteLane::kShadowBytes >= kFileSize, "a recents file fits the lane's shadow");
    const std::string rel = "config/" + file_name(core, id);

    auto armed = writes_->peek(rel);
    if (!armed)
        return std::unexpected(armed.error());
    std::vector<std::byte> buf(kFileSize, std::byte{0});
    if (!armed->empty()) {
        std::memcpy(buf.data(), armed->data(), std::min(armed->size(), buf.size()));
        return buf;
    }
    auto f = vfs_->open(rel, svc::OpenMode::ReadWhole);
    if (!f)
        return std::unexpected(f.error());
    std::size_t off = 0;
    while (off < buf.size()) {
        auto n = (*f)->read_at(off, std::span{buf}.subspan(off));
        if (!n)
            return std::unexpected(n.error());
        if (*n == 0)
            break;
        off += *n;
    }
    return buf;
}

std::vector<RecentsStore::Entry> RecentsStore::load(std::string_view core, ListId id) {
    TASTY_SEAT_BODY(RecentsStore);
    std::vector<Entry> out;
    if (!enabled_)
        return out;
    ++loads_;
    auto raw = read_raw(core, id);
    if (!raw)
        return out;

    const std::span<std::byte> image{raw->data(), raw->size()};
    for (const RecentRecord rec : RecentRecords{image}) {
        const std::string_view name = rec.name();
        if (name.empty())
            break;
        Entry e;
        e.dir = std::string{rec.dir()};
        e.name = std::string{name};
        e.label = std::string{rec.label()};
        e.exists = vfs_->file_exists(join(e.dir, e.name));
        out.push_back(static_cast<Entry&&>(e));
    }
    return out;
}

Ex<void> RecentsStore::update(
    std::string_view core,
    ListId id,
    std::string_view full_path,
    std::string_view label
) {
    TASTY_SEAT_BODY(RecentsStore);
    if (!enabled_ || full_path.empty())
        return {};

    const std::string_view dir = infra::dir_or_empty(full_path);
    const std::string_view name = infra::basename(full_path);
    if (label.empty())
        label = name;

    auto raw = read_raw(core, id);
    std::vector<std::byte> buf = raw ? static_cast<std::vector<std::byte>&&>(*raw)
                                     : std::vector<std::byte>(kFileSize, std::byte{0});

    const std::span<std::byte> image{buf.data(), buf.size()};
    std::size_t erase = kMaxEntries - 1;
    std::size_t at = 0;
    for (const RecentRecord rec : RecentRecords{image}) {
        if (rec.dir() == dir && rec.name() == name) {
            erase = at;
            break;
        }
        ++at;
    }
    if (erase != 0) {
        std::memmove(buf.data() + kRecordSize, buf.data(), kRecordSize * erase);
    }
    RecentRecord{image.first<kRecordSize>()}.assign(dir, name, label);

    const auto r = writes_->arm("config/" + file_name(core, id), buf, VerdictSink::Recents);
    if (!r)
        ++errors_;
    return r;
}

Ex<void> RecentsStore::clear(std::string_view core, ListId id) {
    TASTY_SEAT_BODY(RecentsStore);
    const std::vector<std::byte> zeroed(kFileSize, std::byte{0});
    const auto r = writes_->arm("config/" + file_name(core, id), zeroed, VerdictSink::Recents);
    if (!r)
        ++errors_;
    return r;
}

void RecentsStore::on_written(bool durable) noexcept {
    TASTY_SEAT_BODY(RecentsStore);
    if (durable) {
        ++updates_;
    } else {
        ++errors_;
    }
}

}
