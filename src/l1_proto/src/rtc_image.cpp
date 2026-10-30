// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/rtc_image.h"

#include <cerrno>
#include <ctime>

#include "infra/seat.h"

namespace mister::proto {

[[nodiscard]] Ex<RtcImage> rtc_image_at(std::uint32_t unix_seconds) noexcept {
    TASTY_SEAT(Main);
    const std::time_t t = static_cast<std::time_t>(unix_seconds);
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

    const auto bcd = [](int v) {
        return static_cast<std::uint8_t>((v % 10) | ((v / 10) << 4));
    };
    image.rtc[0] = bcd(local.tm_sec);
    image.rtc[1] = bcd(local.tm_min);
    image.rtc[2] = bcd(local.tm_hour);
    image.rtc[3] = bcd(local.tm_mday);
    image.rtc[4] = bcd(local.tm_mon + 1);
    image.rtc[5] = static_cast<std::uint8_t>(
        (local.tm_year % 10) | (((local.tm_year / 10) % 10) << 4)
    );
    image.rtc[6] = static_cast<std::uint8_t>(local.tm_wday);
    image.rtc[7] = 0x40;
    return image;
}

}
