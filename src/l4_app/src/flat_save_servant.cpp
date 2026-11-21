// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/flat_save_servant.h"

#include <algorithm>
#include <cstring>

#include "svc/save_stage_keeper.h"

namespace mister::app {

namespace {

[[nodiscard]] constexpr proto::SaveAct done(bool ok) noexcept {
    return proto::SaveAct{.kind = proto::SaveAct::Kind::Done, .ok = ok};
}

}

FlatSaveServant::FlatSaveServant(svc::SaveStageKeeper& keeper)
    : keeper_(keeper),
      image_(kImageCap.zeroed<std::vector<std::byte>>()) {}

proto::SaveAct FlatSaveServant::start(
    const cores::mra::NvramSave& image,
    std::string_view rel,
    hal::Width word
) noexcept {
    TASTY_SEAT_BODY(FlatSaveServant);
    phase_ = Phase::Idle;
    if (image.size.empty() || image.size > kImageCap || !rel_.assign(rel) || rel_.empty()) {
        return done(false);
    }
    index_ = image.index;
    size_ = image.size.file_word<std::size_t>();
    at_ = 0;
    asked_ = 0;

    piece_ = static_cast<std::uint16_t>(hal::word_bytes(word) * proto::kSavePieceWords);
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
            const auto r = keeper_.commit_image(
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
