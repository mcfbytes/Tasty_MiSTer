// SPDX-License-Identifier: GPL-3.0-or-later
#include "config_detail.h"
#include "svc/config.h"
#include "svc/config_snapshot.h"
#include "svc/option_codec.h"

#include <cstddef>
#include <iterator>
#include <span>

namespace mister::svc {
namespace {

using detail::iequals;
using detail::kExtSchema;
using detail::kSchema;

static_assert(std::size(kSchema) == kNumIniOptions, "req: ini_vars[] has exactly 111 entries");
static_assert(
    std::size(kExtSchema) == kNumExtOptions,
    "kNumExtOptions must count the tasty-only rows exactly"
);

consteval std::size_t count_sentinels() {
    std::size_t n = 0;
    for (const Option& o : kSchema) {
        if (o.sentinel_default)
            ++n;
    }
    return n;
}

static_assert(
    count_sentinels() == 3,
    "req: exactly three options have "
    "a compiled-in default outside [min,max]"
);

static_assert(
    sizeof(ConfigSnapshot) <= 0xFFFF,
    "Option::offset is uint16_t; ConfigSnapshot must stay addressable"
);

constexpr std::size_t type_width(OptionType t) {
    return codec_for(t).width;
}

consteval bool rows_fit(std::span<const Option> rows) {
    for (const Option& o : rows) {
        const std::size_t span = (o.length != 0) ? o.length : type_width(o.type);
        if (span == 0)
            return false;
        if (std::size_t{o.offset} + span > sizeof(ConfigSnapshot))
            return false;
        if (o.type == OptionType::Str && static_cast<std::size_t>(o.max) > o.length)
            return false;
        if (o.type == OptionType::StrArr
            && static_cast<std::size_t>(o.min) * static_cast<std::size_t>(o.max) != o.length)
            return false;
    }
    return true;
}

static_assert(rows_fit(kSchema), "a schema row addresses memory outside ConfigSnapshot");
static_assert(rows_fit(kExtSchema), "an ext row addresses memory outside ConfigSnapshot");

consteval bool ext_rows_are_scalar() {
    for (const Option& o : kExtSchema) {
        if (type_width(o.type) == 0)
            return false;
    }
    return true;
}

static_assert(ext_rows_are_scalar(), "an ext row must be a fixed-width scalar type");

consteval bool ext_names_are_new() {
    for (const Option& e : kExtSchema) {
        for (const Option& o : kSchema) {
            if (iequals(e.name, o.name))
                return false;
        }
    }
    return true;
}

static_assert(ext_names_are_new(), "an ext row reuses a classic option name");

}

std::span<const Option> option_schema() {
    return kSchema;
}

std::span<const Option> option_schema_ext() {
    return kExtSchema;
}

}
