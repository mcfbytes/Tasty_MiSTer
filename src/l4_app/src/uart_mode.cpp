// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/uart_mode.h"
#include "proto/link_frame.h"
#include "proto/uio_opcodes.h"

namespace mister::app {

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

    const bool usb = rt_.usb_ser;
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

Ex<void> UartModeController::load_for_core(proto::ILinkFrame& frame, const UartBinding& binding) {

    rt_.uart = BaudSpaceState{binding.uart()};
    rt_.midi = BaudSpaceState{binding.midi()};
    rt_.mlink = BaudSpaceState{BaudTable{kMlinkSpeeds}};
    rt_.uart_capable = binding.capable();
    rt_.usb_ser = binding.usb_ser();
    ++rt_.stats.loads;

    if (binding.capable()) {

        validate_baud(UartMode::Ppp, binding.picks().uart);
        validate_baud(UartMode::Midi, binding.picks().midi);
        validate_baud(UartMode::Modem, binding.picks().mlink);
    }

    Ex<void> first = {};
    auto keep = [&first](Ex<void> r) {
        if (!r && first)
            first = std::unexpected(r.error());
    };

    keep(set_mode(frame, UartMode::None));

    set_midilink(binding.persist().midilink);
    keep(set_mode(frame, binding.persist().mode));
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
    while (n < s.table.speed.size() && s.table.speed[n] != cores::BaudRate{})
        ++n;
    return std::span<const cores::BaudRate>(s.table.speed.data(), n);
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
    latch_(space_of_(uart_mode_facts(m).space), baud);
}

cores::BaudRate UartModeController::baud_for(UartMode m) const noexcept {
    return latched_(space_of_(uart_mode_facts(m).space));
}

}
