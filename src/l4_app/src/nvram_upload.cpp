// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/nvram_upload.h"

#include <unistd.h>

#include "app/config_rel.h"
#include "app/log_clock.h"
#include "app/save_upload_relay.h"
#include "cores/core_support.h"
#include "cores/save_upload.h"
#include "infra/log_rec.h"
#include "proto/save_request.h"
#include "proto/spi_fio_queue.h"

namespace mister::app {

void NvramUpload::sleep_ms(unsigned ms) {
    if (sleeper_) {
        sleeper_->sleep_ms(ms);
        return;
    }
    ::usleep(static_cast<::useconds_t>(ms) * 1000u);
}

NvramUpload::Tally NvramUpload::tally() const noexcept {
    return Tally{
        .save_uploads = tally_.save_uploads + saves_.saved(),
        .save_upload_errors = tally_.save_upload_errors
            + saves_.failed()
            + saves_.wire_faults()
            + saves_.liveness_drops()
    };
}

bool NvramUpload::saves_drained() const noexcept {
    return saves_.drained();
}

void NvramUpload::drain_write_verdicts() {
    TASTY_SEAT_BODY(NvramUpload);

    while (writes_.reap()) {
    }
}

constexpr unsigned kSaveSettleTries = 8192;

void NvramUpload::settle_save_lane(bool fio_free) {
    TASTY_SEAT_BODY(NvramUpload);

    for (unsigned i = 0; i < kSaveSettleTries && !saves_.settled(); ++i) {

        if (!fio_free && !saves_.holds_fio())
            break;
        saves_.service_rt(link_, proto::SpiFioQueue::kUnbudgeted, fio_free);
        sleep_ms(1);
    }

    for (unsigned i = 0; i < kSaveSettleTries && writes_.in_flight() != 0; ++i) {
        drain_write_verdicts();
        if (writes_.in_flight() == 0)
            break;
        sleep_ms(1);
    }
}

void NvramUpload::settle_writes() noexcept {
    TASTY_SEAT_BODY(NvramUpload);
    writes_.drain_on_caller();
    drain_write_verdicts();
}

Ex<std::size_t> NvramUpload::write_save_upload_(cores::ISaveUpload& up) {
    const auto rel = config_rel("config/nvram/", up.save_file_name());
    if (rel.empty())
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 1});

    const cores::SaveImage image = up.save_image();
    if (!saves_.order(image, rel.view())) {
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});
    }
    return image.size;
}

Ex<std::size_t> NvramUpload::flush_save_upload(const SaveCtx& ctx) {
    TASTY_SEAT_BODY(NvramUpload);
    const infra::OptRef<cores::Core> current = ctx.core;
    if (!current)
        return std::size_t{0};
    if (ctx.fenced)
        return std::size_t{0};

    cores::ISaveUpload* up = current->save_upload();
    if (up == nullptr)
        return std::size_t{0};
    if (!vfs_) {
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    }
    if (up->save_file_name().empty()) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    auto req = proto::read_save_request(link_);
    if (!req)
        return std::unexpected(req.error());
    if (!req->requested())
        return std::size_t{0};
    return write_save_upload_(*up);
}

bool NvramUpload::save_upload_counted(const SaveCtx& ctx) noexcept {
    TASTY_SEAT_BODY(NvramUpload);
    auto s = save_upload_now(ctx);
    if (!s) {
        ++tally_.save_upload_errors;
        log_lane_.push(LogRec::Refusal{s.error().code, s.error().site}, log_now_ns());
        return false;
    }
    return true;
}

Ex<std::size_t> NvramUpload::save_upload_now(const SaveCtx& ctx) {
    TASTY_SEAT_BODY(NvramUpload);
    const infra::OptRef<cores::Core> current = ctx.core;
    if (!current)
        return std::size_t{0};
    if (ctx.fenced)
        return std::size_t{0};
    cores::ISaveUpload* up = current->save_upload();
    if (up == nullptr)
        return std::size_t{0};
    if (!vfs_)
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    if (up->save_file_name().empty()) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    return write_save_upload_(*up);
}

}
