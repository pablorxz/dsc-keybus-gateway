// Native, offline tests for the policy used around ESPHome Digest validation.
#include "components/dsc_web/digest_nonce.h"
#include "components/dsc_web/digest_fields.h"
#include <cassert>
#include <iostream>
using namespace esphome::dsc_web;
int main() {
  const char *n="0123456789abcdef0123456789abcdef";
  DigestNonces g; g.issue(n,100);
  assert(g.accept(n,"00000001",101));
  assert(!g.accept(n,"00000001",102));
  assert(g.accept(n,"00000003",103));
  assert(g.accept(n,"00000002",104)); // reordered requests are valid once
  assert(!g.accept(n,"00000002",105));
  assert(!g.accept(n,"00000000",106));
  assert(!g.accept(n,"0000000g",106));
  assert(!g.accept("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","00000001",106));
  assert(!g.accept(n,"00000004",100+DigestNonces::LIFETIME_MS));
  DigestNonces wrap; wrap.issue(n,0xfffffff0u);
  assert(wrap.accept(n,"00000001",5));
  assert(wrap.accept(n,"00000050",6));
  assert(!wrap.accept(n,"00000001",7)); // outside replay window
  DigestNonces bounded; bounded.issue(n,1);
  for(int i=0;i<8;++i) bounded.issue("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",2);
  assert(!bounded.accept(n,"00000001",3)); // old challenges are evicted
  DigestFields f;
  assert(digest_fields("Digest username=\"user\", realm=\"Login Required\", nonce=\"0123456789abcdef0123456789abcdef\", uri=\"/api/state?a=1\", qop=auth, nc=00000001, cnonce=\"x\", response=\"y\"",f));
  assert(f.uri=="/api/state?a=1" && f.qop=="auth" && f.nc=="00000001");
  assert(digest_matches_request(f,"/api/state?a=1"));
  assert(!digest_matches_request(f,"/api/command"));
  assert(!digest_matches_request(f,"/api/state?a=2"));
  f.qop="auth-int"; assert(!digest_matches_request(f,"/api/state?a=1"));
  DigestFields duplicate;
  assert(!digest_fields("Digest nonce=\"a\", nonce=\"b\", nc=00000001, uri=\"/\", realm=\"Login Required\", qop=auth",duplicate));
  DigestFields malformed;
  assert(!digest_fields("Digest nonce=\"unterminated",malformed));
  std::cout << "PASS Digest nonce replay, expiry, counter ordering and parser\n";
}
