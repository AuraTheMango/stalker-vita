#include "vita_trader_model.h"
#include "vita_inventory.h"
#include "vita_grenades.h"
#include <fstream>
#include <iterator>
#include <cassert>
#include <cstdio>
std::vector<uint8_t> read(const std::string&p){std::ifstream f(p,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char**argv){
 assert(argc==2);std::string root=argv[1];
 const char*paths[]={"bm_16/wpn_bm-16_hud.ogf","ak74/wpn_ak74_hud.ogf","grenade/wpn_rgd_5_hud.ogf","grenade/wpn_f_1_hud.ogf","pm/wpn_pm_hud.ogf"};
 for(unsigned i=0;i<5;++i){auto data=read(root+"/"+paths[i]);std::vector<VitaWeaponHudMesh> parts;VitaWeaponClips clips{};
  assert(read_vita_weapon_hud(data.data(),data.size(),i<2||i==4,parts,clips,i==0?2:i==1?3:i==4?0:4,i<2||i==4));
  size_t bytes=0,addon=0;for(auto&p:parts){assert(p.positions.size()>=p.vertices.size()*3);bytes+=p.positions.size()*4;
   for(auto type:p.triangle_addons)addon+=type!=0;for(float value:p.positions)assert(std::isfinite(value));}
  assert(clips[0].source_frames);assert(parts.size()<=8);assert(bytes<=48u*1024*1024);
  if(i<2)assert(clips[size_t(VitaWeaponClip::Reload)].source_frames&&clips[size_t(VitaWeaponClip::Shoot)].source_frames);
  if(i==1||i==4)assert(addon);
  printf("%s parts=%zu bytes=%zu addons=%zu idle=%u reload=%u fire=%u\n",paths[i],parts.size(),bytes,addon,clips[0].source_frames,
    clips[size_t(VitaWeaponClip::Reload)].source_frames,clips[size_t(VitaWeaponClip::Shoot)].source_frames);
 }
 for(const char*name:{"grenade/wpn_rgd_5.ogf","grenade/wpn_f_1.ogf"}){
  auto data=read(root+"/"+name);std::vector<VitaStalkerMesh> parts;
  assert(read_vita_weapon_world(data.data(),data.size(),parts)&&!parts.empty()&&parts.size()<=2);
  for(const auto&p:parts)assert(!p.indices.empty());
 }
 struct Ground{
  bool blocked=false;
  bool blocks_ray(const float*,const float*,float)const{return blocked;}
  bool height_at(float,float,float,float,float,float&y)const{y=0;return true;}
 }ground;
 VitaGrenades grenades;assert(grenades.launch({0,1.6f,0},{0,0,1},0));
 unsigned blasts=0;for(unsigned i=0;i<360;++i){for(const auto&e:grenades.update(ground,1.f/120)){
  ++blasts;assert(e.position[1]>=.08f);
  assert(VitaGrenades::damage(ground,e,e.position)==120);
  ground.blocked=true;auto target=e.position;target[2]+=1;assert(!VitaGrenades::damage(ground,e,target));ground.blocked=false;
 }}assert(blasts==1);
 VitaInventory inv;inv.grant_starting_equipment();inv.add(VitaInventoryItem::Shotgun,1);inv.toggle_pistol(VitaInventoryItem::Shotgun);
 inv.add(VitaInventoryItem::Buckshot,10);inv.cycle_weapon();assert(inv.weapon()==VitaWeapon::Knife);inv.cycle_weapon();assert(inv.weapon()==VitaWeapon::Shotgun);
 assert(inv.reload_pistol()&&inv.loaded()==2&&inv.reserve()==8);assert(inv.fire_pistol());
 inv.add(VitaInventoryItem::Slug,10);assert(inv.select_ammo(VitaInventoryItem::Slug)&&inv.count(VitaInventoryItem::Buckshot)==9);
 assert(inv.reload_pistol()&&inv.reserve()==8);assert(inv.remove(VitaInventoryItem::Shotgun,1)&&inv.count(VitaInventoryItem::Slug)==10&&inv.weapon()==VitaWeapon::Holstered);
 puts("Player weapon clips, addons, ammo conservation, grenade fuse/bounce/occlusion passed");
}
