#pragma once

#include "vita_inventory.h"
#include "vita_loot_trade.h"
#include <array>
#include <cstdint>
#include <vector>
#include "vita_campaign.h"
#include "vita_garbage_road.h"

enum VitaSaveFlag : uint32_t
{
	SaveBunker = 1u << 0, SaveSurface = 1u << 1, SaveDoorUnlocked = 1u << 2,
	SaveDoorOpen = 1u << 3, SaveTraderConfirmed = 1u << 4, SaveWolfMission = 1u << 5,
	SaveScoutMet = 1u << 6, SaveFarmCleared = 1u << 7, SaveGroup = 1u << 8,
	SaveFightStarted = 1u << 9, SaveScoutsArrived = 1u << 10, SaveTraderIntro = 1u << 11
};

// Relative combat clocks survive process restarts and menu pauses.
struct VitaSavedAgent
{
 std::array<float,3> home{};
 float yaw=0;
 unsigned shots=0;
 int target=-1;
 unsigned flags=0, next_shot_ms=0, reload_ms=0, shot_age_ms=60000;
};
struct VitaSaveState
{
	VitaInventory::State inventory{};
	unsigned stance = 0;
	float endurance = 1.f;
	std::array<float, 3> eye{};
	float ground = 0.f, yaw = 0.f, pitch = 1.45f;
	unsigned health = 100, flags = 0;
	std::array<unsigned, 6> guards{};
	float assault_elapsed = 0.f;
	std::array<std::array<float, 16>, 3> scouts{};
	std::array<std::array<float,3>,6> guard_positions{};
	std::array<unsigned,3> scout_health{100,100,100};
	bool npc_positions = true;
 uint64_t saved_at=0;
 unsigned play_seconds=0;
 float survival_seconds=0;
 bool world_details=false;
 std::array<float,3> route_metres{};
 unsigned route_rejoin=0, voice_flags=0;
 std::array<unsigned,2> voice_delay_ms{};
 std::array<VitaSavedAgent,9> agents{};
 VitaGarbageRoadState garbage_road;
 unsigned zone=0,story=0;
 bool needs_landing=false;
 // Complete v5 snapshots, heap-backed so ordinary frame state stays small.
 std::array<std::vector<uint8_t>,vita_zone_count> worlds{};
 VitaLootWorld loot;
 VitaTraderStock trader;
};

// Alternate two checksummed generations so interrupted writes preserve the previous save.
bool has_vita_save(const char* directory);
bool read_vita_save(const char* directory, VitaSaveState& state);
bool write_vita_save(const char* directory, const VitaSaveState& state);


// Slot zero retains the original save_a/save_b pair. Autosaves never touch manual slots.
constexpr unsigned vita_manual_slots=6, vita_auto_slots=3, vita_save_slots=9;
struct VitaSaveEntry
{
 bool exists=false, valid=false;
 uint64_t saved_at=0;
 unsigned play_seconds=0, flags=0, flash=0, zone=0,story=0;
};
inline bool vita_save_entry_equal(const VitaSaveEntry&a,const VitaSaveEntry&b){
 return a.exists==b.exists&&a.valid==b.valid&&a.saved_at==b.saved_at&&a.play_seconds==b.play_seconds&&
  a.flags==b.flags&&a.flash==b.flash&&a.zone==b.zone&&a.story==b.story;
}
std::array<VitaSaveEntry,vita_save_slots> list_vita_saves(const char* directory);
bool read_vita_slot(const char* directory,unsigned slot,VitaSaveState& state);
bool write_vita_slot(const char* directory,unsigned slot,const VitaSaveState& state);
bool delete_vita_slot(const char* directory,unsigned slot);
int latest_vita_slot(const char* directory);
unsigned next_vita_auto_slot(const char* directory);

// Transition snapshots are part of every manual/automatic save.
bool remember_vita_world(VitaSaveState& state);
bool change_vita_zone(const VitaSaveState& source,const VitaZoneExit& exit,VitaSaveState& target);
