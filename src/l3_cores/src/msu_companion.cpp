// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/msu_companion.h"

#include <string_view>

#include "cores/manifests/snes.h"
#include "cores/msu_data_loader.h"
#include "cores/msu_wire.h"
#include "infra/path_parts.h"

namespace mister::cores {

Ex<CompanionPlan> MsuCompanion::plan(const CompanionAsk& ask) {
    TASTY_SEAT_BODY(MsuCompanion);

    std::string_view stem = ask.path;
    stem.remove_suffix(infra::extension(stem).size());
    if (!stem_.assign(stem))
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    CompanionPlan p{};
    p.stem = stem_.view();
    p.opcode = proto::UioOp::CdSet;
    proto::FileSize size{};
    if (ask.mailbox_live && data_.assign(stem_.view()) && data_.append(".msu")) {
        if (auto f = vfs_.open(data_.view(), svc::OpenMode::Read)) {
            p.present = true;
            if (const auto sz = (*f)->size())
                size = *sz;
        }
    }
    TransferRow row{
        .dest = RowDest::Window,
        .index = proto::WideIoIndex{msu::kDataIndex},
        .addr = 0,
        .window = WindowRef::at(CoreWindowSlot{0}),
        .bracket = RowBracket::AroundWithLength,
        .source = data_.view(),
        .placed = true
    };
    const auto base = row_address(row, manifests::kSnesWindows, ask.aperture_base);
    if (!size.empty() && size <= msu::kDataBound && base) {
        row.set_file_extent(size, msu::data_extent(size));
        p.load = CompanionPlan::Load{.before = msu::set_data_base(*base), .row = row};
    }
    p.after = msu::set_enable(p.present);
    p.poll = p.present ? proto::MailboxPoll::On : proto::MailboxPoll::Off;
    return p;
}

std::unique_ptr<ILoader> MsuCompanion::loader() {
    TASTY_SEAT_BODY(MsuCompanion);
    return std::make_unique<MsuDataLoader>();
}

}
