// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <tuple>

#include "infra/counter.h"
#include "infra/error.h"
#include "infra/inbox.h"
#include "infra/message_sum.h"
#include "infra/seat.h"

namespace mister::hal {
class ISpiTransport;
}
namespace mister::app {

enum class UartMode : std::uint8_t {
    None = 0,
    Ppp = 1,
    Console = 2,
    Midi = 3,
    Modem = 4,
    Udp = 5,
    Sni = 6,
};

enum class MidiLinkMode : std::uint8_t {
    FSynth = 0,
    Munt = 1,
    UsbMidi = 2,
    Udp = 3,
    Tcp = 4,
    UdpAlt = 5,
    UsbSer = 6,
};

enum class BaudSpace : std::uint8_t {
    Uart,
    Midi,
    Mlink,
};

inline constexpr std::size_t kBaudTableSize = 13;
inline constexpr std::size_t kBaudTokenMax = 10;

inline constexpr std::array<std::uint32_t, kBaudTableSize> kMlinkSpeeds{
    110, 300, 600, 1200, 2400, 4800, 9600, 14400, 19200, 31250, 38400, 57600, 115200,
};

inline constexpr std::array<const char*, 6> kUartSentinels{
    "/tmp/uartmode1", "/tmp/uartmode2", "/tmp/uartmode3",
    "/tmp/uartmode4", "/tmp/uartmode5", "/tmp/uartmode6",
};
inline constexpr std::array<const char*, 7> kMidiLinkSentinels{
    "/tmp/ML_FSYNTH", "/tmp/ML_MUNT",    "/tmp/ML_USBMIDI", "/tmp/ML_UDP",
    "/tmp/ML_TCP",    "/tmp/ML_UDP_ALT", "/tmp/ML_USBSER",
};

inline constexpr std::array<const char*, 2> kMidiLinkRetiredSentinels{
    "/tmp/ML_TCP_ALT",
    "/tmp/ML_SERMIDI",
};

inline constexpr std::array<const char*, 9> kMidiLinkRemovalOrder{
    "/tmp/ML_FSYNTH",  "/tmp/ML_MUNT",    "/tmp/ML_UDP",    "/tmp/ML_USBMIDI", "/tmp/ML_UDP_ALT",
    "/tmp/ML_TCP_ALT", "/tmp/ML_SERMIDI", "/tmp/ML_USBSER", "/tmp/ML_TCP",
};

constexpr std::uint32_t pack_uart_persist(UartMode m, MidiLinkMode ml) noexcept {
    return static_cast<std::uint32_t>(m) | (static_cast<std::uint32_t>(ml) << 8);
}
struct UartPersist {
    UartMode mode;
    MidiLinkMode midilink;
};
constexpr UartPersist unpack_uart_persist(std::uint32_t w) noexcept {
    return UartPersist{static_cast<UartMode>(w & 0xFFu),
                       static_cast<MidiLinkMode>((w >> 8) & 0xFFu)};
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
    if (m == UartMode::Midi) return BaudSpace::Midi;
    if (static_cast<std::uint8_t>(m) > 3) return BaudSpace::Mlink;
    return BaudSpace::Uart;
}

void parse_baud_token(std::string_view token,
                      std::span<std::uint32_t, kBaudTableSize> out) noexcept;

inline constexpr std::size_t kUartNameMax = 64;

inline constexpr std::size_t kUartHandoffDepth = 8;

struct UartHandoff {
    TASTY_SEAT_EXEMPT(component);
    enum class Kind : std::uint8_t { Mode, MidiLink, kCount };
    static constexpr std::size_t kStore = 136;

    struct Mode {
        static constexpr Kind kKind = Kind::Mode;
        std::uint32_t baud = 0;
        char core[kUartNameMax] = {};
        char rbf[kUartNameMax] = {};
        UartMode mode = UartMode::None;
        std::uint8_t pad_[3]{};
    };
    struct MidiLink {
        static constexpr Kind kKind = Kind::MidiLink;
        MidiLinkMode mode = MidiLinkMode::FSynth;
    };

    using Alternatives = std::tuple<Mode, MidiLink>;

    Kind kind = Kind::Mode;
    std::uint8_t pad_[3]{};
    alignas(4) std::array<std::byte, kStore> store{};
};
static_assert(infra::MessageSum<UartHandoff> && infra::alternatives_are_total<UartHandoff>());
static_assert(sizeof(UartHandoff) == 140 && alignof(UartHandoff) == 4);

class UartModeController {
    TASTY_SEAT_MEDIATOR(RT, Ui);

public:
    using Handoffs = xthread::Inbox<UartHandoff, kUartHandoffDepth>;

    void bind_handoffs(Handoffs& h) noexcept { inbox_ = &h; }

    Ex<UartMode> probe_mode() const;
    Ex<MidiLinkMode> probe_midilink() const;

    Ex<void> set_mode(hal::ISpiTransport& link, UartMode m);
    Ex<void> set_midilink(MidiLinkMode ml);
    Ex<void> reset_uart(hal::ISpiTransport& link);

    void bind_identity(std::string_view core, std::string_view rbf) noexcept;

    Ex<void> load_for_core(hal::ISpiTransport& link, std::string_view core_name, bool uart_capable,
                           std::string_view uart_token, std::string_view midi_token,
                           const UartPersistFiles* files = nullptr);

    void latch_usb_ser(bool present) noexcept;
    Ex<void> save_for_core(std::string_view core_name) const;

    std::span<const std::uint32_t> baud_table(BaudSpace s) const;

    Ex<void> validate_baud(UartMode m, std::uint32_t baud);

    std::uint32_t baud_for(UartMode m) const noexcept;

    UartMode mode() const noexcept { return mode_; }
    MidiLinkMode midilink() const noexcept { return midilink_; }
    bool uart_capable() const noexcept { return uart_capable_; }

    void on(const UartHandoff::Mode& a);
    void on(const UartHandoff::MidiLink& a);
    void misrouted(const UartHandoff& h) noexcept;

    struct Stats {
        std::uint32_t mode_writes = 0;
        std::uint32_t script_spawns = 0;
        std::uint32_t script_failures = 0;
        std::uint32_t script_nonzero = 0;
        std::uint32_t loads = 0;
        std::uint32_t handoff_drops = 0;
        std::uint32_t handoff_misrouted = 0;
    };

    Stats stats() const noexcept {
        Stats s = stats_;
        s.script_spawns = script_spawns_.get();
        s.script_failures = script_failures_.get();
        s.script_nonzero = script_nonzero_.get();
        s.handoff_drops = handoff_drops_.get();
        s.handoff_misrouted = handoff_misrouted_.get();
        return s;
    }

private:
    UartMode mode_ = UartMode::None;
    MidiLinkMode midilink_ = MidiLinkMode::FSynth;
    bool uart_capable_ = false;

    bool usb_ser_latched_ = false;
    bool usb_ser_present_ = false;

    std::array<std::uint32_t, 3> idx_{};
    std::array<std::uint32_t, kBaudTableSize> uart_table_{};
    std::array<std::uint32_t, kBaudTableSize> midi_table_{};

    static constexpr std::size_t kNameMax = kUartNameMax;
    char core_name_[kNameMax] = {};
    char rbf_name_[kNameMax] = {};

    void post_(const UartHandoff& h) noexcept;

    Stats stats_{};
    Handoffs* inbox_ = nullptr;
    xthread::Counter script_spawns_{}, script_failures_{}, script_nonzero_{}, handoff_drops_{},
        handoff_misrouted_{};
};

}  // namespace mister::app
