// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/file_table_backing.h"

namespace mister::svc {

namespace {

std::uint64_t cue_file_pos(const Track& t) noexcept {
    const std::int64_t pos = static_cast<std::int64_t>(t.start.v)
            * static_cast<std::int64_t>(t.sector_size)
        - t.offset;
    return pos > 0 ? static_cast<std::uint64_t>(pos) : 0u;
}

}

Ex<void> FileTableBacking::do_read_payload(
    Lba lba,
    const Track& t,
    std::uint32_t s_off,
    std::span<std::byte> out
) {
    IFile* f = (t.backing < file_count_) ? files_[t.backing].get() : nullptr;
    if (f == nullptr)
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), t.backing});
    const std::int64_t pos = static_cast<std::int64_t>(lba.v)
            * static_cast<std::int64_t>(t.sector_size)
        - t.offset
        + static_cast<std::int64_t>(s_off);
    if (pos < 0)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), lba.v});
    auto rd = f->read_at(static_cast<std::uint64_t>(pos), out);
    if (!rd)
        return std::unexpected(rd.error());
    if (*rd != out.size()) {
        return std::unexpected(
            Error{Errc::short_read, ERR_SITE(), static_cast<std::uint32_t>(*rd)}
        );
    }
    return {};
}

void FileTableBacking::advise_first_window(const Track& t0) noexcept {
    if (t0.backing < file_count_ && files_[t0.backing]) {
        constexpr std::uint64_t kFirstWindowBytes = 1u * 1024u * 1024u;
        (void)files_[t0.backing]
            ->advise(IFile::Access::WillNeed, cue_file_pos(t0), kFirstWindowBytes);
    }
}

void FileTableBacking::advise_ahead(Lba lba, const Track& t, std::uint32_t sectors) noexcept {
    IFile* const f = (t.backing < file_count_) ? files_[t.backing].get() : nullptr;
    if (f == nullptr)
        return;
    const std::int64_t pos = static_cast<std::int64_t>(lba.v)
            * static_cast<std::int64_t>(t.sector_size)
        - t.offset;
    if (pos < 0)
        return;
    const std::uint64_t len = static_cast<std::uint64_t>(sectors) * t.sector_size;

    (void)f->advise(IFile::Access::WillNeed, static_cast<std::uint64_t>(pos), len);
}

}
