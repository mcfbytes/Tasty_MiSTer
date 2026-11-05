// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/stream_io.h"

namespace mister::app {

[[nodiscard]] Ex<StreamFile> stream_open(
    const svc::Vfs& vfs,
    std::string_view rel,
    svc::OpenMode mode
) {
    if (!svc::opens_read_only(mode) && mode != svc::OpenMode::ReadWrite) {

        if (const std::size_t slash = rel.rfind('/');
            slash != std::string_view::npos && slash > 0) {
            if (auto r = vfs.ensure_dir(rel.substr(0, slash)); !r)
                return std::unexpected(r.error());
        }
    }
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
[[nodiscard]] Ex<void> pump(proto::FileSize off, Buf buf, Errc stalled, Io io) {
    proto::FileSize at = off;
    while (!buf.empty()) {
        auto n = io(at.v, buf);
        if (!n)
            return std::unexpected(n.error());
        if (*n == 0) {
            return std::unexpected(Error{stalled, ERR_SITE(), static_cast<std::uint32_t>(at.v)});
        }
        buf = buf.subspan(*n);
        at += proto::FileSize{*n};
    }
    return {};
}
}

[[nodiscard]] Ex<void> stream_read(svc::IFile& f, proto::FileSize off, std::span<std::byte> dst) {
    return pump(off, dst, Errc::short_read, [&f](auto at, auto rest) {
        return f.read_at(at, rest);
    });
}

[[nodiscard]] Ex<void> stream_write(
    svc::IFile& f,
    proto::FileSize off,
    std::span<const std::byte> src
) {
    return pump(off, src, Errc::short_write, [&f](auto at, auto rest) {
        return f.write_at(at, rest);
    });
}

[[nodiscard]] Ex<void> stream_sync(svc::IFile& f) {
    return f.flush();
}

std::optional<std::vector<std::uint8_t>> read_whole(svc::IFile& f, proto::FileSize size) {
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(size.v));
    std::size_t off = 0;
    while (off < buf.size()) {
        auto n = f.read_at(off, std::as_writable_bytes(std::span<std::uint8_t>(buf).subspan(off)));
        if (!n || *n == 0)
            return std::nullopt;
        off += *n;
    }
    return buf;
}

}
