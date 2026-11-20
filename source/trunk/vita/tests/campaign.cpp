
#include "vita_save.cpp"
#include "vita_cform_ground.h"
#include "vita_assault_advance.h"
#include <cassert>
#include <filesystem>
#include <cstdio>
#include <iostream>
int main(int argc,char**argv){
 assert(argc==3);std::filesystem::create_directories(argv[2]);
 for(unsigned zone=0;zone<3;++zone){
  const auto&config=vita_campaign_zones[zone];
  const auto file=std::filesystem::path(argv[1])/"levels"/config.directory/"level.cform";
  FILE*f=fopen(file.c_str(),"rb");assert(f);VitaCformGround ground;
  const auto center=zone?config.allies[0]:std::array<float,3>{-246,-24,-133};
  const auto reader=[](void*p,void*out,size_t n){return fread(out,1,n,(FILE*)p)==n;};
  assert(ground.load(reader,f,std::filesystem::file_size(file),center[0],center[2],{},nullptr,true));fclose(f);
  printf("Full map %s ground=%zu walls=%zu\n",config.directory,ground.face_count(),ground.wall_count());
  for(const auto&e:vita_zone_exits){
   float floor=e.arrival[1];
   if(e.to==zone){assert(ground.height_at(e.arrival[0],e.arrival[2],floor,3,30,floor));
    const float feet[]={e.arrival[0],floor,e.arrival[2]};
    printf(" arrival %.2f %.2f %.2f\n",feet[0],feet[1],feet[2]);}
  }
  for(const auto&e:vita_zone_exits)if(e.from==zone){
   float floor=e.center[1];assert(ground.height_at(e.center[0],e.center[2],floor,3,30,floor));
   assert(vita_exit_near(e,zone,{e.center[0],floor,e.center[2]}));
   printf(" exit %.2f %.2f %.2f valid trigger\n",e.center[0],floor,e.center[2]);
  }
  if(zone){
   VitaPatrolPoint targets[6];std::array<bool,6>present;present.fill(true);
   for(unsigned i=0;i<6;++i)targets[i]=config.enemies[i];
   unsigned routed=0;
   for(const auto&ally:config.allies){
    float floor=ally[1];assert(ground.height_at(ally[0],ally[2],floor,.5,.85,floor));
    std::vector<VitaPatrolPoint> path;VitaAssaultAdvanceStats stats;
    const bool ready=find_vita_assault_advance(ground,ally,targets,present,13.5f,path,stats);
    routed+=ready;
    printf(" ally advance ready=%u samples=%zu visits=%u\n",unsigned(ready),path.size(),stats.visited);
   }
   assert(routed>0);
  }
  if(zone)for(const auto&position:config.enemies){
   float floor=position[1];assert(ground.height_at(position[0],position[2],floor,.25,.25,floor));
   assert(std::abs(floor-position[1])<.1f);
  }
 }
 VitaLootWorld bodies;bodies.body(0,{0,0,0},2);bodies.body(7,{0,0,0},1);
 for(unsigned id:{0u,7u}){const auto&items=bodies.containers[id].items;
  assert(items[13]==1&&items[16]==30&&!items[0]&&!items[1]&&!items[12]&&!items[14]&&!items[11]);}
 bodies.containers[0].items={};bodies.body(0,{0,0,0},2);assert(!bodies.containers[0].items[13]);
 VitaSaveState cordon;cordon.eye={10,1.7f,20};cordon.flags=SaveWolfMission|SaveScoutMet|SaveFarmCleared;
 VitaInventory inventory;assert(inventory.grant_starting_equipment());inventory.receive_flash();inventory.deliver_flash();
 cordon.inventory=inventory.state();cordon.trader.cash=12345;cordon.story=1;cordon.guards.fill(0);
 cordon.loot.containers[0].kind=1;cordon.loot.containers[0].cash=0;cordon.loot.containers[0].items={};
 VitaSaveState garbage;assert(change_vita_zone(cordon,vita_zone_exits[0],garbage));
 assert(garbage.zone==1&&garbage.story==1&&garbage.inventory.cash==1500&&garbage.worlds[0].size()==4628);
 assert(!vita_exit_allowed(vita_zone_exits[2],1));
 garbage.guards.fill(0);garbage.flags|=SaveFarmCleared;garbage.story=3;
 assert(garbage.inventory.items==cordon.inventory.items&&garbage.trader.cash==12345);
 assert(write_vita_slot(argv[2],1,garbage));VitaSaveState loaded;
 assert(read_vita_slot(argv[2],1,loaded)&&loaded.zone==1&&loaded.worlds[0]==garbage.worlds[0]);
 VitaSaveState agro;assert(change_vita_zone(loaded,vita_zone_exits[2],agro)&&agro.zone==2&&agro.guards[0]==2);
 VitaSaveState back;assert(change_vita_zone(agro,vita_zone_exits[3],back)&&back.zone==1&&back.guards[0]==0);
 VitaSaveState home;assert(change_vita_zone(back,vita_zone_exits[1],home)&&home.zone==0&&home.guards[0]==0);
 assert(home.loot.containers[0].kind==1&&home.loot.containers[0].cash==0&&home.trader.cash==12345);
 auto record=encode(home,1);assert(record.size()==23232);
 auto old=encode_base(cordon,1);VitaSaveState legacy;uint64_t gen=0;
 assert(decode_record(old,legacy,gen)&&legacy.zone==0&&legacy.story==0&&legacy.worlds[0].empty());
 // Nested worlds cannot be corrupt or contain recursive v6 records.
 auto bad=home;bad.worlds[0][450]^=1;assert(!write_vita_slot(argv[2],2,bad));
 bad=home;bad.worlds[0]=record;assert(!write_vita_slot(argv[2],2,bad));
 puts("Campaign roundtrip, per-map enemies/loot, shared inventory/trader, v5 migration and corruption passed");
}
