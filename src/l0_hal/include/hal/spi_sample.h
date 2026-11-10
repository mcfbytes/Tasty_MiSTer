// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/types.h"

namespace mister::hal {

inline constexpr GpiField kGpiWidthField{16, 0x1};
inline constexpr GpiField kGpiAckField{17, 0x1};
inline constexpr GpiField kGpiIoVersionField{18, 0x3};
inline constexpr GpiField kGpiHdmiField{20, 0x1};
inline constexpr GpiField kGpiFreeCapsField{21, 0x3F};
inline constexpr GpiField kGpiDirectoryField{27, 0x1};
inline constexpr GpiField kGpiBoardDigitalField{28, 0x1};
inline constexpr GpiField kGpiButtonOsdField{29, 0x1};
inline constexpr GpiField kGpiButtonUserField{30, 0x1};
inline constexpr GpiField kGpiNotReadyField{31, 0x1};

inline constexpr GpiField kGpiCoreIdField{8, 0x00FF'FFFFu};
inline constexpr GpiField kGpiTypeByteField{0, 0xFF};

struct SpiSample {

    static constexpr ButtonMask kMenuButton{1u << 0};
    static constexpr ButtonMask kUserButton{1u << 1};

    bool ready = false;
    ButtonMask buttons{};
    bool hdmi = false;
};

[[nodiscard]] constexpr SpiSample decode_sample(GpiWord gpi) noexcept {
    return SpiSample{
        .ready = !gpi.test(kGpiNotReadyField),
        .buttons = gpi.buttons(),
        .hdmi = gpi.test(kGpiHdmiField),
    };
}

}
