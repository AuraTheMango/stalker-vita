#include "vita_save.cpp"
#include "vita_cform_ground.h"
#include "vita_trader_model.h"
#include "vita_campaign_dialogue.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cstdio>
#include <cmath>
std::vector<uint8_t> get(const std::filesystem::path&p){std::ifstream f(p,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char**argv){
 assert(argc==4);const auto assets=std::filesystem::path(argv[1]);
 std::filesystem::create_directories(argv[3]);
 // A real, pre-change v7 save, not a record emitted by the new encoder.
 VitaSaveState old;assert(read_vita_save(argv[2],old));
 assert(old.zone==1&&old.story==3&&old.inventory.flash==2&&old.worlds[3].empty()&&!old.inventory.items[23]);
 VitaSaveState surface,cave;
 assert(change_vita_zone(old,vita_zone_exits[2],surface));surface.story=5;
 assert(!vita_exit_allowed(vita_zone_exits[4],4)&&vita_exit_allowed(vita_zone_exits[4],5));
 assert(change_vita_zone(surface,vita_zone_exits[4],cave));
 assert((cave.zone==3&&cave.story==5&&cave.scout_health==std::array<unsigned,3>{}));
 assert(!(cave.flags&SaveGroup)&&cave.guards[0]==2);
 const auto file=assets/"levels/l03u_agr_underground/level.cform";
 FILE*f=fopen(file.c_str(),"rb");assert(f);VitaCformGround ground;
 const auto reader=[](void*p,void*out,size_t n){return fread(out,1,n,(FILE*)p)==n;};
 assert(ground.load(reader,f,std::filesystem::file_size(file),cave.eye[0],cave.eye[2],{},nullptr,true));fclose(f);
 const float up[]={0,1,0};
 for(const auto&p:{vita_zone_exits[4].arrival,vita_strelok_ladder_bottom,vita_strelok_ladder_top}){
  float y=p[1];assert(ground.height_at(p[0],p[2],y,.25f,.25f,y));
  const float base[]={p[0],y+.05f,p[2]};assert(!ground.blocks_ray(base,up,1.8f));
  printf("Safe landing %.3f %.3f %.3f\n",p[0],y,p[2]);
 }
 unsigned reachable=0;
 for(int ix=0;ix<=12;++ix)for(int iz=0;iz<=12;++iz){
  const float x=-71.8f+ix*.25f,z=-66.2f+iz*.25f;float y=-6.8732f;
  if(!ground.height_at(x,z,y,.25f,.25f,y)||
    !vita_stash_near({x,y,z},vita_strelok_flash,2.4f,2.f))continue;
  const float base[]={x,y+.05f,z};if(ground.blocks_ray(base,up,1.8f))continue;
  const float eye[]={x,y+1.7f,z},dx=vita_strelok_flash[0]-x,dy=vita_strelok_flash[1]-eye[1],dz=vita_strelok_flash[2]-z;
  const float length=std::sqrt(dx*dx+dy*dy+dz*dz),ray[]={dx/length,dy/length,dz/length};
  if(!ground.blocks_ray(eye,ray,length-.15f))++reachable;
 }
 assert(reachable);printf("Reachable flash pickup sight lines: %u\n",reachable);
 assert(vita_stash_progress(3,5,vita_strelok_room)==6);
 auto below=vita_strelok_room;below[1]-=8;assert(vita_stash_progress(3,5,below)==5);
 assert(vita_stash_progress(2,5,vita_strelok_room)==5);
 assert(vita_stash_progress(3,7,vita_strelok_room)==7);
 // The original item must actually parse; its textures are resolved from the user's archives at runtime.
 const auto model=get(assets/"meshes/equipments/item_datchik2.ogf");std::vector<VitaStalkerMesh>parts;
 assert(read_vita_weapon_world(model.data(),model.size(),parts)&&!parts.empty());
 for(const char*lang:{"fra","eng"}){
  const auto b=get(assets/(std::string("config/text/")+lang+"/stable_storyline_info_agroprom.xml"));
  std::string text;assert(read_dialog_line(std::string(b.begin(),b.end()),"storyline_gunslinger_flash_info",text,8192)&&text.size()>100);
  printf("Original Strelok information %s: %zu bytes\n",lang,text.size());
 }
 cave.story=6;cave.ground=vita_strelok_ladder_top[1];cave.eye=vita_strelok_ladder_top;cave.eye[1]+=1.7f;
 cave.needs_landing=false;
 assert(write_vita_slot(argv[3],1,cave));VitaSaveState restored;
 assert(read_vita_slot(argv[3],1,restored)&&restored.story==6&&!restored.inventory.items[23]);
 VitaInventory inventory;assert(inventory.restore(restored.inventory));
 const unsigned weight=inventory.weight_grams();assert(inventory.receive_strelok_flash());
 assert(!inventory.receive_strelok_flash()&&!inventory.remove(VitaInventoryItem::StrelokFlash,1));
 assert(!VitaInventory::tradable(VitaInventoryItem::StrelokFlash)&&inventory.weight_grams()==weight+100);
 restored.story=7;restored.inventory=inventory.state();restored.guards[0]=0;restored.world_details=true;
 assert(write_vita_slot(argv[3],2,restored));assert(read_vita_slot(argv[3],2,cave));
 assert(cave.story==7&&cave.inventory.items[23]==1&&cave.guards[0]==0);
 VitaSaveState return_surface,again;
 assert(change_vita_zone(cave,vita_zone_exits[5],return_surface));
 assert(return_surface.zone==2&&return_surface.story==7&&return_surface.inventory.items[23]==1);
 assert(change_vita_zone(return_surface,vita_zone_exits[4],again));
 assert(again.zone==3&&again.story==7&&again.inventory.items[23]==1&&again.guards[0]==0);
 const auto record=encode(again,42);assert(record.size()==23232);
 auto damaged=record;damaged[22000]^=1;uint64_t generation=0;
 assert(!decode_record(damaged,restored,generation));
 assert(decode_record(record,restored,generation)&&generation==42&&restored.inventory.items[23]==1);
 printf("v7 migration, v8 saves, quest stages, unique flash, per-map combat and roundtrip passed\n");
}
