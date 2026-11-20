#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

constexpr unsigned vita_trader_idle_fps = 15;
constexpr char vita_trader_idle_clip[] = "idle_2";

struct VitaTraderVertex
{
	float position[3];
	float uv[2];
};

// animation_positions contains consecutive XYZ streams. target is the head in frame zero.
bool read_vita_trader_model(const void* model, size_t model_size, const void* motion, size_t motion_size,
	std::vector<VitaTraderVertex>& vertices, std::vector<uint16_t>& indices, std::string& texture,
	float target[3], std::vector<float>& animation_positions);

// Reads the original trader door's single-bone progressive mesh in its closed pose.
bool read_vita_trader_door(const void* model, size_t model_size, std::vector<VitaTraderVertex>& vertices,
	std::vector<uint16_t>& indices, std::string& texture);

constexpr char vita_stalker_idle_clip[] = "norm_idle_1_";
constexpr char vita_stalker_walk_clip[] = "norm_walk_fwd_1";
constexpr unsigned vita_stalker_walk_fps = 15;

struct VitaStalkerMesh
{
	std::vector<VitaTraderVertex> vertices;
	std::vector<uint16_t> indices;
	std::string texture;
	std::vector<float> walk_positions;
};

struct VitaStalkerWeaponPose
{
	std::array<float, 16> idle{};
	std::vector<std::array<float, 16>> walk;
	bool ready = false;
};

enum class VitaNpcClip { Idle, Alternate, Aim, Fire, Walk, Reload, Dead, Count };
struct VitaNpcPoseSample { size_t first=0,next=0; float blend=0.f; };
struct VitaNpcRange
{
	size_t first = 0, count = 0;
	float fps = 15.f;
	VitaNpcPoseSample sample(float seconds,bool loop=true)const
	{
		if(!count)return {};
		const float time=seconds>0.f?seconds*fps:0.f;
		const size_t tick=static_cast<size_t>(time),frame=loop?tick%count:tick<count?tick:count-1;
		const size_t next=frame+1<count?frame+1:loop?0:frame;
		return {first+frame,first+next,!loop&&tick>=count?0.f:time-float(tick)};
	}

	size_t frame(float seconds, bool loop = true) const
	{
		if (!count) return 0;
		const size_t tick = static_cast<size_t>(seconds > 0.f ? seconds * fps : 0.f);
		return first + (loop ? tick % count : tick < count ? tick : count - 1);
	}
};
using VitaNpcRanges = std::array<VitaNpcRange, static_cast<size_t>(VitaNpcClip::Count)>;
using VitaNpcMotions = std::array<std::vector<uint8_t>, 6>;
bool read_vita_npc_animations(const void* model, size_t model_size, const std::vector<uint8_t>& params,
	const std::vector<uint8_t>& standing, const std::vector<uint8_t>& walking, const VitaNpcMotions& motions,
	VitaStalkerMesh (&parts)[2], VitaStalkerWeaponPose& pose, VitaNpcRanges& ranges);

bool read_vita_weapon_world(const void* model, size_t model_size, std::vector<VitaStalkerMesh>& parts);

bool read_vita_stalker_model(const void* model, size_t model_size, const std::vector<uint8_t>& params,
	const std::vector<uint8_t>& keys, VitaStalkerMesh (&parts)[2], float target[3], bool& posed, const std::vector<uint8_t>* walk_keys = nullptr, VitaStalkerWeaponPose* weapon_pose = nullptr);

constexpr unsigned vita_weapon_animation_fps = 30;

enum class VitaWeaponClip { Idle, Aim, Reload, ReloadEmpty, KnifeWindup, KnifeStrike, Shoot, ShootLast, Count };

struct VitaWeaponClipRange
{
	size_t first = 0;
	unsigned source_frames = 0;
	unsigned step = 1;

	uint64_t duration_us() const { return uint64_t(source_frames) * 1000000 / vita_weapon_animation_fps; }

	size_t frame_at(uint64_t elapsed_us) const
	{
		if (!source_frames)
			return 0;
		const uint64_t duration = duration_us();
		const uint64_t source_frame = elapsed_us >= duration ? source_frames - 1 : elapsed_us * vita_weapon_animation_fps / 1000000;
		return first + source_frame / step;
	}
};

using VitaWeaponClips = std::array<VitaWeaponClipRange, static_cast<size_t>(VitaWeaponClip::Count)>;

struct VitaWeaponHudMesh
{
	std::vector<VitaTraderVertex> vertices;
	std::vector<uint16_t> indices;
	std::string texture;
	std::vector<float> positions;
    std::vector<uint8_t> triangle_addons;
};

bool read_vita_weapon_hud(const void* model, size_t model_size, bool pistol,
	std::vector<VitaWeaponHudMesh>& parts, VitaWeaponClips& clips, unsigned kind=0, bool include_addons=false);
