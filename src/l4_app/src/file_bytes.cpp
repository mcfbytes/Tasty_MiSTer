// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/file_bytes.h"

#include "app/mgl_player.h"
#include "cores/boot_asset.h"
#include "cores/file_tx.h"
#include "infra/indexed_array.h"
#include "infra/opt_ref.h"

namespace mister::app {

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
        std::vector<std::uint8_t>{}.swap(infra::at_index(heap_, *i));
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
Ex<proto::SlotIndex> FileBytes<P>::stage_(std::string_view path) noexcept {
    const auto i = slab_.producer().stage();
    if (!i)
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});
    const infra::OptRef<InternedFile> s = slab_.producer().staged(*i);
    if (!s)
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});
    *s = InternedFile{};
    if (!s->path.assign(path)) {
        slab_.producer().unstage(*i);
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    return *i;
}

template <SeatTag P>
std::optional<proto::SlotIndex> FileBytes<P>::pooled_slot_(proto::FileId id) noexcept {
    const std::uint32_t id_v = id.v;
    if (id_v == InternedFile::kNone || id_v > InternedFile::kRecycleSlots)
        return std::nullopt;
    return proto::SlotIndex::of(id_v - 1u);
}

template <SeatTag P>
infra::OptRef<InternedFile> FileBytes<P>::staged_payload_(proto::FileId id) noexcept {
    const std::optional<proto::SlotIndex> slot = pooled_slot_(id);
    if (!slot)
        return {};
    return slab_.producer().staged(*slot);
}

template <SeatTag P>
Ex<proto::FileId> FileBytes<P>::intern(FileIntern&& file) {

    if (file.bytes.empty() && (file.piece.whole.empty() || file.piece.offset != file.piece.whole))
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});

    if (file.bytes.empty() && (file.save != proto::FileId{} || file.edge != ContentEdge::Content))
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    reap_all_();
    if (live_files_() >= InternedFile::kMaxLiveFiles) {
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), InternedFile::kMaxLiveFiles});
    }
    const auto i = stage_(file.path);
    if (!i)
        return std::unexpected(i.error());
    const infra::OptRef<InternedFile> staged = slab_.producer().staged(*i);
    if (!staged)
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});
    InternedFile& s = *staged;
    if (!s.ext.assign(file.ext)) {
        slab_.producer().unstage(*i);
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    infra::at_index(heap_, *i) = std::move(file.bytes);
    s.bytes = infra::at_index(heap_, *i);
    s.load_addr = file.load_addr;
    s.size_bytes = file.piece.whole.empty() ? proto::FileSize{s.bytes.size()} : file.piece.whole;

    s.offset = proto::detail::positional_word(file.piece.offset);
    s.crc = file.crc;
    s.save = file.save;
    s.edge = file.edge;
    return proto::FileId::of_slot(*i);
}

template <SeatTag P>
Ex<proto::FileId> FileBytes<P>::intern_path(
    std::string_view path,
    proto::FileSize size,
    svc::Crc32 crc,
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
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});
    InternedFile& s = *staged;
    s.size_bytes = size;
    s.crc = crc;
    s.aux = aux;
    return proto::FileId::of_slot(*i);
}

template <SeatTag P>
void FileBytes<P>::unstage_path(proto::FileId file) noexcept {
    const std::optional<proto::SlotIndex> slot = pooled_slot_(file);
    if (slot)
        slab_.producer().unstage(*slot);
}

template <SeatTag P>
Ex<proto::FileId> FileBytes<P>::intern_empty(FileIntern&& file) {
    if (!file.bytes.empty() || file.path.empty()) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    reap_all_();
    const auto i = stage_(file.path);
    if (!i)
        return std::unexpected(i.error());
    const infra::OptRef<InternedFile> staged = slab_.producer().staged(*i);
    if (!staged)
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});
    InternedFile& s = *staged;
    s.crc = file.crc;
    s.save = file.save;
    s.edge = file.edge;
    return proto::FileId::of_slot(*i);
}

template <SeatTag P>
bool FileBytes<P>::stamp_remount(proto::FileId file) noexcept {
    const infra::OptRef<InternedFile> s = staged_payload_(file);
    if (!s)
        return false;
    s->remounts_saves = true;
    return true;
}

template <SeatTag P>
infra::OptRef<const InternedFile> FileBytes<P>::get(proto::FileId fid) const noexcept {
    const std::optional<proto::SlotIndex> slot = pooled_slot_(fid);
    if (!slot)
        return {};
    const infra::OptRef<const InternedFile> s = slab_.consumer().held(*slot);
    if (!s || (s->bytes.empty() && s->path.empty()))
        return {};
    return s;
}

template class FileBytes<SeatTag::Main>;
template class FileBytes<SeatTag::Ui>;
template class FileBytes<SeatTag::Input>;

}
