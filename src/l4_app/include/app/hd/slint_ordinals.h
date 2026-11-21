// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/hd/hd_help.h"
#include "app/hd/hd_osd_status.h"
#include "app/hd/page_description.h"
#include "app/percent.h"
#include "app/hd/tier_set.h"
#include "infra/message_sum.h"

namespace mister::app::hd {

static_assert(Percent::of(100) == Percent{100});
static_assert(Percent::of(101) == Percent{100});

static_assert(infra::ordinal(PageDescription::RowKind::Action) == 0);
static_assert(infra::ordinal(PageDescription::RowKind::Submenu) == 1);
static_assert(infra::ordinal(PageDescription::RowKind::Value) == 2);
static_assert(infra::ordinal(PageDescription::RowKind::Separator) == 3);
static_assert(infra::ordinal(PageDescription::RowKind::Text) == 4);
static_assert(infra::ordinal(PageDescription::RowKind::Bar) == 5);

static_assert(infra::ordinal(PageDescription::RowIcon::None) == 0);
static_assert(infra::ordinal(PageDescription::RowIcon::Folder) == 1);
static_assert(infra::ordinal(PageDescription::RowIcon::Archive) == 2);
static_assert(infra::ordinal(PageDescription::RowIcon::Up) == 3);
static_assert(infra::ordinal(PageDescription::RowIcon::Recent) == 4);
static_assert(infra::ordinal(PageDescription::RowIcon::Cores) == 5);
static_assert(infra::ordinal(PageDescription::RowIcon::Bluetooth) == 6);
static_assert(infra::ordinal(PageDescription::RowIcon::Settings) == 7);

static_assert(infra::ordinal(RowTier::Place) == 0);
static_assert(infra::ordinal(RowTier::Verb) == 1);
static_assert(infra::ordinal(RowTier::Shelf) == 2);

static_assert(infra::ordinal(HdHelp::Glyph::None) == 0);
static_assert(infra::ordinal(HdHelp::Glyph::North) == 1);
static_assert(infra::ordinal(HdHelp::Glyph::East) == 2);
static_assert(infra::ordinal(HdHelp::Glyph::South) == 3);
static_assert(infra::ordinal(HdHelp::Glyph::West) == 4);
static_assert(infra::ordinal(HdHelp::Glyph::DpadLr) == 5);

static_assert(infra::ordinal(HdOsdStatus::Badge::Unknown) == 0);
static_assert(infra::ordinal(HdOsdStatus::Badge::Running) == 1);
static_assert(infra::ordinal(HdOsdStatus::Badge::Paused) == 2);

}
