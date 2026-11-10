// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "infra/telemetry.h"
#include "proto/types.h"
#include "svc/disc_counters.h"
#include "svc/disc_engine.h"
#include "svc/image_opener.h"
#include "svc/disc_geometry.h"
#include "svc/disc_identity_role.h"
#include "svc/disc_mounter.h"
#include "svc/disc_reader.h"
#include "svc/sector_header.h"
#include "svc/vfs.h"
#include "svc/vfs_image_opener.h"

namespace mister::svc {

class ChdPrefetch;

class DiscStore final : public IDiscReader, public IDiscMounter {
    TASTY_SEAT_RESIDENT(Io);

public:
    using GeomCell = xthread::Telemetry<DiscGeometry, SeatTag::Io>;
    using CountCell = xthread::Telemetry<DiscCounters, SeatTag::Io>;

    DiscStore(
        const IImageOpener& opener,
        GeomCell& geom,
        CountCell& counters,
        ChdPrefetch& prefetch
    ) noexcept
        : opener_(opener),
          geom_(geom),
          counters_(counters),
          prefetch_(prefetch) {}

    DiscStore(const Vfs& vfs, GeomCell& geom, CountCell& counters, ChdPrefetch& prefetch)
        : owned_opener_(std::make_unique<VfsImageOpener>(vfs)),
          opener_(*owned_opener_),
          geom_(geom),
          counters_(counters),
          prefetch_(prefetch) {}

    [[nodiscard]] Ex<std::span<std::byte>> read_form(
        DiscForm form,
        proto::Lba lba,
        std::span<std::byte> dst,
        SectorHeader header = {}
    ) override;

    [[nodiscard]] bool mounted_for_test() const noexcept;

private:
    [[nodiscard]] bool do_mount(
        infra::OptRef<const CuePolicy> policy,
        std::string_view path,
        infra::OptRef<const IDiscIdentity> identity
    ) noexcept override;
    [[nodiscard]] bool do_unmount(infra::OptRef<const CuePolicy> policy) noexcept override;
    void do_release() noexcept override;

    [[nodiscard]] bool ensure_engine_(infra::OptRef<const CuePolicy> policy) noexcept;

    [[nodiscard]] Ex<std::span<std::byte>> read_engine_(
        DiscForm form,
        proto::Lba lba,
        std::span<std::byte> dst,
        SectorHeader header
    );

    void publish_(infra::OptRef<const IDiscIdentity> identity = {}) noexcept;

    std::unique_ptr<IImageOpener> owned_opener_;
    const IImageOpener& opener_;
    GeomCell& geom_;
    CountCell& counters_;
    ChdPrefetch& prefetch_;

    infra::OptRef<const CuePolicy> policy_;

    proto::Lba next_lba_{};
    std::optional<DiscEngine> engine_;
};

}
