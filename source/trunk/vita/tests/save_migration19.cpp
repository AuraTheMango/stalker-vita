
#include "vita_save.cpp"
#include <cassert>
#include <iostream>
int main(int argc,char**argv){
 assert(argc==2);VitaSaveState state;
 assert(read_vita_slot(argv[1],2,state));
 assert(state.zone==0&&state.story==0&&state.worlds[0].empty());
 assert(state.world_details&&state.agents[0].next_shot_ms==1500&&state.agents[0].yaw==2.4f);
 std::cout<<"Real format-5 file from 0.0.19 read with complete NPC state\n";
}
