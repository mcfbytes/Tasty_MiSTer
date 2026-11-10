// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/uart_mode.h"
#include "proto/link_frame.h"
#include "proto/uio_opcodes.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <string>
#include <vector>

#include "cores/stock_scan.h"
#include "infra/ascii.h"
#include "svc/vfs.h"

namespace mister::app {
namespace {

using cores::stock_scan::Radix;
using cores::stock_scan::scan_ul;

bool file_exists(const char* path) noexcept {
    struct stat st{};
    return ::stat(path, &st) == 0;
}

}

UartPersist clamp_uart_modes(UartPersist p) noexcept {

    if (p.mode == UartMode::Modem && !is_network(p.midilink))
        p.midilink = MidiLinkMode::Tcp;

    if (p.mode == UartMode::Midi && !is_local(p.midilink))
        p.midilink = MidiLinkMode::FSynth;

    if (p.mode != UartMode::Midi && p.mode != UartMode::Modem)
        p.midilink = MidiLinkMode::FSynth;
    return p;
}

void parse_baud_token(
    std::string_view token,
    std::span<cores::BaudRate, kBaudTableSize> out
) noexcept {
    constexpr std::size_t kKeywordLen = 4;
    for (auto& speed : out)
        speed = {};

    std::size_t p = 0;
    const std::size_t n = token.size();

    if (n >= kKeywordLen) {
        const std::string_view keyword = token.substr(0, kKeywordLen);
        if (infra::ieq(keyword, "uart") || infra::ieq(keyword, "midi"))
            p = kKeywordLen;
    }

    for (std::size_t i = 0; i < kBaudTokenMax && p < n; ++i) {

        const cores::stock_scan::Scan scanned = scan_ul(token.substr(p), Radix::Dec);
        out[i] = cores::BaudRate{scanned.value};
        p += scanned.used;

        if (p < n && token[p] == '(') {
            ++p;

            while (
                p < n && token[p] != ';' && token[p] != ':' && token[p] != ')' && token[p] != ',') {
                ++p;
            }
            if (p < n && token[p] == ')')
                ++p;
        }
        if (p < n && token[p] == ':')
            ++p;
    }
}

Ex<UartMode> UartModeController::probe_mode() const {

    return unimplemented(ERR_SITE());
}

Ex<MidiLinkMode> UartModeController::probe_midilink() const {

    return unimplemented(ERR_SITE());
}

Ex<void> UartModeController::set_mode(proto::ILinkFrame& frame, UartMode m) {
    seat_assert<SeatTag::RT>(ERR_SITE(), "UartModeController::set_mode off T-RT");

    const UartMode masked = masked_mode(m);
    const cores::BaudRate baud = baud_for(masked);
    const BaudWords words = BaudWords::of(baud);

    {
        if (auto r = frame.begin(proto::UioOp::SetUart); !r)
            return r;
        if (auto r = frame.word(wire_mode(masked)); !r)
            return r;
        if (auto r = frame.word(words.lo_); !r)
            return r;
        if (auto r = frame.word(words.hi_); !r)
            return r;
        if (auto r = frame.end(); !r)
            return r;
    }
    rt_.mode = masked;
    ++rt_.stats.mode_writes;

    UartHandoff::Mode a{};
    a.baud = baud;
    (void)a.core.assign(rt_.core_name.view());
    (void)a.rbf.assign(rt_.rbf_name.view());
    a.mode = masked;
    post_(infra::make<UartHandoff>(a));
    return {};
}

void UartModeController::set_midilink(MidiLinkMode ml) {
    seat_assert<SeatTag::RT>(ERR_SITE(), "UartModeController::set_midilink off T-RT");

    const bool usb = rt_.usb_ser_latched ? rt_.usb_ser_present : file_exists("/dev/ttyUSB0");
    if (ml == MidiLinkMode::UsbSer && !usb) {
        ml = MidiLinkMode::Tcp;
    }

    post_(infra::make<UartHandoff>(UartHandoff::MidiLink{ml}));
    rt_.midilink = ml;
}

void UartModeController::post_(const UartHandoff& h) noexcept {
    if (!inbox_ || !inbox_->push(h))
        rt_.handoff_drops.add(1);
}

Ex<void> UartModeController::reset_uart(proto::ILinkFrame&) {

    return unimplemented(ERR_SITE());
}

void UartModeController::bind_identity(CoreNames names) noexcept {
    (void)rt_.core_name.assign(names.effective);
    (void)rt_.rbf_name.assign(names.conf_str);
}

void UartModeController::latch_usb_ser(bool present) noexcept {
    rt_.usb_ser_latched = true;
    rt_.usb_ser_present = present;
}

Ex<void> UartModeController::load_for_core(
    proto::ILinkFrame& frame,
    bool uart_capable,
    UartTokens tokens,
    const UartPersistFiles& files,
    cores::BaudRate default_baud
) {
    constexpr cores::BaudRate kMidiBaud{31250};
    constexpr std::size_t kModemSpeedSlot = 2;

    rt_.uart = BaudSpaceState{};
    rt_.midi = BaudSpaceState{};
    rt_.mlink = BaudSpaceState{kMlinkSpeeds};
    parse_baud_token(tokens.uart, std::span<cores::BaudRate, kBaudTableSize>(rt_.uart.table));
    parse_baud_token(tokens.midi, std::span<cores::BaudRate, kBaudTableSize>(rt_.midi.table));

    if (!tokens.midi.empty() && rt_.midi.empty())
        rt_.midi.table[0] = kMidiBaud;
    rt_.uart_capable = uart_capable;
    ++rt_.stats.loads;

    std::uint32_t word = 0;
    if (uart_capable) {
        if (rt_.uart.empty()) {

            rt_.uart.table[0] = default_baud;

            if (rt_.midi.empty())
                rt_.midi.table[0] = kMidiBaud;
        }

        std::array<cores::BaudRate, 3> speeds{};
        if (files.mode.has_value())
            word = *files.mode;
        if (files.speeds.has_value())
            speeds = *files.speeds;

        validate_baud(UartMode::Ppp, speeds[0]);
        validate_baud(UartMode::Midi, speeds[1]);

        validate_baud(
            UartMode::Modem,
            speeds[kModemSpeedSlot] != cores::BaudRate{} ? speeds[kModemSpeedSlot]
                                                         : rt_.uart.table[0]
        );
    }

    Ex<void> first = {};
    auto keep = [&first](Ex<void> r) {
        if (!r && first)
            first = std::unexpected(r.error());
    };

    keep(set_mode(frame, UartMode::None));

    const UartPersist p = clamp_uart_modes(unpack_uart_persist(word));

    set_midilink(p.midilink);
    keep(set_mode(frame, p.mode));
    return first;
}

Ex<void> UartModeController::save_for_core(std::string_view) const {

    return unimplemented(ERR_SITE());
}

UartModeController::BaudSpaceState& UartModeController::space_of_(BaudSpace s) noexcept {
    switch (s) {
        case BaudSpace::Uart:
            return rt_.uart;
        case BaudSpace::Midi:
            return rt_.midi;
        case BaudSpace::Mlink:
            return rt_.mlink;
    }
    return rt_.uart;
}

const UartModeController::BaudSpaceState& UartModeController::space_of_(
    BaudSpace s
) const noexcept {
    switch (s) {
        case BaudSpace::Uart:
            return rt_.uart;
        case BaudSpace::Midi:
            return rt_.midi;
        case BaudSpace::Mlink:
            return rt_.mlink;
    }
    return rt_.uart;
}

std::span<const cores::BaudRate> UartModeController::offered_(
    const BaudSpaceState& s
) const noexcept {

    std::size_t n = 0;
    while (n < s.table.size() && s.table[n] != cores::BaudRate{})
        ++n;
    return std::span<const cores::BaudRate>(s.table.data(), n);
}

cores::BaudRate UartModeController::latched_(const BaudSpaceState& s) const noexcept {
    return s.latched.chosen(offered_(s));
}

void UartModeController::latch_(BaudSpaceState& s, cores::BaudRate baud) const noexcept {
    const auto speeds = offered_(s);
    std::size_t at = 0;
    for (std::size_t i = 0; i < speeds.size(); ++i) {

        if (speeds[i] == baud) {
            at = i;
            break;
        }
    }
    s.latched = BaudSpaceState::Slot::of(at);
}

std::span<const cores::BaudRate> UartModeController::baud_table(BaudSpace s) const {
    return offered_(space_of_(s));
}

void UartModeController::validate_baud(UartMode m, cores::BaudRate baud) {
    latch_(space_of_(baud_space_for(m)), baud);
}

cores::BaudRate UartModeController::baud_for(UartMode m) const noexcept {
    return latched_(space_of_(baud_space_for(m)));
}

}
