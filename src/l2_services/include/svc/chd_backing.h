// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>

#include "infra/error.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/chd_meta_row.h"
#include "svc/chd_prefetch.h"
#include "svc/chd_source.h"
#include "svc/disc_backing.h"
#include "svc/disc_counters.h"
#include "svc/track.h"
#include "svc/vfs.h"

namespace mister::svc {

struct ChdHandles {
    std::unique_ptr<IChdSource> primary{};
    std::unique_ptr<IChdSource> prefetch{};
};

class ChdBacking final : public IDiscBacking {
    TASTY_SEAT_RESIDENT(Io);

public:
    explicit ChdBacking(ChdPrefetch& prefetch) noexcept : prefetch_(prefetch) {}

    [[nodiscard]] bool attached() const noexcept { return chd_ != nullptr; }

    [[nodiscard]] Ex<void> attach(ChdHandles handles);

    [[nodiscard]] std::optional<ChdMetaRow> track_metadata(TrackIndex index) {
        return chd_->track_metadata(index);
    }

    [[nodiscard]] Ex<std::span<std::byte>> read_subcode(
        proto::Lba lba,
        const Track& t,
        std::span<std::byte> dst
    );

    [[nodiscard]] bool prefetch_off() const noexcept { return !chd_ || geo_.hunk_bytes.empty(); }

    [[nodiscard]] bool prefetch_opened() const noexcept { return prefetch_.get().opened(); }

    void advise_first_window(IFile& primary) noexcept;

    void park() noexcept {
        if (!prefetch_.get().settle_park())
            ++park_timeouts_;
    }

    void release() noexcept;

    [[nodiscard]] std::uint32_t hunk_bytes() const noexcept { return geometry().hunk_detail(); }

    [[nodiscard]] ChdGeometry geometry() const noexcept {
        return chd_ != nullptr ? geo_ : ChdGeometry{};
    }

    [[nodiscard]] DiscCounters counters() const noexcept {
        return DiscCounters{
            .sync_decompress = sync_decompress_,
            .park_timeouts = park_timeouts_,
            .prefetch_refusals = prefetch_refusals_
        };
    }

    [[nodiscard]] std::uint32_t sync_decompress() const noexcept { return sync_decompress_; }

    [[nodiscard]] std::uint32_t park_timeouts() const noexcept { return park_timeouts_; }

    [[nodiscard]] std::uint32_t prefetch_refusals() const noexcept { return prefetch_refusals_; }

private:
    [[nodiscard]] Ex<void> do_read_payload(
        proto::Lba lba,
        const Track& t,
        std::uint32_t s_off,
        std::span<std::byte> out
    ) override;

    [[nodiscard]] Ex<void> load_hunk(HunkIndex hunk);

    std::unique_ptr<IChdSource> chd_;
    std::unique_ptr<std::byte[]> hunk_buf_;

    ChdGeometry geo_{};
    HunkIndex hunk_memo_{HunkIndex::none()};

    std::span<const std::byte> hunk_view_{};

    std::reference_wrapper<ChdPrefetch> prefetch_;

    HunkCount prefetch_depth_{};
    std::uint32_t sync_decompress_ = 0;

    std::uint32_t park_timeouts_ = 0;
    std::uint32_t prefetch_refusals_ = 0;
};

}
