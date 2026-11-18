// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/stream_io.h"

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

namespace {

template <class Buf, class Io>
[[nodiscard]] Ex<void> pump(proto::FileOffset off, Buf buf, Errc stalled, Io io) {
    proto::FileOffset at = off;
    while (!buf.empty()) {
        auto n = io(at, buf);
        if (!n)
            return std::unexpected(n.error());
        if (*n == 0) {
            return std::unexpected(at.refusal(stalled, ERR_SITE()));
        }
        buf = buf.subspan(*n);
        at += proto::FileSize{*n};
    }
    return {};
}
}

[[nodiscard]] Ex<void> stream_read(svc::IFile& f, proto::FileOffset off, std::span<std::byte> dst) {
    return pump(off, dst, Errc::short_read, [&f](auto at, auto rest) {
        return f.read_at(at, rest);
    });
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
