#include "vita_settings.h"
#include "vita_hud_layout.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <vector>
int main(int argc,char**argv){
 assert(argc==2);std::filesystem::create_directories(argv[1]);
 VitaSettings settings;
 for(int i=0;i<100;++i)settings.change(12,-1);assert(settings.hud_scale==75&&settings.valid());
 for(int i=0;i<100;++i)settings.change(12,1);assert(settings.hud_scale==150&&settings.valid());
 for(unsigned size=75;size<=150;size+=5){
  settings.hud_scale=size;assert(write_vita_settings(argv[1],settings));
  VitaSettings restored;assert(read_vita_settings(argv[1],restored)&&restored.hud_scale==size);
 }
 // Migration from both previous settings formats preserves all existing values.
 for(bool v2:{false,true}){
  std::vector<uint32_t> words=v2?std::vector<uint32_t>{0x3254504f,80,65,50,40,30,1,1,1,1,1,1,130}:
   std::vector<uint32_t>{0x3154504f,80,65,50,40,30,1,1,1};
  words.push_back(vita_settings_hash(words.data(),words.size()));
  {std::ofstream f(std::string(argv[1])+"/settings.bin",std::ios::binary);f.write((char*)words.data(),words.size()*4);}
  VitaSettings restored;restored.hud_scale=150;
  assert(read_vita_settings(argv[1],restored)&&restored.hud_scale==100&&restored.volume[1]==65&&restored.resolution==1);
  if(v2)assert(restored.language==1&&restored.hints==1&&restored.invert_y==1&&restored.sensitivity==130);
 }
 // Checksum and validation reject corrupt settings without changing the live object.
 settings.hud_scale=150;assert(write_vita_settings(argv[1],settings));
 {std::fstream f(std::string(argv[1])+"/settings.bin",std::ios::binary|std::ios::in|std::ios::out);f.seekp(52);f.put('\0');}
 VitaSettings live;live.hud_scale=95;assert(!read_vita_settings(argv[1],live)&&live.hud_scale==95);
 settings.hud_scale=74;assert(!write_vita_settings(argv[1],settings));
 settings.hud_scale=151;assert(!settings.valid());
 // Scale corners without moving their anchor or stretching rectangles.
 for(float scale:{.75f,1.f,1.5f}){
  auto a=vita_hud_rect(840,489.63f,112,46.37f,scale,952,536);
  assert(std::abs(a[0]+a[2]-952)<.01f&&std::abs(a[1]+a[3]-536)<.01f);
  assert(a[0]>0&&a[1]>0);
  auto b=vita_hud_rect(12,34,132,137,scale,12,34);
  assert(b[0]==12&&b[1]==34);
 }
}
