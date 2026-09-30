// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/audio_service.h"
#include "hal/selected.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>

#include "infra/unique_fd.h"

namespace mister::svc {

namespace {

constexpr std::string_view kAfilterDir = "filters_audio";

std::optional<std::int32_t> parse_line_int(std::string_view s) {
    if (!s.empty() && s.front() == '+') s.remove_prefix(1);
    if (s.empty()) return std::nullopt;
    std::int32_t v = 0;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v, 10);
    if (ec != std::errc{} || ptr == s.data()) return std::nullopt;
    return v;
}

std::optional<double> parse_line_double(std::string_view s) {
    if (s.empty()) return std::nullopt;
    double v = 0;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || ptr == s.data()) return std::nullopt;
    return v;
}

std::string_view strip_leading(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r'))
        ++i;
    return s.substr(i);
}

bool is_v1_marker(std::string_view st) {
    if (st.size() < 2) return false;
    auto lower = [](char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c); };
    return lower(st[0]) == 'v' && lower(st[1]) == '1';
}

Ex<std::string> read_whole_file(const UniqueFd& fd) {
    std::string out;
    char chunk[4096];
    for (;;) {
        const ssize_t n = ::read(fd.get(), chunk, sizeof chunk);
        if (n < 0) {
            if (errno == EINTR) continue;
            return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
        }
        if (n == 0) break;
        out.append(chunk, static_cast<std::size_t>(n));
    }
    return out;
}

}  // namespace

Ex<AudioService> AudioService::create() { return AudioService(); }

void AudioService::set_volume(proto::VolumeCmd kind, int arg) {
    vol_att_ &= kVolValidMask;

    switch (kind) {
        case proto::VolumeCmd::SetMute:
            vol_att_ =
                static_cast<std::uint8_t>((vol_att_ & kVolAttMask) | (arg != 0 ? kVolMuteBit : 0));
            break;
        case proto::VolumeCmd::SetAtten:
            vol_att_ = static_cast<std::uint8_t>((vol_att_ & kVolMuteBit) |
                                                 (static_cast<unsigned>(arg) & kVolAttMask));
            break;
        case proto::VolumeCmd::ToggleMute:
            vol_att_ ^= kVolMuteBit;
            break;
        case proto::VolumeCmd::Relative:
            if (arg == 0) {
                vol_att_ ^= kVolMuteBit;
            } else if (vol_att_ & kVolMuteBit) {
                vol_att_ &= static_cast<std::uint8_t>(~kVolMuteBit);
            } else if (arg < 0 && attenuation() < kVolAttMask) {
                vol_att_ = static_cast<std::uint8_t>(vol_att_ + 1);
            } else if (arg > 0 && attenuation() > 0) {
                vol_att_ = static_cast<std::uint8_t>(vol_att_ - 1);
            }
            break;
    }

    vol_att_ &= kVolValidMask;
    dirty_ = true;
}

void AudioService::set_core_volume(int cmd) {
    if (has_boost_) {
        unsigned boost = corevol_att_ & kCoreBoostMask;
        corevol_att_ &= kVolAttMask;
        if (cmd < 0) {
            if (boost != 0) {
                corevol_att_ = 0;
                boost -= 0x20;
            } else if (corevol_att_ < kCoreAttMax) {
                corevol_att_ = static_cast<std::uint8_t>(corevol_att_ + 1);
            }
        }
        if (cmd > 0) {
            if (boost != 0 || corevol_att_ == 0) {
                boost += 0x20;
                if (boost > kCoreBoostMax) boost = kCoreBoostMax;
            } else if (corevol_att_ > 0) {
                corevol_att_ = static_cast<std::uint8_t>(corevol_att_ - 1);
            }
        }
        corevol_att_ = static_cast<std::uint8_t>(corevol_att_ | boost);
    } else {
        corevol_att_ &= kVolAttMask;
        if (cmd < 0 && corevol_att_ < kCoreAttMax) {
            corevol_att_ = static_cast<std::uint8_t>(corevol_att_ + 1);
        }
        if (cmd > 0 && corevol_att_ > 0) {
            corevol_att_ = static_cast<std::uint8_t>(corevol_att_ - 1);
        }
    }
    dirty_ = true;
}

Ex<bool> AudioService::probe_filter() { return has_filter_; }

void AudioService::accept_probe_reply(std::uint8_t raw_reply, bool front_end) noexcept {
    has_filter_ = raw_reply != 0;
    has_boost_ = ((raw_reply & 0x02u) != 0) && !front_end;
    if (!has_boost_) {
        corevol_att_ &= kVolAttMask;
    }
}

Ex<void> AudioService::load_filter(std::string_view name) {
    if (name.empty()) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    if (!has_filter_) {

        return {};
    }

    std::string path;
    path.reserve(kAfilterDir.size() + 1 + name.size());
    path.append(kAfilterDir);
    path.push_back('/');
    path.append(name);

    const int raw_fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (raw_fd < 0) {

        filter_words_[0] = core_volume_byte();
        filter_word_count_ = 1;
        return {};
    }
    UniqueFd fd{raw_fd};

    auto contents = read_whole_file(fd);
    if (!contents) {

        return std::unexpected(contents.error());
    }
    return apply_filter(*contents);
}

Ex<void> AudioService::apply_filter(std::string_view contents) {
    if (!has_filter_) return {};

    if (contents.empty()) {

        return {};
    }

    filter_word_count_ = 0;
    filter_words_[filter_word_count_++] = core_volume_byte();

    const std::string_view& buf = contents;
    std::size_t pos = 0;
    int line = 0;
    while (pos < buf.size() && line < 9) {
        const std::size_t start = pos;
        while (pos < buf.size() && buf[pos] != '\n')
            ++pos;
        std::string_view raw_line(buf.data() + start, pos - start);
        if (pos < buf.size()) ++pos;

        const std::string_view st = strip_leading(raw_line);
        if (st.empty() || st[0] == '#' || st[0] == ';') continue;

        if (line == 0) {
            if (!is_v1_marker(st)) break;
            ++line;
            continue;
        }
        if (line == 1 || line == 3 || line == 4 || line == 5) {
            const auto val = parse_line_int(st);
            if (!val) continue;
            const auto u = static_cast<std::uint32_t>(*val);
            filter_words_[filter_word_count_++] = static_cast<std::uint16_t>(u);
            if (line == 1) {
                filter_words_[filter_word_count_++] = static_cast<std::uint16_t>(u >> 16);
            }
            ++line;
            continue;
        }
        if (line == 2) {
            const auto val = parse_line_double(st);
            if (!val) continue;
            const auto coeff = static_cast<std::int64_t>(0x8000000000LL * (*val));
            const auto u = static_cast<std::uint64_t>(coeff);
            filter_words_[filter_word_count_++] = static_cast<std::uint16_t>(u);
            filter_words_[filter_word_count_++] = static_cast<std::uint16_t>(u >> 16);
            filter_words_[filter_word_count_++] = static_cast<std::uint16_t>(u >> 32);
            ++line;
            continue;
        }

        const auto val = parse_line_double(st);
        if (!val) continue;
        const auto coeff = static_cast<std::int32_t>(0x200000 * (*val));
        const auto u = static_cast<std::uint32_t>(coeff);
        filter_words_[filter_word_count_++] = static_cast<std::uint16_t>(u);
        filter_words_[filter_word_count_++] = static_cast<std::uint16_t>(u >> 16);
        ++line;
    }
    return {};
}

Ex<void> AudioService::flush() {
    if (has_filter_) {
        filter_words_[0] = core_volume_byte();
        filter_word_count_ = 1;

        pending_audvol_ = volume_byte();
    } else {

        if ((vol_att_ & kVolMuteBit) == 0 && static_cast<unsigned>(vol_att_) + corevol_att_ > 7u) {
            vol_att_ = static_cast<std::uint8_t>(7 - static_cast<int>(corevol_att_));
        }
        pending_audvol_ = static_cast<std::uint8_t>(vol_att_ + corevol_att_);
    }
    return {};
}

Ex<std::uint16_t> emit_afilter_probe(hal::ISpiTransport& link) {
    hal::Selected cs(link, hal::ChipSelect::Io);

    auto op = link.transfer(hal::SpiWord{kUioSetAfilter});
    if (!op) return std::unexpected(op.error());
    return op->v;
}

namespace {
Ex<void> write_bytes(hal::ISpiTransport& link, std::span<const std::uint8_t> b) {
    for (const std::uint8_t byte : b) {
        auto r = link.transfer(hal::SpiWord{byte});
        if (!r) return std::unexpected(r.error());
    }
    return {};
}
}  // namespace

Ex<void> emit_afilter_volume(hal::ISpiTransport& link, std::uint8_t vol) {
    hal::Selected cs(link, hal::ChipSelect::Io);

    const std::uint8_t b[2] = {kUioSetAfilter, vol};
    return write_bytes(link, std::span<const std::uint8_t>(b, 2));
}

Ex<void> emit_audvol(hal::ISpiTransport& link, std::uint8_t vol) {
    hal::Selected cs(link, hal::ChipSelect::Io);

    const std::uint8_t b[2] = {kUioAudVol, vol};
    return write_bytes(link, std::span<const std::uint8_t>(b, 2));
}

}  // namespace mister::svc
