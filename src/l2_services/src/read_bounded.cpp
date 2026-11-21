// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/read_bounded.h"

#include <algorithm>
#include <memory>
#include <span>

#include "infra/prelude.h"
#include "svc/file.h"
#include "infra/span_cut.h"
#include "svc/vfs.h"

namespace mister::svc {

Ex<std::vector<std::byte>> read_bounded(IFile& file, std::uint64_t cap) {
    constexpr std::uint64_t kChunk = 64u * 1024u;
    std::vector<std::byte> out;
    std::vector<std::byte> chunk(static_cast<std::size_t>(std::min(cap, kChunk)));
    while (out.size() < cap) {
        const std::size_t want = static_cast<std::size_t>(
            std::min<std::uint64_t>(chunk.size(), cap - out.size())
        );
        auto n = file.read_at(
            proto::kFileStart + proto::FileSize{out.size()},
            std::span<std::byte>(chunk.data(), want)
        );
        if (!n)
            return std::unexpected(n.error());
        if (n->empty())
            break;
        const std::span<const std::byte> got = n->first_in(std::span<const std::byte>(chunk));
        out.insert(out.end(), got.begin(), got.end());
    }
    return out;
}

Ex<std::vector<std::byte>> read_bounded(const Vfs& vfs, std::string_view path, std::uint64_t cap) {
    auto f = vfs.open(path, OpenMode::Read);
    if (!f)
        return std::unexpected(f.error());
    return read_bounded(**f, cap);
}

std::span<std::byte> read_bounded_partial(IFile& file, std::span<std::byte> dst) {
    proto::FileOffset at = proto::kFileStart;
    const proto::FileSize room = proto::FileSize::of(dst);
    while (room.holds(at)) {
        auto n = file.read_at(at, infra::affine::from(dst, at));
        if (!n || n->empty())
            break;
        at += proto::FileSize::of(*n);
    }
    return infra::affine::first(dst, at - proto::kFileStart);
}

}
