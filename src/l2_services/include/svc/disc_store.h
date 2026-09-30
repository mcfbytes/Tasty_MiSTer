// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "infra/error.h"
#include "infra/seat.h"
#include "infra/telemetry.h"
#include "proto/types.h"
#include "svc/disc_counters.h"
#include "svc/disc_engine.h"
#include "svc/image_opener.h"
#include "svc/disc_geometry.h"
#include "svc/disc_mounter.h"
#include "svc/disc_reader.h"

namespace mister::svc {

class ChdPrefetch;

class DiscStore final : public IDiscReader, public IDiscMounter {
    TASTY_SEAT_RESIDENT(Io);

public:
    using GeomCell = xthread::Telemetry<DiscGeometry, SeatTag::Io>;
    using CountCell = xthread::Telemetry<DiscCounters, SeatTag::Io>;

    DiscStore(const Vfs& vfs, GeomCell& geom, CountCell& counters) noexcept
        : vfs_(&vfs), geom_(&geom), counters_(&counters) {}

    DiscStore(const IImageOpener& opener, GeomCell& geom, CountCell& counters) noexcept
        : opener_(&opener), geom_(&geom), counters_(&counters) {}

    void bind_prefetch(ChdPrefetch* p) noexcept { prefetch_ = p; }

    [[nodiscard]] Ex<std::size_t> read_form(DiscForm form, proto::Lba lba,
                                            std::span<std::byte> dst) override;
    [[nodiscard]] bool mount(const CuePolicy* policy, std::string_view path) noexcept override;
    [[nodiscard]] bool unmount(const CuePolicy* policy) noexcept override;
    void release() noexcept override;

    [[nodiscard]] bool mounted_for_test() const noexcept;

private:
    [[nodiscard]] bool ensure_engine_(const CuePolicy* policy) noexcept;
    void publish_() noexcept;

    const Vfs* vfs_ = nullptr;
    const IImageOpener* opener_ = nullptr;
    GeomCell* geom_;
    CountCell* counters_;
    ChdPrefetch* prefetch_ = nullptr;
    const CuePolicy* policy_ = nullptr;

    std::uint32_t next_lba_ = 0;
    std::optional<DiscEngine> engine_;
};

}  // namespace mister::svc
