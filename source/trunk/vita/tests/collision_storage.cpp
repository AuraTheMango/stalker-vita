
#include "vita_cform_ground.h"
#include <cassert>
#include <filesystem>
#include <cstdio>
#include <cstdint>
int main(int argc,char**argv){
 assert(argc==3);FILE*f=fopen(argv[1],"rb");assert(f);
 const auto reader=[](void*p,void*out,size_t n){return fread(out,1,n,(FILE*)p)==n;};
 VitaCformGround ground;const float dest[]={113.7583f,-7.4996f,8.6161f};
 assert(ground.load(reader,f,std::filesystem::file_size(argv[1]),-246.73f,-134.43f,{},dest,argv[2][0]=='f'));fclose(f);
#ifdef COMPACT_GROUND_CHECKS
 assert(ground.memory_bytes()<=32u*1024u*1024u);
#endif
 printf("memory=%zu ground=%zu walls=%zu\n",ground.memory_bytes(),ground.face_count(),ground.wall_count());
 uint32_t seed=793;
 const auto random=[&](){seed=seed*1664525u+1013904223u;return float((seed>>8)&65535)/65535;};
 for(unsigned i=0;i<3000;++i){
  const float x=-310+random()*650,z=-400+random()*1100;
  float y=-30+random()*65;const bool hit=ground.height_at(x,z,y,8,16,y);
  float nx=x+(random()-.5f)*2,nz=z+(random()-.5f)*2;
  const bool wall=hit?ground.slide_move(x,z,y,nx,nz):false;
  const float ray[]={x,y+1.6f,z},dir[]={.6f,0,.8f};
  const bool blocked=ground.blocks_ray(ray,dir,8);
  printf("%u %u %.9g %u %.9g %.9g %u\n",i,unsigned(hit),y,unsigned(wall),nx,nz,unsigned(blocked));
 }
}
