// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/bk2_text.h"

#include "cores/i_movie_format.h"
#include "cores/movie_codec.h"
#include "cores/movie_refusal.h"
#include "infra/ascii.h"

namespace mister::cores {

bool bk2_starts_log(std::string_view line) noexcept {
    return !line.empty() && line.front() == '|';
}

bool bk2_ends_log(std::string_view line) noexcept {
    return IMovieFormat::trim(line) == "[/Input]";
}

bool bk2_version_ok(std::string_view value) noexcept {
    return value.starts_with("BizHawk v2");
}

std::optional<bool> bk2_boolean(std::string_view v) noexcept {
    if (v == "1" || infra::ieq(v, "true"))
        return true;
    if (v == "0" || infra::ieq(v, "false"))
        return false;
    return std::nullopt;
}

[[nodiscard]] Ex<Bk2Line> bk2_read_line(std::string_view raw) noexcept {
    constexpr std::string_view kLogKeyPrefix = "LogKey:";
    const std::string_view line = IMovieFormat::trim(raw);
    if (line.empty())
        return Bk2Line{};
    const std::size_t token_end = line.find_first_of(" \t");
    const std::string_view first = line.substr(0, token_end);
    const std::string_view rest = token_end == std::string_view::npos
        ? std::string_view{}
        : IMovieFormat::trim(line.substr(token_end + 1));

    if (first == "@Core"
        || first == "@CoreText"
        || first == "@SaveRam"
        || first == "SavestateBinaryBase64Blob"
        || line.starts_with("SavestateBinaryBase64Blob"))
        return std::unexpected(movie_refusal(MovieRefusal::Savestate, ERR_SITE()));
    if (first == "StartsFromSavestate" || first == "StartsFromSaveRam") {
        const auto b = bk2_boolean(rest);
        if (!b)
            return std::unexpected(movie_refusal(MovieRefusal::NotAMovie, ERR_SITE()));
        if (*b)
            return std::unexpected(movie_refusal(MovieRefusal::Savestate, ERR_SITE()));
    }
    if (line.starts_with(kLogKeyPrefix)) {
        Bk2Line out{};
        out.kind = Bk2Line::Kind::LogKey;
        out.value = line.substr(kLogKeyPrefix.size());
        return out;
    }
    if (line.size() > kMovieLineMax) {
        Bk2Line out{};
        out.kind = Bk2Line::Kind::Long;
        return out;
    }
    const std::size_t sp = line.find(' ');
    const std::string_view key = line.substr(0, sp);
    if (key.find('\t') != std::string_view::npos)
        return std::unexpected(movie_refusal(MovieRefusal::NotAMovie, ERR_SITE()));
    Bk2Line out{};
    out.kind = Bk2Line::Kind::Pair;
    out.key = key;
    out.value = sp == std::string_view::npos ? std::string_view{}
                                             : IMovieFormat::trim(line.substr(sp + 1));
    return out;
}

Ex<std::optional<DigestValue>> bk2_game_db_digest(
    std::string_view key,
    std::string_view value,
    bool sha1_seen
) noexcept {
    constexpr std::size_t kHexPerByte = 2;
    if (key != "SHA1" && key != "SHA256" && key != "MD5")
        return std::nullopt;
    if (key != "SHA1" && sha1_seen)
        return std::nullopt;
    const std::size_t md5_width = kHexPerByte * digest_len(DigestKind::Md5);
    const std::size_t crc32_width = kHexPerByte * digest_len(DigestKind::Crc32);
    DigestKind kind = DigestKind::Sha1;
    if (key == "SHA256") {
        kind = DigestKind::Sha256;
    } else if (key == "MD5" || value.size() == md5_width) {
        kind = DigestKind::Md5;
    } else if (value.size() == crc32_width) {
        kind = DigestKind::Crc32;
    }
    const auto d = parse_hex_digest(value, kind);
    if (!d)
        return std::unexpected(movie_refusal(MovieRefusal::Checksum, ERR_SITE()));
    return std::optional<DigestValue>{*d};
}

Ex<std::optional<DigestValue>> bk2_prefixed_digest(
    std::string_view key,
    std::string_view value,
    bool have_digest
) noexcept {
    constexpr std::size_t kHexPerByte = 2;
    if (key != "SHA1" && key != "MD5")
        return std::nullopt;
    if (key == "MD5" && have_digest)
        return std::nullopt;
    if (const std::size_t colon = value.find(':'); colon != std::string_view::npos)
        value.remove_prefix(colon + 1);
    for (const DigestKind k : {DigestKind::Sha1, DigestKind::Md5, DigestKind::Crc32}) {
        if (value.size() != kHexPerByte * digest_len(k))
            continue;
        const auto d = parse_hex_digest(value, k);
        if (!d)
            break;
        return std::optional<DigestValue>{*d};
    }
    return std::unexpected(movie_refusal(MovieRefusal::Checksum, ERR_SITE()));
}

}
