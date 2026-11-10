// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/n64_cpak.h"

#include "svc/entropy.h"

namespace mister::cores::n64::cpak {

[[nodiscard]] Ex<Serial> random_serial() {
    Serial out{};
    const auto drawn = svc::read_entropy(std::span(out));
    if (!drawn)
        return std::unexpected(drawn.error());
    return out;
}

}
