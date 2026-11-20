
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
 std::array<GLuint,12> m_labels{51,52,53,54,55,56,57,58,59,60,61,62};
 std::array<GLuint,4> m_tabs{70,71,72,73};
 unsigned tab=0;unsigned rows()const{return tab==0?3:tab==1?5:2;}
 unsigned setting_row(unsigned choice)const{return tab==0?5+choice:tab==1?choice:tab==2?8+choice:10+choice;}
 Texture frame{84},background{85},tabs{86};GLuint m_controls=87;
 GLuint m_white=80,m_title=81,m_hint=82,m_restart=83;
};

constexpr unsigned vita_manual_slots=6,vita_save_slots=9;
class VitaSaveUI{
public:
 void draw(VitaGLDevice&,const VitaMainMenu&,unsigned,unsigned)const;
 Canvas m_canvas;bool m_saving=false;
 Texture artwork{700},common{701},preview{729};
 GLuint m_title=702,m_hint=703,m_empty=704,m_overwrite=705,m_delete=706,m_invalid=707;
 std::array<GLuint,3>m_buttons{708,709,710};
 std::array<GLuint,9>m_rows{711,712,713,714,715,716,717,718,719};
 std::array<GLuint,9>m_details{720,721,722,723,724,725,726,727,728};
 std::array<GLuint,9>m_slot_names{730,731,732,733,734,735,736,737,738};
};
#include "vita_save_draw.inc"
int main(int argc,char**argv){
 assert(argc==2);VitaMainMenu menu;menu.background.id=740;VitaSaveUI ui;
 for(bool saving:{false,true})for(unsigned selected=0;selected<(saving?6:9);++selected){
  ui.m_saving=saving;VitaGLDevice d;ui.draw(d,menu,selected,0);assert(d.presents==1);
  unsigned rows=0,highlight=0;
  for(const auto&q:ui.m_canvas.quads){
   assert(q.id);
   for(float n:q.dest)assert(std::isfinite(n));
   assert(q.dest[0]>=0&&q.dest[1]>=0&&q.dest[0]+q.dest[2]<=1.001&&q.dest[1]+q.dest[3]<=1.001);
   assert(q.source[0]>=0&&q.source[1]>=0&&q.source[0]+q.source[2]<=1.001&&q.source[1]+q.source[3]<=1.001);
   if(q.id>=711&&q.id<=719){++rows;if(q.color[1]==1)++highlight;
    assert(q.dest[0]>480.f/1024&&q.dest[0]+q.dest[2]<929.f/1024);
    assert(q.dest[1]>(saving?264.f:389.f)/768&&q.dest[1]+q.dest[3]<(saving?571.f:589.f)/768);
   }
  }
  assert(rows==(saving?6:9)&&highlight==1);
  if(selected==1){std::ofstream f(std::string(argv[1])+(saving?"/save.csv":"/load.csv"));
   for(auto&q:ui.m_canvas.quads){f<<q.id;for(float n:q.dest)f<<","<<n;for(float n:q.source)f<<","<<n;for(float n:q.color)f<<","<<n;f<<"\n";}}
 }
 puts("Original save/load dialogs: every row inside list frame, nine-slice UVs, selection and full bounds passed");
}
