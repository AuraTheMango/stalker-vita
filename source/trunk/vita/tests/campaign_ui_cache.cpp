#include "vita_ui_page_cache.h"
#include "vita_campaign_dialogue.h"
#include "vita_campaign.h"
#include <cassert>
#include <cstdio>
#include <array>
#include <vector>
#include <string>
#include <set>
using GLuint=unsigned;using SceFontLibHandle=unsigned;
unsigned fonts=0,next_texture=1,uploads=0,closes=0;bool fail_font=false,fail_upload=false;
std::set<unsigned>deleted;
unsigned open_ui_font(unsigned&lib){++fonts;lib=1;return fail_font?0:1;}
void sceFontClose(unsigned){++closes;}void sceFontDoneLib(unsigned){}
void glDeleteTextures(size_t count,const unsigned*ids){for(size_t i=0;i<count;++i)if(ids[i])assert(deleted.insert(ids[i]).second);}
unsigned texture(){++uploads;return fail_upload?0:next_texture++;}
struct TextImage{TextImage(int,int){};template<class...A>void line(A&&...){}unsigned upload(bool){return texture();}};
unsigned upload_centered_label(unsigned,const char*,unsigned){return texture();}
const char* vita_text(const char*f,const char*){return f;}
constexpr unsigned prompt_width=398,prompt_height=40;
class VitaTraderUI{public:
 bool set_campaign_page(const std::string&,const std::string&);bool set_campaign_objective(const char*);
 VitaUiPageCache<4> m_campaign_page_cache,m_campaign_objective_cache;
 std::array<unsigned,vita_zone_exits.size()>m_zone_prompts{};std::array<unsigned,3>m_stash_prompts{};
 unsigned m_campaign_dialog=0,m_campaign_name=0,m_campaign_prompt=0,m_zone_locked=0,m_campaign_objective=0;
};
#include "campaign_cache.inc"
int main(){
 VitaTraderUI ui;
 const auto pages=paginate_vita_campaign_text(std::string(599,'a')+" "+std::string(599,'b')+" "+std::string(100,'c'));
 assert(pages.size()==3);for(const auto&p:pages)assert(ui.set_campaign_page("PDA",p));
 for(const char*o:{"stash","information","ghost"})assert(ui.set_campaign_objective(o));
 const auto warmed_fonts=fonts,warmed_uploads=uploads;
 fail_font=true;for(unsigned n=0;n<5;++n)for(const auto&p:pages)assert(ui.set_campaign_page("PDA",p));
 assert(ui.set_campaign_objective("ghost"));assert(fonts==warmed_fonts&&uploads==warmed_uploads);
 const auto active=ui.m_campaign_dialog;assert(!ui.set_campaign_page("Mole","uncached"));assert(ui.m_campaign_dialog==active);
 fail_font=false;fail_upload=true;assert(!ui.set_campaign_page("Mole","allocation failure"));assert(ui.m_campaign_dialog==active);
 fail_upload=false;
 assert(ui.set_campaign_page("Mole","page4"));
 assert(ui.set_campaign_page("Mole","page5")); // LRU drops page0, with exactly three textures.
 const auto now=uploads;assert(ui.set_campaign_page("PDA",pages[1]));assert(uploads==now);
 assert(ui.set_campaign_page("PDA",pages[0]));assert(uploads==now+3);
 auto free=[](const std::array<unsigned,3>&ids){glDeleteTextures(ids.size(),ids.data());};
 ui.m_campaign_page_cache.clear(free);ui.m_campaign_objective_cache.clear(free);
 glDeleteTextures(ui.m_zone_prompts.size(),ui.m_zone_prompts.data());glDeleteTextures(ui.m_stash_prompts.size(),ui.m_stash_prompts.data());
 glDeleteTextures(1,&ui.m_zone_locked);assert(deleted.size()==next_texture-1);
 puts("Production campaign cache: prewarm, no font/upload during reading, LRU, allocation failure and exact cleanup passed");
}
