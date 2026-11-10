// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <utility>

#include "app/lw_block_endpoint.h"
#include "app/lw_line.h"
#include "app/sys_grant.h"
#include "app/sys_route.h"
#include "hal/reg_block.h"
#include "hal/register_window.h"
#include "hal/sys_block.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "os/monotonic_clock.h"
#include "proto/status_snapshot.h"
#include "svc/io_coworker.h"

namespace mister::app {

class LwSysEndpoint final : public svc::IIoCoworker,
                            public LwBlockEndpoint<SysGrant, SysRoute, SeatTag::Io> {
    TASTY_SEAT_RESIDENT(Io);

public:
    class Port {
    public:
        [[nodiscard]] virtual std::uint32_t read(hal::SysBlock::Reg r) noexcept = 0;

    protected:
        Port() = default;
        Port(const Port&) = default;
        Port& operator=(const Port&) = default;
        ~Port() = default;
    };

    static constexpr unsigned kMaxReads = 4;

    struct Wiring {
        const SysGrantCell& grant;
        SysRouteCell& route;
        proto::StatusSnapshotCell& snapshots;
        int doorbells = -1;
        infra::OptRef<Port> port{};
    };

    explicit LwSysEndpoint(const Wiring& w) noexcept;

    void serve() noexcept override;
    [[nodiscard]] bool idle() const noexcept override;
    void on_pause() noexcept override;
    void on_resume() noexcept override;
    void on_stop() noexcept override;

    [[nodiscard]] const SysRoute& route() const noexcept { return tally_; }

private:
    class BlockPort final : public Port {
        TASTY_SEAT_RESIDENT(Io);

    public:
        explicit BlockPort(hal::reg::Block lease) noexcept
            : regs_(hal::RegisterWindow<hal::SysBlock>::borrow(std::move(lease))) {}

        [[nodiscard]] std::uint32_t read(hal::SysBlock::Reg r) noexcept override {
            TASTY_SEAT_BODY(BlockPort);
            return regs_.read(r);
        }

    private:
        hal::RegisterWindow<hal::SysBlock> regs_;
    };

    using Base = LwBlockEndpoint<SysGrant, SysRoute, SeatTag::Io>;
    void publish_hook_(const SysRoute& r) noexcept override;
    void prepare_hook_(const SysGrant& g) noexcept override;
    [[nodiscard]] bool arm_hook_(const SysGrant& g) noexcept override;
    void bind_hook_(const SysGrant& g) noexcept override;
    void bound_hook_() noexcept override;
    void answer_hook_(std::uint16_t gen) noexcept override;
    void unbind_hook_() noexcept override;
    void serve_bound_() noexcept override;
    [[nodiscard]] bool idle_bound_hook_() const noexcept override;

    [[nodiscard]] Port& port_() noexcept;

    [[nodiscard]] std::optional<proto::StatusSnapshot> read_snapshot_() noexcept;
    void serve_status_(bool rang) noexcept;

    Wiring w_;
    std::optional<BlockPort> block_{};
    LwLine line_{};
    bool recheck_ = false;
    std::optional<hal::SysBlock::Stflg> sent_stflg_{};
    os::MonotonicClock clock_{};
};

}
