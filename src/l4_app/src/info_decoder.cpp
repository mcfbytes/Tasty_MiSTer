// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/info_decoder.h"

#include <utility>

#include "proto/user_io/poll_shape.h"

namespace mister::app {

std::string_view InfoDecoder::decode(const proto::LinkEvent::PolledWord& w) {
    TASTY_SEAT_BODY(InfoDecoder);
    refresh_();
    switch (w.shape) {
        case proto::user_io::PollShape::InfoPoll:
            return info_text_(w);
        case proto::user_io::PollShape::RumblePoll:
        case proto::user_io::PollShape::F12ModPoll:
        case proto::user_io::PollShape::kCount:
            break;
    }
    ++unmapped_;
    return {};
}

void InfoDecoder::refresh_() {
    if (!reader_.take_if_changed(cell_, scratch_))
        return;
    gen_ = scratch_.gen;
    table_ = proto::ItemTable{};
    row_.reset();
    if (scratch_.truncated)
        return;
    const std::size_t len = scratch_.len <= sizeof(scratch_.text) ? scratch_.len
                                                                  : sizeof(scratch_.text);
    auto parsed = proto::ItemTable::parse(std::string_view{scratch_.text, len});
    if (!parsed)
        return;
    table_ = std::move(*parsed);
    row_ = table_.info_row();
}

std::string_view InfoDecoder::info_text_(const proto::LinkEvent::PolledWord& w) {
    const unsigned index = w.word & proto::user_io::poll_row(w.shape).mask;
    if (index == 0)
        return {};
    if (w.bind_gen != gen_) {
        ++stale_;
        return {};
    }
    const std::string_view text = row_.has_value() ? table_.subfield(table_.items()[*row_], index)
                                                   : std::string_view{};
    if (text.empty()) {
        ++unmapped_;
        return {};
    }
    ++decoded_;
    return text;
}

}
