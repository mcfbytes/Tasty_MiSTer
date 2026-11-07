// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <span>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "hal/video_out_decl.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "os/types.h"

namespace mister::svc {
struct Modeline;
}

namespace mister::svc::adv7513 {

class II2cAdapter;
struct ProbeReport;
struct InitOptions;
struct RegWrite;

inline constexpr bool kAdvModeWrite = true;

inline constexpr os::I2cReg kRegPacketEnable{0x40};
inline constexpr std::uint8_t kPacketMaskSpd = 0x40;
inline constexpr os::I2cReg kPacketOffsetSpd{0x00};
inline constexpr std::size_t kSpdPacketBytes = 31;

struct SpdPacket {
    std::array<std::uint8_t, kSpdPacketBytes> b{};
    friend bool operator==(const SpdPacket&, const SpdPacket&) = default;
};

SpdPacket build_standard_spd(std::string_view core_name);

class Adv7513Bus {
    TASTY_SEAT_RESIDENT(Ui);

public:
    Adv7513Bus(const Adv7513Bus&) = delete;
    Adv7513Bus& operator=(const Adv7513Bus&) = delete;
    Adv7513Bus(Adv7513Bus&&) noexcept = default;

    Adv7513Bus& operator=(Adv7513Bus&&) = delete;

    [[nodiscard]] static Ex<Adv7513Bus> attach(
        II2cAdapter& a,
        const hal::VideoOutDecl& board,
        ProbeReport& out
    );

    [[nodiscard]] Ex<void> configure(const InitOptions& o);

    [[nodiscard]] Ex<void> configure_audio(const InitOptions& o);

    [[nodiscard]] Ex<void> tmds_power(bool on);

    [[nodiscard]] Ex<std::uint8_t> read_status();

    [[nodiscard]] Ex<void> arm_interrupts(bool on);

    [[nodiscard]] Ex<void> set_mode(const Modeline& m, bool direct_video_menu);

    [[nodiscard]] Ex<void> set_packet(
        std::uint8_t mask,
        os::I2cReg first_reg,
        std::optional<std::span<const std::uint8_t>> payload
    );

    [[nodiscard]] Ex<void> spd_config(const SpdPacket& p);
    [[nodiscard]] Ex<void> spd_disable();

    struct Verify {
        std::uint8_t r41 = 0, r17 = 0, r3b = 0, r3c = 0;
        bool matched = false;
    };

    [[nodiscard]] Ex<Verify> verify(const Modeline& m, bool direct_video_menu);

    unsigned bus() const noexcept { return bus_; }

    std::uint32_t writes() const noexcept { return writes_; }

    std::uint32_t errors() const noexcept { return errors_; }

    os::I2cReg fail_reg() const noexcept { return fail_reg_; }

    os::I2cReg spd_fail_reg() const noexcept { return spd_fail_reg_; }

    bool mode_cached() const noexcept { return last_vic_ != 0xFFu; }

private:
    Adv7513Bus(II2cAdapter& a, unsigned bus) noexcept : a_(a), bus_(bus) {}

    [[nodiscard]] Ex<void> write_rows(std::span<const RegWrite> rows);

    II2cAdapter& a_;
    unsigned bus_ = 0;

    std::uint8_t last_sync_invert_ = 0xFF;
    std::uint8_t last_pr_flags_ = 0xFF;
    std::uint8_t last_vic_ = 0xFF;

    std::uint32_t writes_ = 0;
    std::uint32_t errors_ = 0;
    os::I2cReg fail_reg_{};
    os::I2cReg spd_fail_reg_{};
};

}
