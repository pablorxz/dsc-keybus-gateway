// SPDX-License-Identifier: GPL-3.0-or-later
#include "keypad_session.h"
namespace dsc::app {
bool valid_keypad_sequence(const std::array<char,33> &keys) {
  if (keys[0]==0) return false;
  for (unsigned i=0; i<keys.size(); ++i) {
    if (keys[i]==0) return true;
    if (!((keys[i]>='0' && keys[i]<='9') || keys[i]=='*' || keys[i]=='#')) return false;
  }
  return false;
}
}  // namespace dsc::app
