// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <optional>

#include "cores/archie/kart_key.h"
#include "svc/types.h"

namespace mister::cores::archie {

struct KartKeyRow {
    svc::EvdevCode code{};
    KartKey key{};
};

inline constexpr auto kKartKeyRows = std::to_array<KartKeyRow>({
    {svc::EvdevCode{1}, KartKey{0x00}},   {svc::EvdevCode{2}, KartKey{0x11}},
    {svc::EvdevCode{3}, KartKey{0x12}},   {svc::EvdevCode{4}, KartKey{0x13}},
    {svc::EvdevCode{5}, KartKey{0x14}},   {svc::EvdevCode{6}, KartKey{0x15}},
    {svc::EvdevCode{7}, KartKey{0x16}},   {svc::EvdevCode{8}, KartKey{0x17}},
    {svc::EvdevCode{9}, KartKey{0x18}},   {svc::EvdevCode{10}, KartKey{0x19}},
    {svc::EvdevCode{11}, KartKey{0x1A}},  {svc::EvdevCode{12}, KartKey{0x1B}},
    {svc::EvdevCode{13}, KartKey{0x1C}},  {svc::EvdevCode{14}, KartKey{0x1E}},
    {svc::EvdevCode{15}, KartKey{0x26}},  {svc::EvdevCode{16}, KartKey{0x27}},
    {svc::EvdevCode{17}, KartKey{0x28}},  {svc::EvdevCode{18}, KartKey{0x29}},
    {svc::EvdevCode{19}, KartKey{0x2A}},  {svc::EvdevCode{20}, KartKey{0x2B}},
    {svc::EvdevCode{21}, KartKey{0x2C}},  {svc::EvdevCode{22}, KartKey{0x2D}},
    {svc::EvdevCode{23}, KartKey{0x2E}},  {svc::EvdevCode{24}, KartKey{0x2F}},
    {svc::EvdevCode{25}, KartKey{0x30}},  {svc::EvdevCode{26}, KartKey{0x31}},
    {svc::EvdevCode{27}, KartKey{0x32}},  {svc::EvdevCode{28}, KartKey{0x47}},
    {svc::EvdevCode{29}, KartKey{0x3B}},  {svc::EvdevCode{30}, KartKey{0x3C}},
    {svc::EvdevCode{31}, KartKey{0x3D}},  {svc::EvdevCode{32}, KartKey{0x3E}},
    {svc::EvdevCode{33}, KartKey{0x3F}},  {svc::EvdevCode{34}, KartKey{0x40}},
    {svc::EvdevCode{35}, KartKey{0x41}},  {svc::EvdevCode{36}, KartKey{0x42}},
    {svc::EvdevCode{37}, KartKey{0x43}},  {svc::EvdevCode{38}, KartKey{0x44}},
    {svc::EvdevCode{39}, KartKey{0x45}},  {svc::EvdevCode{40}, KartKey{0x46}},
    {svc::EvdevCode{41}, KartKey{0x10}},  {svc::EvdevCode{42}, KartKey{0x4C}},
    {svc::EvdevCode{43}, KartKey{0x33}},  {svc::EvdevCode{44}, KartKey{0x4E}},
    {svc::EvdevCode{45}, KartKey{0x4F}},  {svc::EvdevCode{46}, KartKey{0x50}},
    {svc::EvdevCode{47}, KartKey{0x51}},  {svc::EvdevCode{48}, KartKey{0x52}},
    {svc::EvdevCode{49}, KartKey{0x53}},  {svc::EvdevCode{50}, KartKey{0x54}},
    {svc::EvdevCode{51}, KartKey{0x55}},  {svc::EvdevCode{52}, KartKey{0x56}},
    {svc::EvdevCode{53}, KartKey{0x57}},  {svc::EvdevCode{54}, KartKey{0x58}},
    {svc::EvdevCode{55}, KartKey{0x24}},  {svc::EvdevCode{56}, KartKey{0x5E}},
    {svc::EvdevCode{57}, KartKey{0x5F}},  {svc::EvdevCode{58}, KartKey{0x5D}},
    {svc::EvdevCode{59}, KartKey{0x01}},  {svc::EvdevCode{60}, KartKey{0x02}},
    {svc::EvdevCode{61}, KartKey{0x03}},  {svc::EvdevCode{62}, KartKey{0x04}},
    {svc::EvdevCode{63}, KartKey{0x05}},  {svc::EvdevCode{64}, KartKey{0x06}},
    {svc::EvdevCode{65}, KartKey{0x07}},  {svc::EvdevCode{66}, KartKey{0x08}},
    {svc::EvdevCode{67}, KartKey{0x09}},  {svc::EvdevCode{68}, KartKey{0x0A}},
    {svc::EvdevCode{69}, KartKey{0x22}},  {svc::EvdevCode{71}, KartKey{0x37}},
    {svc::EvdevCode{72}, KartKey{0x38}},  {svc::EvdevCode{73}, KartKey{0x39}},
    {svc::EvdevCode{74}, KartKey{0x3A}},  {svc::EvdevCode{75}, KartKey{0x48}},
    {svc::EvdevCode{76}, KartKey{0x49}},  {svc::EvdevCode{77}, KartKey{0x4A}},
    {svc::EvdevCode{78}, KartKey{0x4B}},  {svc::EvdevCode{79}, KartKey{0x5A}},
    {svc::EvdevCode{80}, KartKey{0x5B}},  {svc::EvdevCode{81}, KartKey{0x5C}},
    {svc::EvdevCode{82}, KartKey{0x65}},  {svc::EvdevCode{83}, KartKey{0x66}},
    {svc::EvdevCode{87}, KartKey{0x0B}},  {svc::EvdevCode{88}, KartKey{0x0C}},
    {svc::EvdevCode{96}, KartKey{0x67}},  {svc::EvdevCode{97}, KartKey{0x61}},
    {svc::EvdevCode{98}, KartKey{0x23}},  {svc::EvdevCode{99}, KartKey{0x0D}},
    {svc::EvdevCode{100}, KartKey{0x60}}, {svc::EvdevCode{102}, KartKey{0x20}},
    {svc::EvdevCode{103}, KartKey{0x59}}, {svc::EvdevCode{104}, KartKey{0x21}},
    {svc::EvdevCode{105}, KartKey{0x62}}, {svc::EvdevCode{106}, KartKey{0x64}},
    {svc::EvdevCode{107}, KartKey{0x35}}, {svc::EvdevCode{108}, KartKey{0x63}},
    {svc::EvdevCode{109}, KartKey{0x36}}, {svc::EvdevCode{110}, KartKey{0x1F}},
    {svc::EvdevCode{111}, KartKey{0x34}}, {svc::EvdevCode{119}, KartKey{0x0F}},
    {svc::EvdevCode{127}, KartKey{0x71}},
});

[[nodiscard]] constexpr std::optional<KartKey> kart_key_of(svc::EvdevCode code) noexcept {
    for (const KartKeyRow& r : kKartKeyRows)
        if (r.code == code)
            return r.key;
    return std::nullopt;
}

}
