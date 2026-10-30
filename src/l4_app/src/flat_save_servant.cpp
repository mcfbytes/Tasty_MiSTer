// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/flat_save_servant.h"

#include <algorithm>
#include <cstring>

#include "app/durable_write.h"

namespace mister::app {

namespace {

[[nodiscard]] constexpr proto::SaveAct done(bool ok) noexcept {
    return proto::SaveAct{.kind = proto::SaveAct::Kind::Done, .ok = ok};
}

}

FlatSaveServant::FlatSaveServant(const svc::Vfs& vfs) : vfs_(vfs), image_(kImageCap) {}

proto::SaveAct FlatSaveServant::start(
    const cores::SaveImage& image,
    std::string_view rel,
    std::uint8_t word_bytes
) noexcept {
    TASTY_SEAT_BODY(FlatSaveServant);
    phase_ = Phase::Idle;
    if (image.size == 0 || image.size > kImageCap || !rel_.assign(rel) || rel_.empty()) {
        return done(false);
    }
    index_ = image.index;
    size_ = image.size;
    at_ = 0;
    asked_ = 0;

    piece_ = static_cast<std::uint16_t>(
        word_bytes == 2 ? proto::kSavePieceBytes : proto::kSavePieceBytes / 2u
    );
    phase_ = Phase::Opening;
    return proto::SaveAct{.kind = proto::SaveAct::Kind::Open, .index = index_};
}

proto::SaveAct FlatSaveServant::resume(
    std::span<const std::uint8_t, proto::kSavePieceBytes> bytes
) noexcept {
    TASTY_SEAT_BODY(FlatSaveServant);
    switch (phase_) {
        case Phase::Idle:
            return done(false);
        case Phase::Opening:
            phase_ = Phase::Reading;
            return next_read_();
        case Phase::Reading:
            std::memcpy(image_.data() + at_, bytes.data(), asked_);
            at_ += asked_;
            return next_read_();
        case Phase::Closing: {
            phase_ = Phase::Idle;
            const auto r = durable_write(
                vfs_,
                rel_.view(),
                std::span<const std::byte>(image_.data(), size_)
            );
            return done(r.has_value());
        }
    }
    return done(false);
}

proto::SaveAct FlatSaveServant::next_read_() noexcept {
    if (at_ >= size_) {
        phase_ = Phase::Closing;
        return proto::SaveAct{.kind = proto::SaveAct::Kind::Close};
    }
    asked_ = static_cast<std::uint16_t>(std::min<std::size_t>(piece_, size_ - at_));
    return proto::SaveAct{.kind = proto::SaveAct::Kind::Read, .len = asked_};
}

}
