#pragma once
#include "vita_campaign.h"
class VitaZoneConfirmation {
public:
 enum class Action {None,Cancel,Confirm};
 bool active()const{return pending>=0;}
 unsigned exit()const{return static_cast<unsigned>(pending);}
 bool open(unsigned id){
  if(active()||id>=vita_zone_exits.size())return false;
  pending=static_cast<int>(id);armed=false;return true;
 }
 void reset(){pending=-1;armed=false;}
 Action update(bool confirm_pressed,bool cancel_pressed,bool buttons_released){
  if(!active())return Action::None;
  if(!armed){armed=buttons_released;return Action::None;}
  if(cancel_pressed){reset();return Action::Cancel;}
  if(confirm_pressed){reset();return Action::Confirm;}
  return Action::None;
 }
private:
 int pending=-1;bool armed=false;
};
