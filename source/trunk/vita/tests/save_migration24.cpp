#include "vita_save.cpp"
#include <cassert>
#include <fstream>
#include <iterator>
#include <filesystem>
#include <iostream>
int main(int argc,char**argv){
 assert(argc==3);std::ifstream f(argv[1],std::ios::binary);assert(f);
 std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(f),{}};
 assert(bytes.size()==18532&&bytes[4]==6);
 VitaSaveState state;uint64_t generation=0;assert(decode_record(bytes,state,generation));
 assert(state.garbage_road.flags==0&&state.garbage_road.health[1]==2);
 std::filesystem::create_directories(argv[2]);assert(write_vita_slot(argv[2],0,state));
 VitaSaveState read;assert(read_vita_slot(argv[2],0,read));
 assert(read.inventory.items==state.inventory.items&&read.worlds==state.worlds&&read.story==state.story&&read.zone==state.zone);
 std::cout<<"Real format 6 campaign file migrated; inventory, map memories and story preserved\n";
}
