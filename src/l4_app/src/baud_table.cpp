// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/baud_table.h"

#include <cstddef>

#include "cores/stock_scan.h"
#include "infra/ascii.h"

namespace mister::app {

BaudTable BaudTable::parse(std::string_view token) noexcept {
    constexpr std::size_t kKeywordLen = 4;
    constexpr std::size_t kTokenMax = 10;
    BaudTable out{};

    std::size_t p = 0;
    const std::size_t n = token.size();

    if (n >= kKeywordLen) {
        const std::string_view keyword = token.substr(0, kKeywordLen);
        if (infra::ieq(keyword, "uart") || infra::ieq(keyword, "midi"))
            p = kKeywordLen;
    }

    for (std::size_t i = 0; i < kTokenMax && p < n; ++i) {

        const cores::stock_scan::Scan scanned = cores::stock_scan::scan_ul(
            token.substr(p),
            cores::stock_scan::Radix::Dec
        );
        out.speed[i] = cores::BaudRate{scanned.value};
        p += scanned.used;

        if (p < n && token[p] == '(') {
            ++p;

            while (
                p < n && token[p] != ';' && token[p] != ':' && token[p] != ')' && token[p] != ',') {
                ++p;
            }
            if (p < n && token[p] == ')')
                ++p;
        }
        if (p < n && token[p] == ':')
            ++p;
    }
    return out;
}

}
