// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cores/i_movie_format.h"
#include "infra/decimal_text.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/file.h"

namespace mister::svc {
class Vfs;
}

namespace mister::cores {

class MovieRomMatch {
    TASTY_SEAT_RESIDENT(Main);

public:
    static constexpr std::string_view kGamesRel{"media/fat/games/"};

    enum class DigestFail : std::uint8_t { None, Open, Read, Short };

    struct Digested {
        std::optional<DigestValue> value{};
        std::vector<DigestValue> alts{};
        DigestFail fail = DigestFail::None;
    };

    struct NameCount {
        std::uint32_t v = 0;

        [[nodiscard]] constexpr NameCount next() const noexcept { return NameCount{v + 1u}; }

        [[nodiscard]] constexpr bool reached(NameCount cap) const noexcept { return v >= cap.v; }

        [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }

        [[nodiscard]] constexpr std::uint32_t printed() const noexcept { return v; }

        friend constexpr bool operator==(NameCount, NameCount) = default;
    };

    class NameBudget {
        TASTY_SEAT_EXEMPT(component);

    public:
        [[nodiscard]] bool take() noexcept;

        [[nodiscard]] NameCount taken() const noexcept { return used_; }

    private:
        static constexpr NameCount kCap{4000};
        NameCount used_{};
    };

    class ByteBudget {
        TASTY_SEAT_EXEMPT(component);

    public:
        [[nodiscard]] bool take(const IMovieFormat::DigestSource& src) noexcept;

    private:
        static constexpr proto::FileSize kCap{64ull << 20};
        proto::FileSize spent_{};
    };

    struct Search {
        enum class End : std::uint8_t { Found, NoChecksum, NoMatch, Stopped };
        End end = End::NoMatch;
        std::string rom{};
        NameCount names{};
    };

    MovieRomMatch(const svc::Vfs& vfs, const IMovieFormat& format) noexcept
        : vfs_(vfs),
          format_(format) {}

    [[nodiscard]] Digested digest(const IMovieFormat::DigestSource& src, DigestKind kind) const;

    [[nodiscard]] static Digested digest_file(
        svc::IFile& file,
        const IMovieFormat::DigestSource& src,
        DigestKind kind
    );

    [[nodiscard]] bool matches(const Digested& d, const MovieFacts& facts) const;

    [[nodiscard]] Search find_rom(const MovieFacts& facts, std::string_view folder) const;

private:
    static constexpr proto::FileSize kDigestChunk{64u * 1024u};
    const svc::Vfs& vfs_;
    const IMovieFormat& format_;
};

}
