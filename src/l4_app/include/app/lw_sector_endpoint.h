// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

#include "app/grant_endpoint.h"
#include "app/block_line.h"
#include "app/disc_frame_source.h"
#include "app/save_slot_blocks.h"
#include "app/sector_grant.h"
#include "app/sector_route.h"
#include "hal/fpga_memory.h"
#include "hal/reg_port.h"
#include "hal/sector_block.h"
#include "hal/window_port.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "os/epoll_set.h"
#include "infra/seat.h"
#include "infra/seat_owner_io.h"
#include "os/monotonic_clock.h"
#include "proto/types.h"
#include "svc/disc/disc_geometry.h"
#include "svc/disc/disc_read_service.h"
#include "svc/disc/disc_read_slot.h"
#include "svc/io_coworker.h"
#include "svc/save_files_on_io.h"

namespace mister::app {

class LwSectorEndpoint final : public svc::IIoCoworker,
                               public GrantEndpoint<SectorGrant, SectorRoute, seat_owner::Io> {
    TASTY_SEAT_RESIDENT(Io);
    static_assert(
        seat_owner::Io::kSeatDecl == kSeatDecl,
        "the base's owner tag and this endpoint's declaration must agree"
    );

public:
    using Port = hal::IRegPort<hal::SectorBlock>;

    static constexpr unsigned kMaxResends = 2;

    struct Wiring {
        const SectorGrantCell& grant;
        SectorRouteCell& route;
        SectorLeaseCell& lease;
        const svc::disc::DiscReadService::GeomCell& geometry;
        svc::disc::DiscReadService& discs;
        infra::OptRef<const os::EpollSet> doorbells{};
        infra::OptRef<Port> port{};

        infra::OptRef<const svc::ISaveFilesOnIo> saves{};
        std::span<std::byte> image_backing{};
    };

    explicit LwSectorEndpoint(const Wiring& w) noexcept;

    void serve() noexcept override;
    [[nodiscard]] bool idle() const noexcept override;
    void on_pause() noexcept override;
    void on_resume() noexcept override;
    void on_stop() noexcept override;

    [[nodiscard]] const SectorRoute& route() const noexcept { return tally_; }

private:
    struct Card {

        hal::SectorBlock::Slot index{};

        proto::SlotIndex slot{};
        proto::BlockSize bytes{};
        std::optional<hal::FpgaMemory> image{};

        [[nodiscard]] bool is_bound() const noexcept { return !bytes.empty(); }

        [[nodiscard]] proto::FileSize image_bytes_before(proto::Lba lba) const noexcept {
            return bytes.position_of(lba) - proto::kFileStart;
        }

        [[nodiscard]] proto::SlotIndex save_slot() const noexcept { return slot; }
    };

    enum class BlockDir : std::uint8_t { Read, Write };

    struct Request {
        infra::OptRef<const Card> card{};
        hal::SectorBlock::Slot slot{};
        BlockDir dir = BlockDir::Read;
        proto::Lba lba{};
    };

    using Base = GrantEndpoint<SectorGrant, SectorRoute, seat_owner::Io>;
    static_assert(
        std::is_same_v<SectorLeaseCell, Base::LeaseCell>,
        "the route's lease cell is the base's lease cell"
    );
    [[nodiscard]] bool accepts_hook_(const SectorGrant& g) const noexcept override;
    void publish_hook_(const SectorRoute& r) noexcept override;
    void prepare_hook_(const SectorGrant& g) noexcept override;
    [[nodiscard]] bool arm_hook_(const SectorGrant& g) noexcept override;
    void bind_hook_(const SectorGrant& g) noexcept override;
    void bound_hook_() noexcept override;
    void answer_hook_(proto::BuildGeneration gen) noexcept override;
    void unbind_hook_() noexcept override;
    void serve_bound_() noexcept override;
    [[nodiscard]] bool idle_bound_hook_() const noexcept override;

    void store_ctrl_(hal::RegWord v) noexcept;
    [[nodiscard]] Port& port_() noexcept;
    [[nodiscard]] std::optional<Request> read_request_() noexcept;
    [[nodiscard]] hal::SectorBlock::Enables enables_() const noexcept;
    void bind_cards_(const SectorGrant& g) noexcept;

    [[nodiscard]] bool store_frame_(
        hal::SectorBlock::Slot slot,
        proto::Lba lba,
        std::span<const std::byte> bytes
    ) noexcept;
    void peek_more_() noexcept;
    void serve_card_(
        const Card& c,
        hal::SectorBlock::Slot slot,
        BlockDir dir,
        proto::Lba lba,
        os::RawMonotonicClock::time_point t0
    ) noexcept;
    void fill_card_(const Card& c, proto::Lba lba) noexcept;
    [[nodiscard]] bool copy_image_(const Card& c, proto::Lba lba) noexcept;
    [[nodiscard]] bool store_wack_(const Card& c, proto::Lba lba) noexcept;
    void persist_card_(const Card& c, proto::Lba lba) noexcept;
    void note_card_latency_(os::RawMonotonicClock::time_point t0) noexcept;
    void read_ahead_one_() noexcept;
    static void write_lba_(Port& port, hal::SectorBlock::Reg reg, proto::Lba lba) noexcept;

    Wiring w_;
    DiscFrameSource source_;
    std::optional<hal::WindowPort<hal::SectorBlock, seat_owner::Io>> block_{};
    BlockLine<seat_owner::Io> line_{};
    bool recheck_ = false;
    std::array<Card, SectorGrantBody::kCards> cards_{};
    SaveSlotBlocks blocks_;
    std::array<std::byte, hal::SectorBlock::kMaxFrameWords * 4u> card_buf_{};
    os::MonotonicClock clock_{};
};

extern template class GrantEndpoint<SectorGrant, SectorRoute, seat_owner::Io>;

}

namespace mister::hal {

extern template class WindowPort<SectorBlock, seat_owner::Io>;
}

namespace mister::infra::xthread {
extern template class GrantLease<app::SectorGrant, seat_owner::Io>;
}
