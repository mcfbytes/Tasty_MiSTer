// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

#include "infra/error.h"
#include "svc/cue_mount.h"
#include "svc/disc_file_access.h"

namespace mister::svc {

class ICueProgram {
public:
    virtual ~ICueProgram() = default;

    [[nodiscard]] Ex<void> mount(std::string_view cue_path, const CueMount& m) const {
        return parse(cue_path, m);
    }

protected:
    ICueProgram() = default;
    ICueProgram(const ICueProgram&) = default;
    ICueProgram& operator=(const ICueProgram&) = default;

private:
    [[nodiscard]] virtual Ex<void> parse(std::string_view cue_path, const CueMount& m) const = 0;
};

}
