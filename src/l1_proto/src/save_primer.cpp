// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/save_primer.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

namespace mister::proto {

Ex<void> SavePrimer::begin(const Env& env, const SaveTarget& target, const SaveStateDecl& ss) {
    end();
    load_failures_ = 0;
    const std::optional<SaveStateName> name = SaveStateName::of(target);
    if (!name) {
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    }

    if (ss.size < SaveStateEngine::kHeaderBytes) {
        return std::unexpected(Error{Errc::bad_confstr, ERR_SITE(), ss.size});
    }
    for (const StateSlot slot : kStateSlots) {
        auto m = env.map.map(SaveStateEngine::region_for(ss, slot));
        if (!m) {
            end();
            return std::unexpected(m.error());
        }
        regions_[slot] = std::move(*m);
    }
    base_ = *name;
    window_ = ss;
    slot_ = kStateSlots.front();
    loading_ = false;
    file_slot_.reset();
    off_ = 0;
    return {};
}

void SavePrimer::end() noexcept {
    for (auto& r : regions_)
        r.reset();
    slot_.reset();
    file_slot_.reset();
    window_ = SaveStateDecl{};
}

Ex<bool> SavePrimer::step(const Env& env) {
    if (!priming())
        return false;
    auto& mem = *regions_[*slot_];
    const std::size_t len = static_cast<std::size_t>(window_.size);
    if (!loading_) {
        const std::size_t n = std::min(kStepBytes, len - off_);
        const auto win = mem.view(off_, n);
        std::memset(win.data(), 0, win.size());
        off_ += n;
        if (off_ == len) {
            loading_ = true;
            off_ = 0;
            file_slot_ = *slot_;
        }
        return true;
    }
    if (load_step_(env, mem)) {
        mem.store_release(SaveStateEngine::kCounterWord, SaveStateEngine::kSentinel);
        slot_ = slot_->next();
        loading_ = false;
        off_ = 0;
    }
    return priming();
}

bool SavePrimer::load_step_(const Env& env, hal::FpgaMemory& mem) {
    const std::size_t len = static_cast<std::size_t>(window_.size);
    const std::size_t n = std::min(kStepBytes, len - off_);
    const std::string path = file_slot_ ? base_.path(*file_slot_) : base_.stock_path();
    auto r = env.files.read_at(path, off_, mem.view(off_, n));
    if (!r && r.error().code == Errc::not_found && off_ == 0 && file_slot_ == kStateSlots.front()) {

        file_slot_.reset();
        r = env.files.read_at(base_.stock_path(), 0, mem.view(0, n));
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
