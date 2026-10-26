// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <utility>

#include "app/sys_grant.h"
#include "app/sys_route.h"
#include "hal/reg_block.h"
#include "hal/register_window.h"
#include "hal/sys_block.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "os/monotonic_clock.h"
#include "os/uio_handle.h"
#include "proto/status_snapshot.h"
#include "svc/io_coworker.h"

namespace mister::app {

class LwSysEndpoint final : public svc::IIoCoworker {
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
    ~LwSysEndpoint() override;

    void serve() noexcept override;
    [[nodiscard]] bool idle() const noexcept override;
    void on_pause() noexcept override;
    void on_resume() noexcept override;
    void on_stop() noexcept override;

    [[nodiscard]] const SysRoute& route() const noexcept { return tally_; }

    [[nodiscard]] bool bound() const noexcept { return bound_gen_ != 0; }

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

    void take_grant_() noexcept;
    void bind_(const SysGrant& g, std::uint32_t gen) noexcept;
    void unbind_() noexcept;
    void answer_bind_(std::uint16_t gen) noexcept;
    [[nodiscard]] bool arm_line_(const SysGrant& g) noexcept;
    [[nodiscard]] bool lease_() noexcept;
    void release_() noexcept;
    void publish_() noexcept;
    [[nodiscard]] Port& port_() noexcept;
    [[nodiscard]] bool line_rang_() const noexcept;
    [[nodiscard]] std::uint32_t drain_cause_(bool rang) noexcept;

    [[nodiscard]] bool read_snapshot_(proto::StatusSnapshot& into) noexcept;
    void serve_status_(bool rang) noexcept;

    Wiring w_;
    SysGrantCell::Reader grant_rd_{};
    SysGrant grant_{};
    std::uint32_t bound_gen_ = 0;
    std::optional<BlockPort> block_{};
    os::UioHandle line_{};
    bool recheck_ = false;
    std::optional<std::uint32_t> sent_stflg_{};
    proto::StatusSnapshot snap_{};
    os::MonotonicClock clock_{};
    SysRoute tally_{};
};

}
