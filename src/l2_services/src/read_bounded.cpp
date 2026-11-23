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

Ex<std::vector<std::byte>> read_bounded(IFile& file, proto::FileSize cap) {
    constexpr proto::FileSize kChunk{64u * 1024u};
    std::vector<std::byte> out;
    std::vector<std::byte> chunk = kChunk.min(cap).zeroed<std::vector<std::byte>>();
    const proto::FileSize chunk_size = proto::FileSize::of(std::span<std::byte>(chunk));
    proto::FileSize at{};
    while (cap.holds(proto::kFileStart + at)) {
        const proto::FileSize want = chunk_size.min(cap - at);
        const std::span<std::byte> asked = infra::affine::first(std::span<std::byte>(chunk), want);
        auto n = file.read_at(proto::kFileStart + at, asked);
        if (!n)
            return std::unexpected(n.error());
        if (n->empty())
            break;
        const std::span<const std::byte> got = infra::affine::first_clamped(
            std::span<const std::byte>(asked),
            proto::FileSize::of(*n)
        );
        out.insert(out.end(), got.begin(), got.end());
        at += proto::FileSize::of(got);
    }
    return out;
}

Ex<std::vector<std::byte>> read_bounded(IFile& file, std::uint64_t cap) {
    return read_bounded(file, proto::FileSize{cap});
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
