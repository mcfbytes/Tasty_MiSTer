// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/st_config.h"

#include <algorithm>
#include <cstring>

#include "cores/st_sysctrl.h"
#include "infra/byte_view.h"

namespace mister::cores {

namespace {

std::uint32_t get_u32(std::span<const std::byte> img, std::size_t off) noexcept {
    return static_cast<std::uint32_t>(img[off])
        | (static_cast<std::uint32_t>(img[off + 1]) << 8)
        | (static_cast<std::uint32_t>(img[off + 2]) << 16)
        | (static_cast<std::uint32_t>(img[off + 3]) << 24);
}

void put_u32(std::span<std::byte> img, std::size_t off, std::uint32_t v) noexcept {
    for (std::size_t i = 0; i < 4; ++i)
        img[off + i] = static_cast<std::byte>((v >> (8 * i)) & 0xFFu);
}

std::string_view get_str(std::span<const std::byte> img, std::size_t off) noexcept {
    return infra::text_until_nul(img.data() + off, st_cfg::kPathBytes);
}

void put_str(std::span<std::byte> img, std::size_t off, std::string_view s) noexcept {
    std::memcpy(img.data() + off, s.data(), s.size());
}

struct Line {
    char buf[64]{};
    std::size_t len = 0;

    void add(std::string_view s) noexcept {
        const std::size_t n = std::min(s.size(), sizeof(buf) - 1 - len);
        std::memcpy(buf + len, s.data(), n);
        len += n;
        buf[len] = '\0';
    }

    std::string_view view() const noexcept { return {buf, len}; }
};

constexpr std::string_view kMemNames[8] = {"512kb", "1mb", "2mb", "4mb", "8mb", "14mb", "--", "--"};
constexpr std::string_view kChipsetShort[4] = {"ST", "STe", "MST", "ST+"};
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
    put_str(img, st_cfg::kAcsiImg0, acsi_img[0].view());
    put_str(img, st_cfg::kAcsiImg1, acsi_img[1].view());
    put_u32(img, st_cfg::kExtCtrl, ext_ctrl);
}

void StConfig::decode(std::span<const std::byte, st_cfg::kBytes> img) noexcept {
    system_ctrl = get_u32(img, st_cfg::kSystemCtrl);
    (void)tos_img.assign(get_str(img, st_cfg::kTosImg));
    (void)acsi_img[0].assign(get_str(img, st_cfg::kAcsiImg0));
    (void)acsi_img[1].assign(get_str(img, st_cfg::kAcsiImg1));
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
    const std::uint32_t sc = get_u32(tmp, st_cfg::kSystemCtrl);
    Line line;
    line.add(kMemNames[(sc >> 1) & 7u]);
    line.add(" ");
    if (!get_str(tmp, st_cfg::kAcsiImg0).empty())
        line.add("H0 ");
    if (!get_str(tmp, st_cfg::kAcsiImg1).empty())
        line.add("H1 ");
    const std::uint32_t chipset = (sc >> 23) & 3u;
    line.add(kChipsetShort[chipset]);
    line.add(" ");
    if (chipset == 0 && (sc & st_ctrl::kBlitter) != 0)
        line.add("B ");
    if ((sc & st_ctrl::kViking) != 0)
        line.add("V ");
    const unsigned ar = ((sc & st_ctrl::kVideoAspect1) != 0 ? 1u : 0u)
        | ((sc & st_ctrl::kVideoAspect2) != 0 ? 2u : 0u);
    line.add(ar == 0 ? "A0 " : ar == 1 ? "A1 " : ar == 2 ? "A2 " : "A3 ");
    line.add((sc & st_ctrl::kVideoColor) != 0 ? "C " : (sc & st_ctrl::kMde60) != 0 ? "M6 " : "M ");
    if ((sc & st_ctrl::kBorder) == 0)
        line.add("F ");
    const std::uint32_t sl = (sc >> 20) & 3u;
    if (sl != 0)
        line.add(sl == 1 ? "S1 " : sl == 2 ? "S2 " : "S3 ");
    line.add((sc & st_ctrl::kStereo) != 0 ? "AS " : "AM ");

    if (const auto tos = get_str(tmp, st_cfg::kTosImg); !tos.empty()) {
        const std::string_view leaf = leaf_of(tos);
        line.add(leaf.size() >= 4 ? leaf.substr(0, leaf.size() - 4) : leaf);
    }
    (void)out.assign(line.view());
    return out;
}

}
