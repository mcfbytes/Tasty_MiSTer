// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "app/baud_words.h"
#include "app/uart_handoff.h"
#include "app/uart_modes.h"
#include "cores/types.h"
#include "infra/counter.h"
#include "infra/error.h"
#include "infra/fixed_str.h"
#include "infra/inbox.h"
#include "infra/indexed_array.h"
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

inline constexpr std::array<cores::BaudRate, kBaudTableSize> kMlinkSpeeds{
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

inline constexpr std::array<const char*, 6> kUartSentinels{
    "/tmp/uartmode1",
    "/tmp/uartmode2",
    "/tmp/uartmode3",
    "/tmp/uartmode4",
    "/tmp/uartmode5",
    "/tmp/uartmode6",
};

struct MidiLinkSlot {
    std::uint8_t v = 0;

    enum : std::uint8_t { kCount = 7 };

    [[nodiscard]] static constexpr MidiLinkSlot of(MidiLinkMode ml) noexcept {
        return MidiLinkSlot{static_cast<std::uint8_t>(ml)};
    }

    friend constexpr bool operator==(MidiLinkSlot, MidiLinkSlot) = default;
};

[[nodiscard]] constexpr auto midi_link_sentinels() noexcept {
    infra::IndexedArray<const char*, MidiLinkSlot::kCount, MidiLinkSlot> names{};
    names[MidiLinkSlot::of(MidiLinkMode::FSynth)] = "/tmp/ML_FSYNTH";
    names[MidiLinkSlot::of(MidiLinkMode::Munt)] = "/tmp/ML_MUNT";
    names[MidiLinkSlot::of(MidiLinkMode::UsbMidi)] = "/tmp/ML_USBMIDI";
    names[MidiLinkSlot::of(MidiLinkMode::Udp)] = "/tmp/ML_UDP";
    names[MidiLinkSlot::of(MidiLinkMode::Tcp)] = "/tmp/ML_TCP";
    names[MidiLinkSlot::of(MidiLinkMode::UdpAlt)] = "/tmp/ML_UDP_ALT";
    names[MidiLinkSlot::of(MidiLinkMode::UsbSer)] = "/tmp/ML_USBSER";
    return names;
}

inline constexpr auto kMidiLinkSentinels = midi_link_sentinels();

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
    std::optional<std::uint32_t> mode{};
    std::optional<std::array<cores::BaudRate, 3>> speeds{};
};

struct CoreNames {
    std::string_view effective;
    std::string_view conf_str;
};

struct UartTokens {
    std::string_view uart;
    std::string_view midi;
};

enum class MidiLinkGroup : std::uint8_t { Local, Network, Neither };

[[nodiscard]] constexpr MidiLinkGroup midi_link_group(MidiLinkMode ml) noexcept {
    switch (ml) {
        case MidiLinkMode::FSynth:
        case MidiLinkMode::Munt:
        case MidiLinkMode::UsbMidi:
        case MidiLinkMode::Udp:
            return MidiLinkGroup::Local;
        case MidiLinkMode::Tcp:
        case MidiLinkMode::UdpAlt:
        case MidiLinkMode::UsbSer:
            return MidiLinkGroup::Network;
    }
    return MidiLinkGroup::Neither;
}

[[nodiscard]] constexpr bool is_local(MidiLinkMode ml) noexcept {
    return midi_link_group(ml) == MidiLinkGroup::Local;
}

[[nodiscard]] constexpr bool is_network(MidiLinkMode ml) noexcept {
    return midi_link_group(ml) == MidiLinkGroup::Network;
}

[[nodiscard]] UartPersist clamp_uart_modes(UartPersist p) noexcept;

[[nodiscard]] constexpr UartMode masked_mode(UartMode m) noexcept {
    constexpr std::uint8_t kModeMask = 0x0F;
    return static_cast<UartMode>(static_cast<std::uint8_t>(m) & kModeMask);
}

[[nodiscard]] constexpr std::uint16_t wire_mode(UartMode masked) noexcept {
    if (masked == UartMode::Modem || masked == UartMode::Udp)
        return std::uint16_t{1};
    return static_cast<std::uint16_t>(masked);
}

[[nodiscard]] constexpr BaudSpace baud_space_for(UartMode m) noexcept {
    switch (m) {
        case UartMode::None:
        case UartMode::Ppp:
        case UartMode::Console:
            return BaudSpace::Uart;
        case UartMode::Midi:
            return BaudSpace::Midi;
        case UartMode::Modem:
        case UartMode::Udp:
        case UartMode::Sni:
            return BaudSpace::Mlink;
    }

    return BaudSpace::Mlink;
}

void parse_baud_token(
    std::string_view token,
    std::span<cores::BaudRate, kBaudTableSize> out
) noexcept;

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

    [[nodiscard]] Ex<void> load_for_core(
        proto::ILinkFrame& frame,
        bool uart_capable,
        UartTokens tokens,
        const UartPersistFiles& files,
        cores::BaudRate default_baud
    );

    void latch_usb_ser(bool present) noexcept;
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

        [[nodiscard]] bool empty() const noexcept { return table[0] == cores::BaudRate{}; }

        std::array<cores::BaudRate, kBaudTableSize> table{};
        Slot latched{};
    };

    struct Rt {
        UartMode mode = UartMode::None;
        MidiLinkMode midilink = MidiLinkMode::FSynth;
        bool uart_capable = false;

        bool usb_ser_latched = false;
        bool usb_ser_present = false;

        BaudSpaceState uart{};
        BaudSpaceState midi{};
        BaudSpaceState mlink{kMlinkSpeeds};
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
