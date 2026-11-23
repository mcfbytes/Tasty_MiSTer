// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/ide/atapi_disc.h"

#include <utility>

namespace mister::cores::ide {

Ex<std::unique_ptr<AtapiDisc>> AtapiDisc::open(const svc::Vfs& vfs, std::string_view path) {

    std::unique_ptr<AtapiDisc> disc{new AtapiDisc()};
    if (auto mounted = disc->mount_(vfs, path); !mounted)
        return std::unexpected(mounted.error());
    return disc;
}

Ex<void> AtapiDisc::mount_(const svc::Vfs& vfs, std::string_view path) {
    TASTY_SEAT_BODY(AtapiDisc);
    auto engine = svc::disc::DiscEngine::create(vfs, kAtapiCuePolicy, prefetch_);
    if (!engine)
        return std::unexpected(engine.error());
    engine_.emplace(std::move(*engine));
    return engine_->mount(path);
}

Ex<std::span<std::byte>> AtapiDisc::read_form(
    svc::disc::DiscForm form,
    proto::Lba lba,
    std::span<std::byte> dst,
    std::optional<svc::disc::SectorHeader> header
) {
    TASTY_SEAT_BODY(AtapiDisc);
    if (!engine_)
        return std::unexpected(Error{Errc::mount_failed, ERR_SITE(), 0});
    switch (form) {
        case svc::disc::DiscForm::UserData:
            return engine_->read_user_data(lba, dst);
        case svc::disc::DiscForm::RawFrame:
            return engine_->read_raw_frame(lba, dst, header);
        case svc::disc::DiscForm::Subcode:
        case svc::disc::DiscForm::FullFrame:
            break;
    }
    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
}

}
