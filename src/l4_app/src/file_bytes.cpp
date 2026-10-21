// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/file_bytes.h"

#include "app/mgl.h"
#include "cores/boot_asset.h"
#include "cores/config_slots.h"
#include "cores/file_tx.h"
#include "infra/opt_ref.h"

namespace mister::app {

static_assert(InternedFile::kConfigSlots == cores::kMaxConfigSlots);

static_assert(InternedFile::config_id(cores::kMaxConfigSlots - 1).v == InternedFile::kSlots - 1);
static_assert(InternedFile::config_id(0).v > InternedFile::kRecycleSlots);

static_assert(
    InternedFile::kMaxLiveFiles
    == cores::kMaxStartAssets
        + (MglPlayer::kMaxItems - 1)
        + cores::kMaxBootBuffers
        + InternedFile::kHeldOps
);
static_assert(InternedFile::kMaxLiveFiles <= InternedFile::kRecycleSlots);

static_assert(InternedFile::kPieceBytes % cores::kFileTxChunkBytes == 0);

template <SeatTag P>
void FileBytes<P>::reap_all_() noexcept {
    for (std::size_t n = 0; n < kPool; ++n) {
        const auto i = slab_.producer().reap();
        if (!i)
            return;
        std::vector<std::uint8_t>{}.swap(heap_[*i]);
    }
}

template <SeatTag P>
std::uint16_t FileBytes<P>::live_files_() const noexcept {
    std::uint16_t n = 0;
    for (const auto& h : heap_) {
        if (!h.empty())
            ++n;
    }
    return n;
}

template <SeatTag P>
std::size_t FileBytes<P>::retained_bytes() const noexcept {
    std::size_t n = 0;
    for (const auto& h : heap_)
        n += h.capacity();
    return n;
}

template <SeatTag P>
Ex<std::uint8_t> FileBytes<P>::stage_(std::string_view path) noexcept {
    const auto i = slab_.producer().stage();
    if (!i)
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});
    const infra::OptRef<InternedFile> s = slab_.producer().staged(*i);
    if (!s)
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), *i});
    *s = InternedFile{};
    if (!s->path.assign(path)) {
        slab_.producer().unstage(*i);
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), *i});
    }
    return *i;
}

template <SeatTag P>
Ex<proto::FileId> FileBytes<P>::intern(
    std::vector<std::uint8_t>&& bytes,
    std::string_view ext,
    std::string_view path,
    std::uint32_t load_addr,
    std::uint32_t crc,
    std::uint64_t whole,
    std::uint64_t offset
) {

    if (bytes.empty() && (whole == 0 || offset != whole)) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    reap_all_();
    if (live_files_() >= InternedFile::kMaxLiveFiles) {
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), InternedFile::kMaxLiveFiles});
    }
    const auto i = stage_(path);
    if (!i)
        return std::unexpected(i.error());
    const infra::OptRef<InternedFile> staged = slab_.producer().staged(*i);
    if (!staged)
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), *i});
    InternedFile& s = *staged;
    if (!s.ext.assign(ext)) {
        slab_.producer().unstage(*i);
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), *i});
    }
    heap_[*i] = std::move(bytes);
    s.bytes = heap_[*i];
    s.load_addr = load_addr;
    s.size_bytes = whole != 0 ? whole : s.bytes.size();
    s.offset = offset;
    s.crc = crc;
    return proto::FileId{static_cast<std::uint16_t>(*i + 1)};
}

template <SeatTag P>
Ex<proto::FileId> FileBytes<P>::intern_path(
    std::string_view path,
    std::uint64_t size_bytes,
    std::uint32_t crc,
    std::uint32_t aux
) {
    if (path.empty()) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    reap_all_();
    const auto i = stage_(path);
    if (!i)
        return std::unexpected(i.error());
    const infra::OptRef<InternedFile> staged = slab_.producer().staged(*i);
    if (!staged)
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), *i});
    InternedFile& s = *staged;
    s.size_bytes = size_bytes;
    s.crc = crc;
    s.aux = aux;
    return proto::FileId{static_cast<std::uint16_t>(*i + 1)};
}

template <SeatTag P>
Ex<proto::FileId> FileBytes<P>::intern_at(
    proto::FileId fid,
    std::vector<std::uint8_t>&& bytes,
    std::string_view ext,
    std::string_view path,
    std::uint32_t load_addr
) {
    const std::uint16_t id = fid.v;
    if (id <= InternedFile::kRecycleSlots || id >= InternedFile::kSlots) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), id});
    }
    InternedFile& s = config_[id - InternedFile::kConfigBase];
    std::vector<std::uint8_t>& h = config_heap_[id - InternedFile::kConfigBase];
    const std::uint64_t n = bytes.size();
    h = std::move(bytes);
    s.bytes = h;
    if (!ext.empty() && !s.ext.assign(ext)) {
        h.clear();
        s.bytes = {};
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), id});
    }
    if (!path.empty() && !s.path.assign(path)) {
        h.clear();
        s.bytes = {};
        s.ext.clear();
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), id});
    }
    if (ext.empty())
        s.ext.clear();
    if (path.empty())
        s.path.clear();
    s.load_addr = load_addr;
    s.size_bytes = n;
    s.crc = 0;
    s.save = {};
    s.aux = 0;
    s.edge = ContentEdge::Content;
    return fid;
}

template <SeatTag P>
bool FileBytes<P>::stamp_save(proto::FileId file, proto::FileId save) noexcept {
    const std::uint16_t id = file.v;
    if (id == InternedFile::kNone || id >= InternedFile::kSlots)
        return false;
    infra::OptRef<InternedFile> s = id <= InternedFile::kRecycleSlots
        ? slab_.producer().staged(static_cast<std::uint8_t>(id - 1))
        : infra::OptRef<InternedFile>{config_[id - InternedFile::kConfigBase]};
    if (!s || s->bytes.empty())
        return false;
    s->save = save;
    return true;
}

template <SeatTag P>
bool FileBytes<P>::stamp_edge(proto::FileId file, ContentEdge edge) noexcept {
    const std::uint16_t id = file.v;
    if (id == InternedFile::kNone || id >= InternedFile::kSlots)
        return false;
    infra::OptRef<InternedFile> s = id <= InternedFile::kRecycleSlots
        ? slab_.producer().staged(static_cast<std::uint8_t>(id - 1))
        : infra::OptRef<InternedFile>{config_[id - InternedFile::kConfigBase]};
    if (!s || s->bytes.empty())
        return false;
    s->edge = edge;
    return true;
}

template <SeatTag P>
const InternedFile* FileBytes<P>::get(proto::FileId fid) const noexcept {
    const std::uint16_t id = fid.v;
    infra::OptRef<const InternedFile> s{};
    if (id != InternedFile::kNone && id <= InternedFile::kRecycleSlots) {
        s = slab_.consumer().held(static_cast<std::uint8_t>(id - 1));
    } else if (id > InternedFile::kRecycleSlots && id < InternedFile::kSlots) {
        s = infra::OptRef<const InternedFile>{config_[id - InternedFile::kConfigBase]};
    }
    if (!s || (s->bytes.empty() && s->path.empty()))
        return nullptr;
    return &*s;
}

template class FileBytes<SeatTag::Main>;
template class FileBytes<SeatTag::Ui>;
template class FileBytes<SeatTag::Input>;

}
