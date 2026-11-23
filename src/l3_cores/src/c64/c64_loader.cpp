// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/c64/c64_loader.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cores/held_image_file.h"
#include "cores/c64/easyflash_save_name.h"
#include "infra/ascii.h"
#include "infra/span_cut.h"
#include "svc/card_dirs.h"

namespace mister::cores::c64 {

namespace {

[[nodiscard]] proto::FileSize read_some(
    svc::IFile& file,
    proto::FileOffset at,
    std::span<std::uint8_t> dst
) {
    proto::FileSize got{};
    while (got < proto::FileSize::of(dst)) {
        const auto n = file.read_at(
            at + got,
            std::as_writable_bytes(infra::affine::from(dst, got))
        );
        if (!n || n->empty())
            break;
        got += proto::FileSize::of(*n);
    }
    return got;
}

}

Ex<LoadPlan> C64Loader::plan(const LoadAsk& ask, const proto::StatusWord&) {
    TASTY_SEAT_BODY(C64Loader);
    LoadPlan plan{};
    plan.count = 1;
    if (!infra::iends_with(ask.path, kCartExt))
        return plan;

    CrtHeader header{};
    if (auto file = vfs_.open(ask.path, svc::OpenMode::Read))
        (void)read_some(**file, proto::kFileStart, header);
    plan.rows[0].derived = EasyFlashImage::named_by(header);
    return plan;
}

Ex<std::unique_ptr<svc::IFile>> C64Loader::source(
    const LoadAsk& ask,
    std::unique_ptr<svc::IFile> raw
) {
    TASTY_SEAT_BODY(C64Loader);
    framed_ = false;
    from_save_ = false;
    packets_skipped_ = infra::Tally{};
    std::unique_ptr<svc::IFile> file = std::move(raw);
    if (const std::optional<infra::PathText> name = save_name_of(ask.path)) {
        const std::string save = svc::card_rel({svc::kSavesDir, core_dir_, name->view()});
        if (vfs_.file_exists(save)) {
            auto opened = vfs_.open(save, svc::OpenMode::ReadWhole);
            if (!opened)
                return std::unexpected(opened.error());
            file = std::move(*opened);
            from_save_ = true;
        }
    }
    if (file == nullptr)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    const auto size = file->size();
    if (!size)
        return std::unexpected(size.error());
    if (*size < kCrtHeaderBytes)
        return std::unexpected(size->refusal(Errc::bad_format, ERR_SITE()));
    CrtHeader header{};
    if (auto r = file->read_exact_at(proto::kFileStart, std::as_writable_bytes(std::span(header)));
        !r) {
        return std::unexpected(r.error());
    }
    EasyFlashImage image{header};
    ChipPacket packet{};
    for (proto::FileOffset at = proto::kFileStart + kCrtHeaderBytes;; at += kChipPacketBytes) {
        const proto::FileSize got = read_some(*file, at, packet);
        if (got.empty())
            break;

        std::ranges::fill(infra::affine::from(std::span<std::uint8_t>(packet), got), kErased);
        if (!image.place(packet))
            packets_skipped_.add();
    }
    header_ = header;
    framed_ = true;
    return std::make_unique<HeldImageFile>(std::move(image).release());
}

Ex<void> C64Loader::finish(infra::OptRef<ICoreWindow>) {
    TASTY_SEAT_BODY(C64Loader);
    return {};
}

std::span<const std::uint8_t> C64Loader::facts() const noexcept {
    TASTY_SEAT_BODY(C64Loader);
    if (!framed_)
        return {};
    return header_;
}

}
