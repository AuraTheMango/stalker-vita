#include "vita_cform_ground.h"
#include <array>
#include <vector>
#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <chrono>
struct Mesh{
 struct V{float x,y,z;};
 std::vector<V>v;std::vector<std::array<uint32_t,4>>f;std::vector<unsigned char>bytes;size_t cursor=0;
 void tri(V a,V b,V c){const auto i=uint32_t(v.size());v.insert(v.end(),{a,b,c});f.push_back({i,i+1,i+2,0});}
 void quad(V a,V b,V c,V d){tri(a,b,c);tri(a,c,d);}
 void floor(float z0,float z1,float y){quad({-.7f,y,z0},{.7f,y,z0},{.7f,y,z1},{-.7f,y,z1});}
 void wall(float z,float height){quad({-.7f,0,z},{.7f,0,z},{.7f,height,z},{-.7f,height,z});}
 void finish(float angle=0){
  float c=std::cos(angle),s=std::sin(angle);
  for(auto&p:v){float x=p.x,z=p.z;p.x=c*x-s*z;p.z=s*x+c*z;}
  struct Header{uint32_t version,vertices,faces;V minimum,maximum;}h{4,uint32_t(v.size()),uint32_t(f.size()),{-20,-5,-20},{20,10,20}};
  bytes.resize(sizeof(h)+v.size()*sizeof(V)+f.size()*16);auto*out=bytes.data();
  memcpy(out,&h,sizeof(h));out+=sizeof(h);memcpy(out,v.data(),v.size()*sizeof(V));out+=v.size()*sizeof(V);memcpy(out,f.data(),f.size()*16);
 }
 bool load(VitaCformGround&g){cursor=0;return g.load([](void*p,void*out,size_t n){auto&m=*(Mesh*)p;if(n>m.bytes.size()-m.cursor)return false;memcpy(out,m.bytes.data()+m.cursor,n);m.cursor+=n;return true;},this,bytes.size(),0,0,{},nullptr,true);}
};
Mesh staircase(unsigned count,bool gap,bool ceiling=false,float angle=0){
 Mesh m;m.floor(-3,7,0); // Original stairs may overlap a lower floor.
 for(unsigned i=0;i<count;++i){float y=(i+1)*.2f,z=i*.22f;m.floor(z,z+(gap?.16f:.22f),y);m.quad({-.7f,i*.2f,z},{.7f,i*.2f,z},{.7f,y,z},{-.7f,y,z});}
 m.floor(count*.22f,7,count*.2f);
 if(ceiling)m.floor(-.2f,3,1.85f);
 m.finish(angle);return m;
}
void run_stairs(float angle,float distance,bool gap,float body){
 auto mesh=staircase(8,gap,false,angle);VitaCformGround g;assert(mesh.load(g));
 const float sx=-std::sin(angle),sz=std::cos(angle);
 float x=-sx,z=-sz,y=0;unsigned contacts=0;
 for(unsigned i=0;i<unsigned(4.5f/distance)+1;++i){float nx=x+sx*distance,nz=z+sz*distance,ny=y;bool wall=false;
  assert(g.walk_move(x,z,y,nx,ny,nz,wall,nullptr,.45f,body));
  assert(ny-y<=.4501f);x=nx;z=nz;y=ny;contacts+=wall;
  float saved=y;assert(g.support_at(x,z,y,.25f,.25f,saved,false)&&std::abs(saved-y)<.01f);
 }
 assert(x*sx+z*sz>3&&std::abs(y-1.6f)<.01f);
 for(unsigned i=0;i<unsigned(4.5f/distance)+1;++i){float nx=x-sx*distance,nz=z-sz*distance,ny=y;bool wall=false;
  assert(g.walk_move(x,z,y,nx,ny,nz,wall,nullptr,.45f,body));x=nx;z=nz;y=ny;
 }
 assert(x*sx+z*sz<-.8f&&std::abs(y)<.01f);
 printf("stairs angle=%.2f step=%.2f gap=%u body=%.2f contacts=%u ascent/descent/save support passed\n",angle,distance,unsigned(gap),body,contacts);
}
int main(int argc,char**argv){
 assert(argc==3);
 for(float angle:{0.f,1.5707963f,.7853982f})for(float step:{.02f,.07f,.18f,.30f})for(bool gap:{false,true})
  run_stairs(angle,step,gap,1.8f);
 run_stairs(.7853982f,.07f,true,1.2f);
 // A high wall cannot become a step.
 Mesh wall;wall.floor(-3,7,0);wall.wall(0,2);wall.finish();VitaCformGround g;assert(wall.load(g));
 float x=0,z=-1,y=0;
 for(unsigned i=0;i<60;++i){float nx=x,nz=z+.07f,ny=y;bool contact=false;assert(g.walk_move(x,z,y,nx,ny,nz,contact,nullptr));x=nx;z=nz;y=ny;}
 assert(z<-.29f&&y<.01f);
 // Closed interactive doors remain blocking.
 auto flat=staircase(0,false);assert(flat.load(g));z=-1;y=0;
 VitaCformGround::DoorPanel door{{{-.7f,-.04f},{.7f,-.04f},{.7f,.04f},{-.7f,.04f}},0,2};
 for(unsigned i=0;i<60;++i){float nx=x,nz=z+.07f,ny=y;bool contact=false;assert(g.walk_move(x,z,y,nx,ny,nz,contact,&door));x=nx;z=nz;y=ny;}
 assert(z<-.32f);
 // Standing may not raise the head through a low ceiling; crouching fits.
 auto low=staircase(1,false,true);assert(low.load(g));z=-1;y=0;
 for(unsigned i=0;i<40;++i){float nx=x,nz=z+.05f,ny=y;bool contact=false;assert(g.walk_move(x,z,y,nx,ny,nz,contact,nullptr,.45f,1.8f));x=nx;z=nz;y=ny;}
 assert(z<0&&y<.01f);
 z=-1;y=0;
 for(unsigned i=0;i<40;++i){float nx=x,nz=z+.05f,ny=y;bool contact=false;assert(g.walk_move(x,z,y,nx,ny,nz,contact,nullptr,.45f,1.2f));x=nx;z=nz;y=ny;}
 assert(z>.8f&&y>.19f);
 // Reproduction at the logged Agroprom coordinates, using the original collision map.
 FILE*f=fopen(argv[1],"rb");assert(f);
 assert(g.load([](void*p,void*out,size_t n){return fread(out,1,n,(FILE*)p)==n;},f,std::filesystem::file_size(argv[1]),28,-12,{},nullptr,true));fclose(f);
 x=28.64f;z=-13.5f;y=-.2f;g.height_at(x,z,0,1,1,y);
 auto start=std::chrono::steady_clock::now();
 for(unsigned i=0;i<90;++i){float nx=x,nz=z+.04f,ny=y;bool contact=false;
  assert(g.walk_move(x,z,y,nx,ny,nz,contact,nullptr));x=nx;z=nz;y=ny;
 }
 assert(z>-10&&y>2.4f);
 printf("Agroprom ascent x=%.3f y=%.3f z=%.3f host_us=%lld\n",x,y,z,(long long)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count());
 for(unsigned i=0;i<110;++i){float nx=x,nz=z-.04f,ny=y;bool contact=false;
  assert(g.walk_move(x,z,y,nx,ny,nz,contact,nullptr));x=nx;z=nz;y=ny;
 }
 assert(z<-13.5f&&y<.2f);
 printf("Agroprom descent x=%.3f y=%.3f z=%.3f\n",x,y,z);
 f=fopen(argv[2],"rb");assert(f);
 assert(g.load([](void*p,void*out,size_t n){return fread(out,1,n,(FILE*)p)==n;},f,std::filesystem::file_size(argv[2]),-255,-130,{},nullptr,true));fclose(f);
 x=-255.5f;z=-134.5f;y=-23.38f;assert(g.height_at(x,z,y,1,1,y));
 for(unsigned i=0;i<150;++i){float nx=x,nz=z+.04f,ny=y;bool contact=false;
  assert(g.walk_move(x,z,y,nx,ny,nz,contact,nullptr,.65f,1.8f));x=nx;z=nz;y=ny;
 }
 assert(z>-128.6f&&y>-21.1f);
 printf("Cordon bunker ascent x=%.3f y=%.3f z=%.3f\n",x,y,z);
 for(unsigned i=0;i<150;++i){float nx=x,nz=z-.04f,ny=y;bool contact=false;
  assert(g.walk_move(x,z,y,nx,ny,nz,contact,nullptr,.65f,1.8f));x=nx;z=nz;y=ny;
 }
 assert(z<-134.4f&&y<-23.2f);
 printf("Cordon bunker descent x=%.3f y=%.3f z=%.3f\n",x,y,z);

}
