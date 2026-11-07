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
    if (!s.empty() && s.front() == '+')
        s.remove_prefix(1);
    if (s.empty())
        return std::nullopt;
    std::int32_t v = 0;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v, 10);
    if (ec != std::errc{} || ptr == s.data())
        return std::nullopt;
    return v;
}

std::optional<double> parse_line_double(std::string_view s) {
    if (s.empty())
        return std::nullopt;
    double v = 0;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || ptr == s.data())
        return std::nullopt;
    return v;
}

std::string_view strip_leading(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r'))
        ++i;
    return s.substr(i);
}

bool is_v1_marker(std::string_view st) {
    if (st.size() < 2)
        return false;
    auto lower = [](char c) {
        return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    };
    return lower(st[0]) == 'v' && lower(st[1]) == '1';
}

[[nodiscard]] Ex<std::string> read_whole_file(const UniqueFd& fd) {
    std::string out;
    char chunk[4096];
    for (;;) {
        const ssize_t n = ::read(fd.get(), chunk, sizeof chunk);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
        }
        if (n == 0)
            break;
        out.append(chunk, static_cast<std::size_t>(n));
    }
    return out;
}

}

AudioService AudioService::create() noexcept {
    return AudioService();
}

void AudioService::set_volume(proto::VolumeCmd kind, int arg) {
    vol_ = vol_.valid();

    switch (kind) {
        case proto::VolumeCmd::SetMute:
            vol_ = vol_.with_mute(arg != 0);
            break;
        case proto::VolumeCmd::SetAtten:
            vol_ = vol_.with_attenuation(Attenuation{static_cast<std::uint8_t>(arg)});
            break;
        case proto::VolumeCmd::ToggleMute:
            vol_ = vol_.toggled_mute();
            break;
        case proto::VolumeCmd::Relative:
            if (arg == 0) {
                vol_ = vol_.toggled_mute();
            } else if (vol_.muted()) {
                vol_ = vol_.without_mute();
            } else if (arg < 0 && !vol_.quietest()) {
                vol_ = vol_.quieter();
            } else if (arg > 0 && !vol_.loudest()) {
                vol_ = vol_.louder();
            }
            break;
    }

    vol_ = vol_.valid();
    dirty_ = true;
}

void AudioService::set_core_volume(int cmd) {
    const VolumeStep step = cmd < 0 ? VolumeStep::Quieter
        : cmd > 0                   ? VolumeStep::Louder
                                    : VolumeStep::Hold;
    corevol_ = corevol_.stepped(step, has_boost_);
    dirty_ = true;
}

Ex<bool> AudioService::probe_filter() {
    return has_filter_;
}

void AudioService::accept_probe_reply(std::uint8_t raw_reply, bool front_end) noexcept {
    has_filter_ = raw_reply != 0;
    has_boost_ = ((raw_reply & 0x02u) != 0) && !front_end;
    if (!has_boost_) {
        corevol_ = corevol_.attenuation_only();
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
    if (!has_filter_)
        return {};

    if (contents.empty()) {

        return {};
    }

    filter_word_count_ = 0;
    filter_words_[filter_word_count_++] = core_volume_byte();

    const std::string_view& buf = contents;
    std::size_t pos = 0;
    int line = 0;
    while (pos < buf.size() && line < kFilterLines) {
        const std::size_t start = pos;
        while (pos < buf.size() && buf[pos] != '\n')
            ++pos;
        std::string_view raw_line(buf.data() + start, pos - start);
        if (pos < buf.size())
            ++pos;

        const std::string_view st = strip_leading(raw_line);
        if (st.empty() || st[0] == '#' || st[0] == ';')
            continue;

        if (line == 0) {
            if (!is_v1_marker(st))
                break;
            ++line;
            continue;
        }
        if (line == 1 || line == 3 || line == 4 || line == 5) {
            const auto val = parse_line_int(st);
            if (!val)
                continue;
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
            if (!val)
                continue;
            const auto coeff = static_cast<std::int64_t>(kCoefScaleQ39 * (*val));
            const auto u = static_cast<std::uint64_t>(coeff);
            filter_words_[filter_word_count_++] = static_cast<std::uint16_t>(u);
            filter_words_[filter_word_count_++] = static_cast<std::uint16_t>(u >> 16);
            filter_words_[filter_word_count_++] = static_cast<std::uint16_t>(u >> 32);
            ++line;
            continue;
        }

        const auto val = parse_line_double(st);
        if (!val)
            continue;
        const auto coeff = static_cast<std::int32_t>(kCoefScaleQ21 * (*val));
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

        vol_ = vol_.capped_under(corevol_);
        pending_audvol_ = vol_.summed_with(corevol_);
    }
    return {};
}

[[nodiscard]] Ex<void> emit_afilter_volume(proto::ILinkFrame& frame, std::uint8_t vol) {

    const std::uint16_t words[1] = {vol};
    return proto::send_frame(
        frame,
        proto::UioOp::SetAfilter,
        std::span<const std::uint16_t>(words, 1)
    );
}

[[nodiscard]] Ex<void> emit_audvol(proto::ILinkFrame& frame, std::uint8_t vol) {

    const std::uint16_t words[1] = {vol};
    return proto::send_frame(frame, proto::UioOp::AudVol, std::span<const std::uint16_t>(words, 1));
}

}
