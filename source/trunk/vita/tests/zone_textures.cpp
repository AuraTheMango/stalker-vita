#include "vita_gl_texture.h"
#include <fstream>
#include <iterator>
void glBindTexture(GLenum,GLuint){}void glDeleteTextures(GLsizei,const GLuint*){}void glGenTextures(GLsizei,GLuint*){}
GLenum glGetError(){return 0;}void glTexImage2D(GLenum,GLint,GLint,GLsizei,GLsizei,GLint,GLenum,GLenum,const void*){}void glTexParameteri(GLenum,GLenum,GLint){}

#include "vita_texture_policy.h"
#include <cassert>
#include <iostream>
std::vector<uint8_t> read(const std::string&p){std::ifstream f(p,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char**argv){
 assert(argc==2);const std::string root=argv[1];
 for(const char*zone:{"escape","garbage","agroprom"}){
  auto bytes=read(root+"/textures/map/map_"+zone+".dds");VitaDecodedDDS old,map;
  size_t required=0;const bool previous=vita_decode_dds(bytes.data(),bytes.size(),2u*1024u*1024u,1024,old,nullptr,&required);
  if(std::string(zone)=="garbage")assert(!previous&&required==4u*1024u*1024u);
  assert(vita_decode_dds(bytes.data(),bytes.size(),1024u*1024u,512,map));
  assert(map.width<=512&&map.height<=512&&map.rgba.size()<=1024u*1024u);
  std::cout<<zone<<" old_loaded="<<previous<<" previous_bytes="<<required<<" new="<<map.width<<"x"<<map.height<<"\n";
 }
 assert(vita_level_texture_dimension("terrain\\terrain_garbage_new",1,1048576,0)==512);
 assert(vita_level_texture_dimension("terrain\\terrain_escape",0,1048576,0)==512);
 assert(vita_level_texture_dimension("ston\\wall",1,1048576,0)==256);
 assert(vita_level_texture_dimension("veh\\car",1,1048576,0)==256);
 assert(vita_level_texture_dimension("ston\\wall",1,65536,0)==128);
 assert(vita_level_texture_dimension("veh\\car",1,1048576,2097152)==128);
 assert(vita_level_texture_dimension("ston\\wall",0,1048576,0)==128);
 assert(vita_level_texture_dimension("trees\\leaf",1,1048576,0)==128);
}
