// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "app/uart_handoff.h"
#include "app/uart_modes.h"
#include "infra/counter.h"
#include "infra/error.h"
#include "infra/inbox.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"

namespace mister::proto {
class ILinkFrame;
}

namespace mister::app {

enum class BaudSpace : std::uint8_t {
    Uart,
    Midi,
    Mlink,
};

inline constexpr std::size_t kBaudTableSize = 13;
inline constexpr std::size_t kBaudTokenMax = 10;

inline constexpr std::array<std::uint32_t, kBaudTableSize> kMlinkSpeeds{
    110,
    300,
    600,
    1200,
    2400,
    4800,
    9600,
    14400,
    19200,
    31250,
    38400,
    57600,
    115200,
};

inline constexpr std::array<const char*, 6> kUartSentinels{
    "/tmp/uartmode1",
    "/tmp/uartmode2",
    "/tmp/uartmode3",
    "/tmp/uartmode4",
    "/tmp/uartmode5",
    "/tmp/uartmode6",
};
inline constexpr std::array<const char*, 7> kMidiLinkSentinels{
    "/tmp/ML_FSYNTH",
    "/tmp/ML_MUNT",
    "/tmp/ML_USBMIDI",
    "/tmp/ML_UDP",
    "/tmp/ML_TCP",
    "/tmp/ML_UDP_ALT",
    "/tmp/ML_USBSER",
};

inline constexpr std::array<const char*, 2> kMidiLinkRetiredSentinels{
    "/tmp/ML_TCP_ALT",
    "/tmp/ML_SERMIDI",
};

inline constexpr std::array<const char*, 9> kMidiLinkRemovalOrder{
    "/tmp/ML_FSYNTH",
    "/tmp/ML_MUNT",
    "/tmp/ML_UDP",
    "/tmp/ML_USBMIDI",
    "/tmp/ML_UDP_ALT",
    "/tmp/ML_TCP_ALT",
    "/tmp/ML_SERMIDI",
    "/tmp/ML_USBSER",
    "/tmp/ML_TCP",
};

constexpr std::uint32_t pack_uart_persist(UartMode m, MidiLinkMode ml) noexcept {
    return static_cast<std::uint32_t>(m) | (static_cast<std::uint32_t>(ml) << 8);
}

struct UartPersist {
    UartMode mode;
    MidiLinkMode midilink;
};

constexpr UartPersist unpack_uart_persist(std::uint32_t w) noexcept {
    return UartPersist{
        static_cast<UartMode>(w & 0xFFu),
        static_cast<MidiLinkMode>((w >> 8) & 0xFFu)
    };
}

struct UartPersistFiles {
    bool ready = false;
    bool have_mode = false;
    bool have_speeds = false;
    std::uint32_t mode = 0;
    std::uint32_t speeds[3] = {};
};

[[nodiscard]] UartPersist clamp_uart_modes(UartPersist p) noexcept;

constexpr BaudSpace baud_space_for(UartMode m) noexcept {
    if (m == UartMode::Midi)
        return BaudSpace::Midi;
    if (static_cast<std::uint8_t>(m) > 3)
        return BaudSpace::Mlink;
    return BaudSpace::Uart;
}

void parse_baud_token(
    std::string_view token,
    std::span<std::uint32_t, kBaudTableSize> out
) noexcept;

class UartModeController {
    TASTY_SEAT_RESIDENT(RT);

public:
    using Handoffs = xthread::Inbox<UartHandoff, kUartHandoffDepth>;

    explicit UartModeController(infra::OptRef<Handoffs> inbox = {}) noexcept : inbox_(inbox) {}

    [[nodiscard]] Ex<UartMode> probe_mode() const;
    [[nodiscard]] Ex<MidiLinkMode> probe_midilink() const;

    [[nodiscard]] Ex<void> set_mode(proto::ILinkFrame& frame, UartMode m);
    [[nodiscard]] Ex<void> set_midilink(MidiLinkMode ml);
    [[nodiscard]] Ex<void> reset_uart(proto::ILinkFrame& frame);

    void bind_identity(std::string_view core, std::string_view rbf) noexcept;

    [[nodiscard]] Ex<void> load_for_core(
        proto::ILinkFrame& frame,
        bool uart_capable,
        std::string_view uart_token,
        std::string_view midi_token,
        const UartPersistFiles* files,
        std::uint32_t default_baud
    );

    void latch_usb_ser(bool present) noexcept;
    [[nodiscard]] Ex<void> save_for_core(std::string_view core_name) const;

    std::span<const std::uint32_t> baud_table(BaudSpace s) const;

    [[nodiscard]] Ex<void> validate_baud(UartMode m, std::uint32_t baud);

    std::uint32_t baud_for(UartMode m) const noexcept;

    UartMode mode() const noexcept { return rt_.mode; }

    MidiLinkMode midilink() const noexcept { return rt_.midilink; }

    bool uart_capable() const noexcept { return rt_.uart_capable; }

    struct Stats {
        std::uint32_t mode_writes = 0;
        std::uint32_t loads = 0;
        std::uint32_t handoff_drops = 0;
    };

    Stats stats() const noexcept {
        Stats s = rt_.stats;
        s.handoff_drops = rt_.handoff_drops.get();
        return s;
    }

private:
    struct Rt {
        UartMode mode = UartMode::None;
        MidiLinkMode midilink = MidiLinkMode::FSynth;
        bool uart_capable = false;

        bool usb_ser_latched = false;
        bool usb_ser_present = false;

        std::array<std::uint32_t, 3> idx{};
        std::array<std::uint32_t, kBaudTableSize> uart_table{};
        std::array<std::uint32_t, kBaudTableSize> midi_table{};
        char core_name[kUartNameMax] = {};
        char rbf_name[kUartNameMax] = {};
        Stats stats{};
        xthread::Counter handoff_drops{};
    };

    void post_(const UartHandoff& h) noexcept;

    const infra::OptRef<Handoffs> inbox_;
    Rt rt_{};
};

}
