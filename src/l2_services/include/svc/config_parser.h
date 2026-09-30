// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "infra/error.h"
#include "svc/config.h"
#include "svc/config_snapshot.h"

namespace mister::svc {

class ConfigParser {
public:
    struct PassNames {
        std::string_view video_qualified;
        std::string_view video_unqualified;
        std::string_view core_name;

        std::string_view orig_core_name;
        bool is_arcade = false;
        bool arcade_vertical = false;
    };

    struct PassOutcome {
        bool saw_video_section = false;
        bool used_video_section = false;
    };

    static Ex<PassOutcome> parse(std::string_view text, const PassNames& names,
                                 ConfigSnapshot& into);

    static Ex<ConfigSnapshot> parse_two_pass(std::string_view text, const PassNames& names);

    static bool admits(char c);

    static std::int64_t clamp(const Option& o, std::int64_t v);

    static SectionMatch match_section(std::string_view header, const PassNames& names);
};

}  // namespace mister::svc
