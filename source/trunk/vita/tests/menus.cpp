
#include <array>
#include <vector>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <fstream>
using GLuint=unsigned;
enum class VitaSaveMessage { None, Saved, SaveFailed, LoadFailed, Unsafe, ConfirmNew, Count };
struct VitaGLDevice {int begins=0,presents=0; void begin_frame(float,float,float){++begins;} void present(){++presents;}};
struct Texture {GLuint id; GLuint handle() const{return id;}};
struct Quad {GLuint id;std::array<float,4> dest,source,color;};
struct Canvas {
 mutable std::vector<Quad> quads;
 void begin()const{quads.clear();}void end()const{}
 void quad(GLuint id,const float (&d)[4],const float (&s)[4],const float (&c)[4],float=0)const{
  Quad q{id};for(int i=0;i<4;++i){q.dest[i]=d[i];q.source[i]=s[i];q.color[i]=c[i];}quads.push_back(q);
 }
};
class VitaMainMenu {
public:
 void draw(VitaGLDevice&,unsigned,bool,bool,bool=false,bool=false,VitaSaveMessage=VitaSaveMessage::None)const;
 Texture background{100},atlas{101};
 Canvas m_canvas;
 std::array<GLuint,5> m_options{1,2,3,4,5};
 std::array<GLuint,6> m_pause_options{11,12,13,14,15,16};
 std::array<GLuint,5> m_messages{30,31,32,33,34};
 GLuint m_hint=21,m_error=22,m_pause_hint=23,m_dead=24;
};
class VitaOptionsMenu{
public:
 void draw(VitaGLDevice&,const VitaMainMenu&,unsigned,bool=false)const;
 Canvas m_canvas;
 std::array<GLuint,13> m_labels{51,52,53,54,55,56,57,58,59,60,61,62,63};
 std::array<GLuint,4> m_tabs{70,71,72,73};
 unsigned tab=0;unsigned rows()const{return tab==0?3:tab==1?5:tab==2?3:2;}
 unsigned setting_row(unsigned choice)const{return tab==0?5+choice:tab==1?choice:tab==2?(choice==2?12:8+choice):10+choice;}
 Texture frame{84},background{85},tabs{86};GLuint m_controls=87;
 GLuint m_white=80,m_title=81,m_hint=82,m_restart=83;
};
#include "vita_menu_draw.inc"

int main(int argc,char**argv){
 VitaMainMenu menu;VitaGLDevice graphics;
 menu.draw(graphics,3,true,false);
 assert(menu.m_canvas.quads.size()==9&&graphics.presents==1);
 for(unsigned choice=0;choice<6;++choice){
  VitaGLDevice pause;menu.draw(pause,choice,true,false,true);
  assert(pause.presents==0);const auto&q=menu.m_canvas.quads;assert(q.size()==10);
  assert(q[3+choice].color[0]>.9f);
  for(unsigned i=0;i<6;++i){assert(q[3+i].id==11+i);assert(q[3+i].dest[1]+q[3+i].dest[3]<506.f/544);}
 }
 VitaOptionsMenu options;
 for(unsigned tab=0;tab<4;++tab)for(unsigned choice=0;choice<options.rows();++choice){
  options.tab=tab;VitaGLDevice d;options.draw(d,menu,choice,true);assert(d.presents==1);
  unsigned rows=0,highlight=0;
  for(const auto&quad:options.m_canvas.quads){
   if(quad.id>=51&&quad.id<=63){++rows;if(quad.color[0]==.98f)++highlight;}
   for(float c:quad.dest)assert(std::isfinite(c));
   assert(quad.dest[0]>=0&&quad.dest[1]>=0&&quad.dest[0]+quad.dest[2]<=1.001&&quad.dest[1]+quad.dest[3]<=1.001);
  }assert(rows==options.rows()&&highlight==1);
  if(argc==2&&choice==0){std::ofstream out(std::string(argv[1])+"/options-"+std::to_string(tab)+".csv");
   for(auto&q:options.m_canvas.quads){out<<q.id;for(auto v:q.dest)out<<","<<v;for(auto v:q.source)out<<","<<v;for(auto v:q.color)out<<","<<v;out<<"\n";}}

 }

 puts("Menus: five startup choices, six pause choices, four category tabs and all options, highlight, solid panels, bounds and one presentation passed");
}
