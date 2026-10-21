// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/conf_str_caps.h"
#include "conf_str_detail.h"

#include <cctype>
#include <charconv>

namespace mister::proto {

namespace {

[[nodiscard]] std::optional<std::uint32_t> parse_num(std::string_view s, unsigned base) {
    if (s.starts_with("0x") || s.starts_with("0X")) {
        s.remove_prefix(2);
        base = 16;
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
    std::uint32_t value = 0;
    const auto [ptr, ec] = std::from_chars(s.data() + pos, s.data() + s.size(), value, 16);
    if (ec != std::errc{} || ptr == s.data() + pos)
        return {0, pos};
    return {value, static_cast<std::size_t>(ptr - s.data())};
}

[[nodiscard]] std::size_t scan_speed_list(std::string_view s, std::size_t pos) {
    for (int entry = 0; entry < 10 && pos < s.size(); ++entry) {
        std::uint32_t speed = 0;

        const auto [ptr, ec] = std::from_chars(s.data() + pos, s.data() + s.size(), speed, 10);
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

std::size_t ConfStrCaps::try_ss(std::string_view f1, std::size_t pos, hal::PhysRegion aperture) {
    if (!starts_with_icase(f1.substr(pos), "SS"))
        return pos;
    std::size_t p = pos + 2;
    const Scan base_scan = scan_hex(f1, p);
    p = base_scan.pos;
    const std::uint32_t base = base_scan.value;
    std::uint32_t size = 0;

    std::size_t next = p;
    if (p < f1.size() && f1[p] == ':') {
        ++p;
        next = p;
        const Scan size_scan = scan_hex(f1, p);
        p = size_scan.pos;
        size = size_scan.value;
    }

    const std::optional<hal::FabricAddr> at = hal::to_fabric(aperture.phys);
    const std::uint32_t low = at ? at->v : 0u;
    const auto end = static_cast<std::uint32_t>(low + aperture.len);
    const bool valid = at.has_value()
        && size > 0
        && size <= (128u * 1024u * 1024u)
        && base >= low
        && base < end
        && (base + size) < end;
    ss_ = valid ? std::optional<SaveStateDecl>(SaveStateDecl{hal::FabricAddr{base}, size})
                : std::nullopt;
    caps_.push_back({Capability::Kind::SaveState, std::string(f1.substr(pos, p - pos))});
    return next;
}

std::size_t ConfStrCaps::try_uart(std::string_view f1, std::size_t pos) {
    if (!starts_with_icase(f1.substr(pos), "UART"))
        return pos;
    const std::size_t p = scan_speed_list(f1, pos + 4);
    caps_.push_back({Capability::Kind::Uart, std::string(f1.substr(pos, p - pos))});
    return p;
}

std::size_t ConfStrCaps::try_midi(std::string_view f1, std::size_t pos) {
    if (!starts_with_icase(f1.substr(pos), "MIDI"))
        return pos;
    const std::size_t p = scan_speed_list(f1, pos + 4);
    caps_.push_back({Capability::Kind::Midi, std::string(f1.substr(pos, p - pos))});
    return p;
}

Ex<std::size_t> ConfStrCaps::try_irq(std::string_view f1, std::size_t pos) {
    if (!f1.substr(pos).starts_with("IRQ:"))
        return pos;
    const std::size_t comma = f1.find(',', pos);
    const std::size_t seg_end = comma == std::string_view::npos ? f1.size() : comma;
    const std::string_view body = f1.substr(pos + 4, seg_end - (pos + 4));

    const std::size_t eq = body.find('=');
    if (eq == std::string_view::npos) {
        return std::unexpected(Error{Errc::bad_confstr, ERR_SITE(), 1});
    }
    const auto klass = cause_from(body.substr(0, eq));
    std::string_view rest = body.substr(eq + 1);
    std::string_view line_s = rest;
    std::uint32_t cause_base = 0;
    if (const std::size_t colon = rest.find(':'); colon != std::string_view::npos) {
        line_s = rest.substr(0, colon);
        const auto parsed_base = parse_num(rest.substr(colon + 1), 16);
        if (!parsed_base) {
            return std::unexpected(Error{Errc::bad_confstr, ERR_SITE(), 2});
        }
        cause_base = *parsed_base;
    }

    const auto line = parse_num(line_s, 10);
    if (!klass || !line || *line < kIrqPoolFirstLine || *line > kIrqOrdinalMax) {
        return std::unexpected(Error{Errc::bad_confstr, ERR_SITE(), 3});
    }
    irq_.push_back(
        IrqBinding{
            *klass,
            static_cast<os::UioLine>(static_cast<std::uint8_t>(*line - kIrqPoolFirstLine)),
            hal::LwOffset{cause_base}
        }
    );
    caps_.push_back({Capability::Kind::Irq, std::string(f1.substr(pos, seg_end - pos))});
    return seg_end;
}

Ex<std::size_t> ConfStrCaps::try_axi(std::string_view f1, std::size_t pos) {
    if (!f1.substr(pos).starts_with("AXI:"))
        return pos;
    const std::size_t comma = f1.find(',', pos);
    const std::size_t seg_end = comma == std::string_view::npos ? f1.size() : comma;
    const std::string_view body = f1.substr(pos + 4, seg_end - (pos + 4));

    const auto parts = split(body, ':');

    if (parts.size() < 2 || parts.size() > 3) {
        return std::unexpected(Error{Errc::bad_confstr, ERR_SITE(), 4});
    }
    const auto addr = parse_num(parts[0], 16);
    const auto size = parse_num(parts[1], 16);
    if (!addr || !size) {
        return std::unexpected(Error{Errc::bad_confstr, ERR_SITE(), 4});
    }

    if (parts.size() == 2) {

        axi_unkinded_.push_back(AxiUnkindedDecl{AxiRawBase{*addr}, *size});
    } else if (parts[2] == "LW") {
        axi_lw_.push_back(AxiLwDecl{hal::LwOffset{*addr}, *size});
    } else if (parts[2] == "DDR") {
        axi_ddr_.push_back(AxiDdrDecl{hal::FabricAddr{*addr}, *size});
    } else {

        ++axi_unknown_kinds_;
    }
    caps_.push_back({Capability::Kind::Axi, std::string(f1.substr(pos, seg_end - pos))});
    return seg_end;
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
