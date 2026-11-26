// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/minimig/io_frame_sink.h"

#include "cores/minimig/io_frame_sink_op.h"
#include "hal/selected.h"
#include "hal/types.h"

namespace mister::cores::minimig {

Ex<void> IoFrameSink::begin(proto::WideIoIndex, const proto::SessionParams&) {
    TASTY_SEAT_BODY(IoFrameSink);
    return {};
}

Ex<void> IoFrameSink::write(std::span<const std::uint8_t> data) {
    TASTY_SEAT_BODY(IoFrameSink);

    const bool whole = !data.empty() && data.size() % sizeof(hal::LinkWord) == 0;
    if (!whole || !opens_frame(hal::LinkWord::load_le(data.first<sizeof(hal::LinkWord)>()))) {
        tally_.refused.add();
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    hal::Selected cs(link_, hal::ChipSelect::Io);
    for (; !data.empty(); data = data.subspan(sizeof(hal::LinkWord))) {
        if (auto r = link_.transfer(hal::LinkWord::load_le(data.first<sizeof(hal::LinkWord)>()));
            !r) {
            tally_.faults.add();
            return std::unexpected(r.error());
        }
    }
    tally_.frames.add();
    return {};
}

Ex<void> IoFrameSink::end() {
    TASTY_SEAT_BODY(IoFrameSink);
    return {};
}

}
