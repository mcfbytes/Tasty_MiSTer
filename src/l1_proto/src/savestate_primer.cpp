// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/savestate_primer.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace mister::proto {

Ex<void> SavePrimer::begin(
    const Env& env,
    const SaveStateEngine::SaveTarget& target,
    const SaveStateDecl& ss
) {
    end();
    load_failures_ = 0;
    const std::string_view subdir = target.arcade ? SaveStateEngine::kArcadeSubdir
                                                  : target.core_name;
    std::string stem = SaveStateEngine::savestate_stem(target.rom_path);
    if (subdir.empty() || stem.empty()) {
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    }

    if (ss.size < SaveStateEngine::kHeaderBytes) {
        return std::unexpected(Error{Errc::bad_confstr, ERR_SITE(), ss.size});
    }
    for (unsigned i = 0; i < SaveStateEngine::kSlots; ++i) {
        auto m = env.map.map(SaveStateEngine::region_for(ss, i));
        if (!m) {
            end();
            return std::unexpected(m.error());
        }
        regions_[i] = std::move(*m);
    }
    subdir_.assign(subdir);
    stem_ = std::move(stem);
    window_ = ss;
    slot_ = 0;
    loading_ = false;
    digit_ = 0;
    off_ = 0;
    return {};
}

void SavePrimer::end() noexcept {
    for (auto& r : regions_)
        r.reset();
    slot_ = SaveStateEngine::kSlots;
    window_ = SaveStateDecl{};
}

Ex<bool> SavePrimer::step(const Env& env) {
    if (!priming())
        return false;
    auto& mem = *regions_[slot_];
    const std::size_t len = static_cast<std::size_t>(window_.size);
    if (!loading_) {
        const std::size_t n = std::min(kStepBytes, len - off_);
        const auto win = mem.view(off_, n);
        std::memset(win.data(), 0, win.size());
        off_ += n;
        if (off_ == len) {
            loading_ = true;
            off_ = 0;
            digit_ = slot_ + 1u;
        }
        return true;
    }
    if (load_step_(env, mem)) {
        mem.store_release(SaveStateEngine::kCounterWord, SaveStateEngine::kSentinel);
        ++slot_;
        loading_ = false;
        off_ = 0;
    }
    return priming();
}

bool SavePrimer::load_step_(const Env& env, hal::FpgaMemory& mem) {
    const std::size_t len = static_cast<std::size_t>(window_.size);
    const std::size_t n = std::min(kStepBytes, len - off_);
    auto r = env.files.read_at(
        SaveStateEngine::slot_file(subdir_, stem_, digit_),
        off_,
        mem.view(off_, n)
    );
    if (!r && r.error().code == Errc::not_found && off_ == 0 && slot_ == 0 && digit_ != 0) {

        digit_ = 0;
        r = env.files.read_at(SaveStateEngine::slot_file(subdir_, stem_, 0u), 0, mem.view(0, n));
    }
    if (!r) {

        if (r.error().code != Errc::not_found || off_ != 0)
            ++load_failures_;
        return true;
    }
    off_ += *r;
    return *r < n || off_ == len;
}

}
