// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/conf_str_caps.h"
#include "conf_str_detail.h"

#include <cctype>
#include <charconv>

namespace mister::proto {

namespace {

constexpr std::string_view kHexPrefixLower = "0x";
constexpr std::string_view kHexPrefixUpper = "0X";
constexpr std::string_view kSsToken = "SS";
constexpr std::string_view kUartToken = "UART";
constexpr std::string_view kMidiToken = "MIDI";
constexpr std::string_view kIrqToken = "IRQ:";
constexpr std::string_view kAxiToken = "AXI:";

[[nodiscard]] std::optional<std::uint32_t> parse_num(std::string_view s, unsigned base) {
    constexpr int kHexRadix = 16;
    if (s.starts_with(kHexPrefixLower) || s.starts_with(kHexPrefixUpper)) {
        s.remove_prefix(kHexPrefixLower.size());
        base = kHexRadix;
    }
    const auto* first = s.data();
    const auto* last = s.data() + s.size();
    std::uint32_t out = 0;
    auto [ptr, ec] = std::from_chars(first, last, out, static_cast<int>(base));
    if (ec != std::errc{} || ptr != last || s.empty())
        return std::nullopt;
    return out;
}

bool starts_with_icase(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size())
        return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        const auto a = static_cast<unsigned char>(s[i]);
        const auto b = static_cast<unsigned char>(prefix[i]);
        if (std::toupper(a) != std::toupper(b))
            return false;
    }
    return true;
}

std::optional<reactor::Cause> cause_from(std::string_view name) {
    using reactor::Cause;
    if (name == "BLK")
        return Cause::Blk;
    if (name == "FIFO")
        return Cause::Fifo;
    if (name == "RING")
        return Cause::Ring;
    if (name == "TICK")
        return Cause::CoreTick;
    return std::nullopt;
}

[[nodiscard]] Scan scan_hex(std::string_view s, std::size_t pos) {
    constexpr int kHexRadix = 16;
    std::uint32_t value = 0;
    const auto [ptr, ec] = std::from_chars(s.data() + pos, s.data() + s.size(), value, kHexRadix);
    if (ec != std::errc{} || ptr == s.data() + pos)
        return {0, pos};
    return {value, static_cast<std::size_t>(ptr - s.data())};
}

[[nodiscard]] std::size_t scan_speed_list(std::string_view s, std::size_t pos) {
    constexpr int kDecimalRadix = 10;
    constexpr int kMaxSpeedEntries = 10;
    for (int entry = 0; entry < kMaxSpeedEntries && pos < s.size(); ++entry) {
        std::uint32_t speed = 0;

        const auto [ptr, ec] = std::from_chars(
            s.data() + pos,
            s.data() + s.size(),
            speed,
            kDecimalRadix
        );
        (void)ec;
        pos = static_cast<std::size_t>(ptr - s.data());
        if (pos < s.size() && s[pos] == '(') {
            ++pos;

            while (pos < s.size()
                   && s[pos] != ';'
                   && s[pos] != ':'
                   && s[pos] != ')'
                   && s[pos] != ',') {
                ++pos;
            }
            if (pos < s.size() && s[pos] == ')')
                ++pos;
        }
        if (pos < s.size() && s[pos] == ':')
            ++pos;
    }
    return pos;
}

}

std::string_view ConfStrCaps::try_ss(std::string_view rest, hal::PhysRegion aperture) {
    if (!starts_with_icase(rest, kSsToken))
        return rest;
    constexpr std::uint32_t kMaxSaveStateBytes = 128u * 1024u * 1024u;
    std::size_t p = kSsToken.size();
    const Scan base_scan = scan_hex(rest, p);
    p = base_scan.pos;
    const std::uint32_t base = base_scan.value;
    std::uint32_t size = 0;

    std::size_t next = p;
    if (p < rest.size() && rest[p] == ':') {
        ++p;
        next = p;
        const Scan size_scan = scan_hex(rest, p);
        p = size_scan.pos;
        size = size_scan.value;
    }

    const std::optional<hal::FabricAddr> at = hal::to_fabric(aperture.phys);
    const std::uint32_t low = at ? at->v : 0u;
    const auto end = static_cast<std::uint32_t>(low + aperture.len);
    const bool valid = at.has_value()
        && size > 0
        && size <= kMaxSaveStateBytes
        && base >= low
        && base < end
        && (base + size) < end;
    ss_ = valid ? std::optional<SaveStateDecl>(SaveStateDecl{hal::FabricAddr{base}, size})
                : std::nullopt;
    caps_.push_back({Capability::Kind::SaveState, std::string(rest.substr(0, p))});
    return rest.substr(next);
}

std::string_view ConfStrCaps::try_uart(std::string_view rest) {
    if (!starts_with_icase(rest, kUartToken))
        return rest;
    const std::size_t p = scan_speed_list(rest, kUartToken.size());
    caps_.push_back({Capability::Kind::Uart, std::string(rest.substr(0, p))});
    return rest.substr(p);
}

std::string_view ConfStrCaps::try_midi(std::string_view rest) {
    if (!starts_with_icase(rest, kMidiToken))
        return rest;
    const std::size_t p = scan_speed_list(rest, kMidiToken.size());
    caps_.push_back({Capability::Kind::Midi, std::string(rest.substr(0, p))});
    return rest.substr(p);
}

Ex<std::string_view> ConfStrCaps::try_irq(std::string_view rest) {
    constexpr int kHexRadix = 16;
    constexpr int kDecimalRadix = 10;

    constexpr std::uint32_t kFaultNoEquals = 1;
    constexpr std::uint32_t kFaultBadBase = 2;
    constexpr std::uint32_t kFaultBadLine = 3;
    if (!rest.starts_with(kIrqToken))
        return rest;
    const std::size_t comma = rest.find(',');
    const std::size_t seg_end = comma == std::string_view::npos ? rest.size() : comma;
    const std::string_view body = rest.substr(kIrqToken.size(), seg_end - kIrqToken.size());

    const std::size_t eq = body.find('=');
    if (eq == std::string_view::npos) {
        return std::unexpected(Error{Errc::bad_confstr, ERR_SITE(), kFaultNoEquals});
    }
    const auto klass = cause_from(body.substr(0, eq));
    std::string_view tail = body.substr(eq + 1);
    std::string_view line_s = tail;
    std::uint32_t cause_base = 0;
    if (const std::size_t colon = tail.find(':'); colon != std::string_view::npos) {
        line_s = tail.substr(0, colon);
        const auto parsed_base = parse_num(tail.substr(colon + 1), kHexRadix);
        if (!parsed_base) {
            return std::unexpected(Error{Errc::bad_confstr, ERR_SITE(), kFaultBadBase});
        }
        cause_base = *parsed_base;
    }

    const auto line = parse_num(line_s, kDecimalRadix);
    if (!klass || !line || *line < kIrqPoolFirstLine || *line > kIrqOrdinalMax) {
        return std::unexpected(Error{Errc::bad_confstr, ERR_SITE(), kFaultBadLine});
    }
    irq_.push_back(
        IrqBinding{
            *klass,
            static_cast<os::UioLine>(static_cast<std::uint8_t>(*line - kIrqPoolFirstLine)),
            hal::LwOffset{cause_base}
        }
    );
    caps_.push_back({Capability::Kind::Irq, std::string(rest.substr(0, seg_end))});
    return rest.substr(seg_end);
}

Ex<std::string_view> ConfStrCaps::try_axi(std::string_view rest) {
    constexpr int kHexRadix = 16;
    constexpr std::size_t kAxiMinParts = 2;
    constexpr std::size_t kAxiMaxParts = 3;
    constexpr std::size_t kAxiKindIndex = 2;
    constexpr std::uint32_t kFaultBadAxi = 4;
    if (!rest.starts_with(kAxiToken))
        return rest;
    const std::size_t comma = rest.find(',');
    const std::size_t seg_end = comma == std::string_view::npos ? rest.size() : comma;
    const std::string_view body = rest.substr(kAxiToken.size(), seg_end - kAxiToken.size());

    const auto parts = split(body, ':');

    if (parts.size() < kAxiMinParts || parts.size() > kAxiMaxParts) {
        return std::unexpected(Error{Errc::bad_confstr, ERR_SITE(), kFaultBadAxi});
    }
    const auto addr = parse_num(parts[0], kHexRadix);
    const auto size = parse_num(parts[1], kHexRadix);
    if (!addr || !size) {
        return std::unexpected(Error{Errc::bad_confstr, ERR_SITE(), kFaultBadAxi});
    }

    if (parts.size() == kAxiMinParts) {

        axi_unkinded_.push_back(AxiUnkindedDecl{AxiRawBase{*addr}, *size});
    } else if (parts[kAxiKindIndex] == "LW") {
        axi_lw_.push_back(AxiLwDecl{hal::LwOffset{*addr}, *size});
    } else if (parts[kAxiKindIndex] == "DDR") {
        axi_ddr_.push_back(AxiDdrDecl{hal::FabricAddr{*addr}, *size});
    } else {

        ++axi_unknown_kinds_;
    }
    caps_.push_back({Capability::Kind::Axi, std::string(rest.substr(0, seg_end))});
    return rest.substr(seg_end);
}

void ConfStrCaps::carry_unknown(std::string_view run) {
    caps_.push_back({Capability::Kind::Unknown, std::string(run)});
}

std::optional<os::UioLine> ConfStrCaps::irq_line_for(reactor::Cause klass) const {
    for (const IrqBinding& b : irq_) {
        if (b.klass == klass)
            return b.line;
    }
    return std::nullopt;
}

}
