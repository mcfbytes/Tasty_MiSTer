// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "infra/fixed_str.h"
#include "svc/dir_entry.h"
#include "svc/types.h"

namespace mister::svc {

inline constexpr std::size_t kMaxAltInis = 3;

enum class IniChoice : std::uint8_t { Main = 0, Alt1 = 1, Alt2 = 2, Alt3 = 3 };

[[nodiscard]] constexpr IniChoice ini_choice_of_handoff(std::uint8_t byte) noexcept {
    return byte > kMaxAltInis ? IniChoice::Main : static_cast<IniChoice>(byte);
}

[[nodiscard]] constexpr std::uint8_t handoff_byte_of(IniChoice c) noexcept {
    return static_cast<std::uint8_t>(c);
}

inline constexpr EvdevCode kIniSwitchRight{106};
inline constexpr EvdevCode kIniSwitchLeft{105};
inline constexpr EvdevCode kIniSwitchUp{103};
inline constexpr EvdevCode kIniSwitchDown{108};

[[nodiscard]] constexpr IniChoice ini_choice_of_direction(EvdevCode code) noexcept {
    if (code == kIniSwitchRight)
        return IniChoice::Main;
    if (code == kIniSwitchLeft)
        return IniChoice::Alt1;
    if (code == kIniSwitchUp)
        return IniChoice::Alt2;
    return IniChoice::Alt3;
}

struct AltIniSlot {
    std::uint8_t v = 0;

    [[nodiscard]] static constexpr std::optional<AltIniSlot> of(std::uint8_t alt) noexcept {
        if (alt == 0 || alt > kMaxAltInis)
            return std::nullopt;
        return AltIniSlot{static_cast<std::uint8_t>(alt - 1)};
    }

    [[nodiscard]] static constexpr std::optional<AltIniSlot> of(IniChoice c) noexcept {
        return of(handoff_byte_of(c));
    }

    friend constexpr bool operator==(AltIniSlot, AltIniSlot) = default;
};

class AltIniIndex {
public:
    AltIniIndex() = default;

    [[nodiscard]] static AltIniIndex scan(std::span<const std::string> root_dirents);

    [[nodiscard]] static AltIniIndex from_scan(std::span<const DirEntry> entries);

    [[nodiscard]] bool has(IniChoice c) const noexcept;

    [[nodiscard]] std::string_view name_for(IniChoice c) const noexcept;

    [[nodiscard]] std::string label_for(IniChoice c) const;

private:
    std::array<FixedStr<64, StrFit::Clip>, kMaxAltInis> names_{};
    bool main_ = false;
};

}
