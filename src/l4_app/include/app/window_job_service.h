// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "app/load_window.h"
#include "app/load_window_map.h"
#include "cores/window_job.h"
#include "infra/counter.h"
#include "infra/error.h"
#include "infra/loan_channel.h"
#include "infra/seat.h"
#include "hal/phys_region.h"
#include "proto/reset_edge.h"
#include "proto/types.h"
#include "svc/io_coworker.h"
#include "svc/vfs.h"

namespace mister::app {

struct WindowJobSlot {
    TASTY_SEAT_MEDIATOR(Any, Any);
    const cores::IWindowJobKind* kind = nullptr;
    Error err{Errc::io, 0, 0};
    std::uint8_t ok = 0;
    std::byte order[cores::kWindowOrderBytes];
    alignas(cores::kWindowJobAlign) std::byte arena[cores::kWindowJobBytes];
};

class WindowJobService final : public svc::IIoCoworker {
    TASTY_SEAT_MEDIATOR(RT, Io);

public:
    using Chan = xthread::LoanChannel<WindowJobSlot, 1, SeatTag::RT, SeatTag::Io>;

    WindowJobService(const svc::Vfs& vfs, ILoadWindowMap& map, hal::PhysRegion aperture) noexcept
        : vfs_(&vfs), map_(&map), aperture_(aperture), chan_(xthread::Polled{}) {}

    WindowJobService(const svc::Vfs& vfs, ILoadWindowMap& map, hal::PhysRegion aperture,
                     xthread::WakeFlag& io_wake) noexcept
        : vfs_(&vfs), map_(&map), aperture_(aperture), chan_(io_wake) {}

    ~WindowJobService() override;

    [[nodiscard]] bool arm_osd_open(cores::IWindowSave& role) noexcept;
    [[nodiscard]] bool arm_reset_edge(cores::IWindowSave& role,
                                      const proto::ResetEdge& edge) noexcept;

    [[nodiscard]] bool hold_open(const cores::IWindowSave& role, proto::IoIndex index,
                                 std::uint32_t act) noexcept;

    [[nodiscard]] std::optional<std::uint32_t> service(cores::IWindowSave* role) noexcept;

    void forget_held() noexcept;

    [[nodiscard]] bool busy() const noexcept { return armed_; }
    [[nodiscard]] std::uint32_t jobs_ok() const noexcept { return ok_.get(); }
    [[nodiscard]] std::uint32_t jobs_failed() const noexcept { return failed_.get(); }
    [[nodiscard]] std::uint32_t refusals() const noexcept { return refusals_.get(); }
    [[nodiscard]] std::uint32_t flushes() const noexcept { return flushes_.get(); }

    void serve() noexcept override;
    [[nodiscard]] bool idle() const noexcept override;

private:
    enum class Held : std::uint8_t { None, Waiting, Flushing };

    [[nodiscard]] Chan::Loan acquire_() noexcept;
    void send_(Chan::Loan&& loan, const cores::IWindowJobKind* kind) noexcept;
    void finish_(bool ok, Error err) noexcept;

    const svc::Vfs* vfs_;
    ILoadWindowMap* map_;
    hal::PhysRegion aperture_;

    bool armed_ = false;
    Held held_ = Held::None;
    std::uint32_t held_act_ = 0;
    proto::IoIndex held_index_{};
    xthread::Counter ok_{}, failed_{}, refusals_{}, flushes_{};
    Chan chan_;

    Chan::Job job_{};
    cores::IWindowJob* running_ = nullptr;
    std::optional<LoadWindow> window_{};
};

}  // namespace mister::app
