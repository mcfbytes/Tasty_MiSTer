// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/stream_io.h"

namespace mister::app {

[[nodiscard]] Ex<StreamFile> stream_open(const svc::Vfs& vfs, std::string_view rel,
                                         svc::OpenMode mode) {
    if (!svc::opens_read_only(mode) && mode != svc::OpenMode::ReadWrite) {

        if (const std::size_t slash = rel.rfind('/');
            slash != std::string_view::npos && slash > 0) {
            if (auto r = vfs.ensure_dir(rel.substr(0, slash)); !r)
                return std::unexpected(r.error());
        }
    }
    auto f = vfs.open(rel, mode);
    if (!f) return std::unexpected(f.error());
    auto sz = (*f)->size();
    if (!sz) return std::unexpected(sz.error());
    return StreamFile{std::move(*f), sz->v};
}

[[nodiscard]] Ex<void> stream_read(svc::IFile& f, std::uint64_t off, std::span<std::byte> dst) {
    std::size_t done = 0;
    while (done < dst.size()) {
        auto n = f.read_at(off + done, dst.subspan(done));
        if (!n) return std::unexpected(n.error());
        if (*n == 0) {
            return std::unexpected(
                Error{Errc::short_read, ERR_SITE(), static_cast<std::uint32_t>(off + done)});
        }
        done += *n;
    }
    return {};
}

[[nodiscard]] Ex<void> stream_write(svc::IFile& f, std::uint64_t off,
                                    std::span<const std::byte> src) {
    std::size_t done = 0;
    while (done < src.size()) {
        auto n = f.write_at(off + done, src.subspan(done));
        if (!n) return std::unexpected(n.error());
        if (*n == 0) {
            return std::unexpected(
                Error{Errc::short_write, ERR_SITE(), static_cast<std::uint32_t>(off + done)});
        }
        done += *n;
    }
    return {};
}

[[nodiscard]] Ex<void> stream_sync(svc::IFile& f) { return f.flush(); }

}  // namespace mister::app
