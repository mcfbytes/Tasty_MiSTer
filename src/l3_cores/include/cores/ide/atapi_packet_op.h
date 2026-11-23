// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace mister::cores::ide {

enum class AtapiPacketOp : std::uint8_t {
    TestUnitReady = 0x00,
    RequestSense = 0x03,
    Inquiry = 0x12,
    StartStopUnit = 0x1B,
    PreventAllowRemoval = 0x1E,
    ReadCapacity = 0x25,
    Read10 = 0x28,
    Seek10 = 0x2B,
    SynchronizeCache = 0x35,
    ReadSubchannel = 0x42,
    ReadToc = 0x43,
    PlayAudio10 = 0x45,
    GetConfiguration = 0x46,
    PlayAudioMsf = 0x47,
    GetEventStatus = 0x4A,
    PauseResume = 0x4B,
    StopPlayScan = 0x4E,
    ReadDiscInformation = 0x51,
    ReadTrackInformation = 0x52,
    ModeSelect10 = 0x55,
    ModeSense10 = 0x5A,
    Read12 = 0xA8,
};

inline constexpr std::array kAtapiPacketOps{
    AtapiPacketOp::TestUnitReady,
    AtapiPacketOp::RequestSense,
    AtapiPacketOp::Inquiry,
    AtapiPacketOp::StartStopUnit,
    AtapiPacketOp::PreventAllowRemoval,
    AtapiPacketOp::ReadCapacity,
    AtapiPacketOp::Read10,
    AtapiPacketOp::Seek10,
    AtapiPacketOp::SynchronizeCache,
    AtapiPacketOp::ReadSubchannel,
    AtapiPacketOp::ReadToc,
    AtapiPacketOp::PlayAudio10,
    AtapiPacketOp::GetConfiguration,
    AtapiPacketOp::PlayAudioMsf,
    AtapiPacketOp::GetEventStatus,
    AtapiPacketOp::PauseResume,
    AtapiPacketOp::StopPlayScan,
    AtapiPacketOp::ReadDiscInformation,
    AtapiPacketOp::ReadTrackInformation,
    AtapiPacketOp::ModeSelect10,
    AtapiPacketOp::ModeSense10,
    AtapiPacketOp::Read12,
};

[[nodiscard]] constexpr std::optional<AtapiPacketOp> atapi_packet_op_of(std::byte raw) noexcept {
    for (const AtapiPacketOp op : kAtapiPacketOps)
        if (static_cast<std::byte>(op) == raw)
            return op;
    return std::nullopt;
}

[[nodiscard]] constexpr bool answers_when_not_ready(AtapiPacketOp op) noexcept {
    return op == AtapiPacketOp::GetConfiguration
        || op == AtapiPacketOp::GetEventStatus
        || op == AtapiPacketOp::Inquiry
        || op == AtapiPacketOp::RequestSense;
}

static_assert(sizeof(AtapiPacketOp) == 1, "the opcode is the packet's first byte");
static_assert(atapi_packet_op_of(std::byte{0x43}) == AtapiPacketOp::ReadToc);
static_assert(!atapi_packet_op_of(std::byte{0xBE}), "a byte outside the set is refused");

}
