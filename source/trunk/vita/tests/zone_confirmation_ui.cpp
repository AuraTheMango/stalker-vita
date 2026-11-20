#include "vita_campaign.h"
#include <array>
#include <vector>
#include <cassert>
#include <cmath>
#include <cstdio>
using GLuint=unsigned;
struct Quad{unsigned id;std::array<float,4>d,u,c;};
struct Canvas{
 mutable std::vector<Quad>quads;
 void begin()const{quads.clear();}void end()const{}
 void quad(unsigned id,const float(&d)[4],const float(&u)[4],const float(&c)[4],float=0)const{
  Quad q{id};for(unsigned i=0;i<4;++i){q.d[i]=d[i];q.u[i]=u[i];q.c[i]=c[i];}quads.push_back(q);
 }
};
struct Texture{unsigned id=20;unsigned handle()const{return id;}};
class VitaTraderUI{public:
 void draw_zone_confirmation(unsigned)const;
 std::array<unsigned,vita_zone_count>m_zone_questions{10,11,12,13};
 unsigned m_zone_white=1,m_zone_title=2,m_zone_choices=3;
 Texture talk_art;Canvas m_canvas;
};
#include "zone_draw.inc"
int main(){
 VitaTraderUI ui;
 for(unsigned zone=0;zone<vita_zone_count;++zone){
  ui.draw_zone_confirmation(zone);assert(ui.m_canvas.quads.size()==8);bool question=false,choices=false;
  for(const auto&q:ui.m_canvas.quads){
   question|=q.id==ui.m_zone_questions[zone];choices|=q.id==ui.m_zone_choices;
   for(unsigned i=0;i<4;++i)assert(std::isfinite(q.d[i])&&std::isfinite(q.u[i]));
   assert(q.d[0]>=0&&q.d[1]>=0&&q.d[0]+q.d[2]<=1.001&&q.d[1]+q.d[3]<=1.001);
   assert(q.u[0]>=0&&q.u[1]>=0&&q.u[0]+q.u[2]<=1.001&&q.u[1]+q.u[3]<=1.001);
  }assert(question&&choices);assert(ui.m_canvas.quads[0].c[3]>.5f&&ui.m_canvas.quads[0].c[3]<1);
 }
 ui.talk_art.id=0;ui.draw_zone_confirmation(0);assert(ui.m_canvas.quads.size()==5);
 puts("Production zone popup: original frame, dimmed world, destination and choices, bounds/UVs and fallback passed");
}
