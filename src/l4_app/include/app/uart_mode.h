// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "app/baud_table.h"
#include "app/baud_words.h"
#include "app/uart_binding.h"
#include "app/uart_handoff.h"
#include "app/uart_mode_facts.h"
#include "app/uart_modes.h"
#include "app/uart_persist.h"
#include "cores/types.h"
#include "infra/counter.h"
#include "infra/error.h"
#include "infra/fixed_str.h"
#include "infra/inbox.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"

namespace mister::proto {
class ILinkFrame;
}

namespace mister::app {

inline constexpr std::array kMlinkSpeeds{
    cores::BaudRate{110},
    cores::BaudRate{300},
    cores::BaudRate{600},
    cores::BaudRate{1200},
    cores::BaudRate{2400},
    cores::BaudRate{4800},
    cores::BaudRate{9600},
    cores::BaudRate{14400},
    cores::BaudRate{19200},
    cores::BaudRate{31250},
    cores::BaudRate{38400},
    cores::BaudRate{57600},
    cores::BaudRate{115200},
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

struct CoreNames {
    std::string_view effective;
    std::string_view conf_str;
};

[[nodiscard]] constexpr UartMode masked_mode(UartMode m) noexcept {
    constexpr std::uint8_t kModeMask = 0x0F;
    return static_cast<UartMode>(static_cast<std::uint8_t>(m) & kModeMask);
}

[[nodiscard]] constexpr std::uint16_t wire_mode(UartMode masked) noexcept {
    return static_cast<std::uint16_t>(uart_mode_facts(masked).wire_as.value_or(masked));
}

class UartModeController {
    TASTY_SEAT_RESIDENT(RT);

public:
    using Handoffs = xthread::Inbox<UartHandoff, kUartHandoffDepth>;

    explicit UartModeController(infra::OptRef<Handoffs> inbox = {}) noexcept : inbox_(inbox) {}

    [[nodiscard]] Ex<UartMode> probe_mode() const;
    [[nodiscard]] Ex<MidiLinkMode> probe_midilink() const;

    [[nodiscard]] Ex<void> set_mode(proto::ILinkFrame& frame, UartMode m);
    void set_midilink(MidiLinkMode ml);
    [[nodiscard]] Ex<void> reset_uart(proto::ILinkFrame& frame);

    void bind_identity(CoreNames names) noexcept;

    [[nodiscard]] Ex<void> load_for_core(proto::ILinkFrame& frame, const UartBinding& binding);
    [[nodiscard]] Ex<void> save_for_core(std::string_view core_name) const;

    std::span<const cores::BaudRate> baud_table(BaudSpace s) const;

    void validate_baud(UartMode m, cores::BaudRate baud);

    cores::BaudRate baud_for(UartMode m) const noexcept;

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
    struct BaudSpaceState {

        struct Slot {
            std::size_t v = 0;

            [[nodiscard]] static constexpr Slot of(std::size_t i) noexcept { return Slot{i}; }

            [[nodiscard]] constexpr cores::BaudRate chosen(
                std::span<const cores::BaudRate> speeds
            ) const noexcept {
                return v < speeds.size() ? speeds[v] : cores::BaudRate{};
            }

            friend constexpr bool operator==(Slot, Slot) = default;
        };

        [[nodiscard]] bool empty() const noexcept { return table.empty(); }

        BaudTable table{};
        Slot latched{};
    };

    struct Rt {
        UartMode mode = UartMode::None;
        MidiLinkMode midilink = MidiLinkMode::FSynth;
        bool uart_capable = false;

        bool usb_ser = false;

        BaudSpaceState uart{};
        BaudSpaceState midi{};
        BaudSpaceState mlink{BaudTable{kMlinkSpeeds}};
        FixedStr<kUartNameMax, StrFit::Clip> core_name{};
        FixedStr<kUartNameMax, StrFit::Clip> rbf_name{};
        Stats stats{};
        xthread::Counter handoff_drops{};
    };

    [[nodiscard]] BaudSpaceState& space_of_(BaudSpace s) noexcept;
    [[nodiscard]] const BaudSpaceState& space_of_(BaudSpace s) const noexcept;

    [[nodiscard]] std::span<const cores::BaudRate> offered_(const BaudSpaceState& s) const noexcept;

    [[nodiscard]] cores::BaudRate latched_(const BaudSpaceState& s) const noexcept;

    void latch_(BaudSpaceState& s, cores::BaudRate baud) const noexcept;

    void post_(const UartHandoff& h) noexcept;

    const infra::OptRef<Handoffs> inbox_;
    Rt rt_{};
};

}
