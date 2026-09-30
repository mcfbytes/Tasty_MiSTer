// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "cores/core_window.h"
#include "infra/error.h"
#include "proto/reset_edge.h"
#include "proto/status_word.h"
#include "proto/types.h"
#include "svc/vfs.h"

namespace mister::cores {

inline constexpr std::size_t kWindowJobBytes = 640u * 1024u;
inline constexpr std::size_t kWindowJobAlign = 64;

inline constexpr std::size_t kWindowOrderBytes = 1100;

class IWindowJob {
public:
    virtual ~IWindowJob() = default;
    IWindowJob(const IWindowJob&) = delete;
    IWindowJob& operator=(const IWindowJob&) = delete;

    [[nodiscard]] virtual std::uint32_t window_addr() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t window_len() const noexcept = 0;

    [[nodiscard]] virtual Ex<bool> step(ICoreWindow& window, const svc::Vfs& vfs) = 0;

protected:
    IWindowJob() = default;
};

class IWindowJobKind {
public:
    [[nodiscard]] virtual IWindowJob* build(std::span<std::byte> arena,
                                            std::span<const std::byte> order) const noexcept = 0;

protected:
    IWindowJobKind() = default;
    ~IWindowJobKind() = default;
    IWindowJobKind(const IWindowJobKind&) = default;
    IWindowJobKind& operator=(const IWindowJobKind&) = default;
};

class IWindowSave {
public:
    virtual ~IWindowSave() = default;

    [[nodiscard]] virtual const IWindowJobKind* on_osd_open(
        std::span<std::byte> order) noexcept = 0;

    [[nodiscard]] virtual const IWindowJobKind* on_reset_edge(
        const proto::ResetEdge& edge, std::span<std::byte> order) noexcept = 0;

    [[nodiscard]] virtual bool owes_flush(proto::IoIndex index) const noexcept = 0;

    [[nodiscard]] virtual const IWindowJobKind* take_flush(proto::IoIndex index,
                                                           std::span<std::byte> order) noexcept = 0;

protected:
    IWindowSave() = default;
    IWindowSave(const IWindowSave&) = default;
    IWindowSave& operator=(const IWindowSave&) = default;
};

}  // namespace mister::cores
