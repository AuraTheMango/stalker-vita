
#include "vita_trader_model.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <cmath>
#include <cstdio>
std::vector<uint8_t> get(const std::string&p){std::ifstream f(p,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char**argv){
 assert(argc==3);const std::string root=argv[1],clips=std::string(argv[2])+"/";
 const auto params=get(clips+"params"),idle=get(clips+"idle"),walk=get(clips+"walk");
 for(const char*path:{"neytral/stalker_neytral_hood_4.ogf","neytral/stalker_neytral_rukzak_4.ogf","soldier/soldier_bandana_6.ogf","novice/green_stalker_11.ogf"}){
  const auto model=get(root+"/"+path);VitaStalkerMesh parts[2];VitaStalkerWeaponPose pose;VitaNpcRanges ranges;
  float head[3]={};bool posed=false;
  assert(read_vita_stalker_model(model.data(),model.size(),params,idle,parts,head,posed,&walk,&pose)&&posed&&pose.ready);
  VitaNpcMotions motion;const bool novice=std::string(path).find("green_stalker_11")!=std::string::npos;
  const unsigned ids[]={novice?259u:262u,novice?260u:263u,novice?1330u:1340u,novice?1332u:1342u,novice?1339u:1349u,78};
  for(unsigned i=0;i<6;++i)motion[i]=get(clips+"clip-"+std::to_string(ids[i]));
  assert(read_vita_npc_animations(model.data(),model.size(),params,idle,walk,motion,parts,pose,ranges));
  for(const auto&p:parts)for(float x:p.walk_positions)assert(std::isfinite(x));
  printf("%s frames=%zu positions=%zu head=%.2f\n",path,pose.walk.size(),
   (parts[0].walk_positions.size()+parts[1].walk_positions.size())*4,head[1]);
 }
}
