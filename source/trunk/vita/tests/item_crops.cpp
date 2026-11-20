#include "vita_gl_texture.h"
#include "vita_item_catalog.h"
#include <fstream>
#include <iterator>
#include <cstring>
#include <cassert>
#include <cstdio>
std::vector<uint8_t> uploaded;unsigned next_id=1;
void glBindTexture(GLenum,GLuint){}void glDeleteTextures(GLsizei,const GLuint*){}
void glGenTextures(GLsizei n,GLuint*out){while(n--)*out++=next_id++;}
GLenum glGetError(){return 0;}void glTexParameteri(GLenum,GLenum,GLint){}
void glTexImage2D(GLenum,GLint,GLint,GLsizei w,GLsizei h,GLint,GLenum,GLenum,const void*data){
 const auto*p=(const uint8_t*)data;uploaded.assign(p,p+w*h*4);}
int main(int argc,char**argv){
 assert(argc==2);std::ifstream file(argv[1],std::ios::binary);std::vector<uint8_t>data(std::istreambuf_iterator<char>(file),{});
 VitaDecodedDDS full;assert(vita_decode_dds(data.data(),data.size(),64*1024*1024,4096,full));
 for(unsigned i=0;i<vita_item_count;++i){
  const auto&a=vita_item_art[i];const unsigned x=a.x*50,y=a.y*50,left=x&~3u,top=y&~3u;
  const VitaDDSRegion region{1024,left,top,((x+a.columns*50+3)&~3u)-left,((y+a.rows*50+3)&~3u)-top,2048};
  VitaGLTexture texture;assert(texture.initialize_dds_region(data.data(),data.size(),region.width*region.height*4,region));
  for(unsigned row=0;row<region.height;++row)assert(!memcmp(uploaded.data()+row*region.width*4,
    full.rgba.data()+((top+row)*full.width+left)*4,region.width*4));
 }
 puts("All 23 production equipment atlas regions match original DDS pixels");
}
