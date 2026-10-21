// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/user_io/poll_reader.h"

#include "hal/selected.h"

namespace mister::proto::user_io {

reactor::LinkDecoderDecl PollReader::census_row(const reactor::ILinkDecoder& impl) const noexcept {
    reactor::LinkDecoderDecl row = kHostPollRows[infra::ordinal(shape_)];
    row.impl = &impl;
    return row;
}

void PollReader::arm(bool declared, BindGeneration gen) noexcept {
    TASTY_SEAT_BODY(PollReader);
    const PollShapeRow& row = poll_row(shape_);
    if (keeps_mirror(row)) {

        for (std::uint8_t p = 0; p < row.params; ++p) {
            if (mirror_[p] != 0 && push_(row.shape, p, 0))
                mirror_[p] = 0;
        }
    }
    armed_ = declared;
    gen_ = gen;
}

void PollReader::service() noexcept {
    TASTY_SEAT_BODY(PollReader);
    const PollShapeRow& row = poll_row(shape_);
    if (row.cadence == Cadence::Census)
        issue_(row);
}

void PollReader::start_beat() noexcept {
    TASTY_SEAT_BODY(PollReader);
    const PollShapeRow& row = poll_row(shape_);
    if (row.cadence == Cadence::SessionStart)
        issue_(row);
}

void PollReader::issue_(const PollShapeRow& row) noexcept {
    const std::uint8_t issued = params_.issued_params(shape_);
    for (std::uint8_t p = 0; p < row.params; ++p) {
        if ((issued & (1u << p)) != 0u) {
            beat_(row, p);
        } else {
            mirror_[p] = 0;
        }
    }
}

void PollReader::beat_(const PollShapeRow& row, std::uint8_t param) noexcept {
    std::uint16_t word = 0;
    {
        hal::Selected cs(link_, hal::ChipSelect::Io);
        ++polls_;
        const auto r = link_.transfer(hal::SpiWord{command_word(row, param)});
        if (!r) {
            ++errors_;
            return;
        }
        word = r->v;
    }
    if (!guard_passes(row, word, mirror_[param]))
        return;

    if (push_(row.shape, param, word) && keeps_mirror(row))
        mirror_[param] = word;
}

bool PollReader::push_(PollShape shape, std::uint8_t param, std::uint16_t word) noexcept {
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
