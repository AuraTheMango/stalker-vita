
#include "vita_trader_model.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <algorithm>
std::vector<uint8_t> read(const std::string& p){std::ifstream f(p,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
void dump(std::ofstream& out,const VitaStalkerMesh& p){uint32_t n=p.vertices.size(),k=p.indices.size();out.write((char*)&n,4);out.write((char*)&k,4);out.write((char*)p.vertices.data(),n*sizeof(VitaTraderVertex));out.write((char*)p.indices.data(),k*2);}
int main(int argc,char**argv){
 assert(argc==3);
 const std::string base=std::string(argv[1])+"/",test=std::string(argv[2])+"/";
 auto params=read(test+"params"),idle=read(test+"idle"),walk=read(test+"walk"),unarmed=read(test+"unarmed");
 for(const char* name:{"stalker_neytral_balon_1","green_stalker_8","green_stalker_2","green_stalker_3","stalker_bandit_3","green_stalker_1"}){
  auto model=read(base+name+".ogf");VitaStalkerMesh parts[2];VitaStalkerWeaponPose pose;float head[3]={};bool posed=false;
  assert(read_vita_stalker_model(model.data(),model.size(),params,std::string(name)=="green_stalker_1"?unarmed:idle,parts,head,posed,&walk,&pose));
  assert(posed && pose.ready);assert(head[1]>.5f && head[1]<2.f);
  const size_t frames=parts[0].walk_positions.size()/(3*parts[0].vertices.size());
  assert(frames>1 && pose.walk.size()==frames);
  for(const auto& m:pose.walk)for(float v:m)assert(std::isfinite(v));
  for(float v:pose.idle)assert(std::isfinite(v));
  float delta=0;for(const auto& m:pose.walk)for(size_t i=0;i<16;++i)delta=std::max(delta,std::abs(m[i]-pose.walk.front()[i]));
  assert(delta>.01f);assert(pose.idle[13]>.3f && pose.idle[13]<1.9f);
  printf("%s posed=1 hand=(%.3f,%.3f,%.3f) walk=%zu motion=%.3f\n",name,pose.idle[12],pose.idle[13],pose.idle[14],frames,delta);

  unsigned slot=std::string(name)=="stalker_neytral_balon_1"?0:std::string(name)=="stalker_bandit_3"?2:1;
  unsigned ids[3][6]={{262,263,1340,1342,1349,78},{269,270,1350,1352,1359,78},{259,260,1330,1332,1339,78}};
  if(std::string(name)!="green_stalker_1"){
   VitaNpcMotions motions;for(unsigned i=0;i<6;++i)motions[i]=read(test+"clip-"+std::to_string(ids[slot][i]));
   VitaNpcRanges ranges;
   assert(read_vita_npc_animations(model.data(),model.size(),params,idle,walk,motions,parts,pose,ranges));
   size_t end=1;for(const auto&r:ranges){assert(r.first==end&&r.count>=1&&r.fps>0);end+=r.count;
    assert(r.frame(0)==r.first);assert(r.frame(100,false)==r.first+r.count-1);
    auto midpoint=r.sample(.5f/r.fps);
    assert(midpoint.first==r.first&&midpoint.next==r.first+(r.count>1?1:0)&&std::abs(midpoint.blend-.5f)<.0001f);
    auto tail=r.sample((r.count-.5f)/r.fps);assert(tail.next==r.first);
    auto end=r.sample(100,false);assert(end.first==r.first+r.count-1&&end.next==end.first);

   }
   assert(ranges[size_t(VitaNpcClip::Dead)].count==1);
   float dead_min=100,dead_max=-100;
   const auto&body=parts[0];const size_t begin=(ranges[size_t(VitaNpcClip::Dead)].first-1)*body.vertices.size()*3;
   for(size_t vertex=0;vertex<body.vertices.size();++vertex){
    const float y=body.walk_positions[begin+vertex*3+1];dead_min=std::min(dead_min,y);dead_max=std::max(dead_max,y);}
   fprintf(stderr,"dead bounds %s %.3f %.3f\n",name,dead_min,dead_max);
   assert(dead_max-dead_min<.8f);
   printf("Corpse height %.3f..%.3f\n",dead_min,dead_max);

   assert(end==pose.walk.size()+1);assert(ranges[size_t(VitaNpcClip::Walk)].count==20);
   for(auto&p:parts){assert(p.walk_positions.size()/(3*p.vertices.size())==pose.walk.size());assert(p.walk_positions.size()*4<=4*1024*1024);
    for(float v:p.walk_positions)assert(std::isfinite(v));}
   for(const auto&m:pose.walk){for(float v:m)assert(std::isfinite(v));assert(m[13]>-.5f&&m[13]<1.9f);}
   printf("BANK %s frames=%zu bytes=%zu idle_hand=(%.3f,%.3f,%.3f)\n",name,pose.walk.size(),parts[0].walk_positions.size()*4,
    pose.walk[0][12],pose.walk[0][13],pose.walk[0][14]);
  }
  std::ofstream out(test+name+".bin",std::ios::binary);out.write((char*)pose.idle.data(),64);for(auto&p:parts)dump(out,p);
  std::vector<uint8_t> empty;
  assert(read_vita_stalker_model(model.data(),model.size(),params,empty,parts,head,posed,nullptr,&pose));
  assert(!posed && !pose.ready && pose.walk.empty());
 }
 for(const char* name:{"ak74u","wpn_bm-16","wpn_pm"}){
  auto model=read(base+name+".ogf");std::vector<VitaStalkerMesh> parts;
  assert(read_vita_weapon_world(model.data(),model.size(),parts));size_t indices=0;
  std::ofstream out(test+name+".bin",std::ios::binary);
  for(const auto&p:parts){
   indices+=p.indices.size();assert(p.indices.size()%3==0);
   for(auto i:p.indices)assert(i<p.vertices.size());
   for(auto&v:p.vertices)for(auto x:v.position)assert(std::isfinite(x));dump(out,p);
  }
  printf("%s parts=%zu indices=%zu\n",name,parts.size(),indices);
  for(size_t size=0;size<model.size();size+=137){std::vector<VitaStalkerMesh> p;read_vita_weapon_world(model.data(),size,p);}
 }
 for(bool pistol:{true,false}){
  auto model=read(base+(pistol?"wpn_pm_hud.ogf":"wpn_knife_hud.ogf"));
  std::vector<VitaWeaponHudMesh> parts;VitaWeaponClips clips{};
  assert(read_vita_weapon_hud(model.data(),model.size(),pistol,parts,clips));
  assert(parts.size()==2);assert(clips[size_t(VitaWeaponClip::Idle)].source_frames>0);
  assert(clips[size_t(pistol?VitaWeaponClip::Reload:VitaWeaponClip::KnifeStrike)].source_frames>1);
  printf("HUD %s parts=%zu idle=%u\n",pistol?"PM":"knife",parts.size(),clips[0].source_frames);
 }

}
