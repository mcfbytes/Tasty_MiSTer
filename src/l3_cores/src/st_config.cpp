// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/st_config.h"

#include <algorithm>
#include <array>
#include <cstring>

#include "cores/st_ctrl.h"
#include "cores/st_sysctrl.h"
#include "infra/byte_view.h"
#include "infra/endian.h"
#include "infra/indexed_array.h"

namespace mister::cores {

namespace {

std::uint32_t get_u32(std::span<const std::byte> img, std::size_t off) noexcept {
    return infra::load_le<std::uint32_t>(img.subspan(off).first<4>());
}

void put_u32(std::span<std::byte> img, std::size_t off, std::uint32_t v) noexcept {
    infra::store_le<std::uint32_t>(img.subspan(off).first<4>(), v);
}

std::string_view get_str(std::span<const std::byte> img, std::size_t off) noexcept {
    return infra::text_until_nul(img.data() + off, st_cfg::kPathBytes);
}

void put_str(std::span<std::byte> img, std::size_t off, std::string_view s) noexcept {
    std::memcpy(img.data() + off, s.data(), s.size());
}

struct Line {
    std::array<char, st_cfg::kPreviewChars> buf{};
    std::size_t len = 0;

    void add(std::string_view s) noexcept {
        const std::size_t n = std::min(s.size(), buf.size() - 1 - len);
        std::memcpy(buf.data() + len, s.data(), n);
        len += n;
        buf[len] = '\0';
    }

    std::string_view view() const noexcept { return {buf.data(), len}; }
};

constexpr std::array<std::string_view, st_ctrl::CtrlWord::kMemConfigCount> kMemNames =
    {"512kb", "1mb", "2mb", "4mb", "8mb", "14mb", "--", "--"};
constexpr std::array<std::string_view, st_ctrl::CtrlWord::kChoiceCount> kChipsetShort =
    {"ST", "STe", "MST", "ST+"};
constexpr std::array<std::string_view, st_ctrl::CtrlWord::kChoiceCount> kAspectNames =
    {"A0 ", "A1 ", "A2 ", "A3 "};
constexpr std::array<std::string_view, st_ctrl::CtrlWord::kChoiceCount> kScanNames =
    {"", "S1 ", "S2 ", "S3 "};
constexpr std::string_view kEmptySlot = "< empty slot >";

}

std::string_view StConfig::leaf_of(std::string_view path) noexcept {
    const auto slash = path.rfind('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

StConfig StConfig::defaults() noexcept {
    StConfig c{};
    c.system_ctrl = st_ctrl::kDefaultSystemCtrl;
    (void)c.tos_img.assign("games/AtariST/TOS.IMG");
    return c;
}

void StConfig::overlay(
    std::span<const std::byte> blob,
    std::string_view cart,
    std::span<std::byte, st_cfg::kBytes> img
) noexcept {
    encode(img, cart);
    const std::size_t n = std::min(blob.size(), img.size());
    if (n != 0)
        std::memcpy(img.data(), blob.data(), n);
    decode(img);
}

void StConfig::encode(
    std::span<std::byte, st_cfg::kBytes> img,
    std::string_view cart
) const noexcept {
    std::ranges::fill(img, std::byte{0});
    put_u32(img, st_cfg::kSystemCtrl, system_ctrl);
    put_str(img, st_cfg::kTosImg, tos_img.view());
    put_str(img, st_cfg::kCartImg, cart);
    put_str(img, st_cfg::kAcsiImg0, acsi_img[AcsiTarget{0}].view());
    put_str(img, st_cfg::kAcsiImg1, acsi_img[AcsiTarget{1}].view());
    put_u32(img, st_cfg::kExtCtrl, ext_ctrl);
}

void StConfig::decode(std::span<const std::byte, st_cfg::kBytes> img) noexcept {
    system_ctrl = get_u32(img, st_cfg::kSystemCtrl);
    (void)tos_img.assign(get_str(img, st_cfg::kTosImg));
    (void)acsi_img[AcsiTarget{0}].assign(get_str(img, st_cfg::kAcsiImg0));
    (void)acsi_img[AcsiTarget{1}].assign(get_str(img, st_cfg::kAcsiImg1));
    ext_ctrl = get_u32(img, st_cfg::kExtCtrl);
}

std::string_view StConfig::cart_of(std::span<const std::byte, st_cfg::kBytes> img) noexcept {
    return get_str(img, st_cfg::kCartImg);
}

FixedStr<kConfigPreviewChars, StrFit::Clip> StConfig::preview(ConfigBlob blob) noexcept {
    FixedStr<kConfigPreviewChars, StrFit::Clip> out{};
    if (blob.bytes.empty()) {
        (void)out.assign(kEmptySlot);
        return out;
    }

    std::array<std::byte, st_cfg::kBytes> tmp{};
    std::memcpy(tmp.data(), blob.bytes.data(), std::min(blob.bytes.size(), tmp.size()));
    const st_ctrl::CtrlWord sc{get_u32(tmp, st_cfg::kSystemCtrl)};
    Line line;
    line.add(infra::at_index(kMemNames, sc.mem_config()));
    line.add(" ");
    if (!get_str(tmp, st_cfg::kAcsiImg0).empty())
        line.add("H0 ");
    if (!get_str(tmp, st_cfg::kAcsiImg1).empty())
        line.add("H1 ");
    const auto chipset = sc.chipset();
    line.add(infra::at_index(kChipsetShort, chipset));
    line.add(" ");
    if (chipset == st_ctrl::CtrlWord::kChipsetSt && sc.has(st_ctrl::CtrlWord::kBlitter)) {
        line.add("B ");
    }
    if (sc.has(st_ctrl::CtrlWord::kViking))
        line.add("V ");
    line.add(infra::at_index(kAspectNames, sc.aspect()));
    line.add(
        sc.has(st_ctrl::CtrlWord::kVideoColor)  ? "C "
            : sc.has(st_ctrl::CtrlWord::kMde60) ? "M6 "
                                                : "M "
    );
    if (!sc.has(st_ctrl::CtrlWord::kBorder))
        line.add("F ");
    line.add(infra::at_index(kScanNames, sc.scanlines()));
    line.add(sc.has(st_ctrl::CtrlWord::kStereo) ? "AS " : "AM ");

    if (const auto tos = get_str(tmp, st_cfg::kTosImg); !tos.empty()) {
        const std::string_view leaf = leaf_of(tos);
        line.add(
            leaf.size() >= st_cfg::kTosExtChars ? leaf.substr(0, leaf.size() - st_cfg::kTosExtChars)
                                                : leaf
        );
    }
    (void)out.assign(line.view());
    return out;
}

}
