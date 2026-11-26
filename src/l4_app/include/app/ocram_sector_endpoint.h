// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>

#include "app/block_line.h"
#include "app/disc_frame_source.h"
#include "app/grant_endpoint.h"
#include "app/mount_slot.h"
#include "app/ocram_event_relay.h"
#include "app/save_slot_blocks.h"
#include "app/sector_grant.h"
#include "app/sector_route.h"
#include "hal/ocram.h"
#include "infra/opt_ref.h"
#include "infra/park_timeout.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/seat_owner_io.h"
#include "infra/tally.h"
#include "os/epoll_set.h"
#include "proto/types.h"
#include "svc/disc/disc_read_service.h"
#include "svc/io_coworker.h"
#include "svc/save_files_on_io.h"

namespace mister::app {

class OcramSectorEndpoint final : public svc::IIoCoworker,
                                  public GrantEndpoint<SectorGrant, SectorRoute, seat_owner::Io> {
    TASTY_SEAT_RESIDENT(Io);
    static_assert(
        seat_owner::Io::kSeatDecl == kSeatDecl,
        "the base's owner tag and this endpoint's declaration must agree"
    );

public:
    static constexpr proto::BlockCount kQueueAhead{12};

    static constexpr infra::ParkTimeout kDrainPoll{1};

    struct Wiring {
        const SectorGrantCell& grant;
        SectorRouteCell& route;
        SectorLeaseCell& lease;
        const svc::disc::DiscReadService::GeomCell& geometry;
        svc::disc::DiscReadService& discs;
        infra::OptRef<const os::EpollSet> doorbells{};

        infra::OptRef<const svc::ISaveFilesOnIo> saves{};

        infra::OptRef<OcramEventRelay> events{};
    };

    explicit OcramSectorEndpoint(const Wiring& w) noexcept;

    void serve() noexcept override;
    [[nodiscard]] bool idle() const noexcept override;

    [[nodiscard]] infra::ParkTimeout poll_period() const noexcept override;
    void on_pause() noexcept override;
    void on_resume() noexcept override;
    void on_stop() noexcept override;

    [[nodiscard]] const SectorRoute& route() const noexcept { return tally_; }

private:
    using Base = GrantEndpoint<SectorGrant, SectorRoute, seat_owner::Io>;
    using Writer = hal::ocram::RingWriter<seat_owner::Io>;
    using Reader = hal::ocram::RingReader<seat_owner::Io>;
    static_assert(
        std::is_same_v<SectorLeaseCell, Base::LeaseCell>,
        "the route's lease cell is the base's lease cell"
    );

    enum class Phase : std::uint8_t { Asking, Draining };

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

    struct Asks {
        infra::Tally count{};
        std::optional<proto::Lba> latest{};
    };

    struct Drained {
        Asks sector{};
        infra::Tally voided{};
        std::optional<proto::Lba> write_lba{};
        hal::ocram::MailboxSlot write_k = hal::ocram::MailboxSlot::Mount0;
    };

    struct Mount {
        hal::ocram::MailboxSlot k = hal::ocram::MailboxSlot::Mount0;
        MountSlot grant{};
        Asks taken{};
        std::optional<proto::Lba> wanted{};

        [[nodiscard]] bool is_bound() const noexcept { return grant.granted(); }
    };

    void drop_ask_() noexcept;
    void store_ask_(hal::ocram::SectorAsk ask) noexcept;

    [[nodiscard]] hal::ocram::SectorAsk wanted_ask_() const noexcept;

    [[nodiscard]] Drained drain_evt_(bool rang) noexcept;
    void take_record_(const Reader::Record& rec, Drained& d) noexcept;

    void judge_(Drained& d, hal::ocram::SlotBinds binds) noexcept;

    [[nodiscard]] hal::ocram::SlotBinds binds_view_() noexcept;
    void persist_write_(const Drained& d) noexcept;
    [[nodiscard]] infra::OptRef<Mount> mount_of_(hal::ocram::MailboxSlot k) noexcept;

    enum class Sent : std::uint8_t { Stored, Full, Skipped, Gone };

    [[nodiscard]] Sent publish_frame_(proto::Lba lba) noexcept;

    [[nodiscard]] Sent publish_mount_(Mount& m) noexcept;

    [[nodiscard]] bool retake_() noexcept;

    [[nodiscard]] std::span<const std::uint32_t> pack_(
        proto::Lba lba,
        std::span<const std::byte> bytes,
        hal::ocram::Words words
    ) noexcept;
    [[nodiscard]] proto::BlockCount frames_waiting_() const noexcept;
    void lose_(infra::Tally& count) noexcept;

    Wiring w_;
    DiscFrameSource source_;
    SaveSlotBlocks blocks_;
    std::optional<Writer> sector_{};
    std::optional<Writer> mount_ring_{};
    std::optional<Reader> evt_{};
    BlockLine<seat_owner::Io> line_{};
    std::array<Mount, SectorGrantBody::kCards> mounts_{};
    hal::ocram::Words frame_{};
    hal::ocram::Words ring_words_{};
    Phase phase_ = Phase::Draining;
    bool recheck_ = false;
    bool bind_records_ = false;
    bool events_ = false;
    bool seed_ = false;
    hal::ocram::SectorAsk ask_{};
    hal::ocram::SlotBinds seen_{};
    hal::ocram::SlotBinds risen_{};
    proto::BlockCount ahead_left_{};
    std::optional<proto::Lba> wanted_{};
    std::optional<proto::Lba> next_{};
    std::array<std::uint32_t, 1u + hal::ocram::SectorShape::kMaxFrame.size()> payload_{};
    std::array<std::byte, hal::ocram::SectorShape::kMaxFrame.bytes().size()> block_{};
};

}
