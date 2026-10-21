// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

namespace mister::cores::n64 {

struct StatusField {
    std::uint8_t lo = 0;
    std::uint8_t width = 1;

    [[nodiscard]] constexpr std::uint8_t hi() const noexcept {
        return static_cast<std::uint8_t>(lo + width - 1);
    }
};

namespace opt {

inline constexpr StatusField kReset{0, 1};
inline constexpr StatusField kReloadSave{40, 1};
inline constexpr StatusField kSaveBackup{41, 1};
inline constexpr StatusField kAutosave{42, 1};
inline constexpr StatusField kAutoDetect{64, 1};
inline constexpr StatusField kCicType{65, 4};
inline constexpr StatusField kNoEpak{70, 1};
inline constexpr StatusField kCpak{71, 1};
inline constexpr StatusField kRpak{72, 1};
inline constexpr StatusField kTpak{73, 1};
inline constexpr StatusField kRtc{74, 1};
inline constexpr StatusField kSaveType{75, 3};
inline constexpr StatusField kSysType{79, 2};
inline constexpr StatusField kAutoPak{81, 1};
inline constexpr StatusField kPatches{90, 1};
inline constexpr StatusField kCheats{103, 1};
inline constexpr StatusField kDdDevelopment{107, 1};

inline constexpr std::array<StatusField, 4>
    kControllers{StatusField{49, 3}, StatusField{52, 3}, StatusField{55, 3}, StatusField{58, 3}};

}

}
