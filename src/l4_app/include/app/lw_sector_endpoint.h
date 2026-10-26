// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

#include "app/sector_grant.h"
#include "app/sector_route.h"
#include "hal/fpga_memory.h"
#include "hal/reg_block.h"
#include "hal/register_window.h"
#include "hal/sector_block.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "os/monotonic_clock.h"
#include "os/uio_handle.h"
#include "proto/save_slot_rule.h"
#include "svc/disc_geometry.h"
#include "svc/disc_read_service.h"
#include "svc/disc_read_slot.h"
#include "svc/io_coworker.h"
#include "svc/save_files_on_io.h"

namespace mister::app {

class LwSectorEndpoint final : public svc::IIoCoworker {
    TASTY_SEAT_RESIDENT(Io);

public:
    class Port {
    public:
        [[nodiscard]] virtual std::uint32_t read(hal::SectorBlock::Reg r) noexcept = 0;
        virtual void write(hal::SectorBlock::Reg r, std::uint32_t v) noexcept = 0;

    protected:
        Port() = default;
        Port(const Port&) = default;
        Port& operator=(const Port&) = default;
        ~Port() = default;
    };

    static constexpr unsigned kMaxResends = 2;

    static constexpr std::size_t kReadAhead = 6;

    struct Wiring {
        const SectorGrantCell& grant;
        SectorRouteCell& route;
        const svc::DiscReadService::GeomCell& geometry;
        svc::DiscReadService& discs;
        int doorbells = -1;
        infra::OptRef<Port> port{};

        infra::OptRef<const svc::ISaveFilesOnIo> saves{};
        std::span<std::byte> image_backing{};
    };

    explicit LwSectorEndpoint(const Wiring& w) noexcept;
    ~LwSectorEndpoint() override;

    void serve() noexcept override;
    [[nodiscard]] bool idle() const noexcept override;
    void on_pause() noexcept override;
    void on_resume() noexcept override;
    void on_stop() noexcept override;

    [[nodiscard]] const SectorRoute& route() const noexcept { return tally_; }

    [[nodiscard]] bool bound() const noexcept { return bound_gen_ != 0; }

private:
    class BlockPort final : public Port {
        TASTY_SEAT_RESIDENT(Io);

    public:
        explicit BlockPort(hal::reg::Block lease) noexcept
            : regs_(hal::RegisterWindow<hal::SectorBlock>::borrow(std::move(lease))) {}

        [[nodiscard]] std::uint32_t read(hal::SectorBlock::Reg r) noexcept override {
            TASTY_SEAT_BODY(BlockPort);
            return regs_.read(r);
        }

        void write(hal::SectorBlock::Reg r, std::uint32_t v) noexcept override {
            TASTY_SEAT_BODY(BlockPort);
            regs_.write(r, v);
        }

    private:
        hal::RegisterWindow<hal::SectorBlock> regs_;
    };

    struct Ahead {
        svc::DiscReadSlot slot{};
        bool live = false;
    };

    struct Card {
        std::uint8_t slot = 0;
        std::uint32_t bytes = 0;
        std::optional<hal::FpgaMemory> image{};
    };

    struct SaveState {
        std::optional<std::uint32_t> gen{};
        proto::SaveSlotRule rule{};
    };

    struct Request {
        bool pending = false;
        std::optional<std::size_t> card{};
        bool write = false;
        std::uint32_t lba = 0;
    };

    void take_grant_() noexcept;
    void take_geometry_() noexcept;
    void bind_(const SectorGrant& g, std::uint32_t gen) noexcept;
    void unbind_() noexcept;
    void answer_bind_(std::uint16_t gen) noexcept;
    [[nodiscard]] bool arm_line_(const SectorGrant& g) noexcept;
    void store_ctrl_(std::uint32_t v) noexcept;
    void read_counts_() noexcept;
    [[nodiscard]] bool lease_() noexcept;
    void release_() noexcept;
    void publish_() noexcept;
    [[nodiscard]] Port& port_() noexcept;
    [[nodiscard]] bool line_rang_() const noexcept;
    [[nodiscard]] std::uint32_t drain_cause_(bool rang) noexcept;
    [[nodiscard]] bool cause_names_route_(std::uint32_t w) const noexcept;
    [[nodiscard]] Request read_request_() noexcept;
    [[nodiscard]] std::uint32_t enables_() const noexcept;
    void bind_cards_(const SectorGrant& g) noexcept;
    void service_request_() noexcept;
    void fill_frame_(std::uint32_t lba) noexcept;

    [[nodiscard]] bool store_frame_(
        hal::SectorBlock::Reg begin,
        std::uint32_t lba,
        std::span<const std::byte> bytes
    ) noexcept;
    void peek_more_() noexcept;
    void serve_card_(
        const Card& c,
        bool write,
        std::uint32_t lba,
        std::chrono::nanoseconds t0
    ) noexcept;
    [[nodiscard]] svc::ISaveFilesOnIo::Save save_of_(const Card& c) noexcept;
    void fill_card_(const Card& c, std::uint32_t lba) noexcept;
    [[nodiscard]] bool copy_image_(const Card& c, std::uint32_t lba) noexcept;
    [[nodiscard]] bool store_wack_(const Card& c, std::uint32_t lba) noexcept;
    void persist_card_(const Card& c, std::uint32_t lba) noexcept;
    void note_card_latency_(std::chrono::nanoseconds t0) noexcept;
    void read_ahead_one_() noexcept;
    [[nodiscard]] bool take_ahead_(std::uint32_t lba) noexcept;
    void read_engine_(svc::DiscReadSlot& s, std::uint32_t lba) noexcept;
    [[nodiscard]] std::uint32_t frame_bytes_() const noexcept;

    Wiring w_;
    SectorGrantCell::Reader grant_rd_{};
    svc::DiscReadService::GeomCell::Reader geom_rd_{};
    std::optional<svc::DiscGeometry> disc_{std::in_place};
    SectorGrant grant_{};
    std::uint32_t bound_gen_ = 0;
    std::optional<BlockPort> block_{};
    os::UioHandle line_{};
    bool recheck_ = false;
    std::uint32_t ahead_from_ = 0;
    std::size_t ahead_done_ = kReadAhead;
    std::size_t ahead_next_ = 0;
    std::array<Ahead, kReadAhead> ahead_{};
    svc::DiscReadSlot frame_{};
    std::array<Card, SectorGrant::kCards> cards_{};
    std::array<SaveState, 4> saves_{};
    std::array<std::byte, hal::SectorBlock::kMaxFrameWords * 4u> card_buf_{};
    os::MonotonicClock clock_{};
    SectorRoute tally_{};
};

}
