// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/disc/file_table_backing.h"

#include <cstdint>
#include <optional>

#include "infra/prelude.h"

namespace mister::svc::disc {

namespace {

std::optional<proto::FileOffset> cue_byte_pos(
    const Track& t,
    Lba at,
    FileSize s_off = FileSize{}
) noexcept {
    return (proto::BlockSize{t.sector_size}.position_of(at) + s_off).seek_by(-t.offset);
}

}

Ex<void> FileTableBacking::do_read_payload(
    Lba lba,
    const Track& t,
    std::uint32_t s_off,
    std::span<std::byte> out
) {
    IFile* f = resident_(t.backing);
    if (f == nullptr)
        return std::unexpected(t.backing.refusal(Errc::not_found, ERR_SITE()));
    const std::optional<proto::FileOffset> pos = cue_byte_pos(t, lba, FileSize{s_off});
    if (!pos)
        return std::unexpected(lba.refusal(Errc::bad_format, ERR_SITE()));
    auto rd = f->read_at(*pos, out);
    if (!rd)
        return std::unexpected(rd.error());
    if (!rd->covers(out)) {
        return std::unexpected(proto::FileSize::of(*rd).refusal(Errc::short_read, ERR_SITE()));
    }
    return {};
}

void FileTableBacking::advise_first_window(const Track& t0) noexcept {
    if (IFile* const f = resident_(t0.backing)) {
        constexpr FileSize kFirstWindowBytes{1u * 1024u * 1024u};
        const proto::FileOffset pos = cue_byte_pos(t0, t0.start).value_or(proto::kFileStart);
        (void)f->advise(IFile::Access::WillNeed, pos, kFirstWindowBytes);
    }
}

void FileTableBacking::advise_ahead(Lba lba, const Track& t, proto::BlockCount sectors) noexcept {
    IFile* const f = resident_(t.backing);
    if (f == nullptr)
        return;
    const std::optional<proto::FileOffset> pos = cue_byte_pos(t, lba);
    if (!pos)
        return;
    const FileSize len = proto::BlockSize{t.sector_size}.bytes_of(sectors);

    (void)f->advise(IFile::Access::WillNeed, *pos, len);
}

}
