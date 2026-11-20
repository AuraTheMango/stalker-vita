#include "vita_hud_layout.h"

#include "vita_loot_trade.h"
#include "vita_item_catalog.h"
#include <array>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cassert>
#include <cstdio>
#include <climits>
#include <string>
using GLuint=unsigned;
struct Texture {GLuint id=0;GLuint handle()const{return id;}};
struct Quad {GLuint id; std::array<float,4> destination,uv,color;};
struct Canvas {
 mutable std::vector<Quad> quads;
 void begin()const{quads.clear();}void end()const{}
 void quad(GLuint id,const float (&d)[4],const float (&s)[4],const float (&c)[4],float=0)const{
  Quad q{id};
  for(int i=0;i<4;++i){q.destination[i]=d[i];q.uv[i]=s[i];q.color[i]=c[i];}
  quads.push_back(q);
 }
};
class VitaInventoryUI {
public:
 void draw(const VitaInventory&,unsigned)const;
 void draw_weapon_ammo(const VitaInventory&,bool,float=1)const;
 void scroll_description(float);
 void draw_exchange(const VitaInventory&,const VitaLootContainer*,const VitaTraderStock&,const VitaTradeBasket&,bool,unsigned,unsigned,VitaTradeResult)const;
 void draw_hint(unsigned)const;
 float number(int,float,float,float=1)const;
 void decimal(float,unsigned,float,float)const;
 Canvas m_canvas;
 Texture m_white{1},panel{2},slots{3},portrait{4},gauges{5},trade_art{98},actor_portrait{97};
 GLuint merchant_portrait=96,hud_art=99,m_ammo_slash=112;
 std::array<GLuint,vita_item_count> m_ammo_labels{};
 std::array<Texture,vita_item_count> icons{};
 GLuint m_headers=10,m_footer=11,m_empty=12,m_statistics=40,m_condition=41,m_ru=42,m_kg=43,m_outfit_labels=44,m_outfit_values=45,m_dot=110;
 std::array<GLuint,vita_item_count> m_names{},m_descriptions{};
 std::array<GLuint,10> m_digits{100,101,102,103,104,105,106,107,108,109};
 std::array<float,vita_item_count> m_description_heights{},m_name_widths{};
 GLuint m_percent=111;
 std::array<GLuint,8> m_exchange_labels{},m_exchange_results{};
 std::array<GLuint,3> m_exchange_columns{530,531,532};
 std::array<GLuint,4> m_hints{};
 float m_scroll=0;mutable float m_scroll_limit=0;
};

#include "vita_inventory_draw.inc"
#include <fstream>
int main(int argc,char**argv){
 assert(argc==2);
 VitaInventoryUI ui;
 for(unsigned i=0;i<vita_item_count;++i){ui.m_ammo_labels[i]=550+i;ui.icons[i].id=200+i;ui.m_names[i]=300+i;ui.m_descriptions[i]=400+i;ui.m_description_heights[i]=250;ui.m_name_widths[i]=232;}
 for(unsigned i=0;i<8;++i){ui.m_exchange_labels[i]=500+i;ui.m_exchange_results[i]=510+i;}
 for(unsigned i=0;i<4;++i)ui.m_hints[i]=520+i;
 auto dump=[&](const char*name){std::ofstream out(std::string(argv[1])+"/"+name);
  for(auto&q:ui.m_canvas.quads){out<<q.id;for(auto v:q.destination)out<<","<<v;for(auto v:q.uv)out<<","<<v;for(auto v:q.color)out<<","<<v;out<<"\n";}
 };
 auto bounds=[&](){
  for(const auto&q:ui.m_canvas.quads){
   assert(q.id);
   for(float v:q.destination)assert(std::isfinite(v));
   for(float v:q.uv)assert(std::isfinite(v));
   assert(q.destination[0]>=0&&q.destination[1]>=0&&q.destination[2]>=0&&q.destination[3]>=0);
   assert(q.destination[0]+q.destination[2]<=1.001&&q.destination[1]+q.destination[3]<=1.001);
   assert(q.uv[0]>=0&&q.uv[1]>=0&&q.uv[0]+q.uv[2]<=1.001&&q.uv[1]+q.uv[3]<=1.001);
  }
 };
 VitaInventory inv;inv.grant_starting_equipment();inv.receive_flash();
 for(unsigned i=4;i<vita_item_count-1;++i)assert(inv.add(static_cast<VitaInventoryItem>(i),2));
 assert(inv.receive_strelok_flash());
 inv.toggle_pistol(VitaInventoryItem::Pistol);
 for(unsigned selection=0;selection<vita_item_count;++selection){
  ui.draw(inv,77);bounds();inv.move_selection(1);
 }
 ui.draw(inv,77);dump("inventory.csv");
 VitaInventory report;report.grant_starting_equipment();report.add(VitaInventoryItem::Pistol,5);report.add(VitaInventoryItem::Ammunition,100);
 report.add(VitaInventoryItem::Medkit,1);report.add(VitaInventoryItem::Bread,2);report.add(VitaInventoryItem::CannedFood,1);report.add(VitaInventoryItem::Bandage,1);report.add(VitaInventoryItem::Sausage,1);
 ui.draw(report,100);bounds();dump("weight-report.csv");
 // All price/cash glyph rectangles and RU units have a physical gap inside their frames.
 for(const auto&unit:ui.m_canvas.quads)if(unit.id==42){
  const float ux=unit.destination[0]*960,uy=unit.destination[1]*544;
  for(const auto&digit:ui.m_canvas.quads)if(digit.id>=100&&digit.id<=109&&
   std::abs(digit.destination[1]*544-uy)<5&&digit.destination[0]*960<ux&&
   digit.destination[0]*960>(uy<350?545:690)){
    assert((digit.destination[0]+digit.destination[2])*960<=ux+.01f);
  }
  assert((unit.destination[0]+unit.destination[2])*960<=(uy<350?644:798));
 }

 VitaTraderStock stock;VitaTradeBasket basket;inv.give_money(1500);
 basket.add(true,VitaInventoryItem::Medkit,1,inv,stock);
 basket.add(false,VitaInventoryItem::NoviceOutfit,1,inv,stock);
 ui.draw_exchange(inv,nullptr,stock,basket,true,4,1,VitaTradeResult::Ready);bounds();dump("trade.csv");
 for(const auto&q:ui.m_canvas.quads){
  if(q.id==97||q.id==96)assert(q.destination[1]*544>=41&&
   (q.destination[1]+q.destination[3])*544<=135);
  if(q.id==500||q.id==501)assert(q.destination[1]*544>=6&&
   (q.destination[1]+q.destination[3])*544<=37.1);
  if(q.id>=100&&q.id<=109&&q.destination[0]*960<130&&q.destination[1]*544>140&&q.destination[1]*544<180)
   assert(q.destination[1]*544>=150&&(q.destination[1]+q.destination[3])*544<177);
 }
 VitaLootWorld loot;loot.body(0,{0,0,0});
 ui.draw_exchange(inv,&loot.containers[0],stock,{},true,1,1,VitaTradeResult::Ready);bounds();dump("loot.csv");
 // All inventory icons fit, no intersection between bag rectangles, equipped slots stay outside bag.
 ui.draw(inv,77);std::vector<Quad> bag;
 for(const auto&q:ui.m_canvas.quads)if(q.id>=200&&q.id<200+vita_item_count&&q.destination[0]<.34&&q.destination[1]>.36)bag.push_back(q);
 assert(bag.size()==vita_item_count-2);
 for(size_t a=0;a<bag.size();++a)for(size_t b=a+1;b<bag.size();++b){
  const auto&x=bag[a].destination;const auto&y=bag[b].destination;
  assert(x[0]+x[2]<=y[0]||y[0]+y[2]<=x[0]||x[1]+x[3]<=y[1]||y[1]+y[3]<=x[1]);
 }
 assert(inv.remove(VitaInventoryItem::Pistol,1));ui.draw(inv,100);bounds();
 bool descriptions=false;for(auto&q:ui.m_canvas.quads)descriptions|=q.id>=400&&q.id<400+vita_item_count;assert(descriptions);
 // Original ammo frame remains anchored and visible at all HUD sizes / ammo types.
 for(unsigned slot=0;slot<3;++slot)for(unsigned mode=0;mode<2;++mode)for(unsigned percent:{75u,100u,150u}){
  VitaInventory fresh;fresh.grant_starting_equipment();auto state=fresh.state();state.items[12]=state.items[13]=1;state.weapon=slot==0?1:slot==1?3:4;state.equipped=1;
  state.primary=slot?slot:0;state.ammo_modes[slot]=mode;state.loaded=0;
  state.primary_loaded={0,0};state.items[vita_item_index(VitaInventory::ammo_item(slot,mode))]=slot==0?4096:900;
  state.reserve=state.items[1];assert(inv.restore(state));
  ui.draw_weapon_ammo(inv,false,percent/100.f);bounds();
  bool frame=false,icon=false,slash=false;
  for(const auto&q:ui.m_canvas.quads){frame|=q.id==99;icon|=q.id==ui.icons[vita_item_index(inv.current_ammo())].id;slash|=q.id==112;}
  assert(frame&&icon&&slash);
  if(slot==0&&mode==0)dump((std::string("ammo-")+std::to_string(percent)+".csv").c_str());
 }
 puts("Inventory / trade / loot: original draw code, all 24 selections, bounds, UVs, grid, and inventory without PM passed");
}
