// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/uart_performer.h"

#include <unistd.h>

#include <cstdio>
#include <string_view>

#include "app/process_integration.h"

namespace mister::app {
namespace {

constexpr const char* kUartSpeedFile = "/tmp/UART_SPEED";
constexpr const char* kCoreNameFile = "/tmp/CORENAME";
constexpr const char* kRbfNameFile = "/tmp/RBFNAME";

}  // namespace

void UartPerformer::on(const UartHandoff::MidiLink& a) {
    TASTY_SEAT_BODY(UartPerformer);
    for (const char* p : kMidiLinkRemovalOrder)
        (void)::unlink(p);
    const auto i = static_cast<std::size_t>(a.mode);
    if (i < kMidiLinkSentinels.size()) {
        (void)proc::write_tmp_handoff(kMidiLinkSentinels[i], std::string_view{});
    }
}

void UartPerformer::on(const UartHandoff::Mode& a) {
    TASTY_SEAT_BODY(UartPerformer);
    (void)proc::write_tmp_handoff(kCoreNameFile, a.core);
    (void)proc::write_tmp_handoff(kRbfNameFile, a.rbf);
    char sp[16] = {};
    const int len = std::snprintf(sp, sizeof sp, "%u", a.baud);
    (void)proc::write_tmp_handoff(
        kUartSpeedFile, std::string_view{sp, len < 0 ? 0u : static_cast<std::size_t>(len)});
    script_spawns_.add(1);
    if (const auto r = proc::uartmode_script(static_cast<std::uint8_t>(a.mode)); !r) {
        script_failures_.add(1);
    } else if (*r != 0) {
        script_nonzero_.add(1);
    }
}

void UartPerformer::misrouted(const UartHandoff&) noexcept {
    TASTY_SEAT_BODY(UartPerformer);
    handoff_misrouted_.add(1);
}

}  // namespace mister::app
