// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/nvram_upload.h"

#include "app/config_rel.h"
#include "app/log_clock.h"
#include "app/save_upload_relay.h"
#include "cores/core.h"
#include "cores/save_home.h"
#include "cores/save_upload.h"
#include "app/log_rec.h"
#include "infra/prelude.h"
#include "proto/save_request.h"
#include "proto/link/link.h"
#include "proto/link/link_ops.h"
#include "proto/link/spi_fio_queue.h"

namespace mister::app {

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

bool NvramUpload::settle_step(bool fio_free) {
    TASTY_SEAT_BODY(NvramUpload);
    if (saves_.settled())
        return false;

    if (!fio_free && !saves_.holds_fio())
        return false;
    saves_.service_rt(proto::FioWordCount{proto::link::SpiFioQueue::kUnbudgeted}, fio_free);
    return true;
}

namespace {

[[nodiscard]] FixedStr<kConfigRelCap, StrFit::Reject> save_rel(
    const cores::ISaveUpload& up,
    std::string_view core
) {
    const std::string_view name = up.save_file_name();
    switch (up.save_home()) {
        case cores::SaveHome::ConfigNvram:
            return config_rel("nvram/", name);
        case cores::SaveHome::CoreSaves: {
            auto rel = core_saves_dir(core);
            if (rel.empty() || name.empty() || !rel.append(name))
                rel.clear();
            return rel;
        }
    }
    return {};
}

}

Ex<std::size_t> NvramUpload::write_save_upload_(cores::ISaveUpload& up, std::string_view core) {
    const auto rel = save_rel(up, core);
    if (rel.empty())
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 1});

    const std::optional<cores::mra::NvramSave> image = up.save_image();
    if (!image || !saves_.order(*image, rel.view(), up.save_shape())) {
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});
    }
    return image->size.file_word<std::size_t>();
}

Ex<std::size_t> NvramUpload::flush_save_upload(const SaveCtx& ctx) {
    TASTY_SEAT_BODY(NvramUpload);
    const infra::OptRef<cores::Core> current = ctx.core;
    if (!current)
        return std::size_t{0};
    if (ctx.fenced)
        return std::size_t{0};

    const auto up = current->save_upload();
    if (!up)
        return std::size_t{0};
    if (!vfs_) {
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    }
    if (up->save_file_name().empty()) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    proto::link::Link link{link_};
    proto::link::LinkOps ops{link};
    auto req = ops.read_save_request();
    if (!req)
        return std::unexpected(req.error());
    if (!req->requested())
        return std::size_t{0};
    return write_save_upload_(*up, current->profile().name);
}

bool NvramUpload::save_upload_counted(const SaveCtx& ctx) noexcept {
    TASTY_SEAT_BODY(NvramUpload);
    auto s = save_upload_now(ctx);
    if (!s) {
        ++tally_.save_upload_errors;
        log_lane_.push(LogRec::Refusal{s.error().code, s.error().site}, log_stamp(log_now()));
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
    const auto up = current->save_upload();
    if (!up || !up->saved_with_settings())
        return std::size_t{0};
    if (!vfs_)
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    if (up->save_file_name().empty()) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    return write_save_upload_(*up, current->profile().name);
}

}
