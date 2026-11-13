// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/bt_device_path.h"

#include <string_view>

#include "infra/decimal_text.h"

namespace mister::os {
namespace {

constexpr std::string_view kPrefix{"/org/bluez/hci"};
constexpr std::string_view kDev{"/dev_"};
constexpr char kZero = '0';
constexpr char kNine = '9';
constexpr char kPathSep = '_';

[[nodiscard]] Error invalid() noexcept {
    return Error{Errc::bad_format, ERR_SITE(), 0};
}

}

Ex<BtDevicePath> BtDevicePath::parse(std::string_view path) noexcept {
    constexpr std::uint32_t kRadix = 10;
    if (!path.starts_with(kPrefix))
        return std::unexpected(invalid());
    const std::string_view rest = path.substr(kPrefix.size());
    std::size_t digits = 0;
    std::uint32_t adapter = 0;
    while (digits < rest.size() && rest[digits] >= kZero && rest[digits] <= kNine) {
        if (digits == kMaxAdapterDigits)
            return std::unexpected(invalid());
        adapter = adapter * kRadix + static_cast<std::uint32_t>(rest[digits] - kZero);
        ++digits;
    }
    if (digits == 0 || adapter > kMaxAdapter)
        return std::unexpected(invalid());
    const std::string_view after = rest.substr(digits);
    if (!after.starts_with(kDev))
        return std::unexpected(invalid());
    const auto addr = BtAddr::parse(after.substr(kDev.size()), kPathSep);
    if (!addr)
        return std::unexpected(addr.error());
    BtDevicePath out;
    out.adapter_ = static_cast<std::uint8_t>(adapter);
    out.addr_ = *addr;
    return out;
}

BtDevicePath::Text BtDevicePath::format() const noexcept {
    Text out;
    std::size_t at = 0;
    for (const char c : kPrefix)
        out.text_[at++] = c;
    const infra::DecimalText digits{adapter_};
    for (const char c : digits.view())
        out.text_[at++] = c;
    for (const char c : kDev)
        out.text_[at++] = c;
    const BtAddr::Text addr = addr_.format(kPathSep);
    for (const char c : addr.view())
        out.text_[at++] = c;
    out.text_[at] = '\0';
    out.length_ = at;
    return out;
}

}
