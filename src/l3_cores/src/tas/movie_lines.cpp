// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/tas/movie_lines.h"

namespace mister::cores::tas {

void MovieLines::reset() noexcept {
    held_.clear();
}

}
