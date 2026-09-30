// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/read_bounded.h"

#include <algorithm>
#include <memory>
#include <span>

#include "svc/file.h"
#include "svc/vfs.h"

namespace mister::svc {

Ex<std::vector<std::byte>> read_bounded(IFile& file, std::uint64_t cap) {
    constexpr std::uint64_t kChunk = 64u * 1024u;
    std::vector<std::byte> out;
    std::vector<std::byte> chunk(static_cast<std::size_t>(std::min(cap, kChunk)));
    while (out.size() < cap) {
        const std::size_t want =
            static_cast<std::size_t>(std::min<std::uint64_t>(chunk.size(), cap - out.size()));
        auto n = file.read_at(out.size(), std::span<std::byte>(chunk.data(), want));
        if (!n) return std::unexpected(n.error());
        if (*n == 0) break;
        out.insert(out.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(*n));
    }
    return out;
}

Ex<std::vector<std::byte>> read_bounded(const Vfs& vfs, std::string_view path, std::uint64_t cap) {
    auto f = vfs.open(path, OpenMode::Read);
    if (!f) return std::unexpected(f.error());
    return read_bounded(**f, cap);
}

}  // namespace mister::svc
