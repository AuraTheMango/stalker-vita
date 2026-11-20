#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

enum class VitaWeapon { Holstered, Pistol, Knife, Shotgun, Rifle, Grenade };
enum class VitaInventoryItem { Pistol, Ammunition, Knife, FlashDrive, Medkit, Bandage, Bread, Sausage,
 CannedFood, EnergyDrink, NoviceOutfit, BanditOutfit, Shotgun, Rifle, Buckshot, Slug, RifleFMJ, RifleAP, PistolPBP, RGD5, F1, Silencer, Scope, StrelokFlash, Count };
enum class VitaFlashStatus { Find, Carrying, Delivered };
constexpr size_t vita_item_count=static_cast<size_t>(VitaInventoryItem::Count);
using VitaItemStacks=std::array<unsigned,vita_item_count>;
constexpr size_t vita_item_index(VitaInventoryItem item){return static_cast<size_t>(item);}

class VitaInventory
{
public:
 struct State
 {
  unsigned equipment=0,equipped=0,weapon=0,loaded=0,reserve=0,flash=0,selected=0;
  VitaItemStacks items{};
  unsigned cash=0,outfit=0,food=1000,bleeding=0,extended=0;
  std::array<unsigned,2> outfit_conditions{1000,1000};
  unsigned primary=0,addons=0,grenade_type=0;
  std::array<unsigned,2> primary_loaded{};
  std::array<unsigned,3> ammo_modes{};
 };
 static constexpr int pm_capacity=8,fmj_box=20,starting_reserve=40,flash_reward=1500;
 static constexpr unsigned max_money=10000000,max_ammo=4096,hard_weight=60000;
 VitaInventory(){m.extended=1;}
 State state()const{return m;}
 bool restore(const State& source)
 {
  State s=source;
  if(!s.extended)
  {
   if(s.equipment>1||s.equipped>s.equipment||s.loaded>pm_capacity||s.reserve>max_ammo||s.flash>2||s.selected>=4)return false;
   s.items={};s.items[0]=s.equipment;s.items[1]=s.reserve;s.items[2]=s.equipment;
   s.items[3]=s.flash==1;s.items[10]=s.equipment;
   s.cash=s.flash==2?flash_reward:0;s.outfit=s.equipment?1:0;
   s.food=1000;s.bleeding=0;s.outfit_conditions={1000,1000};s.extended=1;
  }
  if(s.extended!=1||s.equipment>1||s.equipped>1||s.weapon>5||s.loaded>pm_capacity||
   s.reserve>max_ammo||s.reserve!=s.items[1]||s.flash>2||s.selected>=vita_item_count||
   s.cash>max_money||s.outfit>2||s.food>1000||s.bleeding>1000||s.outfit_conditions[0]>1000||s.outfit_conditions[1]>1000||
   s.items[3]!=(s.flash==1?1u:0u)||s.items[2]>1||
   (s.equipped&&!s.items[0])||(s.loaded&&!s.items[0])||
   (s.weapon==1&&(!s.equipped||!s.items[0]))||(s.weapon==2&&!s.items[2])||
   (s.outfit&&!s.items[s.outfit==1?10:11]))return false;
  for(size_t i=0;i<vita_item_count;++i)if(s.items[i]>limit(static_cast<VitaInventoryItem>(i)))return false;
  if(s.primary>2||s.addons>5||(s.addons&2)||s.grenade_type>1||s.primary_loaded[0]>2||s.primary_loaded[1]>30||
    std::any_of(s.ammo_modes.begin(),s.ammo_modes.end(),[](unsigned v){return v>1;})||
    (s.primary&&!s.items[s.primary==1?12:13])||
    (s.primary_loaded[0]&&!s.items[12])||(s.primary_loaded[1]&&!s.items[13])||
    (s.weapon==3&&s.primary!=1)||(s.weapon==4&&s.primary!=2)||
    (s.weapon==5&&!s.items[s.grenade_type?20:19])||
    ((s.addons&1)&&(!s.items[21]||!s.items[0]))||((s.addons&4)&&(!s.items[22]||!s.items[13])))return false;
  VitaInventory candidate;candidate.m=s;
  if(candidate.weight_grams()>hard_weight)return false;
  m=s;repair_selection();return true;
 }
 static unsigned grams(VitaInventoryItem item)
 {
  constexpr unsigned weights[]={730,10,0,0,300,100,300,500,500,300,3000,3000,1900,3300,45,50,11,11,10,350,630,100,200,100};
  return vita_item_index(item)<vita_item_count?weights[vita_item_index(item)]:0;
 }
 static unsigned base_cost(VitaInventoryItem item)
 {
  constexpr unsigned costs[]={280,70,70,0,150,20,20,50,100,75,1000,3000,400,2000,50,60,200,250,100,100,210,100,200,0};
  return vita_item_index(item)<vita_item_count?costs[vita_item_index(item)]:0;
 }
 static bool ammunition(VitaInventoryItem item){const auto i=vita_item_index(item);return i==1||(i>=14&&i<=18);}
 static unsigned lot(VitaInventoryItem item){const auto i=vita_item_index(item);return i==1||i==18?20:i==14||i==15?10:i==16||i==17?30:1;}
 static unsigned limit(VitaInventoryItem item){return item==VitaInventoryItem::StrelokFlash?1:ammunition(item)?max_ammo:item==VitaInventoryItem::Pistol||item==VitaInventoryItem::Shotgun||item==VitaInventoryItem::Rifle||item==VitaInventoryItem::NoviceOutfit||item==VitaInventoryItem::BanditOutfit?16:64;}
 static unsigned stack_grams(VitaInventoryItem item,unsigned n){return item==VitaInventoryItem::RifleFMJ||item==VitaInventoryItem::RifleAP?(n*320+15)/30:n*grams(item);}
 static VitaInventoryItem ammo_item(unsigned slot,unsigned mode){constexpr unsigned types[3][2]={{1,18},{14,15},{16,17}};return static_cast<VitaInventoryItem>(types[slot][mode]);}
 static unsigned capacity(unsigned slot){return slot==0?8:slot==1?2:30;}
 unsigned active_slot()const{return m.weapon==3?1:m.weapon==4?2:0;}
 bool firearm()const{return m.weapon==1||m.weapon==3||m.weapon==4;}
 bool scoped()const{return m.weapon==4&&(m.addons&4);}
 bool silenced()const{return m.weapon==1&&(m.addons&1);}
 bool attachment(unsigned bit)const{return (m.addons&bit)!=0;}
 unsigned magazine(unsigned slot)const{return slot==0?m.loaded:m.primary_loaded[slot-1];}
 unsigned ammo_mode(unsigned slot)const{return m.ammo_modes[slot];}
 VitaInventoryItem current_ammo()const{return ammo_item(active_slot(),m.ammo_modes[active_slot()]);}
 unsigned magazine_capacity()const{return capacity(active_slot());}
 bool select_ammo(VitaInventoryItem item){
  if(!ammunition(item)||!contains(item))return false;
  for(unsigned slot=0;slot<3;++slot)for(unsigned mode=0;mode<2;++mode)if(ammo_item(slot,mode)==item){
   if(m.ammo_modes[slot]==mode)return true;
   const auto previous=vita_item_index(ammo_item(slot,m.ammo_modes[slot]));
   if(m.items[previous]>max_ammo-magazine(slot))return false;
   m.items[previous]+=magazine(slot);mag(slot)=0;m.ammo_modes[slot]=mode;normalize();return true;
  }return false;
 }
 bool toggle_accessory(VitaInventoryItem item){
  const unsigned bit=item==VitaInventoryItem::Silencer?1:item==VitaInventoryItem::Scope?4:0;
  if(!bit||!contains(item)||!contains(bit==1?VitaInventoryItem::Pistol:VitaInventoryItem::Rifle))return false;
  m.addons^=bit;return true;
 }
 bool select_grenade(VitaInventoryItem item){
  if((item!=VitaInventoryItem::RGD5&&item!=VitaInventoryItem::F1)||!contains(item))return false;
  m.grenade_type=item==VitaInventoryItem::F1;return true;
 }
 VitaInventoryItem grenade()const{return m.grenade_type?VitaInventoryItem::F1:VitaInventoryItem::RGD5;}
 bool throw_grenade(){if(m.weapon!=5||!contains(grenade()))return false;--m.items[vita_item_index(grenade())];normalize();repair_selection();return true;}
 static bool tradable(VitaInventoryItem item)
 {return vita_item_index(item)<vita_item_count&&item!=VitaInventoryItem::Knife&&item!=VitaInventoryItem::FlashDrive&&item!=VitaInventoryItem::StrelokFlash;}
 unsigned count(VitaInventoryItem item)const{return vita_item_index(item)<vita_item_count?m.items[vita_item_index(item)]:0;}
 bool contains(VitaInventoryItem item)const{return count(item)>0;}
 bool has_items()const{return std::any_of(m.items.begin(),m.items.end(),[](unsigned n){return n!=0;});}
 unsigned item_weight_grams(VitaInventoryItem item)const{
  unsigned mass=stack_grams(item,count(item));
  if(item==VitaInventoryItem::Pistol||item==VitaInventoryItem::Shotgun||item==VitaInventoryItem::Rifle){
   const unsigned slot=item==VitaInventoryItem::Pistol?0:item==VitaInventoryItem::Shotgun?1:2;
   mass+=stack_grams(ammo_item(slot,m.ammo_modes[slot]),magazine(slot));
  }return mass;
 }
 unsigned weight_grams()const
 {
  auto quantities=m.items;
  // Round a calibre only once, including rounds in its magazine: reloading cannot change mass.
  for(unsigned slot=0;slot<3;++slot)quantities[vita_item_index(ammo_item(slot,m.ammo_modes[slot]))]+=magazine(slot);
  unsigned total=0;for(size_t i=0;i<vita_item_count;++i)total+=stack_grams(static_cast<VitaInventoryItem>(i),quantities[i]);
  return total;
 }
 float weight()const{return weight_grams()/1000.f;}
 bool overweight()const{return weight_grams()>50000;}
 float carry_speed()const{return overweight()?.60f:1.f;}
 bool add(VitaInventoryItem item,unsigned amount)
 {
  if(!amount||vita_item_index(item)>=vita_item_count||item==VitaInventoryItem::FlashDrive||item==VitaInventoryItem::StrelokFlash||item==VitaInventoryItem::Knife)return false;
  const auto index=vita_item_index(item);
  const unsigned maximum=limit(item);
  if(amount>maximum-m.items[index])return false;
  auto candidate=*this;candidate.m.items[index]+=amount;
  if(candidate.weight_grams()>hard_weight)return false;
  candidate.normalize();candidate.repair_selection();*this=candidate;return true;
 }
 bool remove(VitaInventoryItem item,unsigned amount)
 {
  if(!tradable(item)||!amount||amount>count(item))return false;
  const auto index=vita_item_index(item);
  if((item==VitaInventoryItem::Pistol||item==VitaInventoryItem::Shotgun||item==VitaInventoryItem::Rifle)&&amount==m.items[index]){
   const unsigned slot=item==VitaInventoryItem::Pistol?0:item==VitaInventoryItem::Shotgun?1:2;
   const unsigned ammo=vita_item_index(ammo_item(slot,m.ammo_modes[slot]));
   if(m.items[ammo]>max_ammo-magazine(slot))return false;
   m.items[ammo]+=magazine(slot);mag(slot)=0;
  }
  m.items[index]-=amount;normalize();repair_selection();return true;
 }
 bool give_money(unsigned amount){if(amount>max_money-m.cash)return false;m.cash+=amount;return true;}
 bool spend_money(unsigned amount){if(amount>m.cash)return false;m.cash-=amount;return true;}
 int money()const{return static_cast<int>(m.cash);}
 bool grant_starting_equipment()
 {
  if(m.equipment)return false;
  auto next=*this;
  next.m.equipment=1;next.m.items[2]=1;
  if(!next.add(VitaInventoryItem::Pistol,1)||!next.add(VitaInventoryItem::Ammunition,starting_reserve)||
   !next.add(VitaInventoryItem::NoviceOutfit,1)||next.weight_grams()+pm_capacity*10>hard_weight)return false;
  next.m.outfit=1;next.m.loaded=pm_capacity;next.normalize();*this=next;return true;
 }
 VitaFlashStatus flash_status()const{return static_cast<VitaFlashStatus>(m.flash);}
 bool receive_flash(){if(!m.equipment||m.flash)return false;m.flash=1;m.items[3]=1;return true;}
 bool receive_strelok_flash(){if(m.items[23]||weight_grams()+100>hard_weight)return false;m.items[23]=1;return true;}
 bool deliver_flash()
 {
  if(m.flash!=1||!give_money(flash_reward))return false;
  m.flash=2;m.items[3]=0;repair_selection();return true;
 }
 bool pistol_equipped()const{return m.equipped!=0;}
 bool toggle_pistol(VitaInventoryItem item)
 {
  if(!contains(item))return false;
  if(item==VitaInventoryItem::Shotgun||item==VitaInventoryItem::Rifle){
   const unsigned primary=item==VitaInventoryItem::Shotgun?1:2;m.primary=m.primary==primary?0:primary;
   if((m.weapon==3&&m.primary!=1)||(m.weapon==4&&m.primary!=2))m.weapon=0;return true;}
  if(item!=VitaInventoryItem::Pistol)return false;
  m.equipped=!m.equipped;if(!m.equipped&&m.weapon==1)m.weapon=0;return true;
 }
 bool toggle_outfit(VitaInventoryItem item)
 {
  if((item!=VitaInventoryItem::NoviceOutfit&&item!=VitaInventoryItem::BanditOutfit)||!contains(item))return false;
  const unsigned outfit=item==VitaInventoryItem::NoviceOutfit?1:2;
  m.outfit=m.outfit==outfit?0:outfit;return true;
 }
 unsigned outfit()const{return m.outfit;}
 unsigned outfit_condition()const{return m.outfit?m.outfit_conditions[m.outfit-1]:0;}
 unsigned protection()const{return (m.outfit==1?10u:m.outfit==2?15u:0u)*outfit_condition()/1000;}
 unsigned absorb_damage(unsigned damage)
 {
  const unsigned applied=damage*(100-protection())/100;
  if(damage&&m.outfit){auto&condition=m.outfit_conditions[m.outfit-1];condition-=std::min(condition,damage*2);}
  if(applied)m.bleeding=std::min(1000u,m.bleeding+70);
  return applied;
 }
 unsigned food()const{return m.food;}
 unsigned bleeding()const{return m.bleeding;}
 void survival_tick(unsigned& health)
 {
  if(m.food)m.food-=1;
  if(m.bleeding){m.bleeding-=std::min(m.bleeding,3u);if(m.bleeding>=150&&health) --health;}
 }
 bool use(VitaInventoryItem item,unsigned& health,float& endurance)
 {
  if(!health||!contains(item))return false;
  unsigned healing=0,satiety=0,bleed=0;float power=0;
  switch(item){
  case VitaInventoryItem::Medkit:healing=60;bleed=600;break;
  case VitaInventoryItem::Bandage:healing=10;bleed=1000;break;
  case VitaInventoryItem::Bread:healing=5;satiety=200;break;
  case VitaInventoryItem::Sausage:healing=10;satiety=300;break;
  case VitaInventoryItem::CannedFood:healing=15;satiety=400;break;
  case VitaInventoryItem::EnergyDrink:power=1.f;break;
  default:return false;
  }
  if(!((healing&&health<100)||(satiety&&m.food<1000)||(bleed&&m.bleeding)||(power>0&&endurance<1.f)))return false;
  --m.items[vita_item_index(item)];
  health=std::min(100u,health+healing);m.food=std::min(1000u,m.food+satiety);
  m.bleeding-=std::min(m.bleeding,bleed);endurance=std::min(1.f,endurance+power);
  repair_selection();return true;
 }
 VitaWeapon weapon()const{return static_cast<VitaWeapon>(m.weapon);}
 unsigned primary()const{return m.primary;}
 int loaded()const{return magazine(active_slot());}
 int reserve()const{return count(current_ammo());}
 void cycle_weapon()
 {
  const unsigned cycle[]={0,1,2,m.primary==1?3u:4u,5};
  unsigned position=0;for(unsigned i=0;i<5;++i)if(cycle[i]==m.weapon)position=i;
  for(unsigned step=1;step<=5;++step){
   const unsigned next=cycle[(position+step)%5];
   if(next==0||(next==1&&m.equipped&&contains(VitaInventoryItem::Pistol))||
     (next==2&&contains(VitaInventoryItem::Knife))||((next==3||next==4)&&m.primary)||
     (next==5&&(contains(VitaInventoryItem::RGD5)||contains(VitaInventoryItem::F1)))){
    m.weapon=next;if(next==5&&!contains(grenade()))m.grenade_type^=1;return;}
  }
 }
 bool fire_pistol(){if(!firearm()||!loaded())return false;--mag(active_slot());return true;}
 bool reload_pistol(){
  if(!firearm()||!reserve()||unsigned(loaded())==magazine_capacity())return false;
  const unsigned i=vita_item_index(current_ammo()),rounds=std::min(m.items[i],magazine_capacity()-unsigned(loaded()));
  mag(active_slot())+=rounds;m.items[i]-=rounds;normalize();return true;
 }
 VitaInventoryItem selected()const{return static_cast<VitaInventoryItem>(m.selected);}
 void move_selection(int step)
 {
  if(!has_items())return;
  int current=m.selected;
  for(size_t i=0;i<vita_item_count;++i){current=(current+int(vita_item_count)+(step<0?-1:1))%int(vita_item_count);
   if(m.items[current]){m.selected=current;return;}}
 }
private:
 unsigned& mag(unsigned slot){return slot==0?m.loaded:m.primary_loaded[slot-1];}
 void normalize()
 {
  m.reserve=m.items[1];
  if(!m.items[0]){m.equipped=0;if(m.weapon==1)m.weapon=0;}
  if(m.outfit&&!m.items[m.outfit==1?10:11])m.outfit=0;
  if(m.primary&&!m.items[m.primary==1?12:13]){m.primary=0;if(m.weapon==3||m.weapon==4)m.weapon=0;}
  if(!m.items[21]||!m.items[0])m.addons&=~1u;
  if(!m.items[22]||!m.items[13])m.addons&=~4u;
  if(m.weapon==5&&!contains(grenade())){
   if(contains(m.grenade_type?VitaInventoryItem::RGD5:VitaInventoryItem::F1))m.grenade_type^=1;else m.weapon=0;}

 }
 void repair_selection()
 {
  if(m.selected>=vita_item_count)m.selected=0;
  if(!m.items[m.selected])for(unsigned i=0;i<vita_item_count;++i)if(m.items[i]){m.selected=i;break;}
 }
 State m{};
};
