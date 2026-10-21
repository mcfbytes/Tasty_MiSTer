// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/n64dd_expander.h"

#include <algorithm>
#include <cstring>

namespace mister::cores::n64dd {

void NddExpander::start(const DiskMap& map) noexcept {
    map_ = infra::OptRef<const DiskMap>{map};
    step_ = 0;
    have_ = 0;
}

bool NddExpander::complete() const noexcept {
    return map_.has_value() && step_ == map_->steps().size() && have_ == 0;
}

Ex<void> NddExpander::feed(std::span<const std::uint8_t> piece, ICoreWindow& window) {
    if (!map_)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    const auto steps = map_->steps();
    std::size_t at = 0;
    while (step_ < steps.size()) {
        const DiskMap::Step& s = steps[step_];
        if (s.good) {
            const std::uint32_t need = std::uint32_t{s.zone_bytes} * kSectorsPerBlock;
            const auto take = static_cast<std::uint32_t>(
                std::min<std::size_t>(need - have_, piece.size() - at)
            );
            std::memcpy(block_.data() + have_, piece.data() + at, take);
            have_ += take;
            at += take;
            if (have_ < need)
                break;
        }
        if (auto r = place_(s, window); !r)
            return r;
        have_ = 0;
        ++step_;
    }
    if (at != piece.size()) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(at)});
    }
    return {};
}

Ex<void> NddExpander::place_(const DiskMap::Step& s, ICoreWindow& window) {
    const auto mark = [this] {
        for (std::uint32_t sec = 0; sec < kSectorsPerBlock; ++sec) {
            std::memset(
                image_.data() + sec * kSectorStride + kBadMarkOffset,
                kBadBlockFill,
                kBadMarkBytes
            );
        }
    };
    const auto put = [&](std::uint32_t block) -> Ex<void> {
        const std::uint32_t off = flat_offset(s.track, s.head, block, 0);
        auto w = window.write(off, std::as_bytes(std::span<const std::uint8_t>(image_)));
        if (!w)
            return std::unexpected(w.error());
        return {};
    };
    image_.fill(0);
    if (!s.good) {
        mark();
        if (auto r = put(0); !r)
            return r;
        return put(1);
    }
    for (std::uint32_t sec = 0; sec < kSectorsPerBlock; ++sec) {
        std::memcpy(
            image_.data() + sec * kSectorStride,
            block_.data() + sec * s.copy_bytes,
            s.copy_bytes
        );
    }
    if (s.bad_lba)
        mark();
    return put(s.flat_block);
}

}
