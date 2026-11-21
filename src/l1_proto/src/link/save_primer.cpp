// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/link/save_primer.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

#include "infra/prelude.h"

namespace mister::proto::link {

Ex<void> SavePrimer::begin(
    const Env& env,
    const SaveTarget& target,
    const SaveStateDecl& ss,
    const hal::FpgaAperture& aperture
) {
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
        auto region = SaveStateEngine::slot_region(aperture, ss, slot);
        if (!region)
            continue;
        auto m = env.map.map(*region);
        if (!m) {
            end();
            return std::unexpected(m.error());
        }
        regions_[slot] = std::move(*m);
    }
    base_ = *name;
    window_ = ss;
    loading_ = false;
    file_slot_.reset();
    off_ = 0;
    for (const StateSlot slot : kStateSlots) {
        if (!regions_[slot])
            continue;
        slot_ = slot;
        break;
    }
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
        const std::size_t n = kStepBytes.min(FileSize{len - off_}).file_word<std::size_t>();
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
        while (slot_ && !regions_[*slot_])
            slot_ = slot_->next();
        loading_ = false;
        off_ = 0;
    }
    return priming();
}

bool SavePrimer::load_step_(const Env& env, hal::FpgaMemory& mem) {
    const std::size_t len = static_cast<std::size_t>(window_.size);
    const std::size_t n = kStepBytes.min(FileSize{len - off_}).file_word<std::size_t>();
    const std::string path = file_slot_ ? base_.path(*file_slot_) : base_.stock_path();
    auto r = env.files.read_at(path, proto::kFileStart + proto::FileSize{off_}, mem.view(off_, n));
    if (!r && r.error().code == Errc::not_found && off_ == 0 && file_slot_ == kStateSlots.front()) {

        file_slot_.reset();
        r = env.files.read_at(base_.stock_path(), proto::kFileStart, mem.view(0, n));
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
