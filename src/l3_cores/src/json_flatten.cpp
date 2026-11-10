// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/json_flatten.h"

#include <algorithm>
#include <optional>

#include "cores/movie_archive.h"
#include "cores/movie_codec.h"

namespace mister::cores {
namespace {

using CR = MovieRefusal;

constexpr unsigned char kFirstPrintable = 0x20;
constexpr unsigned char kDel = 0x7F;
constexpr std::size_t kEscapeBytes = 2;
constexpr std::size_t kLineFrameBytes = 2;

bool printable(char c) noexcept {
    const auto u = static_cast<unsigned char>(c);
    return u >= kFirstPrintable && u != kDel;
}

bool all_printable(std::string_view s) noexcept {
    return std::ranges::all_of(s, printable);
}

class Flattener {
public:
    Flattener(std::string_view in, std::string_view prefix, JsonCap cap)
        : in_(in),
          path_(prefix),
          cap_(cap) {}

    [[nodiscard]] Ex<std::string> run() {
        ws_();
        if (pos_ < in_.size()) {
            if (!value_(0)) {
                const CR why = over_ ? CR::TooLong : CR::Archive;
                return std::unexpected(movie_refusal(why, ERR_SITE()));
            }
            ws_();
            if (pos_ != in_.size())
                return std::unexpected(movie_refusal(CR::Archive, ERR_SITE()));
        }
        return std::move(out_);
    }

private:
    void ws_() noexcept {
        while (pos_ < in_.size()
               && (in_[pos_] == ' ' || in_[pos_] == '\t' || in_[pos_] == '\n' || in_[pos_] == '\r'))
            ++pos_;
    }

    bool eat_(char c) noexcept {
        ws_();
        if (pos_ < in_.size() && in_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    std::optional<std::string_view> string_() noexcept {
        if (!eat_('"'))
            return std::nullopt;
        const std::size_t start = pos_;
        while (pos_ < in_.size()) {
            const char c = in_[pos_];
            if (static_cast<unsigned char>(c) < kFirstPrintable)
                return std::nullopt;
            if (c == '\\') {
                pos_ += kEscapeBytes;
                continue;
            }
            if (c == '"') {
                const std::string_view s = in_.substr(start, pos_ - start);
                ++pos_;
                return s;
            }
            ++pos_;
        }
        return std::nullopt;
    }

    std::optional<std::string_view> scalar_() noexcept {
        ws_();
        const std::size_t start = pos_;
        for (const std::string_view w : {"true", "false", "null"}) {
            if (in_.substr(pos_).starts_with(w)) {
                pos_ += w.size();
                return in_.substr(start, w.size());
            }
        }
        while (pos_ < in_.size()
               && (std::string_view("+-.0123456789eE").find(in_[pos_]) != std::string_view::npos))
            ++pos_;
        if (pos_ == start)
            return std::nullopt;
        return in_.substr(start, pos_ - start);
    }

    bool emit_(std::string_view v) {
        if (path_.size() + kLineFrameBytes + v.size() > kMovieLineMax)
            return true;
        if (!all_printable(path_) || path_.find(' ') != std::string::npos || !all_printable(v))
            return true;
        out_.append(path_).append(1, ' ').append(v).append(1, '\n');
        over_ = cap_.exceeded_by(out_);
        return !over_;
    }

    bool value_(std::size_t depth) {
        ws_();
        if (pos_ >= in_.size())
            return false;
        const char c = in_[pos_];
        if (c == '{' || c == '[') {
            if (depth == MovieArchiveFormat::kJsonDepth)
                return false;
            const bool obj = c == '{';
            ++pos_;
            if (eat_(obj ? '}' : ']'))
                return true;
            for (std::size_t i = 0;; ++i) {
                const std::size_t keep = path_.size();
                if (obj) {
                    const auto k = string_();
                    if (!k || !eat_(':'))
                        return false;
                    path_.append(1, '.').append(*k);
                } else {
                    path_.append(1, '.').append(std::to_string(i));
                }
                if (!value_(depth + 1))
                    return false;
                path_.resize(keep);
                if (eat_(','))
                    continue;
                return eat_(obj ? '}' : ']');
            }
        }
        const auto v = c == '"' ? string_() : scalar_();
        return v && emit_(*v);
    }

    std::string_view in_;
    std::string path_;
    JsonCap cap_;
    std::string out_;
    std::size_t pos_ = 0;
    bool over_ = false;
};

}

[[nodiscard]] Ex<std::string> flatten_json(
    std::string_view json,
    std::string_view prefix,
    JsonCap cap
) {
    return Flattener(json, prefix, cap).run();
}

}
