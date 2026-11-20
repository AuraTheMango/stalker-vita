
#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

struct VitaSettings
{
	std::array<unsigned,5> volume{100,70,100,100,60};
	unsigned resolution=0, aa=0, dof=0;
    unsigned language=0,hints=0,invert_y=0,sensitivity=100,hud_scale=100;
	unsigned msaa()const{return aa>=2?aa==2?2:4:0;}
	int width()const{return resolution==0?960:resolution==1?720:640;}
	int height()const{return resolution==0?544:resolution==1?408:368;}
	void change(unsigned row,int direction)
	{
		if(row<volume.size())volume[row]=unsigned(std::clamp(int(volume[row])+direction*5,0,100));
		else if(row==5)resolution=(resolution+3+direction)%3;
		else if(row==6){aa=(aa+4+direction)%4;if(msaa())dof=0;}
		else if(row==7){if(!msaa())dof^=1;else dof=0;}
        else if(row==8)language^=1;
        else if(row==9)hints^=1;
        else if(row==10)invert_y^=1;
        else if(row==12)hud_scale=unsigned(std::clamp(int(hud_scale)+direction*5,75,150));
        else if(row==11)sensitivity=unsigned(std::clamp(int(sensitivity)+direction*10,50,150));
    }
    bool valid()const
	{
		return hud_scale>=75&&hud_scale<=150&&hud_scale%5==0&&language<2&&hints<2&&invert_y<2&&sensitivity>=50&&sensitivity<=150&&sensitivity%10==0&&resolution<3&&aa<4&&dof<2&&(!msaa()||!dof)&&std::all_of(volume.begin(),volume.end(),[](unsigned v){return v<=100&&v%5==0;});
	}
};
inline uint32_t vita_settings_hash(const uint32_t* words,unsigned count=9)
{
 uint32_t hash=2166136261u;
 for(unsigned i=0;i<count;++i)for(unsigned shift=0;shift<32;shift+=8)hash=(hash^((words[i]>>shift)&255))*16777619u;
 return hash;
}
inline bool read_vita_settings(const char* directory,VitaSettings& out)
{
 FILE*f=std::fopen((std::string(directory)+"/settings.bin").c_str(),"rb");if(!f)return false;
 uint32_t words[15]{};const size_t bytes=std::fread(words,1,sizeof(words),f);
 const bool complete=std::fgetc(f)==EOF&&!std::ferror(f);std::fclose(f);
 const bool old=bytes==40&&words[0]==0x3154504f;
 const bool v2=bytes==56&&words[0]==0x3254504f;
 const bool v3=bytes==60&&words[0]==0x3354504f;
 if(!complete||(!old&&!v2&&!v3))return false;
 const unsigned payload=old?9:v2?13:14;
 if(vita_settings_hash(words,payload)!=words[payload])return false;
 VitaSettings settings;std::copy(words+1,words+6,settings.volume.begin());
 settings.resolution=words[6];settings.aa=words[7];settings.dof=words[8];
 if(!old){settings.language=words[9];settings.hints=words[10];settings.invert_y=words[11];settings.sensitivity=words[12];}
 if(v3)settings.hud_scale=words[13];
 if(!settings.valid())return false;out=settings;return true;
}
inline bool write_vita_settings(const char* directory,const VitaSettings& settings)
{
 if(!settings.valid())return false;
 mkdir(directory,0777);const std::string path=std::string(directory)+"/settings.bin";
 uint32_t words[15]={0x3354504f};std::copy(settings.volume.begin(),settings.volume.end(),words+1);
 words[6]=settings.resolution;words[7]=settings.aa;words[8]=settings.dof;
 words[9]=settings.language;words[10]=settings.hints;words[11]=settings.invert_y;words[12]=settings.sensitivity;
 words[13]=settings.hud_scale;words[14]=vita_settings_hash(words,14);
 FILE*f=std::fopen(path.c_str(),"wb");if(!f)return false;
 const bool wrote=std::fwrite(words,sizeof(words),1,f)==1,closed=std::fclose(f)==0;
 if(!wrote||!closed)return false;VitaSettings verified;
 return read_vita_settings(directory,verified)&&verified.volume==settings.volume&&verified.resolution==settings.resolution&&
 verified.aa==settings.aa&&verified.dof==settings.dof&&verified.language==settings.language&&verified.hints==settings.hints&&
 verified.invert_y==settings.invert_y&&verified.sensitivity==settings.sensitivity&&verified.hud_scale==settings.hud_scale;
}
