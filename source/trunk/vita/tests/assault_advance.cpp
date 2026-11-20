
#include "vita_npc_battle.h"
#include "vita_loot_trade.h"
#include "vita_assault_advance.h"
#include "vita_cform_ground.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <chrono>
int main(int argc,char**argv){
 assert(argc==2);
 VitaCformGround ground;const char*path=argv[1];FILE*f=fopen(path,"rb");assert(f);
 const float destination[]={113.7583f,-7.4996f,8.6161f};
 auto read=[](void*p,void*dst,size_t n){return fread(dst,1,n,(FILE*)p)==n;};
 assert(ground.load(read,f,std::filesystem::file_size(path),-246.73f,-134.43f,{},destination));fclose(f);
 VitaLootWorld loot;assert(loot.place_supplies(ground,{-246.73f,-24.79f,-134.43f},0));
 const auto bag=loot.containers[9].position;
 assert(std::hypot(bag[0]+246.73f,bag[2]+134.43f)>=.75f);
 printf("Bunker supply bag grounded and reachable at %.3f %.3f %.3f\n",bag[0],bag[1],bag[2]);

 VitaNpcBattle battle;
 const std::array<float,3> spawn[] = {{94.346725f,-7.180477f,-11.245847f},{111.653091f,-7.663469f,-20.156815f},{93.787979f,-7.180478f,-23.776489f},{114.109932f,-7.494280f,7.850361f},{123.194931f,-7.272591f,-35.665333f},{106.953056f,-2.834281f,0.077179f}};
 for(size_t i=0;i<6;++i){auto p=spawn[i];float y=p[1];assert(ground.height_at(p[0],p[2],y,.55f,.8f,y));p[1]=y;battle.guards[i].position=battle.guards[i].home=p;battle.guards[i].health=2;}

 const VitaPatrolPoint approach={80.6107407f,-8.4035034f,-47.3260841f};

 // Reproduce the field report: the old approach endpoint alone yields no allied shot.
 auto baseline=battle;for(auto&a:baseline.scouts)a.position=a.home=approach;
 unsigned baseline_ally=0;
 for(uint64_t now=1000000;now<46000000;now+=33333)
  for(const auto&shot:baseline.update(ground,now,approach,true,100))baseline_ally+=shot.ally;
 assert(baseline_ally==0);
 printf("Baseline allied shots from the original endpoint: %u\n",baseline_ally);
 VitaPatrolPoint targets[6];std::array<bool,6> present;present.fill(true);
 for(unsigned j=0;j<6;++j)targets[j]=battle.guards[j].position;
 std::array<std::vector<VitaPatrolPoint>,3> routes;
 for(unsigned member=0;member<3;++member){
  std::vector<VitaPatrolPoint> path;VitaAssaultAdvanceStats stats;
  auto before=std::chrono::steady_clock::now();
  assert(find_vita_assault_advance(ground,approach,targets,present,13.5f-member*1.5f,path,stats));
  auto after=std::chrono::steady_clock::now();
  assert(!path.empty());printf("advance member=%u points=%zu visits=%u end=(%.2f,%.2f,%.2f) ms=%.2f\n",member,path.size(),stats.visited,
   path.back()[0],path.back()[1],path.back()[2],std::chrono::duration<double,std::milli>(after-before).count());
  auto from=approach;
  for(const auto& to:path){
   float x=to[0],z=to[2],y=from[1];ground.slide_move(from[0],from[2],from[1],x,z);
   assert(std::hypot(x-to[0],z-to[2])<.06f);assert(ground.height_at(to[0],to[2],from[1],.45,.65,y));from=to;
  }
  routes[member].push_back(approach);routes[member].insert(routes[member].end(),path.begin(),path.end());
  battle.scouts[member].position=battle.scouts[member].home=approach;
 }
 const auto rendezvous=approach;
 unsigned shots=0,ally=0,moved=0;bool movement[6]={};double worst=0,total=0;
 std::array<unsigned,3> ally_shots{};
 std::array<float,3> route_progress{};
 float elapsed=0.f;
 for(uint64_t now=1000000;now<91000000;now+=33333){

  const float step=1.f/30.f;
  elapsed+=step;
  for(unsigned i=0;i<3;++i)if(!battle.scouts[i].engaged&&battle.scouts[i].health){
   auto& a=battle.scouts[i];const float seconds=elapsed-float(i)*1.25f;if(seconds<=0)continue;
   route_progress[i]+=step*2.2f;float goal=route_progress[i];
   auto desired=routes[i].back();
   for(unsigned j=1;j<routes[i].size();++j){
    const auto& from=routes[i][j-1];const auto&to=routes[i][j];
    float span=std::hypot(to[0]-from[0],to[2]-from[2]);
    if(goal<=span){for(unsigned k=0;k<3;++k)desired[k]=from[k]+(to[k]-from[k])*goal/span;break;}goal-=span;
   }
   const float dx=desired[0]-a.position[0],dz=desired[2]-a.position[2],distance=std::hypot(dx,dz);
   if(distance>2.2f*step)for(unsigned k:{0u,2u})desired[k]=a.position[k]+(desired[k]-a.position[k])*(2.2f*step/distance);
   float y=a.position[1];ground.slide_move(a.position[0],a.position[2],y,desired[0],desired[2]);
   if(ground.height_at(desired[0],desired[2],y,.45f,.65f,y)){desired[1]=y;a.position=a.home=desired;}
  }
  auto before=std::chrono::steady_clock::now();
  auto events=battle.update(ground,now,{rendezvous[0]-8,rendezvous[1],rendezvous[2]},true,100);
  auto after=std::chrono::steady_clock::now();const double us=std::chrono::duration<double,std::micro>(after-before).count();total+=us;worst=std::max(worst,us);
  for(size_t i=0;i<6;++i)movement[i]=movement[i]||battle.guards[i].walking;
  for(const auto&e:events){++shots;if(e.ally){++ally;++ally_shots[e.shooter];if(e.damage&&battle.guards[e.target].health)--battle.guards[e.target].health;}else if(e.target>=0){auto&a=battle.scouts[e.target];a.health-=std::min(a.health,e.damage);}}
  for(const auto&a:battle.guards)if(a.health){float y;assert(ground.height_at(a.position[0],a.position[2],a.position[1],.1,.1,y));assert(std::abs(y-a.position[1])<.02);}
 }
 for(bool v:movement)moved+=v;
 assert(shots>0&&ally>0);
 assert(std::count_if(ally_shots.begin(),ally_shots.end(),[](unsigned n){return n>0;})>=2);
 printf("Per ally shots: %u,%u,%u; elapsed=%.2f\n",ally_shots[0],ally_shots[1],ally_shots[2],elapsed);
 printf("Actual Cordon: moved=%u shots=%u allied_shots=%u avg_tick=%.1f us worst=%.1f us (host only)\n",moved,shots,ally,total/2700,worst);
}
