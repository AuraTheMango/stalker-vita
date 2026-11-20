#include "vita_save.cpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
using Item=VitaInventoryItem;
int main(int argc,char**argv){
 assert(argc==2);std::filesystem::create_directories(argv[1]);
 VitaInventory inv;assert(inv.grant_starting_equipment());assert(!inv.grant_starting_equipment());
 assert(inv.weight_grams()==4210&&inv.reserve()==40&&inv.loaded()==8);
 assert(inv.toggle_pistol(Item::Pistol));inv.cycle_weapon();assert(inv.fire_pistol());
 assert(inv.remove(Item::Pistol,1)&&inv.reserve()==47&&!inv.loaded()&&!inv.pistol_equipped());
 assert(inv.add(Item::Pistol,1));assert(inv.toggle_pistol(Item::Pistol));inv.cycle_weapon();
 assert(inv.reload_pistol()&&inv.loaded()==8&&inv.reserve()==39);
 assert(!inv.add(Item::Knife,1)&&!inv.remove(Item::Knife,1));
 assert(inv.receive_flash()&&!inv.receive_flash()&&!inv.remove(Item::FlashDrive,1));
 assert(inv.deliver_flash()&&!inv.deliver_flash()&&inv.money()==1500);
 assert(inv.add(Item::Medkit,2)&&inv.add(Item::Bandage,2)&&inv.add(Item::Bread,2)&&inv.add(Item::EnergyDrink,2));
 unsigned hp=100;float stamina=1;assert(!inv.use(Item::Medkit,hp,stamina)&&inv.count(Item::Medkit)==2);
 assert(inv.absorb_damage(20)==18&&inv.bleeding()==70&&inv.outfit_condition()==960);hp=82;
 assert(inv.use(Item::Bandage,hp,stamina)&&hp==92&&!inv.bleeding());
 assert(inv.use(Item::Medkit,hp,stamina)&&hp==100);
 stamina=.1f;assert(inv.use(Item::EnergyDrink,hp,stamina)&&stamina==1);
 inv.survival_tick(hp);assert(inv.food()==999&&inv.use(Item::Bread,hp,stamina)&&inv.food()==1000);
 hp=0;assert(!inv.use(Item::Medkit,hp,stamina));hp=100;
 assert(inv.add(Item::BanditOutfit,1)&&inv.toggle_outfit(Item::BanditOutfit)&&inv.protection()==15);
 inv.absorb_damage(20);assert(inv.outfit_condition()==960);
 assert(inv.toggle_outfit(Item::NoviceOutfit)&&inv.outfit_condition()==960);
 VitaInventory heavy;assert(heavy.add(Item::BanditOutfit,16)&&heavy.add(Item::CannedFood,24));
 assert(heavy.weight_grams()==60000&&heavy.overweight()&&heavy.carry_speed()<1);
 assert(!heavy.add(Item::Bandage,1));assert(!heavy.grant_starting_equipment()&&!heavy.state().equipment);
 assert(heavy.remove(Item::CannedFood,24)&&!heavy.overweight());
 VitaLootWorld world;world.body(0,{2,0,3});const unsigned cash=world.containers[0].cash;
 assert(world.take_cash(0,inv)&&!world.take_cash(0,inv));
 auto saved=world.containers[0];world.body(0,{99,0,99});
 assert(world.containers[0].cash==0&&world.containers[0].position[0]==2);
 const unsigned ammo=inv.reserve(),bodyammo=world.containers[0].items[1];
 assert(world.take(0,inv,Item::Ammunition,bodyammo));assert(inv.reserve()==ammo+bodyammo);
 assert(!world.take(0,inv,Item::Ammunition,1));world.body(0,{2,0,3});assert(!world.containers[0].items[1]);
 const unsigned meds=inv.count(Item::Medkit);
 assert(world.drop(inv,Item::Medkit,1,{0,0,0}));assert(inv.count(Item::Medkit)==meds-1);
 assert(world.take(12,inv,Item::Medkit,1));assert(inv.count(Item::Medkit)==meds);
 assert(!world.drop(inv,Item::FlashDrive,1,{0,0,0})&&!world.drop(inv,Item::Knife,1,{0,0,0}));
 world.supplies(9,{0,0,0});world.take_all(9,inv);world.supplies(9,{0,0,0});assert(world.containers[9].empty());
 assert(world.valid());
 // Exhaust the bounded drop pool without losing an object.
 VitaLootWorld full;for(unsigned i=12;i<32;++i){full.containers[i].kind=3;full.containers[i].items[5]=1;}
 auto previous=inv.state();assert(!full.drop(inv,Item::Bandage,1,{0,0,0}));assert(inv.state().items==previous.items);
 // Barter from zero cash, cancellation, atomic failures and conservation.
 VitaInventory buyer;buyer.grant_starting_equipment();VitaTraderStock vendor;VitaTradeBasket basket;
 assert(basket.add(true,Item::Medkit,1,buyer,vendor));
 auto before=buyer.state();auto stock=vendor;
 assert(basket.commit(buyer,vendor)==VitaTradeResult::Funds);
 assert(buyer.state().items==before.items&&vendor.items==stock.items&&vendor.cash==stock.cash);
 basket.undo(true,Item::Medkit,1);assert(basket.empty());
 assert(basket.add(false,Item::NoviceOutfit,1,buyer,vendor));
 assert(basket.add(true,Item::Medkit,1,buyer,vendor));
 assert(basket.balance()==320);const auto money=buyer.money()+vendor.cash;
 assert(basket.commit(buyer,vendor)==VitaTradeResult::Success);
 assert(buyer.money()==320&&buyer.count(Item::Medkit)==1&&buyer.outfit()==0);
 assert(buyer.money()+vendor.cash==money&&vendor.items[10]==stock.items[10]+1);
 assert(basket.commit(buyer,vendor)==VitaTradeResult::Empty);
 assert(!basket.add(false,Item::Knife,1,buyer,vendor));
 basket.add(true,Item::Medkit,1,buyer,vendor);auto old=basket;
 assert(!basket.add(false,Item::Medkit,8192,buyer,vendor));assert(basket.buy==old.buy&&basket.sell==old.sell);
 basket={};assert(basket.add(true,Item::Ammunition,20,buyer,vendor));assert(basket.balance()==-84);
 assert(basket.commit(buyer,vendor)==VitaTradeResult::Success&&buyer.reserve()==60);
 basket.add(false,Item::Ammunition,20,buyer,vendor);
 assert(basket.balance()==35&&basket.commit(buyer,vendor)==VitaTradeResult::Success&&buyer.reserve()==40);
 basket={};vendor.cash=0;basket.add(false,Item::Medkit,1,buyer,vendor);before=buyer.state();
 assert(basket.commit(buyer,vendor)==VitaTradeResult::TraderFunds&&buyer.state().items==before.items);
 // Weight failure rolls back both sides, including sold goods.
 vendor=VitaTraderStock{};heavy.give_money(10000);heavy.add(Item::CannedFood,24);
 basket={};basket.add(true,Item::Pistol,1,heavy,vendor);
 stock=vendor;before=heavy.state();
 assert(basket.commit(heavy,vendor)==VitaTradeResult::Capacity);
 assert(heavy.state().items==before.items&&heavy.money()==int(before.cash)&&vendor.items==stock.items);
 // Rifle round fractions are rounded once per calibre across the magazine and backpack.
 VitaInventory rifle_mass;rifle_mass.grant_starting_equipment();rifle_mass.add(Item::Rifle,1);rifle_mass.add(Item::RifleFMJ,2);
 rifle_mass.toggle_pistol(Item::Rifle);for(unsigned i=0;i<5&&rifle_mass.weapon()!=VitaWeapon::Rifle;++i)rifle_mass.cycle_weapon();
 auto grams=rifle_mass.weight_grams();assert(rifle_mass.reload_pistol()&&rifle_mass.weight_grams()==grams);
 assert(rifle_mass.fire_pistol()&&rifle_mass.add(Item::RifleFMJ,1)&&rifle_mass.weight_grams()==grams);
 assert(rifle_mass.reload_pistol()&&rifle_mass.weight_grams()==grams);
 // Full save, emptied corpse marker, vendor currency, quantities, condition, consumables.
 VitaSaveState state;state.eye={0,1.7f,0};state.inventory=inv.state();state.flags=SaveDoorUnlocked|SaveWolfMission|SaveScoutMet|SaveFarmCleared;
 state.guards.fill(0);state.loot=world;state.trader=vendor;
 assert(valid(state));auto bytes=encode(state,3);assert(bytes.size()==18532);
 const std::string directory=argv[1],a=directory+"/save_a.bin",b=directory+"/save_b.bin";
 auto put=[&](const std::string&path,const auto&data){std::ofstream f(path,std::ios::binary);f.write((char*)data.data(),data.size());};
 std::filesystem::remove(a);std::filesystem::remove(b);put(a,bytes);
 VitaSaveState loaded;assert(read_vita_save(argv[1],loaded));
 VitaInventory restored;assert(restored.restore(loaded.inventory));
 assert(restored.state().items==inv.state().items&&restored.money()==inv.money());
 assert(restored.state().outfit_conditions==inv.state().outfit_conditions);
 assert(loaded.loot.containers[0].cash==0&&loaded.loot.containers[9].empty()&&loaded.loot.containers[9].kind==2);
 assert(loaded.trader.cash==vendor.cash&&loaded.trader.items==vendor.items);
 // Reported six PM / 140 rounds / light supplies: 10.86 kg, never 109 kg.
 VitaInventory reported;assert(reported.grant_starting_equipment());
 assert(reported.add(Item::Pistol,5)&&reported.add(Item::Ammunition,100));
 assert(reported.add(Item::Medkit,1)&&reported.add(Item::Bread,2)&&reported.add(Item::CannedFood,1)&&
  reported.add(Item::Bandage,1)&&reported.add(Item::Sausage,1));
 assert(reported.count(Item::Pistol)==6&&reported.reserve()==140&&reported.weight_grams()==10860&&!reported.overweight());
 // New slots, real weapon prices, exact 30-round box mass and attachable inventory conservation.
 assert(VitaInventory::base_cost(Item::Shotgun)==400);
 assert(VitaInventory::stack_grams(Item::RifleFMJ,30)==320);
 assert(inv.add(Item::Rifle,1)&&inv.add(Item::RifleAP,60)&&inv.toggle_pistol(Item::Rifle));
 assert(inv.add(Item::Scope,1)&&inv.add(Item::Silencer,1)&&inv.toggle_accessory(Item::Scope)&&inv.toggle_accessory(Item::Silencer));
 assert(inv.select_ammo(Item::RifleAP));
 for(unsigned n=0;n<6&&inv.weapon()!=VitaWeapon::Rifle;++n)inv.cycle_weapon();
 assert(inv.weapon()==VitaWeapon::Rifle&&inv.reload_pistol()&&inv.loaded()==30&&inv.reserve()==30&&inv.scoped());
 state.inventory=inv.state();bytes=encode(state,3);put(a,bytes);
 assert(read_vita_save(argv[1],loaded)&&restored.restore(loaded.inventory));
 assert(restored.scoped()&&restored.loaded()==30&&restored.reserve()==30&&restored.attachment(1));
 // Format 4 uses the complete 4220-byte pre-checksum body, without v5 extension.
 auto old4=bytes;old4.resize(4220);old4[4]=4;
 for(unsigned k=0;k<4;++k)old4[16+k]=4224u>>(k*8);
 auto crc4=checksum(old4);for(unsigned k=0;k<4;++k)old4.push_back(crc4>>(k*8));
 put(a,old4);assert(read_vita_save(argv[1],loaded)&&!loaded.world_details&&restored.restore(loaded.inventory));
 assert(restored.scoped()&&restored.loaded()==30&&restored.reserve()==30);
 // Legacy 0.0.17 (format 3, 12 stack entries), including emptied-container markers.
 auto old_inventory=reported.state();state.inventory=old_inventory;auto new_record=encode(state,3);
 std::vector<uint8_t> legacy3(new_record.begin(),new_record.begin()+392);
 const auto append=[&](size_t a,size_t b){legacy3.insert(legacy3.end(),new_record.begin()+a,new_record.begin()+b);};
 append(392,440);append(484,508);append(508,556);append(600,604);
 for(unsigned i=0;i<32;++i)append(604+i*112,604+i*112+68);
 assert(legacy3.size()==2692);legacy3[4]=3;const unsigned v3size=2696;
 for(unsigned k=0;k<4;++k)legacy3[16+k]=v3size>>(k*8);
 auto crc3=checksum(legacy3);for(unsigned k=0;k<4;++k)legacy3.push_back(crc3>>(k*8));
 put(a,legacy3);assert(read_vita_save(argv[1],loaded)&&restored.restore(loaded.inventory));
 assert(restored.weight_grams()==10860&&restored.primary()==0&&loaded.loot.containers[9].kind==2);
 assert(loaded.trader.items[12]==2&&loaded.trader.items[13]==1&&loaded.trader.items[19]==5);
 put(a,bytes);
 // Every byte and every shortened record must be rejected without modifying the caller.
 for(size_t i=0;i<bytes.size();++i){auto bad=bytes;bad[i]^=1;put(a,bad);loaded.health=77;
  assert(!read_vita_save(argv[1],loaded)&&loaded.health==77);}
 for(size_t n:{0u,1u,311u,312u,395u,396u,4223u}){auto truncated=bytes;truncated.resize(n);put(a,truncated);assert(!read_vita_save(argv[1],loaded));}
 // Versions 1 and 2 migrate inventory and allow a subsequent version 3 save.
 VitaInventory starter;starter.grant_starting_equipment();state.inventory=starter.state();state.flags=SaveDoorUnlocked;
 state.loot={};state.guards.fill(2);bytes=encode(state,1);
 for(unsigned version:{1u,2u}){
  auto legacy=bytes;const unsigned count=version==1?312:396;legacy.resize(count-4);
  legacy[4]=version;for(unsigned k=0;k<4;++k)legacy[16+k]=count>>(k*8);
  const auto crc=checksum(legacy);for(unsigned k=0;k<4;++k)legacy.push_back(crc>>(k*8));
  put(a,legacy);std::filesystem::remove(b);
  assert(read_vita_save(argv[1],loaded)&&restored.restore(loaded.inventory));
  assert(restored.reserve()==40&&restored.outfit()==1&&restored.money()==0);
  assert(write_vita_save(argv[1],loaded)&&read_vita_save(argv[1],loaded)&&loaded.inventory.extended==1);
 }
 // A damaged newest generation preserves the older committed save.
 std::filesystem::remove(a);std::filesystem::remove(b);
 assert(write_vita_save(argv[1],state));state.health=65;assert(write_vita_save(argv[1],state));
 {std::fstream f(b,std::ios::binary|std::ios::in|std::ios::out);f.put('!');}
 assert(read_vita_save(argv[1],loaded)&&loaded.health==100);
 std::cout<<"Economy, loot, consumables, carry limits, atomic barter, save v1/v2/v3/v4 and corruption checks passed\n";
}
