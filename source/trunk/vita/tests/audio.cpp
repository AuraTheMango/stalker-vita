
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>
#define private public
#include "vita_game_audio.h"
#undef private
#include "vita_game_audio.cpp"
#include <cassert>
#include <cstdio>
#include <cstring>
VitaGameAudio* active=nullptr;
bool stereo=false;
std::vector<int16_t> stream;
size_t cursor=0;
std::vector<std::array<int16_t,1024>> output;
int sceAudioOutOpenPort(int,unsigned size,int rate,int){assert(size==512 && rate==48000);return 1;}
int sceAudioOutReleasePort(int){return 0;}
int sceAudioOutOutput(int,const void* data){
 if(!data)return 0;
 std::array<int16_t,1024> block;
 std::memcpy(block.data(),data,sizeof(block));output.push_back(block);
 if(output.size()==40)active->m_stopping.store(true);
 return 0;
}
int sceIoGetstat(const char* path,SceIoStat* stat){
 if(std::strstr(path,stereo?"main_menu.pcm":"bunker_radio.pcm")){stat->st_size=stream.size()*2;return 0;}
 return -1;
}
int sceIoOpen(const char*,int,int){cursor=0;return 5;}
int sceIoClose(int){return 0;}
int sceIoRead(int,void* data,size_t requested){
 size_t count=std::min(stream.size()-cursor,requested/2);
 std::memcpy(data,stream.data()+cursor,count*2);cursor+=count;return count*2;
}
long long sceIoLseek(int,long long pos,int){assert(pos==0);cursor=0;return 0;}
int sceKernelCreateSema(const char*,int,int,int,void*){return 1;}
int sceKernelSignalSema(int,int){return 0;}
int sceKernelWaitSema(int,int,void*){assert(false);return -1;}
int sceKernelDeleteSema(int){return 0;}
int sceKernelCreateThread(const char*,int(*)(SceSize,void*),int,int,int,int,void*){return 1;}
int sceKernelStartThread(int,unsigned,void*){return 0;}
int sceKernelDeleteThread(int){return 0;}
int sceKernelWaitThreadEnd(int,void*,void*){return 0;}
void run_stream(bool menu){
 stereo=menu;output.clear();cursor=0;
 // Deliberately tiny stream exercises end-of-file looping hundreds of times.
 stream=menu?std::vector<int16_t>{1000,-2000,3000,-4000}:std::vector<int16_t>{1000,3000};
 VitaGameAudio audio;active=&audio;audio.set_levels(100,100,100,100,100);
 if(menu)assert(audio.initialize(true));
 else { audio.m_radio=5;audio.m_radio_stereo=false;audio.m_port=1; }
 audio.m_radio_gains.store(256u|(256u<<16));
 audio.m_running.store(true);
 assert(audio.run()==0);
 assert(output.size()==40);
 const auto& block=output.back();
 for(size_t frame=0;frame<512;++frame){
  const int16_t expected_left=(frame/2)%2?3000:1000;
  const int16_t expected_right=menu?((frame/2)%2?-4000:-2000):expected_left;
  assert(block[frame*2]==expected_left);
  assert(block[frame*2+1]==expected_right);
 }
 audio.release();
}

void mix_level(VitaSound sound,unsigned effects,unsigned voices,unsigned ambient,unsigned master,int expected){
 output.clear();VitaGameAudio audio;active=&audio;audio.m_port=1;audio.m_wake=1;audio.m_running.store(true);
 audio.set_levels(master,100,effects,voices,ambient);
 audio.m_samples[static_cast<unsigned>(sound)].assign(48000,1000);
 if(sound==VitaSound::AmbientWind)audio.set_ambience(true);else audio.play(sound,1.f,0.f);
 audio.run();assert(output.size()==40);
 for(auto value:output.back())assert(value==expected);
 audio.release();
}
int main(){
 run_stream(true);run_stream(false);
 mix_level(VitaSound::Pistol,25,100,100,100,250);
 mix_level(VitaSound::RifleShot,25,100,100,100,250);
 mix_level(VitaSound::GrenadeExplosion,25,100,100,100,250);
 mix_level(VitaSound::BanditVariants,100,25,100,100,250);
 mix_level(VitaSound::AmbientWind,100,100,100,100,234);
 mix_level(VitaSound::Pistol,100,100,100,0,0);
 VitaGameAudio voice;
 for(unsigned actor=0;actor<6;++actor)for(unsigned event=0;event<4;++event){
  unsigned last=999,seen=0;
  for(unsigned i=0;i<120;++i){unsigned v=static_cast<unsigned>(voice.bandit_voice(actor,event));
   assert(v!=last);last=v;v-=static_cast<unsigned>(VitaSound::BanditVariants);
   assert(v/3==(actor%3)*4+event);seen|=1u<<(v%3);
  }
  assert(seen==7);
 }
 puts("Mixer: stereo loop, mono radio, separate volumes, master mute, ambient loop and nonrepeating per-speaker variants passed");
}
