// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "cores/atari8/atari8_status.h"
#include "cores/atari8/sio_call.h"
#include "cores/atari8/sio_command.h"
#include "cores/atari8/sio_drive.h"
#include "cores/atari8/sio_frame.h"
#include "cores/atari8/sio_reply.h"
#include "cores/atari8/sio_slot.h"
#include "cores/atari8/sio_types.h"
#include "infra/byte_count.h"
#include "infra/error.h"
#include "infra/indexed_array.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "svc/sector_ask.h"
#include "svc/sector_read.h"

namespace mister::svc {
class Vfs;
}

namespace mister::cores::atari8 {

class SioDrives {
    TASTY_SEAT_RESIDENT(Pcm);

public:
    static constexpr SioDriveCount kDrives = SioDriveCount::kAll;
    static constexpr SioUnit kHardDisk = SioUnit::kHardDisk;

    static constexpr infra::ByteCount kBufferBytes{768};
    using Buffer = std::span<std::byte, kBufferBytes.size()>;

    void mount(SioUnit unit, const svc::Vfs& vfs, std::string_view path, Atari8Status status);

    void close_all() noexcept;

    [[nodiscard]] static SioUnit unit_of(SioDevice device) noexcept;

    [[nodiscard]] static std::optional<SioUnit> unit_of(SioSlot slot) noexcept;
    [[nodiscard]] infra::OptRef<SioDrive> drive(SioUnit unit) noexcept;
    [[nodiscard]] infra::OptRef<const SioDrive> drive(SioUnit unit) const noexcept;

    [[nodiscard]] bool serves_serial(const SioFrame& frame) const noexcept;

    [[nodiscard]] std::optional<SioCommand> verb_for(
        const SioFrame& frame,
        const SioCall& call
    ) const noexcept;

    [[nodiscard]] infra::ByteCount write_bytes(const SioFrame& frame, SioUnit unit) const noexcept;

    [[nodiscard]] bool timed(SioUnit unit) const noexcept;

    [[nodiscard]] SioReply execute(
        SioCommand verb,
        const SioFrame& frame,
        SioUnit unit,
        const SioCall& call,
        Buffer buffer
    );

private:
    struct Placed {
        SioDrive& drive;
        svc::ISectorImage& image;
        svc::SectorIndex at;
    };

    [[nodiscard]] std::optional<Placed> place_(SioUnit unit, SioSector sector) noexcept;

    [[nodiscard]] bool scan_table_(bool read_only);
    void unmap_all_() noexcept;

    [[nodiscard]] SioReply read_(
        const SioFrame& frame,
        SioUnit unit,
        const SioCall& call,
        Buffer buffer
    );
    [[nodiscard]] SioReply write_(
        SioCommand verb,
        const SioFrame& frame,
        SioUnit unit,
        const SioCall& call,
        Buffer buffer
    );
    [[nodiscard]] SioReply status_(SioUnit unit, const SioCall& call, Buffer buffer) const noexcept;
    [[nodiscard]] SioReply percom_(SioUnit unit, Buffer buffer) const noexcept;
    [[nodiscard]] SioReply format_(SioUnit unit, Buffer buffer);
    [[nodiscard]] SioReply speed_(
        const SioFrame& frame,
        SioUnit unit,
        const SioCall& call,
        Buffer buffer
    ) const noexcept;
    [[nodiscard]] SioReply media_change_();
    [[nodiscard]] SioReply device_info_(SioUnit unit, const SioCall& call, Buffer buffer);
    [[nodiscard]] static svc::SectorAsk ask_(svc::SectorIndex at, const SioCall& call) noexcept;

    infra::IndexedArray<SioDrive, kDrives.v, SioUnit> drives_{};
};

}
