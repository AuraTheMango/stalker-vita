
#include "vita_npc_battle.h"
#include "vita_settings.h"
#include "vita_save.cpp"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
struct Ground{
 bool blocked=false,walkable=true;float wall=100;
 bool blocks_ray(const float*,const float*,float)const{return blocked;}
 bool height_at(float,float,float y,float,float,float&h)const{h=y;return walkable;}
 bool slide_move(float x,float,float,float&tx,float&tz){if(tx>wall)tx=x;return true;}
};
int main(int argc,char**argv){
 assert(argc==2);std::filesystem::create_directories(argv[1]);Ground ground;
 VitaNpcBattle battle;
 for(auto&a:battle.guards)a.health=0;for(auto&a:battle.scouts)a.health=0;
 battle.guards[0].health=2;battle.guards[0].position=battle.guards[0].home={0,0,0};
 battle.scouts[0].health=100;battle.scouts[0].position=battle.scouts[0].home={0,0,8};
 unsigned allies=0,enemies=0,hits=0;
 for(uint64_t now=1000000;now<50000000;now+=33333){
  for(const auto&e:battle.update(ground,now,{0,0,70},true,100)){
   if(e.ally){++allies;if(e.damage&&battle.guards[e.target].health){--battle.guards[e.target].health;++hits;}}
   else{++enemies;assert(e.target==0);}
  }
  if(!battle.guards[0].health)break;
 }
 assert(allies>=2&&enemies>0&&hits==2);assert(battle.active);
 // LOS blocks shooting and the solo choice excludes allies.
 auto solo=battle;solo={};for(auto&a:solo.guards)a.health=0;
 solo.guards[0].health=2;solo.guards[0].home=solo.guards[0].position={0,0,0};
 ground.blocked=true;
 for(uint64_t now=1;now<20000000;now+=33333)assert(solo.update(ground,now,{0,0,8},false,100).empty());
 ground.blocked=false;
 unsigned shots=0;bool moved=false,reloaded=false;
 for(uint64_t now=20000001;now<60000000;now+=33333)
 {
  auto events=solo.update(ground,now,{0,0,8},false,100);
  moved=moved||solo.guards[0].walking;reloaded=reloaded||solo.guards[0].reload_until>now;
  for(auto&e:events){assert(!e.ally&&e.target==-1);++shots;}
 }
 assert(shots>=8&&moved&&reloaded);
 auto deadline=solo.guards[0].next_shot;solo.pause(3000000);assert(solo.guards[0].next_shot==deadline+3000000);
 // Settings bounds, round trip, checksum rejection without mutating the output.
 VitaSettings settings;
 for(int n=0;n<100;++n)settings.change(0,-1);assert(settings.volume[0]==0);
 for(int n=0;n<100;++n)settings.change(0,1);assert(settings.volume[0]==100);
 for(unsigned row=5;row<8;++row)for(int n=0;n<100;++n){settings.change(row,-1);assert(settings.valid());}
 assert(write_vita_settings(argv[1],settings));VitaSettings loaded;
 assert(read_vita_settings(argv[1],loaded));assert(loaded.aa==settings.aa&&loaded.resolution==settings.resolution);
 const auto path=std::string(argv[1])+"/settings.bin";
 {std::fstream f(path,std::ios::binary|std::ios::in|std::ios::out);f.put('!');}
 loaded.aa=3;assert(!read_vita_settings(argv[1],loaded)&&loaded.aa==3);
 // New inventory, ammo conservation, and no duplicate grants.
 VitaInventory inv;assert(inv.grant_starting_equipment());assert(inv.reserve()==40&&inv.loaded()==8);
 assert(!inv.grant_starting_equipment());inv.toggle_pistol(VitaInventoryItem::Pistol);inv.cycle_weapon();
 for(int n=0;n<8;++n)assert(inv.fire_pistol());assert(inv.reload_pistol()&&inv.reserve()==32);
 // New combat state plus legacy save migration.
 VitaSaveState state;state.eye={0,1.7f,0};state.guards.fill(2);
 state.scout_health={71,0,94};state.guard_positions[0]={12,3,8};
 state.inventory=inv.state();state.flags=SaveDoorUnlocked;
 auto bytes=encode(state,1);assert(bytes.size()==18600);
 const auto save=std::string(argv[1])+"/save_a.bin";
 auto put=[&](const auto& data){std::ofstream f(save,std::ios::binary);f.write((const char*)data.data(),data.size());};
 put(bytes);VitaSaveState restored;
 assert(read_vita_save(argv[1],restored));assert(restored.scout_health[1]==0&&restored.guard_positions[0][0]==12);
 auto legacy=bytes;legacy.resize(308);legacy[4]=1;legacy[16]=56;legacy[17]=1;
 const uint32_t hash=checksum(legacy);for(unsigned s=0;s<32;s+=8)legacy.push_back(hash>>s);
 put(legacy);assert(read_vita_save(argv[1],restored));assert(!restored.npc_positions&&restored.scout_health[0]==100);
 // Corrupt every new-format byte; none may be accepted.
 for(size_t i=0;i<bytes.size();++i){auto bad=bytes;bad[i]^=1;put(bad);assert(!read_vita_save(argv[1],restored));}
 put(bytes);
 printf("Battle: allies=%u enemy=%u hits=%u solo_shots=%u; settings and v1/v4 saves passed\n",allies,enemies,hits,shots);
}
