// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/audio_service.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>

#include "infra/ascii.h"
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

FilterWireImage decode_filter_image(std::string_view contents) {
    FilterWireImage image;
    std::size_t pos = 0;
    int line = 0;
    while (pos < contents.size() && line < kFilterLines) {
        const std::size_t start = pos;
        while (pos < contents.size() && contents[pos] != '\n')
            ++pos;
        std::string_view raw_line(contents.data() + start, pos - start);
        if (pos < contents.size())
            ++pos;

        const std::string_view st = strip_leading(raw_line);
        if (st.empty() || st[0] == '#' || st[0] == ';')
            continue;

        if (line == 0) {
            if (!infra::istarts_with(st, "v1"))
                break;
            ++line;
            continue;
        }
        if (line == 1 || line == 3 || line == 4 || line == 5) {
            const auto val = parse_line_int(st);
            if (!val)
                continue;
            const auto u = static_cast<std::uint32_t>(*val);
            append_filter_word(image, static_cast<std::uint16_t>(u));
            if (line == 1) {
                append_filter_word(image, static_cast<std::uint16_t>(u >> 16));
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
            append_filter_word(image, static_cast<std::uint16_t>(u));
            append_filter_word(image, static_cast<std::uint16_t>(u >> 16));
            append_filter_word(image, static_cast<std::uint16_t>(u >> 32));
            ++line;
            continue;
        }

        const auto val = parse_line_double(st);
        if (!val)
            continue;
        const auto coeff = static_cast<std::int32_t>(kCoefScaleQ21 * (*val));
        const auto u = static_cast<std::uint32_t>(coeff);
        append_filter_word(image, static_cast<std::uint16_t>(u));
        append_filter_word(image, static_cast<std::uint16_t>(u >> 16));
        ++line;
    }
    return image;
}

}

AudioService AudioService::create() noexcept {
    return AudioService();
}

void AudioService::set_mute(bool on) noexcept {
    vol_ = vol_.valid().with_mute(on);
}

void AudioService::set_attenuation(proto::Attenuation attenuation) noexcept {

    vol_ = vol_.valid().with_attenuation(attenuation);
}

void AudioService::toggle_mute() noexcept {
    vol_ = vol_.valid().toggled_mute();
}

void AudioService::nudge(proto::VolumeStep step) noexcept {
    vol_ = vol_.valid();
    if (vol_.muted()) {
        vol_ = vol_.without_mute();
    } else if (step == proto::VolumeStep::Quieter) {
        vol_ = vol_.quieter();
    } else {
        vol_ = vol_.louder();
    }
    vol_ = vol_.valid();
}

void AudioService::set_core_volume(int cmd) {
    const VolumeStep step = cmd < 0 ? VolumeStep::Quieter
        : cmd > 0                   ? VolumeStep::Louder
                                    : VolumeStep::Hold;
    corevol_ = corevol_.stepped(step, has_boost_);
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

void AudioService::frame_filter_image(const AfilterParse& parse) {
    if (!has_filter_ || !parse)
        return;

    FilterWireImage framed;
    append_filter_word(framed, core_volume_byte());
    for (const std::uint16_t word : parse->words())
        append_filter_word(framed, word);
    image_ = framed;
}

Ex<void> AudioService::flush() {
    if (has_filter_) {
        image_ = FilterWireImage{};
        append_filter_word(image_, core_volume_byte());

        pending_audvol_ = volume_byte();
    } else {

        vol_ = vol_.capped_under(corevol_);
        pending_audvol_ = vol_.summed_with(corevol_);
    }
    return {};
}

AfilterParser AfilterParser::create() noexcept {
    return AfilterParser();
}

Ex<AfilterParse> AfilterParser::parse(std::string_view name) const {
    if (name.empty()) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }

    std::string path;
    path.reserve(kAfilterDir.size() + 1 + name.size());
    path.append(kAfilterDir);
    path.push_back('/');
    path.append(name);

    const int raw_fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (raw_fd < 0) {

        return AfilterParse{FilterWireImage{}};
    }
    UniqueFd fd{raw_fd};

    auto contents = read_whole_file(fd);
    if (!contents) {

        return std::unexpected(contents.error());
    }
    if (contents->empty()) {

        return AfilterParse{};
    }
    return AfilterParse{decode_filter_image(*contents)};
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
