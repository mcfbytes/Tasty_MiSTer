// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/c64/c64_core.h"

#include <algorithm>
#include <cstring>

#include "cores/c64/easyflash_save_name.h"
#include "cores/core_adopt.h"
#include "cores/mounted_path.h"
#include "infra/ascii.h"
#include "infra/static_bytes.h"

namespace mister::cores::c64 {

Ex<void> C64Core::do_adopt(const CoreAdopt&) {
    declare_resets(kResetTerms);
    return {};
}

Ex<void> C64Core::on_mount(IoIndex slot, const MountedPath& p) {
    return stream_.mount(slot, p);
}

std::string_view C64Core::save_file_name() const noexcept {
    TASTY_SEAT_BODY(C64Core);
    return dump_ ? dump_->save_name.view() : std::string_view{};
}

std::optional<mra::NvramSave> C64Core::save_image() const noexcept {
    TASTY_SEAT_BODY(C64Core);
    if (!dump_)
        return std::nullopt;
    return mra::NvramSave{.index = kEasyFlashUploadIndex, .size = kEasyFlashWireBytes};
}

SaveShape C64Core::save_shape() const noexcept {
    TASTY_SEAT_BODY(C64Core);
    SaveShape shape{
        .frame = {
            .filter = SaveFilter::BlankBanksDropped,
            .banks = kEasyFlashBanks,
            .heads = infra::StaticBytes{kEasyFlashHeads}
        }
    };
    if (dump_)
        shape.lead = dump_->header;
    return shape;
}

Ex<proto::SessionParams> C64Core::stream_opening(const StreamAsk& ask) {
    TASTY_SEAT_BODY(C64Core);
    if (infra::iends_with(ask.path, kCartExt))
        dump_.reset();
    return stream_.stream_opening(ask);
}

void C64Core::stream_closed(const StreamLoadEnd& end) noexcept {
    TASTY_SEAT_BODY(C64Core);
    stream_.stream_closed(end);
}

Ex<void> C64Core::on_apply_load_facts(std::span<const std::uint8_t> facts, std::string_view image) {
    const std::optional<infra::PathText> name = save_name_of(image);
    if (proto::FileSize::of(facts) != kCrtHeaderBytes || !name) {
        facts_refusals_.add();
        return std::unexpected(proto::FileSize::of(facts).refusal(Errc::bad_format, ERR_SITE()));
    }
    Dump dump{.save_name = *name};
    std::memcpy(dump.header.data(), facts.data(), dump.header.size());
    dump_ = dump;
    return {};
}

infra::OptRef<ISaveUpload> C64Core::on_save_upload() noexcept {
    if (!dump_)
        return {};
    return infra::OptRef<ISaveUpload>{*this};
}

}
