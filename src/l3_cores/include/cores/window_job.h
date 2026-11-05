// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "cores/core_window.h"
#include "cores/job_arena.h"
#include "cores/window_job_order.h"
#include "hal/types.h"
#include "infra/error.h"
#include "proto/reset_edge.h"
#include "proto/status_word.h"
#include "proto/types.h"
#include "svc/vfs.h"

namespace mister::cores {

class IWindowJob {
public:
    virtual ~IWindowJob() = default;
    IWindowJob(const IWindowJob&) = delete;
    IWindowJob& operator=(const IWindowJob&) = delete;

    [[nodiscard]] virtual hal::FabricAddr window_addr() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t window_len() const noexcept = 0;

    [[nodiscard]] virtual Ex<bool> step(ICoreWindow& window, const svc::Vfs& vfs) = 0;

protected:
    IWindowJob() = default;
};

using WindowJobBuild = IWindowJob* (*)(JobArena& arena, const WindowJobOrder& order) noexcept;

class IWindowSave {
public:
    virtual ~IWindowSave() = default;

    [[nodiscard]] virtual WindowJobBuild on_osd_open(WindowJobOrder& order) noexcept = 0;

    [[nodiscard]] virtual WindowJobBuild on_reset_edge(
        const proto::ResetEdge& edge,
        WindowJobOrder& order
    ) noexcept = 0;

    [[nodiscard]] virtual bool owes_flush(proto::IoIndex index) const noexcept = 0;

    [[nodiscard]] virtual WindowJobBuild take_flush(
        proto::IoIndex index,
        WindowJobOrder& order
    ) noexcept = 0;

protected:
    IWindowSave() = default;
    IWindowSave(const IWindowSave&) = default;
    IWindowSave& operator=(const IWindowSave&) = default;
};

}
