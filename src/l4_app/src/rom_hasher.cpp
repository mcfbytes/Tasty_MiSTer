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
    std::uint64_t off,
    std::uint64_t end,
    cores::RomDigest digest,
    std::optional<cores::RomDigest> alt
) noexcept {
    file_ = std::move(file);
    off_ = off;
    end_ = end;
    digest_ = std::move(digest);
    alt_ = std::move(alt);
}

RomHasher::Pace RomHasher::step() noexcept {
    std::array<std::uint8_t, ReplayPlay::kReadChunk> buf;
    std::uint32_t spent = 0;
    while (spent < ReplayPlay::kTickBudget && off_ < end_) {
        const std::size_t want = static_cast<std::size_t>(
            std::min<std::uint64_t>(buf.size(), end_ - off_)
        );
        const std::span<std::uint8_t> chunk(buf.data(), want);
        const auto got = file_->read_at(off_, std::as_writable_bytes(chunk));
        if (!got || *got == 0 || *got > want)
            return Pace::ReadFailed;
        digest_.update(chunk.first(*got));
        if (alt_)
            alt_->update(chunk.first(*got));
        off_ += *got;
        spent += static_cast<std::uint32_t>(*got);
    }
    if (off_ < end_)
        return Pace::Pending;
    file_.reset();
    return Pace::Done;
}

}
