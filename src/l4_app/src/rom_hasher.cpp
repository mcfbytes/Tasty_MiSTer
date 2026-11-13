// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/rom_hasher.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <utility>

#include "app/replay_play.h"
#include "svc/file.h"

namespace mister::app {

void RomHasher::arm(
    std::unique_ptr<svc::IFile> file,
    cores::IMovieFormat::DigestSpan span,
    cores::RomDigest digest,
    std::vector<cores::RomDigest> alts
) noexcept {
    file_ = std::move(file);
    read_at_ = proto::FileSize{span.offset};
    remaining_ = proto::FileSize{span.length};
    digest_ = std::move(digest);
    alts_ = std::move(alts);
}

RomHasher::Pace RomHasher::step() noexcept {
    std::array<std::uint8_t, ReplayPlay::kReadChunk> buf;
    std::uint32_t spent = 0;
    while (spent < ReplayPlay::kTickBudget && !remaining_.empty()) {

        const std::size_t remaining_bytes = remaining_.file_word<std::size_t>();
        const std::size_t want = std::min(buf.size(), remaining_bytes);
        const std::span<std::uint8_t> chunk(buf.data(), want);
        const auto got = file_->read_at(
            read_at_.file_word<std::size_t>(),
            std::as_writable_bytes(chunk)
        );
        if (!got || *got == 0 || *got > want)
            return Pace::ReadFailed;
        digest_.update(chunk.first(*got));
        for (auto& a : alts_)
            a.update(chunk.first(*got));
        read_at_ += proto::FileSize{*got};
        remaining_ -= proto::FileSize{*got};
        spent += static_cast<std::uint32_t>(*got);
    }
    if (!remaining_.empty())
        return Pace::Pending;
    file_.reset();
    return Pace::Done;
}

}
