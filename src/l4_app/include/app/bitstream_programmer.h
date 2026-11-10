// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "app/all_leases_home.h"
#include "app/park_receipt.h"
#include "infra/ascii.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "hal/boards_table.h"
#include "hal/program_geometry.h"

namespace mister::hal {
class IBridgeSequencer;
class IFpgaProgrammer;
}

namespace mister::svc {
class IFile;
class Vfs;
}

namespace mister::app {

inline constexpr std::size_t kContainerHeaderBytes = 16;
inline constexpr proto::FileSize kContainerHeader{kContainerHeaderBytes};

inline constexpr std::size_t kProgramBufferBytes = 64u * 1024u;

struct PayloadWindow {
    proto::FileSize base{};
    proto::FileSize length{};
};

[[nodiscard]] Ex<PayloadWindow> container_window(
    std::span<const std::byte> hdr,
    proto::FileSize fsz
);

[[nodiscard]] Ex<PayloadWindow> read_payload_window(svc::IFile& f, proto::FileSize size);

class BitstreamProgrammer {
    TASTY_SEAT_RESIDENT(Main);

public:
    BitstreamProgrammer(
        hal::IFpgaProgrammer& programmer,
        hal::IBridgeSequencer& bridges,
        const svc::Vfs& vfs,
        const hal::ProgramGeometryValues& geometry
    ) noexcept
        : programmer_(&programmer),
          bridges_(&bridges),
          vfs_(&vfs),
          chunk_bytes_(geometry.chunk_bytes.clamped<std::uint32_t>(
              hal::ChunkBytes{1u},
              hal::ChunkBytes{static_cast<std::uint32_t>(kProgramBufferBytes)}
          )) {}

    BitstreamProgrammer(const BitstreamProgrammer&) = delete;
    BitstreamProgrammer& operator=(const BitstreamProgrammer&) = delete;

    [[nodiscard]] Ex<void> program(
        const ParkReceipt& park,
        AllLeasesHome home,
        std::string_view rel
    );

    [[nodiscard]] Ex<void> program_at_boot(AllLeasesHome home, std::string_view rel);

    [[nodiscard]] bool bridges_down() const noexcept { return bridges_down_; }

    [[nodiscard]] bool programmed() const;

private:
    [[nodiscard]] Ex<void> run_(std::string_view rel);

    hal::IFpgaProgrammer* programmer_;
    hal::IBridgeSequencer* bridges_;
    const svc::Vfs* vfs_;
    bool bridges_down_ = false;
    std::uint32_t chunk_bytes_;
    alignas(64) std::array<std::byte, kProgramBufferBytes> buf_{};
};

consteval bool program_geometry_fits_every_board() {
    return hal::every_measured(
        &hal::BoardProfile::program,
        [](const hal::ProgramGeometry& p) consteval {
            return p.is_valid(
                infra::ByteCount{kProgramBufferBytes},
                hal::AlignBytes{alignof(BitstreamProgrammer)}
            );
        }
    );
}

static_assert(
    program_geometry_fits_every_board(),
    "a board's program chunk must fit the buffer, keep its quantum and its alignment"
);
static_assert(kProgramBufferBytes >= kContainerHeaderBytes);

static_assert(sizeof(BitstreamProgrammer) < 128u * 1024u);

}
