
#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <fstream>
#include <iterator>
#include <string>
#include <cstdio>
#include <cassert>
#include "vita_map_parse.inc"
int main(int argc,char**argv){
 assert(argc==2);std::ifstream f(argv[1]);assert(f);const std::string config{std::istreambuf_iterator<char>(f),{}};
 for(const char*level:{"l01_escape","l02_garbage","l03_agroprom"}){
  std::string texture;float bounds[4]={};assert(read_vita_hud_map(config,texture,bounds,level));
  printf("%s -> %s [%.2f %.2f %.2f %.2f]\n",level,texture.c_str(),bounds[0],bounds[1],bounds[2],bounds[3]);
 }
}
