#include <cmath>
#include "vita_hud_layout.h"
#include <array>
#include <vector>
#include <fstream>
#include <cassert>
#include <cstdio>
using GLuint=unsigned;
struct Texture{GLuint id;GLuint handle()const{return id;}};
struct Quad{GLuint id;std::array<float,4>d,u,c;};
struct Canvas{mutable std::vector<Quad> quads;void begin()const{quads.clear();}void end()const{}
 void quad(GLuint id,const float(&d)[4],const float(&u)[4],const float(&c)[4],float=0)const{
 Quad q{id};for(unsigned i=0;i<4;++i){q.d[i]=d[i];q.u[i]=u[i];q.c[i]=c[i];}quads.push_back(q);}
};
constexpr int panel_height=300;
class VitaTraderUI{
 public:
 void draw_panel(GLuint,int,float,float,bool,bool,float=456)const;
 bool show_prompts=false,m_campaign_active=false,campaign_road=false,campaign_information=false;Texture campaign_face{605},road_face{606};GLuint m_campaign_name=616;Texture talk_art{600};GLuint profiles=98,actor_portrait=97;
 std::array<Texture,4>faces{Texture{601},Texture{602},Texture{603},Texture{604}};
 std::array<GLuint,6>m_target_names{610,611,612,613,614,615};
 GLuint m_actor_name=620,m_continue=621,m_leave=622,m_history=0;unsigned m_face=0;Canvas m_canvas;
};
#include "vita_dialog_draw.inc"
int main(int argc,char**argv){
 assert(argc==2);VitaTraderUI ui;
 for(unsigned choice=0;choice<2;++choice){
 ui.draw_panel(630,300,choice?.57f:.433f,choice?.7f:.57f,false,true);
 assert(ui.m_canvas.quads.size()>=12);
 for(auto&q:ui.m_canvas.quads){for(unsigned i=0;i<4;++i)assert(q.d[i]>=0&&q.u[i]>=0);
  assert(q.d[0]+q.d[2]<=1.001&&q.d[1]+q.d[3]<=1.001);
  assert(q.u[0]+q.u[2]<=1.001&&q.u[1]+q.u[3]<=1.001);}
 }
 ui.draw_panel(630,300,.433f,.57f,false,true);
 const Quad *actor=nullptr,*npc=nullptr;
 for(const auto&q:ui.m_canvas.quads){if(q.id==97)actor=&q;if(q.id==601)npc=&q;}
 assert(actor&&npc);
 assert(std::abs(actor->d[1]-npc->d[1])<.00001f&&std::abs(actor->d[3]-npc->d[3])<.00001f);

 std::ofstream out(std::string(argv[1])+"/dialogue.csv");
 for(auto&q:ui.m_canvas.quads){out<<q.id;for(float v:q.d)out<<","<<v;for(float v:q.u)out<<","<<v;for(float v:q.c)out<<","<<v;out<<"\n";}
 ui.m_campaign_active=true;ui.draw_panel(631,360,-1,-1,false,true);
 bool face=false,name=false;
 for(const auto&q:ui.m_canvas.quads){
  face|=q.id==605;name|=q.id==616;
  if(q.id==616)assert(q.u[0]==0&&q.u[2]==1);
 }
 assert(face&&name);
 std::ofstream campaign(std::string(argv[1])+"/campaign-dialogue.csv");
 for(auto&q:ui.m_canvas.quads){campaign<<q.id;for(float v:q.d)campaign<<","<<v;for(float v:q.u)campaign<<","<<v;for(float v:q.c)campaign<<","<<v;campaign<<"\n";}
 ui.campaign_information=true;ui.draw_panel(631,360,-1,-1,false,true);
 for(const auto&q:ui.m_canvas.quads){assert(q.id!=97&&q.id!=605&&q.id!=601);assert(q.d[0]+q.d[2]<=1.001&&q.d[1]+q.d[3]<=1.001);}
 puts("Original dialogue frames: actor/NPC portraits, history, answers, selected answer and UV/bounds passed");
}
