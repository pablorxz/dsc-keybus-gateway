// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "operation.h"
namespace dsc::app {
bool valid_clock(const PanelClock &clock);
bool format_clock_sequence(const Command &command, char (&output)[33]);
}  // namespace dsc::app
