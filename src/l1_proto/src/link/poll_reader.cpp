// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/link/poll_reader.h"

#include "hal/selected.h"
#include "infra/affine.h"
#include "proto/link/link.h"
#include "proto/link/link_sel_carriers.h"
#include "proto/link/poll_frame_read.h"

namespace mister::proto::link {

reactor::LinkDecoderDecl PollReader::census_row(const reactor::ILinkDecoder& impl) const noexcept {
    reactor::LinkDecoderDecl row = kHostPollRows[infra::ordinal(shape_)];
    row.impl = &impl;
    return row;
}

void PollReader::arm(bool declared, BindGeneration gen) noexcept {
    TASTY_SEAT_BODY(PollReader);
    const user_io::PollShapeRow& row = user_io::poll_row(shape_);
    if (user_io::keeps_mirror(row)) {

        for (std::uint8_t p = 0; p < row.params; ++p) {
            if (mirror_[p] != hal::SpiWord{} && push_(row.shape, p, hal::SpiWord{}))
                mirror_[p] = hal::SpiWord{};
        }
    }
    armed_ = declared;
    gen_ = gen;
}

void PollReader::service() noexcept {
    TASTY_SEAT_BODY(PollReader);
    const user_io::PollShapeRow& row = user_io::poll_row(shape_);
    if (row.cadence == user_io::Cadence::Census)
        issue_(row);
}

void PollReader::start_beat() noexcept {
    TASTY_SEAT_BODY(PollReader);
    const user_io::PollShapeRow& row = user_io::poll_row(shape_);
    if (row.cadence == user_io::Cadence::SessionStart)
        issue_(row);
}

void PollReader::issue_(const user_io::PollShapeRow& row) noexcept {

    const proto::ParamSet issued = row.form == user_io::CommandForm::ParamInHighByte
        ? params_.issued_params(shape_)
        : user_io::kBareRowParams;
    for (std::uint8_t p = 0; p < row.params; ++p) {
        if (issued.contains(proto::PollParam::of(p))) {
            beat_(row, p);
        } else {
            mirror_[p] = hal::SpiWord{};
        }
    }
}

void PollReader::frame_beat_(
    const user_io::PollShapeRow& row,
    const user_io::PollFrameShape& frame
) noexcept {
    ++polls_;
    Link link{link_};
    const PollFrameRead read = link.read_poll_frame(frame, std::nullopt);
    if (read.outcome == PollFrameRead::Outcome::Faulted)
        ++errors_;
    if (read.outcome != PollFrameRead::Outcome::Taken)
        return;
    const ReplyIndex last = infra::affine::end_of<ReplyIndex>(read.frame.kept - ReplyCount{1});

    (void)push_(row.shape, 0, read.frame.replies[last]);
}

void PollReader::beat_(const user_io::PollShapeRow& row, std::uint8_t param) noexcept {
    if (row.frame)
        return frame_beat_(row, *row.frame);
    hal::SpiWord word{};
    {
        hal::Selected cs(link_, chip_select(row.select));
        ++polls_;
        const auto r = link_.transfer(user_io::command_word(row, param));
        if (!r) {
            ++errors_;
            return;
        }
        word = *r;
    }
    const user_io::PollReply reply{
        .word = ReplyWord::of(word),
        .mirror = ReplyWord::of(mirror_[param])
    };
    if (!user_io::guard_passes(row, reply))
        return;

    if (push_(row.shape, param, word) && user_io::keeps_mirror(row))
        mirror_[param] = word;
}

bool PollReader::push_(user_io::PollShape shape, std::uint8_t param, hal::SpiWord word) noexcept {
    if (!out_.push(
            LinkEvent::PolledWord{.shape = shape, .param = param, .word = word, .bind_gen = gen_}
        )) {
        ++declined_;
        return false;
    }
    ++pushes_;
    return true;
}

}
