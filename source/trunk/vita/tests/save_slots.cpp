#include "vita_save.cpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
int main(int argc,char**argv){
 assert(argc==2);std::filesystem::create_directories(argv[1]);const char*dir=argv[1];
 VitaSaveState state;state.eye={0,1.7f,0};state.guards.fill(2);
 state.world_details=true;state.play_seconds=200;state.survival_seconds=3.5f;
 state.route_metres={12.5f,18.f,55.f};state.route_rejoin=5;state.voice_flags=193;
 state.voice_delay_ms={1200,1700};state.agents[0].home={12,3,9};
 state.agents[0].yaw=2.4f;state.agents[0].shots=7;state.agents[0].target=2;
 state.agents[0].flags=3;state.agents[0].next_shot_ms=1500;state.agents[0].reload_ms=1700;state.agents[0].shot_age_ms=50;
 // All slots empty; the original pair is manual slot 1 without migration or deletion.
 assert(latest_vita_slot(dir)==-1&&next_vita_auto_slot(dir)==6);
 auto old=encode(state,3);old.resize(4220);old[4]=4;
 for(unsigned i=0;i<4;++i)old[16+i]=4224u>>(i*8);
 const auto hash=checksum(old);for(unsigned i=0;i<4;++i)old.push_back(hash>>(i*8));
 {std::ofstream f(std::string(dir)+"/save_a.bin",std::ios::binary);f.write((char*)old.data(),old.size());}
 VitaSaveState restored;assert(read_vita_slot(dir,0,restored)&&!restored.world_details);
 for(unsigned slot=1;slot<vita_save_slots;++slot){
  state.play_seconds=200+slot;state.health=100-slot;
  assert(write_vita_slot(dir,slot,state));
  assert(read_vita_slot(dir,slot,restored)&&restored.health==100-slot);
  assert(restored.world_details&&restored.route_metres==state.route_metres&&restored.route_rejoin==5);
  assert(restored.survival_seconds==3.5f&&restored.voice_flags==193&&restored.voice_delay_ms==state.voice_delay_ms);
  const auto&a=restored.agents[0];assert(a.home==state.agents[0].home&&a.yaw==2.4f&&a.shots==7&&a.target==2);
  assert((a.flags&3)==3&&a.next_shot_ms==1500&&a.reload_ms==1700&&a.shot_age_ms==50);
 }
 auto entries=list_vita_saves(dir);for(auto&e:entries)assert(e.valid&&e.exists);
 assert(latest_vita_slot(dir)==8&&next_vita_auto_slot(dir)==6);
 assert(!read_vita_slot(dir,9,restored)&&!write_vita_slot(dir,9,state)&&!delete_vita_slot(dir,9));
 // Rotate the automatic slots: the manual world is untouched.
 state.play_seconds=300;assert(write_vita_slot(dir,next_vita_auto_slot(dir),state));
 assert(next_vita_auto_slot(dir)==7&&read_vita_slot(dir,0,restored)&&!restored.world_details);
 // Newest damaged generation falls back inside the selected slot, not to another world.
 state.health=35;assert(write_vita_slot(dir,1,state));
 {std::ofstream f(std::string(dir)+"/save_slot02/save_b.bin",std::ios::binary);f<<"torn";}
 assert(read_vita_slot(dir,1,restored)&&restored.health==99);
 assert(delete_vita_slot(dir,1)&&!read_vita_slot(dir,1,restored)&&read_vita_slot(dir,2,restored));
 auto broken=state;broken.agents[0].target=99;assert(!write_vita_slot(dir,1,broken));
 broken=state;broken.survival_seconds=4;assert(!write_vita_slot(dir,1,broken));
 std::cout<<"9 slots, v4 legacy, world state, autosave isolation/rotation, torn write fallback, deletion and validation passed\n";
}
