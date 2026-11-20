#pragma once
#include <string>
#include <cstddef>
inline bool vita_terrain_atlas(const std::string&name){return name.rfind("terrain\\",0)==0;}
inline bool vita_structure_texture(const std::string&name){
 for(const char*prefix:{"ston\\","wood\\","mtl\\","veh\\","vehicle\\","car\\"})
  if(name.rfind(prefix,0)==0)return true;
 return false;
}
inline unsigned vita_level_texture_dimension(const std::string&name,unsigned zone,size_t available,size_t quality_bytes){
 if(vita_terrain_atlas(name))return available>=512u*512u*4?512:128;
 if(zone&&vita_structure_texture(name)&&available>=256u*256u*4&&quality_bytes+256u*256u*4<=2u*1024u*1024u)return 256;
 return 128;
}
