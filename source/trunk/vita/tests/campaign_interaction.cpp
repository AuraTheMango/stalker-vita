// Compile against the actual input branches extracted from main.cpp.
#include <algorithm>
#include <cassert>
#include <string>
#include <vector>
constexpr unsigned SCE_CTRL_CIRCLE=1,SCE_CTRL_CROSS=2;
void* g_boot_log=nullptr;
void write_boot_log(void*,const char*){}
struct Movement{void cancel_input(){}}movement;
struct UI{bool ready=true,campaign_road=false;bool set_campaign_page(const char*,const std::string&){return ready;}}trader_ui;
struct Inventory{struct State{int equipment=1;};State state(){return {};}}inventory;
const char*campaign_name(){return "Sidorovitch";}
unsigned pressed=0,campaign_page=0,campaign_story=0,g_zone=0,trader_choice=0;
bool campaign_open=false,campaign_cleared_dialog=false,campaign_focused=true,campaign_is_road=false,road_focused=false;
struct Road{unsigned flags=0;}road_encounter;
bool any_dialog=false,menu_consumed_input=false,diagnostic_toggle=false,interact_pressed=true;
bool farm_cleared=true,dialog_consumed_input=false,trader_dialog_open=false,trader_confirmed=false;
std::vector<std::string> north_pages={"first","second","third","fourth","last"},attack_pages,campaign_pages,road_pages;
void tick(){
 if(false){}
 #include "campaign_input.inc"
}
int main(){
 // Active USB reward conversation must keep its input.
 any_dialog=true;tick();assert(!campaign_open&&!dialog_consumed_input);
 any_dialog=false;menu_consumed_input=true;tick();assert(!campaign_open);
 menu_consumed_input=false;tick();assert(campaign_open&&campaign_page==0);
 // Closing early does not unlock the next mission.
 pressed=SCE_CTRL_CIRCLE;tick();assert(!campaign_open&&campaign_story==0);
 pressed=0;tick();assert(campaign_open);
 for(unsigned i=1;i<5;++i){tick();assert(campaign_open&&campaign_page==i&&campaign_story==0);}
 tick();assert(!campaign_open&&campaign_story==1);
 // UI allocation failure leaves normal talk/trade available.
 campaign_story=0;trader_ui.ready=false;tick();assert(!campaign_open&&trader_dialog_open&&trader_confirmed);
 trader_ui.ready=true;road_focused=true;campaign_focused=false;campaign_story=1;road_pages={"thanks","info"};
 tick();assert(campaign_open&&campaign_is_road);
 tick();assert(campaign_open);tick();
 assert(!campaign_open&&campaign_story==1&&(road_encounter.flags&4));

}
