// SPDX-License-Identifier: GPL-3.0-or-later
#include "report.h"

#include <atomic>
#include <cstdio>

#include "infra/persist.h"

namespace mister::fw {

namespace {
std::atomic<const IReportVoice*> g_report_voice TASTY_PERSIST(proc, fw_report_voice){nullptr};
}

void set_report_voice(const IReportVoice* voice) noexcept {
    g_report_voice.store(voice, std::memory_order_release);
}

void report(const char* what, const Error& e) {
    char buf[192];
    std::snprintf(buf, sizeof buf, "mister: %s failed: Errc=%u site=%u detail=%u", what,
                  static_cast<unsigned>(e.code), static_cast<unsigned>(e.site),
                  static_cast<unsigned>(e.detail));
    if (const IReportVoice* const voice = g_report_voice.load(std::memory_order_acquire)) {
        voice->say(buf);
    } else {
        std::fprintf(stderr, "%s\n", buf);
    }
}

}  // namespace mister::fw
