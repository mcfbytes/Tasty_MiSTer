// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace mister::cores::atari8 {

enum class SioCommand : std::uint8_t {
    FormatSingle = 0x21,
    FormatMedium = 0x22,
    Speed = 0x3F,
    MediaChange = 0x46,
    ReadPercom = 0x4E,
    Write = 0x50,
    Read = 0x52,
    Status = 0x53,
    WriteVerify = 0x57,
    DeviceInfo = 0x6E,
};

enum class SioBus : std::uint8_t { Serial, Pbi };

inline constexpr auto kSioCommands = std::to_array<SioCommand>({
    SioCommand::FormatSingle,
    SioCommand::FormatMedium,
    SioCommand::Speed,
    SioCommand::MediaChange,
    SioCommand::ReadPercom,
    SioCommand::Write,
    SioCommand::Read,
    SioCommand::Status,
    SioCommand::WriteVerify,
    SioCommand::DeviceInfo,
});

[[nodiscard]] constexpr std::optional<SioCommand> classify(std::byte code) noexcept {
    const auto* const hit = std::ranges::find_if(kSioCommands, [code](SioCommand c) {
        return std::to_underlying(c) == std::to_integer<std::uint8_t>(code);
    });
    if (hit == kSioCommands.end())
        return std::nullopt;
    return *hit;
}

}
