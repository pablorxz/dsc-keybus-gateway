// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <string>
#include <vector>

namespace esphome::dsc_web {
struct DigestFields { std::string nonce, nc, uri, realm, qop; };
inline bool digest_matches_request(const DigestFields &fields,const char *uri) {
  return uri && fields.realm=="Login Required" && fields.qop=="auth" && fields.uri==uri;
}
inline bool digest_fields(const std::string &header,DigestFields &out) {
  if(header.compare(0,7,"Digest ")!=0) return false;
  std::vector<std::string> seen; size_t pos=7;
  while(pos<header.size()) {
    while(pos<header.size() && (header[pos]==' ' || header[pos]==',')) ++pos;
    if(pos==header.size()) break;
    const auto start=pos;
    while(pos<header.size() && header[pos]!='=' && header[pos]!=',' && header[pos]!=' ') ++pos;
    const auto name=header.substr(start,pos-start);
    while(pos<header.size() && header[pos]==' ') ++pos;
    if(name.empty() || pos==header.size() || header[pos++]!='=' ||
       std::find(seen.begin(),seen.end(),name)!=seen.end()) return false;
    seen.push_back(name);
    std::string value;
    if(pos<header.size() && header[pos]=='"') {
      ++pos; const auto begin=pos;
      while(pos<header.size() && header[pos]!='"') { if(header[pos]=='\\') return false; ++pos; }
      if(pos==header.size()) return false;
      value=header.substr(begin,pos-begin); ++pos;
    } else {
      const auto begin=pos; while(pos<header.size() && header[pos]!=',' && header[pos]!=' ') ++pos;
      value=header.substr(begin,pos-begin);
    }
    while(pos<header.size() && header[pos]==' ') ++pos;
    if(pos<header.size() && header[pos]!=',') return false;
    if(name=="nonce") out.nonce=value;
    else if(name=="nc") out.nc=value;
    else if(name=="uri") out.uri=value;
    else if(name=="realm") out.realm=value;
    else if(name=="qop") out.qop=value;
  }
  return !out.nonce.empty() && !out.nc.empty() && !out.uri.empty() && !out.realm.empty() && !out.qop.empty();
}
}  // namespace esphome::dsc_web
