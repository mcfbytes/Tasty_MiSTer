// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/bk2_log.h"

#include "cores/bk2_text.h"
#include "cores/i_movie_format.h"
#include "cores/movie_codec.h"
#include "infra/parse_int.h"

namespace mister::cores {

std::optional<Bk2PadName> bk2_pad_name(std::string_view column) noexcept {
    if (!column.starts_with('P'))
        return std::nullopt;
    column.remove_prefix(1);
    if (column.empty() || column.front() < '1' || column.front() > '9')
        return std::nullopt;
    const auto n = infra::parse_int<unsigned>(column.substr(0, 1));
    if (!n)
        return std::nullopt;
    column.remove_prefix(1);
    if (!column.starts_with(' '))
        return std::nullopt;
    column.remove_prefix(1);
    if (column.empty())
        return std::nullopt;
    return Bk2PadName{proto::PlayerIndex::of(*n - 1), column};
}

[[nodiscard]] Ex<std::string_view> bk2_frame_text(std::string_view line) noexcept {
    while (!line.empty() && line.back() == '\r')
        line.remove_suffix(1);
    if (line.size() > kMovieLineMax || !bk2_starts_log(line))
        return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));
    return line;
}

std::optional<bool> bk2_cell(char ch) noexcept {
    if (ch < '!' || ch > '~' || ch == '|')
        return std::nullopt;
    return ch != '.';
}

[[nodiscard]] Ex<void> bk2_press(MovieFrame& out, Bk2Column c) noexcept {
    switch (c.kind()) {
        case Bk2Column::Kind::Reset:
            out.commands = out.commands.with_reset();
            return {};
        case Bk2Column::Kind::Power:
            out.commands = out.commands.with_power();
            return {};
        case Bk2Column::Kind::Pad:
            if (const auto cell = c.pad_cell()) {
                out = out.with_pad(cell->port, cell->bit.mask());
                return {};
            }
            break;
        case Bk2Column::Kind::Subframe:
            return std::unexpected(movie_refusal(MovieRefusal::Subframe, ERR_SITE()));
        case Bk2Column::Kind::Tray:
            return std::unexpected(movie_refusal(MovieRefusal::Disk, ERR_SITE()));
        case Bk2Column::Kind::Unset:
        case Bk2Column::Kind::ResetDelay:
        case Bk2Column::Kind::DiscIndex:
        case Bk2Column::Kind::GroupEnd:
            break;
    }
    return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));
}

[[nodiscard]] std::optional<std::string_view> bk2_reset_delay_field(
    std::string_view field
) noexcept {
    constexpr std::size_t kResetDelayChars = 12;
    const std::size_t comma = field.find(',');
    if (comma == std::string_view::npos || comma > kResetDelayChars)
        return std::nullopt;
    std::string_view v = field.substr(0, comma);
    while (!v.empty() && v.front() == ' ')
        v.remove_prefix(1);
    if (!infra::parse_int<int>(v))
        return std::nullopt;
    return field.substr(comma + 1);
}

[[nodiscard]] std::optional<std::string_view> bk2_disc_index_field(
    std::string_view field
) noexcept {
    constexpr std::size_t kDiscDigitsMax = 3;
    const std::size_t comma = field.find(',');
    if (comma == std::string_view::npos)
        return std::nullopt;
    const std::string_view disc = IMovieFormat::trim(field.substr(0, comma));
    if (disc.size() > kDiscDigitsMax || !infra::parse_int<std::uint32_t>(disc))
        return std::nullopt;
    return field.substr(comma + 1);
}

[[nodiscard]] Ex<MovieFrame> bk2_decode_grouped(
    std::string_view line,
    const MovieFacts& f
) noexcept {
    const auto text = bk2_frame_text(line);
    if (!text)
        return std::unexpected(text.error());
    std::string_view rest = text->substr(1);
    MovieFrame out{};
    for (const Bk2Column& c : f.columns.view()) {
        if (rest.empty())
            return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));
        if (c.kind() == Bk2Column::Kind::GroupEnd) {
            if (rest.front() != '|')
                return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));
            rest.remove_prefix(1);
            continue;
        }
        if (c.kind() == Bk2Column::Kind::ResetDelay) {
            const auto next = bk2_reset_delay_field(rest);
            if (!next)
                return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));
            rest = *next;
            continue;
        }
        const auto pressed = bk2_cell(rest.front());
        rest.remove_prefix(1);
        if (!pressed)
            return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));
        if (!*pressed)
            continue;
        const auto ok = bk2_press(out, c);
        if (!ok)
            return std::unexpected(ok.error());
    }
    if (!rest.empty())
        return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));
    return out;
}

[[nodiscard]] Ex<MovieFrame> bk2_decode_flat(std::string_view line, const MovieFacts& f) noexcept {
    const auto text = bk2_frame_text(line);
    if (!text)
        return std::unexpected(text.error());
    std::string_view rest = *text;
    MovieFrame out{};
    for (const Bk2Column& c : f.columns.view()) {
        while (!rest.empty() && rest.front() == '|')
            rest.remove_prefix(1);
        if (rest.empty())
            return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));
        if (c.kind() == Bk2Column::Kind::DiscIndex) {
            const auto next = bk2_disc_index_field(rest);
            if (!next)
                return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));
            rest = *next;
            continue;
        }
        const auto pressed = bk2_cell(rest.front());
        rest.remove_prefix(1);
        if (!pressed)
            return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));
        if (!*pressed)
            continue;
        const auto ok = bk2_press(out, c);
        if (!ok)
            return std::unexpected(ok.error());
    }
    for (const char ch : rest)
        if (ch != '|')
            return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));
    return out;
}

}
