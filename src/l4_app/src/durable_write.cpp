// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/durable_write.h"

#include <memory>
#include <string>

#include "svc/vfs.h"

namespace mister::app {

Ex<void> durable_write(const svc::Vfs& vfs, std::string_view rel,
                       std::span<const std::byte> bytes) {
    if (rel.empty()) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }

    if (const std::size_t slash = rel.rfind('/'); slash != std::string_view::npos && slash > 0) {
        if (auto r = vfs.ensure_dir(rel.substr(0, slash)); !r) return r;
    }

    const std::string tmp = std::string(rel) + ".tmp";
    auto f = vfs.open(tmp, svc::OpenMode::Truncate);
    if (!f) return std::unexpected(f.error());

    std::size_t off = 0;
    while (off < bytes.size()) {
        auto n = (*f)->write_at(off, bytes.subspan(off));
        if (!n) return std::unexpected(n.error());
        if (*n == 0) {
            return std::unexpected(
                Error{Errc::short_write, ERR_SITE(), static_cast<std::uint32_t>(off)});
        }
        off += *n;
    }

    if (auto r = (*f)->flush(); !r) return r;

    f->reset();

    return vfs.replace(tmp, rel);
}

}  // namespace mister::app
