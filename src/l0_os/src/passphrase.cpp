// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/passphrase.h"

#include <cstring>
#include <string.h>

namespace mister::os {
namespace {

constexpr char kPrintableFirst = ' ';
constexpr char kPrintableLast = '~';

[[nodiscard]] Error invalid() noexcept {
    return Error{Errc::bad_format, ERR_SITE(), 0};
}

}

Ex<Passphrase> Passphrase::parse(std::string_view text) noexcept {
    if (text.size() < kMinBytes || text.size() > kMaxBytes)
        return std::unexpected(invalid());
    for (const char c : text) {
        if (c < kPrintableFirst || c > kPrintableLast)
            return std::unexpected(invalid());
    }
    Passphrase out;
    std::memcpy(out.text_.data(), text.data(), text.size());
    out.text_[text.size()] = '\0';
    out.length_ = static_cast<std::uint8_t>(text.size());
    return out;
}

Passphrase::Quoted Passphrase::write_quoted() const noexcept {
    constexpr std::size_t kQuoteBytes = 2;
    Quoted out;
    out.text_[0] = kQuote;
    std::memcpy(out.text_.data() + 1, text_.data(), length_);
    out.text_[length_ + 1] = kQuote;
    out.length_ = length_ + kQuoteBytes;
    out.text_[out.length_] = '\0';
    return out;
}

void Passphrase::scrub() noexcept {
    ::explicit_bzero(text_.data(), text_.size());
    length_ = 0;
}

}
