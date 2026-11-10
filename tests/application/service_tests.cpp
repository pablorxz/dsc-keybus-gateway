#include "alarm_service.h"
#include "scripted_panel.h"
#include "fake_arduino.h"
#include <functional>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>

using namespace dsc;
using namespace dsc::app;
namespace {
void check(bool value,const char *message) { if(!value) throw std::runtime_error(message); }
struct Rig {
  Keybus bus{22,21};
  bench::ScriptedPanel model{bus};
  AlarmService service{bus};
  Rig() { check(model.begin(),"replay begin failed"); service.tick(millis()); }
  ~Rig() { bus.stop(); }
  void tick(unsigned ms=20) {
    fake::advance(ms*1000); bus.loop(); model.step(millis()); service.tick(millis());
  }
  OperationView finish(std::uint32_t id,unsigned ticks=1000) {
    check(id!=0,"command was not admitted");
    for(unsigned i=0;i<ticks;++i) { tick(); const auto op=service.operation(id); if(op.terminal()) return op; }
    const auto view=service.operation(id);
    throw std::runtime_error(std::string("operation did not terminate: ")+command_name(view.kind)+"/"+
                             operation_status_name(view.status)+", observed="+state_name(service.partition(view.partition).state));
  }
};
Command command(CommandKind kind,unsigned partition=1,const char *code="2468") {
  Command c; c.kind=kind; c.partition=partition;
  std::copy_n(code,std::min<std::size_t>(std::strlen(code),6),c.code.begin()); return c;
}
void complete_cycle(CommandKind kind,AlarmState target) {
  Rig rig;
  auto submitted=rig.service.submit(command(kind),CommandSource::HomeAssistant,millis());
  check(rig.service.partition(1).state==AlarmState::Disarmed,"request published as armed before confirmation");
  check(rig.finish(submitted.id).status==OperationStatus::Succeeded,"arming not confirmed");
  check(rig.service.partition(1).state==target,"wrong observed arm mode");
  submitted=rig.service.submit(command(CommandKind::Disarm),CommandSource::Http,millis());
  check(rig.finish(submitted.id).status==OperationStatus::Succeeded,"disarm not confirmed");
  check(rig.service.partition(1).state==AlarmState::Disarmed,"disarm state wrong");
}
void code_prompt(CommandKind kind,CommandSource source) {
  Rig rig; auto c=command(kind,1,"");
  auto id=rig.service.submit(c,source,millis()).id;
  check(id!=0,"arm without code not admitted");
  for(unsigned i=0;i<500 && rig.service.operation(id).status!=OperationStatus::AwaitingCode && !rig.service.operation(id).terminal();++i)rig.tick();
  auto op=rig.service.operation(id);
  if(source==CommandSource::HomeAssistant) {
    check(op.status==OperationStatus::Rejected && op.reason==OperationReason::CodeRequired,"HA missing-code behavior incorrect"); return;
  }
  check(op.status==OperationStatus::AwaitingCode,"HTTP did not wait for code");
  auto good=command(kind).code; auto bad=command(kind,1,"123").code;
  check(rig.service.provide_code(id,bad,millis())==OperationReason::InvalidCode,"invalid continuation code accepted");
  check(rig.service.provide_code(id+1,good,millis())==OperationReason::Busy,"wrong operation continued");
  check(rig.service.provide_code(id,good,millis()+c.timeout_ms)==OperationReason::NoConfirmation,"expired code continuation accepted");
  check(rig.service.provide_code(id,good,millis())==OperationReason::None,"code continuation rejected");
  check(rig.service.provide_code(id,good,millis())==OperationReason::Busy,"code continuation replay accepted");
  check(rig.finish(id).status==OperationStatus::Succeeded,"continued arm not confirmed");
  check(rig.model.trace_size()==5,"arm key or code was transmitted more than once");
}
void abandon_code_prompt() {
  Rig rig; auto c=command(CommandKind::ArmNight,1,""); auto id=rig.service.submit(c,CommandSource::Http,millis()).id;
  for(unsigned i=0;i<500 && rig.service.operation(id).status!=OperationStatus::AwaitingCode;++i)rig.tick();
  check(rig.service.abandon_code(id)==OperationReason::None,"abandon rejected");
  check(rig.service.operation(id).status==OperationStatus::Uncertain,"abandoned operation treated as confirmed");
  check(rig.service.provide_code(id,command(CommandKind::ArmNight).code,millis())==OperationReason::Busy,"abandoned operation accepted code");
}
void code_prompt_timeout() {
  Rig rig; auto c=command(CommandKind::ArmNight,1,""); c.timeout_ms=4000;
  auto id=rig.service.submit(c,CommandSource::Http,millis()).id;
  for(unsigned i=0;i<150 && rig.service.operation(id).status!=OperationStatus::AwaitingCode;++i)rig.tick();
  check(rig.service.operation(id).status==OperationStatus::AwaitingCode,"panel prompt not reached");
  rig.tick(4500);
  check(rig.service.operation(id).status==OperationStatus::TimedOut,"code prompt did not time out");
  check(rig.service.provide_code(id,command(CommandKind::ArmNight).code,millis())==OperationReason::Busy,"timed-out prompt accepted code");
  check(rig.model.trace_size()==1,"code sent after prompt timeout");
}
void code_prompt_disconnect() {
  Rig rig; auto c=command(CommandKind::ArmNight,1,"");
  auto id=rig.service.submit(c,CommandSource::Http,millis()).id;
  for(unsigned i=0;i<500 && rig.service.operation(id).status!=OperationStatus::AwaitingCode;++i)rig.tick();
  check(rig.service.operation(id).status==OperationStatus::AwaitingCode,"panel prompt not reached");
  rig.model.set_online(false);
  check(rig.finish(id).status==OperationStatus::Uncertain,"disconnect while awaiting code was not uncertain");
  check(rig.service.provide_code(id,command(CommandKind::ArmNight).code,millis())==OperationReason::Busy,"disconnected operation accepted code");
  check(rig.model.trace_size()==1,"code was sent after disconnection");
}
void wrong_code() {
  Rig rig;
  auto c=command(CommandKind::ArmAway,1,"1357");
  const auto op=rig.finish(rig.service.submit(c,CommandSource::Http,millis()).id);
  check(op.status==OperationStatus::Rejected && op.reason==OperationReason::PanelRejected,"bad code falsely accepted");
}
void mixed_adapters_serialized() {
  Rig rig;
  const auto first=rig.service.submit(command(CommandKind::ArmAway),CommandSource::HomeAssistant,millis());
  const auto second=rig.service.submit(command(CommandKind::Disarm),CommandSource::Http,millis());
  check(rig.finish(first.id).status==OperationStatus::Succeeded,"first mixed-source command failed");
  check(rig.finish(second.id).status==OperationStatus::Succeeded,"second mixed-source command failed");
  const std::string expected="w24682468";
  check(rig.model.trace_size()==expected.size(),"unexpected transmitted key count");
  for(unsigned i=0;i<expected.size();++i) check(rig.model.trace(i)==expected[i],"adapter sequences interleaved");
}
void no_confirmation() {
  Rig rig; rig.model.set_behavior(bench::ScriptedPanel::Behavior::NoConfirmation);
  auto c=command(CommandKind::ArmAway); c.timeout_ms=1000;
  const auto op=rig.finish(rig.service.submit(c,CommandSource::Http,millis()).id);
  check(op.status==OperationStatus::TimedOut,"TX completion falsely treated as panel confirmation");
  check(rig.service.partition(1).state==AlarmState::Disarmed,"no-confirmation request changed state");
}
void disconnect_active() {
  Rig rig; auto c=command(CommandKind::ArmAway);
  const auto id=rig.service.submit(c,CommandSource::Http,millis()).id;
  rig.tick(); rig.model.set_online(false);
  const auto op=rig.finish(id);
  check(op.status==OperationStatus::Uncertain,"disconnect did not report uncertainty");
  check(rig.service.partition(1).state==AlarmState::Unavailable,"disconnected state not unavailable");
}
void queue_bounds() {
  Rig rig;
  for(unsigned i=0;i<4;++i) check(rig.service.submit(command(CommandKind::ArmAway),CommandSource::Http,millis()).id!=0,"capacity rejected early");
  const auto denied=rig.service.submit(command(CommandKind::ArmAway),CommandSource::Http,millis());
  check(denied.id==0 && denied.reason==OperationReason::QueueFull,"application queue not bounded");
}
void concurrent_admission() {
  Rig rig;
  constexpr unsigned clients=12;
  std::array<Submission,clients> results{};
  std::array<std::thread,clients> threads;
  const auto now=static_cast<std::uint32_t>(millis());
  for(unsigned i=0;i<clients;++i) threads[i]=std::thread([&,i] {
    results[i]=rig.service.submit(command(CommandKind::ArmAway),
        i%2 ? CommandSource::Http : CommandSource::HomeAssistant,now);
  });
  for(auto &thread:threads) thread.join();
  unsigned accepted=0,full=0;
  for(unsigned i=0;i<clients;++i) {
    if(results[i].id) {
      ++accepted;
      for(unsigned j=0;j<i;++j) check(results[j].id!=results[i].id,"duplicate concurrent operation ID");
    } else if(results[i].reason==OperationReason::QueueFull) ++full;
  }
  check(accepted==4 && full==clients-4,"concurrent admission violated queue capacity");
}
void inputs() {
  Rig rig; auto c=command(CommandKind::Disarm,1,"");
  check(rig.service.submit(c,CommandSource::Http,millis()).reason==OperationReason::CodeRequired,"missing code accepted");
  c=command(CommandKind::ArmAway,0);
  check(rig.service.submit(c,CommandSource::Http,millis()).reason==OperationReason::InvalidPartition,"partition zero accepted");
  c=command(CommandKind::Disarm,1,"123");
  check(rig.service.submit(c,CommandSource::Http,millis()).reason==OperationReason::InvalidCode,"short code accepted");
  c=command(CommandKind::Disarm,1,"12#4");
  check(rig.service.submit(c,CommandSource::Http,millis()).reason==OperationReason::InvalidCode,"key injection accepted as code");
}
void zone_catalog() {
  Rig rig;
  check(rig.service.configure_zone(9,"Entrada","Departamento principal","door"),"valid zone metadata rejected");
  check(!rig.service.configure_zone(9,"Duplicada","Otro","sensor"),"duplicate zone metadata admitted");
  check(!rig.service.configure_zone(0,"Fuera de rango","Otro","sensor"),"invalid zone admitted");
  const auto catalog=rig.service.zone_catalog();
  check(catalog.zones()[8].number==9 && std::string(catalog.zones()[8].group)=="Departamento principal",
        "zone metadata did not survive the service boundary");
  ZoneCatalog copied;
  rig.service.copy_zone_catalog(copied);
  check(copied.zones()[8].number==9 && std::string(copied.zones()[8].group)=="Departamento principal",
        "catalog copy did not preserve configured metadata");
}
void keypad() {
  Rig rig; auto c=command(CommandKind::Keypad,1,""); c.keys[0]='*';c.keys[1]='2';c.keys[2]='#';
  const auto op=rig.finish(rig.service.submit(c,CommandSource::Http,millis()).id);
  check(op.status==OperationStatus::Transmitted,"keypad result should report transmission, not state confirmation");
  check(rig.model.trace_size()==3 && rig.model.trace(0)=='*' && rig.model.trace(1)=='2' && rig.model.trace(2)=='#',"keypad handshake/order wrong");
}
void clock_confirmation() {
  Rig rig; auto c=command(CommandKind::SetClock); c.clock={2026,10,27,12,0,true};
  const auto op=rig.finish(rig.service.submit(c,CommandSource::Http,millis()).id);
  check(op.status==OperationStatus::Succeeded,"clock not observed/confirmed");
  check(rig.service.snapshot().panel_clock.minute==0,"minute 00 clock lost");
  c.clock={2026,2,29,12,0,true};
  check(rig.service.submit(c,CommandSource::Http,millis()).reason==OperationReason::InvalidTime,"invalid calendar date accepted");
}
void observed_activity() {
  Rig rig;
  for(unsigned i=0;i<40;++i) rig.tick();
  const auto first_clock=rig.service.snapshot().bus_activity_ms;
  for(unsigned i=0;i<20;++i) rig.tick();
  check(rig.service.snapshot().bus_activity_ms!=first_clock,"bus activity sample did not advance");
  rig.model.set_zone(9,true);
  for(unsigned i=0;i<40;++i) rig.tick();
  rig.model.set_zone(9,false);
  for(unsigned i=0;i<40;++i) rig.tick();
  ActivitySnapshot events; rig.service.copy_activity(events);
  bool opened=false,closed=false;
  for(std::size_t i=0;i<events.count;++i) {
    const auto &event=events.events[i];
    if(event.number==9 && event.kind==ActivityKind::ZoneOpen) opened=true;
    if(event.number==9 && event.kind==ActivityKind::ZoneClosed) closed=true;
  }
  check(opened && closed,"observed zone changes missing from activity");
}
void cancel_exit_delay() {
  Rig rig; rig.model.set_exit_delay(10000);
  const auto arm=rig.service.submit(command(CommandKind::ArmAway),CommandSource::Http,millis());
  check(arm.id!=0,"arm request rejected");
  for(unsigned i=0;i<300 && !rig.service.snapshot().partitions[0].exit_delay;++i) rig.tick();
  check(rig.service.snapshot().partitions[0].exit_delay,"exit delay not observed");
  const auto trace_before=rig.model.trace_size();
  // Saturate normal queue: the explicit cancellation has a reserved slot.
  std::uint32_t displaced[3]{};
  for(unsigned i=0;i<3;++i) {
    auto key=command(CommandKind::Keypad,1,""); key.keys[0]='#';
    displaced[i]=rig.service.submit(key,CommandSource::Test,millis()).id;
    check(displaced[i]!=0,"normal queue rejected");
  }
  const auto cancel=rig.service.submit(command(CommandKind::Disarm),CommandSource::Http,millis());
  check(cancel.id!=0,"exit-delay disarm did not get priority");
  const auto duplicate=rig.service.submit(command(CommandKind::Disarm),CommandSource::HomeAssistant,millis());
  check(duplicate.reason==OperationReason::Busy,"duplicate disarm was queued");
  auto another=command(CommandKind::Keypad,1,""); another.keys[0]='1';
  check(rig.service.submit(another,CommandSource::Test,millis()).reason==OperationReason::Busy,"keypad admitted while disarming");
  check(rig.finish(cancel.id).status==OperationStatus::Succeeded,"cancellation not confirmed");
  const auto state=rig.service.snapshot().partitions[0];
  check(!state.armed && !state.exit_delay,"panel did not return to disarmed");
  const auto original=rig.service.operation(arm.id);
  check(original.status==OperationStatus::Uncertain && original.reason==OperationReason::Superseded,"original arm result must remain uncertain");
  check(rig.model.trace_size()==trace_before+4,"another queued action or duplicate was sent");
  const char *expected="2468";
  for(unsigned i=0;i<4;++i) check(rig.model.trace(trace_before+i)==expected[i],"wrong cancellation sequence");
  for(auto id:displaced) {
    const auto result=rig.service.operation(id);
    check(result.status==OperationStatus::Rejected && result.reason==OperationReason::Superseded,"queued operation was not superseded");
  }
  for(unsigned i=0;i<50;++i) rig.tick();
  check(rig.model.trace_size()==trace_before+4,"superseded action was sent after cancellation");
}
void cancel_exit_wrong_code() {
  Rig rig; rig.model.set_exit_delay(10000);
  const auto arm=rig.service.submit(command(CommandKind::ArmAway),CommandSource::Http,millis());
  for(unsigned i=0;i<300 && !rig.service.snapshot().partitions[0].exit_delay;++i) rig.tick();
  check(rig.service.snapshot().partitions[0].exit_delay,"exit delay not observed");
  rig.model.set_behavior(bench::ScriptedPanel::Behavior::RejectCode);
  const auto before=rig.model.trace_size();
  const auto cancel=rig.service.submit(command(CommandKind::Disarm,1,"1357"),CommandSource::Http,millis());
  const auto result=rig.finish(cancel.id);
  check(result.status==OperationStatus::Rejected && result.reason==OperationReason::PanelRejected,"wrong cancellation code falsely succeeded");
  for(unsigned i=0;i<25;++i) rig.tick();
  check(rig.model.trace_size()==before+4,"wrong code was retried automatically");
  check(rig.service.snapshot().partitions[0].exit_delay,"rejected cancellation should not appear disarmed");
  check(rig.service.operation(arm.id).status==OperationStatus::Uncertain,"interrupted arm should remain uncertain");
  rig.model.set_behavior(bench::ScriptedPanel::Behavior::Normal);
  const auto retry=rig.service.submit(command(CommandKind::Disarm),CommandSource::Http,millis());
  check(rig.finish(retry.id).status==OperationStatus::Succeeded,"explicit retry failed");
}
void cancel_exit_other_partition_queue() {
  Rig rig; rig.model.enable_partition(2); rig.tick(); rig.model.set_exit_delay(10000);
  const auto arm=rig.service.submit(command(CommandKind::ArmAway),CommandSource::Http,millis());
  for(unsigned i=0;i<300 && !rig.service.snapshot().partitions[0].exit_delay;++i) rig.tick();
  check(rig.service.snapshot().partitions[0].exit_delay,"exit delay not observed");
  std::uint32_t other[3]{};
  for(unsigned i=0;i<3;++i) {
    auto key=command(CommandKind::Keypad,2,""); key.keys[0]='#';
    other[i]=rig.service.submit(key,CommandSource::Test,millis()).id;
    check(other[i]!=0,"other partition operation rejected");
  }
  const auto cancel=rig.service.submit(command(CommandKind::Disarm),CommandSource::Http,millis());
  check(cancel.id!=0,"reserved cancellation slot unavailable");
  check(rig.finish(cancel.id).status==OperationStatus::Succeeded,"cancellation did not finish");
  check(rig.service.operation(arm.id).reason==OperationReason::Superseded,"arm was not superseded");
  for(auto id:other) check(rig.service.operation(id).status==OperationStatus::Queued,"other partition operation lost");
}
void cancel_exit_no_confirmation() {
  Rig rig; rig.model.set_exit_delay(10000);
  rig.service.submit(command(CommandKind::ArmAway),CommandSource::Http,millis());
  for(unsigned i=0;i<300 && !rig.service.snapshot().partitions[0].exit_delay;++i) rig.tick();
  check(rig.service.snapshot().partitions[0].exit_delay,"exit delay not observed");
  rig.model.set_behavior(bench::ScriptedPanel::Behavior::NoConfirmation);
  const auto before=rig.model.trace_size();
  auto disarm=command(CommandKind::Disarm); disarm.timeout_ms=1500;
  const auto cancel=rig.service.submit(disarm,CommandSource::Http,millis());
  check(rig.finish(cancel.id).status==OperationStatus::TimedOut,"unconfirmed cancellation reported success");
  for(unsigned i=0;i<25;++i) rig.tick();
  check(rig.model.trace_size()==before+4,"unconfirmed cancellation retried automatically");
  check(rig.service.snapshot().partitions[0].exit_delay,"unconfirmed cancellation reported disarmed");
}
void thousand_cycles() {
  fake::set_millis(0xFFFFFF00U);
  Rig rig;
  for(unsigned i=0;i<1000;++i) {
    if(i%100==0) {
      rig.model.set_behavior(bench::ScriptedPanel::Behavior::NoConfirmation);
      auto uncertain=command(CommandKind::ArmAway); uncertain.timeout_ms=1000;
      const auto timeout=rig.finish(rig.service.submit(uncertain,CommandSource::Http,millis()).id);
      check(timeout.status==OperationStatus::TimedOut,"stress missing-confirmation falsely succeeded");
      rig.model.set_behavior(bench::ScriptedPanel::Behavior::Normal);
      const auto wrong=rig.finish(rig.service.submit(command(CommandKind::ArmAway,1,"1357"),CommandSource::Http,millis()).id);
      check(wrong.status==OperationStatus::Rejected && wrong.reason==OperationReason::PanelRejected,
            "stress rejected code falsely succeeded");
      auto clear=command(CommandKind::Keypad,1,""); clear.keys[0]='#';
      check(rig.finish(rig.service.submit(clear,CommandSource::Http,millis()).id).status==OperationStatus::Transmitted,
            "stress rejection recovery failed");
    }
    const auto kind=i%3==0?CommandKind::ArmStay:i%3==1?CommandKind::ArmAway:CommandKind::ArmNight;
    check(rig.finish(rig.service.submit(command(kind),CommandSource::HomeAssistant,millis()).id).status==OperationStatus::Succeeded,"stress arm failed");
    check(rig.finish(rig.service.submit(command(CommandKind::Disarm),CommandSource::Http,millis()).id).status==OperationStatus::Succeeded,"stress disarm failed");
    rig.model.reset_trace();
  }
}
void timeout_across_clock_wrap() {
  fake::set_millis(0xFFFFFF00U);
  Rig rig; rig.model.set_behavior(bench::ScriptedPanel::Behavior::NoConfirmation);
  auto c=command(CommandKind::ArmAway); c.timeout_ms=1000;
  const auto op=rig.finish(rig.service.submit(c,CommandSource::Http,millis()).id);
  check(op.status==OperationStatus::TimedOut,"timeout failed across 32-bit clock wrap");
}
}
int main(int argc,char **argv) {
  const std::map<std::string,std::function<void()>> cases={
    {"arm-away-cycle",[]{complete_cycle(CommandKind::ArmAway,AlarmState::ArmedAway);}},
    {"arm-stay-cycle",[]{complete_cycle(CommandKind::ArmStay,AlarmState::ArmedStay);}},
    {"arm-night-cycle",[]{complete_cycle(CommandKind::ArmNight,AlarmState::ArmedNight);}},
    {"arm-away-code-prompt",[]{code_prompt(CommandKind::ArmAway,CommandSource::Http);}},
    {"arm-stay-code-prompt",[]{code_prompt(CommandKind::ArmStay,CommandSource::Http);}},
    {"arm-night-code-prompt",[]{code_prompt(CommandKind::ArmNight,CommandSource::Http);}},
    {"ha-away-code-prompt",[]{code_prompt(CommandKind::ArmAway,CommandSource::HomeAssistant);}},
    {"ha-stay-code-prompt",[]{code_prompt(CommandKind::ArmStay,CommandSource::HomeAssistant);}},
    {"ha-night-code-prompt",[]{code_prompt(CommandKind::ArmNight,CommandSource::HomeAssistant);}},
    {"abandon-code-prompt",abandon_code_prompt},
    {"code-prompt-timeout",code_prompt_timeout},
    {"code-prompt-disconnect",code_prompt_disconnect},
    {"wrong-code",wrong_code},{"mixed-adapters-serialized",mixed_adapters_serialized},
    {"no-confirmation",no_confirmation},{"disconnect-active",disconnect_active},
    {"queue-bounds",queue_bounds},{"concurrent-admission",concurrent_admission},{"input-validation",inputs},{"zone-catalog",zone_catalog},{"keypad-session",keypad},
    {"clock-confirmation",clock_confirmation},{"observed-activity",observed_activity},{"cancel-exit-delay",cancel_exit_delay},{"cancel-exit-wrong-code",cancel_exit_wrong_code},{"cancel-exit-other-partition",cancel_exit_other_partition_queue},{"cancel-exit-no-confirmation",cancel_exit_no_confirmation},{"1000-arm-disarm-cycles",thousand_cycles},
    {"timeout-across-clock-wrap",timeout_across_clock_wrap}};
  if(argc==2 && std::string(argv[1])=="--list") { for(const auto &item:cases) std::cout<<item.first<<'\n'; return 0; }
  if(argc!=2 || !cases.count(argv[1])) return 2;
  try { cases.at(argv[1])(); std::cout<<"PASS "<<argv[1]<<'\n'; return 0; }
  catch(const std::exception &error) { std::cerr<<error.what()<<'\n'; return 1; }
}
