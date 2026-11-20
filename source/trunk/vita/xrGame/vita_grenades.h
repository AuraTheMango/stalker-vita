#pragma once
#include <array>
#include <vector>
#include <algorithm>
#include <cmath>

struct VitaGrenadeBody{bool active=false;unsigned type=0;float age=0;std::array<float,3> position{},velocity{};};
struct VitaExplosion{unsigned type=0;std::array<float,3> position{};float radius()const{return type?10.f:7.f;}};
class VitaGrenades
{
public:
 std::array<VitaGrenadeBody,8> bodies{};
 bool launch(const std::array<float,3>& origin,const std::array<float,3>& direction,unsigned type){
  for(auto&g:bodies)if(!g.active){g={};g.active=true;g.type=type;g.position=origin;
   for(unsigned i=0;i<3;++i)g.velocity[i]=direction[i]*12.f;g.velocity[1]+=2.f;return true;}
  return false;
 }
 template<class Ground>std::vector<VitaExplosion> update(Ground& ground,float seconds){
  std::vector<VitaExplosion> explosions;
  seconds=std::clamp(seconds,0.f,.1f);
  const unsigned steps=std::max(1u,unsigned(std::ceil(seconds*120)));
  const float dt=seconds/steps;
  for(auto&g:bodies)if(g.active)for(unsigned step=0;step<steps;++step){
   g.age+=dt;g.velocity[1]-=9.81f*dt;
   std::array<float,3> delta{},next=g.position;
   float length=0;for(unsigned i=0;i<3;++i){delta[i]=g.velocity[i]*dt;next[i]+=delta[i];length+=delta[i]*delta[i];}
   length=std::sqrt(length);
   if(length>.00001f){const float ray[]={delta[0]/length,delta[1]/length,delta[2]/length};
    if(ground.blocks_ray(g.position.data(),ray,length+.06f)){
     g.velocity[0]*=-.35f;g.velocity[2]*=-.35f;g.velocity[1]=std::abs(g.velocity[1])*.3f;
     next=g.position;
    }}
   float floor=g.position[1]-.15f;
   if(ground.height_at(next[0],next[2],g.position[1],1.f,2.f,floor)&&next[1]<floor+.08f){
    next[1]=floor+.08f;g.velocity[1]=std::abs(g.velocity[1])*.30f;g.velocity[0]*=.75f;g.velocity[2]*=.75f;}
   g.position=next;
   if(g.age>=3.f){explosions.push_back({g.type,g.position});g.active=false;break;}
  }
  return explosions;
 }
 template<class Ground>static unsigned damage(Ground& ground,const VitaExplosion&e,const std::array<float,3>& target){
  std::array<float,3> delta;float length=0;for(unsigned i=0;i<3;++i){delta[i]=target[i]-e.position[i];length+=delta[i]*delta[i];}
  length=std::sqrt(length);if(length>=e.radius())return 0;
  if(length>.1f){const float ray[]={delta[0]/length,delta[1]/length,delta[2]/length};
   if(ground.blocks_ray(e.position.data(),ray,std::max(.01f,length-.1f)))return 0;}
  return unsigned(120.f*(1.f-length/e.radius()));
 }
};
