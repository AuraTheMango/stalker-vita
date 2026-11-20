#include "vita_zone_confirmation.h"
#include <cassert>
#include <algorithm>
#include <array>
#include <cstdio>
#include <vector>
constexpr unsigned SCE_CTRL_CROSS=1,SCE_CTRL_CIRCLE=2,SCE_CTRL_SQUARE=4,SCE_CTRL_START=8;
struct VitaSaveState{unsigned zone=0;};
enum class VitaSaveMessage{LoadFailed};
VitaZoneConfirmation zone_confirmation;
struct Movement{void cancel_input(){}}movement;
struct Pad{unsigned buttons=0;}pad;
void* g_boot_log=nullptr;void write_boot_log(void*,const char*){}
unsigned g_zone=2,campaign_story=5,pressed=0,zone_exit_choice=0;
uint64_t now=1000,transition_retry_after=0,pause_started=0;
bool zone_dialog_active=false,zone_transition_confirmed=false,dialog_consumed_input=false,any_dialog=false;
bool interact_pressed=false,grounded=true,zone_confirmation_ui_ready=true,grenade_pending=false,save_menu_open=false;
unsigned reload_deadline=0;
VitaSaveMessage save_message{};
struct Jump{bool active()const{return false;}}jump;
struct Grenade{bool active=false;};
using VitaGrenadeBody=Grenade;
struct Grenades{std::array<Grenade,4>bodies{};}grenades;
const VitaZoneExit*near_exit=&vita_zone_exits[4];
float moving_player_eye[]={-75.06146f,3.47897f,9.56245f};float ground_y=1.77897f;
unsigned captures=0,changes=0,relaunches=0,destination_seen=0;
std::vector<int>order;
struct Post{void finish(){order.push_back(1);}}postprocess;
struct Audio{void stop_speech(){}}game_audio;
VitaSaveState capture_save(){++captures;return {g_zone};}
bool change_vita_zone(const VitaSaveState&from,const VitaZoneExit&e,VitaSaveState&to){
 assert(from.zone==e.from);++changes;to.zone=e.to;return true;
}
void relaunch_vita_state(int,VitaSaveState&s){++relaunches;destination_seen=s.zone;order.push_back(2);}
int graphics=0;
void tick(unsigned buttons,unsigned edges,bool interact=false){
 pad.buttons=buttons;pressed=edges;interact_pressed=interact;now+=100;
 zone_dialog_active=zone_confirmation.active();zone_exit_choice=zone_dialog_active?zone_confirmation.exit():0;
 zone_transition_confirmed=false;dialog_consumed_input=zone_dialog_active;any_dialog=zone_dialog_active;
#include "zone_input.inc"
#include "zone_transition.inc"
}
int main(){
 tick(0,0,true);assert(zone_confirmation.active()&&captures==0&&changes==0&&relaunches==0);
 // Holding a confirmation button while the window opens must never launch the transition.
 tick(SCE_CTRL_CROSS,SCE_CTRL_CROSS);assert(zone_confirmation.active()&&!zone_transition_confirmed&&dialog_consumed_input);
 tick(0,0);tick(SCE_CTRL_CIRCLE,SCE_CTRL_CIRCLE);
 assert(!zone_confirmation.active()&&captures==0&&changes==0&&relaunches==0);
 tick(0,0);assert(!zone_confirmation.active()); // standing at the gate is not a confirmation
 tick(0,0,true);tick(0,0);tick(SCE_CTRL_CROSS,SCE_CTRL_CROSS);
 assert(!zone_confirmation.active()&&captures==1&&changes==1&&relaunches==1&&destination_seen==3);
 assert(order==std::vector<int>({1,2}));
 // A gate that becomes invalid cannot write a transition checkpoint.
 transition_retry_after=0;save_menu_open=false;tick(0,0,true);tick(0,0);
 moving_player_eye[0]+=100;tick(SCE_CTRL_CROSS,SCE_CTRL_CROSS);
 assert(!zone_confirmation.active()&&captures==1&&relaunches==1);moving_player_eye[0]-=100;
 // Story locks and unavailable UI must never bypass the question.
 transition_retry_after=0;campaign_story=4;tick(0,0,true);assert(!zone_confirmation.active());
 campaign_story=5;zone_confirmation_ui_ready=false;tick(0,0,true);assert(!zone_confirmation.active());zone_confirmation_ui_ready=true;
 for(unsigned i=0;i<vita_zone_exits.size();++i){
  VitaZoneConfirmation prompt;assert(prompt.open(i)&&prompt.exit()==i);
  assert(!prompt.open(0));prompt.update(false,false,true);
  assert(prompt.update(true,true,false)==VitaZoneConfirmation::Action::Cancel&&!prompt.active());
  assert(prompt.open(i));prompt.update(false,false,true);
  assert(prompt.update(true,false,false)==VitaZoneConfirmation::Action::Confirm&&!prompt.active());
 }
 VitaZoneConfirmation invalid;assert(!invalid.open(vita_zone_exits.size()));
 puts("Production zone transition: explicit prompt, held buttons, cancel/no checkpoint, confirm/loading, locks and all exits passed");
}
