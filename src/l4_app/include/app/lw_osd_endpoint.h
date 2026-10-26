// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <utility>

#include "app/osd_grant.h"
#include "app/osd_route.h"
#include "hal/osd_block.h"
#include "hal/reg_block.h"
#include "hal/register_window.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/osd_surface.h"

namespace mister::app {

class LwOsdEndpoint {
    TASTY_SEAT_RESIDENT(Ui);

public:
    class Port {
    public:
        [[nodiscard]] virtual std::uint32_t read(hal::OsdBlock::Reg r) noexcept = 0;
        virtual void write(hal::OsdBlock::Reg r, std::uint32_t v) noexcept = 0;

    protected:
        Port() = default;
        Port(const Port&) = default;
        Port& operator=(const Port&) = default;
        ~Port() = default;
    };

    struct Wiring {
        const OsdGrantCell& grant;
        OsdRouteCell& route;
        const proto::OsdSurface& surface;
        infra::OptRef<Port> port{};
    };

    explicit LwOsdEndpoint(const Wiring& w) noexcept : w_(w) {}

    void serve() noexcept;

    void on_pause() noexcept;
    void on_resume() noexcept;
    void on_stop() noexcept;

    [[nodiscard]] const OsdRoute& route() const noexcept { return tally_; }

    [[nodiscard]] bool bound() const noexcept { return bound_gen_ != 0; }

private:
    static constexpr std::uint32_t kRows = proto::OsdSurface::kPageRows;
    static_assert(
        kRows == hal::OsdBlock::kImageRows
            && proto::OsdSurface::kRowBytes == hal::OsdBlock::kRowBytes,
        "the shadow holds exactly the surface's page rows"
    );

    class BlockPort final : public Port {
        TASTY_SEAT_RESIDENT(Ui);

    public:
        explicit BlockPort(hal::reg::Block lease) noexcept
            : regs_(hal::RegisterWindow<hal::OsdBlock>::borrow(std::move(lease))) {}

        [[nodiscard]] std::uint32_t read(hal::OsdBlock::Reg r) noexcept override {
            TASTY_SEAT_BODY(BlockPort);
            return regs_.read(r);
        }

        void write(hal::OsdBlock::Reg r, std::uint32_t v) noexcept override {
            TASTY_SEAT_BODY(BlockPort);
            regs_.write(r, v);
        }

    private:
        hal::RegisterWindow<hal::OsdBlock> regs_;
    };

    void take_grant_() noexcept;
    void bind_(const OsdGrant& g, std::uint32_t gen) noexcept;
    void unbind_() noexcept;
    void store_frame_() noexcept;
    [[nodiscard]] bool store_row_(std::uint32_t row) noexcept;
    [[nodiscard]] std::uint32_t ctrl_() const noexcept;
    [[nodiscard]] std::uint32_t bound_rows_() const noexcept;
    [[nodiscard]] bool lease_() noexcept;
    void release_() noexcept;
    void publish_() noexcept;
    [[nodiscard]] Port& port_() noexcept;

    Wiring w_;
    OsdGrantCell::Reader grant_rd_{};
    std::uint32_t bound_gen_ = 0;
    std::optional<BlockPort> block_{};
    std::array<std::uint32_t, kRows> stored_{};
    std::uint32_t ctrl_stored_ = 0;
    proto::OsdSurface::Row row_{};
    OsdRoute tally_{};
};

}
