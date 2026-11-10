// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/mra_stager.h"

#include <algorithm>
#include <array>
#include <utility>

#include "cores/assembled_rom.h"
#include "cores/mra_doc.h"
#include "cores/rom.h"
#include "cores/rom_bytes.h"
#include "cores/rom_set_assembler.h"
#include "hal/types.h"
#include "infra/ascii.h"
#include "infra/byte_view.h"
#include "svc/read_bounded.h"

namespace mister::cores::mra {

namespace {

class ImageFile final : public svc::IFile {
public:
    ImageFile(RomBytes bytes, std::vector<FillRun> runs) noexcept
        : bytes_(std::move(bytes)),
          runs_(std::move(runs)) {
        end_ = bytes_.extent();
        for (const FillRun& r : runs_) {
            end_ += r.len;
        }
    }

    [[nodiscard]] Ex<std::size_t> read_at(std::uint64_t off, std::span<std::byte> dst) override {
        const std::optional<RomOffset> from = end_.read_from(off);
        if (!from)
            return std::size_t{0};
        RomOffset p = *from;
        std::span<std::uint8_t> out = (end_ - p).first_of(infra::byte_cast<std::uint8_t>(dst));
        const std::size_t n = out.size();
        RomLength before{};

        const auto take = [&](std::span<const std::uint8_t> src) {
            const std::span<std::uint8_t> m = RomLength::of(src).first_of(out);
            std::ranges::copy(src.first(m.size()), m.begin());
            p += RomLength::of(m);
            out = out.subspan(m.size());
        };
        for (const FillRun& r : runs_) {
            if (out.empty())
                break;
            if (p < r.at)
                take((r.at - p).first_of(bytes_.from(p - before)));
            if (!out.empty() && p < r.end()) {
                const std::span<std::uint8_t> m = (r.end() - p).first_of(out);
                r.write_to(m, p - r.at);
                p += RomLength::of(m);
                out = out.subspan(m.size());
            }
            before += r.len;
        }
        if (!out.empty())
            take(bytes_.from(p - before));
        return n;
    }

    [[nodiscard]] Ex<std::size_t> write_at(std::uint64_t, std::span<const std::byte>) override {
        return std::unexpected(Error{Errc::io, ERR_SITE(), 0});
    }

    [[nodiscard]] Ex<svc::FileSize> size() const override {
        return (end_ - RomOffset{}).file_size();
    }

    [[nodiscard]] Ex<void> flush() override { return {}; }

private:
    RomBytes bytes_;
    std::vector<FillRun> runs_;
    RomOffset end_{};
};

}

bool MraStager::audited(std::string_view rbf) noexcept {
    return std::any_of(kAudited.begin(), kAudited.end(), [rbf](std::string_view a) {
        return infra::ieq(a, rbf);
    });
}

std::unique_ptr<IManifestStager> MraStager::of(
    std::string_view doc,
    std::string_view manifest_rel,
    const svc::Vfs& vfs
) {
    if (manifest_rel.empty())
        return nullptr;
    return std::make_unique<MraStager>(doc, manifest_rel, mame_root_of(vfs, manifest_rel), vfs);
}

struct MraStager::Assembly {
    std::vector<Rom> roms;
    std::size_t at = 0;
    RomSetAssembler assembler;
};

MraStager::MraStager(
    std::string_view doc,
    std::string_view manifest_rel,
    std::string_view mame_root,
    const svc::Vfs& vfs
)
    : vfs_(vfs) {
    auto parsed = read_arcade_document(doc, manifest_rel, vfs);
    if (!parsed) {

        const bool missing = parsed.error().code == Errc::not_found;
        error_.assign(manifest_rel);
        error_.append(missing ? "\nmanifest not found" : "\nmanifest unreadable");
        leg_ = Leg::Done;
    }
    ArcadeDocument d = parsed ? std::move(*parsed) : ArcadeDocument{};
    a_ = std::make_unique<Assembly>(
        Assembly{std::move(d.roms), 0, RomSetAssembler(vfs, mame_root)}
    );
    nvrams_ = std::move(d.nvrams);
    facts_ = std::move(d.facts);
    audited_ = audited(d.rbf);
}

MraStager::~MraStager() = default;

std::optional<ManifestAct> MraStager::next() {
    TASTY_SEAT_BODY(MraStager);
    image_.clear();
    image_runs_.clear();
    while (true) {
        switch (leg_) {
            case Leg::Roms:
                if (auto act = rom_())
                    return act;
                finish_();
                leg_ = refused_ ? Leg::Done : Leg::Dips;
                break;
            case Leg::Dips:
                leg_ = Leg::Done;

                if (!facts_.switches.rows.empty()) {
                    const auto pack = facts_.dip_cur.le_bytes();
                    return SendBracket{.index = kDipIndex, .bytes = {pack.begin(), pack.end()}};
                }
                break;
            case Leg::Done:
                return std::nullopt;
        }
    }
}

std::optional<ManifestAct> MraStager::rom_() {
    if (window_) {
        const Window w = *std::exchange(window_, std::nullopt);
        if (w.torn) {

            ++counts_.ddr_refusals;
            ++counts_.discarded;
        } else {
            ++counts_.sent;
            ++counts_.staged;
            return SendBracket{.index = w.index, .bytes = {}, .aux = w.bytes};
        }
    }
    auto& roms = a_->roms;
    while (true) {

        if (nvram_next_ < nvrams_.size() && nvrams_[nvram_next_].after <= a_->at) {
            const NvramDecl d = nvrams_[nvram_next_++];
            if (auto act = nvram_(d))
                return act;
            continue;
        }
        if (a_->at >= roms.size())
            break;
        const Rom& rom = roms[a_->at++];
        AssembledRom out = a_->assembler.assemble(rom);
        const RomLength len = out.length();
        if (!out.send || len.empty()) {
            ++counts_.discarded;
            continue;
        }
        if (rom.address != hal::FabricAddr{} && !audited_) {
            ++counts_.ddr_refusals;
            ++counts_.discarded;
            continue;
        }
        if (!out.index) {

            ++counts_.discarded;
            continue;
        }
        const proto::WideIoIndex index = *out.index;
        const std::uint32_t bytes = len.v;
        if (rom.address != hal::FabricAddr{}) {
            window_ = Window{.index = index, .bytes = bytes};
            image_ = std::move(out.bytes);
            image_runs_ = std::move(out.runs);
            return StageWindow{.index = index, .address = rom.address, .bytes = bytes};
        }
        if (out.runs.empty()) {
            ++counts_.sent;

            std::vector<std::uint8_t> wire(out.bytes.begin(), out.bytes.end());
            return SendBracket{.index = index, .bytes = std::move(wire)};
        }
        if (bytes > kPlainFillMax) {

            ++fill_refusals_;
            ++counts_.discarded;
            refused_ = Errc::bad_format;
            return std::nullopt;
        }
        ++counts_.sent;

        std::vector<std::uint8_t> flat(bytes);
        ImageFile f(std::move(out.bytes), std::move(out.runs));
        (void)f.read_at(0, std::as_writable_bytes(std::span<std::uint8_t>(flat)));
        return SendBracket{.index = index, .bytes = std::move(flat)};
    }
    return std::nullopt;
}

std::optional<ManifestAct> MraStager::nvram_(const NvramDecl& d) {
    if (!d.save)
        return std::nullopt;
    auto f = vfs_.open(std::string("config/nvram/").append(facts_.nvm_name), svc::OpenMode::Read);
    if (!f)
        return std::nullopt;
    std::vector<std::uint8_t> buf(d.save->size.length_of(proto::BlockCount{1}), 0);
    auto read = svc::read_bounded(**f, buf.size());
    if (!read)
        return std::nullopt;
    const auto got = infra::byte_cast<const std::uint8_t>(std::span(*read));
    std::copy(got.begin(), got.end(), buf.begin());
    ++counts_.nvram_loads;
    return SendBracket{.index = d.save->index, .bytes = std::move(buf)};
}

void MraStager::finish_() noexcept {
    const auto& a = a_->assembler;
    counts_.repeat_refusals = a.repeat_refusals() + fill_refusals_;
    if (!a.valid_rom0() && !a.error().empty())
        error_.assign(a.error());
}

std::unique_ptr<svc::IFile> MraStager::take_image() {
    TASTY_SEAT_BODY(MraStager);
    return std::make_unique<ImageFile>(std::exchange(image_, {}), std::exchange(image_runs_, {}));
}

void MraStager::mark_staged() noexcept {
    TASTY_SEAT_BODY(MraStager);
    if (window_)
        window_->staged = true;
}

void MraStager::mark_torn() noexcept {
    TASTY_SEAT_BODY(MraStager);
    if (window_)
        window_->torn = true;
}

}
