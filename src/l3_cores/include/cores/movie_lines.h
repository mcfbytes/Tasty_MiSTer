// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "cores/movie_codec.h"
#include "infra/byte_view.h"
#include "infra/error.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/file.h"

namespace mister::cores {

class MovieLines {
    TASTY_SEAT_EXEMPT(component);

public:
    struct Line {
        std::string_view text{};
        bool clipped = false;
    };

    static constexpr proto::FileSize kChunk{4096};

    template <class OnLine>
    [[nodiscard]] bool feed(std::span<const std::byte> bytes, OnLine&& on_line);

    template <class OnLine>
    [[nodiscard]] bool finish(OnLine&& on_line);
    void reset() noexcept;

private:
    FixedStr<kMovieLineMax + 2, StrFit::Clip> held_{};
};

enum class WalkEnd : std::uint8_t { Whole, Stopped };

template <class OnLine>
[[nodiscard]] Ex<WalkEnd> for_each_movie_line(svc::IFile& file, OnLine&& on_line) {
    MovieLines lines;
    std::array<std::byte, MovieLines::kChunk.file_word<std::size_t>()> buf;
    std::uint64_t off = 0;
    for (;;) {
        const auto got = file.read_at(off, buf);
        if (!got)
            return std::unexpected(got.error());
        if (*got == 0)
            break;
        off += *got;
        if (!lines.feed(std::span<const std::byte>(buf.data(), *got), on_line))
            return WalkEnd::Stopped;
    }
    return lines.finish(on_line) ? WalkEnd::Whole : WalkEnd::Stopped;
}

template <class OnLine>
bool MovieLines::feed(std::span<const std::byte> bytes, OnLine&& on_line) {
    std::string_view rest = infra::as_text(bytes);
    while (!rest.empty()) {
        const std::size_t nl = rest.find('\n');
        if (!held_.append(rest.substr(0, nl)))
            return false;
        if (nl == std::string_view::npos)
            return true;
        std::string_view text = held_.view();
        if (!text.empty() && text.back() == '\r')
            text.remove_suffix(1);
        const bool go = on_line(Line{text, held_.clipped()});
        held_.clear();
        if (!go)
            return false;
        rest.remove_prefix(nl + 1);
    }
    return true;
}

template <class OnLine>
bool MovieLines::finish(OnLine&& on_line) {
    if (held_.empty() && !held_.clipped())
        return true;
    std::string_view text = held_.view();
    if (!text.empty() && text.back() == '\r')
        text.remove_suffix(1);
    const bool go = on_line(Line{text, held_.clipped()});
    held_.clear();
    return go;
}

}
