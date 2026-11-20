#pragma once
#include "vita_npc_battle.h"
struct VitaGarbageRoadState{
 unsigned flags=0; // initialized, confrontation started, survivor acknowledged
 std::array<unsigned,4> health{100,2,2,2};
 std::array<std::array<float,3>,4> position{};
};
inline constexpr std::array<std::array<float,3>,4> vita_garbage_road_spawns={{
 {32.01196289f,.53999978f,-264.05310059f},
 {31.62051773f,.46800005f,-261.89779663f},
 {34.52821732f,.48600000f,-263.69821167f},
 {32.23302460f,.52199948f,-266.25201416f}
}};
class VitaGarbageRoad{
public:
 VitaNpcBattle battle;
 unsigned flags=0;
 bool ready=false;
 bool cleared()const{return !battle.guards[0].health&&!battle.guards[1].health&&!battle.guards[2].health;}
 template<class Ground> bool restore(const VitaGarbageRoadState&s,Ground&ground){
  VitaNpcBattle next;
  for(auto&a:next.scouts)a.health=0;
  for(auto&a:next.guards)a.health=0;
  for(unsigned i=0;i<4;++i){
   auto&a=i?next.guards[i-1]:next.scouts[0];
   a.position=(s.flags&1)?s.position[i]:vita_garbage_road_spawns[i];a.home=vita_garbage_road_spawns[i];
   float y=a.position[1];
   if(!ground.height_at(a.position[0],a.position[2],y,.8f,1.2f,y))return false;
   a.position[1]=y;a.home[1]=y;a.health=s.health[i];a.yaw=i?0.f:3.14159265f;
  }
  battle=next;flags=s.flags|1;ready=true;return true;
 }
 VitaGarbageRoadState state()const{
  VitaGarbageRoadState s;s.flags=flags;
  for(unsigned i=0;i<4;++i){const auto&a=i?battle.guards[i-1]:battle.scouts[0];s.health[i]=a.health;s.position[i]=a.position;}
  return s;
 }
 template<class Ground> std::vector<VitaNpcShot> update(Ground&g,uint64_t now,const std::array<float,3>&player,unsigned health){
  if(!ready||std::hypot(player[0]-32,player[2]+264)>55){return {};}
  flags|=2;
  auto events=battle.update(g,now,player,true,health);
  for(const auto&e:events){
   if(e.ally&&e.target>=0&&e.target<3){auto&a=battle.guards[e.target];a.health-=std::min(a.health,e.damage);}
   else if(!e.ally&&e.target==0){auto&a=battle.scouts[0];a.health=std::max(1u,a.health-std::min(a.health,e.damage));}
  }
  return events;
 }
};
