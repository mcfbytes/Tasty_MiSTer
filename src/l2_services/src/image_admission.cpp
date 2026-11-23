// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/image_admission.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "infra/ascii.h"
#include "svc/archive_copy_image.h"
#include "svc/asset_name.h"
#include "svc/dc42_image.h"
#include "svc/raw_disk_image.h"
#include "svc/twomg_image.h"
#include "svc/woz35_codec.h"
#include "svc/woz525_codec.h"
#include "svc/woz_format.h"

namespace mister::svc {
namespace {

constexpr proto::FileSize k140KBytes{143'360};
constexpr proto::FileSize kNibbleBytes{232'960};
constexpr proto::FileSize k800KBytes{819'200};
constexpr proto::FileSize k400KBytes{409'600};
constexpr proto::FileSize kBlockBytes{512};

enum class DiskClass : std::uint8_t { Unknown, Block, Floppy525, Floppy35 };

[[nodiscard]] constexpr DiskClass class_of(FluxSide drive) noexcept {
    return drive == FluxSide::Inch35 ? DiskClass::Floppy35 : DiskClass::Floppy525;
}

[[nodiscard]] constexpr woz::DiskType woz_type_of(FluxSide drive) noexcept {
    return drive == FluxSide::Inch35 ? woz::DiskType::Inch35 : woz::DiskType::Inch525;
}

[[nodiscard]] std::optional<woz::DiskType> typed_woz(const ImageFacts& f) noexcept {
    const std::optional<woz::DiskType> t = woz::disk_type(f.file_head());
    if (!t || *t == woz::DiskType::Unset)
        return std::nullopt;
    return t;
}

[[nodiscard]] DiskClass classify(const ImageFacts& f) noexcept {
    if (const std::optional<woz::DiskType> t = woz::disk_type(f.file_head())) {
        if (*t == woz::DiskType::Inch525)
            return DiskClass::Floppy525;
        if (*t == woz::DiskType::Inch35)
            return DiskClass::Floppy35;
    }
    if (const auto m = TwoMgImage::Header::parse(f.file_head(), f.size)) {
        if (m->order == TwoMgImage::Order::Nibble || m->declared == k140KBytes)
            return DiskClass::Floppy525;
        if (m->declared == k800KBytes)
            return DiskClass::Floppy35;
        return DiskClass::Block;
    }
    if (const auto d = Dc42Image::Header::probe(f.file_head(), f.size)) {
        if (d->data == k800KBytes || d->data == k400KBytes)
            return DiskClass::Floppy35;
        return DiskClass::Block;
    }
    if (f.ext == "nib" || f.size == kNibbleBytes || f.size == k140KBytes)
        return DiskClass::Floppy525;
    if (f.size == k800KBytes)
        return DiskClass::Floppy35;
    if (f.ext == "hdv" || f.ext == "po")
        return DiskClass::Block;
    if (!f.size.empty() && f.size.bytes_past_whole(kBlockBytes).empty())
        return DiskClass::Block;
    return DiskClass::Unknown;
}

struct Conversion {
    std::unique_ptr<IDiskImage> source;
    Woz525Codec::SectorOrder order = Woz525Codec::SectorOrder::Dos;
    bool writes_back = false;
};

[[nodiscard]] Ex<Conversion> conversion_of(
    const ImageFacts& f,
    std::unique_ptr<IFile> file,
    bool writable
) {
    using Order = Woz525Codec::SectorOrder;
    std::unique_ptr<IDiskImage> raw = std::make_unique<RawDiskImage>(
        std::move(file),
        f.size,
        writable
    );
    if (const auto m = TwoMgImage::Header::parse(f.file_head(), f.size)) {
        auto payload = TwoMgImage::open(std::move(raw), PayloadReach::Declared);
        if (!payload)
            return std::unexpected(payload.error());
        const Order order = m->order == TwoMgImage::Order::ProDos ? Order::ProDos
            : m->order == TwoMgImage::Order::Nibble               ? Order::Nibble
                                                                  : Order::Dos;
        return Conversion{
            .source = std::move(*payload),
            .order = order,
            .writes_back = writable && order != Order::Nibble
        };
    }
    if (Dc42Image::Header::probe(f.file_head(), f.size)) {
        auto payload = Dc42Image::open(std::move(raw), PayloadReach::Declared);
        if (!payload)
            return std::unexpected(payload.error());

        return Conversion{.source = std::move(*payload), .order = Order::ProDos};
    }
    if (f.size == kNibbleBytes)
        return Conversion{.source = std::move(raw), .order = Order::Nibble};
    const Order order = infra::ieq(f.ext, "po") ? Order::ProDos : Order::Dos;
    return Conversion{.source = std::move(raw), .order = order, .writes_back = writable};
}

[[nodiscard]] Error conversion_refusal(const Error& e, infra::ErrorSite at) noexcept {
    return refusal_error(
        e.code == Errc::bad_format ? MountRefusal::ConversionFailed
                                   : MountRefusal::SourceUnreadable,
        at
    );
}

[[nodiscard]] Ex<std::unique_ptr<IDiskImage>> convert(
    FluxSide drive,
    const ImageFacts& f,
    std::unique_ptr<IFile> file,
    bool writable
) {
    auto c = conversion_of(f, std::move(file), writable);
    if (!c)
        return std::unexpected(conversion_refusal(c.error(), ERR_SITE()));
    Ex<std::unique_ptr<IDiskImage>> image = std::unexpected(
        refusal_error(MountRefusal::ConversionFailed, ERR_SITE())
    );
    if (drive == FluxSide::Inch525) {
        image = Woz525Codec::open(std::move(c->source), c->order, c->writes_back);
    } else if (c->order != Woz525Codec::SectorOrder::Nibble) {
        image = Woz35Codec::open(std::move(c->source), c->writes_back);
    }
    if (!image)
        return std::unexpected(conversion_refusal(image.error(), ERR_SITE()));
    return image;
}

[[nodiscard]] std::string_view after_last_dot(std::string_view path) noexcept {
    const std::size_t dot = path.rfind('.');
    return dot == std::string_view::npos ? std::string_view{} : path.substr(dot + 1);
}

}

Ex<ImageFacts> ImageFacts::read(IFile& file, std::string_view path, proto::FileSize size) {
    ImageFacts f{
        .size = size,
        .ext = after_last_dot(path),
        .archive = asset_name::zip_split(path).zipped
    };
    auto got = file.read_at(proto::kFileStart, std::span{f.head}.first(f.file_head().size()));
    if (!got)
        return std::unexpected(got.error());
    return f;
}

std::expected<FluxAdmission, MountRefusal> admit_flux_image(
    FluxSide drive,
    const ImageFacts& facts
) noexcept {

    if (const std::optional<woz::DiskType> t = typed_woz(facts)) {
        if (*t != woz_type_of(drive))
            return std::unexpected(
                drive == FluxSide::Inch35 ? MountRefusal::Inch525FluxOnInch35Slot
                                          : MountRefusal::Inch35FluxOnInch525Slot
            );
        return facts.archive ? FluxAdmission::CopyWhole : FluxAdmission::Passthrough;
    }

    if (classify(facts) != class_of(drive))
        return std::unexpected(
            drive == FluxSide::Inch35 ? MountRefusal::NotAn800KImage : MountRefusal::NotA140KImage
        );
    return FluxAdmission::Convert;
}

std::expected<BlockAdmission, MountRefusal> admit_block_image(const ImageFacts& facts) noexcept {

    if (typed_woz(facts))
        return std::unexpected(MountRefusal::FluxImageOnBlockSlot);
    if (facts.size == kNibbleBytes)
        return std::unexpected(MountRefusal::NibbleImageOnBlockSlot);
    const auto m = TwoMgImage::Header::parse(facts.file_head(), facts.size);
    if (m && m->order != TwoMgImage::Order::ProDos)
        return std::unexpected(MountRefusal::SectorOrderContainerOnBlockSlot);
    if (infra::ieq(facts.ext, "do"))
        return std::unexpected(MountRefusal::DosOrderOnBlockSlot);
    if (m)
        return BlockAdmission::TwoMgPayload;
    if (Dc42Image::Header::probe(facts.file_head(), facts.size))
        return BlockAdmission::Dc42Payload;
    return BlockAdmission::Passthrough;
}

Ex<std::unique_ptr<IDiskImage>> open_flux_image(
    FluxSide drive,
    std::unique_ptr<IFile> file,
    std::string_view path,
    proto::FileSize size,
    bool writable
) {
    auto facts = ImageFacts::read(*file, path, size);
    if (!facts)
        return std::unexpected(facts.error());
    const auto admitted = admit_flux_image(drive, *facts);
    if (!admitted)
        return std::unexpected(refusal_error(admitted.error(), ERR_SITE()));
    switch (*admitted) {
        case FluxAdmission::Passthrough:
            return std::unique_ptr<IDiskImage>{new RawDiskImage(std::move(file), size, writable)};
        case FluxAdmission::CopyWhole: {

            auto copy = ArchiveCopyImage::open(std::move(file), size);
            if (!copy)
                return std::unexpected(refusal_error(MountRefusal::ArchiveUnreadable, ERR_SITE()));
            return copy;
        }
        case FluxAdmission::Convert:
            return convert(drive, *facts, std::move(file), writable);
    }
    return std::unexpected(Error{Errc::contract, ERR_SITE(), 0});
}

Ex<std::unique_ptr<IDiskImage>> open_block_image(
    std::unique_ptr<IFile> file,
    std::string_view path,
    proto::FileSize size,
    bool writable
) {
    auto facts = ImageFacts::read(*file, path, size);
    if (!facts)
        return std::unexpected(facts.error());
    const auto admitted = admit_block_image(*facts);
    if (!admitted)
        return std::unexpected(refusal_error(admitted.error(), ERR_SITE()));
    std::unique_ptr<IDiskImage> raw = std::make_unique<RawDiskImage>(
        std::move(file),
        size,
        writable
    );
    switch (*admitted) {
        case BlockAdmission::Passthrough:
            return raw;
        case BlockAdmission::TwoMgPayload:
            return TwoMgImage::open(std::move(raw), PayloadReach::ToFileEnd);
        case BlockAdmission::Dc42Payload:
            return Dc42Image::open(std::move(raw), PayloadReach::ToFileEnd);
    }
    return std::unexpected(Error{Errc::contract, ERR_SITE(), 0});
}

}
