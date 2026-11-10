// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/rtc_image.h"

#include <array>
#include <cerrno>
#include <cstdint>
#include <ctime>
#include <optional>
#include <span>

#include "infra/endian.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::proto {

std::array<std::uint16_t, 4> RtcImage::rtc_words() const noexcept {
    std::array<std::uint16_t, 4> words{};
    infra::load_le_run<std::uint16_t>(std::span<const std::uint8_t>(rtc), words);
    return words;
}

std::array<std::uint16_t, 2> RtcImage::stamp_words() const noexcept {
    std::array<std::uint8_t, sizeof(std::uint32_t)> bytes{};
    infra::store_le<std::uint32_t>(std::span<std::uint8_t, sizeof(std::uint32_t)>(bytes), stamp);
    std::array<std::uint16_t, 2> words{};
    infra::load_le_run<std::uint16_t>(std::span<const std::uint8_t>(bytes), words);
    return words;
}

namespace {

[[nodiscard]] PackedBcd bcd(int v) noexcept {
    return PackedBcd::of(static_cast<std::uint8_t>(v)).value_or(PackedBcd{});
}

}

[[nodiscard]] Ex<RtcImage> rtc_image_at(std::time_t unix_seconds) noexcept {
    TASTY_SEAT(Main);
    const std::time_t t = unix_seconds;
    std::tm local{};
    if (::localtime_r(&t, &local) == nullptr) {
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    }
    std::tm utc{};
    if (::gmtime_r(&t, &utc) == nullptr) {
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    }
    const std::time_t utc_as_local = std::mktime(&utc);
    if (utc_as_local == static_cast<std::time_t>(-1)) {
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    }
    const std::int64_t shifted = static_cast<std::int64_t>(t)
        + (static_cast<std::int64_t>(t) - static_cast<std::int64_t>(utc_as_local));

    RtcImage image;
    image.stamp = static_cast<std::uint32_t>(static_cast<std::uint64_t>(shifted));

    image.put(RtcField::Sec, bcd(local.tm_sec));
    image.put(RtcField::Min, bcd(local.tm_min));
    image.put(RtcField::Hour, bcd(local.tm_hour));
    image.put(RtcField::Mday, bcd(local.tm_mday));
    image.put(RtcField::Mon, bcd(local.tm_mon + 1));

    constexpr int kYearsPerCentury = 100;
    image.put(RtcField::Year, bcd(local.tm_year % kYearsPerCentury));
    image.put_binary(RtcField::Wday, static_cast<std::uint8_t>(local.tm_wday));
    image.put_binary(RtcField::Control, kRtcControl);
    return image;
}

}
