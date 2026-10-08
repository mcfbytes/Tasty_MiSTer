// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "infra/error.h"
#include "hal/spi_transport.h"
#include "infra/seat.h"
#include "proto/volume_cmd.h"

namespace mister::svc {

inline constexpr std::uint8_t kUioSetAfilter = 0x39;
inline constexpr std::uint8_t kUioAudVol = 0x26;

inline constexpr std::uint8_t kVolAttMask = 0x07;
inline constexpr std::uint8_t kVolMuteBit = 0x10;
inline constexpr std::uint8_t kVolValidMask = 0x17;
inline constexpr std::uint8_t kCoreBoostMask = 0x60;
inline constexpr std::uint8_t kCoreBoostMax = 0x40;
inline constexpr std::uint8_t kCoreAttMax = 6;

inline constexpr std::size_t kFilterWireWords = 15;

class AudioService {
    TASTY_SEAT_RESIDENT(RT);

public:
    [[nodiscard]] static AudioService create() noexcept;

    void set_volume(proto::VolumeCmd kind, int arg);

    std::uint8_t volume_byte() const noexcept { return vol_att_ & kVolValidMask; }
    bool muted() const noexcept { return (vol_att_ & kVolMuteBit) != 0; }
    std::uint8_t attenuation() const noexcept { return vol_att_ & kVolAttMask; }

    void set_core_volume(int cmd);
    std::uint8_t core_volume_byte() const noexcept { return corevol_att_; }

    Ex<bool> probe_filter();
    bool has_filter() const noexcept { return has_filter_; }

    void accept_probe_reply(std::uint8_t raw_reply, bool front_end) noexcept;

    Ex<void> load_filter(std::string_view name);

    [[nodiscard]] Ex<void> apply_filter(std::string_view contents);

    std::span<const std::uint16_t> filter_wire_words() const noexcept {
        return std::span<const std::uint16_t>(filter_words_.data(), filter_word_count_);
    }

    Ex<void> flush();

    std::uint8_t audvol_wire_byte() const noexcept { return pending_audvol_; }

    bool take_dirty() noexcept {
        const bool d = dirty_;
        dirty_ = false;
        return d;
    }

private:
    AudioService() = default;
    std::uint8_t vol_att_ = 0;
    std::uint8_t corevol_att_ = 0;
    bool has_filter_ = false;
    bool has_boost_ = false;
    bool dirty_ = false;

    std::array<std::uint16_t, kFilterWireWords> filter_words_{};
    std::uint8_t filter_word_count_ = 0;
    std::uint8_t pending_audvol_ = 0;
};

Ex<std::uint16_t> emit_afilter_probe(hal::ISpiTransport& link);

Ex<void> emit_afilter_volume(hal::ISpiTransport& link, std::uint8_t vol);

Ex<void> emit_audvol(hal::ISpiTransport& link, std::uint8_t vol);

}  // namespace mister::svc
