// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/mra_stager.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

#include "cores/assembled_rom.h"
#include "cores/mra_doc.h"
#include "cores/rom.h"
#include "cores/rom_bytes.h"
#include "cores/rom_set_assembler.h"
#include "infra/ascii.h"
#include "infra/byte_view.h"

namespace mister::cores::mra {

namespace {

class ImageFile final : public svc::IFile {
public:
    ImageFile(RomBytes bytes, std::vector<FillRun> runs) noexcept
        : bytes_(std::move(bytes)),
          runs_(std::move(runs)) {
        size_ = bytes_.size();
        for (const FillRun& r : runs_) {
            size_ += r.len;
        }
    }

    [[nodiscard]] Ex<std::size_t> read_at(std::uint64_t off, std::span<std::byte> dst) override {
        if (off >= size_)
            return std::size_t{0};
        const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(dst.size(), size_ - off));
        auto* out = infra::byte_cast<std::uint8_t>(dst).data();
        std::uint64_t p = off;
        std::uint64_t before = 0;
        std::size_t k = 0;
        for (const FillRun& r : runs_) {
            if (k == n)
                break;
            if (p < r.at) {
                const auto m = static_cast<std::size_t>(std::min<std::uint64_t>(n - k, r.at - p));
                std::memcpy(out + k, bytes_.data() + static_cast<std::size_t>(p - before), m);
                k += m;
                p += m;
            }
            const std::uint64_t end = std::uint64_t{r.at} + r.len;
            if (k < n && p < end) {
                const auto m = static_cast<std::size_t>(std::min<std::uint64_t>(n - k, end - p));
                if (r.pattern.size() == 1) {
                    std::memset(out + k, r.pattern[0], m);
                } else {
                    const auto ph = static_cast<std::size_t>(p - r.at);
                    for (std::size_t i = 0; i < m; ++i) {
                        out[k + i] = r.pattern[(ph + i) % r.pattern.size()];
                    }
                }
                k += m;
                p += m;
            }
            before += r.len;
        }
        if (k < n)
            std::memcpy(out + k, bytes_.data() + static_cast<std::size_t>(p - before), n - k);
        return n;
    }

    [[nodiscard]] Ex<std::size_t> write_at(std::uint64_t, std::span<const std::byte>) override {
        return std::unexpected(Error{Errc::io, ERR_SITE(), 0});
    }

    [[nodiscard]] Ex<svc::FileSize> size() const override { return svc::FileSize{size_}; }

    [[nodiscard]] Ex<void> flush() override { return {}; }

    svc::FileKind kind() const noexcept override { return svc::FileKind::Memory; }

private:
    RomBytes bytes_;
    std::vector<FillRun> runs_;
    std::uint64_t size_ = 0;
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
    ArcadeDocument d = read_arcade_document(doc, manifest_rel, vfs);
    a_ = std::make_unique<Assembly>(
        Assembly{std::move(d.roms), 0, RomSetAssembler(vfs, mame_root)}
    );
    nvrams_ = std::move(d.nvrams);
    facts_ = std::move(d.facts);
    audited_ = audited(d.rbf);
    if (!facts_.error.empty()) {
        error_ = facts_.error;
        leg_ = Leg::Done;
    }
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
                    const auto pack = dip_pack(facts_.dip_cur);
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
        if (!out.send || out.size() == 0) {
            ++counts_.discarded;
            continue;
        }
        if (rom.address != 0 && !audited_) {
            ++counts_.ddr_refusals;
            ++counts_.discarded;
            continue;
        }
        if (out.index < 0 || out.index > 255) {

            ++counts_.discarded;
            continue;
        }
        const auto index = static_cast<std::uint8_t>(out.index);
        const auto bytes = static_cast<std::uint32_t>(out.size());
        if (rom.address != 0) {
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
    if (!nvram_declared(d.index, d.size))
        return std::nullopt;
    auto f = vfs_.open(std::string("config/nvram/").append(facts_.nvm_name), svc::OpenMode::Read);
    if (!f)
        return std::nullopt;
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(d.size), 0);
    std::size_t got = 0;
    while (got < buf.size()) {
        auto r = (*f)->read_at(
            got,
            std::as_writable_bytes(std::span<std::uint8_t>(buf)).subspan(got)
        );
        if (!r || *r == 0)
            break;
        got += *r;
    }
    ++counts_.nvram_loads;
    return SendBracket{.index = static_cast<std::uint8_t>(d.index), .bytes = std::move(buf)};
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
