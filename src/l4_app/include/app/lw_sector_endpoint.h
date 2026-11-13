// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

#include "app/lw_block_endpoint.h"
#include "app/lw_line.h"
#include "app/sector_grant.h"
#include "app/sector_route.h"
#include "hal/fpga_memory.h"
#include "hal/reg_port.h"
#include "hal/sector_block.h"
#include "hal/window_port.h"
#include "infra/indexed_array.h"
#include "infra/opt_ref.h"
#include "os/epoll_set.h"
#include "infra/seat.h"
#include "infra/seat_owner_io.h"
#include "os/monotonic_clock.h"
#include "proto/save_slot_rule.h"
#include "svc/disc_geometry.h"
#include "svc/disc_read_service.h"
#include "svc/disc_read_slot.h"
#include "svc/io_coworker.h"
#include "svc/save_files_on_io.h"
#include "svc/types.h"

namespace mister::app {

class LwSectorEndpoint final : public svc::IIoCoworker,
                               public LwBlockEndpoint<SectorGrant, SectorRoute, seat_owner::Io> {
    TASTY_SEAT_RESIDENT(Io);
    static_assert(
        seat_owner::Io::kSeatDecl == kSeatDecl,
        "the base's owner tag and this endpoint's declaration must agree"
    );

public:
    using Port = hal::IRegPort<hal::SectorBlock>;

    static constexpr unsigned kMaxResends = 2;

    static constexpr std::size_t kReadAhead = 6;

    struct Wiring {
        const SectorGrantCell& grant;
        SectorRouteCell& route;
        SectorLeaseCell& lease;
        const svc::DiscReadService::GeomCell& geometry;
        svc::DiscReadService& discs;
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
    struct Ahead {
        svc::DiscReadSlot slot{};
        bool live = false;
    };

    struct Card {

        std::uint8_t slot = 0;
        proto::BlockSize bytes{};
        std::optional<hal::FpgaMemory> image{};

        [[nodiscard]] bool is_bound() const noexcept { return !bytes.empty(); }

        [[nodiscard]] std::uint64_t offset_of(proto::Lba lba) const noexcept {
            return bytes.position_of(lba);
        }

        [[nodiscard]] proto::SlotIndex save_slot() const noexcept { return proto::SlotIndex{slot}; }
    };

    struct SaveState {
        std::optional<svc::Generation> gen{};
        proto::SaveSlotRule rule{};
    };

    enum class BlockDir : std::uint8_t { Read, Write };

    struct Request {
        infra::OptRef<const Card> card{};
        BlockDir dir = BlockDir::Read;
        proto::Lba lba{};
    };

    using Base = LwBlockEndpoint<SectorGrant, SectorRoute, seat_owner::Io>;
    static_assert(
        std::is_same_v<SectorLeaseCell, Base::LeaseCell>,
        "the route's lease cell is the base's lease cell"
    );
    void publish_hook_(const SectorRoute& r) noexcept override;
    void prepare_hook_(const SectorGrant& g) noexcept override;
    [[nodiscard]] bool arm_hook_(const SectorGrant& g) noexcept override;
    void bind_hook_(const SectorGrant& g) noexcept override;
    void bound_hook_() noexcept override;
    void answer_hook_(proto::BuildGeneration gen) noexcept override;
    void unbind_hook_() noexcept override;
    void serve_bound_() noexcept override;
    [[nodiscard]] bool idle_bound_hook_() const noexcept override;

    void take_geometry_() noexcept;
    void store_ctrl_(std::uint32_t v) noexcept;
    void read_counts_() noexcept;
    [[nodiscard]] Port& port_() noexcept;
    [[nodiscard]] bool cause_names_route_(std::uint32_t w) const noexcept;
    [[nodiscard]] std::optional<Request> read_request_() noexcept;
    [[nodiscard]] std::uint32_t enables_() const noexcept;
    void bind_cards_(const SectorGrant& g) noexcept;
    void fill_frame_(proto::Lba lba) noexcept;

    [[nodiscard]] bool store_frame_(
        hal::SectorBlock::Reg begin,
        proto::Lba lba,
        std::span<const std::byte> bytes
    ) noexcept;
    void peek_more_() noexcept;
    void serve_card_(
        const Card& c,
        BlockDir dir,
        proto::Lba lba,
        os::RawMonotonicClock::time_point t0
    ) noexcept;
    [[nodiscard]] svc::ISaveFilesOnIo::Save save_of_(const Card& c) noexcept;
    void fill_card_(const Card& c, proto::Lba lba) noexcept;
    [[nodiscard]] bool copy_image_(const Card& c, proto::Lba lba) noexcept;
    [[nodiscard]] bool store_wack_(const Card& c, proto::Lba lba) noexcept;
    void persist_card_(const Card& c, proto::Lba lba) noexcept;
    void note_card_latency_(os::RawMonotonicClock::time_point t0) noexcept;
    void read_ahead_one_() noexcept;
    [[nodiscard]] bool take_ahead_(proto::Lba lba) noexcept;
    void read_engine_(svc::DiscReadSlot& s, proto::Lba lba) noexcept;
    [[nodiscard]] std::uint32_t frame_bytes_() const noexcept;
    [[nodiscard]] svc::ISectorProgram::Source answer_(proto::Lba lba) const noexcept;
    static void write_lba_(Port& port, hal::SectorBlock::Reg reg, proto::Lba lba) noexcept;

    Wiring w_;
    svc::DiscReadService::GeomCell::Reader geom_rd_{};
    std::optional<svc::DiscGeometry> disc_{std::in_place};
    std::optional<hal::WindowPort<hal::SectorBlock, seat_owner::Io>> block_{};
    LwLine line_{};
    bool recheck_ = false;
    proto::Lba ahead_from_{};
    std::size_t ahead_done_ = kReadAhead;
    std::size_t ahead_next_ = 0;
    std::array<Ahead, kReadAhead> ahead_{};
    svc::DiscReadSlot frame_{};
    std::array<Card, SectorGrant::kCards> cards_{};

    infra::IndexedArray<SaveState, hal::SectorBlock::kCardSlots, proto::SlotIndex> saves_{};
    std::array<std::byte, hal::SectorBlock::kMaxFrameWords * 4u> card_buf_{};
    os::MonotonicClock clock_{};
};

extern template class LwBlockEndpoint<SectorGrant, SectorRoute, seat_owner::Io>;

}

namespace mister::hal {

extern template class WindowPort<SectorBlock, seat_owner::Io>;
}

namespace mister::xthread {
extern template class GrantLease<app::SectorGrant, seat_owner::Io>;
}
