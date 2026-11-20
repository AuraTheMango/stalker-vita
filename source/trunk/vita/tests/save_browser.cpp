#include "vita_save.h"
#include <cassert>
#include <array>
#include <vector>
#include <string>
#include <set>
#include <map>
#include <ctime>
#include <cstdio>
#include <cmath>
using GLuint=unsigned;using SceFontLibHandle=unsigned;
constexpr unsigned SCE_SYSMODULE_PGF=1;
bool pgf_loaded=false,fail_module=false;unsigned module_loads=0;
int sceSysmoduleIsLoaded(unsigned){return pgf_loaded?0:-1;}
int sceSysmoduleLoadModule(unsigned){++module_loads;if(fail_module)return -1;pgf_loaded=true;return 0;}

unsigned next_id=1,uploads=0,fonts=0,closes=0,loads=0,scans=0,fail_at=0;bool fail_font=false;
std::set<unsigned>deleted;std::map<unsigned,std::string>text;
void glDeleteTextures(size_t n,const unsigned*ids){for(size_t i=0;i<n;++i)if(ids[i])assert(deleted.insert(ids[i]).second);}
unsigned new_texture(std::string value=""){
 ++uploads;if(uploads==fail_at)return 0;unsigned id=next_id++;text[id]=std::move(value);return id;
}
unsigned open_ui_font(unsigned&lib){++fonts;lib=1;return fail_font?0:1;}
void sceFontClose(unsigned){++closes;}void sceFontDoneLib(unsigned){}
const char* vita_text(const char*f,const char*){return f;}
struct TextImage{std::string value;TextImage(unsigned,unsigned){}
 void line(unsigned,const std::string&s,int,int,int){value+=s+"\n";}
 unsigned upload(bool){return new_texture(value);}
};
unsigned upload_centered_label(unsigned,const char*s,unsigned){return new_texture(s);}
struct Texture{unsigned id=0;unsigned handle()const{return id;}void release(){if(id)glDeleteTextures(1,&id);id=0;}};
struct Quad{unsigned id;std::array<float,4>d,u;};
struct Canvas{unsigned initializes=0;mutable std::vector<Quad>quads;bool initialize(){++initializes;return true;}void release(){}
 void begin()const{quads.clear();}void end()const{}
 void quad(unsigned id,const float(&d)[4],const float(&u)[4],const float(&)[4],float=0)const{
  Quad q{id};for(unsigned i=0;i<4;++i){q.d[i]=d[i];q.u[i]=u[i];}quads.push_back(q);
 }
};
struct VitaGLDevice{unsigned presents=0;void begin_frame(float,float,float){}void present(){++presents;}};
struct VitaMainMenu{Texture background;};
class VitaSaveUI{public:
 bool initialize(bool);bool refresh(const std::array<VitaSaveEntry,vita_save_slots>&);
 void draw(VitaGLDevice&,const VitaMainMenu&,unsigned,unsigned)const;void release();
 Texture artwork,common;std::array<Texture,vita_zone_count>previews;
 const auto&entries()const{return m_entries;}unsigned refreshed_slots()const{return m_refreshed_slots;}
 Canvas m_canvas;bool m_saving=false,m_initialized=false;
 std::array<GLuint,2>m_titles{},m_actions{};
 std::array<VitaSaveEntry,vita_save_slots>m_entries{};std::array<bool,vita_save_slots>m_entry_ready{};
 unsigned m_refreshed_slots=0,m_title=0,m_hint=0,m_empty=0,m_overwrite=0,m_delete=0,m_invalid=0;
 std::array<GLuint,3>m_buttons{};std::array<GLuint,vita_save_slots>m_rows{},m_details{},m_slot_names{};
};
#include "save_browser_ui.inc"
uint64_t sceKernelGetProcessTimeWide(){static uint64_t tick=0;return ++tick;}
void*g_boot_log=nullptr;void write_boot_log(void*,const char*){}
std::array<VitaSaveEntry,vita_save_slots>catalog{};
std::array<VitaSaveEntry,vita_save_slots>list_vita_saves(const char*){++scans;return catalog;}
bool load_level_dds_texture(const std::string&,Texture&t,unsigned budget,bool&,unsigned size){
 assert(size<=512&&budget<=512u*512u*4);assert(!t.id);t.id=new_texture();++loads;return true;
}
#include "save_browser_prepare.inc"
int main(){
 VitaSaveUI ui;VitaGLDevice graphics;VitaMainMenu menu;menu.background.id=new_texture();
 assert(prepare_vita_save_browser(ui));assert(module_loads==1);assert(loads==2+vita_zone_count&&scans==1&&ui.refreshed_slots()==9);
 const auto initial_fonts=fonts,initial_uploads=uploads;const auto names=ui.m_slot_names,rows=ui.m_rows,details=ui.m_details;
 assert(ui.initialize(true));assert(text[ui.m_title]=="Sauvegarder la partie"&&text[ui.m_buttons[0]]=="Sauvegarder");
 assert(prepare_vita_save_browser(ui));assert(ui.initialize(true));
 assert(fonts==initial_fonts&&uploads==initial_uploads&&loads==6&&scans==2&&ui.refreshed_slots()==0);
 assert(ui.m_canvas.initializes==1);
 catalog[1].exists=catalog[1].valid=true;catalog[1].saved_at=1795200000;catalog[1].zone=2;catalog[1].story=7;catalog[1].play_seconds=125;
 assert(prepare_vita_save_browser(ui));assert(ui.refreshed_slots()==1&&uploads==initial_uploads+2&&fonts==initial_fonts+1);
 assert(ui.m_slot_names==names&&ui.m_rows[0]==rows[0]&&ui.m_details[0]==details[0]);
 assert(ui.m_rows[1]!=rows[1]&&deleted.count(rows[1])&&deleted.count(details[1]));
 assert(text[ui.m_details[1]].find("Agroprom")!=std::string::npos&&text[ui.m_details[1]].find("Trouver Spectre")!=std::string::npos);
 // A failed changed-row allocation keeps the previous metadata and both old texture handles.
 const auto old_row=ui.m_rows[1],old_details=ui.m_details[1];const auto old_entry=ui.entries()[1];
 catalog[1].play_seconds+=60;fail_at=uploads+1;
 assert(!prepare_vita_save_browser(ui));assert(ui.m_rows[1]==old_row&&ui.m_details[1]==old_details&&vita_save_entry_equal(ui.entries()[1],old_entry));
 assert(!deleted.count(old_row)&&!deleted.count(old_details));fail_at=0;assert(prepare_vita_save_browser(ui));
 assert(ui.entries()[1].play_seconds==185);
 // Deletion, corruption and a changed autosave update only the affected slot.
 catalog[1]={};assert(prepare_vita_save_browser(ui));assert(ui.refreshed_slots()==1&&text[ui.m_rows[1]].find("Vide")!=std::string::npos);
 catalog[1].exists=true;assert(prepare_vita_save_browser(ui));assert(text[ui.m_rows[1]].find("Illisible")!=std::string::npos);
 catalog[6].exists=catalog[6].valid=true;catalog[6].zone=3;catalog[6].story=6;
 assert(prepare_vita_save_browser(ui));assert(ui.refreshed_slots()==1&&text[ui.m_rows[6]].find("Auto 01")!=std::string::npos);
 const auto warmed_fonts=fonts,warmed_uploads=uploads;
 for(bool saving:{false,true,false,true})for(unsigned selection=0;selection<vita_save_slots;++selection)for(unsigned confirmation=0;confirmation<4;++confirmation){
  assert(ui.initialize(saving));ui.draw(graphics,menu,selection,confirmation);
  for(const auto&q:ui.m_canvas.quads){
   assert(q.id);for(unsigned i=0;i<4;++i)assert(std::isfinite(q.d[i])&&std::isfinite(q.u[i]));
   assert(q.d[0]>=0&&q.d[1]>=0&&q.d[0]+q.d[2]<=1.001&&q.d[1]+q.d[3]<=1.001);
   assert(q.u[0]>=0&&q.u[1]>=0&&q.u[0]+q.u[2]<=1.001&&q.u[1]+q.u[3]<=1.001);
  }
 }
 assert(fonts==warmed_fonts&&uploads==warmed_uploads&&loads==6&&ui.m_canvas.initializes==1);
 assert(ui.initialize(false));ui.draw(graphics,menu,6,0);
 bool preview=false;for(const auto&q:ui.m_canvas.quads)preview|=q.id==ui.previews[3].id;assert(preview);
 ui.release();ui.release();menu.background.release();assert(deleted.size()==next_id-1);
 fail_font=true;assert(!ui.initialize(false));fail_font=false;assert(ui.initialize(false));ui.release();assert(deleted.size()==next_id-1);
 pgf_loaded=false;fail_module=true;assert(!ui.initialize(false));fail_module=false;
 assert(ui.initialize(false));ui.release();assert(deleted.size()==next_id-1);
 puts("Production save browser: shared save/load modes, no reload on warm open, fresh slot changes, atomic allocation failure, preview, bounds/UVs and exact cleanup passed");
}
