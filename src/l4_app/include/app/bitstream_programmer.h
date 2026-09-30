// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "app/park_receipt.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "hal/boards_table.h"
#include "hal/program_geometry.h"

namespace mister::hal {
class IBridgeSequencer;
class IFpgaProgrammer;
}  // namespace mister::hal

namespace mister::svc {
class Vfs;
}

namespace mister::app {

inline constexpr std::uint64_t kContainerHeaderBytes = 16;

inline constexpr std::size_t kProgramBufferBytes = 64u * 1024u;

struct PayloadWindow {
    std::uint64_t base = 0;
    std::uint64_t length = 0;
};

[[nodiscard]] Ex<PayloadWindow> container_window(std::span<const std::byte> hdr, std::uint64_t fsz);

class BitstreamProgrammer {
    TASTY_SEAT_EXEMPT(main);

public:
    BitstreamProgrammer(hal::IFpgaProgrammer& programmer, hal::IBridgeSequencer& bridges,
                        const svc::Vfs& vfs, const hal::ProgramGeometryValues& geometry) noexcept
        : programmer_(&programmer), bridges_(&bridges), vfs_(&vfs),
          chunk_bytes_(std::clamp(geometry.chunk_bytes, 1u,
                                  static_cast<std::uint32_t>(kProgramBufferBytes))) {}

    BitstreamProgrammer(const BitstreamProgrammer&) = delete;
    BitstreamProgrammer& operator=(const BitstreamProgrammer&) = delete;

    [[nodiscard]] Ex<void> program(const ParkReceipt& park, std::string_view rel);

    [[nodiscard]] Ex<void> program_at_boot(std::string_view rel);

    [[nodiscard]] bool bridges_down() const noexcept { return bridges_down_; }

    [[nodiscard]] bool programmed() const;

private:
    [[nodiscard]] Ex<void> run_(std::string_view rel);

    hal::IFpgaProgrammer* programmer_;
    hal::IBridgeSequencer* bridges_;
    const svc::Vfs* vfs_;
    bool bridges_down_ = false;
    std::uint32_t chunk_bytes_;
    alignas(64) std::byte buf_[kProgramBufferBytes];
};

consteval bool program_geometry_fits_every_board() {
    return hal::every_measured(
        &hal::BoardProfile::program, [](const hal::ProgramGeometry& p) consteval {
            const auto g = hal::ProgramGeometryValues::measured(p);
            if (g.chunk_bytes == 0u || g.chunk_bytes > kProgramBufferBytes) return false;
            if (g.quantum == 0u || g.chunk_bytes % g.quantum != 0u) return false;
            return g.align != 0u && alignof(BitstreamProgrammer) % g.align == 0u;
        });
}
static_assert(program_geometry_fits_every_board(),
              "a board's program chunk must fit the buffer, keep its quantum and its alignment");
static_assert(kProgramBufferBytes >= kContainerHeaderBytes);

static_assert(sizeof(BitstreamProgrammer) < 128u * 1024u);

}  // namespace mister::app
