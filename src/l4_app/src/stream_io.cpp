// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/stream_io.h"

#include "infra/prelude.h"

namespace mister::app {

[[nodiscard]] Ex<StreamFile> stream_open(
    const svc::Vfs& vfs,
    std::string_view rel,
    svc::OpenMode mode
) {
    auto f = vfs.open(rel, mode);
    if (!f)
        return std::unexpected(f.error());
    auto sz = (*f)->size();
    if (!sz)
        return std::unexpected(sz.error());
    return StreamFile{std::move(*f), *sz};
}

[[nodiscard]] Ex<void> stream_read(svc::IFile& f, proto::FileOffset off, std::span<std::byte> dst) {
    return f.read_exact_at(off, dst);
}

Ex<std::vector<std::uint8_t>> read_whole(svc::IFile& f, proto::FileSize size) {
    std::vector<std::uint8_t> buf = size.zeroed<std::vector<std::uint8_t>>();
    if (auto r = stream_read(
            f,
            proto::kFileStart,
            std::as_writable_bytes(std::span<std::uint8_t>(buf))
        );
        !r) {
        return std::unexpected(r.error());
    }
    return buf;
}

Ex<std::vector<std::uint8_t>> read_file(
    const svc::Vfs& vfs,
    std::string_view rel,
    std::optional<proto::FileSize> cap
) {
    auto f = vfs.open(rel, svc::OpenMode::ReadWhole);
    if (!f)
        return std::unexpected(f.error());
    auto size = (*f)->size();
    if (!size)
        return std::unexpected(size.error());

    if (cap && *size > *cap)
        return std::unexpected(Error{Errc::short_read, ERR_SITE(), 0});
    return read_whole(**f, *size);
}

}
