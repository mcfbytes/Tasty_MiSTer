// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <string_view>
#include <tuple>
#include <type_traits>

#include "app/link_session.h"
#include "infra/message_sum.h"
#include "proto/link_event.h"
#include "proto/spi_block_decoder.h"
#include "proto/spi_conf_str_decoder.h"
#include "proto/spi_identify_decoder.h"
#include "proto/spi_osd_mask_decoder.h"
#include "proto/spi_ps2_decoder.h"
#include "proto/spi_sampler.h"

namespace mister::app {

template <proto::LinkEvent::Kind K, const std::string_view& OwedBy>
struct Unproduced {
    static constexpr proto::LinkEvent::Kind kKind = K;
    static constexpr std::string_view kOwedBy = OwedBy;
};

using LinkEventProducers =
    std::tuple<LinkSession, proto::SpiIdentifyDecoder, proto::SpiConfStrDecoder, proto::SpiSampler,
               proto::SpiBlockDecoder, proto::SpiPs2Decoder, proto::SpiOsdMaskDecoder>;

namespace detail {
template <class T>
struct IsUnproduced : std::false_type {};
template <proto::LinkEvent::Kind K, const std::string_view& O>
struct IsUnproduced<Unproduced<K, O>> : std::true_type {};
}  // namespace detail

template <class List>
consteval bool producers_cover_each_kind_once() {
    std::array<unsigned, infra::ordinal(proto::LinkEvent::Kind::kCount)> seen{};
    auto port = [&]<class... A>(std::tuple<A...>*) { (++seen[infra::ordinal(A::kKind)], ...); };
    auto one = [&]<class P>(P*) {
        if constexpr (detail::IsUnproduced<P>::value)
            ++seen[infra::ordinal(P::kKind)];
        else
            port(static_cast<typename P::Port::Emits*>(nullptr));
    };
    [&]<class... P>(std::tuple<P...>*) {
        (one(static_cast<P*>(nullptr)), ...);
    }(static_cast<List*>(nullptr));
    for (unsigned n : seen)
        if (n != 1) return false;
    return true;
}
static_assert(producers_cover_each_kind_once<LinkEventProducers>(),
              "every LinkEvent kind is in exactly one producer's Port or one Unproduced entry");

}  // namespace mister::app
