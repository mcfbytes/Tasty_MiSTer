// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

namespace mister::svc {

struct SysButton {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr SysButton next() const noexcept {
        return SysButton{static_cast<std::uint8_t>(v + 1u)};
    }

    friend constexpr bool operator==(SysButton, SysButton) = default;
    friend constexpr auto operator<=>(SysButton, SysButton) = default;
};

inline constexpr SysButton kSysBtnRight{0};
inline constexpr SysButton kSysBtnLeft{1};
inline constexpr SysButton kSysBtnDown{2};
inline constexpr SysButton kSysBtnUp{3};
inline constexpr SysButton kSysBtnA{4};
inline constexpr SysButton kSysBtnB{5};
inline constexpr SysButton kSysBtnX{6};
inline constexpr SysButton kSysBtnY{7};
inline constexpr SysButton kSysBtnL{8};
inline constexpr SysButton kSysBtnR{9};
inline constexpr SysButton kSysBtnSelect{10};
inline constexpr SysButton kSysBtnStart{11};
inline constexpr SysButton kSysBtnCntOk{21};
inline constexpr SysButton kSysBtnCntEsc{22};
inline constexpr SysButton kSysBtnMenuFunc{23};
inline constexpr SysButton kSysBtnAxis1X{24};
inline constexpr SysButton kSysBtnAxis1Y{25};
inline constexpr SysButton kSysBtnAxis2X{26};
inline constexpr SysButton kSysBtnAxis2Y{27};
inline constexpr SysButton kSysBtnAxisX{28};
inline constexpr SysButton kSysBtnAxisY{29};

inline constexpr std::array<SysButton, 4>
    kSysAxisCluster{kSysBtnAxis1X, kSysBtnAxis1Y, kSysBtnAxis2X, kSysBtnAxis2Y};

[[nodiscard]] constexpr bool is_direction(SysButton word) noexcept {
    return word <= kSysBtnUp;
}

[[nodiscard]] constexpr bool is_button(SysButton word) noexcept {
    return word >= kSysBtnA && word <= kSysBtnStart;
}

[[nodiscard]] constexpr bool is_pad_control(SysButton word) noexcept {
    return is_direction(word) || is_button(word);
}

}
