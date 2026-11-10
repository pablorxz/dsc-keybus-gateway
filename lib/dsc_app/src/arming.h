// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "operation.h"
namespace dsc::app {
bool target_matches(const Command &command, const PanelState &state);
bool valid_code(const std::array<char,7> &code, bool required);
OperationReason validate_command(const Command &command, const PanelState &state);
}  // namespace dsc::app
