// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripted_panel.h"
#ifdef DSC_ENABLE_REPLAY
#include <cstring>
namespace dsc::bench {
void ScriptedPanel::emit(Frame frame,bool checksum) {
  if (checksum) {
    std::uint8_t sum=0;
    for(unsigned i=0;i<frame.size;++i) if(i!=1) sum+=frame.bytes[i];
    frame.bytes[frame.size++]=sum;
  }
  bus_.injectReplayFrame(frame); bus_.loop();
}
void ScriptedPanel::emit_status() {
  for(unsigned block=0;block<2;++block) {
    Frame frame; frame.size=10; frame.bytes[0]=block?0x1B:0x05;
    for(unsigned p=0;p<4;++p) {
      const auto index=block*4+p;
      frame.bytes[2+p*2]=0x80 | (message_[index]==1?1:2) | (trouble_?0x10:0) | (fire_?0x40:0);
      frame.bytes[3+p*2]=message_[index];
    }
    emit(frame);
  }
}
void ScriptedPanel::emit_zones() {
  constexpr std::uint8_t commands[]={0x27,0x2D,0x34,0x3E};
  constexpr std::uint8_t subcommands[]={0x09,0x0B,0x0D,0x0F};
  for(unsigned group=0;group<8;++group) {
    Frame frame;
    if(group<4) {
      frame.size=7; frame.bytes[0]=commands[group];
      frame.bytes[2]=0x81; frame.bytes[3]=message_[0];
      frame.bytes[4]=0x80; frame.bytes[5]=message_[1]; frame.bytes[6]=zones_[group];
    } else {
      frame.size=4; frame.bytes[0]=0xE6; frame.bytes[2]=subcommands[group-4]; frame.bytes[3]=zones_[group];
    }
    emit(frame,true);
  }
}
bool ScriptedPanel::begin() {
  if(!bus_.beginReplay()) return false;
  emit_status(); emit_zones(); return true;
}
void ScriptedPanel::enable_partition(std::uint8_t p) {
  if(p>=1 && p<=8) { message_[p-1]=1; emit_status(); }
}
void ScriptedPanel::set_zone(std::uint8_t zone,bool open) {
  if(zone<1 || zone>64) return;
  const auto index=zone-1;
  if(open) zones_[index/8] |= 1U<<(index%8);
  else zones_[index/8] &= ~(1U<<(index%8));
}
void ScriptedPanel::close_menu(unsigned p) {
  // Supported clock script: *6 + synthetic code 2468 + 1 + HHMMmmddYY + #.
  const auto &keys=menu_keys_[p];
  if(menu_size_[p]==17 && std::memcmp(keys.data(),"*624681",7)==0) {
    auto number=[&keys](unsigned at) { return unsigned(keys[at]-'0')*10+unsigned(keys[at+1]-'0'); };
    const auto hour=number(7),minute=number(9),month=number(11),day=number(13),year=number(15);
    Frame frame; frame.size=8; frame.bytes[0]=0xA5;
    frame.bytes[2]=(year/10)*16+year%10;
    frame.bytes[3]=(month<<2)|(day>>3);
    frame.bytes[4]=((day&7)<<5)|hour;
    frame.bytes[5]=minute<<2;
    emit(frame,true);
  }
  menu_[p]=false; menu_size_[p]=0; menu_keys_[p].fill(0);
  message_[p]=armed_mode_[p]=='s'?4:armed_mode_[p]=='w'?5:armed_mode_[p]=='n'?6:1;
}
void ScriptedPanel::key(char value,std::uint8_t partition,std::uint32_t now) {
  const unsigned p=partition-1;
  if(trace_size_<trace_.size()) trace_[trace_size_++]=value;
  if(behavior_==Behavior::NoConfirmation) return;
  if(value=='*') {
    if(behavior_==Behavior::MissingStarAck) return;
    menu_[p]=true; menu_size_[p]=1; menu_keys_[p][0]='*'; message_[p]=0x9E;
  } else if(value=='#') {
    if(menu_[p]) close_menu(p);
    else message_[p]=armed_mode_[p]=='s'?4:armed_mode_[p]=='w'?5:armed_mode_[p]=='n'?6:1;
    code_size_[p]=0; entered_code_[p].fill(0);
  } else if(menu_[p]) {
    if(menu_size_[p]<menu_keys_[p].size()-1) menu_keys_[p][menu_size_[p]++]=value;
  } else if(value=='s' || value=='w' || value=='n') {
    pending_arm_[p]=value; message_[p]=0x9F; code_size_[p]=0; entered_code_[p].fill(0);
  } else if(value>='0' && value<='9') {
    if(code_size_[p]<4) entered_code_[p][code_size_[p]++]=value;
    if(code_size_[p]==4) {
      const bool valid=behavior_!=Behavior::RejectCode && std::memcmp(entered_code_[p].data(),"2468",4)==0;
      if(!valid) {
        const bool was_exit=message_[p]==8;
        message_[p]=0x8F; resume_exit_[p]=was_exit;
        if(!was_exit) pending_arm_[p]=0;
      }
      else if(message_[p]==8) { pending_arm_[p]=0; armed_mode_[p]=0; message_[p]=0x3E; }
      else if(pending_arm_[p]) { message_[p]=8; arm_start_[p]=now; }
      else { armed_mode_[p]=0; message_[p]=0x3E; }
      code_size_[p]=0; entered_code_[p].fill(0);
    }
  }
  emit_status();
}
void ScriptedPanel::step(std::uint32_t now) {
  if(!online_) return;
  for(unsigned p=0;p<8;++p) if(resume_exit_[p]) { resume_exit_[p]=false; message_[p]=8; emit_status(); }
  char value; std::uint8_t partition;
  if(bus_.takeReplayKey(value,partition)) key(value,partition,now);
  for(unsigned p=0;p<8;++p) if(message_[p]==8 && static_cast<std::uint32_t>(now-arm_start_[p])>=exit_delay_ms_) {
    armed_mode_[p]=pending_arm_[p]; pending_arm_[p]=0;
    message_[p]=armed_mode_[p]=='s'?4:armed_mode_[p]=='w'?5:6;
    emit_status();
  }
  if(static_cast<std::uint32_t>(now-last_frame_)>=200) {
    emit_status(); emit_zones(); last_frame_=now;
  }
}
}  // namespace dsc::bench
#endif
