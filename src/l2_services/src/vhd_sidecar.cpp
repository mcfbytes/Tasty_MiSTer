// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/vhd_sidecar.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

#include "infra/ascii.h"
#include "infra/fixed_str.h"
#include "infra/parse_int.h"

namespace mister::svc {

namespace {

using Cylinders = DiskGeometry::Cylinders;
using Heads = DiskGeometry::Heads;
using Sectors = DiskGeometry::Sectors;

constexpr std::string_view kSidecarExtension = ".cfg";
constexpr std::string_view kSectorsKey = "SECTORS";
constexpr std::string_view kHeadsKey = "HEADS";
constexpr std::string_view kCylindersKey = "CYLINDERS";

struct NumeralBase {
    std::string_view prefix;
    infra::Radix radix;

    [[nodiscard]] constexpr bool opens(std::string_view digits) const noexcept {
        return digits.size() > prefix.size() && infra::istarts_with(digits, prefix);
    }
};

constexpr std::array kNumeralBases{
    NumeralBase{.prefix = "0x", .radix = infra::Radix::Hex},
    NumeralBase{.prefix = "0b", .radix = infra::Radix::Binary},
    NumeralBase{.prefix = "0", .radix = infra::Radix::Octal},
};

constexpr std::size_t kLineBuf = 64;
using Line = FixedStr<kLineBuf, StrFit::Clip>;

template <class Value>
struct Within {
    Value least{};
    Value most{};

    [[nodiscard]] constexpr bool holds(Value v) const noexcept { return least <= v && v <= most; }
};

constexpr Within<Sectors> kSectorsRange{Sectors{17}, Sectors{256}};
constexpr Within<Heads> kHeadsRange{Heads{2}, Heads{16}};
constexpr Within<Cylinders> kCylindersRange{Cylinders{0}, Cylinders{65535}};

[[nodiscard]] Error invalid(infra::ErrorSite at) noexcept {
    return VhdSidecar::refused(VhdSidecar::Refusal::InvalidConfig, at);
}

[[nodiscard]] Line kept(std::string_view raw) noexcept {
    Line line{};
    bool leading = true;
    for (const char& c : raw) {
        if (!infra::ascii_space(c))
            leading = false;
        if (leading)
            continue;
        if (infra::ascii_space(c) || infra::ascii_alnum(c))
            (void)line.append({&c, 1});
    }
    std::string_view text = line.view();
    while (!text.empty() && infra::ascii_space(text.back()))
        text.remove_suffix(1);
    Line trimmed{};
    (void)trimmed.assign(text);
    return trimmed;
}

template <class Value>
[[nodiscard]] std::optional<Value> number(std::string_view digits) noexcept {
    using Rep = decltype(Value{}.v);
    std::optional<Rep> rep{};
    bool decimal = true;
    for (const NumeralBase& base : kNumeralBases) {
        if (!base.opens(digits))
            continue;
        rep = infra::parse_int<Rep>(digits.substr(base.prefix.size()), base.radix);
        decimal = false;
        break;
    }
    if (decimal)
        rep = infra::parse_int<Rep>(digits, infra::Radix::Decimal);
    if (!rep)
        return std::nullopt;
    return Value{*rep};
}

template <class Value>
[[nodiscard]] std::optional<Value> ranged(
    std::string_view digits,
    const Within<Value>& range
) noexcept {
    const std::optional<Value> value = number<Value>(digits);
    if (!value || !range.holds(*value))
        return std::nullopt;
    return value;
}

[[nodiscard]] Ex<void> take_line(std::string_view line, VhdSidecar& sidecar) noexcept {
    const auto key_end = std::ranges::find_if(line, infra::ascii_space);
    if (key_end == line.end())
        return {};
    const std::string_view key{line.begin(), key_end};
    const auto value_at = std::find_if_not(key_end, line.end(), infra::ascii_space);
    const std::string_view value{value_at, line.end()};
    if (infra::ieq(key, kSectorsKey)) {
        const auto sectors = ranged(value, kSectorsRange);
        if (!sectors)
            return std::unexpected(invalid(ERR_SITE()));
        sidecar.sectors = sectors->in_byte();
        return {};
    }
    if (infra::ieq(key, kHeadsKey)) {
        const auto heads = ranged(value, kHeadsRange);
        if (!heads)
            return std::unexpected(invalid(ERR_SITE()));
        sidecar.heads = *heads;
        return {};
    }
    if (infra::ieq(key, kCylindersKey)) {
        const auto cylinders = ranged(value, kCylindersRange);
        if (!cylinders)
            return std::unexpected(invalid(ERR_SITE()));
        sidecar.cylinders = *cylinders;
        return {};
    }
    return std::unexpected(invalid(ERR_SITE()));
}

}

std::optional<infra::PathText> vhd_sidecar_path(std::string_view image_path) noexcept {
    const std::size_t dot = image_path.rfind('.');
    if (dot == std::string_view::npos || dot == 0 || dot + 1 == image_path.size())
        return std::nullopt;
    infra::PathText path{};
    if (!path.assign(image_path.substr(0, dot)) || !path.append(kSidecarExtension))
        return std::nullopt;
    return path;
}

[[nodiscard]] Ex<VhdSidecar> parse_vhd_sidecar(std::string_view text) noexcept {

    text = text.substr(0, text.find('\0'));
    VhdSidecar sidecar{};
    while (true) {
        const std::size_t line_end = text.find('\n');
        const Line line = kept(text.substr(0, line_end));
        if (const auto taken = take_line(line.view(), sidecar); !taken)
            return std::unexpected(taken.error());
        if (line_end == std::string_view::npos)
            break;
        text.remove_prefix(line_end + 1);
    }
    return sidecar;
}

[[nodiscard]] Ex<proto::BlockCount> apply_vhd_sidecar(
    const VhdSidecar& sidecar,
    proto::FileSize image
) noexcept {
    if (sidecar.sectors.none() || sidecar.heads.none()) {
        return std::unexpected(VhdSidecar::refused(VhdSidecar::Refusal::MissingConfig, ERR_SITE()));
    }
    const proto::BlockCount cylinder = sidecar.heads.cylinder_blocks(sidecar.sectors);

    const proto::BlockCount configured = sidecar.cylinders.none()
        ? cylinder
        : sidecar.cylinders.blocks(cylinder);
    if (image < DiskGeometry::kSector.bytes_of(configured)) {
        return std::unexpected(VhdSidecar::refused(VhdSidecar::Refusal::InvalidSize, ERR_SITE()));
    }
    if (sidecar.cylinders.none())
        return DiskGeometry::kSector.blocks_in(image);
    return configured;
}

}
