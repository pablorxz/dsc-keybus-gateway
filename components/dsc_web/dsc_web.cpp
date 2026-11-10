// SPDX-License-Identifier: GPL-3.0-or-later
#include "dsc_web.h"
#include "generated_assets.h"
#include "esphome/components/json/json_util.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>
#include <cstdio>
#include "digest_fields.h"
#include <esp_random.h>
#ifdef DSC_ENABLE_REPLAY
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

namespace esphome::dsc_web {
namespace {
std::string path(AsyncWebServerRequest *request) {
  char buffer[AsyncWebServerRequest::URL_BUF_SIZE];
  const auto value=request->url_to(buffer);
  return std::string(value.c_str(),value.size());
}
void send(AsyncWebServerRequest *request,int status,const char *type,const std::string &content) {
  auto *response=request->beginResponse(status,type,content);
  response->addHeader("Cache-Control","no-store");
  response->addHeader("X-Content-Type-Options","nosniff");
  response->addHeader("X-Frame-Options","DENY");
  request->send(response);
}
void asset(AsyncWebServerRequest *request,const char *type,const std::uint8_t *data,std::size_t size) {
  auto *response=request->beginResponse(200,type,data,size);
  response->addHeader("Content-Encoding","gzip");
  response->addHeader("Cache-Control","no-store");
  response->addHeader("X-Content-Type-Options","nosniff");
  response->addHeader("X-Frame-Options","DENY");
  response->addHeader("Content-Security-Policy","default-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'");
  request->send(response);
}
bool parse_number(const std::string &text,unsigned &result) {
  if(text.empty() || text.size()>10 || !std::all_of(text.begin(),text.end(),[](char c){return c>='0' && c<='9';})) return false;
  char *end=nullptr; errno=0; const auto value=std::strtoul(text.c_str(),&end,10);
  if(errno==ERANGE || !end || *end || value>0xffffffffUL) return false;
  result=static_cast<unsigned>(value); return true;
}
bool same_origin_write(AsyncWebServerRequest *request) {
  const auto marker=request->get_header("X-DSC-Request");
  if(!marker.has_value() || *marker!="1") return false;
  const auto content_type=request->get_header("Content-Type");
  if(!content_type.has_value() || content_type->find("application/x-www-form-urlencoded")!=0) return false;
  // Query parameters are forbidden for writes: codes stay out of URLs/logs.
  if(httpd_req_get_url_query_len(*request)!=0 || request->contentLength()>128) return false;
  const auto origin=request->get_header("Origin");
  if(!origin.has_value()) return true;  // Non-browser clients still need the custom header and authentication.
  const auto host=request->get_header("Host");
  return host.has_value() && *origin==std::string("http://")+*host;
}
void failure(AsyncWebServerRequest *request,int code,const char *reason) {
  const auto json=json::build_json([reason](JsonObject root) { root["reason"]=reason; });
  send(request,code,"application/json",std::string(json));
}
}  // namespace

void DscWeb::setup() {
  // This handler performs Digest authentication with bounded replay protection.
  base_->add_handler_without_auth(this);
  base_->init();
}
void DscWeb::challenge(AsyncWebServerRequest *request,bool stale) {
  unsigned char random[16]; esp_fill_random(random,sizeof random);
  static constexpr char hex[]="0123456789abcdef"; char nonce[33]{};
  for(unsigned i=0;i<16;++i) { nonce[i*2]=hex[random[i]>>4]; nonce[i*2+1]=hex[random[i]&15]; }
  nonces_.issue(nonce,millis());
  char header[128]; std::snprintf(header,sizeof header,"Digest realm=\"Login Required\", qop=\"auth\", nonce=\"%s\"%s",nonce,stale?", stale=true":"");
  httpd_resp_set_hdr(*request,"WWW-Authenticate",header);
  httpd_resp_send_err(*request,HTTPD_401_UNAUTHORIZED,nullptr);
}
bool DscWeb::authenticate(AsyncWebServerRequest *request) {
  const auto authorization=request->get_header("Authorization"); DigestFields fields;
  httpd_req_t *raw=*request;
  if(!username_ || !password_ || !authorization.has_value() ||
     !digest_fields(*authorization,fields) || !digest_matches_request(fields,raw->uri) ||
     !request->authenticate(username_,password_)) {
    challenge(request); return false;
  }
  if(!nonces_.accept(fields.nonce.c_str(),fields.nc.c_str(),millis())) {
    challenge(request,true); return false;
  }
  return true;
}
bool DscWeb::canHandle(AsyncWebServerRequest *request) const {
  const auto url=path(request);
  if(request->method()==HTTP_GET)
    return url=="/" || url=="/assets/app.css" || url=="/assets/app.js" || url=="/api/state" || url=="/api/operation" || url=="/api/activity";
  if(request->method()==HTTP_POST)
    return url=="/api/command" || url=="/api/operation/code" || url=="/api/simulation/zone" || url=="/api/simulation/online";
  return false;
}
void DscWeb::handleRequest(AsyncWebServerRequest *request) {
  if(!authenticate(request)) return;
  const auto url=path(request);
  if(url=="/") return asset(request,"text/html; charset=utf-8",dsc_web_assets::index,sizeof dsc_web_assets::index);
  if(url=="/assets/app.css") return asset(request,"text/css; charset=utf-8",dsc_web_assets::css,sizeof dsc_web_assets::css);
  if(url=="/assets/app.js") return asset(request,"application/javascript; charset=utf-8",dsc_web_assets::js,sizeof dsc_web_assets::js);
  if(url=="/api/state") return state(request);
  if(url=="/api/operation") return operation(request);
  if(url=="/api/activity") return activity(request);
  if(!same_origin_write(request)) return failure(request,400,"invalid_request");
  if(url=="/api/command") return command(request);
  if(url=="/api/operation/code") return operation_code(request);
  if(url=="/api/simulation/zone") return simulation(request,true);
  if(url=="/api/simulation/online") return simulation(request,false);
  failure(request,404,"not_found");
}
void DscWeb::state(AsyncWebServerRequest *request) {
  const auto observed=parent_->service().snapshot();
  // The IDF HTTP task has a 4512-byte stack. A 64-zone catalog contains many
  // pointers; keep the bounded copy on the heap rather than its small stack.
  std::unique_ptr<dsc::app::ZoneCatalog> catalog(new (std::nothrow) dsc::app::ZoneCatalog);
  if (!catalog) return failure(request,503,"out_of_memory");
  parent_->service().copy_zone_catalog(*catalog);
  const auto document=json::build_json([&](JsonObject root) {
    root["version"]=observed.version; root["connected"]=observed.connected; root["simulated"]=observed.simulated;
    if(observed.connected) root["bus_activity_ms"]=observed.bus_activity_ms;
    else root["bus_activity_ms"]=nullptr;
    if(observed.connected && observed.trouble_known) root["trouble"]=observed.trouble;
    else root["trouble"]=nullptr;
    auto partitions=root["partitions"].to<JsonArray>();
    for(unsigned i=0;i<observed.partitions.size();++i) {
      auto output=partitions.add<JsonObject>(); const auto &item=observed.partitions[i];
      output["number"]=i+1; output["known"]=observed.connected && item.known && !item.disabled;
      output["state"]=dsc::app::state_name(dsc::app::describe_partition(observed,i+1).state);
      if(observed.connected && item.known && !item.disabled) output["keypad_status"]=item.status;
      else output["keypad_status"]=nullptr;
      output["ready"]=item.ready;
      if(observed.connected && item.fire_known) output["fire"]=item.fire;
      else output["fire"]=nullptr;
    }
    auto zones=root["zones"].to<JsonArray>();
    for(const auto &zone:catalog->zones()) if(zone.number) {
      const auto index=zone.number-1, group=index/8; auto output=zones.add<JsonObject>();
      output["number"]=zone.number; output["name"]=zone.name; output["group"]=zone.group; output["category"]=zone.category;
      if(observed.connected && (observed.known_zone_groups & (1U<<group)))
        output["open"]=bool(observed.open_zones[group] & (1U<<(index%8)));
      else output["open"]=nullptr;
    }
  });
  send(request,200,"application/json",std::string(document));
#ifdef DSC_ENABLE_REPLAY
  static std::uint32_t last_stack_report=0;
  const auto now=millis();
  if (last_stack_report==0 || static_cast<std::uint32_t>(now-last_stack_report)>=60000) {
    last_stack_report=now;
    ESP_LOGI("dsc_web", "bench_http_stack_min_bytes=%u", unsigned(uxTaskGetStackHighWaterMark(nullptr)));
  }
#endif
}
void DscWeb::operation(AsyncWebServerRequest *request) {
  unsigned id=0;
  if(!parse_number(request->arg("id"),id) || id==0) return failure(request,400,"invalid_id");
  const auto view=parent_->service().operation(id);
  const auto document=json::build_json([&](JsonObject root) {
    root["id"]=view.id; root["status"]=dsc::app::operation_status_name(view.status);
    root["reason"]=dsc::app::operation_reason_name(view.reason);
  });
  send(request,200,"application/json",std::string(document));
}
void DscWeb::activity(AsyncWebServerRequest *request) {
  std::unique_ptr<dsc::app::ActivitySnapshot> events(new (std::nothrow) dsc::app::ActivitySnapshot);
  if(!events) return failure(request,503,"out_of_memory");
  parent_->service().copy_activity(*events);
  const auto document=json::build_json([&](JsonObject root) {
    auto list=root["events"].to<JsonArray>();
    for(std::size_t i=0;i<events->count;++i) {
      const auto &event=events->events[i]; auto item=list.add<JsonObject>();
      item["kind"]=dsc::app::activity_kind_name(event.kind);
      item["number"]=event.number;
      item["age_seconds"]=(events->now_ms-event.time_ms)/1000;
    }
  });
  send(request,200,"application/json",std::string(document));
}
void DscWeb::operation_code(AsyncWebServerRequest *request) {
  unsigned id=0;
  if(!parse_number(request->arg("id"),id) || !id) return failure(request,400,"invalid_operation");
  dsc::app::OperationReason reason;
  if(request->arg("action")=="cancel") reason=parent_->service().abandon_code(id);
  else if(request->arg("action")=="provide") {
    std::array<char,7> code{}; auto input=request->arg("code");
    if(input.size()>=code.size()) { std::fill(input.begin(),input.end(),0); return failure(request,400,"invalid_code"); }
    std::copy(input.begin(),input.end(),code.begin()); std::fill(input.begin(),input.end(),0);
    reason=parent_->service().provide_code(id,code,millis());
    volatile char *wipe=code.data(); for(unsigned i=0;i<code.size();++i) wipe[i]=0;
  } else return failure(request,400,"invalid_action");
  if(reason!=dsc::app::OperationReason::None) return failure(request,409,dsc::app::operation_reason_name(reason));
  send(request,200,"application/json","{\"status\":\"accepted\"}");
}
void DscWeb::command(AsyncWebServerRequest *request) {
  const auto kind=request->arg("kind"); unsigned partition=0;
  if(!parse_number(request->arg("partition"),partition) || partition<1 || partition>8)
    return failure(request,400,"invalid_partition");
  dsc::app::Command item; item.partition=partition;
  if(kind=="arm_away") item.kind=dsc::app::CommandKind::ArmAway;
  else if(kind=="arm_home") item.kind=dsc::app::CommandKind::ArmStay;
  else if(kind=="arm_night") item.kind=dsc::app::CommandKind::ArmNight;
  else if(kind=="disarm") item.kind=dsc::app::CommandKind::Disarm;
  else if(kind=="keypad") item.kind=dsc::app::CommandKind::Keypad;
  else return failure(request,400,"invalid_command");
  if(item.kind==dsc::app::CommandKind::Keypad) {
    auto key=request->arg("key");
    if(key.size()!=1 || !((key[0]>='0' && key[0]<='9') || key[0]=='*' || key[0]=='#'))
      return failure(request,400,"invalid_key");
    item.keys[0]=key[0];
    std::fill(key.begin(),key.end(),0);
  }
  auto code=request->arg("code");
  if(code.size()>=item.code.size()) { std::fill(code.begin(),code.end(),0); return failure(request,400,"invalid_code"); }
  std::copy(code.begin(),code.end(),item.code.begin()); std::fill(code.begin(),code.end(),0);
  const auto submitted=parent_->service().submit(item,dsc::app::CommandSource::Http,millis());
  volatile char *wipe=item.code.data(); for(unsigned i=0;i<item.code.size();++i) wipe[i]=0;
  volatile char *wipe_key=item.keys.data(); for(unsigned i=0;i<item.keys.size();++i) wipe_key[i]=0;
  if(!submitted.id) return failure(request,409,dsc::app::operation_reason_name(submitted.reason));
  const auto document=json::build_json([&](JsonObject root) { root["id"]=submitted.id; root["status"]="queued"; });
  // IDF's response adapter supports 200; operation state is explicitly queued.
  send(request,200,"application/json",std::string(document));
}
void DscWeb::simulation(AsyncWebServerRequest *request,bool zone) {
#ifdef DSC_ENABLE_REPLAY
  const auto value=request->arg(zone ? "open" : "online");
  if(value!="0" && value!="1") return failure(request,400,"invalid_value");
  bool accepted=false;
  if(zone) {
    unsigned number=0;
    if(!parse_number(request->arg("zone"),number) || number<1 || number>64) return failure(request,400,"invalid_zone");
    accepted=parent_->schedule_simulated_zone(number,value=="1");
  } else accepted=parent_->schedule_simulated_online(value=="1");
  if(accepted) return send(request,200,"application/json","{\"status\":\"queued\"}");
#endif
  failure(request,404,"not_available");
}
}  // namespace esphome::dsc_web
