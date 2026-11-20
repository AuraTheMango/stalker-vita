#pragma once
#include <array>
#include <cstddef>
#include <string>
#include <cstdint>
#include <utility>

// Texture ownership remains with this bounded cache; active UI fields only borrow handles.
template<size_t Capacity> class VitaUiPageCache {
public:
 struct Page {std::string name,text;std::array<unsigned,3> textures{};uint64_t used=0;};
 const Page* find(const std::string&name,const std::string&text){
  for(auto&p:pages)if(p.used&&p.name==name&&p.text==text){p.used=++clock;return &p;}
  return nullptr;
 }
 template<class Release> const Page& insert(Page page,Release release){
  size_t slot=0;
  for(size_t i=0;i<Capacity;++i)if(pages[i].used<pages[slot].used)slot=i;
  if(pages[slot].used)release(pages[slot].textures);
  page.used=++clock;pages[slot]=std::move(page);return pages[slot];
 }
 template<class Release> void clear(Release release){
  for(auto&p:pages){if(p.used)release(p.textures);p={};}clock=0;
 }
private:
 static_assert(Capacity>0);
 std::array<Page,Capacity> pages{};
 uint64_t clock=0;
};
