// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <utility>

#include "app/lw_block_endpoint.h"
#include "app/osd_grant.h"
#include "app/osd_route.h"
#include "hal/osd_block.h"
#include "hal/reg_block.h"
#include "hal/register_window.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/osd_surface.h"

namespace mister::app {

class LwOsdEndpoint : public LwBlockEndpoint<OsdGrant, OsdRoute, SeatTag::Ui> {
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

    explicit LwOsdEndpoint(const Wiring& w) noexcept
        : LwBlockEndpoint<OsdGrant, OsdRoute, SeatTag::Ui>(w.grant),
          w_(w) {}

    [[nodiscard]] const OsdRoute& route() const noexcept { return tally_; }

private:
    static constexpr proto::OsdRowCount kRows = proto::OsdSurface::kPageRows;
    static_assert(
        kRows == proto::OsdRowCount{hal::OsdBlock::kImageRows}
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

    using Base = LwBlockEndpoint<OsdGrant, OsdRoute, SeatTag::Ui>;
    void publish_hook_(const OsdRoute& r) noexcept override;
    void prepare_hook_(const OsdGrant& g) noexcept override;
    [[nodiscard]] bool arm_hook_(const OsdGrant& g) noexcept override;
    void bind_hook_(const OsdGrant& g) noexcept override;
    void bound_hook_() noexcept override;
    void answer_hook_(std::uint16_t) noexcept override;
    void unbind_hook_() noexcept override;
    void serve_bound_() noexcept override;
    [[nodiscard]] bool idle_bound_hook_() const noexcept override;

    void store_frame_() noexcept;
    [[nodiscard]] bool store_row_(proto::OsdRow row) noexcept;
    [[nodiscard]] std::uint32_t ctrl_() const noexcept;
    [[nodiscard]] proto::OsdRowCount bound_rows_() const noexcept;
    [[nodiscard]] Port& port_() noexcept;

    Wiring w_;
    std::optional<BlockPort> block_{};
    proto::OsdRowCells<std::uint32_t, kRows.size()> stored_{};
    std::uint32_t ctrl_stored_ = 0;
    proto::OsdSurface::Row row_{};
};

}
