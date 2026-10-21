// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/config_parser.h"
#include "svc/config.h"
#include "svc/config_field.h"
#include "svc/config_snapshot.h"
#include "svc/option_codec.h"
#include "config_detail.h"

#include <cstdlib>
#include <cstring>
#include <cstddef>
#include <iterator>
#include <optional>
#include <span>

namespace mister::svc {

namespace {

using detail::icmp_n;
using detail::iequals;
using detail::kExtSchema;
using detail::kSchema;

constexpr std::size_t kIniLineSize = 1024;
constexpr char kSectionStart = '[';
constexpr char kSectionEnd = ']';
constexpr char kInclSection = '+';
constexpr char kCommentChar = ';';
constexpr char kLineEnd = '\n';

constexpr bool is_space(char c) {
    return c == ' ' || c == '\t';
}

class MsgBuf {
public:
    void put(std::string_view s) {
        for (const char ch : s) {
            if (len_ + 1 >= kParseErrorChars)
                return;
            buf_[len_++] = ch;
        }
    }

    const char* data() const { return buf_; }

    std::size_t size() const { return len_; }

private:
    char buf_[kParseErrorChars]{};
    std::size_t len_ = 0;
};

void record(ConfigSnapshot& into, ParseError::Kind kind, const MsgBuf& msg) {

    if (into.parse_error_count >= kMaxParseErrors)
        return;
    ParseError& slot = into.parse_errors[into.parse_error_count];
    slot.kind = kind;
    (void)slot.text.assign(std::string_view{msg.data(), msg.size()});
    ++into.parse_error_count;
}

void record_unknown(ConfigSnapshot& into, std::string_view key) {
    MsgBuf m;
    m.put(key);
    m.put(": unknown option");
    record(into, ParseError::Kind::UnknownOption, m);
}

void record_value(
    ConfigSnapshot& into,
    ParseError::Kind kind,
    std::string_view name,
    std::string_view value,
    std::string_view what
) {
    MsgBuf m;
    m.put(name);
    m.put(": '");
    m.put(value);
    m.put("' ");
    m.put(what);
    record(into, kind, m);
}

void record_diag(ConfigSnapshot& into, const Option& o, const char* text, ParseDiag diag) {
    switch (diag) {
        case ParseDiag::None:
            break;
        case ParseDiag::NotANumber:
            record_value(into, ParseError::Kind::NotANumber, o.name, text, "not a number");
            break;
        case ParseDiag::OutOfRange:
            record_value(into, ParseError::Kind::OutOfRange, o.name, text, "out of range");
            break;
        case ParseDiag::InvalidFormat:
            record_value(into, ParseError::Kind::InvalidFormat, o.name, text, "invalid format");
            break;
    }
}

void assign_number(const Option& o, const char* text, ConfigField out, ConfigSnapshot& into) {
    const ParsedValue v = codec_for(o.type).parse(o, text);
    record_diag(into, o, text, v.diag);
    out.store(v.bits);
}

struct SectionVerdict {
    SectionMatch kind = SectionMatch::NoMatch;
    bool matched = false;
    bool video_header = false;
};

SectionVerdict classify_section(std::string_view raw, const config_parser::PassNames& names) {
    SectionVerdict v;
    if (raw.empty())
        return v;
    const char lead = raw.front();
    if (lead != kSectionStart && lead != kInclSection)
        return v;

    const std::string_view body = raw.substr(1);
    std::size_t end = body.size();
    std::size_t wc_pos = std::string_view::npos;
    std::size_t eq_pos = std::string_view::npos;
    for (std::size_t i = 0; i < body.size(); ++i) {
        const char ch = body[i];
        if (ch == kSectionEnd) {
            end = i;
            break;
        }
        if (ch == '*')
            wc_pos = i;
        if (ch == '=')
            eq_pos = i;
    }
    const std::string_view name = body.substr(0, end);

    const std::string_view cur = names.core_name;
    const std::string_view orig = names.orig_core_name.empty() ? names.core_name
                                                               : names.orig_core_name;

    if (iequals(name, "MiSTer")
        || (names.is_arcade && iequals(name, "arcade"))
        || (names.arcade_vertical && iequals(name, "arcade_vertical"))) {
        v.kind = SectionMatch::ExactCoreName;
        v.matched = true;
        return v;
    }

    const bool wildcard = (wc_pos != std::string_view::npos);
    const bool hit = wildcard ? (icmp_n(name, orig, wc_pos) == 0 || icmp_n(name, cur, wc_pos) == 0)
                              : (iequals(name, orig) || iequals(name, cur));
    if (hit) {
        v.kind = wildcard ? SectionMatch::PrefixWildcard : SectionMatch::ExactCoreName;
        v.matched = true;
        return v;
    }

    if (eq_pos != std::string_view::npos && icmp_n(name, "video", eq_pos) == 0) {
        v.video_header = true;
        const std::string_view mode = name.substr(eq_pos + 1);
        if (iequals(mode, names.video_qualified)) {

            v.kind = (names.video_qualified == names.video_unqualified)
                ? SectionMatch::VideoUnqualified
                : SectionMatch::VideoQualified;
            v.matched = true;
        }
    }
    return v;
}

class IniLineReader {
public:
    explicit IniLineReader(std::string_view text) : text_(text) { line_.reserve(kIniLineSize); }

    struct Line {
        std::string_view text;
        bool eof = false;
    };

    [[nodiscard]] Line next() {
        line_.clear();
        char c = 0;
        bool ignore = false;
        bool skip = true;
        for (;;) {
            c = (pos_ < text_.size()) ? text_[pos_++] : '\0';
            if (c == '\0')
                break;
            if (!is_space(c))
                skip = false;

            if (line_.size() >= kIniLineSize - 1 || c == kCommentChar)
                ignore = true;
            if (c == kLineEnd)
                break;
            if (config_parser::admits(c) && !ignore && !skip)
                line_.push_back(c);
        }

        while (!line_.empty() && is_space(line_.back()))
            line_.pop_back();
        return Line{line_, c == '\0'};
    }

private:
    std::string_view text_;
    std::size_t pos_ = 0;
    std::string line_;
};

char char_at(std::string_view s, std::size_t i) {
    return i < s.size() ? s[i] : '\0';
}

void parse_var(
    std::string_view line,
    ConfigSnapshot& into,
    bool (&array_append)[kNumIniOptions + kNumExtOptions]
) {

    std::size_t i = 0;
    for (;;) {
        const char ch = char_at(line, i);
        if (ch == '=' || is_space(ch))
            break;

        if (ch == '\0')
            return;
        ++i;
    }
    const std::string_view key{line.data(), i};

    ++i;
    while (char_at(line, i) == '=' || is_space(char_at(line, i)))
        ++i;

    const char* value = line.data() + i;

    constexpr std::size_t kNoRow = kNumIniOptions + kNumExtOptions;
    std::size_t var_id = kNoRow;
    for (std::size_t j = 0; j < std::size(kSchema); ++j) {
        if (iequals(key, kSchema[j].name))
            var_id = j;
    }

    for (std::size_t j = 0; var_id == kNoRow && j < std::size(kExtSchema); ++j) {
        if (iequals(key, kExtSchema[j].name))
            var_id = kNumIniOptions + j;
    }
    if (var_id == kNoRow) {
        record_unknown(into, key);
        return;
    }

    const Option& o = (var_id < kNumIniOptions) ? kSchema[var_id]
                                                : kExtSchema[var_id - kNumIniOptions];
    ConfigField field{into, o};
    const std::size_t vlen = std::strlen(value);
    const std::string_view text{value, vlen};

    switch (codec_for(o.type).shape) {
        case FieldShape::String:
            field.assign_string(text);
            break;
        case FieldShape::StringSlots:

            if (!array_append[var_id]) {
                array_append[var_id] = true;
                field.clear_slots();
            }
            field.fill_first_empty_slot(text);
            break;
        case FieldShape::CountedU32: {

            if (!array_append[var_id]) {
                array_append[var_id] = true;
                field.clear_count();
            }
            if (std::optional<ConfigField> element = field.next_element())
                assign_number(o, value, *element, into);
            break;
        }
        case FieldShape::Scalar:

            assign_number(o, value, field, into);
            break;
        case FieldShape::Inert:
            break;
    }
}

}

bool config_parser::admits(char c) {

    return (c >= 'a' && c <= 'z')
        || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9')
        || c == '['
        || c == ']'
        || c == '('
        || c == ')'
        || c == '-'
        || c == '+'
        || c == '/'
        || c == '='
        || c == '#'
        || c == '$'
        || c == '@'
        || c == '_'
        || c == ','
        || c == '.'
        || c == '!'
        || c == '*'
        || c == ':'
        || c == '~'
        || c == ' '
        || c == '\t';
}

std::int64_t config_parser::clamp(const Option& o, std::int64_t v) {

    return clamp_to_row(o, v);
}

SectionMatch config_parser::match_section(std::string_view header, const PassNames& names) {
    if (header.empty())
        return SectionMatch::Global;
    return classify_section(header, names).kind;
}

config_parser::PassOutcome config_parser::parse(
    std::string_view text,
    const PassNames& names,
    ConfigSnapshot& into
) {

    PassOutcome outcome;

    bool section = false;

    bool array_append[kNumIniOptions + kNumExtOptions] = {};

    IniLineReader reader{text};

    for (;;) {
        const auto [line, eof] = reader.next();
        const char first = line.empty() ? '\0' : line.front();

        if (first == kSectionStart) {

            const SectionVerdict v = classify_section(line, names);
            if (v.video_header)
                outcome.saw_video_section = true;
            if (v.kind == SectionMatch::VideoQualified
                || v.kind == SectionMatch::VideoUnqualified) {
                outcome.used_video_section = true;
            }
            section = v.matched;
            if (section) {
                for (bool& a : array_append)
                    a = false;
            }
        } else if (first == kInclSection && !section) {

            const SectionVerdict v = classify_section(line, names);
            if (v.video_header)
                outcome.saw_video_section = true;
            if (v.kind == SectionMatch::VideoQualified
                || v.kind == SectionMatch::VideoUnqualified) {
                outcome.used_video_section = true;
            }
            section = v.matched;
            if (section) {
                for (bool& a : array_append)
                    a = false;
            }
        } else if (section) {
            parse_var(line, into, array_append);
        }

        if (eof)
            break;
    }
    return outcome;
}

ConfigSnapshot config_parser::parse_two_pass(std::string_view text, const PassNames& names) {
    ConfigSnapshot cfg{};

    const PassOutcome p1 = parse(text, names, cfg);

    if (p1.saw_video_section && !p1.used_video_section) {
        PassNames unqualified = names;
        unqualified.video_qualified = names.video_unqualified;
        parse(text, unqualified, cfg);
        cfg.second_pass_ran = true;
    }
    return cfg;
}

}
