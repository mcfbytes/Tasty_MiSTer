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
    w[item(ModelineItem::Hact)] = hact;
    w[item(ModelineItem::Hfp)] = hfp;
    w[item(ModelineItem::Hs)] = hs;
    w[item(ModelineItem::Hbp)] = hbp;
    w[item(ModelineItem::Vact)] = vact;
    w[item(ModelineItem::Vfp)] = vfp;
    w[item(ModelineItem::Vs)] = vs;
    w[item(ModelineItem::Vbp)] = vbp;
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
    f.hfp = kFbDvRbrd;
    f.hbp = kFbDvLbrd;
    f.hact = hact + (hfp - f.hfp) + (hbp - f.hbp);
    f.vfp = kFbDvBbrd;
    f.vbp = kFbDvUbrd;
    f.vact = vact + (vfp - f.vfp) + (vbp - f.vbp);
    return f;
}

std::array<std::uint16_t, 26> Modeline::to_wire(const PllBlock& pll, const WireOptions& opt) const {

    const Modeline timing = opt.direct_video ? direct_video_fix() : *this;
    const std::array<std::uint32_t, kModelineItems> fix = timing.to_words(pll);

    std::array<std::uint16_t, 26> out{};
    std::size_t n = 0;
    for (std::size_t i = item(ModelineItem::Hact); i <= item(ModelineItem::Vbp); ++i) {
        std::uint32_t v = fix[i];
        if (i == item(ModelineItem::Hact)) {

            if (pixel_repeat)
                v |= kTimingFlagBit;
            if (opt.use_vrr)
                v |= kWireVrrBit;
        } else if (i == item(ModelineItem::Hs)) {
            if (hpol)
                v |= kTimingFlagBit;
        } else if (i == item(ModelineItem::Vs)) {
            if (vpol)
                v |= kTimingFlagBit;
        }
        out[n++] = static_cast<std::uint16_t>(v);
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
