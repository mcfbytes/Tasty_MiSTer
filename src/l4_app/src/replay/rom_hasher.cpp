// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/replay/rom_hasher.h"
#include "infra/span_cut.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <utility>

#include "app/replay/replay_play.h"
#include "svc/file.h"

namespace mister::app::replay {

void RomHasher::arm(
    std::unique_ptr<svc::IFile> file,
    cores::tas::IMovieFormat::DigestSpan span,
    cores::tas::RomDigest digest,
    std::vector<cores::tas::RomDigest> alts
) noexcept {
    file_ = std::move(file);
    read_at_ = span.offset;
    remaining_ = span.length;
    digest_ = std::move(digest);
    alts_ = std::move(alts);
}

RomHasher::Pace RomHasher::step() noexcept {
    std::array<std::uint8_t, ReplayPlay::kReadChunk> buf;
    proto::FileSize spent{};
    while (spent < proto::FileSize{ReplayPlay::kTickBudget} && !remaining_.empty()) {
        const std::span<std::uint8_t> whole{buf};
        const std::span<std::uint8_t> chunk = infra::affine::first(
            whole,
            remaining_.min(proto::FileSize::of(whole))
        );
        const auto got = file_->read_at(read_at_, std::as_writable_bytes(chunk));
        if (!got || got->empty() || *got > infra::ByteCount{chunk.size()})
            return Pace::ReadFailed;
        digest_.update(got->first_in(chunk));
        for (auto& a : alts_)
            a.update(got->first_in(chunk));
        const proto::FileSize took = proto::FileSize::of(*got);
        read_at_ += took;
        remaining_ -= took;
        spent += took;
    }
    if (!remaining_.empty())
        return Pace::Pending;
    file_.reset();
    return Pace::Done;
}

}
