// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/flat_save_servant.h"

#include <algorithm>
#include <cstring>

#include "cores/registry.h"
#include "infra/seat_owner_io.h"
#include "infra/span_cut.h"
#include "svc/save_frame_layer.h"
#include "svc/save_stage_keeper.h"

namespace mister::app {

namespace {

[[nodiscard]] constexpr proto::SaveAct done(bool ok) noexcept {
    return proto::SaveAct{.kind = proto::SaveAct::Kind::Done, .ok = ok};
}

}

proto::FileSize FlatSaveServant::image_cap() noexcept {
    return cores::largest_declared_save();
}

FlatSaveServant::FlatSaveServant(svc::SaveStageKeeper& keeper)
    : keeper_(keeper),
      image_(image_cap().zeroed<std::vector<std::byte>>()) {}

proto::SaveAct FlatSaveServant::start(
    const cores::mra::NvramSave& image,
    std::string_view rel,
    hal::Width word,
    const cores::SaveShape& shape
) noexcept {
    TASTY_SEAT_BODY(FlatSaveServant);
    phase_ = Phase::Idle;
    const svc::SaveBanks& banks = shape.frame.banks;
    const proto::FileSize whole = banks.image_bytes(image.size);

    const proto::FileSize word_size{hal::word_bytes(word)};
    const bool runs_whole = !banks.framed() || banks.body.bytes_past_whole(word_size).empty();
    if (image.size.empty()
        || !shape.frames(image.size)
        || !runs_whole
        || whole > proto::FileSize::of(std::span(image_))
        || !rel_.assign(rel)
        || rel_.empty()) {
        return done(false);
    }
    index_ = image.index;
    wire_ = image.size;
    at_ = proto::kFileStart;
    whole_ = whole;
    frame_ = shape.frame;

    const std::span<std::byte> framed = infra::affine::first(std::span(image_), whole_);
    const svc::SaveFrameLayerT<seat_owner::Io> layer{banks};
    layer.lay_lead(framed, std::as_bytes(shape.lead));
    layer.lay_heads(framed, std::as_bytes(frame_.heads.view()));
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
        case Phase::Reading: {
            const std::span<std::byte> to = infra::affine::from(
                std::span(image_),
                frame_.banks.place(at_)
            );
            std::memcpy(to.data(), bytes.data(), asked_);
            at_ += proto::FileSize{asked_};
            return next_read_();
        }
        case Phase::Closing: {
            phase_ = Phase::Idle;
            const auto r = keeper_.commit_image(rel_.view(), filtered_());
            return done(r.has_value());
        }
    }
    return done(false);
}

proto::SaveAct FlatSaveServant::next_read_() noexcept {
    const proto::FileSize left = at_.remaining_in(wire_);
    if (left.empty()) {
        phase_ = Phase::Closing;
        return proto::SaveAct{.kind = proto::SaveAct::Kind::Close};
    }

    const svc::SaveBanks& banks = frame_.banks;
    const proto::FileSize run = banks.framed() ? left.min(banks.run_left(at_)) : left;
    asked_ = proto::FileSize{piece_}.min(run).file_word<std::uint16_t>();
    return proto::SaveAct{.kind = proto::SaveAct::Kind::Read, .len = asked_};
}

std::span<const std::byte> FlatSaveServant::filtered_() noexcept {
    const std::span<std::byte> whole = infra::affine::first(std::span(image_), whole_);
    switch (frame_.filter) {
        case cores::SaveFilter::Whole:
            return whole;
        case cores::SaveFilter::BlankBanksDropped:
            return infra::affine::first(whole, blank_banks_.close(whole, frame_.banks));
    }
    return whole;
}

}
