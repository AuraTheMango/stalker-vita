#include "vita_settings.h"
#include <filesystem>
#include <fstream>
#include <cassert>
#include <cstdio>
#include <vector>
int main(int argc,char**argv){
 assert(argc==2);std::filesystem::create_directories(argv[1]);VitaSettings s,out;
 uint32_t old[10]={0x3154504f,100,70,100,100,60,0,0,0,0};old[9]=vita_settings_hash(old);
 const std::string path=std::string(argv[1])+"/settings.bin";
 {std::ofstream f(path,std::ios::binary);f.write((char*)old,40);}
 assert(read_vita_settings(argv[1],out)&&!out.language&&!out.hints&&out.sensitivity==100);
 s.language=1;s.hints=1;s.invert_y=1;s.sensitivity=130;s.resolution=2;s.aa=1;s.dof=1;
 assert(write_vita_settings(argv[1],s)&&read_vita_settings(argv[1],out));
 assert(out.language==1&&out.hints==1&&out.invert_y==1&&out.sensitivity==130&&out.aa==1&&out.dof==1);
 std::ifstream f(path,std::ios::binary);std::vector<char>b((std::istreambuf_iterator<char>(f)),{});
 assert(b.size()==56);
 for(size_t i=0;i<b.size();++i){auto bad=b;bad[i]^=1;{std::ofstream o(path,std::ios::binary);o.write(bad.data(),bad.size());}
 out.language=0;assert(!read_vita_settings(argv[1],out)&&out.language==0);}
 for(unsigned row=0;row<12;++row)for(unsigned step=0;step<100;++step){s.change(row,step&1?1:-1);assert(s.valid());}
 puts("Options: v1 migration, v2 roundtrip, all fields/bounds and corruption passed");
}
