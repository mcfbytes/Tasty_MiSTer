// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/modeline.h"

#include <cstddef>

#include "svc/modeline_item.h"

namespace mister::svc {

namespace {

constexpr std::uint32_t kFbDvLbrd = 3;
constexpr std::uint32_t kFbDvRbrd = 6;
constexpr std::uint32_t kFbDvUbrd = 2;
constexpr std::uint32_t kFbDvBbrd = 2;

constexpr std::uint32_t kTimingFlagBit = 1u << 15;
constexpr std::uint32_t kWireVrrBit = 1u << 14;
constexpr std::uint32_t kWirePllAddressSelector = 0x4000u;
constexpr std::uint32_t kWireVsyncAlign = 0x8000u;

}

std::array<std::uint32_t, 26> Modeline::to_words(const PllBlock& pll) const {

    std::array<std::uint32_t, kModelineItems> w{};
    w[item(ModelineItem::Mode)] = 0;
    w[item(ModelineItem::Hact)] = h.active;
    w[item(ModelineItem::Hfp)] = h.front_porch;
    w[item(ModelineItem::Hs)] = h.sync;
    w[item(ModelineItem::Hbp)] = h.back_porch;
    w[item(ModelineItem::Vact)] = v.active;
    w[item(ModelineItem::Vfp)] = v.front_porch;
    w[item(ModelineItem::Vs)] = v.sync;
    w[item(ModelineItem::Vbp)] = v.back_porch;
    for (std::size_t n = 0; n < PllBlock::kWrites; ++n) {
        const std::size_t at = item(ModelineItem::PllFirst) + n * PllBlock::kWordsPerWrite;
        w[at] = pll_reg_word(pll.writes[n].selector);
        pll.writes[n].payload.store_into(w[at + 1]);
    }
    w[item(ModelineItem::Hpol)] = hpol ? 1u : 0u;
    w[item(ModelineItem::Vpol)] = vpol ? 1u : 0u;
    w[item(ModelineItem::Vic)] = vic;
    w[item(ModelineItem::Rb)] = rb;
    w[item(ModelineItem::PixelRepeat)] = pixel_repeat ? 1u : 0u;
    return w;
}

Modeline Modeline::direct_video_fix() const {

    Modeline f = *this;
    f.h.front_porch = kFbDvRbrd;
    f.h.back_porch = kFbDvLbrd;
    f.h.active = h.active + (h.front_porch - f.h.front_porch) + (h.back_porch - f.h.back_porch);
    f.v.front_porch = kFbDvBbrd;
    f.v.back_porch = kFbDvUbrd;
    f.v.active = v.active + (v.front_porch - f.v.front_porch) + (v.back_porch - f.v.back_porch);
    return f;
}

std::array<std::uint16_t, 26> Modeline::to_wire(const PllBlock& pll, const WireOptions& opt) const {

    const Modeline timing = opt.direct_video ? direct_video_fix() : *this;
    const std::array<std::uint32_t, kModelineItems> fix = timing.to_words(pll);

    std::array<std::uint16_t, 26> out{};
    std::size_t n = 0;
    for (std::size_t i = item(ModelineItem::Hact); i <= item(ModelineItem::Vbp); ++i) {
        std::uint32_t word = fix[i];
        if (i == item(ModelineItem::Hact)) {

            if (pixel_repeat)
                word |= kTimingFlagBit;
            if (opt.use_vrr)
                word |= kWireVrrBit;
        } else if (i == item(ModelineItem::Hs)) {
            if (hpol)
                word |= kTimingFlagBit;
        } else if (i == item(ModelineItem::Vs)) {
            if (vpol)
                word |= kTimingFlagBit;
        }
        out[n++] = static_cast<std::uint16_t>(word);
    }
    for (std::size_t i = 0; i < PllBlock::kWrites; ++i) {
        const PllWrite& pw = pll.writes[i];
        std::uint32_t selector = pll_reg_word(pw.selector) | kWirePllAddressSelector;
        if (i == 0 && opt.vsync_align)
            selector |= kWireVsyncAlign;
        out[n++] = static_cast<std::uint16_t>(selector);
        const std::array<std::uint16_t, 2> payload = pw.payload.halves();
        out[n++] = payload[0];
        out[n++] = payload[1];
    }
    return out;
}

}
