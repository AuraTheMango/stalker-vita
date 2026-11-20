#include "vita_save.h"
#include "vita_player_movement.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <sys/stat.h>
#include <vector>
#include <ctime>
#include <unistd.h>
#include <cerrno>

namespace
{
	constexpr uint32_t magic = 0x53565453; // STVS
	constexpr uint32_t version = 8;
	constexpr size_t record_bytes = 23232;
 constexpr size_t version7_bytes=18600;
 constexpr unsigned base_item_count=23;
 constexpr size_t version6_bytes=18532;
 constexpr size_t version5_bytes=4628;
 constexpr size_t version4_bytes=4224;
 constexpr size_t version3_bytes=2696;
 constexpr size_t version2_bytes=396;
	constexpr size_t legacy_bytes=312;

	uint32_t checksum(const std::vector<uint8_t>& bytes)
	{
		uint32_t hash = 2166136261u;
		for (uint8_t byte : bytes)
			hash = (hash ^ byte) * 16777619u;
		return hash;
	}

	bool decode_record(const std::vector<uint8_t>&,VitaSaveState&,uint64_t&,bool memory=false);

	bool valid(const VitaSaveState& state)
	{
		VitaInventory inventory;
		VitaPlayerMovement movement;
		const auto coordinate = [](float value) { return std::isfinite(value) && std::abs(value) < 2000.f; };
		if (!state.loot.valid() || !state.trader.valid() || !inventory.restore(state.inventory) || !movement.restore(state.stance, state.endurance) ||
			state.health == 0 || state.health > 100 || state.flags >= (1u << 12) ||
			!std::all_of(state.eye.begin(), state.eye.end(), coordinate) || !coordinate(state.ground) ||
			!std::isfinite(state.yaw) || std::abs(state.yaw) > 1000000.f ||
			!std::isfinite(state.pitch) || state.pitch < 0.1f || state.pitch > 3.04f ||
			!std::isfinite(state.assault_elapsed) || state.assault_elapsed < 0.f ||
			state.assault_elapsed > 10000000.f ||
			std::abs(state.eye[1] - state.ground - movement.eye_height()) > 0.02f ||
			std::any_of(state.guards.begin(), state.guards.end(), [](unsigned health) { return health > 2; }))
			return false;
		if (state.zone==0 && (((state.flags & SaveDoorOpen) && !(state.flags & SaveDoorUnlocked)) ||
			((state.flags & SaveDoorUnlocked) && !state.inventory.equipment) ||
			((state.flags & SaveScoutMet) && !(state.flags & SaveWolfMission)) ||
			((state.flags & SaveFarmCleared) && (!(state.flags & SaveScoutMet) ||
				std::any_of(state.guards.begin(), state.guards.end(), [](unsigned health) { return health != 0; }))) ||
			(state.inventory.flash && !(state.flags & SaveFarmCleared))))
			return false;
		for (const auto& matrix : state.scouts)
			if (!std::all_of(matrix.begin(), matrix.end(), coordinate))
				return false;
		if(std::any_of(state.scout_health.begin(),state.scout_health.end(),[](unsigned h){return h>100;}))return false;
		for(const auto& pos:state.guard_positions)
			if(!std::all_of(pos.begin(),pos.end(),coordinate))return false;
  for(size_t i=0;i<VitaLootWorld::body_count;++i)
   if(state.loot.containers[i].kind && (i<6?state.guards[i]:state.scout_health[i-6]))return false;
  if(!std::isfinite(state.survival_seconds)||state.survival_seconds<0||state.survival_seconds>=4||
     state.route_rejoin>7||state.voice_flags>255||state.saved_at>0xffffffffu)return false;
  for(float n:state.route_metres)if(!std::isfinite(n)||n<0||n>100000)return false;
  for(unsigned n:state.voice_delay_ms)if(n>60000)return false;
  for(const auto&a:state.agents)
   if(!std::all_of(a.home.begin(),a.home.end(),coordinate)||!std::isfinite(a.yaw)||std::abs(a.yaw)>1000000||
    a.shots>1000000||a.target< -1||a.target>6||a.flags>7||a.next_shot_ms>60000||a.reload_ms>60000||a.shot_age_ms>60000)return false;
  if(state.garbage_road.flags>7)return false;
  for(unsigned i=0;i<4;++i){
   if(state.garbage_road.health[i]>(i?2u:100u))return false;
   for(float v:state.garbage_road.position[i])if(!coordinate(v))return false;
  }
  if(state.zone>=vita_zone_count||state.story>7||(state.story&&state.inventory.flash!=2))return false;
  for(const auto& memory:state.worlds)if(!memory.empty()){
   VitaSaveState decoded;uint64_t generation=0;
   if(memory.size()!=version5_bytes||!decode_record(memory,decoded,generation,true))return false;
  }
  return true;
 }

 std::vector<uint8_t> encode_base(const VitaSaveState& state, uint64_t generation)
	{
		std::vector<uint8_t> bytes;
		bytes.reserve(version5_bytes);
		const auto word = [&](uint32_t value) {
			for (unsigned shift = 0; shift < 32; shift += 8)
				bytes.push_back(static_cast<uint8_t>(value >> shift));
		};
		const auto real = [&](float value) {
			uint32_t bits;
			static_assert(sizeof(bits) == sizeof(value));
			std::memcpy(&bits, &value, sizeof(bits));
			word(bits);
		};
		word(magic); word(5); word(static_cast<uint32_t>(generation));
		word(static_cast<uint32_t>(generation >> 32)); word(version5_bytes);
		VitaInventory normalized;normalized.restore(state.inventory);
 const auto item = normalized.state();
		for (unsigned value : {item.equipment, item.equipped, item.weapon, item.loaded,
			item.reserve, item.flash, item.selected<base_item_count?item.selected:0u, state.stance})
			word(value);
		real(state.endurance);
		for (float value : state.eye) real(value);
		real(state.ground); real(state.yaw); real(state.pitch);
		word(state.health); word(state.flags);
		for (unsigned value : state.guards) word(value);
		real(state.assault_elapsed);
		for (const auto& matrix : state.scouts)
			for (float value : matrix) real(value);
		for(const auto& pos:state.guard_positions)for(float value:pos)real(value);
		for(unsigned health:state.scout_health)word(health);
  for(unsigned i=0;i<base_item_count;++i)word(item.items[i]);
  word(item.cash);word(item.outfit);word(item.food);word(item.bleeding);
  for(unsigned condition:item.outfit_conditions)word(condition);
  for(unsigned i=0;i<base_item_count;++i)word(state.trader.items[i]);word(state.trader.cash);
  for(const auto& container:state.loot.containers)
  {
   word(container.kind);word(container.cash);
   for(float value:container.position)real(value);
   for(unsigned i=0;i<base_item_count;++i)word(container.items[i]);
  }
  word(item.primary);word(item.addons);word(item.grenade_type);
  for(unsigned n:item.primary_loaded)word(n);for(unsigned n:item.ammo_modes)word(n);
  word(uint32_t(state.saved_at));word(uint32_t(state.saved_at>>32));
  word(state.play_seconds);real(state.survival_seconds);
  for(float n:state.route_metres)real(n);
  word(state.route_rejoin);word(state.voice_flags);
  for(unsigned n:state.voice_delay_ms)word(n);
  for(const auto&a:state.agents){
   for(float n:a.home)real(n);real(a.yaw);word(a.shots);word(uint32_t(a.target));
   word(a.flags|(state.world_details?4u:0u));word(a.next_shot_ms);word(a.reload_ms);word(a.shot_age_ms);
  }
  word(checksum(bytes));
		return bytes;
	}

 std::vector<uint8_t> encode(const VitaSaveState& state,uint64_t generation){
  auto bytes=encode_base(state,generation);bytes.resize(version5_bytes-4);
  const auto word=[&](uint32_t n){for(unsigned shift=0;shift<32;shift+=8)bytes.push_back(uint8_t(n>>shift));};
  bytes[4]=8;for(unsigned i=0;i<4;++i)bytes[16+i]=uint8_t(record_bytes>>(i*8));
  word(state.zone|(state.needs_landing?256u:0u));word(state.story);
  for(const auto&memory:state.worlds){
   word(memory.empty()?0:1);
   if(memory.empty())bytes.insert(bytes.end(),version5_bytes,0);
   else bytes.insert(bytes.end(),memory.begin(),memory.end());
  }
  word(state.garbage_road.flags);
  for(unsigned i=0;i<4;++i){word(state.garbage_road.health[i]);for(float f:state.garbage_road.position[i]){uint32_t n;std::memcpy(&n,&f,4);word(n);}}
  word(checksum(bytes));return bytes;
 }
 bool decode_record(const std::vector<uint8_t>& input,VitaSaveState& state,uint64_t& generation,bool memory)
 {
  const size_t count=input.size();
  if(!(count==record_bytes||count==version7_bytes||count==version6_bytes||count==version5_bytes||count==version4_bytes||count==version3_bytes||count==version2_bytes||count==legacy_bytes))return false;
  auto bytes=input;
		size_t offset = 0;
		const auto word = [&]() {
			uint32_t value = 0;
			for (unsigned shift = 0; shift < 32; shift += 8)
				value |= static_cast<uint32_t>(bytes[offset++]) << shift;
			return value;
		};
		const auto real = [&]() {
			const uint32_t bits = word();
			float value;
			std::memcpy(&value, &bits, sizeof(value));
			return value;
		};
		if (word() != magic) return false;
		const unsigned format=word();
		if(!((format==version&&count==record_bytes)||(format==7&&count==version7_bytes)||(format==6&&count==version6_bytes)||(format==5&&count==version5_bytes)||(format==4&&count==version4_bytes)||(format==3&&count==version3_bytes)||(format==2&&count==version2_bytes)||(format==1&&count==legacy_bytes)))return false;
		if(memory&&format!=5)return false;
        const uint64_t low = word(), high = word();
		generation = low | (high << 32);
		if (!generation || word() != count) return false;
		auto& item = state.inventory;
		item.equipment = word(); item.equipped = word(); item.weapon = word();
		item.loaded = word(); item.reserve = word(); item.flash = word(); item.selected = word();
		state.stance = word(); state.endurance = real();
		for (float& value : state.eye) value = real();
		state.ground = real(); state.yaw = real(); state.pitch = real();
		state.health = word(); state.flags = word();
		for (unsigned& value : state.guards) value = word();
		state.assault_elapsed = real();
		for (auto& matrix : state.scouts)
			for (float& value : matrix) value = real();
		state.guard_positions={};state.scout_health={100,100,100};state.npc_positions=format>=2;
		if(format>=2)
		{
			for(auto& pos:state.guard_positions)for(float& value:pos)value=real();
			for(auto& health:state.scout_health)health=word();
		}
  state.loot=VitaLootWorld{};state.trader=VitaTraderStock{};
  item.extended=0;
  if(format>=3)
  {
   item.items={};for(unsigned i=0;i<(format==3?12:base_item_count);++i)item.items[i]=word();
   item.cash=word();item.outfit=word();item.food=word();item.bleeding=word();
   for(auto& condition:item.outfit_conditions)condition=word();
   item.extended=1;
   state.trader.items=VitaTraderStock{}.items;for(unsigned i=0;i<(format==3?12:base_item_count);++i)state.trader.items[i]=word();state.trader.cash=word();
   for(auto& container:state.loot.containers)
   {
    container.kind=word();container.cash=word();
    for(float& value:container.position)value=real();
    for(unsigned i=0;i<(format==3?12:base_item_count);++i)container.items[i]=word();
   }
  }
  item.primary=item.addons=item.grenade_type=0;item.primary_loaded={};item.ammo_modes={};
  if(format>=4){item.primary=word();item.addons=word();item.grenade_type=word();
   for(auto&n:item.primary_loaded)n=word();for(auto&n:item.ammo_modes)n=word();}
  state.saved_at=0;state.play_seconds=0;state.survival_seconds=0;state.world_details=false;
  state.route_metres={};state.route_rejoin=state.voice_flags=0;state.voice_delay_ms={};state.agents={};
  if(format>=5){
   const uint64_t lo=word(),hi=word();state.saved_at=lo|(hi<<32);
   state.play_seconds=word();state.survival_seconds=real();
   for(float&n:state.route_metres)n=real();
   state.route_rejoin=word();state.voice_flags=word();
   for(unsigned&n:state.voice_delay_ms)n=word();
   for(auto&a:state.agents){
    for(float&n:a.home)n=real();a.yaw=real();a.shots=word();a.target=int32_t(word());
    a.flags=word();a.next_shot_ms=word();a.reload_ms=word();a.shot_age_ms=word();
   }
   state.world_details=(state.agents[0].flags&4)!=0;
  }
  state.zone=state.story=0;state.needs_landing=false;state.worlds={};
  if(format>=6){
   const unsigned zone=word();if(zone&~0x1ffu)return false;
   state.zone=zone&255;state.needs_landing=zone&256;state.story=word();
   for(unsigned i=0;i<(format>=8?vita_zone_count:3u);++i){
    auto&snapshot=state.worlds[i];
    const unsigned present=word();if(present>1)return false;
    if(present)snapshot.assign(bytes.begin()+offset,bytes.begin()+offset+version5_bytes);
    else if(std::any_of(bytes.begin()+offset,bytes.begin()+offset+version5_bytes,[](uint8_t b){return b!=0;}))return false;
    offset+=version5_bytes;
   }
  }
  state.garbage_road={};
  if(format>=7){state.garbage_road.flags=word();for(unsigned i=0;i<4;++i){state.garbage_road.health[i]=word();for(float&f:state.garbage_road.position[i])f=real();}}
  if(format>=8)item.items[23]=state.story>=7?1:0;
  if(offset+4!=count)return false;
  const uint32_t expected = word();
		bytes.resize(count - 4);
		return expected == checksum(bytes) && valid(state);
	}

 bool read_record(const std::string& path,VitaSaveState& state,uint64_t& generation){
  FILE*file=std::fopen(path.c_str(),"rb");if(!file)return false;
  std::vector<uint8_t> bytes(record_bytes);
  const size_t count=std::fread(bytes.data(),1,bytes.size(),file);
  const bool complete=std::fgetc(file)==EOF&&!std::ferror(file);
  const bool closed=std::fclose(file)==0;bytes.resize(count);
  return complete&&closed&&decode_record(bytes,state,generation);
 }

	struct Saves
	{
		VitaSaveState states[2];
		uint64_t generations[2] = {};
		bool ready[2] = {};
		int latest = -1;
		explicit Saves(const char* directory)
		{
			for (unsigned i = 0; i < 2; ++i)
			{
				ready[i] = read_record(std::string(directory) + (i ? "/save_b.bin" : "/save_a.bin"),
					states[i], generations[i]);
				if (ready[i] && (latest < 0 || generations[i] > generations[latest]))
					latest = static_cast<int>(i);
			}
		}
	};
}

bool has_vita_save(const char* directory)
{
	struct stat info;
	return stat((std::string(directory) + "/save_a.bin").c_str(), &info) == 0 ||
		stat((std::string(directory) + "/save_b.bin").c_str(), &info) == 0;
}

bool read_vita_save(const char* directory, VitaSaveState& state)
{
	const Saves saves(directory);
	if (saves.latest < 0) return false;
	state = saves.states[saves.latest];
	return true;
}

bool write_vita_save(const char* directory, const VitaSaveState& state)
{
	if (!valid(state)) return false;
	const Saves saves(directory);
	const uint64_t previous = saves.latest < 0 ? 0 : saves.generations[saves.latest];
	if (previous == std::numeric_limits<uint64_t>::max()) return false;
	const int target = saves.latest == 0 ? 1 : 0;
	const auto bytes = encode(state, previous + 1);
	if (bytes.size() != record_bytes) return false;
	const std::string path = std::string(directory) + (target ? "/save_b.bin" : "/save_a.bin");
	FILE* file = std::fopen(path.c_str(), "wb");
	if (!file) return false;
	const bool written = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
	const bool flushed = std::fflush(file) == 0;
	const bool closed = std::fclose(file) == 0;
	if (!written || !flushed || !closed) return false;
	VitaSaveState check;
	uint64_t generation = 0;
	return read_record(path, check, generation) && generation == previous + 1;
}


namespace {
 std::string slot_directory(const char* directory,unsigned slot){
  if(slot==0)return directory;
  char name[32];std::snprintf(name,sizeof(name),slot<vita_manual_slots?"/save_slot%02u":"/autosave%02u",
   slot<vita_manual_slots?slot+1:slot-vita_manual_slots+1);
  return std::string(directory)+name;
 }
}
bool read_vita_slot(const char* directory,unsigned slot,VitaSaveState& state){
 return slot<vita_save_slots&&read_vita_save(slot_directory(directory,slot).c_str(),state);
}
bool write_vita_slot(const char* directory,unsigned slot,const VitaSaveState& state){
 if(slot>=vita_save_slots)return false;
 const auto path=slot_directory(directory,slot);
 if(slot&&mkdir(path.c_str(),0777)!=0&&errno!=EEXIST)return false;
 VitaSaveState copy=state;const auto now=std::time(nullptr);
 copy.saved_at=now>0?uint64_t(now):0;
 return write_vita_save(path.c_str(),copy);
}
std::array<VitaSaveEntry,vita_save_slots> list_vita_saves(const char* directory){
 std::array<VitaSaveEntry,vita_save_slots> entries{};
 for(unsigned i=0;i<entries.size();++i){
  auto&e=entries[i];const auto path=slot_directory(directory,i);
  e.exists=has_vita_save(path.c_str());VitaSaveState state;e.valid=read_vita_save(path.c_str(),state);
  if(!e.valid)continue;
  e.saved_at=state.saved_at;e.play_seconds=state.play_seconds;e.flags=state.flags;e.flash=state.inventory.flash;e.zone=state.zone;e.story=state.story;
  if(!e.saved_at){struct stat info{};for(const char*name:{"/save_a.bin","/save_b.bin"})
   if(!stat((path+name).c_str(),&info))e.saved_at=std::max(e.saved_at,uint64_t(std::max(time_t(0),info.st_mtime)));}
 }
 return entries;
}
bool delete_vita_slot(const char* directory,unsigned slot){
 if(slot>=vita_save_slots)return false;
 const auto path=slot_directory(directory,slot);bool ok=true;
 for(const char*name:{"/save_a.bin","/save_b.bin"})if(unlink((path+name).c_str())!=0&&errno!=ENOENT)ok=false;
 return ok&&!has_vita_save(path.c_str());
}
int latest_vita_slot(const char* directory){
 const auto entries=list_vita_saves(directory);int selected=-1;
 for(unsigned i=0;i<entries.size();++i)if(entries[i].valid&&
  (selected<0||entries[i].saved_at>entries[selected].saved_at||
   (entries[i].saved_at==entries[selected].saved_at&&entries[i].play_seconds>entries[selected].play_seconds)))selected=int(i);
 return selected;
}
unsigned next_vita_auto_slot(const char* directory){
 const auto entries=list_vita_saves(directory);unsigned selected=vita_manual_slots;
 for(unsigned i=vita_manual_slots;i<entries.size();++i){
  if(!entries[i].valid)return i;
  if(entries[i].saved_at<entries[selected].saved_at||
    (entries[i].saved_at==entries[selected].saved_at&&entries[i].play_seconds<entries[selected].play_seconds))selected=i;
 }
 return selected;
}

bool remember_vita_world(VitaSaveState& state){
 if(!valid(state))return false;
 auto local=state;local.worlds={};local.zone=local.story=0;local.needs_landing=false;
 // The USB belongs to the Cordon quest. Other maps have independent combat flags.
 if(state.zone){local.inventory.flash=0;local.inventory.items[3]=0;}
 state.worlds[state.zone]=encode_base(local,1);return true;
}
bool change_vita_zone(const VitaSaveState& source,const VitaZoneExit& exit,VitaSaveState& target){
 if(exit.from!=source.zone||exit.to>=vita_zone_count||!vita_exit_allowed(exit,source.story))return false;
 VitaSaveState from=source;if(!remember_vita_world(from))return false;
 VitaSaveState next;
 if(!from.worlds[exit.to].empty()){
  uint64_t gen=0;if(!decode_record(from.worlds[exit.to],next,gen,true))return false;
 }else{
  if(exit.to==0)return false; // Cordon is always visited before the other maps.
  next.flags=SaveSurface|SaveDoorUnlocked|SaveDoorOpen|SaveTraderConfirmed|SaveWolfMission|
   SaveScoutMet|SaveGroup|SaveScoutsArrived|SaveTraderIntro;
  if(exit.to==3){next.flags&=~SaveGroup;next.scout_health.fill(0);}
  next.guards.fill(2);next.npc_positions=true;
  next.guard_positions=vita_campaign_zones[exit.to].enemies;
  for(unsigned i=0;i<3;++i){
   next.scouts[i]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
   for(unsigned j=0;j<3;++j)next.scouts[i][12+j]=vita_campaign_zones[exit.to].allies[i][j];
  }
 }
 next.garbage_road=source.garbage_road;
 next.inventory=source.inventory;next.trader=source.trader;next.health=source.health;
 next.endurance=source.endurance;next.stance=0;next.survival_seconds=source.survival_seconds;
 next.play_seconds=source.play_seconds;next.saved_at=source.saved_at;
 next.zone=exit.to;next.story=source.story;next.worlds=from.worlds;next.needs_landing=true;
 next.ground=exit.arrival[1];next.eye=exit.arrival;next.eye[1]+=1.7f;
 next.yaw=exit.yaw;next.pitch=1.45f;
 if(!valid(next))return false;target=std::move(next);return true;
}
