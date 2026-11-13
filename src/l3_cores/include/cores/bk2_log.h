// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <concepts>
#include <cstddef>
#include <optional>
#include <string_view>

#include "cores/bk2_column.h"
#include "cores/bk2_columns.h"
#include "cores/movie_facts.h"
#include "cores/movie_frame.h"
#include "cores/movie_refusal.h"
#include "infra/error.h"
#include "proto/types.h"
#include "svc/types.h"

namespace mister::cores {

struct Bk2PadName {
    proto::PlayerIndex port;
    std::string_view name;
};

[[nodiscard]] std::optional<Bk2PadName> bk2_pad_name(std::string_view column) noexcept;

template <class N>
concept Bk2Names = requires(std::string_view name) {
    { N::column_of(name) } -> std::same_as<Ex<Bk2Column>>;
    N::kReservesGroupEnd;
};

template <Bk2Names N>
[[nodiscard]] Ex<MovieFacts> bk2_fold_groups(std::string_view log_key, MovieFacts f) noexcept {
    const std::size_t first = log_key.find('#');
    if (first == std::string_view::npos)
        return std::unexpected(movie_refusal(MovieRefusal::NotAMovie, ERR_SITE()));
    log_key.remove_prefix(first + 1);
    svc::PlayerMask ports{};
    while (true) {
        const std::size_t hash = log_key.find('#');
        std::string_view group = log_key.substr(0, hash);
        while (!group.empty()) {
            const std::size_t bar = group.find('|');
            const std::string_view name = group.substr(0, bar);
            group.remove_prefix(bar == std::string_view::npos ? group.size() : bar + 1);
            if (name.empty())
                continue;
            const auto c = N::column_of(name);
            if (!c)
                return std::unexpected(c.error());
            std::optional<Bk2Columns> next;
            if constexpr (N::kReservesGroupEnd) {
                next = f.columns.append_keeping_group_end(*c);
            } else {
                next = f.columns.append(*c);
            }
            if (!next)
                return std::unexpected(movie_refusal(MovieRefusal::PortType, ERR_SITE()));
            f.columns = *next;
            if (const auto cell = c->pad_cell())
                ports = ports.with(cell->port);
        }
        const auto next = f.columns.append(Bk2Column::group_end());
        if (!next)
            return std::unexpected(movie_refusal(MovieRefusal::PortType, ERR_SITE()));
        f.columns = *next;
        if (hash == std::string_view::npos)
            break;
        log_key.remove_prefix(hash + 1);
    }
    f.ports = ports;
    return f;
}

[[nodiscard]] Ex<std::string_view> bk2_frame_text(std::string_view line) noexcept;

[[nodiscard]] std::optional<bool> bk2_cell(char ch) noexcept;

[[nodiscard]] Ex<void> bk2_press(MovieFrame& out, Bk2Column c) noexcept;

[[nodiscard]] std::optional<std::string_view> bk2_reset_delay_field(
    std::string_view field
) noexcept;

[[nodiscard]] std::optional<std::string_view> bk2_disc_index_field(std::string_view field) noexcept;

[[nodiscard]] Ex<MovieFrame> bk2_decode_grouped(
    std::string_view line,
    const MovieFacts& f
) noexcept;

[[nodiscard]] Ex<MovieFrame> bk2_decode_flat(std::string_view line, const MovieFacts& f) noexcept;

}
