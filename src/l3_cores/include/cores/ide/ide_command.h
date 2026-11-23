// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace mister::cores::ide {

enum class IdeCommand : std::uint8_t {
    Nop = 0x00,
    Reset = 0x08,
    Recalibrate = 0x10,
    ReadRetry = 0x20,
    Read = 0x21,
    WriteRetry = 0x30,
    Write = 0x31,
    ReadVerify = 0x40,
    Seek = 0x70,
    Diagnostic = 0x90,
    InitParameters = 0x91,
    Packet = 0xA0,
    IdentifyPacket = 0xA1,
    ReadMultiple = 0xC4,
    WriteMultiple = 0xC5,
    SetMultiple = 0xC6,
    StandbyImmediate = 0xE3,
    Identify = 0xEC,
    SetFeatures = 0xEF,
    MountImage = 0xFA,
};

[[nodiscard]] constexpr std::optional<IdeCommand> ide_command_of(std::byte raw) noexcept {
    constexpr std::byte kRowMask{0xF0};
    if ((raw & kRowMask) == static_cast<std::byte>(IdeCommand::Recalibrate))
        return IdeCommand::Recalibrate;

    const auto named = static_cast<IdeCommand>(raw);
    switch (named) {
        case IdeCommand::Nop:
        case IdeCommand::Reset:
        case IdeCommand::Recalibrate:
        case IdeCommand::ReadRetry:
        case IdeCommand::Read:
        case IdeCommand::WriteRetry:
        case IdeCommand::Write:
        case IdeCommand::ReadVerify:
        case IdeCommand::Seek:
        case IdeCommand::Diagnostic:
        case IdeCommand::InitParameters:
        case IdeCommand::Packet:
        case IdeCommand::IdentifyPacket:
        case IdeCommand::ReadMultiple:
        case IdeCommand::WriteMultiple:
        case IdeCommand::SetMultiple:
        case IdeCommand::StandbyImmediate:
        case IdeCommand::Identify:
        case IdeCommand::SetFeatures:
        case IdeCommand::MountImage:
            return named;
    }
    return std::nullopt;
}

}
