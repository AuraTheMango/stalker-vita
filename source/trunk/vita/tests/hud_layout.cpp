#include "vita_hud_layout.h"
#include "vita_player_movement.h"
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>
#include <cassert>
#include <fstream>
using GLuint=unsigned;
const int GL_DEPTH_TEST=0,GL_TEXTURE0=0,GL_ARRAY_BUFFER=0,GL_FLOAT=0,GL_FALSE=0;
void glDisable(int){}void glEnable(int){}void glActiveTexture(int){}void glUniform1i(int,int){}
void glBindBuffer(int,int){}void glVertexAttribPointer(int,int,int,int,int,void*){}void glEnableVertexAttribArray(int){}
struct Texture{unsigned id;unsigned handle()const{return id;}};
inline void vita_reticle_color(int relation,float(&c)[4]){c[0]=c[1]=c[2]=c[3]=1;}
class VitaHud{
public:
 struct Rect{float x,y,width,height;};
 struct Quad{unsigned id;std::array<float,4>d,u;};
 mutable std::vector<Quad>quads;
 Texture cursor{900},atlas{99},stance{901},map{902},compass{903};
 unsigned m_quad=1,m_texture=0,m_position=0;float map_bounds[4]={-100,-100,100,100};
 struct Program{void use()const{}}m_program;
 float hud_scale=1;mutable float m_group_scale=1,m_anchor_x=0,m_anchor_y=0;
 const float stance_width=132,stance_height=228;
 void draw_status(const VitaPlayerMovement&,unsigned)const;
 void draw(const float*,float,bool,const float*,const VitaPlayerMovement&,unsigned,bool,int)const;
 void sprite(unsigned tex,const Rect&r,const float(&ux)[3],const float(&uy)[3],float,float,const float(&)[4])const{
  auto d=vita_hud_rect(r.x,r.y,r.width,r.height,m_group_scale,m_anchor_x,m_anchor_y);
  quads.push_back({tex,{d[0]/960,d[1]/544,d[2]/960,d[3]/544},{ux[2],uy[2],ux[0],uy[1]}});
 }
 void atlas_sprite(const Rect&r,const Rect&s)const{
  constexpr float white[]={1,1,1,1};
  sprite(atlas.handle(),r,{s.width/620,0,s.x/620},{0,s.height/224,s.y/224},1,0,white);
 }
};
#include "hud_draw.inc"
int main(int argc,char**argv){
 assert(argc==2);VitaHud hud;VitaPlayerMovement movement;const float eye[]={0,1.7f,0},objective[]={90,0,-90};
 for(unsigned size:{75u,100u,150u}){
  hud.hud_scale=size/100.f;hud.quads.clear();hud.draw(eye,.5f,true,objective,movement,77,true,0);
  assert(hud.m_group_scale==1);
  for(const auto&q:hud.quads){
   assert(q.d[0]>=0&&q.d[1]>=0&&q.d[0]+q.d[2]<1.001&&q.d[1]+q.d[3]<1.001);
   if(q.id==900&&q.d[0]>.4f)assert(std::abs(q.d[2]*960-28)<.01f);
  }
  std::ofstream out(std::string(argv[1])+"/hud-"+std::to_string(size)+".csv");
  for(auto&q:hud.quads){out<<q.id;for(float v:q.d)out<<","<<v;for(float v:q.u)out<<","<<v;out<<",1,1,1,1\n";}
 }
}
