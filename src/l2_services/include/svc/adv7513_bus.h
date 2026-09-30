// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <span>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "hal/video_out_decl.h"
#include "infra/error.h"
#include "infra/seat.h"

namespace mister::svc {
struct Modeline;
}

namespace mister::svc::adv7513 {

class II2cAdapter;
struct ProbeReport;
struct InitOptions;
struct RegWrite;

inline constexpr bool kAdvModeWrite = true;

inline constexpr std::uint8_t kRegPacketEnable = 0x40;
inline constexpr std::uint8_t kPacketMaskSpd = 0x40;
inline constexpr std::uint8_t kPacketOffsetSpd = 0x00;

inline constexpr std::uint8_t kPacketUpdateOff = 0x1F;
inline constexpr std::size_t kSpdPacketBytes = 31;

struct SpdPacket {
    std::array<std::uint8_t, kSpdPacketBytes> b{};
    friend bool operator==(const SpdPacket&, const SpdPacket&) = default;
};

SpdPacket build_standard_spd(std::string_view core_name);

constexpr std::uint32_t reg_errno_detail(std::uint8_t reg, std::uint32_t err) noexcept {
    return static_cast<std::uint32_t>(reg) | (err << 8);
}
constexpr std::uint8_t detail_reg(std::uint32_t d) noexcept {
    return static_cast<std::uint8_t>(d & 0xFFu);
}
constexpr std::uint32_t detail_errno(std::uint32_t d) noexcept { return d >> 8; }

class Adv7513Bus {
    TASTY_SEAT_RESIDENT(Ui);

public:
    Adv7513Bus(const Adv7513Bus&) = delete;
    Adv7513Bus& operator=(const Adv7513Bus&) = delete;
    Adv7513Bus(Adv7513Bus&&) noexcept = default;
    Adv7513Bus& operator=(Adv7513Bus&&) noexcept = default;

    static Ex<Adv7513Bus> attach(II2cAdapter& a, const hal::VideoOutDecl& board, ProbeReport& out);

    Ex<void> configure(const InitOptions& o);

    Ex<void> configure_audio(const InitOptions& o);

    Ex<void> tmds_power(bool on);

    Ex<std::uint8_t> read_status();

    Ex<void> arm_interrupts(bool on);

    Ex<void> set_mode(const Modeline& m, bool direct_video_menu);

    Ex<void> set_packet(std::uint8_t mask, std::uint8_t offset, const std::uint8_t* data,
                        std::size_t n);

    Ex<void> spd_config(const SpdPacket& p);
    Ex<void> spd_disable();

    struct Verify {
        std::uint8_t r41 = 0, r17 = 0, r3b = 0, r3c = 0;
        bool matched = false;
    };
    Ex<Verify> verify(const Modeline& m, bool direct_video_menu);

    unsigned bus() const noexcept { return bus_; }
    std::uint32_t writes() const noexcept { return writes_; }
    std::uint32_t errors() const noexcept { return errors_; }

    std::uint8_t fail_reg() const noexcept { return fail_reg_; }

    std::uint8_t spd_fail_reg() const noexcept { return spd_fail_reg_; }
    bool mode_cached() const noexcept { return last_vic_ != 0xFFu; }

private:
    Adv7513Bus() = default;

    Ex<void> write_rows(std::span<const RegWrite> rows);

    II2cAdapter* a_ = nullptr;
    unsigned bus_ = 0;

    std::uint8_t last_sync_invert_ = 0xFF;
    std::uint8_t last_pr_flags_ = 0xFF;
    std::uint8_t last_vic_ = 0xFF;

    std::uint32_t writes_ = 0;
    std::uint32_t errors_ = 0;
    std::uint8_t fail_reg_ = 0;
    std::uint8_t spd_fail_reg_ = 0;
};

}  // namespace mister::svc::adv7513
