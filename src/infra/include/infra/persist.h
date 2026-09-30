// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#define TASTY_PERSIST(tier, name) __attribute__((section(".tasty_persist." #tier "." #name), used))
