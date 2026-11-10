// SPDX-License-Identifier: GPL-3.0-or-later
// Bounded anti-replay state for HTTP Digest. Called from the single HTTP task.
#pragma once
#include <array>
#include <cstdint>
#include <cstring>

namespace esphome::dsc_web {
class DigestNonces {
 public:
  static constexpr std::uint32_t LIFETIME_MS = 300000;
  void issue(const char *nonce, std::uint32_t now) {
    auto &entry=entries_[next_]; next_=(next_+1)%entries_.size();
    entry={}; std::memcpy(entry.nonce.data(),nonce,32); entry.issued=now; entry.used=true;
  }
  bool accept(const char *nonce, const char *nc, std::uint32_t now) {
    if(std::strlen(nonce)!=32 || std::strlen(nc)!=8) return false;
    std::uint32_t count=0;
    for(unsigned i=0;i<8;i++) {
      const char c=nc[i]; unsigned value;
      if(c>='0' && c<='9') value=c-'0';
      else if(c>='a' && c<='f') value=c-'a'+10;
      else if(c>='A' && c<='F') value=c-'A'+10;
      else return false;
      count=(count<<4)|value;
    }
    if(count==0) return false;
    for(auto &entry:entries_) {
      if(!entry.used || std::memcmp(entry.nonce.data(),nonce,32)!=0) continue;
      if(static_cast<std::uint32_t>(now-entry.issued)>=LIFETIME_MS) return false;
      if(count>entry.highest) {
        const auto shift=count-entry.highest;
        entry.window=shift>=64?1:(entry.window<<shift)|1;
        entry.highest=count; return true;
      }
      const auto offset=entry.highest-count;
      if(offset>=64 || (entry.window & (std::uint64_t{1}<<offset))) return false;
      entry.window|=std::uint64_t{1}<<offset; return true;
    }
    return false;
  }
 private:
  struct Entry { std::array<char,33> nonce{}; std::uint32_t issued{0},highest{0}; std::uint64_t window{0}; bool used{false}; };
  std::array<Entry,8> entries_{};
  std::size_t next_{0};
};
}  // namespace esphome::dsc_web
