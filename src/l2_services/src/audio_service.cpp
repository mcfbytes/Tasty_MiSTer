// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/audio_service.h"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "infra/ascii.h"
#include "svc/afilter_parser.h"
#include "infra/byte_view.h"
#include "svc/read_bounded.h"
#include "svc/vfs.h"

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

void AudioService::step_core_volume(VolumeStep step) {
    corevol_ = corevol_.stepped(step, has_boost_);
}

AfilterEnable AudioService::filter_enable() const noexcept {
    if (!has_filter_)
        return AfilterEnable::Unsupported;
    return filter_enabled_ ? AfilterEnable::On : AfilterEnable::Off;
}

void AudioService::accept_probe_reply(proto::ReplyWord r, bool front_end) noexcept {
    has_filter_ = r.nonzero();
    has_boost_ = r.any(proto::user_io::kBoostCapability) && !front_end;
    if (!has_boost_) {
        corevol_ = corevol_.attenuation_only();
    }
}

CoreVolume AudioService::lead_with_boost() const noexcept {
    return corevol_;
}

CoreVolume AudioService::lead_without_boost() const noexcept {
    return corevol_.attenuation_only();
}

AudvolByte AudioService::audvol_with_filter() const noexcept {
    return AudvolByte{vol_.kept_bits()};
}

AudvolByte AudioService::audvol_without_filter() const noexcept {
    const VolumeByte capped = vol_.capped_under(corevol_);
    return AudvolByte{capped.summed_with(corevol_)};
}

AudvolByte AudioService::settle_after_probe() noexcept {
    return send_volume();
}

AudvolByte AudioService::send_volume() noexcept {
    if (has_filter_)
        return audvol_with_filter();
    const AudvolByte out = audvol_without_filter();
    vol_ = vol_.capped_under(corevol_);
    return out;
}

void AudioService::load_core_volume(CoreVolume raw) noexcept {
    const unsigned field = raw.field_bits();
    unsigned boost = field & CoreVolume::kBoostMask;
    if (boost > CoreVolume::kBoostMax)
        boost = CoreVolume::kBoostMax;
    unsigned atten = field & CoreVolume::kAttenMask;
    if (atten > CoreVolume::kAttenMax)
        atten = CoreVolume::kAttenMax;
    corevol_ = CoreVolume{static_cast<std::uint8_t>(atten | boost)};
}

AfilterParser AfilterParser::create() noexcept {
    return AfilterParser();
}

Ex<AfilterRead> AfilterParser::parse(const Vfs& vfs, std::string_view name) const {
    if (name.empty()) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }

    std::string path;
    path.reserve(kAfilterDir.size() + 1 + name.size());
    path.append(kAfilterDir);
    path.push_back('/');
    path.append(name);

    auto file = vfs.open(path, OpenMode::ReadWhole);
    if (!file) {

        return AfilterRead{};
    }
    AfilterRead out;
    out.opened = true;
    auto bytes = read_bounded(**file, kFilterFileReadCap.file_word<std::uint64_t>());
    if (!bytes) {

        return std::unexpected(bytes.error());
    }
    if (bytes->empty()) {

        return out;
    }
    out.parse = decode_filter_image(infra::as_text(std::span<const std::byte>(*bytes)));
    return out;
}

[[nodiscard]] Ex<void> emit_afilter_stream(
    proto::ILinkFrame& frame,
    CoreVolume lead,
    std::span<const std::uint16_t> words
) {

    if (words.size() + 1 > kFilterWireWords) {
        return std::unexpected(
            Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(words.size())}
        );
    }
    std::array<std::uint16_t, kFilterWireWords> window{};
    window[0] = lead.field_bits();
    for (std::size_t i = 0; i < words.size(); ++i)
        window[i + 1] = words[i];
    return proto::send_frame(
        frame,
        proto::UioOp::SetAfilter,
        std::span<const std::uint16_t>(window.data(), words.size() + 1)
    );
}

[[nodiscard]] Ex<void> emit_afilter_volume(proto::ILinkFrame& frame, CoreVolume vol) {

    const std::uint16_t words[1] = {vol.field_bits()};
    return proto::send_frame(
        frame,
        proto::UioOp::SetAfilter,
        std::span<const std::uint16_t>(words, 1)
    );
}

[[nodiscard]] Ex<void> emit_audvol(proto::ILinkFrame& frame, AudvolByte vol) {

    const std::uint16_t words[1] = {vol.word()};
    return proto::send_frame(frame, proto::UioOp::AudVol, std::span<const std::uint16_t>(words, 1));
}

}
