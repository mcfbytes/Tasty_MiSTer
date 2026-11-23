// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"
#include "proto/types.h"

namespace mister::svc {

enum class MountRefusal : std::uint8_t {
    FluxImageOnBlockSlot,
    NibbleImageOnBlockSlot,
    SectorOrderContainerOnBlockSlot,
    DosOrderOnBlockSlot,
    Inch525FluxOnInch35Slot,
    Inch35FluxOnInch525Slot,
    ArchiveUnreadable,
    NotAn800KImage,
    NotA140KImage,
    SourceUnreadable,
    ConversionFailed,
    kCount,
};

inline constexpr std::string_view kMountRefusalTitle = "Wrong disk for slot";
inline constexpr std::chrono::milliseconds kMountRefusalShows{5000};

[[nodiscard]] constexpr std::string_view mount_refusal_text(MountRefusal r) noexcept {
    switch (r) {
        case MountRefusal::FluxImageOnBlockSlot:
            return "Hard-disk slot needs a ProDOS block image, not a WOZ floppy.";
        case MountRefusal::NibbleImageOnBlockSlot:
            return "Hard-disk slot needs a ProDOS block image, not a .nib floppy.";
        case MountRefusal::SectorOrderContainerOnBlockSlot:
            return "This 2MG is DOS/NIB order; not a ProDOS hard-disk image.";
        case MountRefusal::DosOrderOnBlockSlot:
            return "DOS-order disk \xE2\x80\x94 load it in a 5.25\" drive, not a hard disk.";
        case MountRefusal::Inch525FluxOnInch35Slot:
            return "That's a 5.25\" WOZ \xE2\x80\x94 use the 5.25\" drive.";
        case MountRefusal::Inch35FluxOnInch525Slot:
            return "That's a 3.5\" WOZ \xE2\x80\x94 use the 3.5\" drive.";
        case MountRefusal::ArchiveUnreadable:
            return "Could not read the WOZ from the archive.";
        case MountRefusal::NotAn800KImage:
            return "3.5\" drive needs an 800K disk image.";
        case MountRefusal::NotA140KImage:
            return "5.25\" drive needs a 140K disk image.";
        case MountRefusal::SourceUnreadable:
            return "Could not read the disk image.";
        case MountRefusal::ConversionFailed:
            return "Could not convert this disk to WOZ.";
        case MountRefusal::kCount:
            break;
    }
    return {};
}

[[nodiscard]] constexpr Error refusal_error(MountRefusal r, infra::ErrorSite at) noexcept {
    return Error{Errc::mount_failed, at, std::uint32_t{std::to_underlying(r)} + 1u};
}

[[nodiscard]] constexpr std::optional<MountRefusal> refusal_in(const Error& e) noexcept {
    if (e.code != Errc::mount_failed
        || e.detail == 0u
        || e.detail > std::to_underlying(MountRefusal::kCount))
        return std::nullopt;
    return static_cast<MountRefusal>(e.detail - 1u);
}

static_assert(
    !refusal_in(Error{Errc::mount_failed, infra::kNoSite, 0u})
    && refusal_in(refusal_error(MountRefusal::FluxImageOnBlockSlot, infra::kNoSite))
        == MountRefusal::FluxImageOnBlockSlot
);

struct MountRefusalNote {
    proto::SlotIndex slot{};
    MountRefusal reason = MountRefusal::FluxImageOnBlockSlot;
};

using MountRefusalCell = xthread::Telemetry<MountRefusalNote, SeatTag::Io>;

}
