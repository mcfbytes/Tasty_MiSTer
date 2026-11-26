// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "app/sector_grant.h"
#include "hal/ocram.h"
#include "infra/byte_count.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/disc/disc_geometry.h"
#include "svc/disc/disc_read_service.h"
#include "svc/disc/disc_read_slot.h"
#include "svc/disc/sector_program.h"

namespace mister::app {

class DiscFrameSource {
    TASTY_SEAT_RESIDENT(Io);

public:
    static constexpr std::size_t kReadAhead = 6;

    struct Wiring {
        const svc::disc::DiscReadService::GeomCell& geometry;
        svc::disc::DiscReadService& discs;
    };

    explicit DiscFrameSource(const Wiring& w) noexcept : w_(w) {}

    DiscFrameSource(const DiscFrameSource&) = delete;
    DiscFrameSource& operator=(const DiscFrameSource&) = delete;

    void bind(const SectorGrantBody& body) noexcept;
    void unbind() noexcept;

    [[nodiscard]] bool mount_changed() noexcept;

    [[nodiscard]] std::span<const std::byte> frame_for(proto::Lba lba) noexcept;

    void read_ahead_from(proto::Lba served) noexcept;
    [[nodiscard]] bool read_ahead_one() noexcept;

    [[nodiscard]] bool ahead_idle() const noexcept { return ahead_done_ >= kReadAhead; }

private:
    struct Ahead {
        svc::disc::DiscReadSlot slot{};
        bool live = false;
    };

    void drop_ahead_() noexcept;
    void fill_frame_(proto::Lba lba) noexcept;
    void read_engine_(svc::disc::DiscReadSlot& s, proto::Lba lba) noexcept;
    [[nodiscard]] bool take_ahead_(proto::Lba lba) noexcept;

    [[nodiscard]] std::span<std::byte> frame_view_() noexcept;
    [[nodiscard]] svc::disc::ISectorProgram::Source answer_(proto::Lba lba) const noexcept;

    Wiring w_;
    infra::OptRef<const svc::disc::ISectorProgram> program_{};
    hal::ocram::Words frame_words_{};
    svc::disc::DiscReadService::GeomCell::Reader geom_rd_{};
    std::optional<svc::disc::DiscGeometry> disc_{std::in_place};
    proto::Lba ahead_from_{};
    std::size_t ahead_done_ = kReadAhead;
    std::size_t ahead_next_ = 0;
    std::array<Ahead, kReadAhead> ahead_{};
    svc::disc::DiscReadSlot frame_{};
};

}
