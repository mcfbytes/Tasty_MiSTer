// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/disc_store.h"

namespace mister::svc {

Ex<std::span<std::byte>> DiscStore::read_engine_(
    DiscForm form,
    proto::Lba lba,
    std::span<std::byte> dst,
    std::optional<SectorHeader> header
) {
    DiscEngine& engine = *engine_;
    switch (form) {
        case DiscForm::UserData:
            return engine.read_user_data(lba, dst);
        case DiscForm::RawFrame:

            return engine.read_raw_frame(lba, dst, header);
        case DiscForm::Subcode:
            return engine.read_subcode(lba, dst);
        case DiscForm::FullFrame:
            return engine.read_full_frame(lba, dst);
    }
    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
}

bool DiscStore::mounted_for_test() const noexcept {
    return engine_.has_value() && engine_->mounted();
}

void DiscStore::publish_(infra::OptRef<const IDiscRules> rules) noexcept {

    if (engine_.has_value()) {
        DiscGeometry shape = engine_->geometry();

        if (shape.mounted && rules)
            shape.identity = rules->identify(shape, *this);
        geom_.publish(shape);
        counters_.publish(engine_->counters());
        shape_ = shape;
        rules_ = shape.mounted ? rules : infra::OptRef<const IDiscRules>{};
    } else {
        const DiscGeometry shape{};
        geom_.publish(shape);
        counters_.publish(DiscCounters{});
        shape_ = shape;
        rules_ = {};
    }
}

Ex<std::span<std::uint8_t>> DiscStore::read_at(proto::FileSize at, std::span<std::uint8_t> dst) {
    TASTY_SEAT_BODY(DiscStore);
    if (!engine_.has_value() || !rules_) {
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    }
    const infra::OptRef<const IDiscRules> rules = rules_;
    const proto::BlockSize bs = rules->block_size();
    proto::Lba lba = proto::end_lba(at, bs);

    const DiscGeometry shape = shape_;

    const std::size_t block_bytes = bs.length_of(proto::BlockCount{1});
    std::span<std::uint8_t> rest = dst;
    while (rest.size() >= block_bytes) {
        rules->fill(shape, *this, lba, bs.block_of(rest));
        rest = rest.subspan(block_bytes);
        lba = lba + proto::BlockCount{1};
    }
    return dst.first(dst.size() - rest.size());
}

Ex<std::span<const std::uint8_t>> DiscStore::write_at(
    proto::FileSize,
    std::span<const std::uint8_t>
) {
    TASTY_SEAT_BODY(DiscStore);
    return std::unexpected(Error{Errc::io, ERR_SITE(), 0});
}

Ex<proto::FileSize> DiscStore::open_extent(proto::SlotIndex, const BlockCtl::StorageOrder&) {
    TASTY_SEAT_BODY(DiscStore);
    return proto::FileSize{};
}

void DiscStore::release(proto::SlotIndex) noexcept {
    TASTY_SEAT_BODY(DiscStore);
}

Ex<std::span<std::byte>> DiscStore::read_form(
    DiscForm form,
    proto::Lba lba,
    std::span<std::byte> dst,
    std::optional<SectorHeader> header
) {
    TASTY_SEAT_BODY(DiscStore);
    if (!engine_.has_value())
        return std::unexpected(Error{Errc::mount_failed, ERR_SITE(), 0});
    Ex<std::span<std::byte>> r = read_engine_(form, lba, dst, header);

    switch (form) {
        case DiscForm::Subcode:
            break;
        case DiscForm::UserData:
        case DiscForm::RawFrame:
        case DiscForm::FullFrame:

            if (lba != next_lba_)
                engine_->advise_ahead(lba);
            next_lba_ = lba + proto::BlockCount{1};
            break;
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
        auto d = DiscEngine::create(opener_, *policy_, prefetch_);
        if (!d) {
            publish_();
            return false;
        }
        engine_.emplace(std::move(*d));
        next_lba_ = {};
    }
    return true;
}

bool DiscStore::do_mount(
    infra::OptRef<const CuePolicy> policy,
    std::string_view path,
    infra::OptRef<const IDiscRules> rules
) noexcept {
    TASTY_SEAT_BODY(DiscStore);
    if (!ensure_engine_(policy))
        return false;

    if (path.empty()) {
        (void)engine_->unmount();
        publish_();
        return false;
    }
    const bool ok = engine_->mount(path).has_value();
    publish_(ok ? rules : infra::OptRef<const IDiscRules>{});
    return ok;
}

bool DiscStore::do_unmount(infra::OptRef<const CuePolicy> policy) noexcept {
    TASTY_SEAT_BODY(DiscStore);
    if (!ensure_engine_(policy))
        return false;
    (void)engine_->unmount();
    publish_();
    return false;
}

void DiscStore::do_release() noexcept {
    TASTY_SEAT_BODY(DiscStore);
    engine_.reset();
    policy_.reset();
    publish_();
}

}
