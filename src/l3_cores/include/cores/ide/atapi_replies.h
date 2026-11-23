// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "cores/ide/atapi_packet.h"
#include "cores/ide/atapi_toc.h"
#include "cores/ide/atapi_volume.h"
#include "infra/byte_count.h"
#include "proto/types.h"
#include "svc/types.h"

namespace mister::cores::ide {

enum class AtapiAudioStatus : std::uint8_t { Playing = 0x11, Paused = 0x12, Stopped = 0x13 };

[[nodiscard]] constexpr std::byte wire_byte(AtapiAudioStatus status) noexcept {
    return static_cast<std::byte>(status);
}

[[nodiscard]] infra::ByteCount write_toc(
    const AtapiToc& toc,
    const AtapiPacket& packet,
    std::span<std::byte> out
) noexcept;

[[nodiscard]] infra::ByteCount write_capacity(
    const AtapiToc& toc,
    std::span<std::byte> out
) noexcept;

[[nodiscard]] infra::ByteCount write_subchannel(
    const AtapiToc& toc,
    const AtapiPacket& packet,
    AtapiAudioStatus status,
    std::optional<proto::Lba> head,
    std::span<std::byte> out
) noexcept;

[[nodiscard]] infra::ByteCount write_mode_pages(
    AtapiPacket::PageSelect page,
    AtapiVolume port0,
    std::span<std::byte> out
) noexcept;

[[nodiscard]] infra::ByteCount write_inquiry(
    infra::ByteCount alloc,
    std::span<std::byte> out
) noexcept;

[[nodiscard]] infra::ByteCount write_configuration(
    infra::ByteCount alloc,
    std::span<std::byte> out
) noexcept;

[[nodiscard]] infra::ByteCount write_disc_information(
    const AtapiToc& toc,
    infra::ByteCount alloc,
    std::span<std::byte> out
) noexcept;

[[nodiscard]] infra::ByteCount write_track_information(
    const AtapiToc& toc,
    svc::TrackIndex row,
    infra::ByteCount alloc,
    std::span<std::byte> out
) noexcept;

}
