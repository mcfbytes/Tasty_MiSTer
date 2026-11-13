// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "proto/bit_field.h"

namespace mister::cores::n64 {

enum class PadPort : std::uint8_t { P1, P2, P3, P4 };

[[nodiscard]] constexpr std::size_t port_index(PadPort p) noexcept {
    return static_cast<std::size_t>(p);
}

inline constexpr std::array<PadPort, 4>
    kPadPorts{PadPort::P1, PadPort::P2, PadPort::P3, PadPort::P4};

namespace opt {

inline constexpr proto::BitField kReset{proto::StatusBit{0}, 1};
inline constexpr proto::BitField kReloadSave{proto::StatusBit{40}, 1};
inline constexpr proto::BitField kSaveBackup{proto::StatusBit{41}, 1};
inline constexpr proto::BitField kAutosave{proto::StatusBit{42}, 1};
inline constexpr proto::BitField kAutoDetect{proto::StatusBit{64}, 1};
inline constexpr proto::BitField kCicType{proto::StatusBit{65}, 4};
inline constexpr proto::BitField kNoEpak{proto::StatusBit{70}, 1};
inline constexpr proto::BitField kCpak{proto::StatusBit{71}, 1};
inline constexpr proto::BitField kRpak{proto::StatusBit{72}, 1};
inline constexpr proto::BitField kTpak{proto::StatusBit{73}, 1};
inline constexpr proto::BitField kRtc{proto::StatusBit{74}, 1};
inline constexpr proto::BitField kSaveType{proto::StatusBit{75}, 3};
inline constexpr proto::BitField kSysType{proto::StatusBit{79}, 2};
inline constexpr proto::BitField kAutoPak{proto::StatusBit{81}, 1};
inline constexpr proto::BitField kPatches{proto::StatusBit{90}, 1};
inline constexpr proto::BitField kCheats{proto::StatusBit{103}, 1};
inline constexpr proto::BitField kDdDevelopment{proto::StatusBit{107}, 1};

inline constexpr std::array<proto::BitField, 4> kControllers{
    proto::BitField{proto::StatusBit{49}, 3},
    proto::BitField{proto::StatusBit{52}, 3},
    proto::BitField{proto::StatusBit{55}, 3},
    proto::BitField{proto::StatusBit{58}, 3}
};

}

[[nodiscard]] constexpr proto::BitField field_of(PadPort p) noexcept {
    return opt::kControllers[port_index(p)];
}

}
