#pragma once
#include "vita_inventory.h"
#include <array>
#include <cmath>

struct VitaLootContainer
{
 // 0 unused, 1 searchable body, 2 supply bag, 3 dropped object.
 unsigned kind=0,cash=0;
 std::array<float,3> position{};
 VitaItemStacks items{};
 bool empty()const{return !cash&&std::none_of(items.begin(),items.end(),[](unsigned n){return n!=0;});}
};
class VitaLootWorld
{
public:
 static constexpr size_t capacity=32,body_count=9;
 std::array<VitaLootContainer,capacity> containers{};
 void body(unsigned id,const std::array<float,3>& position,unsigned zone=0)
 {
  if(id>=body_count||containers[id].kind)return;
  auto& c=containers[id];c.kind=1;c.position=position;
  c.items[0]=id<6?1:0;c.items[1]=id<6?12+(id%3)*6:0;
  if(id>=6){c.items[12]=1;c.items[14]=10;}
  if(id==2||id==4)c.items[19]=1;c.items[5]=1;
  c.items[id%2?6:8]=1;c.cash=35+(id%4)*15;
  if(id==0)c.items[11]=1;
  // Added zones use rifles for allied stalkers and Agroprom soldiers.
  if(zone&&(id>=6||zone==2)){
   c.items[0]=c.items[1]=c.items[12]=c.items[14]=c.items[11]=0;
   c.items[13]=1;c.items[16]=30;
  }
 }
 void supplies(unsigned id,const std::array<float,3>& position)
 {
  if(id<9||id>11||containers[id].kind)return;
  auto&c=containers[id];c.kind=2;c.position=position;
  c.items[4]=1;c.items[5]=2;c.items[6]=1;c.items[9]=1;
  if(id!=9)c.items[1]=20;
 }
 template<class Ground>
 bool place_supplies(Ground& ground,const std::array<float,3>& feet,float yaw)
 {
  if(containers[9].kind)return true;
  for(unsigned ring=0;ring<3;++ring)for(unsigned direction=0;direction<8;++direction)
  {
   const float angle=yaw+direction*.78539816f,distance=1.f+ring*.3f;
   float x=feet[0]+std::sin(angle)*distance,z=feet[2]+std::cos(angle)*distance,y=feet[1];
   ground.slide_move(feet[0],feet[2],feet[1],x,z);
   const float dx=x-feet[0],dz=z-feet[2];
   if(std::hypot(dx,dz)<.75f||!ground.height_at(x,z,y,.4f,.4f,y))continue;
   const float origin[]={feet[0],feet[1]+1.6f,feet[2]},dy=y+.22f-origin[1];
   const float length=std::sqrt(dx*dx+dy*dy+dz*dz);
   const float ray[]={dx/length,dy/length,dz/length};
   if(ground.blocks_ray(origin,ray,length-.25f))continue;
   supplies(9,{x,y,z});return true;
  }
  return false;
 }
 bool drop(VitaInventory& inventory,VitaInventoryItem item,unsigned amount,const std::array<float,3>& position)
 {
  if(!VitaInventory::tradable(item)||!amount)return false;
  size_t slot=capacity;
  for(size_t i=12;i<capacity;++i)if(containers[i].empty()){slot=i;break;}
  if(slot==capacity)return false;
  auto candidate=inventory;
  if(!candidate.remove(item,amount))return false;
  VitaLootContainer drop;drop.kind=3;drop.position=position;drop.items[vita_item_index(item)]=amount;
  containers[slot]=drop;inventory=candidate;return true;
 }
 bool take(size_t id,VitaInventory& inventory,VitaInventoryItem item,unsigned amount)
 {
  if(id>=capacity||vita_item_index(item)>=vita_item_count||!amount)return false;
  auto&c=containers[id];const auto index=vita_item_index(item);
  if(!c.kind||amount>c.items[index]||!inventory.add(item,amount))return false;
  c.items[index]-=amount;return true;
 }
 bool take_cash(size_t id,VitaInventory& inventory)
 {
  if(id>=capacity||!containers[id].cash||!inventory.give_money(containers[id].cash))return false;
  containers[id].cash=0;return true;
 }
 unsigned take_all(size_t id,VitaInventory& inventory)
 {
  if(id>=capacity)return 0;
  unsigned taken=take_cash(id,inventory)?1:0;
  for(size_t i=0;i<vita_item_count;++i)
   while(containers[id].items[i])
   {
    const auto item=static_cast<VitaInventoryItem>(i);
    const unsigned amount=std::min(containers[id].items[i],VitaInventory::lot(item));
    if(!take(id,inventory,item,amount))break;
    taken+=amount;
   }
  return taken;
 }
 bool valid()const
 {
  for(size_t id=0;id<capacity;++id)
  {
   const auto&c=containers[id];
   if(c.kind>3||c.cash>100000||!std::all_of(c.position.begin(),c.position.end(),
     [](float v){return std::isfinite(v)&&std::abs(v)<2000.f;}))return false;
   for(size_t i=0;i<vita_item_count;++i)if(c.items[i]>VitaInventory::limit(static_cast<VitaInventoryItem>(i)))return false;
   if(c.items[2]||c.items[3]||(!c.kind&&!c.empty()))return false;
   if(c.kind==1&&id>=body_count)return false;
   if(id<body_count&&c.kind&&c.kind!=1)return false;
  }
  return true;
 }
};
struct VitaTraderStock
{
 VitaItemStacks items{};
 unsigned cash=50000;
 VitaTraderStock(){items={2,400,0,0,10,20,20,10,10,10,2,1,2,1,100,30,240,60,80,5,3,2,1};}
 bool valid()const
 {
  if(cash>VitaInventory::max_money||items[2]||items[3])return false;
  for(size_t i=0;i<vita_item_count;++i)if(items[i]>(VitaInventory::ammunition(static_cast<VitaInventoryItem>(i))?8192u:512u))return false;
  return true;
 }
};
enum class VitaTradeResult { Ready, Success, Empty, Funds, TraderFunds, Capacity, Stock, Invalid };

struct VitaTradeBasket
{
 VitaItemStacks buy{},sell{};
 bool empty()const
 {
  return std::none_of(buy.begin(),buy.end(),[](unsigned n){return n;})&&
   std::none_of(sell.begin(),sell.end(),[](unsigned n){return n;});
 }
 static unsigned price(VitaInventoryItem item,unsigned amount,bool buying)
 {
  if(!VitaInventory::tradable(item)||amount>8192)return 0;
  const uint64_t total=uint64_t(VitaInventory::base_cost(item))*amount*(buying?120u:50u);
  const unsigned denominator=100*VitaInventory::lot(item);
  return unsigned((total+(buying?denominator-1:0))/denominator);
 }
 int balance()const
 {
  int value=0;
  for(size_t i=0;i<vita_item_count;++i)
  {auto item=static_cast<VitaInventoryItem>(i);value+=int(price(item,sell[i],false))-int(price(item,buy[i],true));}
  return value;
 }
 bool add(bool buying,VitaInventoryItem item,unsigned amount,const VitaInventory& inv,const VitaTraderStock& stock)
 {
  if(!VitaInventory::tradable(item)||!amount||amount>8192)return false;
  const auto original=*this;
  const auto i=vita_item_index(item);
  auto&selected=buying?buy[i]:sell[i];auto&opposite=buying?sell[i]:buy[i];
  // Changing sides first withdraws an existing proposal for this item.
  if(opposite){const unsigned remove=std::min(opposite,amount);opposite-=remove;amount-=remove;if(!amount)return true;}
  const unsigned available=buying?stock.items[i]:inv.count(item);
  if(selected>available||amount>available-selected){*this=original;return false;}
  selected+=amount;return true;
 }
 void undo(bool buying,VitaInventoryItem item,unsigned amount)
 {
  if(vita_item_index(item)>=vita_item_count)return;
  auto& n=buying?buy[vita_item_index(item)]:sell[vita_item_index(item)];n-=std::min(n,amount);
 }
 VitaTradeResult commit(VitaInventory& inventory,VitaTraderStock& trader)
 {
  if(empty())return VitaTradeResult::Empty;
  if(!trader.valid())return VitaTradeResult::Invalid;
  const int64_t difference=balance(),money=int64_t(inventory.money())+difference;
  if(money<0)return VitaTradeResult::Funds;
  if(money>VitaInventory::max_money)return VitaTradeResult::Invalid;
  const int64_t trader_money=int64_t(trader.cash)-difference;
  if(trader_money<0)return VitaTradeResult::TraderFunds;
  if(trader_money>VitaInventory::max_money)return VitaTradeResult::Invalid;
  VitaInventory next=inventory;VitaTraderStock stock=trader;
  for(size_t i=0;i<vita_item_count;++i)
  {
   auto item=static_cast<VitaInventoryItem>(i);
   if((buy[i]||sell[i])&&!VitaInventory::tradable(item))return VitaTradeResult::Invalid;
   if(buy[i]>stock.items[i]||sell[i]>next.count(item))return VitaTradeResult::Stock;
   if(sell[i]&&!next.remove(item,sell[i]))return VitaTradeResult::Invalid;
   if(sell[i]>8192-stock.items[i])return VitaTradeResult::Invalid;
   stock.items[i]+=sell[i];
  }
  for(size_t i=0;i<vita_item_count;++i)
   if(buy[i])
   {
    if(!next.add(static_cast<VitaInventoryItem>(i),buy[i]))return VitaTradeResult::Capacity;
    stock.items[i]-=buy[i];
   }
  if(!stock.valid())return VitaTradeResult::Stock;
  if(difference<0)next.spend_money(unsigned(-difference));else next.give_money(unsigned(difference));
  stock.cash=unsigned(trader_money);
  inventory=next;trader=stock;buy={};sell={};return VitaTradeResult::Success;
 }
};
