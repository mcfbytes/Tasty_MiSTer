// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/uart_mode.h"
#include "hal/selected.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "app/process_integration.h"
#include "cores/registry.h"
#include "hal/spi_transport.h"
#include "svc/vfs.h"

namespace mister::app {
namespace {

constexpr hal::SpiWord kSetUart{0x3B};

constexpr const char* kUartSpeedFile = "/tmp/UART_SPEED";
constexpr const char* kCoreNameFile = "/tmp/CORENAME";
constexpr const char* kRbfNameFile = "/tmp/RBFNAME";

bool file_exists(const char* path) noexcept {
    struct stat st {};
    return ::stat(path, &st) == 0;
}

void copy_name(char* dst, std::size_t cap, std::string_view src) noexcept {
    const std::size_t n = src.size() < cap - 1 ? src.size() : cap - 1;
    if (n != 0) std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

}  // namespace

UartPersist clamp_uart_modes(UartPersist p) noexcept {

    const auto ml = static_cast<std::uint8_t>(p.midilink);

    if (p.mode == UartMode::Modem && (ml < 4 || ml > 6)) {
        p.midilink = MidiLinkMode::Tcp;
    }

    if (p.mode == UartMode::Midi && ml > 3) {
        p.midilink = MidiLinkMode::FSynth;
    }

    if (p.mode != UartMode::Midi && p.mode != UartMode::Modem) {
        p.midilink = MidiLinkMode::FSynth;
    }
    return p;
}

void parse_baud_token(std::string_view token,
                      std::span<std::uint32_t, kBaudTableSize> out) noexcept {
    for (auto& v : out)
        v = 0;

    std::size_t p = 0;
    const std::size_t n = token.size();

    if (n >= 4) {
        const char* k = token.data();
        auto lower = [](char c) {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        };
        const bool uart =
            lower(k[0]) == 'u' && lower(k[1]) == 'a' && lower(k[2]) == 'r' && lower(k[3]) == 't';
        const bool midi =
            lower(k[0]) == 'm' && lower(k[1]) == 'i' && lower(k[2]) == 'd' && lower(k[3]) == 'i';
        if (uart || midi) p = 4;
    }

    for (std::size_t i = 0; i < kBaudTokenMax && p < n; ++i) {

        char num[24] = {};
        std::size_t k = 0;
        std::size_t q = p;
        while (q < n && (token[q] == ' ' || token[q] == '+' || token[q] == '-')) {
            if (k + 1 < sizeof num) num[k++] = token[q];
            ++q;
        }
        while (q < n && std::isdigit(static_cast<unsigned char>(token[q]))) {
            if (k + 1 < sizeof num) num[k++] = token[q];
            ++q;
        }
        out[i] = static_cast<std::uint32_t>(std::strtoul(num, nullptr, 10));
        p = q;

        if (p < n && token[p] == '(') {
            ++p;

            while (p < n && token[p] != ';' && token[p] != ':' && token[p] != ')' &&
                   token[p] != ',') {
                ++p;
            }
            if (p < n && token[p] == ')') ++p;
        }
        if (p < n && token[p] == ':') ++p;
    }
}

Ex<UartMode> UartModeController::probe_mode() const { return unimplemented(ERR_SITE()); }

Ex<MidiLinkMode> UartModeController::probe_midilink() const { return unimplemented(ERR_SITE()); }

Ex<void> UartModeController::set_mode(hal::ISpiTransport& link, UartMode m) {
    seat_assert<SeatTag::RT>(ERR_SITE(), "UartModeController::set_mode off T-RT");

    const auto raw = static_cast<std::uint8_t>(static_cast<std::uint8_t>(m) & 0x0Fu);
    const auto masked = static_cast<UartMode>(raw);
    const std::uint32_t baud = baud_for(masked);

    {
        hal::Selected cs(link, hal::ChipSelect::Io);
        if (auto r = link.transfer(kSetUart); !r) {
            return std::unexpected(r.error());
        }
        const std::uint16_t wire =
            (raw == 4 || raw == 5) ? std::uint16_t{1} : static_cast<std::uint16_t>(raw);
        if (auto r = link.transfer(hal::SpiWord{wire}); !r) {
            return std::unexpected(r.error());
        }
        if (auto r = link.transfer(hal::SpiWord{static_cast<std::uint16_t>(baud & 0xFFFFu)}); !r) {
            return std::unexpected(r.error());
        }
        if (auto r =
                link.transfer(hal::SpiWord{static_cast<std::uint16_t>((baud >> 16) & 0xFFFFu)});
            !r) {
            return std::unexpected(r.error());
        }
    }
    mode_ = masked;
    ++stats_.mode_writes;

    UartHandoff::Mode a{};
    a.baud = baud;
    copy_name(a.core, kNameMax, core_name_);
    copy_name(a.rbf, kNameMax, rbf_name_);
    a.mode = masked;
    post_(infra::make<UartHandoff>(a));
    return {};
}

Ex<void> UartModeController::set_midilink(MidiLinkMode ml) {
    seat_assert<SeatTag::RT>(ERR_SITE(), "UartModeController::set_midilink off T-RT");

    const bool usb = usb_ser_latched_ ? usb_ser_present_ : file_exists("/dev/ttyUSB0");
    if (ml == MidiLinkMode::UsbSer && !usb) {
        ml = MidiLinkMode::Tcp;
    }

    post_(infra::make<UartHandoff>(UartHandoff::MidiLink{ml}));
    midilink_ = ml;
    return {};
}

void UartModeController::post_(const UartHandoff& h) noexcept {
    if (inbox_ == nullptr || !inbox_->push(h)) handoff_drops_.add(1);
}

void UartModeController::on(const UartHandoff::MidiLink& a) {
    seat_assert<SeatTag::Ui>(ERR_SITE(), "UartModeController::on off T-UI");
    for (const char* p : kMidiLinkRemovalOrder)
        (void)::unlink(p);
    const auto i = static_cast<std::size_t>(a.mode);
    if (i < kMidiLinkSentinels.size()) {
        (void)proc::write_tmp_handoff(kMidiLinkSentinels[i], std::string_view{});
    }
}

void UartModeController::on(const UartHandoff::Mode& a) {
    seat_assert<SeatTag::Ui>(ERR_SITE(), "UartModeController::on off T-UI");
    (void)proc::write_tmp_handoff(kCoreNameFile, a.core);
    (void)proc::write_tmp_handoff(kRbfNameFile, a.rbf);
    char sp[16] = {};
    const int len = std::snprintf(sp, sizeof sp, "%u", a.baud);
    (void)proc::write_tmp_handoff(
        kUartSpeedFile, std::string_view{sp, len < 0 ? 0u : static_cast<std::size_t>(len)});
    script_spawns_.add(1);
    int rc = 0;
    if (auto r = proc::uartmode_script(static_cast<std::uint8_t>(a.mode), &rc); !r) {
        script_failures_.add(1);
    } else if (rc != 0) {
        script_nonzero_.add(1);
    }
}

void UartModeController::misrouted(const UartHandoff&) noexcept { handoff_misrouted_.add(1); }

Ex<void> UartModeController::reset_uart(hal::ISpiTransport&) { return unimplemented(ERR_SITE()); }

void UartModeController::bind_identity(std::string_view core, std::string_view rbf) noexcept {
    copy_name(core_name_, kNameMax, core);
    copy_name(rbf_name_, kNameMax, rbf);
}

void UartModeController::latch_usb_ser(bool present) noexcept {
    usb_ser_latched_ = true;
    usb_ser_present_ = present;
}

Ex<void> UartModeController::load_for_core(hal::ISpiTransport& link, std::string_view core_name,
                                           bool uart_capable, std::string_view uart_token,
                                           std::string_view midi_token,
                                           const UartPersistFiles* files) {

    idx_ = {};
    parse_baud_token(uart_token, std::span<std::uint32_t, kBaudTableSize>(uart_table_));
    parse_baud_token(midi_token, std::span<std::uint32_t, kBaudTableSize>(midi_table_));

    if (!midi_token.empty() && midi_table_[0] == 0) midi_table_[0] = 31250;
    uart_capable_ = uart_capable;
    ++stats_.loads;

    std::uint32_t word = 0;
    if (uart_capable) {
        if (uart_table_[0] == 0) {

            uart_table_[0] = cores::profile_for(core_name).default_uart_baud;

            if (midi_table_[0] == 0) midi_table_[0] = 31250u;
        }

        std::array<std::uint32_t, 3> speeds{};
        if (files != nullptr && files->ready) {
            if (files->have_mode) word = files->mode;
            if (files->have_speeds) {
                speeds[0] = files->speeds[0];
                speeds[1] = files->speeds[1];
                speeds[2] = files->speeds[2];
            }
        }

        (void)validate_baud(UartMode::Ppp, speeds[0]);
        (void)validate_baud(UartMode::Midi, speeds[1]);

        (void)validate_baud(UartMode::Modem, speeds[2] != 0 ? speeds[2] : uart_table_[0]);
    }

    Ex<void> first = {};
    auto keep = [&first](Ex<void> r) {
        if (!r && first) first = std::unexpected(r.error());
    };

    keep(set_mode(link, UartMode::None));

    const UartPersist p = clamp_uart_modes(unpack_uart_persist(word));

    keep(set_midilink(p.midilink));
    keep(set_mode(link, p.mode));
    return first;
}

Ex<void> UartModeController::save_for_core(std::string_view) const {

    return unimplemented(ERR_SITE());
}

std::span<const std::uint32_t> UartModeController::baud_table(BaudSpace s) const {
    if (s == BaudSpace::Mlink) return kMlinkSpeeds;
    const auto& t = (s == BaudSpace::Midi) ? midi_table_ : uart_table_;

    std::size_t n = 0;
    while (n < t.size() && t[n] != 0)
        ++n;
    return std::span<const std::uint32_t>(t.data(), n);
}

Ex<void> UartModeController::validate_baud(UartMode m, std::uint32_t baud) {
    const BaudSpace s = baud_space_for(m);
    const auto bauds = baud_table(s);
    std::size_t idx = 0;
    for (std::size_t i = 0; i < bauds.size(); ++i) {

        if (bauds[i] == baud) {
            idx = i;
            break;
        }
    }
    idx_[static_cast<std::size_t>(s)] = static_cast<std::uint32_t>(idx);
    return {};
}

std::uint32_t UartModeController::baud_for(UartMode m) const noexcept {
    const BaudSpace s = baud_space_for(m);
    const auto bauds = baud_table(s);
    const std::size_t i = idx_[static_cast<std::size_t>(s)];
    return i < bauds.size() ? bauds[i] : 0u;
}

}  // namespace mister::app
