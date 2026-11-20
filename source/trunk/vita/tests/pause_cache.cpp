#include <cassert>
#include <cstdio>
#include <cstdint>
using SceUInt64=uint64_t;
uint64_t sceKernelGetProcessTimeWide(){static uint64_t t=0;return ++t;}
void* g_boot_log=nullptr;void write_boot_log(void*,const char*){}
struct Texture{unsigned handle()const{return 1;}};
struct Menu{Texture background,atlas;unsigned initializes=0;bool initialize(){++initializes;return true;}}pause_menu;
struct Audio{unsigned initializes=0,stops=0;float volume=0;bool initialize(bool){++initializes;return true;}
 void set_radio(float v){volume=v;}void stop_speech(){++stops;}void set_ambience(bool){}}pause_music,game_audio;
int settings=0;void apply_audio_settings(Audio&,int){}
unsigned loads=0;
bool load_level_dds_texture(const char*,Texture&,unsigned budget,bool&,unsigned size){assert(size==512&&budget==512u*512u*4);++loads;return true;}
bool save_menu_open=false,pause_resources_active=false;
int main(){
#include "pause_prepare.inc"
 assert(prepare_pause_resources());assert(loads==2&&pause_menu.initializes==1&&pause_music.initializes==1&&pause_music.volume==0);
 for(unsigned i=0;i<8;++i){
  save_menu_open=i%2==0;
#include "pause_lifecycle.inc"
  assert(pause_resources_active==save_menu_open);
  assert(pause_music.volume==(save_menu_open?.7f:0.f));
 }
 assert(loads==2&&pause_menu.initializes==1&&pause_music.initializes==1);
 puts("Production pause menu: resources prepared once, reopen without disk/font work, music muted on close passed");
}
