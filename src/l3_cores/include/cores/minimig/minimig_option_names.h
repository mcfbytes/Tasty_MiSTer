// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <string_view>

namespace mister::cores::minimig {

inline constexpr auto kJoySwapNames = std::to_array<std::string_view>({"Off", " On"});
inline constexpr auto kOffOnNames = std::to_array<std::string_view>({"Off", "On"});
inline constexpr auto kCpuNames = std::to_array<std::string_view>({"68000", "68010", "68020"});
inline constexpr auto kChipsetNames = std::to_array<std::string_view>(
    {"OCS-A500", "OCS-A1000", "ECS", "AGA"}
);
inline constexpr auto kChipRamNames = std::to_array<std::string_view>({"512K", "1M", "1.5M", "2M"});
inline constexpr auto kFastRamNames = std::to_array<std::string_view>(
    {"none", "2M", "4M", "8M", "256M", "384M"}
);
inline constexpr auto kSlowRamNames = std::to_array<std::string_view>(
    {"none", "512K", "1M", "1.5M"}
);
inline constexpr auto kEnabledNames = std::to_array<std::string_view>({"Disabled", "Enabled"});
inline constexpr auto kJoyModeNames = std::to_array<std::string_view>(
    {"Digital", "Analog", "CD32"}
);
inline constexpr auto kFastIdeNames = std::to_array<std::string_view>({"On ", "Off"});
inline constexpr auto kUnitNames = std::to_array<std::string_view>(
    {"Disabled", "Fixed/HDD", "Removable/CD"}
);
inline constexpr auto kTvNames = std::to_array<std::string_view>({"PAL", "NTSC"});
inline constexpr auto kScandoublerNames = std::to_array<std::string_view>(
    {"Off", "HQ2x", "CRT 25%", "CRT 50%", "CRT 75%"}
);
inline constexpr auto kVideoAreaNames = std::to_array<std::string_view>({"Blank", "Blank+"});
inline constexpr auto kAspectNames = std::to_array<std::string_view>(
    {"Original", "Full Screen", "[ARC1]", "[ARC2]"}
);
inline constexpr auto kPixelClockNames = std::to_array<std::string_view>({"28MHz", "Adaptive"});
inline constexpr auto kScalingNames = std::to_array<std::string_view>(
    {"Normal", "V-Integer", "HV-Integer-", "HV-Integer+", "HV-Integer"}
);
inline constexpr auto kRtgNames = std::to_array<std::string_view>({"Normal", "HV-Integer"});
inline constexpr auto kStereoNames = std::to_array<std::string_view>({"0%", "25%", "50%", "100%"});
inline constexpr auto kFilterNames = std::to_array<std::string_view>({"Auto(LED)", "Off", "On"});
inline constexpr auto kModelNames = std::to_array<std::string_view>({"A500", "A1200"});
inline constexpr auto kPaulaNames = std::to_array<std::string_view>({"Normal", "PWM"});

}
