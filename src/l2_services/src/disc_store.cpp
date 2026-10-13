// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/disc_store.h"

namespace mister::svc {

bool DiscStore::mounted_for_test() const noexcept {
    return engine_.has_value() && engine_->mounted();
}

void DiscStore::publish_() noexcept {

    if (engine_.has_value()) {
        geom_.publish(engine_->geometry());
        counters_.publish(engine_->counters());
    } else {
        geom_.publish(DiscGeometry{});
        counters_.publish(DiscCounters{});
    }
}

Ex<std::size_t> DiscStore::read_form(DiscForm form, proto::Lba lba, std::span<std::byte> dst) {
    TASTY_SEAT_BODY(DiscStore);
    if (!engine_.has_value()) return std::unexpected(Error{Errc::mount_failed, ERR_SITE(), 0});
    Ex<std::size_t> r = form == DiscForm::Subcode     ? engine_->read_subcode(lba, dst)
                        : form == DiscForm::UserData  ? engine_->read_user_data(lba, dst)
                        : form == DiscForm::FullFrame ? engine_->read_full_frame(lba, dst)
                                                      : engine_->read_raw_frame(lba, dst);
    if (form != DiscForm::Subcode) {

        if (lba.v != next_lba_) engine_->advise_ahead(lba);
        next_lba_ = lba.v + 1;
    }

    counters_.publish(engine_->counters());
    return r;
}

bool DiscStore::ensure_engine_(infra::OptRef<const CuePolicy> policy) noexcept {

    if (policy.has_value() && (!policy_.has_value() || &*policy_ != &*policy)) {
        engine_.reset();
        policy_ = policy;
    }
    if (!engine_.has_value()) {
        if (!policy_.has_value()) {
            publish_();
            return false;
        }
        auto d = opener_ ? DiscEngine::create(*opener_, *policy_, prefetch_)
                         : DiscEngine::create(*vfs_, *policy_, prefetch_);
        if (!d) {
            publish_();
            return false;
        }
        engine_.emplace(std::move(*d));
        next_lba_ = 0;
    }
    return true;
}

bool DiscStore::mount(infra::OptRef<const CuePolicy> policy, std::string_view path) noexcept {
    TASTY_SEAT_BODY(DiscStore);
    if (!ensure_engine_(policy)) return false;

    if (path.empty()) {
        (void)engine_->unmount();
        publish_();
        return false;
    }
    const bool ok = engine_->mount(path).has_value();
    publish_();
    return ok;
}

bool DiscStore::unmount(infra::OptRef<const CuePolicy> policy) noexcept {
    TASTY_SEAT_BODY(DiscStore);
    if (!ensure_engine_(policy)) return false;
    (void)engine_->unmount();
    publish_();
    return false;
}

void DiscStore::release() noexcept {
    TASTY_SEAT_BODY(DiscStore);
    engine_.reset();
    policy_.reset();
    publish_();
}

}  // namespace mister::svc
