// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/disc_frame_source.h"

#include <algorithm>
#include <cstring>

#include "infra/prelude.h"

namespace mister::app {

void DiscFrameSource::bind(const SectorGrantBody& body) noexcept {
    program_ = body.program;
    frame_words_ = body.frame_words;
}

void DiscFrameSource::unbind() noexcept {
    program_ = {};
    frame_words_ = hal::ocram::Words{};
    drop_ahead_();
}

bool DiscFrameSource::mount_changed() noexcept {
    const auto g = geom_rd_.take_if_changed(w_.geometry);
    if (!g)
        return false;
    disc_.emplace(*g);
    drop_ahead_();
    return true;
}

void DiscFrameSource::drop_ahead_() noexcept {
    for (Ahead& a : ahead_)
        a.live = false;
    ahead_done_ = kReadAhead;
}

std::span<const std::byte> DiscFrameSource::frame_for(proto::Lba lba) noexcept {
    fill_frame_(lba);
    return frame_view_();
}

void DiscFrameSource::read_ahead_from(proto::Lba served) noexcept {
    ahead_from_ = served + proto::BlockCount{1u};
    ahead_done_ = 0;
}

svc::disc::ISectorProgram::Source DiscFrameSource::answer_(proto::Lba lba) const noexcept {
    return program_->answer(*disc_, lba);
}

std::span<std::byte> DiscFrameSource::frame_view_() noexcept {
    return frame_words_.bytes().first_in(std::span<std::byte>{frame_.data});
}

void DiscFrameSource::fill_frame_(proto::Lba lba) noexcept {
    const std::span<std::byte> view = frame_view_();
    if (!program_) {
        std::memset(view.data(), 0, view.size());
        return;
    }
    const svc::disc::ISectorProgram::Source src = answer_(lba);
    if (!src.engine) {
        std::memset(view.data(), src.fill, view.size());
        return;
    }
    if (take_ahead_(lba))
        return;
    read_engine_(frame_, lba);
    const infra::ByteCount got = frame_.ok != 0 ? infra::ByteCount{frame_.got} : infra::ByteCount{};
    if (!got.covers(view)) {
        const std::span<std::byte> rest = got.past_in(view);
        std::memset(rest.data(), src.fill, rest.size());
    }
}

void DiscFrameSource::read_engine_(svc::disc::DiscReadSlot& s, proto::Lba lba) noexcept {
    s.gen = svc::DiscMountGen{};
    s.lba = lba;
    s.form = program_->form();
    s.want = static_cast<std::uint16_t>(svc::disc::disc_form_bytes(s.form));
    s.got = 0;
    s.ok = false;
    w_.discs.serve_on_io(s);
}

bool DiscFrameSource::take_ahead_(proto::Lba lba) noexcept {
    for (Ahead& a : ahead_) {
        if (!a.live || a.slot.lba != lba)
            continue;
        a.live = false;
        if (!a.slot.ok)
            return false;
        std::memcpy(frame_.data, a.slot.data, a.slot.got);
        frame_.got = a.slot.got;
        frame_.ok = true;
        return infra::ByteCount{frame_.got}.covers(frame_view_());
    }
    return false;
}

bool DiscFrameSource::read_ahead_one() noexcept {
    if (!program_)
        return false;
    while (ahead_done_ < kReadAhead) {
        const proto::BlockCount n{static_cast<std::uint32_t>(ahead_done_++)};
        const proto::Lba lba = ahead_from_ + n;
        if (!answer_(lba).engine)
            continue;
        const bool held = std::ranges::any_of(ahead_, [lba](const Ahead& a) noexcept {
            return a.live && a.slot.lba == lba;
        });
        if (held)
            continue;
        Ahead& a = ahead_[ahead_next_];
        ahead_next_ = (ahead_next_ + 1u) % kReadAhead;
        read_engine_(a.slot, lba);
        a.live = true;
        return true;
    }
    return false;
}

}
